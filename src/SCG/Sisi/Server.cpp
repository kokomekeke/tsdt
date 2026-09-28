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
#include "Vendored/argparse/argparse.hpp"
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
#include "MqttClient.hpp"
#include "RidTransform.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

struct Parameters {
    bool success;
    // --help and --version write their text and end the run, there is nothing else to do
    bool helpRequested;
    std::string error;
    std::string configPath;
};

struct ProgramParser {
    Parameters params;
    argparse::ArgumentParser program;

    // exit_on_default_arguments is off so that --help and --version come back here
    // instead of calling std::exit, which would skip every destructor on the way out
    ProgramParser() : program("scgserver.exe", "1.0", argparse::default_arguments::all, false) {
        program.add_argument("config_path")
            .help("path to configuration .json");
    }

    std::string Help() {
        return program.help().str();
    }

    void Parse(int argc, char **argv) {
        params.helpRequested = false;

        std::string parseError;
        try {
            program.parse_args(argc, argv);
            params.configPath = program.get<std::string>("config_path");
        }
        catch(const std::exception &e) {
            parseError = e.what();
        }

        // the help text is written while the arguments are read and the missing ones are
        // only reported after that, so the request is answered before the error is looked at
        if(program.is_used("--help") || program.is_used("--version")) {
            params.helpRequested = true;
            params.success = true;
            return;
        }
        if(!parseError.empty()) {
            params.success = false;
            params.error = parseError;
            return;
        }

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

// the format is 2026-09-02T14:30:00.000+02:00, the fraction of a second is optional and
// can have any number of digits. std::chrono::parse is not in every standard
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

    // digits past nanoseconds are read and dropped
    std::chrono::nanoseconds fraction{0};
    if(ReadLiteral(s, i, '.')) {
        std::size_t iFraction = i;
        std::int64_t scale = 100000000;
        while(i < s.size() && s[i] >= '0' && s[i] <= '9') {
            fraction += std::chrono::nanoseconds((s[i] - '0') * scale);
            scale /= 10;
            ++i;
        }
        if(i == iFraction) {
            return std::format("could not parse starting time: \"{}\"", s);
        }
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
        + std::chrono::duration_cast<std::chrono::system_clock::duration>(fraction)
        - offset;

    if(out <= std::chrono::system_clock::now()) {
        return std::format("starting time is in the past: \"{}\"", s);
    }
    return {};
}

// utc to the second, for example 2026-09-02T12:30:00Z
static std::string FormatIsoTime(std::chrono::system_clock::time_point t) {
    auto seconds = std::chrono::floor<std::chrono::seconds>(t);
    auto days = std::chrono::floor<std::chrono::days>(seconds);
    std::chrono::year_month_day date{days};
    std::chrono::hh_mm_ss<std::chrono::seconds> timeOfDay{seconds - days};

    return std::format(
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",
        int(date.year()),
        unsigned(date.month()),
        unsigned(date.day()),
        timeOfDay.hours().count(),
        timeOfDay.minutes().count(),
        timeOfDay.seconds().count()
    );
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

static CommandError MapRid(
    const SCG::ServerConfig &cfg,
    const SCG::Rid &rid,
    std::chrono::system_clock::time_point startTime,
    const std::string &directory,
    SCG::ScenarioRidMap &out
) {
    try {
        out = SCG::MapRidToScenario(cfg, rid, startTime, directory);
    }
    catch(const std::exception &e) {
        return std::format("failed to build a scenario from the RID: {}", e.what());
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

static std::chrono::system_clock::duration Seconds(double seconds) {
    return std::chrono::duration_cast<std::chrono::system_clock::duration>(
        std::chrono::duration<double>(seconds)
    );
}

// the incoming commands and the schedule are checked at this rate. it is the upper
// bound on how late a step runs, the status period is layered on top of it
constexpr std::chrono::milliseconds scheduleTick{50};

struct ScheduledTest {
    // preparing: the directory is set up and the jobs run
    // done, failed: erased from the list at the end of the UpdateSchedule that set them
    enum struct State {
        pending,
        preparing,
        ready,
        playing,
        done,
        failed
    };

    std::string testId;
    std::chrono::system_clock::time_point time;
    SCG::Rid rid;
    // sink event id -> index into rid.interferenceSources
    std::vector<std::int32_t> eventMap;
    SCG::Scenario scenario;
    std::vector<std::unique_ptr<SCG::Job>> jobs;
    std::string directory;
    State state = State::pending;
};

static double JobEstimate_s(const ScheduledTest &test) {
    double total = 0.0;
    for(auto &job : test.jobs) {
        total += job->Estimate_s();
    }
    return total;
}

struct Program {
private:
    static Program instance;

    // cleared by the signal callback, which runs on an ordinary thread and can take mtx.
    // it starts true so that a signal arriving before the loop is entered still stops it
    bool programRunning = true;
    std::mutex mtx;
    std::condition_variable cv;

    ProgramParser parser;
    MqttClient client;
    SCG::ServerConfig config;
    SCG::PipelineConfig pipelineConfig;

    // the tests are held by pointer so that testRunning stays valid while the list is
    // inserted into and erased from. testRunning is the test being prepared or played,
    // there is at most one
    std::vector<std::unique_ptr<ScheduledTest>> tests;
    ScheduledTest *testRunning = nullptr;

    // prepareError is written before prepareDone is set. the thread only reads the test,
    // and it is stopped before the test is erased. it is declared after the members it
    // uses, so it is joined before they are destroyed
    std::atomic<bool> prepareDone = false;
    std::string prepareError;
    std::jthread prepareThread;

    // the controller dispatches its workers here. it is declared before ctx so that it
    // is still alive when the controller ctx holds goes away. the pool grows on demand,
    // the count is only the number of threads it keeps around
    Common::ThreadPool threadPool{4};
    SCG::ScenarioRunnerContext ctx;

    // the event callbacks run on the sink module thread while it holds its own mutex.
    // the controller takes those same module mutexes while stopping, so the callbacks
    // take mtxSignal only and never mtx. the sources are only set during a playback,
    // events outside of one are dropped
    std::mutex mtxSignal;
    std::vector<std::int32_t> signalEventMap;
    std::vector<SignalStatusMessage::SignalStatus> signalSources;
    std::set<std::int32_t> activeEventIds;

    // only the main loop touches these
    std::optional<SignalStatusMessage::SignalStatus> lastSignalStatus;
    std::chrono::steady_clock::duration statusPeriod{};
    std::chrono::steady_clock::time_point nextSignalStatus;

    void Configure() {
        pipelineConfig = SCG::PipelineConfig{};
        pipelineConfig.mockRadios = config.mockRadios;
        // a mock device is built from the scenario alone, so nothing is opened for one.
        // the radio types are checked in ValidateConfig, which runs before this
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

    // tracked per event, a source can have more than one
    void SetEventActive(std::int32_t eventId, bool active) {
        {
            std::unique_lock lck(mtxSignal);
            if(eventId < 0 || std::size_t(eventId) >= signalEventMap.size()) {
                return;
            }
            if(active) {
                activeEventIds.insert(eventId);
            }
            else {
                activeEventIds.erase(eventId);
            }
        }
        cv.notify_all();
    }

    static SignalStatusMessage::SignalStatus SourceKind(const SCG::Rid::InterferenceSource &source) {
        if(std::holds_alternative<SCG::Rid::InterferenceSource::Spoofing>(source.variant)) {
            return SignalStatusMessage::SignalStatus::spoofing;
        }
        return SignalStatusMessage::SignalStatus::jamming;
    }

    void SetSignalSources(const ScheduledTest &test) {
        std::unique_lock lck(mtxSignal);
        signalEventMap = test.eventMap;
        signalSources.clear();
        for(auto &source : test.rid.interferenceSources) {
            signalSources.push_back(SourceKind(source));
        }
        activeEventIds.clear();
    }

    void ClearSignalSources() {
        std::unique_lock lck(mtxSignal);
        signalEventMap.clear();
        signalSources.clear();
        activeEventIds.clear();
    }

    SignalStatusMessage::SignalStatus CurrentSignalStatus() {
        std::unique_lock lck(mtxSignal);
        auto status = SignalStatusMessage::SignalStatus::none;
        for(std::int32_t eventId : activeEventIds) {
            std::int32_t sourceId = signalEventMap[eventId];
            if(sourceId < 0 || std::size_t(sourceId) >= signalSources.size()) {
                continue;
            }
            if(signalSources[sourceId] == SignalStatusMessage::SignalStatus::spoofing) {
                return SignalStatusMessage::SignalStatus::spoofing;
            }
            status = SignalStatusMessage::SignalStatus::jamming;
        }
        return status;
    }

    // the pipeline holds the preinitialized devices while it runs, so this is only
    // reached once it is down
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

    static TestStatusMessage::TestStatus ReportedStatus(ScheduledTest::State state) {
        switch(state) {
            case ScheduledTest::State::pending:   return TestStatusMessage::TestStatus::TEST_INIT;
            case ScheduledTest::State::preparing: return TestStatusMessage::TestStatus::TEST_PREPARE;
            case ScheduledTest::State::ready:     return TestStatusMessage::TestStatus::TEST_READY;
            case ScheduledTest::State::playing:   return TestStatusMessage::TestStatus::TEST_RUNNING;
            case ScheduledTest::State::done:      return TestStatusMessage::TestStatus::TEST_COMPLETED;
            case ScheduledTest::State::failed:    return TestStatusMessage::TestStatus::TEST_FAILED;
        }
        Common::Assert(false, "unhandled test state");
        return TestStatusMessage::TestStatus::TEST_FAILED;
    }

    void PublishTestStatus(const ScheduledTest &test) {
        TestStatusMessage message;
        message.timestamp = FormatIsoTime(std::chrono::system_clock::now());
        message.testId = test.testId;
        message.status = ReportedStatus(test.state);
        Publish(config.mqtt.testLogTopic, SerializeTestStatusMessage(message));
    }

    // every state change after the test is scheduled goes through here
    void SetState(ScheduledTest &test, ScheduledTest::State state) {
        if(test.state == state) {
            return;
        }
        test.state = state;
        PublishTestStatus(test);
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

    // a playback holds the pipeline from start_ahead before its time until the scenario
    // has run out, and only one pipeline runs at a time. finished tests are already
    // erased when this runs
    CommandError CheckOverlap(const std::string &testId, std::chrono::system_clock::time_point time, double duration) {
        auto startAhead = Seconds(config.startAhead_s);

        auto begin = time - startAhead;
        auto end = time + Seconds(duration);
        for(auto &t : tests) {
            auto otherBegin = t->time - startAhead;
            auto otherEnd = t->time + Seconds(t->scenario.duration);
            if(begin < otherEnd && otherBegin < end) {
                double overlap_s = std::chrono::duration<double>(
                    std::min(end, otherEnd) - std::max(begin, otherBegin)
                ).count();
                return std::format(
                    "test \"{}\" overlaps test \"{}\" by {:.1f} s",
                    testId, t->testId, overlap_s
                );
            }
        }
        return {};
    }

    // the tests are prepared one at a time and not during a playback, so every
    // preparation has to finish before the earliest playback starts. a preparation that
    // is already running is counted whole
    CommandError CheckPreparationQueue(const std::string &testId, std::chrono::system_clock::time_point time, double work) {
        auto startAhead = Seconds(config.startAhead_s);

        auto earliest = time - startAhead;
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::pending || t->state == ScheduledTest::State::preparing) {
                work += JobEstimate_s(*t);
            }
            earliest = std::min(earliest, t->time - startAhead);
        }

        auto queueEnd = std::chrono::system_clock::now() + Seconds(work);
        if(queueEnd > earliest) {
            double late_s = std::chrono::duration<double>(queueEnd - earliest).count();
            return std::format(
                "test \"{}\" cannot be prepared in time, the preparation queue would still be running "
                "{:.1f} s after the first playback starts",
                testId, late_s
            );
        }
        return {};
    }

    CommandError ScheduleTest(const CommandMessage::TestSetup &command) {
        std::chrono::system_clock::time_point time;
        SCG_CHECK(ParseStartTime(command.startTime, time));
        SCG_CHECK(CheckTestId(command.testId));
        for(auto &t : tests) {
            if(t->testId == command.testId) {
                return std::format("test id already scheduled: \"{}\"", command.testId);
            }
        }

        std::string json;
        SCG_CHECK(SCG::Fetch(command.scenarioUrl, config.scenarioFetchTimeout_s, json));

        SCG::Rid rid;
        SCG_CHECK(ReadRid(json, rid));

        Common::Logger::Info("validating RID..");
        SCG_CHECK(CheckValidity(SCG::ValidateRid(config, rid), "RID"));
        Common::Logger::Info("validation successful");

        std::string directory = (
            std::filesystem::absolute(config.recordingDir) / command.testId
        ).string();

        SCG::ScenarioRidMap scenarioMap;
        SCG_CHECK(MapRid(config, rid, time, directory, scenarioMap));
        auto scenario = scenarioMap.scen;

        Common::Logger::Info("validating scenario..");
        SCG_CHECK(CheckValidity(SCG::ValidateScenario(scenario), "scenario"));
        Common::Logger::Info("validation successful");

        auto test = std::make_unique<ScheduledTest>();
        test->testId = command.testId;
        test->time = time;
        test->directory = std::move(directory);
        test->rid = std::move(rid);
        test->eventMap = std::move(scenarioMap.eventMap);
        test->jobs = std::move(scenarioMap.jobs);
        test->scenario = std::move(scenario);
        test->state = ScheduledTest::State::pending;

        SCG_CHECK(CheckOverlap(command.testId, time, test->scenario.duration));
        SCG_CHECK(CheckPreparationQueue(command.testId, time, JobEstimate_s(*test)));

        PublishTestStatus(*test);
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
            StopRunning();
        }
        RemoveDirectory(**it);
        SetState(**it, ScheduledTest::State::failed);
        tests.erase(it);
        Common::Logger::Info("test cancelled: {}", command.testId);
        return {};
    }

    void RemoveDirectory(const ScheduledTest &test) {
        std::error_code ec;
        std::filesystem::remove_all(test.directory, ec);
        if(ec) {
            Common::Logger::Warn("could not remove {}: {}", test.directory, ec.message());
        }
    }

    // runs on prepareThread
    void Prepare(const ScheduledTest &test, std::stop_token stop) {
        std::filesystem::remove_all(test.directory);
        std::filesystem::create_directories(test.directory);
        {
            std::ofstream file(std::filesystem::path(test.directory) / "scenario.json");
            file << SerializeScenario(test.scenario);
            Common::Assert(bool(file), std::format("could not write the scenario into {}", test.directory));
        }

        for(auto &job : test.jobs) {
            if(stop.stop_requested()) {
                return;
            }
            Common::Logger::Info("running {}", job->Describe());
            job->Run(stop);
        }
    }

    void StartPreparation(ScheduledTest &test) {
        Common::Assert(testRunning == nullptr, "a test is already running");
        prepareDone = false;
        prepareError.clear();
        testRunning = &test;
        SetState(test, ScheduledTest::State::preparing);
        Common::Logger::Info("preparation started: {}", test.testId);

        prepareThread = std::jthread([this, &test](std::stop_token stop) {
            std::string error;
            try {
                Prepare(test, stop);
            }
            catch(const std::exception &e) {
                error = e.what();
            }
            prepareError = std::move(error);
            prepareDone = true;
            cv.notify_all();
        });
    }

    // called once prepareDone is seen
    void FinishPreparation() {
        ScheduledTest &test = *testRunning;
        prepareThread.join();
        testRunning = nullptr;

        if(!prepareError.empty()) {
            PublishError(std::format("could not prepare test \"{}\": {}", test.testId, prepareError));
            SetState(test, ScheduledTest::State::failed);
            return;
        }
        SetState(test, ScheduledTest::State::ready);
        Common::Logger::Info("preparation finished: {}", test.testId);
    }

    // leaves the test in the state it had
    void StopRunning() {
        if(testRunning == nullptr) {
            return;
        }
        if(testRunning->state == ScheduledTest::State::preparing) {
            prepareThread.request_stop();
            prepareThread.join();
            testRunning = nullptr;
            Common::Logger::Info("preparation stopped");
        }
        else {
            StopPipeline();
        }
    }

    void StartPipeline(ScheduledTest &test) {
        Common::Assert(testRunning == nullptr, "a test is already running");

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
            SetState(test, ScheduledTest::State::failed);
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
            SetState(test, ScheduledTest::State::failed);
            return;
        }

        SetSignalSources(test);
        SetState(test, ScheduledTest::State::playing);
        testRunning = &test;
        ctx.controller->Start();
        Common::Logger::Info("playback started: {}", test.testId);
    }

    // takes the pipeline down and leaves the test in the state it had
    void StopPipeline() {
        if(testRunning == nullptr) {
            return;
        }
        ctx.controller->Stop(true);
        ctx.controller->Join();
        ctx.Dispose();
        ctx.controller.reset();
        testRunning = nullptr;
        ClearSignalSources();
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
            PublishError(std::format("error running pipeline for test \"{}\": {}", test.testId, e.what()));
            failed = true;
        }
        ctx.Dispose();
        ctx.controller.reset();
        testRunning = nullptr;
        ClearSignalSources();

        if(failed) {
            SetState(test, ScheduledTest::State::failed);
        }
        else {
            SetState(test, ScheduledTest::State::done);
            Common::Logger::Info("playback finished: {}", test.testId);
        }
    }

    void UpdateSchedule() {
        auto now = std::chrono::system_clock::now();
        auto startAhead = Seconds(config.startAhead_s);

        if(testRunning != nullptr) {
            if(testRunning->state == ScheduledTest::State::preparing) {
                if(prepareDone) {
                    FinishPreparation();
                }
            }
            else if(ctx.controller->GetState() != Sagax::Pipeline::Controller::RunState::running) {
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
                SetState(test, ScheduledTest::State::failed);
                continue;
            }
            if(testRunning != nullptr) {
                PublishError(std::format("test \"{}\" cannot start, test \"{}\" is still running", test.testId, testRunning->testId));
                SetState(test, ScheduledTest::State::failed);
                continue;
            }
            StartPipeline(test);
        }

        if(testRunning == nullptr) {
            for(auto &t : tests) {
                if(t->state == ScheduledTest::State::pending) {
                    StartPreparation(*t);
                    break;
                }
            }
        }

        std::erase_if(tests, [](const std::unique_ptr<ScheduledTest> &t) {
            return t->state == ScheduledTest::State::done
                || t->state == ScheduledTest::State::failed;
        });
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
            error = ScheduleTest(v);
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
            // a message that cannot be handled at all costs that one message. the loop
            // and the pipeline behind it stay up
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

        Common::Logger::Info("server running.. ctrl+c to stop");

        statusPeriod = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(config.statusUpdatePeriod_s)
        );
        nextSignalStatus = std::chrono::steady_clock::now();
        while(true) {
            // mtx is held for the wait only, the steps below reach the module mutexes
            // through the controller and must not hold it. the wait also returns early
            // when the mqtt client, an event callback or the signal handler notifies
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

        // constructed after the streams it logs to, so the destructor takes it down
        // before them, and before the mqtt client or a pipeline starts a thread, on linux
        // a thread inherits the signal mask and the blocking has to be in place by then.
        // the callback runs on an ordinary thread, so it can take mtx and notify cv
        SCG::SignalHandler signalHandler({ SIGINT, SIGTERM }, [this](int sig){
            Common::Logger::Info("signal {} received, stopping", sig);
            {
                std::unique_lock lck(mtx);
                programRunning = false;
            }
            cv.notify_all();
        });

        // the radios are opened by the setup and handed to the pipelines, so they are
        // released after Run has left, whether it returned or threw
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

        // a signal that arrived while the radios were opened is acted on here, the run
        // loop is never entered
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
