#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <optional>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <atomic>
#include <csignal>
#include <type_traits>
#include <mutex>
#include <condition_variable>
#include <variant>
#include <set>
#include <future>
#include <stop_token>
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"
#include "Common/ThreadPool.hpp"
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
#include "SCG/Utility/Fetch.hpp"
#include "SCG/Utility/SignalHandler.hpp"
#include "CommandError.hpp"
#include "MqttClient.hpp"
#include "ServerArguments.hpp"
#include "RidTransform.hpp"
#include "TestScheduler.hpp"
#include "TimeFormat.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

static CommandError ReadCommand(const std::string &payload, CommandMessage &out) {
    try {
        out = SCG::ParseCommandMessage(payload);
    }
    catch(const std::exception &e) {
        return std::format("could not parse command: {}", e.what());
    }
    return {};
}

// the main loop runs at least this often
constexpr std::chrono::milliseconds scheduleTick{50};

struct Program {
private:
    static Program instance;

    // cleared by the signal callback under mtx. it starts true, so a signal that arrives
    // before the loop still stops it
    bool programRunning = true;
    std::mutex mtx;
    std::condition_variable cv;

    ProgramParser parser;
    MqttClient client;
    SCG::ServerConfig config;
    SCG::PipelineConfig pipelineConfig;

    // declared before threadPool, the preparation running there reads the tests
    TestScheduler scheduler;
    std::stop_source preparationStop;
    std::future<std::string> preparation;

    // declared before ctx, so it outlives the controller in ctx
    Common::ThreadPool threadPool{4};
    SCG::ScenarioRunnerContext ctx;

    // the event callbacks run on the sink thread while it holds its module mutex, and the
    // controller takes the module mutexes while stopping, so the callbacks only take
    // mtxSignal and never mtx
    std::mutex mtxSignal;
    std::set<std::int32_t> activeEventIds;

    // only the main loop touches these
    std::optional<SignalStatusMessage::SignalStatus> lastSignalStatus;
    std::chrono::steady_clock::duration statusPeriod{};
    std::chrono::steady_clock::time_point nextSignalStatus;

    void Configure() {
        pipelineConfig = SCG::PipelineConfig{};
        pipelineConfig.mockRadios = config.mockRadios;
        // a mock device needs nothing opened
#ifdef HAVE_USRP
        pipelineConfig.preinit.usrp.usePreinitOnly = true;
        if(!config.mockRadios) {
            for(auto &v : config.radios) {
                if(StopRequested()) {
                    return;
                }
                if(std::holds_alternative<SCG::ServerConfig::Radio::UsrpB200>(v.variant)) {
                    auto &w = std::get<SCG::ServerConfig::Radio::UsrpB200>(v.variant);
                    auto dev = SSG::UsrpB200::GetDevice(w.serial);
                    Common::Assert(bool(dev), "failed to initialize radio");
                    pipelineConfig.preinit.usrp.devices[w.serial] = std::move(dev);
                }
            }
        }
#endif
#ifdef HAVE_SIDEKIQ
        pipelineConfig.preinit.sidekiq.usePreinitOnly = true;
        if(!config.mockRadios) {
            for(auto &v : config.radios) {
                if(StopRequested()) {
                    return;
                }
                if(std::holds_alternative<SCG::ServerConfig::Radio::Sidekiq>(v.variant)) {
                    auto &w = std::get<SCG::ServerConfig::Radio::Sidekiq>(v.variant);
                    // has to run before a device is opened, later calls do nothing
                    Common::Assert(SSG::Sidekiq::LibraryInit(), "failed to initialize the sidekiq library");
                    auto dev = SSG::Sidekiq::GetDevice(w.serial);
                    Common::Assert(bool(dev), "failed to initialize radio");
                    pipelineConfig.preinit.sidekiq.devices[w.serial] = std::move(dev);
                }
            }
        }
#endif

        pipelineConfig.events.eventStart = [this](std::int32_t eventId) {
            this->SetEventActive(eventId, true);
        };
        pipelineConfig.events.eventEnd = [this](std::int32_t eventId) {
            this->SetEventActive(eventId, false);
        };
    }

    // tracked per event, a source can have more than one
    void SetEventActive(std::int32_t eventId, bool active) {
        {
            std::unique_lock lck(mtxSignal);
            if(active) {
                activeEventIds.insert(eventId);
            }
            else {
                activeEventIds.erase(eventId);
            }
        }
        cv.notify_all();
    }

    void ClearActiveEvents() {
        std::unique_lock lck(mtxSignal);
        activeEventIds.clear();
    }

    // the active events are mapped to the sources of the test that is playing
    SignalStatusMessage::SignalStatus CurrentSignalStatus() {
        std::set<std::int32_t> eventIds;
        {
            std::unique_lock lck(mtxSignal);
            eventIds = activeEventIds;
        }

        auto status = SignalStatusMessage::SignalStatus::none;
        ScheduledTest *test = scheduler.Running();
        if(test == nullptr || test->state != ScheduledTest::State::playing) {
            return status;
        }
        auto &sources = test->rid.interferenceSources;
        for(std::int32_t eventId : eventIds) {
            if(eventId < 0 || std::size_t(eventId) >= test->eventMap.size()) {
                continue;
            }
            std::int32_t sourceId = test->eventMap[eventId];
            if(sourceId < 0 || std::size_t(sourceId) >= sources.size()) {
                continue;
            }
            if(std::holds_alternative<SCG::Rid::InterferenceSource::Spoofing>(sources[sourceId].variant)) {
                return SignalStatusMessage::SignalStatus::spoofing;
            }
            status = SignalStatusMessage::SignalStatus::jamming;
        }
        return status;
    }

    // only called once the pipeline is down, it holds the devices while it runs
    void ReleaseRadios() {
#ifdef HAVE_USRP
        pipelineConfig.preinit.usrp.devices.clear();
        // uhd keeps dispatching log records on its own thread after the devices are gone
        SSG::UsrpB200::DisposeLogging();
#endif
#ifdef HAVE_SIDEKIQ
        // the devices have to be released before the library that handed them out
        pipelineConfig.preinit.sidekiq.devices.clear();
        SSG::Sidekiq::LibraryCleanup();
#endif
    }

    bool StopRequested() {
        std::unique_lock lck(mtx);
        return !programRunning;
    }

    void Publish(const std::string &topic, const std::string &payload) {
        MqttClient::Message message;
        message.bytes.assign(payload.begin(), payload.end());
        message.qos = 2;
        message.retain = false;
        message.topic = topic;
        client.Push(std::move(message));
    }

    void PublishError(const std::string &text) {
        Common::Logger::Error("{}", text.c_str());
        ErrorMessage message;
        message.message = text;
        Publish(config.mqtt.errorLogTopic, SerializeErrorMessage(message));
    }

    void PublishTestStatus(const std::string &testId, TestStatusMessage::TestStatus status) {
        TestStatusMessage message;
        message.timestamp = FormatIsoTime(std::chrono::system_clock::now());
        message.testId = testId;
        message.status = status;
        Publish(config.mqtt.testLogTopic, SerializeTestStatusMessage(message));
    }

    void PublishSignalStatus() {
        auto status = CurrentSignalStatus();
        auto now = std::chrono::steady_clock::now();
        if(lastSignalStatus == status && now < nextSignalStatus) {
            return;
        }
        lastSignalStatus = status;
        nextSignalStatus = now + statusPeriod;

        SignalStatusMessage message;
        message.timestamp = FormatIsoTime(std::chrono::system_clock::now());
        message.type = status;
        Publish(config.mqtt.signalLogTopic, SerializeSignalStatusMessage(message));
    }

    CommandError CancelTest(const CommandMessage::TestCancel &command) {
        ScheduledTest *running = scheduler.Running();
        if(running != nullptr && running->testId == command.testId) {
            StopRunning();
        }
        return scheduler.Cancel(command.testId);
    }

    void StartPreparation(ScheduledTest &test) {
        scheduler.Started(test);
        Common::Logger::Info("preparation started: {}", test.testId);

        preparationStop = std::stop_source{};
        preparation = threadPool.Dispatch(true, [this, &test, stop = preparationStop.get_token()]() {
            std::string error;
            try {
                TestScheduler::Prepare(test, stop);
            }
            catch(const std::exception &e) {
                error = e.what();
            }
            cv.notify_all();
            return error;
        });
    }

    bool PreparationDone() {
        return (
            preparation.valid() &&
            preparation.wait_for(std::chrono::seconds(0)) == std::future_status::ready
        );
    }

    void FinishPreparation(ScheduledTest &test) {
        std::string error = preparation.get();
        if(!error.empty()) {
            PublishError(std::format("could not prepare test \"{}\": {}", test.testId, error));
            scheduler.Finished(test, false);
            return;
        }
        scheduler.Finished(test, true);
        Common::Logger::Info("preparation finished: {}", test.testId);
    }

    // leaves the test in the state it had
    void StopRunning() {
        ScheduledTest *test = scheduler.Running();
        if(test == nullptr) {
            return;
        }
        if(test->state == ScheduledTest::State::preparing) {
            preparationStop.request_stop();
            preparation.wait();
            preparation = {};
            Common::Logger::Info("preparation stopped");
        }
        else {
            StopPipeline();
        }
        scheduler.Stopped(*test);
    }

    void StartPipeline(ScheduledTest &test) {
        ctx = SCG::ScenarioRunnerContext{};
        try {
            pipelineConfig.events.startTime = test.time;
            auto b = SCG::BuildStreamingPipeline(pipelineConfig, test.scenario);
            ctx.pipeline = b.builder.Build();
            ctx.resources = b.resources;
            ctx.scenario = test.scenario;
        }
        catch(const std::exception &e) {
            PublishError(std::format("could not build pipeline for test \"{}\": {}", test.testId, e.what()));
            scheduler.Finished(test, false);
            return;
        }

        {
            auto controller = std::make_unique<Sagax::Pipeline::PpController>();
            controller->SetThreadPool(threadPool);
            controller->Setup(ctx.pipeline, 32);
            ctx.controller = std::move(controller);
        }
        ctx.controller->Reset();
        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            PublishError(std::format("could not initialize resources for test \"{}\": {}", test.testId, e.what()));
            ctx.Dispose();
            ctx.controller.reset();
            scheduler.Finished(test, false);
            return;
        }

        ClearActiveEvents();
        scheduler.Started(test);
        ctx.controller->Start();
        Common::Logger::Info("playback started: {}", test.testId);
    }

    void StopPipeline() {
        ctx.controller->Stop(true);
        ctx.controller->Join();
        ctx.Dispose();
        ctx.controller.reset();
        ClearActiveEvents();
        Common::Logger::Info("pipeline stopped");
    }

    // called once the controller has left the running state on its own
    void FinishPipeline(ScheduledTest &test) {
        bool failed = false;

        ctx.controller->Join();
        try {
            ctx.controller->Throw();
        }
        catch(const std::exception &e) {
            PublishError(std::format("error running pipeline for test \"{}\": {}", test.testId, e.what()));
            failed = true;
        }
        ctx.Dispose();
        ctx.controller.reset();
        ClearActiveEvents();

        scheduler.Finished(test, !failed);
        if(!failed) {
            Common::Logger::Info("playback finished: {}", test.testId);
        }
    }

    // a playback that is due before the next tick is started now
    void UpdateSchedule() {
        if(ScheduledTest *test = scheduler.Running()) {
            if(test->state == ScheduledTest::State::preparing) {
                if(PreparationDone()) {
                    FinishPreparation(*test);
                }
            }
            else if(ctx.controller->GetState() != Sagax::Pipeline::Controller::RunState::running) {
                FinishPipeline(*test);
            }
        }

        if(ScheduledTest *test = scheduler.DueForPlayback(std::chrono::system_clock::now(), scheduleTick)) {
            StartPipeline(*test);
        }
        if(ScheduledTest *test = scheduler.DueForPreparation()) {
            StartPreparation(*test);
        }
        scheduler.EraseFinished();
    }

    void HandleCommand(const MqttClient::Message &msg) {
        std::string payload(msg.bytes.begin(), msg.bytes.end());
        CommandMessage cm;
        CommandError error = ReadCommand(payload, cm);
        if(!error.empty()) {
            error = std::format("received [{}]: bad payload: \"{}\": {}", msg.topic, payload, error);
        }
        else if(std::holds_alternative<CommandMessage::TestSetup>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestSetup>(cm.variant);
            Common::Logger::Info("received [{}] TEST_SETUP: scenario_url={} start_time={} test_id={}", msg.topic, v.scenarioUrl, v.startTime, v.testId);
            error = scheduler.Schedule(v);
        }
        else if(std::holds_alternative<CommandMessage::TestCancel>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestCancel>(cm.variant);
            Common::Logger::Info("received [{}] TEST_CANCEL: test_id={}", msg.topic, v.testId);
            error = CancelTest(v);
        }
        else {
            error = std::format("received [{}]: unknown command", msg.topic);
        }

        if(!error.empty()) {
            PublishError(error);
        }
    }

    void HandleCommands() {
        MqttClient::Message msg;
        while(client.Pop(msg)) {
            // an exception only drops this message
            try {
                HandleCommand(msg);
            }
            catch(const std::exception &e) {
                Common::Logger::Error("failed to handle the message on [{}]: {}", msg.topic, e.what());
            }
        }
    }

    bool Setup(Parameters params) {
        std::ifstream file(params.configPath);
        if(!file.is_open()) {
            Common::Logger::Error("failed to open config file: {}", params.configPath);
            return false;
        }
        std::stringstream buf;
        buf << file.rdbuf();
        try {
            config = ParseServerConfig(buf.str());
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nfailed to parse config:\n{}", e.what());
            return false;
        }

        Common::Logger::Info("using this configuration:\n{}", SerializeServerConfig(config));

        Common::Logger::Info("validating configuration..");
        CommandError error = CheckValidity(SCG::ValidateConfig(config), "configuration");
        if(!error.empty()) {
            return false;
        }
        Common::Logger::Info("validation successful");

        scheduler.Configure(config);
        scheduler.onStatus = [this](const std::string &testId, TestStatusMessage::TestStatus status) {
            PublishTestStatus(testId, status);
        };
        scheduler.onError = [this](const std::string &text) {
            PublishError(text);
        };

        try {
            Configure();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nfailed to initialize the radios:\n{}", e.what());
            return false;
        }

        return true;
    }

    void Run() {
        MqttClient::LibraryInit();
        Common::Defer disposeMqttLibrary([](){
            MqttClient::LibraryCleanup();
        });
        Common::Defer disposeClient([this](){
            client.Stop();
        });
        Common::Defer disposeRunning([this](){
            StopRunning();
        });

        {
            MqttClient::Configuration mqttConfig;
            mqttConfig.host = config.mqtt.brokerHost;
            mqttConfig.port = config.mqtt.brokerPort;
            mqttConfig.subscribeTopics = { config.mqtt.commandTopic };
            mqttConfig.keepAlive_s = 60;
            mqttConfig.clientId = "RSG";
            mqttConfig.reconnectDelays_ms = { 1000, 2000, 5000, 10000, 30000 };
            mqttConfig.maxQueueSize = 200;
            mqttConfig.onUpdate = [this]() {
                this->cv.notify_all();
            };
            client.Configure(std::move(mqttConfig));
            client.Start();
        }
        if(!client.IsRunning()) {
            Common::Logger::Error("could not start the mqtt client, stopping");
            return;
        }

        scheduler.Restore();

        Common::Logger::Info("server running.. ctrl+c to stop");

        statusPeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(config.statusUpdatePeriod_s)
        );
        nextSignalStatus = std::chrono::steady_clock::now();
        while(true) {
            // mtx is only held for the wait, the steps below reach the module mutexes
            // through the controller. the mqtt client, the event callbacks, the
            // preparation and the signal handler end the wait early
            {
                std::unique_lock lck(mtx);
                cv.wait_for(lck, scheduleTick);
                if(!programRunning) {
                    break;
                }
            }

            HandleCommands();
            UpdateSchedule();
            PublishSignalStatus();
        }
    }

public:

    static Program &GetInstance() {
        return instance;
    }

    int Main(int argc, char **argv) {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .colors = true,
            .file = false,
            .function = false
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){
            Common::Logger::RemoveStream(&logStream);
        });

        // the same records without colours, replaced on every run
        std::ofstream logFile("scgserver.log", std::ios::trunc);
        Common::StdLogStream fileLogStream(logFile, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .colors = false,
            .file = true,
            .function = false
        });
        if(logFile.is_open()) {
            Common::Logger::AddStream(&fileLogStream);
        }
        Common::Defer disposeFileLogger([&](){
            if(logFile.is_open()) {
                Common::Logger::RemoveStream(&fileLogStream);
            }
        });
        if(!logFile.is_open()) {
            Common::Logger::Warn("could not open scgserver.log, logging to the console only");
        }

        // has to exist before the mqtt client or a pipeline starts a thread, on linux a
        // thread inherits the signal mask. the callback runs on an ordinary thread
        SCG::SignalHandler signalHandler({ SIGINT, SIGTERM }, [this](int sig){
            Common::Logger::Info("signal {} received, stopping", sig);
            {
                std::unique_lock lck(mtx);
                programRunning = false;
            }
            cv.notify_all();
        });

        // the radios are opened by Setup, this releases them after Run, also on a throw
        Common::Defer releaseRadios([this](){
            ReleaseRadios();
        });

        Parameters params{};
        parser.Parse(argc, argv);
        params = parser.params;
        if(!params.success) {
            Common::Logger::Error("\nfailed to parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info("\n{}", parser.Help().c_str());
            return 1;
        }
        if(params.helpRequested) {
            return 0;
        }

        bool setupSuccess = false;
        try {
            setupSuccess = Setup(params);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror during setup:\n{}", e.what());
        }

        if(!setupSuccess) {
            Common::Logger::Info("unsuccessful setup, stopping");
            return 1;
        }

        // a signal during the setup ends the run here
        if(StopRequested()) {
            Common::Logger::Info("stopped before the server started");
            return 0;
        }

        try {
            Run();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror during run:\n{}", e.what());
            return 1;
        }
        return 0;
    }

};
Program Program::instance;

int main(int argc, char **argv) {
    return Program::GetInstance().Main(argc, argv);
}
