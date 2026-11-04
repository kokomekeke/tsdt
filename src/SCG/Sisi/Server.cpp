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
#include "SCG/Scenario/ScenarioPipeline.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/ServerConfig.hpp"
#include "SCG/Utility/Fetch.hpp"
#include "SCG/Utility/SignalHandler.hpp"
#include "SCG/Sisi/ServerParameters.hpp"
#include "SCG/Sisi/TsdTransform.hpp"
#include "SCG/Utility/TimeFormat.hpp"
#include "SCG/Utility/Uuid.hpp"
#include "SCG/Utility/WaitUntil.hpp"

#include "Server.hpp"

namespace Sagax::SCG::Server {

    namespace {

        CommandError ReadCommand(const std::string &payload, CommandMessage &out) {
            Json::JsonIssues issues;
            if(!SCG::ParseCommandMessage(payload, out, issues)) {
                return std::format("could not parse command:\n{}", issues.Format());
            }
            return {};
        }

    }

    void Server::Configure() {
        pipelineConfig = SCG::PipelineConfig{};
        pipelineConfig.mockRadios = config.mockRadios;
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

        pipelineConfig.events.eventStart = [this](std::int32_t eventId, std::chrono::system_clock::time_point time) {
            this->QueueSignalEvent(eventId, true, time);
        };
        pipelineConfig.events.eventEnd = [this](std::int32_t eventId, std::chrono::system_clock::time_point time) {
            this->QueueSignalEvent(eventId, false, time);
        };
    }

    void Server::ConfigureMqtt() {
        MqttClient::Configuration mqttConfig;
        mqttConfig.host = config.mqtt.brokerHost;
        mqttConfig.port = config.mqtt.brokerPort;
        mqttConfig.subscribeTopics = { config.mqtt.commandTopic };
        mqttConfig.keepAlive_s = 60;
        mqttConfig.clientId = "RSG";
        mqttConfig.logMessages = config.logMqtt;
        mqttConfig.reconnectDelays_ms = { 1000, 2000, 5000, 10000, 30000 };
        mqttConfig.maxQueueSize = 200;
        mqttConfig.onUpdate = [this]() {
            this->cv.notify_all();
        };
        client.Configure(std::move(mqttConfig));
    }

    void Server::QueueSignalEvent(std::int32_t eventId, bool active, std::chrono::system_clock::time_point time) {
        {
            std::unique_lock lck(mtx);
            signalEvents.insert({ time, SignalEvent{ eventId, active } });
        }
        cv.notify_all();
    }

    // a source can have more than one event, the active ones are tracked by event id
    void Server::ApplySignalEvents() {
        auto now = std::chrono::system_clock::now();
        std::unique_lock lck(mtx);
        while(!signalEvents.empty() && signalEvents.begin()->first <= now) {
            const SignalEvent &event = signalEvents.begin()->second;
            if(event.active) {
                activeEventIds.insert(event.eventId);
            }
            else {
                activeEventIds.erase(event.eventId);
            }
            signalEvents.erase(signalEvents.begin());
        }
    }

    // only called while no pipeline runs
    void Server::ClearSignalEvents() {
        {
            std::unique_lock lck(mtx);
            signalEvents.clear();
        }
        activeEventIds.clear();
    }

    SignalStatusMessage::SignalStatus Server::CurrentSignalStatus() {

        auto status = SignalStatusMessage::SignalStatus::none;
        ScheduledTest *test = scheduler.Active();
        if(test == nullptr || test->state != ScheduledTest::State::playing || !playingPart.has_value()) {
            return status;
        }
        auto &part = test->scenario.parts[playingPart.value()];
        for(std::int32_t eventId : activeEventIds) {
            Common::Assert(eventId >= 0 && std::size_t(eventId) < part.eventSources.size(), "the event is not in the layout of the pipeline");
            const SCG::TsdScenario::EventSource &eventSource = part.eventSources[std::size_t(eventId)];
            const SCG::Rid &rid = test->scenario.rids[std::size_t(eventSource.iStep)].value();
            if(std::holds_alternative<SCG::Rid::InterferenceSource::Spoofing>(rid.interferenceSources[std::size_t(eventSource.iSource)].variant)) {
                return SignalStatusMessage::SignalStatus::spoofing;
            }
            status = SignalStatusMessage::SignalStatus::jamming;
        }
        return status;
    }

    // only called while no pipeline runs
    void Server::ReleaseRadios() {
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

    bool Server::StopRequested() {
        std::unique_lock lck(mtx);
        return !programRunning;
    }

    void Server::Publish(
        const std::string &topic,
        std::uint8_t qos,
        const std::optional<std::string> &payload,
        const Json::JsonIssues &issues
    ) {
        if(!payload.has_value()) {
            Common::Logger::Error(lh, "could not write a message for {}:\n{}", topic, issues.Format());
            return;
        }
        MqttClient::Message message;
        message.bytes.assign(payload.value().begin(), payload.value().end());
        message.qos = qos;
        message.retain = false;
        message.topic = topic;
        client.Push(std::move(message));
    }

    void Server::PublishLog(
        SessionLogMessage::Severity severity,
        const std::optional<std::string> &testId,
        const std::string &text
    ) {
        if(severity == SessionLogMessage::Severity::error) {
            Common::Logger::Error(lh, "{}", text.c_str());
        }
        else {
            Common::Logger::Warn(lh, "{}", text.c_str());
        }
        SessionLogMessage message;
        message.eventId = GenerateUuid();
        message.timestamp = FormatIsoTime(std::chrono::system_clock::now());
        message.sessionId = testId;
        message.subsystem = SessionLogMessage::Subsystem::rsg;
        message.severity = severity;
        message.message = text;
        Json::JsonIssues issues;
        Publish(config.mqtt.errorLogTopic, sessionLogQos, SerializeSessionLogMessage(message, issues), issues);
    }

    void Server::PublishSignalStatus() {
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
        Publish(config.mqtt.signalLogTopic, signalStatusQos, SerializeSignalStatusMessage(message, issues), issues);
    }

    CommandError Server::CancelTest(const CommandMessage::TestCancel &command) {
        ScheduledTest *active = scheduler.Active();
        if(active != nullptr && active->testId == command.testId) {
            StopActive();
        }
        return scheduler.Cancel(command.testId);
    }

    // the test fails and its directory is kept
    CommandError Server::StopTransmission() {
        ScheduledTest *test = scheduler.Active();
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
        if(playingPart.has_value()) {
            StopPipeline();
        }
        scheduler.SetFailed(*test);
        Common::Logger::Info(lh, "playback stopped: {}", testId);
        // a setup with the same id may follow in the same batch of commands
        scheduler.EraseFinished();
        return {};
    }

    void Server::StartPreparation(ScheduledTest &test) {
        scheduler.SetPreparing(test);
        Common::Logger::Info(lh, "preparation started: {}", test.testId);

        preparationStop = std::stop_source{};
        preparation = threadPool.Dispatch(true, [this, &test, stop = preparationStop.get_token()]() {
            std::string error;
            try {
                scheduler.Prepare(test, stop);
            }
            catch(const std::exception &e) {
                error = e.what();
            }
            cv.notify_all();
            return error;
        });
    }

    bool Server::PreparationDone() {
        return (
            preparation.valid() &&
            preparation.wait_for(std::chrono::seconds(0)) == std::future_status::ready
        );
    }

    void Server::FinishPreparation(ScheduledTest &test) {
        std::string error = preparation.get();
        if(!error.empty()) {
            PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("could not prepare test \"{}\": {}", test.testId, error));
            scheduler.SetFailed(test);
            return;
        }
        scheduler.SetReady(test);
        Common::Logger::Info(lh, "preparation finished: {}", test.testId);
    }

    // stops the preparation or the pipeline of the active test, the test keeps its state
    void Server::StopActive() {
        ScheduledTest *test = scheduler.Active();
        if(test == nullptr) {
            return;
        }
        if(test->state == ScheduledTest::State::preparing) {
            preparationStop.request_stop();
            preparation.wait();
            preparation = {};
            Common::Logger::Info(lh, "preparation stopped");
        }
        else if(playingPart.has_value()) {
            StopPipeline();
        }
    }

    void Server::StartPart(ScheduledTest &test) {
        std::size_t iPart = test.nextPart;
        scheduler.SetPartStarted(test);
        auto &part = test.scenario.parts[iPart];

        if(std::chrono::system_clock::now() >= part.time) {
            PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("part {} of test \"{}\" missed its starting time", iPart, test.testId));
            scheduler.SetFailed(test);
            return;
        }

        std::string hotRadios;
        if(!radioMonitor.RadiosReady(hotRadios)) {
            PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("part {} of test \"{}\" is not started, the radios are not ready: {}", iPart, test.testId, hotRadios));
            scheduler.SetFailed(test);
            return;
        }

        ctx = std::move(part.pipeline);
        ctx.Setup(threadPool, SCG::GetDefaultCpuThreadCount());
        try {
            ctx.Init();
        }
        catch(const std::exception &e) {
            PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("could not initialize resources for part {} of test \"{}\": {}", iPart, test.testId, e.what()));
            ctx.Dispose();
            ctx = SCG::ScenarioPipeline{};
            scheduler.SetFailed(test);
            return;
        }

        ClearSignalEvents();
        playingPart = iPart;
        ctx.Start();
        if(test.authorized) {
            ctx.Authorize();
        }
        radioMonitor.SetGainUpdates(part.gainUpdates);
        Common::Logger::Info(lh, "part {} of test \"{}\" started, it plays at {}", iPart, test.testId, FormatIsoTime(part.time));
    }

    void Server::StopPipeline() {
        radioMonitor.SetGainUpdates({});
        ctx.Stop(false);
        ctx.Join();
        ctx.Dispose();
        ctx = SCG::ScenarioPipeline{};
        playingPart.reset();
        ClearSignalEvents();
        Common::Logger::Info(lh, "pipeline stopped");
    }

    // called once the pipeline of the part has stopped on its own
    bool Server::FinishPart(ScheduledTest &test) {
        std::size_t iPart = playingPart.value();
        bool failed = false;

        radioMonitor.SetGainUpdates({});
        ctx.Join();
        try {
            if(std::exception_ptr error = ctx.GetError()) {
                std::rethrow_exception(error);
            }
        }
        catch(const std::exception &e) {
            PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("error running the pipeline of part {} of test \"{}\": {}", iPart, test.testId, e.what()));
            failed = true;
        }
        ctx.Dispose();
        ctx = SCG::ScenarioPipeline{};
        playingPart.reset();
        ClearSignalEvents();

        if(failed) {
            scheduler.SetFailed(test);
            return false;
        }
        Common::Logger::Info(lh, "part {} of test \"{}\" finished", iPart, test.testId);
        return true;
    }

    // the test is playing from its time until its end, also between parts
    void Server::UpdatePlayback(ScheduledTest &test, std::chrono::system_clock::time_point now) {
        if(playingPart.has_value() && ctx.GetState() != Pipeline3::PipelineEngine::RunState::running) {
            if(!FinishPart(test)) {
                return;
            }
        }
        if(test.state == ScheduledTest::State::starting && now >= test.time) {
            if(!test.authorized) {
                if(playingPart.has_value()) {
                    StopPipeline();
                }
                PublishLog(SessionLogMessage::Severity::error, test.testId, std::format("test \"{}\" was not authorized by its starting time", test.testId));
                scheduler.SetFailed(test);
                return;
            }
            scheduler.SetPlaying(test);
        }
        if(playingPart.has_value()) {
            return;
        }
        if(scheduler.PartDue(test, now)) {
            StartPart(test);
        }
        else if(scheduler.PlaybackOver(test, now)) {
            scheduler.SetDone(test);
            Common::Logger::Info(lh, "playback finished: {}", test.testId);
        }
    }

    // when the radios turn not ready, the pipeline of the active test is stopped at once and
    // the test fails
    void Server::UpdateRadios() {
        {
            std::unique_lock lck(mtx);
            radiosTurnedNotReady = false;
        }
        std::string hotRadios;
        bool ready = radioMonitor.RadiosReady(hotRadios);
        if(ready == radiosReady) {
            return;
        }
        radiosReady = ready;
        if(ready) {
            Common::Logger::Info(lh, "the radios are ready again");
            return;
        }
        ScheduledTest *test = scheduler.Active();
        std::optional<std::size_t> stoppedPart = playingPart;
        if(stoppedPart.has_value()) {
            StopPipeline();
        }
        PublishLog(SessionLogMessage::Severity::error, test == nullptr ? std::nullopt : std::optional(test->testId), std::format("the radios are stopped, they are too hot: {}", hotRadios));
        if(stoppedPart.has_value()) {
            PublishLog(SessionLogMessage::Severity::error, test->testId, std::format("part {} of test \"{}\" is stopped, the radios are not ready", stoppedPart.value(), test->testId));
            scheduler.SetFailed(*test);
        }
    }

    void Server::UpdateSchedule() {
        auto now = std::chrono::system_clock::now();
        scheduler.FailMissed(now);

        if(ScheduledTest *test = scheduler.Active()) {
            if(test->state == ScheduledTest::State::preparing) {
                if(PreparationDone()) {
                    FinishPreparation(*test);
                }
            }
            else {
                UpdatePlayback(*test, now);
            }
        }

        if(ScheduledTest *test = scheduler.DueForPlayback(now)) {
            scheduler.SetStarting(*test);
            Common::Logger::Info(lh, "playback started: {}", test->testId);
            UpdatePlayback(*test, now);
        }
        if(ScheduledTest *test = scheduler.DueForPreparation()) {
            StartPreparation(*test);
        }
        scheduler.EraseFinished();
    }

    // waits for the next tick. a scheduler time or a signal event before the tick is waited
    // for accurately. the mqtt client, the event callbacks, the preparation and the signal
    // handler notify cv
    void Server::Wait() {
        std::unique_lock lck(mtx);
        if(!programRunning || radiosTurnedNotReady) {
            return;
        }
        auto now = std::chrono::system_clock::now();
        std::optional<std::chrono::system_clock::time_point> next = scheduler.NextTime(now);
        if(!signalEvents.empty() && (!next.has_value() || signalEvents.begin()->first < next.value())) {
            next = signalEvents.begin()->first;
        }
        if(next.has_value() && next.value() < now + scheduleTick) {
            WaitUntil(cv, lck, next.value(), schedulePrecision);
        }
        else {
            cv.wait_until(lck, now + scheduleTick);
        }
    }

    void Server::Interpreted(const std::string &topic, const char *command) {
        Common::Logger::Info(lh, "interpreted [{}] as {}", topic, command);
    }

    void Server::Ignored(const std::string &topic, const char *command) {
        Common::Logger::Warn(lh, "interpreted [{}] as {}, which does nothing here yet", topic, command);
    }

    void Server::HandleCommand(const MqttClient::Message &msg) {
        std::string payload(msg.bytes.begin(), msg.bytes.end());
        CommandMessage cm;
        CommandError error = ReadCommand(payload, cm);
        SessionLogMessage::Severity severity = SessionLogMessage::Severity::error;
        std::optional<std::string> testId;
        if(!error.empty()) {
            error = std::format("received [{}]: bad payload: \"{}\": {}", msg.topic, payload, error);
        }
        else if(std::holds_alternative<CommandMessage::TestSetup>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestSetup>(cm.variant);
            Interpreted(msg.topic, "TEST_SETUP");
            testId = v.testId;
            error = scheduler.Schedule(v);
        }
        else if(std::holds_alternative<CommandMessage::TestCancel>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestCancel>(cm.variant);
            Interpreted(msg.topic, "TEST_CANCEL");
            severity = SessionLogMessage::Severity::warning;
            testId = v.testId;
            error = CancelTest(v);
        }
        else if(std::holds_alternative<CommandMessage::IntStop>(cm.variant)) {
            Interpreted(msg.topic, "INT_STOP");
            severity = SessionLogMessage::Severity::warning;
            error = StopTransmission();
        }
        else if(std::holds_alternative<CommandMessage::IntStart>(cm.variant)) {
            Ignored(msg.topic, "INT_START");
        }
        else if(std::holds_alternative<CommandMessage::TestStart>(cm.variant)) {
            auto &v = std::get<CommandMessage::TestStart>(cm.variant);
            Interpreted(msg.topic, "TEST_START");
            testId = v.testId;
            error = scheduler.Authorize(v.testId, std::chrono::system_clock::now());
            ScheduledTest *active = scheduler.Active();
            if(error.empty() && active != nullptr && active->testId == v.testId && playingPart.has_value()) {
                ctx.Authorize();
            }
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
            PublishLog(severity, testId, error);
        }
    }

    void Server::HandleCommands() {
        MqttClient::Message msg;
        while(client.Pop(msg)) {
            try {
                HandleCommand(msg);
            }
            catch(const std::exception &e) {
                Common::Logger::Error(lh, "failed to handle the message on [{}]: {}", msg.topic, e.what());
            }
        }
    }

    bool Server::Setup(Parameters params) {
        std::ifstream file(params.configPath);
        if(!file.is_open()) {
            Common::Logger::Error(lh, "failed to open config file: {}", params.configPath);
            return false;
        }
        std::stringstream buf;
        buf << file.rdbuf();
        Json::JsonIssues issues;
        if(!ParseServerConfig(buf.str(), config, issues)) {
            Common::Logger::Error(lh, "\nfailed to parse config:\n{}", issues.Format());
            return false;
        }

        Json::JsonIssues writeIssues;
        if(std::optional<std::string> text = SerializeServerConfig(config, writeIssues); text.has_value()) {
            Common::Logger::Info(lh, "using this configuration:\n{}", text.value());
        }
        else {
            Common::Logger::Error(lh, "could not write the configuration back:\n{}", writeIssues.Format());
        }

        Common::Logger::Info(lh, "validating configuration..");
        CommandError error = CheckValidity(lh, SCG::ValidateConfig(config), "configuration");
        if(!error.empty()) {
            return false;
        }
        Common::Logger::Info(lh, "validation successful");

        scheduler.Configure(config, pipelineConfig);
        scheduler.onStatus = [this](const std::string &testId, TestStatusMessage::TestStatus status, std::chrono::system_clock::time_point time) {
            TestStatusMessage message;
            message.timestamp = FormatIsoTime(time);
            message.testId = testId;
            message.testStatus = status;
            Json::JsonIssues issues;
            Publish(config.mqtt.testLogTopic, testStatusQos, SerializeTestStatusMessage(message, issues), issues);
        };
        scheduler.onError = [this](const std::string &testId, const std::string &text) {
            PublishLog(SessionLogMessage::Severity::error, testId, text);
        };

        try {
            Configure();
        }
        catch(const std::exception &e) {
            Common::Logger::Error(lh, "\nfailed to initialize the radios:\n{}", e.what());
            return false;
        }

        return true;
    }

    void Server::Run() {
        Common::Defer disposeClient([this](){
            client.Stop();
        });
        Common::Defer disposeRadioMonitor([this](){
            radioMonitor.Stop();
        });
        Common::Defer disposeRunning([this](){
            StopActive();
        });

        radioMonitor.onNotReady = [this]() {
            {
                std::unique_lock lck(mtx);
                radiosTurnedNotReady = true;
            }
            cv.notify_all();
        };
        radioMonitor.Start(threadPool, config, pipelineConfig);

        client.Start();
        if(!client.IsRunning()) {
            Common::Logger::Error(lh, "could not start the mqtt client, stopping");
            return;
        }

        scheduler.Restore();

        Common::Logger::Info(lh, "server running.. ctrl+c to stop");

        statusPeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(config.statusUpdatePeriod_s)
        );
        nextSignalStatus = std::chrono::steady_clock::now();
        while(true) {
            Wait();
            if(StopRequested()) {
                break;
            }

            UpdateRadios();
            HandleCommands();
            UpdateSchedule();
            ApplySignalEvents();
            PublishSignalStatus();
        }
    }

    int Server::Main(Parameters params) {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .colors = true,
            .file = false,
            .function = false
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){
            Common::Logger::RemoveStream(&logStream);
        });

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
            Common::Logger::Warn(lh, "could not open scgserver.log, logging to the console only");
        }

        // has to exist before any thread starts, on linux a thread inherits the signal mask
        // the callback runs on an ordinary thread
        SCG::SignalHandler signalHandler({ SIGINT, SIGTERM }, [this](int sig){
            Common::Logger::Info(lh, "signal {} received, stopping", sig);
            {
                std::unique_lock lck(mtx);
                programRunning = false;
            }
            cv.notify_all();
        });

        // disposed after Run has stopped the pipeline and the preparation
        threadPool.Init(4);
        Common::Defer disposeThreadPool([this](){
            threadPool.Dispose();
        });

        Common::Defer releaseRadios([this](){
            ReleaseRadios();
        });

        if(!params.success) {
            Common::Logger::Error(lh, "\nfailed to parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info(lh, "\n{}", params.help.c_str());
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
            Common::Logger::Error(lh, "\nerror during setup:\n{}", e.what());
        }

        if(!setupSuccess) {
            Common::Logger::Info(lh, "unsuccessful setup, stopping");
            return 1;
        }

        try {
            ConfigureMqtt();
        }
        catch(const std::exception &e) {
            Common::Logger::Error(lh, "\nfailed to configure the mqtt client:\n{}", e.what());
            return 1;
        }

        if(StopRequested()) {
            Common::Logger::Info(lh, "stopped before the server started");
            return 0;
        }

        try {
            Run();
        }
        catch(const std::exception &e) {
            Common::Logger::Error(lh, "\nerror during run:\n{}", e.what());
            return 1;
        }
        return 0;
    }

}
