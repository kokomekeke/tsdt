#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <csignal>
#include <type_traits>
#include <variant>
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"
#include "Pipeline/PpController.hpp"
#include "SCG/JsonParsing/Parsers.hpp"
#include "SCG/JsonParsing/OutputParserRegistry.hpp"
#include "SCG/JsonParsing/SignalParserRegistry.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/ScenarioContext.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
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

class MainApp {
public:
    int Main() {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){ Common::Logger::RemoveStream(&logStream); });

        MqttClient::LibraryInit();

        MqttClient::Configuration cfg;
        cfg.host = "localhost";
        cfg.port = 1883;
        cfg.keepAlive_s = 60;
        cfg.subscribeTopics = { "test/topic" };
        cfg.clientId = "RSG";
        cfg.reconnectDelays_ms = { 1000, 2000, 5000, 10000, 30000 };
        cfg.maxQueueSize = 200;

        // configure update handler
        cfg.onUpdate = [this]() { this->OnUpdate(); };

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

private:
    // scenario controller members (moved out of nested struct)
    Parser::Registry<SCG::OutputParser> outputRegistry = SCG::GetOutputParserRegistry();
    Parser::Registry<SCG::SignalParser> signalRegistry = SCG::GetSignalParserRegistry();
    SCG::ScenarioContext ctx;
    bool running = false;

    bool StartScenario(const std::string &profile) {
        if (running) StopScenario();

        std::filesystem::path p(profile);
        if (!std::filesystem::exists(p)) p = std::filesystem::path("scenarios") / (profile + std::string(".json"));
        if (!std::filesystem::exists(p)) {
            Common::Logger::Error("scenario file not found for profile: {}", profile);
            return false;
        }

        std::ifstream file(p);
        if (!file.is_open()) {
            Common::Logger::Error("failed to open scenario file: {}", p.string());
            return false;
        }
        std::stringstream buf; buf << file.rdbuf();
        std::string jsonStr = buf.str();

        try {
            ctx.scenario = SCG::ParseScenario(jsonStr, p.stem().string(), signalRegistry, outputRegistry);
        } catch (const std::exception &e) {
            Common::Logger::Error("could not parse scenario: {}", e.what());
            return false;
        }

        auto problems = SCG::ValidateScenario(ctx.scenario);
        for (auto &w : problems.warnings) Common::Logger::Warn("{}", w);
        if (!problems.errors.empty()) {
            for (auto &er : problems.errors) Common::Logger::Error("{}", er);
            return false;
        }

        try {
            auto b = SCG::BuildStreamingPipeline(ctx.scenario);
            ctx.pipeline = b.builder.Build();
            ctx.resources = b.resources;
        } catch (const std::exception &e) {
            Common::Logger::Error("could not build pipeline: {}", e.what());
            return false;
        }

        ctx.controller = std::make_unique<Sagax::Pipeline::PpController>();
        dynamic_cast<Sagax::Pipeline::PpController*>(ctx.controller.get())->Setup(ctx.pipeline, 8);
        ctx.controller->Reset();
        try { ctx.Init(); } catch (const std::exception &e) {
            Common::Logger::Error("could not initialize resources: {}", e.what());
            ctx.Dispose();
            ctx.controller.reset();
            return false;
        }

        ctx.controller->Start();
        running = true;
        Common::Logger::Info("scenario started: {}", p.string());
        return true;
    }

    void StopScenario() {
        if (!running) return;
        try {
            ctx.controller->Stop(true);
            ctx.controller->Join();
            ctx.Dispose();
        } catch (const std::exception &e) {
            Common::Logger::Error("error stopping scenario: {}", e.what());
        }
        ctx.controller.reset();
        running = false;
        Common::Logger::Info("scenario stopped");
    }

    MqttClient client;

    void OnUpdate() {
        MqttClient::Message msg;
        while(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            try {
                auto cm = Sagax::SCG::ParseCommandMessage(payload);
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
                    StartScenario(v.profile);
                }
                else if (std::holds_alternative<CommandMessage::IntStop>(cm.variant)) {
                    Common::Logger::Info("received [{}]: IntStop", msg.topic);
                    StopScenario();
                }
            } catch (const std::exception &e) {
                Common::Logger::Error("received [{}]: failed to parse payload: {} payload='{}'", msg.topic, e.what(), payload);
            }
        }
    }
};

int main() {
    MainApp app;
    return app.Main();
}
