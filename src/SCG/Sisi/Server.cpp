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
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/ScenarioTransform/BuildPipeline.hpp"
#include "SCG/ScenarioTransform/ScenarioPipeline.hpp"
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
#include "SCG/Utility/TimeFormat.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

static CommandError ReadCommand(const std::string &payload, CommandMessage &out) {
    Json::JsonIssues issues;
    if(!SCG::ParseCommandMessage(payload, out, issues)) {
        return std::format("could not parse command:\n{}", issues.Format());
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

    // declared before ctx, so it outlives the engine in ctx
    Common::ThreadPool threadPool{4};
    SCG::ScenarioPipeline ctx;

    // the event callbacks run on a sink stage while it holds its stage mutex, and the
    // engine takes the stage mutexes while stopping, so the callbacks only take
    // mtxSignal and never mtx
    std::mutex mtxSignal;
    std::set<std::int32_t> activeEventIds;
    // every stream sink reports its first packet, the earliest one is kept
    std::optional<std::chrono::system_clock::time_point> transmissionStart;

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
                    std::unique_ptr<SSG::UsrpB200::Device> dev;
                    SSG::UsrpB200::Error error = SSG::UsrpB200::GetDevice(w.serial, dev);
                    Common::Assert(error == SSG::UsrpB200::Error::none, std::format("failed to initialize radio: {}", SSG::UsrpB200::ToString(error)));
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
                    SSG::Sidekiq::Error error = SSG::Sidekiq::LibraryInit();
                    Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to initialize the sidekiq library: {}", SSG::Sidekiq::ToString(error)));
                    std::unique_ptr<SSG::Sidekiq::Device> dev;
                    error = SSG::Sidekiq::GetDevice(w.serial, dev);
                    Common::Assert(error == SSG::Sidekiq::Error::none, std::format("failed to initialize radio: {}", SSG::Sidekiq::ToString(error)));
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
        pipelineConfig.events.transmissionStart = [this](std::chrono::system_clock::time_point time) {
            this->SetTransmissionStart(time);
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

    void SetTransmissionStart(std::chrono::system_clock::time_point time) {
        {
            std::unique_lock lck(mtxSignal);
            if(!transmissionStart.has_value() || time < transmissionStart.value()) {
                transmissionStart = time;
            }
        }
        cv.notify_all();
    }

    std::optional<std::chrono::system_clock::time_point> GetTransmissionStart() {
        std::unique_lock lck(mtxSignal);
        return transmissionStart;
    }

    void ClearTransmissionStart() {
        std::unique_lock lck(mtxSignal);
        transmissionStart.reset();
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

    // logs instead of publishing when the message could not be written
    void PublishPayload(
        const std::string &topic,
        const std::optional<std::string> &payload,
        const Json::JsonIssues &issues
    ) {
        if(!payload.has_value()) {
            Common::Logger::Error("could not write a message for {}:\n{}", topic, issues.Format());
            return;
        }
        Publish(topic, payload.value());
    }

    void PublishError(const std::string &text) {
        Common::Logger::Error("{}", text.c_str());
        ErrorMessage message;
        message.message = text;
        Json::JsonIssues issues;
        PublishPayload(config.mqtt.errorLogTopic, SerializeErrorMessage(message, issues), issues);
    }

    void PublishTestStatus(const std::string &testId, TestStatusMessage::TestStatus status, std::chrono::system_clock::time_point time) {
        TestStatusMessage message;
        message.timestamp = FormatIsoTime(time);
        message.testId = testId;
        message.testStatus = status;
        Json::JsonIssues issues;
        PublishPayload(config.mqtt.testLogTopic, SerializeTestStatusMessage(message, issues), issues);
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
        Json::JsonIssues issues;
        PublishPayload(config.mqtt.signalLogTopic, SerializeSignalStatusMessage(message, issues), issues);
    }

    CommandError CancelTest(const CommandMessage::TestCancel &command) {
        ScheduledTest *running = scheduler.Running();
        if(running != nullptr && running->testId == command.testId) {
            StopRunning();
        }
        return scheduler.Cancel(command.testId);
    }

    // the transmission and the test end here, the directory stays where it is. a cancel
    // removes that as well, which is the difference between the two
    CommandError StopTransmission() {
        ScheduledTest *test = scheduler.Running();
        if(test == nullptr) {
            return "no test is running";
        }
        if(test->state == ScheduledTest::State::preparing) {
            return std::format(
                "test \"{}\" is being prepared, nothing is being transmitted",
                test->testId
            );
        }

        std::string testId = test->testId;
        StopPipeline();
        scheduler.Finished(*test, false);
        Common::Logger::Info("playback stopped: {}", testId);
        // the id is free again at once, a setup reusing it may arrive in the same batch
        scheduler.EraseFinished();
        return {};
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
        ctx = SCG::ScenarioPipeline{};
        try {
            pipelineConfig.events.startTime = test.time;
            ctx = SCG::BuildStreamingPipeline(pipelineConfig, test.scenario);
        }
        catch(const std::exception &e) {
            PublishError(std::format("could not build pipeline for test \"{}\": {}", test.testId, e.what()));
            scheduler.Finished(test, false);
            return;
        }

        ctx.Setup(threadPool, SCG::GetDefaultCpuThreadCount());
        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            PublishError(std::format("could not initialize resources for test \"{}\": {}", test.testId, e.what()));
            ctx.Dispose();
            ctx = SCG::ScenarioPipeline{};
            scheduler.Finished(test, false);
            return;
        }

        ClearActiveEvents();
        ClearTransmissionStart();
        scheduler.Started(test);
        ctx.Start();
        Common::Logger::Info("playback started: {}", test.testId);
    }

    void StopPipeline() {
        ctx.Stop(false);
        ctx.Join();
        ctx.Dispose();
        ctx = SCG::ScenarioPipeline{};
        ClearActiveEvents();
        Common::Logger::Info("pipeline stopped");
    }

    // called once the pipeline has left the running state on its own
    void FinishPipeline(ScheduledTest &test) {
        bool failed = false;

        ctx.Join();
        try {
            if(std::exception_ptr error = ctx.GetError()) {
                std::rethrow_exception(error);
            }
        }
        catch(const std::exception &e) {
            PublishError(std::format("error running pipeline for test \"{}\": {}", test.testId, e.what()));
            failed = true;
        }
        ctx.Dispose();
        ctx = SCG::ScenarioPipeline{};
        ClearActiveEvents();

        if(!failed && test.state == ScheduledTest::State::starting) {
            PublishError(std::format("the pipeline of test \"{}\" ended before anything was transmitted", test.testId));
            failed = true;
        }
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
            else {
                if(test->state == ScheduledTest::State::starting) {
                    if(auto time = GetTransmissionStart()) {
                        scheduler.Transmitting(*test, time.value());
                    }
                }
                if(ctx.GetState() != Pipeline3::PipelineEngine::RunState::running) {
                    FinishPipeline(*test);
                }
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

    // the payload itself is in the line the client writes for every message it reads,
    // so this only says what the message turned out to be
    void Interpreted(const std::string &topic, const char *command) {
        Common::Logger::Info("interpreted [{}] as {}", topic, command);
    }

    void Ignored(const std::string &topic, const char *command) {
        Common::Logger::Warn("interpreted [{}] as {}, which does nothing here yet", topic, command);
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
            Interpreted(msg.topic, "TEST_SETUP");
            error = scheduler.Schedule(v);
        }
        else if(std::holds_alternative<CommandMessage::TestCancel>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestCancel>(cm.variant);
            Interpreted(msg.topic, "TEST_CANCEL");
            error = CancelTest(v);
        }
        else if(std::holds_alternative<CommandMessage::IntStop>(cm.variant)) {
            Interpreted(msg.topic, "INT_STOP");
            error = StopTransmission();
        }
        else if(std::holds_alternative<CommandMessage::IntStart>(cm.variant)) {
            Ignored(msg.topic, "INT_START");
        }
        else if(std::holds_alternative<CommandMessage::TestStart>(cm.variant)) {
            Ignored(msg.topic, "TEST_START");
        }
        else if(std::holds_alternative<CommandMessage::TestUpdate>(cm.variant)) {
            Ignored(msg.topic, "TEST_UPDATE");
        }
        else if(std::holds_alternative<CommandMessage::TestEnd>(cm.variant)) {
            Ignored(msg.topic, "TEST_END");
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
        Json::JsonIssues issues;
        if(!ParseServerConfig(buf.str(), config, issues)) {
            Common::Logger::Error("\nfailed to parse config:\n{}", issues.Format());
            return false;
        }

        Json::JsonIssues writeIssues;
        if(std::optional<std::string> text = SerializeServerConfig(config, writeIssues); text.has_value()) {
            Common::Logger::Info("using this configuration:\n{}", text.value());
        }
        else {
            Common::Logger::Error("could not write the configuration back:\n{}", writeIssues.Format());
        }

        Common::Logger::Info("validating configuration..");
        CommandError error = CheckValidity(SCG::ValidateConfig(config), "configuration");
        if(!error.empty()) {
            return false;
        }
        Common::Logger::Info("validation successful");

        scheduler.Configure(config);
        scheduler.onStatus = [this](const std::string &testId, TestStatusMessage::TestStatus status, std::chrono::system_clock::time_point time) {
            PublishTestStatus(testId, status, time);
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
            mqttConfig.logMessages = true;
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
            // mtx is only held for the wait, the steps below reach the stage mutexes
            // through the engine. the mqtt client, the event callbacks, the
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
