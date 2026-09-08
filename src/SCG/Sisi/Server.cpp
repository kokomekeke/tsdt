#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <set>
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

// an empty string means the step succeeded. the steps below and the command handlers all
// return one of these, the caller at the boundary publishes it. an exception from here on
// means the code did something it was not supposed to, not that the input was bad
using CommandError = std::string;

// runs the step and hands its error up to the caller if it produced one
#define SCG_CHECK(expr)                 \
    do {                                \
        CommandError err_ = (expr);     \
        if(!err_.empty()) {             \
            return err_;                \
        }                               \
    } while(false)

// the warnings and the errors are logged one by one, the first error is handed back
static CommandError CheckValidity(const SCG::ValidationResult &validity, const char *what) {
    for(auto &s : validity.warnings) {
        Common::Logger::Warn("{}", s.c_str());
    }
    if(validity.errors.empty()) {
        return {};
    }
    for(auto &s : validity.errors) {
        Common::Logger::Error("{}", s.c_str());
    }
    return std::format("{} validation failed: {}", what, validity.errors.front());
}

// reads n digits starting at i into out and steps i past them
static bool ReadDigits(const std::string &s, std::size_t &i, std::size_t n, int &out) {
    if(i + n > s.size()) {
        return false;
    }
    int value = 0;
    for(std::size_t k = 0; k < n; ++k) {
        char c = s[i + k];
        if(c < '0' || c > '9') {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    i += n;
    out = value;
    return true;
}

static bool ReadLiteral(const std::string &s, std::size_t &i, char c) {
    if(i >= s.size() || s[i] != c) {
        return false;
    }
    ++i;
    return true;
}

// the format is 2026-09-02T14:30:00+02:00. std::chrono::parse is not in every standard
// library this builds against, so the fields are read out one at a time. the trailing
// offset is how far the written time is ahead of utc, so it is subtracted to get the
// instant. a Z stands for a zero offset and the colon inside the offset is optional
static CommandError ParseStartTime(const std::string &s, std::chrono::system_clock::time_point &out) {
    std::size_t i = 0;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;

    bool fields =
        ReadDigits(s, i, 4, year) && ReadLiteral(s, i, '-') &&
        ReadDigits(s, i, 2, month) && ReadLiteral(s, i, '-') &&
        ReadDigits(s, i, 2, day) && ReadLiteral(s, i, 'T') &&
        ReadDigits(s, i, 2, hour) && ReadLiteral(s, i, ':') &&
        ReadDigits(s, i, 2, minute) && ReadLiteral(s, i, ':') &&
        ReadDigits(s, i, 2, second);
    if(!fields) {
        return std::format("could not parse starting time: \"{}\"", s);
    }

    std::chrono::minutes offset{0};
    if(i < s.size() && (s[i] == 'Z' || s[i] == 'z')) {
        ++i;
    }
    else if(i < s.size() && (s[i] == '+' || s[i] == '-')) {
        int sign = s[i] == '-' ? -1 : 1;
        ++i;
        int offsetHour = 0;
        int offsetMinute = 0;
        if(!ReadDigits(s, i, 2, offsetHour)) {
            return std::format("could not parse starting time: \"{}\"", s);
        }
        ReadLiteral(s, i, ':');
        if(!ReadDigits(s, i, 2, offsetMinute)) {
            return std::format("could not parse starting time: \"{}\"", s);
        }
        offset = sign * (std::chrono::hours(offsetHour) + std::chrono::minutes(offsetMinute));
    }
    else {
        return std::format("starting time has no utc offset: \"{}\"", s);
    }
    if(i != s.size()) {
        return std::format("could not parse starting time: \"{}\"", s);
    }

    std::chrono::year_month_day date{
        std::chrono::year(year),
        std::chrono::month(unsigned(month)),
        std::chrono::day(unsigned(day))
    };
    if(!date.ok()) {
        return std::format("starting time is not a valid date: \"{}\"", s);
    }
    if(hour > 23 || minute > 59 || second > 59) {
        return std::format("starting time is not a valid time of day: \"{}\"", s);
    }

    out = std::chrono::sys_days(date)
        + std::chrono::hours(hour)
        + std::chrono::minutes(minute)
        + std::chrono::seconds(second)
        - offset;

    if(out <= std::chrono::system_clock::now()) {
        return std::format("starting time is in the past: \"{}\"", s);
    }
    return {};
}

// the id names a directory under the configured recording directory
static CommandError CheckTestId(const std::string &testId) {
    if(testId.empty()
        || testId == "."
        || testId == ".."
        || testId.find_first_of("/\\:*?\"<>|") != std::string::npos
    ) {
        return std::format("test id cannot be used as a directory name: \"{}\"", testId);
    }
    return {};
}

// the scenario is read from a local file, this becomes an http request later
static CommandError FetchScenario(const std::string &location, std::string &out) {
    std::filesystem::path path;
    try {
        path = std::filesystem::absolute(std::filesystem::path(location));
    }
    catch(const std::exception &e) {
        return std::format("invalid scenario file path: \"{}\": {}", location, e.what());
    }
    std::ifstream file(path);
    if(!file.is_open()) {
        return std::format("failed to open scenario file: {}", path.string());
    }
    std::stringstream buf;
    buf << file.rdbuf();
    out = buf.str();
    return {};
}

// the parsers throw, so the conversion to a returned error happens here at the edge
static CommandError ReadRid(const std::string &json, SCG::Rid &out) {
    try {
        out = SCG::ParseRid(json);
    }
    catch(const std::exception &e) {
        return std::format("failed to parse RID: {}", e.what());
    }
    return {};
}

static CommandError ReadCommand(const std::string &payload, CommandMessage &out) {
    try {
        out = SCG::ParseCommandMessage(payload);
    }
    catch(const std::exception &e) {
        return std::format("could not parse command: {}", e.what());
    }
    return {};
}

// the incoming commands and the schedule are checked at this rate. it is the upper
// bound on how late a step runs, the status period is layered on top of it
constexpr std::chrono::milliseconds scheduleTick{50};

// a signal handler may only write a flag of this type. the handler runs on one of the
// existing threads, so locking a mutex or notifying a condition variable from it would
// deadlock against the thread it interrupted. the main loop already wakes on every tick,
// so it polls this instead of being notified
volatile std::sig_atomic_t g_stopRequested = 0;

struct ScheduledTest {
    // pending: accepted, waiting for the recording pipeline
    // recording: the recording pipeline is running for it
    // ready: the recording is on disk
    // playing: the playback pipeline is running for it
    // done, failed: waiting to be removed from the list
    enum struct State {
        pending,
        recording,
        ready,
        playing,
        done,
        failed
    };

    std::string testId;
    std::chrono::system_clock::time_point time;
    SCG::Rid rid;
    SCG::Scenario scenario;
    SCG::ScenarioRidMap scenarioMap;
    std::string recordingDirectory;
    State state = State::pending;
};

struct Program {
private:
    static Program instance;

    std::mutex mtx;
    std::condition_variable cv;

    ProgramParser parser;
    MqttClient client;
    SCG::ServerConfig config;
    SCG::PipelineConfig pipelineConfig;

    // the tests are held by pointer so that testRunning stays valid while the list is
    // inserted into and erased from
    std::vector<std::unique_ptr<ScheduledTest>> tests;
    ScheduledTest *testRunning = nullptr;
    SCG::ScenarioRunnerContext ctx;

    // the event callbacks run on the sink module thread while it holds its own mutex.
    // the controller takes those same module mutexes while stopping, so the callbacks
    // take mtxStatus only and never mtx
    std::mutex mtxStatus;
    std::vector<std::int32_t> statusEventMap;
    std::vector<Rid::InterferenceSource> statusSources;
    std::set<std::int32_t> activeIfsIds;

    // only the main loop touches these. the payload of the last publish is kept so that
    // a change can be detected, nextStatus is when the same payload is sent again
    std::string lastStatusPayload;
    std::chrono::steady_clock::duration statusPeriod{};
    std::chrono::steady_clock::time_point nextStatus;

    void Configure() {
        pipelineConfig = SCG::PipelineConfig{};
        pipelineConfig.mockRadios = config.mockRadios;
        // a mock device is built from the scenario alone, so nothing is opened for one.
        // the radio types are checked in ValidateConfig, which runs before this
#ifdef HAVE_USRP
        pipelineConfig.preinit.usrp.usePreinitOnly = true;
        if(!config.mockRadios) {
            for(auto &v : config.radios) {
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
                if(std::holds_alternative<SCG::ServerConfig::Radio::Sidekiq>(v.variant)) {
                    auto &w = std::get<SCG::ServerConfig::Radio::Sidekiq>(v.variant);
                    // libsidekiq holds the card state, so it is brought up before any
                    // device is opened. the call does nothing after the first one
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

    void SetEventActive(std::int32_t eventId, bool active) {
        {
            std::unique_lock lck(mtxStatus);
            if(eventId < 0 || std::size_t(eventId) >= statusEventMap.size()) {
                return;
            }
            std::int32_t ifsId = statusEventMap[eventId];
            if(active) {
                activeIfsIds.insert(ifsId);
            }
            else {
                activeIfsIds.erase(ifsId);
            }
        }
        cv.notify_all();
    }

    void SetStatusSource(const ScheduledTest &test) {
        std::unique_lock lck(mtxStatus);
        statusEventMap = test.scenarioMap.eventMap;
        statusSources = test.rid.interferenceSources;
        activeIfsIds.clear();
    }

    void ClearStatusSource() {
        std::unique_lock lck(mtxStatus);
        statusEventMap.clear();
        statusSources.clear();
        activeIfsIds.clear();
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
        Publish(config.mqtt.errorTopic, SerializeErrorMessage(message));
    }

    void PublishStatus() {
        StatusMessage message;
        {
            std::unique_lock lck(mtxStatus);
            for(std::int32_t ifsId : activeIfsIds) {
                if(ifsId >= 0 && std::size_t(ifsId) < statusSources.size()) {
                    message.active.push_back(statusSources[ifsId]);
                }
            }
        }

        // the payload is compared instead of flagging every place that changes the
        // status. a completed mqtt publish does not change it, which is what stops the
        // client wakeups from feeding back into another publish
        std::string payload = SerializeStatusMessage(message);
        auto now = std::chrono::steady_clock::now();
        if(payload == lastStatusPayload && now < nextStatus) {
            return;
        }
        lastStatusPayload = payload;
        nextStatus = now + statusPeriod;
        Publish(config.mqtt.logTopic, payload);
    }

    CommandError ScheduleTest(const CommandMessage::TestSetup &command) {
        std::chrono::system_clock::time_point time;
        SCG_CHECK(ParseStartTime(command.time, time));
        SCG_CHECK(CheckTestId(command.testId));
        for(auto &t : tests) {
            if(t->testId == command.testId) {
                return std::format("test id already scheduled: \"{}\"", command.testId);
            }
        }

        std::string json;
        SCG_CHECK(FetchScenario(command.scenario, json));

        SCG::Rid rid;
        SCG_CHECK(ReadRid(json, rid));

        Common::Logger::Info("validating RID..");
        SCG_CHECK(CheckValidity(SCG::ValidateRid(config, rid), "RID"));
        Common::Logger::Info("validation successful");

        auto scenarioMap = SCG::MapRidToScenario(config, rid);
        auto scenario = scenarioMap.scen;

        Common::Logger::Info("validating scenario..");
        SCG_CHECK(CheckValidity(SCG::ValidateScenario(scenario), "scenario"));
        Common::Logger::Info("validation successful");

        auto test = std::make_unique<ScheduledTest>();
        test->testId = command.testId;
        test->time = time;
        test->recordingDirectory = std::format("{}/{}", config.recordingDir, command.testId);
        test->rid = std::move(rid);
        test->scenario = std::move(scenario);
        test->scenarioMap = std::move(scenarioMap);
        test->state = ScheduledTest::State::pending;

        // TODO handle problems with time overlaps
        tests.push_back(std::move(test));
        Common::Logger::Info("test scheduled: {}", command.testId);
        return {};
    }

    CommandError CancelTest(const CommandMessage::TestCancel &command) {
        auto it = std::find_if(tests.begin(), tests.end(), [&](const std::unique_ptr<ScheduledTest> &t) {
            return t->testId == command.testId;
        });
        if(it == tests.end()) {
            return std::format("unknown test id: \"{}\"", command.testId);
        }
        if(testRunning == it->get()) {
            StopPipeline();
        }
        tests.erase(it);
        Common::Logger::Info("test cancelled: {}", command.testId);
        return {};
    }

    void StartPipeline(ScheduledTest &test) {
        Common::Assert(testRunning == nullptr, "a pipeline is already running");
        bool isPlayback = (test.state == ScheduledTest::State::ready);

        ctx = SCG::ScenarioRunnerContext{};
        try {
            if(isPlayback) {
                pipelineConfig.events.startTime = test.time;
                auto b = SCG::BuildPlaybackPipeline(pipelineConfig, test.recordingDirectory);
                ctx.pipeline = b.builder.Build();
                ctx.resources = b.resources;
                ctx.scenario = b.dir->GetScenario();
                ctx.ownedResources.emplace_back(std::move(b.dir));
            }
            else {
                pipelineConfig.events.startTime = std::nullopt;
                auto b = SCG::BuildRecordingPipeline(pipelineConfig, test.scenario, test.recordingDirectory);
                ctx.pipeline = b.builder.Build();
                ctx.resources = b.resources;
                ctx.scenario = test.scenario;
                ctx.ownedResources.emplace_back(std::move(b.dir));
            }
        }
        catch(const std::exception &e) {
            PublishError(std::format("could not build pipeline for \"{}\": {}", test.testId, e.what()));
            test.state = ScheduledTest::State::failed;
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
        catch(const std::exception &e) {
            PublishError(std::format("could not initialize resources for \"{}\": {}", test.testId, e.what()));
            ctx.Dispose();
            ctx.controller.reset();
            test.state = ScheduledTest::State::failed;
            return;
        }

        if(isPlayback) {
            SetStatusSource(test);
            test.state = ScheduledTest::State::playing;
        }
        else {
            test.state = ScheduledTest::State::recording;
        }
        testRunning = &test;
        ctx.controller->Start();
        Common::Logger::Info("{} started: {}", isPlayback ? "playback" : "recording", test.testId);
    }

    // takes the pipeline down and leaves the test in the state it had
    void StopPipeline() {
        if(testRunning == nullptr) {
            return;
        }
        try {
            ctx.controller->Stop(true);
            ctx.controller->Join();
            ctx.Dispose();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror while stopping pipeline:\n{}", e.what());
        }
        ctx.controller.reset();
        testRunning = nullptr;
        ClearStatusSource();
        Common::Logger::Info("pipeline stopped");
    }

    // called once the controller has left the running state on its own
    void FinishPipeline() {
        ScheduledTest &test = *testRunning;
        bool failed = false;

        ctx.controller->Join();
        try {
            ctx.controller->Throw();
        }
        catch(const std::exception &e) {
            PublishError(std::format("error running pipeline for \"{}\": {}", test.testId, e.what()));
            failed = true;
        }
        try {
            ctx.Dispose();
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror while disposing resources:\n{}", e.what());
        }
        ctx.controller.reset();
        testRunning = nullptr;
        ClearStatusSource();

        if(failed) {
            test.state = ScheduledTest::State::failed;
        }
        else if(test.state == ScheduledTest::State::recording) {
            test.state = ScheduledTest::State::ready;
            Common::Logger::Info("recording finished: {}", test.testId);
        }
        else {
            test.state = ScheduledTest::State::done;
            Common::Logger::Info("playback finished: {}", test.testId);
        }
    }

    void UpdateSchedule() {
        auto now = std::chrono::system_clock::now();
        auto startAhead = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(config.startAhead_s)
        );

        if(testRunning != nullptr) {
            if(ctx.controller->GetState() != Sagax::Pipeline::Controller::RunState::running) {
                FinishPipeline();
            }
        }

        // the playback is started startAhead early so the pipeline queues fill up, the
        // sink holds the first packet back until the exact time. the tick is added so
        // that the start happens on the last tick before that point rather than after it
        for(auto &t : tests) {
            ScheduledTest &test = *t;
            if(test.state != ScheduledTest::State::ready) {
                continue;
            }
            if(now + scheduleTick < test.time - startAhead) {
                continue;
            }
            if(now >= test.time) {
                PublishError(std::format("test \"{}\" missed its starting time", test.testId));
                test.state = ScheduledTest::State::failed;
                continue;
            }
            if(testRunning != nullptr) {
                PublishError(std::format("test \"{}\" cannot start, another pipeline is running", test.testId));
                test.state = ScheduledTest::State::failed;
                continue;
            }
            StartPipeline(test);
        }

        if(testRunning == nullptr) {
            for(auto &t : tests) {
                if(t->state == ScheduledTest::State::pending) {
                    StartPipeline(*t);
                    break;
                }
            }
        }

        std::erase_if(tests, [](const std::unique_ptr<ScheduledTest> &t) {
            return t->state == ScheduledTest::State::done
                || t->state == ScheduledTest::State::failed;
        });
    }

    void HandleCommands() {
        MqttClient::Message msg;
        while(client.Pop(msg)) {
            std::string payload(msg.bytes.begin(), msg.bytes.end());
            CommandMessage cm;
            CommandError error = ReadCommand(payload, cm);
            if(!error.empty()) {
                Common::Logger::Error("received [{}]: bad payload: \"{}\"", msg.topic, payload);
            }
            else if(std::holds_alternative<CommandMessage::TestSetup>(cm.variant)) {
                auto &v = std::get<CommandMessage::TestSetup>(cm.variant);
                Common::Logger::Info("received [{}] TestSetup: scenario={} time={} test_id={}", msg.topic, v.scenario, v.time, v.testId);
                error = ScheduleTest(v);
            }
            else if(std::holds_alternative<CommandMessage::TestCancel>(cm.variant)) {
                auto &v = std::get<CommandMessage::TestCancel>(cm.variant);
                Common::Logger::Info("received [{}] TestCancel: test_id={}", msg.topic, v.testId);
                error = CancelTest(v);
            }

            if(!error.empty()) {
                PublishError(error);
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
                this->cv.notify_all();
            };
            client.Configure(std::move(mqttConfig));
            client.Start();
        }

        std::signal(SIGINT, [](int){
            g_stopRequested = 1;
        });
        Common::Logger::Info("server running.. ctrl+c to stop");

        statusPeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(config.statusUpdatePeriod_s)
        );
        nextStatus = std::chrono::steady_clock::now();
        while(true) {
            // mtx is held for the wait only, the steps below reach the module mutexes
            // through the controller and must not hold it. the wait also returns early
            // when the mqtt client or an event callback notifies
            {
                std::unique_lock lck(mtx);
                cv.wait_for(lck, scheduleTick);
            }
            if(g_stopRequested != 0) {
                break;
            }

            HandleCommands();
            UpdateSchedule();
            PublishStatus();
        }

        StopPipeline();
        client.Stop();
        MqttClient::LibraryCleanup();
#ifdef HAVE_SIDEKIQ
        // the devices have to be released before the library that handed them out
        pipelineConfig.preinit.sidekiq.devices.clear();
        SSG::Sidekiq::LibraryCleanup();
#endif
    }

public:

    static Program &GetInstance() {
        return instance;
    }

    void Main(int argc, char **argv) {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .file = true,
            .function = false
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&](){
            Common::Logger::RemoveStream(&logStream);
        });

        // the same records without the colour escape sequences. the file is opened in the
        // working directory and is replaced on every run
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

        Parameters params{};
        try {
            parser.Parse(argc, argv);
            params = parser.params;
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nfailed to parse parameters:\n{}", e.what());
            return;
        }
        if(!params.success) {
            Common::Logger::Error("\nfailed to parse parameters:\n{}", params.error.c_str());
            Common::Logger::Info("\n{}", parser.Help().c_str());
            return;
        }

        try {
            if(Setup(params)) {
                Run();
            }
        }
        catch(const std::exception &e) {
            Common::Logger::Error("\nerror during run:\n{}", e.what());
        }
    }

};
Program Program::instance;

int main(int argc, char **argv) {
    try {
        Program::GetInstance().Main(argc, argv);
    }
    catch(const std::exception &) {
        // cant even log here, so just forget about it
    }
    return 0;
}
