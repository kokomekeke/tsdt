#include <iostream>
#include <thread>
#include <chrono>
#include <string>

#include "MqttClient.hpp"

using namespace Sagax::SCG;

int main() {
    // initialize library once for the process
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
        while(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            std::cout << "received [" << msg.topic << "]: " << payload << std::endl;
        }
    };

    client.Configure(std::move(cfg));
    client.Start();

    MqttClient::Message out;
    out.topic = "test/topic";
    const std::string text = "hello from MqttClient example";
    out.bytes.assign(text.begin(), text.end());
    out.qos = 0;
    out.retain = false;
    client.Push(std::move(out));

    // run for a short while to allow messages to be exchanged
    std::this_thread::sleep_for(std::chrono::seconds(10));

    client.Stop();
    MqttClient::LibraryCleanup();
    return 0;
}
