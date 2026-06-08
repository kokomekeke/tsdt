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
#include <mutex>
#include <condition_variable>
#include <variant>
#include "SSG/argparse/argparse.hpp"
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"
#include "Pipeline/PpController.hpp"
#include "Pipeline/RtcController.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/ScenarioRunnerContext.hpp"
#include "SCG/ScenarioTransform/Transform.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/ServerConfig.hpp"
#include "MqttClient.hpp"
#include "RidTransform.hpp"

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

    ProgramParser() : program("scgserver.exe", "1.0", argparse::default_arguments::all) {
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

struct Program {
private:
    static Program instance;

    bool programRunning = false;
    std::mutex mtx; // only used for signal handler
    std::condition_variable cv;

    ProgramParser parser;
    MqttClient client;
    SCG::ServerConfig config;
    SCG::PipelineConfig pipelineConfig;

    bool scenarioRunning = false;
    SCG::ScenarioRunnerContext ctx;
    SCG::Rid rid;
    SCG::ScenarioRidMap scenarioMap;

    void Configure() {
        pipelineConfig = SCG::PipelineConfig{};
#ifdef HAVE_USRP
        pipelineConfig.preinit.usrp.usePreinitOnly = true;
#endif
        for(auto &v : config.radios) {
            if(std::holds_alternative<SCG::ServerConfig::Radio::UsrpB200>(v.variant)) {
#ifdef HAVE_USRP
                auto &w = std::get<SCG::ServerConfig::Radio::UsrpB200>(v.variant);
                auto dev = SSG::UsrpB200::GetDevice(w.serial);
                Common::Assert(bool(dev), "failed to initialize radio");
                pipelineConfig.preinit.usrp.devices[w.serial] = std::move(dev);
#endif
            }
            else {
                Common::Assert(false, "unknown radio type in config");
            }
        }


        pipelineConfig.events.eventStart = [this](std::int32_t eventId) {
            this->PushLogMessage(eventId, true);
        };
        pipelineConfig.events.eventEnd = [this](std::int32_t eventId) {
            this->PushLogMessage(eventId, false);
        };
    }

    void PushLogMessage(std::int32_t eventId, bool start) {
        LogMessage message;
        message.status = (start ? LogMessage::Status::active : LogMessage::Status::inactive);

        {
            auto ifId = scenarioMap.eventMap[eventId];
            auto &variant = rid.interferenceSources[ifId].variant;
            if(std::holds_alternative<Rid::InterferenceSource::Cw>(variant)) {
                message.type = LogMessage::Type::jamming;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Chirp>(variant)) {
                message.type = LogMessage::Type::jamming;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Noise>(variant)) {
                message.type = LogMessage::Type::jamming;
            }
            else if(std::holds_alternative<Rid::InterferenceSource::Spoofing>(variant)) {
                message.type = LogMessage::Type::spoofing;
            }
            else {
                message.type = LogMessage::Type::none;
            }
        }

        std::string str = SerializeLogMessage(message);
        MqttClient::Message mqttMessage;
        mqttMessage.bytes.assign(str.begin(), str.end());
        mqttMessage.qos = 2;
        mqttMessage.retain = false;
        mqttMessage.topic = config.mqtt.logTopic;
        client.Push(mqttMessage);
    }

    void StartScenario(const std::string &profile) {
        if(scenarioRunning) {
            StopScenario();
        }

        auto p = std::filesystem::path(profile);
        try {
            p = std::filesystem::absolute(p);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("invalid scenario file path: \"{}\": {}", profile, e.what());
            return;
        }
        std::ifstream file(p);
        if(!file.is_open()) {
            Common::Logger::Error("failed to open scenario file: {}", p.string());
            return;
        }
        std::stringstream buf;
        buf << file.rdbuf();
        std::string jsonStr = buf.str();
        file.close();

        try {
            rid = SCG::ParseRid(jsonStr);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nfailed to parse RID:\n{}", e.what());
            return;
        }

        {
            Common::Logger::Info("validating RID..");
            auto validity = SCG::ValidateRid(config, rid);
            for(auto &s : validity.warnings) {
                Common::Logger::Warn("{}", s.c_str());
            }
            if(!validity.errors.empty()) {
                for(auto &s : validity.errors) {
                    Common::Logger::Error("{}", s.c_str());
                }
                return;
            }
            else {
                Common::Logger::Info("validation successful");
            }
        }

        scenarioMap = SCG::MapRidToScenario(config, rid);
        ctx.scenario = scenarioMap.scen;
        Common::Logger::Info("converted scenario:\n{}", SCG::SerializeScenario(ctx.scenario));

        try {
            auto b = SCG::BuildStreamingPipeline(pipelineConfig, ctx.scenario);
            ctx.pipeline = b.builder.Build();
            ctx.resources = b.resources;
        }
        catch (const std::exception &e) {
            Common::Logger::Error("\ncould not build pipeline:\n{}", e.what());
            return;
        }

        {
            auto controller = std::make_unique<Sagax::Pipeline::PpController>();
            controller->Setup(ctx.pipeline, 32);
            ctx.controller = std::move(controller);
        }
        ctx.controller->Reset();
        try {
            ctx.Init();
        }
        catch (const std::exception &e) {
            Common::Logger::Error("\ncould not initialize resources:\n{}", e.what());
            ctx.Dispose();
            ctx.controller.reset();
            return;
        }

        ctx.controller->Start();
        scenarioRunning = true;
        Common::Logger::Info("scenario started: {}", p.string());
        return;
    }

    void StopScenario() {
        if(!scenarioRunning) {
            return;
        }
        try {
            ctx.controller->Stop(true);
            ctx.controller->Join();
            ctx.Dispose();
        }
        catch (const std::exception &e) {
            Common::Logger::Error("\nerror while stopping scenario:\n{}", e.what());
        }
        ctx.controller.reset();
        scenarioRunning = false;
        Common::Logger::Info("scenario stopped");
    }

    void OnUpdate() {
        MqttClient::Message msg;
        while(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            try {
                auto cm = Sagax::SCG::ParseCommandMessage(payload);
                if(std::holds_alternative<CommandMessage::TestStart>(cm.variant)) {
                    auto &v = std::get<CommandMessage::TestStart>(cm.variant);
                    Common::Logger::Info("received [{}]: TestStart - scenario={} vehicle={} customer={}", msg.topic, v.scenario, v.vehicle, v.customer);
                }
                else if(std::holds_alternative<CommandMessage::TestEnd>(cm.variant)) {
                    Common::Logger::Info("received [{}]: TestEnd", msg.topic);
                }
                else if(std::holds_alternative<CommandMessage::TestUpdate>(cm.variant)) {
                    Common::Logger::Info("received [{}]: TestUpdate", msg.topic);
                }
                else if(std::holds_alternative<CommandMessage::IntStart>(cm.variant)) {
                    auto &v = std::get<CommandMessage::IntStart>(cm.variant);
                    Common::Logger::Info("received [{}]: IntStart - profile={}", msg.topic, v.profile);
                    StartScenario(v.profile);
                }
                else if(std::holds_alternative<CommandMessage::IntStop>(cm.variant)) {
                    Common::Logger::Info("received [{}]: IntStop", msg.topic);
                    StopScenario();
                }
            }
            catch (const std::exception &e) {
                Common::Logger::Error("received [{}]: failed to parse payload: \"{}\"\n{}", msg.topic, payload, e.what());
            }
        }
    }

    void Run(Parameters params) {
        {
            std::ifstream file(params.configPath);
            if(!file.is_open()) {
                Common::Logger::Error("failed to open config file: {}", params.configPath);
                return;
            }
            std::stringstream buf;
            buf << file.rdbuf();
            try {
                config = ParseServerConfig(buf.str());
                Configure();
            }
            catch(const std::exception &e) {
                Common::Logger::Error("\nfailed to parse config:\n{}", e.what());
                return;
            }


            Common::Logger::Info("validating configuration..");
            auto validity = SCG::ValidateConfig(config);
            for(auto &s : validity.warnings) {
                Common::Logger::Warn("{}", s.c_str());
            }
            if(!validity.errors.empty()) {
                for(auto &s : validity.errors) {
                    Common::Logger::Error("{}", s.c_str());
                }
                return;
            }
            else {
                Common::Logger::Info("validation successful");
            }
        }

        {
            MqttClient::LibraryInit();
            MqttClient::Configuration mqttConfig;
            mqttConfig.host = config.mqtt.brokerHost;
            mqttConfig.port = config.mqtt.brokerPort;
            mqttConfig.subscribeTopics = { config.mqtt.commandTopic };
            mqttConfig.keepAlive_s = 60;
            mqttConfig.clientId = "RSG";
            mqttConfig.reconnectDelays_ms = { 1000, 2000, 5000, 10000, 30000 };
            mqttConfig.maxQueueSize = 200;
            mqttConfig.onUpdate = [this]() {
                this->OnUpdate();
            };
            client.Configure(std::move(mqttConfig));
            client.Start();
        }

        {
            Common::Logger::Info("server running.. ctrl+c to stop");
            programRunning = true;
            std::signal(SIGINT, [](int){
                auto &program = Program::instance;
                {
                    std::unique_lock lck(program.mtx);
                    program.programRunning = false;
                }
                program.cv.notify_all();
            });
            std::unique_lock lck(mtx);
            cv.wait(lck, [this]() {
                return !(this->programRunning);
            });
        }

        client.Stop();
        MqttClient::LibraryCleanup();
    }

public:

    static Program &GetInstance() {
        return instance;
    }

    void Main(int argc, char **argv) {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, {});
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){
            Common::Logger::RemoveStream(&logStream);
        });

        try {
            parser.Parse(argc, argv);
            Parameters params = parser.params;
            if(!params.success) {
                Common::Logger::Error("\nfailed to parse parameters:\n{}", params.error.c_str());
                Common::Logger::Info("\n{}", parser.Help().c_str());
                return;
            }

            Run(params);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\ntop level exception:\n{}", e.what());
        }
    }

};
Program Program::instance;

int main(int argc, char **argv) {
    Program::GetInstance().Main(argc, argv);
    return 0;
}
