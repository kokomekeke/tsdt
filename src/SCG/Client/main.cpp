#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <atomic>
#include <csignal>
#include <type_traits>
#include <variant>
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"
#include "MqttClient.hpp"
#include "MessageParser.hpp"

namespace {
    std::atomic<bool> g_running{true};

    void HandleSignal(int /*signal*/) {
        g_running.store(false);
    }
}

using namespace Sagax;
using namespace Sagax::SCG;

int main() {
    Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
    Common::Logger::AddStream(&logStream);
    Common::Defer disposeLogger([&](){
        Common::Logger::RemoveStream(&logStream);
    });

    MqttClient::LibraryInit();

    MqttClient client;

    MqttClient::Configuration cfg;
    cfg.host = "localhost";
    cfg.port = 1883;
    cfg.keepAlive_s = 60;
    cfg.subscribeTopics = { "test/topic" };
    cfg.clientId = "RSG";
    cfg.reconnectDelays_ms = { 1000, 2000, 5000, 10000, 30000 };
    cfg.maxQueueSize = 200;

    cfg.onUpdate = [&client]() {
        MqttClient::Message msg;
        Sagax::SCG::CommandMessageParser parser;
        while(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            try {
                auto cm = parser.Parse(payload);
                if (std::holds_alternative<CommandMessage::TestStart>(cm.variant)) {
                    auto &v = std::get<CommandMessage::TestStart>(cm.variant);
                    Common::Logger::Info("received [{}]: TestStart - scenario={} vehicle={} customer={}", msg.topic, v.scenario, v.vehicle, v.customer);
                }
                else if (std::holds_alternative<CommandMessage::TestEnd>(cm.variant)) {
                    Common::Logger::Info("received [{}]: TestEnd", msg.topic);
                }
                else if (std::holds_alternative<CommandMessage::TestUpdate>(cm.variant)) {
                    Common::Logger::Info("received [{}]: TestUpdate", msg.topic);
                }
                else if (std::holds_alternative<CommandMessage::IntStart>(cm.variant)) {
                    auto &v = std::get<CommandMessage::IntStart>(cm.variant);
                    Common::Logger::Info("received [{}]: IntStart - profile={}", msg.topic, v.profile);
                }
                else if (std::holds_alternative<CommandMessage::IntStop>(cm.variant)) {
                    Common::Logger::Info("received [{}]: IntStop", msg.topic);
                }
            } catch (const std::exception &e) {
                Common::Logger::Error("received [{}]: failed to parse payload: {} payload='{}'", msg.topic, e.what(), payload);
            }
        }
    };

    client.Configure(std::move(cfg));
    client.Start();

    std::signal(SIGINT, HandleSignal);
    Common::Logger::Info("waiting for Ctrl+C to stop...");
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    client.Stop();
    MqttClient::LibraryCleanup();
    return 0;
}
