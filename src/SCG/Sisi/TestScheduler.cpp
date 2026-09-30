#include <algorithm>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/Utility/Fetch.hpp"
#include "RidTransform.hpp"
#include "TestScheduler.hpp"
#include "TimeFormat.hpp"

namespace Sagax::SCG {

    namespace {

        CommandError CheckInFuture(const std::string &s, std::chrono::system_clock::time_point t) {
            if(t <= std::chrono::system_clock::now()) {
                return std::format("starting time is in the past: \"{}\"", s);
            }
            return {};
        }

        // the id is used as a directory name
        CommandError CheckTestId(const std::string &testId) {
            if(testId.empty()
                || testId == "."
                || testId == ".."
                || testId.find_first_of("/\\:*?\"<>|") != std::string::npos
            ) {
                return std::format("test id cannot be used as a directory name: \"{}\"", testId);
            }
            return {};
        }

        // test.json holds the setup command and the status while the test is not over,
        // rid.json the rid the command named
        constexpr const char *testFileName = "test.json";
        constexpr const char *ridFileName = "rid.json";

        // a crash leaves either the old or the new content
        void WriteTextFile(const std::filesystem::path &path, const std::string &content) {
            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary);
                file << content;
                file.close();
                Common::Assert(!file.fail(), std::format("could not write {}", temporary.string()));
            }
            std::filesystem::rename(temporary, path);
        }

        CommandError ReadTextFile(const std::filesystem::path &path, std::string &out) {
            std::ifstream file(path, std::ios::binary);
            if(!file.is_open()) {
                return std::format("could not open {}", path.string());
            }
            std::stringstream buf;
            buf << file.rdbuf();
            out = buf.str();
            return {};
        }

        CommandError ReadRid(const std::string &json, Rid &out) {
            try {
                out = ParseRid(json);
            }
            catch(const std::exception &e) {
                return std::format("failed to parse RID: {}", e.what());
            }
            return {};
        }

        CommandError MapRid(
            const ServerConfig &cfg,
            const Rid &rid,
            std::chrono::system_clock::time_point startTime,
            const std::string &directory,
            ScenarioRidMap &out
        ) {
            try {
                out = MapRidToScenario(cfg, rid, startTime, directory);
            }
            catch(const std::exception &e) {
                return std::format("failed to build a scenario from the RID: {}", e.what());
            }
            return {};
        }

        std::chrono::system_clock::duration Seconds(double seconds) {
            return std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(seconds)
            );
        }

        double JobEstimate_s(const ScheduledTest &test) {
            double total = 0.0;
            for(auto &job : test.jobs) {
                total += job->Estimate_s();
            }
            return total;
        }

        CommandError ReadSavedTest(const std::filesystem::path &directory, SavedTest &out) {
            std::string json;
            SCG_CHECK(ReadTextFile(directory / testFileName, json));
            try {
                out = ParseSavedTest(json);
            }
            catch(const std::exception &e) {
                return std::format("could not parse {}: {}", testFileName, e.what());
            }
            if(out.command.testId != directory.filename().string()) {
                return std::format("the test id in {} is \"{}\"", testFileName, out.command.testId);
            }
            return {};
        }

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

    static void RemoveDirectory(const ScheduledTest &test) {
        std::error_code ec;
        std::filesystem::remove_all(test.directory, ec);
        if(ec) {
            Common::Logger::Warn("could not remove {}: {}", test.directory, ec.message());
        }
    }

    void TestScheduler::Configure(const ServerConfig &config_) {
        config = config_;
    }

    ScheduledTest *TestScheduler::Running() {
        return running;
    }

    void TestScheduler::PublishStatus(const ScheduledTest &test) {
        onStatus(test.testId, ReportedStatus(test.state));
    }

    // every state change after the test is scheduled goes through here
    void TestScheduler::SetState(ScheduledTest &test, ScheduledTest::State state) {
        if(test.state == state) {
            return;
        }
        test.state = state;
        PublishStatus(test);
        SaveState(test);
    }

    // the test.json of a test that is over is removed
    void TestScheduler::SaveState(const ScheduledTest &test) {
        auto path = std::filesystem::path(test.directory) / testFileName;
        if(test.state == ScheduledTest::State::done || test.state == ScheduledTest::State::failed) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            return;
        }
        try {
            SavedTest saved;
            saved.command = test.command;
            saved.status = ReportedStatus(test.state);
            WriteTextFile(path, SerializeSavedTest(saved));
        }
        catch(const std::exception &e) {
            Common::Logger::Warn("could not save the state of test \"{}\": {}", test.testId, e.what());
        }
    }

    CommandError TestScheduler::SaveTest(const ScheduledTest &test, const std::string &json) {
        try {
            std::filesystem::path directory(test.directory);
            std::filesystem::create_directories(directory);
            WriteTextFile(directory / ridFileName, json);
        }
        catch(const std::exception &e) {
            return std::format("could not save test \"{}\": {}", test.testId, e.what());
        }
        SaveState(test);
        return {};
    }

    // a playback holds the pipeline from start_ahead before its time until its end
    CommandError TestScheduler::CheckOverlap(const std::string &testId, std::chrono::system_clock::time_point time, double duration) {
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

    // every preparation has to finish before the earliest playback, a running one is
    // counted whole
    CommandError TestScheduler::CheckPreparationQueue(const std::string &testId, std::chrono::system_clock::time_point time, double work) {
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

    CommandError TestScheduler::CheckSetup(const CommandMessage::TestSetup &command, std::chrono::system_clock::time_point &time) {
        SCG_CHECK(ParseStartTime(command.startTime, time));
        SCG_CHECK(CheckInFuture(command.startTime, time));
        SCG_CHECK(CheckTestId(command.testId));
        for(auto &t : tests) {
            if(t->testId == command.testId) {
                return std::format("test id already scheduled: \"{}\"", command.testId);
            }
        }
        return {};
    }

    // state is pending or ready. a ready test does not add to the preparation queue
    CommandError TestScheduler::CreateTest(
        const CommandMessage::TestSetup &command,
        std::chrono::system_clock::time_point time,
        const std::string &json,
        ScheduledTest::State state,
        std::unique_ptr<ScheduledTest> &out
    ) {
        Rid rid;
        SCG_CHECK(ReadRid(json, rid));

        Common::Logger::Info("validating RID..");
        SCG_CHECK(CheckValidity(ValidateRid(config, rid), "RID"));
        Common::Logger::Info("validation successful");

        std::string directory = (
            std::filesystem::absolute(config.recordingDir) / command.testId
        ).string();

        ScenarioRidMap scenarioMap;
        SCG_CHECK(MapRid(config, rid, time, directory, scenarioMap));
        auto scenario = scenarioMap.scen;

        Common::Logger::Info("validating scenario..");
        SCG_CHECK(CheckValidity(ValidateScenario(scenario), "scenario"));
        Common::Logger::Info("validation successful");

        auto test = std::make_unique<ScheduledTest>();
        test->testId = command.testId;
        test->command = command;
        test->time = time;
        test->directory = std::move(directory);
        test->rid = std::move(rid);
        test->eventMap = std::move(scenarioMap.eventMap);
        test->jobs = std::move(scenarioMap.jobs);
        test->scenario = std::move(scenario);
        test->state = state;

        double work = 0.0;
        if(state == ScheduledTest::State::pending) {
            work = JobEstimate_s(*test);
        }
        SCG_CHECK(CheckOverlap(command.testId, time, test->scenario.duration));
        SCG_CHECK(CheckPreparationQueue(command.testId, time, work));

        out = std::move(test);
        return {};
    }

    CommandError TestScheduler::Schedule(const CommandMessage::TestSetup &command) {
        std::chrono::system_clock::time_point time;
        SCG_CHECK(CheckSetup(command, time));

        std::string json;
        SCG_CHECK(Fetch(command.scenarioUrl, config.scenarioFetchTimeout_s, json));

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(command, time, json, ScheduledTest::State::pending, test));
        SCG_CHECK(SaveTest(*test, json));

        PublishStatus(*test);
        tests.push_back(std::move(test));
        Common::Logger::Info("test scheduled: {}", command.testId);
        return {};
    }

    CommandError TestScheduler::Cancel(const std::string &testId) {
        auto it = std::find_if(tests.begin(), tests.end(), [&](const std::unique_ptr<ScheduledTest> &t) {
            return t->testId == testId;
        });
        if(it == tests.end()) {
            return std::format("unknown test id: \"{}\"", testId);
        }
        Common::Assert(running != it->get(), "a running test cannot be cancelled");
        RemoveDirectory(**it);
        SetState(**it, ScheduledTest::State::failed);
        tests.erase(it);
        Common::Logger::Info("test cancelled: {}", testId);
        return {};
    }

    // in the order they start. a test that cannot be restored is failed and its test.json
    // is removed
    void TestScheduler::Restore() {
        std::filesystem::path root = std::filesystem::absolute(config.recordingDir);
        std::error_code ec;
        if(!std::filesystem::is_directory(root, ec)) {
            return;
        }

        struct Found {
            std::filesystem::path directory;
            SavedTest saved;
            std::chrono::system_clock::time_point time;
        };
        std::vector<Found> found;
        auto now = std::chrono::system_clock::now();
        for(auto &entry : std::filesystem::directory_iterator(root, ec)) {
            if(!std::filesystem::exists(entry.path() / testFileName, ec)) {
                continue;
            }
            Found f;
            f.directory = entry.path();
            CommandError error = ReadSavedTest(f.directory, f.saved);
            if(error.empty()) {
                error = ParseStartTime(f.saved.command.startTime, f.time);
            }
            if(error.empty() && f.time <= now) {
                error = "it missed its starting time while the server was down";
            }
            if(!error.empty()) {
                DropSavedTest(f.directory, error);
                continue;
            }
            found.push_back(std::move(f));
        }

        std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
            return a.time < b.time;
        });
        for(auto &f : found) {
            CommandError error = RestoreTest(f.directory, f.saved);
            if(!error.empty()) {
                DropSavedTest(f.directory, error);
            }
        }
    }

    // a preparing test is prepared again, a running one is played again
    CommandError TestScheduler::RestoreTest(const std::filesystem::path &directory, const SavedTest &saved) {
        auto state = ScheduledTest::State::pending;
        switch(saved.status) {
            case TestStatusMessage::TestStatus::TEST_INIT:
            case TestStatusMessage::TestStatus::TEST_PREPARE:
                state = ScheduledTest::State::pending;
                break;
            case TestStatusMessage::TestStatus::TEST_READY:
            case TestStatusMessage::TestStatus::TEST_RUNNING:
                state = ScheduledTest::State::ready;
                break;
            default:
                return "it is already over";
        }

        std::chrono::system_clock::time_point time;
        SCG_CHECK(CheckSetup(saved.command, time));
        std::string json;
        SCG_CHECK(ReadTextFile(directory / ridFileName, json));

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(saved.command, time, json, state, test));

        SaveState(*test);
        PublishStatus(*test);
        Common::Logger::Info("test restored: {}", test->testId);
        tests.push_back(std::move(test));
        return {};
    }

    // the directory name is the test id, test.json may not be readable
    void TestScheduler::DropSavedTest(const std::filesystem::path &directory, const std::string &error) {
        std::string testId = directory.filename().string();
        onError(std::format("could not restore test \"{}\": {}", testId, error));
        onStatus(testId, TestStatusMessage::TestStatus::TEST_FAILED);
        std::error_code ec;
        std::filesystem::remove(directory / testFileName, ec);
    }

    // the playback starts start_ahead early, the sink holds the output until the exact time
    ScheduledTest *TestScheduler::DueForPlayback(std::chrono::system_clock::time_point now, std::chrono::system_clock::duration lead) {
        auto startAhead = Seconds(config.startAhead_s);
        for(auto &t : tests) {
            ScheduledTest &test = *t;
            if(test.state != ScheduledTest::State::ready) {
                continue;
            }
            if(now + lead < test.time - startAhead) {
                continue;
            }
            if(now >= test.time) {
                onError(std::format("test \"{}\" missed its starting time", test.testId));
                SetState(test, ScheduledTest::State::failed);
                continue;
            }
            if(running != nullptr) {
                onError(std::format("test \"{}\" cannot start, test \"{}\" is still running", test.testId, running->testId));
                SetState(test, ScheduledTest::State::failed);
                continue;
            }
            return &test;
        }
        return nullptr;
    }

    ScheduledTest *TestScheduler::DueForPreparation() {
        if(running != nullptr) {
            return nullptr;
        }
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::pending) {
                return t.get();
            }
        }
        return nullptr;
    }

    void TestScheduler::Started(ScheduledTest &test) {
        Common::Assert(running == nullptr, "a test is already running");
        running = &test;
        if(test.state == ScheduledTest::State::pending) {
            SetState(test, ScheduledTest::State::preparing);
        }
        else if(test.state == ScheduledTest::State::ready) {
            SetState(test, ScheduledTest::State::playing);
        }
    }

    void TestScheduler::Finished(ScheduledTest &test, bool success) {
        if(running == &test) {
            running = nullptr;
        }
        if(!success) {
            SetState(test, ScheduledTest::State::failed);
        }
        else if(test.state == ScheduledTest::State::preparing) {
            SetState(test, ScheduledTest::State::ready);
        }
        else if(test.state == ScheduledTest::State::playing) {
            SetState(test, ScheduledTest::State::done);
        }
    }

    void TestScheduler::Stopped(ScheduledTest &test) {
        if(running == &test) {
            running = nullptr;
        }
    }

    void TestScheduler::EraseFinished() {
        std::erase_if(tests, [](const std::unique_ptr<ScheduledTest> &t) {
            return t->state == ScheduledTest::State::done
                || t->state == ScheduledTest::State::failed;
        });
    }

    void TestScheduler::Prepare(const ScheduledTest &test, std::stop_token stop) {
        std::filesystem::path directory(test.directory);
        std::vector<std::filesystem::path> leftovers;
        for(auto &entry : std::filesystem::directory_iterator(directory)) {
            auto name = entry.path().filename();
            if(name != testFileName && name != ridFileName) {
                leftovers.push_back(entry.path());
            }
        }
        for(auto &path : leftovers) {
            std::filesystem::remove_all(path);
        }

        WriteTextFile(directory / "scenario.json", SerializeScenario(test.scenario));

        for(auto &job : test.jobs) {
            if(stop.stop_requested()) {
                return;
            }
            Common::Logger::Info("running {}", job->Describe());
            job->Run(stop);
        }
    }

}
