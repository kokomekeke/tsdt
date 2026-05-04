#pragma once
#include <memory>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <variant>

namespace Sagax::SCG {

    struct MqttClient {
        struct Impl;
        std::unique_ptr<Impl> impl;

        struct Message {
            std::string topic;
            std::vector<std::uint8_t> bytes;
            std::uint8_t qos = 2;
            bool retain = false;
        };

        using UpdateCallback = std::function<void()>;

        struct Configuration {
            std::string host;
            std::int32_t port;
            std::int32_t keepAlive_s;
            std::vector<std::string> subscribeTopics;
            std::string clientId;
            std::vector<std::int32_t> reconnectDelays_ms;
            std::int32_t maxQueueSize;
            UpdateCallback onUpdate;
        };

        static void LibraryInit();
        static void LibraryCleanup();

        MqttClient();
        ~MqttClient();

        void Configure(Configuration config);
        void Start();
        void Stop();
        bool IsRunning();
        void Push(Message message);
        bool Pop(Message &message);
    };

}


