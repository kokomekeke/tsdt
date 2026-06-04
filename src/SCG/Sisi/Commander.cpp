#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

#include "SSG/argparse/argparse.hpp"
#include "Common/Defer.hpp"
#include "Common/Logger.hpp"
#include "Messages.hpp"
#include "MqttClient.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/ServerConfig.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

struct Parameters {
    bool success;
    std::string error;
    std::string configPath;
};

struct ProgramParser {
    Parameters params;
    argparse::ArgumentParser program;

    ProgramParser() : program("scgcommander.exe", "1.0", argparse::default_arguments::all) {
        program.add_argument("config_path")
            .help("path to configuration .json");
    }

    std::string Help() {
        return program.help().str();
    }

    void Parse(int argc, char **argv) {
        try {
            program.parse_args(argc, argv);
        }
        catch(const std::exception &e) {
            params.success = false;
            params.error = e.what();
            return;
        }

        params.configPath = program.get<std::string>("config_path");
        params.success = true;
    }
};

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

static void DrainIncomingMessages(MqttClient &client, std::atomic<bool> &running) {
    while(true) {
        MqttClient::Message msg;
        if(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            Common::Logger::Info("received [{}]: {}", msg.topic, payload);
            continue;
        }

        if(!running.load() && !client.IsRunning()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char **argv) {
    Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
    Common::Logger::AddStream(&logStream);
    Common::Defer disposeLogger([&](){
        Common::Logger::RemoveStream(&logStream);
    });

    ProgramParser parser;
    parser.Parse(argc, argv);
    Parameters params = parser.params;
    if(!params.success) {
        Common::Logger::Error("\nfailed to parse parameters:\n{}", params.error.c_str());
        Common::Logger::Info("\n{}", parser.Help().c_str());
        return 1;
    }

    std::ifstream file(params.configPath);
    if(!file.is_open()) {
        Common::Logger::Error("failed to open config file: {}", params.configPath);
        return 1;
    }

    std::stringstream buf;
    buf << file.rdbuf();

    ServerConfig config;
    try {
        config = ParseServerConfig(buf.str());
    }
    catch(const std::exception &e) {
        Common::Logger::Error("\nfailed to parse config:\n{}", e.what());
        return 1;
    }

    MqttClient::LibraryInit();
    MqttClient client;

    MqttClient::Configuration cfg;
    cfg.host = config.mqtt.brokerHost;
    cfg.port = config.mqtt.brokerPort;
    cfg.keepAlive_s = 60;
    cfg.subscribeTopics = { config.mqtt.logTopic };
    cfg.clientId = "Commander";
    cfg.reconnectDelays_ms = { 1000, 2000, 5000 };
    cfg.maxQueueSize = 100;
    cfg.onUpdate = []() {};

    client.Configure(std::move(cfg));
    client.Start();
    if(!client.IsRunning()) {
        Common::Logger::Error("failed to start MQTT client");
        MqttClient::LibraryCleanup();
        return 1;
    }

    std::atomic<bool> running{true};
    std::thread logThread([&]() {
        DrainIncomingMessages(client, running);
    });

    PrintHelp();
    Common::Logger::Info("connected command topic: {}", config.mqtt.commandTopic);
    Common::Logger::Info("listening log topic: {}", config.mqtt.logTopic);

    std::string line;
    while(running.load()) {
        // std::cout << "> " << std::flush;
        if(!std::getline(std::cin, line)) {
            running.store(false);
            break;
        }

        std::istringstream iss(line);
        std::string cmd;
        if(!(iss >> cmd)) {
            continue;
        }
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return char(std::tolower(c)); });

        if(cmd == "quit" || cmd == "exit") {
            running.store(false);
            break;
        }
        if(cmd == "help") {
            PrintHelp();
            continue;
        }

        CommandMessage cm;
        bool make = true;

        if(cmd == "teststart") {
            std::string scenario, vehicle, customer;
            iss >> scenario >> vehicle >> customer;
            cm.variant = CommandMessage::TestStart{scenario, vehicle, customer};
        }
        else if(cmd == "testend") {
            cm.variant = CommandMessage::TestEnd{};
        }
        else if(cmd == "testupdate") {
            cm.variant = CommandMessage::TestUpdate{};
        }
        else if(cmd == "intstart") {
            std::string profile;
            iss >> profile;
            cm.variant = CommandMessage::IntStart{profile};
        }
        else if(cmd == "intstop") {
            cm.variant = CommandMessage::IntStop{};
        }
        else {
            Common::Logger::Warn("unknown command: {}", cmd);
            make = false;
        }

        if(!make) {
            continue;
        }

        std::string payload = Sagax::SCG::SerializeCommandMessage(cm);
        MqttClient::Message out;
        out.topic = config.mqtt.commandTopic;
        out.bytes.assign(payload.begin(), payload.end());
        out.qos = 0;
        out.retain = false;
        client.Push(std::move(out));

        Common::Logger::Info("sent: {}", payload);
    }

    if(client.IsRunning()) {
        client.Stop();
    }
    if(logThread.joinable()) {
        logThread.join();
    }
    MqttClient::LibraryCleanup();
    return 0;
}
