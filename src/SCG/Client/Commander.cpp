#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"
#include "MqttClient.hpp"
#include "MessageParser.hpp"
#include "Messages.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

static void PrintHelp() {
    Common::Logger::Info("commands:");
    Common::Logger::Info("  teststart <scenario> <vehicle> <customer>");
    Common::Logger::Info("  testend");
    Common::Logger::Info("  testupdate");
    Common::Logger::Info("  intstart <profile>");
    Common::Logger::Info("  intstop");
    Common::Logger::Info("  help");
    Common::Logger::Info("  quit\n");
}

int main(int argc, char **argv) {
    Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
    Common::Logger::AddStream(&logStream);
    Common::Defer disposeLogger([&](){
        Common::Logger::RemoveStream(&logStream);
    });

    std::string host = "localhost";
    std::string topic = "test/topic";
    int port = 1883;

    if (argc > 1) host = argv[1];
    if (argc > 2) topic = argv[2];
    if (argc > 3) {
        try { port = std::stoi(argv[3]); } catch(...){}
    }

    MqttClient::LibraryInit();
    MqttClient client;

    MqttClient::Configuration cfg;
    cfg.host = host;
    cfg.port = port;
    cfg.keepAlive_s = 60;
    cfg.subscribeTopics = { topic };
    cfg.clientId = "Commander";
    cfg.reconnectDelays_ms = {1000,2000,5000};
    cfg.maxQueueSize = 100;
    cfg.onUpdate = [](){};

    client.Configure(std::move(cfg));
    client.Start();
    PrintHelp();

    std::this_thread::sleep_for(std::chrono::seconds(1));

    CommandMessageParser ser;

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        std::istringstream iss(line);
        std::string cmd;
        if (!(iss >> cmd)) continue;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c){ return char(std::tolower(c)); });

        if (cmd == "quit" || cmd == "exit") break;
        if (cmd == "help") { PrintHelp(); continue; }

        CommandMessage cm;
        bool make = true;

        if (cmd == "teststart") {
            std::string scenario, vehicle, customer;
            iss >> scenario >> vehicle >> customer;
            cm.variant = CommandMessage::TestStart{scenario, vehicle, customer};
        } else if (cmd == "testend") {
            cm.variant = CommandMessage::TestEnd{};
        } else if (cmd == "testupdate") {
            cm.variant = CommandMessage::TestUpdate{};
        } else if (cmd == "intstart") {
            std::string profile; iss >> profile;
            cm.variant = CommandMessage::IntStart{profile};
        } else if (cmd == "intstop") {
            cm.variant = CommandMessage::IntStop{};
        } else {
            Common::Logger::Warn("unknown command: {}", cmd);
            make = false;
        }

        if (!make) continue;

        std::string payload = ser.Serialize(cm);
        MqttClient::Message out;
        out.topic = topic;
        out.bytes.assign(payload.begin(), payload.end());
        out.qos = 0;
        out.retain = false;
        client.Push(std::move(out));

        Common::Logger::Info("sent: {}", payload);
    }

    client.Stop();
    MqttClient::LibraryCleanup();
    return 0;
}
