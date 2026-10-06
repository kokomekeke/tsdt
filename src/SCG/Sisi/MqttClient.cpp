#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <format>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <utility>
#include <mosquitto.h>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "MqttClient.hpp"

namespace Sagax::SCG {
    namespace {
        std::atomic<bool> g_libraryInitialized{false};

        struct MessageQueue {
            std::deque<MqttClient::Message> queue;

            std::int32_t Count() {
                return std::int32_t(queue.size());
            }

            void Push(MqttClient::Message message) {
                queue.push_back(std::move(message));
            }

            bool Pop(MqttClient::Message &message) {
                if(queue.empty()) {
                    return false;
                }
                message = std::move(queue.front());
                queue.pop_front();
                return true;
            }

            void Drop() {
                queue.pop_front();
            }

        };

        // a payload is bytes, in practice json. the size is always written, and one
        // longer than this is left out so that a traced message stays a readable line
        constexpr std::size_t logPayloadLimit = 256;

        std::string DescribePayload(const std::vector<std::uint8_t> &bytes) {
            if(bytes.size() > logPayloadLimit) {
                return "[hidden]";
            }
            return std::string(bytes.begin(), bytes.end());
        }

        void LogMessage(const char *direction, const MqttClient::Message &message) {
            Common::Logger::Trace(
                "mqtt {} [{}] qos {} retain {} ({} bytes): {}",
                direction,
                message.topic,
                std::int32_t(message.qos),
                std::int32_t(message.retain),
                message.bytes.size(),
                DescribePayload(message.bytes)
            );
        }
    }

    void MqttClient::LibraryInit() {
        if(!g_libraryInitialized.exchange(true)) {
            mosquitto_lib_init();
        }
    }

    void MqttClient::LibraryCleanup() {
        if(g_libraryInitialized.exchange(false)) {
            mosquitto_lib_cleanup();
        }
    }

    // bool MqttClient::LibraryInitialized() {
    //     return g_libraryInitialized.load();
    // }

    struct MqttClient::Impl {
        bool configValid = false;
        MqttClient::Configuration config;
        MessageQueue messagesIn;
        MessageQueue messagesOut;
        std::mutex mtxIn;
        std::mutex mtxOut;
        mosquitto *mosq = nullptr;
        std::thread loopThread;
        std::atomic<bool> running{false};

        Impl() {
        }

        static void OnMessage_dispatch(struct mosquitto *, void *userdata, const struct mosquitto_message *msg) {
            auto *self = static_cast<Impl*>(userdata);
            if(self == nullptr) {
                return;
            }
            self->OnMessage(msg);
        }

        void OnMessage(const struct mosquitto_message *msg) {
            if(msg == nullptr || msg->payloadlen < 0) {
                return;
            }

            MqttClient::Message message;
            if(msg->topic != nullptr) {
                message.topic = msg->topic;
            }
            const auto payloadLen = static_cast<std::size_t>(msg->payloadlen);
            message.bytes.resize(payloadLen);
            message.qos = static_cast<std::uint8_t>(msg->qos);
            message.retain = msg->retain;
            if(payloadLen > 0 && msg->payload != nullptr) {
                std::memcpy(message.bytes.data(), msg->payload, payloadLen);
            }

            if(config.logMessages) {
                LogMessage("in", message);
            }

            {
                std::unique_lock lck(mtxIn);
                while(messagesIn.Count() >= config.maxQueueSize) {
                    Common::Logger::Warn("input queue overflow");
                    messagesIn.Drop();
                }
                messagesIn.Push(std::move(message));
            }

            if(config.onUpdate) {
                config.onUpdate();
            }
        }

        bool Connect() {
            const int connectResult = mosquitto_connect(mosq, config.host.c_str(), static_cast<int>(config.port), static_cast<int>(config.keepAlive_s));
            if(connectResult != MOSQ_ERR_SUCCESS) {
                Common::Logger::Error("failed to connect to {}:{}: {}", config.host, config.port, mosquitto_strerror(connectResult));
                return false;
            }
            else {
                Common::Logger::Info("connected to {}:{}", config.host, config.port);
            }

            for(const auto &topic : config.subscribeTopics) {
                const int subscribeResult = mosquitto_subscribe(mosq, nullptr, topic.c_str(), 0);
                if(subscribeResult != MOSQ_ERR_SUCCESS) {
                    Common::Logger::Error("failed to subscribe to {}: {}", topic, mosquitto_strerror(subscribeResult));
                    return false;
                }
                else {
                    Common::Logger::Info("subscribed to [{}]", topic.c_str());
                }
            }

            return true;
        }

        void Publish() {
            MqttClient::Message message;
            while(true) {
                std::unique_lock lck(mtxOut);
                if(!messagesOut.Pop(message)) {
                    break;
                }
                lck.unlock();

                Common::Assert(!message.topic.empty(), "outgoing MQTT message topic must not be empty");

                const int publishResult = mosquitto_publish(
                    mosq,
                    nullptr,
                    message.topic.c_str(),
                    static_cast<int>(message.bytes.size()),
                    message.bytes.data(),
                    message.qos,
                    message.retain
                );

                if(publishResult != MOSQ_ERR_SUCCESS) {
                    Common::Logger::Error("failed to publish to {}: {}", message.topic, mosquitto_strerror(publishResult));
                    continue;
                }

                if(config.logMessages) {
                    LogMessage("out", message);
                }

                if(config.onUpdate) {
                    config.onUpdate();
                }
            }
        }

        bool Pop(MqttClient::Message &message) {
            std::unique_lock lck(mtxIn);
            return messagesIn.Pop(message);
        }

        void Push(MqttClient::Message message) {
            std::unique_lock lck(mtxOut);
            while(messagesOut.Count() >= config.maxQueueSize) {
                Common::Logger::Warn("output queue overflow");
                messagesOut.Drop();
            }
            messagesOut.Push(std::move(message));
        }

        void RunLoop() {
            std::size_t attempt = 0;
            while(true) {
                if(!running.load()) {
                    goto runEnd;
                }

                if(!Connect()) {
                    std::int32_t delay = config.reconnectDelays_ms[std::min(attempt, config.reconnectDelays_ms.size() - 1)];
                    Common::Logger::Warn("mqtt connect failed, retrying in {} ms", delay);
                    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<std::int64_t>(delay)));
                    ++attempt;
                    continue;
                }
                attempt = 0;

                while(true) {
                    if(!running.load()) {
                        goto runEnd;
                    }
                    Publish();

                    const int loopResult = mosquitto_loop(mosq, 100, 1);
                    if(loopResult == MOSQ_ERR_SUCCESS) {
                        continue;
                    }
                    else if(loopResult == MOSQ_ERR_NO_CONN) {
                        Common::Logger::Warn("mqtt disconnected: {}", mosquitto_strerror(loopResult));
                        break;
                    }
                    else {
                        Common::Logger::Warn("mosquitto loop error: {}", mosquitto_strerror(loopResult));
                        break;
                    }
                }
            }

            runEnd:
            mosquitto_disconnect(mosq);
        }

        void Configure(MqttClient::Configuration config_) {
            configValid = false;

            Common::Assert(!running.load(), "MqttClient::Configure called while client is running");
            Common::Assert(!config_.host.empty(), "MqttClient config host must not be empty");
            Common::Assert(config_.port > 0, "MqttClient config port must be positive");
            Common::Assert(config_.keepAlive_s > 0, "MqttClient config keepAlive_s must be positive");
            Common::Assert(!config_.subscribeTopics.empty(), "MqttClient config subscribeTopics must not be empty");
            for(const auto &topic : config_.subscribeTopics) {
                Common::Assert(!topic.empty(), "MqttClient config subscribeTopics contains an empty topic");
            }
            Common::Assert(!config_.reconnectDelays_ms.empty(), "MqttClient config reconnectDelays_ms must not be empty");
            for(const auto delay : config_.reconnectDelays_ms) {
                Common::Assert(delay >= 0, "MqttClient config reconnectDelays_ms must be >= 0");
            }
            Common::Assert(config_.maxQueueSize > 0, "MqttClient config maxQueueSize must be positive");

            configValid = true;
            config = std::move(config_);
        }

        void Start() {
            Common::Assert(!running.load(), "MqttClient::Start called while already running");
            Common::Assert(configValid, "MqttClient config invalid");

            Cleanup();

            const char *clientId = config.clientId.empty() ? nullptr : config.clientId.c_str();
            mosq = mosquitto_new(clientId, true, this);
            if(mosq == nullptr) {
                Common::Logger::Error("failed to create the mosquitto client");
                Cleanup();
                return;
            }
            mosquitto_message_callback_set(mosq, &Impl::OnMessage_dispatch);

            running.store(true);
            // the loop is the whole body of the thread, an exception leaving it would end
            // the process. the client stops instead and IsRunning() reports it
            loopThread = std::thread([this]() {
                try {
                    RunLoop();
                }
                catch(const std::exception &e) {
                    Common::Logger::Error("mqtt loop stopped: {}", e.what());
                }
                running.store(false);
            });
        }

        // a client that never started and one whose loop stopped on its own both end up
        // here, so this does not require the client to be running
        void Stop() {
            Cleanup();
        }

        bool IsRunning() {
            return running.load();
        }

        void Cleanup() {
            running.store(false);
            if(loopThread.joinable()) {
                loopThread.join();
            }

            if(mosq != nullptr) {
                mosquitto_destroy(mosq);
                mosq = nullptr;
            }
        }

        ~Impl() {
            Cleanup();
        }
    };

    MqttClient::MqttClient() : impl(std::make_unique<Impl>()) {
    }

    MqttClient::~MqttClient() {
    }

    void MqttClient::Configure(Configuration config) {
        impl->Configure(config);
    }

    void MqttClient::Start() {
        impl->Start();
    }

    void MqttClient::Stop() {
        impl->Stop();
    }

    bool MqttClient::IsRunning() {
        return impl->IsRunning();
    }

    void MqttClient::Push(Message message) {
        impl->Push(std::move(message));
    }

    bool MqttClient::Pop(Message &message) {
        return impl->Pop(message);
    }
}
