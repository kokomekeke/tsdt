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
#include "RidTransform.hpp"
#include "TestScheduler.hpp"
#include "SCG/Utility/TimeFormat.hpp"
#include "TsdMerge.hpp"

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

        // test.json holds the setup command and the last status of the test,
        // rid.json the merged rid the test plays, tsd.json the tsd the command named and
        // rid_000.json the rid of every step that named one
        constexpr const char *testFileName = "test.json";
        constexpr const char *ridFileName = "rid.json";
        constexpr const char *tsdFileName = "tsd.json";

        std::string StepRidFileName(std::size_t step) {
            return std::format("rid_{:03}.json", step);
        }

        // the documents the scheduler writes when a test is set up. the preparation
        // clears everything else out of the directory before it runs
        bool IsSourceFile(const std::filesystem::path &name) {
            std::string text = name.string();
            if(text == testFileName || text == ridFileName || text == tsdFileName) {
                return true;
            }
            return text.starts_with("rid_") && text.ends_with(".json");
        }

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
            Json::JsonIssues issues;
            if(!ParseRid(json, out, issues)) {
                return std::format("failed to parse RID:\n{}", issues.Format());
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
            Json::JsonIssues issues;
            if(!ParseSavedTest(json, out, issues)) {
                return std::format("could not parse {}:\n{}", testFileName, issues.Format());
            }
            if(out.command.testId != directory.filename().string()) {
                return std::format("the test id in {} is \"{}\"", testFileName, out.command.testId);
            }
            return {};
        }

        void WriteSavedTest(const std::filesystem::path &directory, const SavedTest &saved) {
            try {
                Json::JsonIssues issues;
                std::optional<std::string> json = SerializeSavedTest(saved, issues);
                if(!json.has_value()) {
                    Common::Logger::Warn(
                        "could not save the state of test \"{}\":\n{}", saved.command.testId, issues.Format()
                    );
                    return;
                }
                WriteTextFile(directory / testFileName, json.value());
            }
            catch(const std::exception &e) {
                Common::Logger::Warn("could not save the state of test \"{}\": {}", saved.command.testId, e.what());
            }
        }

        bool HasSpoofer(const Rid &rid) {
            return std::any_of(rid.interferenceSources.begin(), rid.interferenceSources.end(), [](const Rid::InterferenceSource &s) {
                return std::holds_alternative<Rid::InterferenceSource::Spoofing>(s.variant);
            });
        }

        // the files a prepared test plays only match the config they were recorded with.
        // the ephemeris only shapes a real recording
        CommandError CheckPreparedGnss(const SavedTest::Gnss &saved, const ServerConfig &config) {
            if(saved.prerecord != config.prerecordGnss) {
                return std::format(
                    "prerecord_gnss was {} when the test was prepared and is {} now",
                    saved.prerecord, config.prerecordGnss
                );
            }
            if(saved.mock != config.mockGnss) {
                return std::format(
                    "mock_gnss was {} when the test was prepared and is {} now",
                    saved.mock, config.mockGnss
                );
            }
            if(saved.prerecord && !saved.mock && saved.ephemerisFile != config.gnssEphemerisFile) {
                return std::format(
                    "the test was prepared with the ephemeris \"{}\", the config names \"{}\" now",
                    saved.ephemerisFile, config.gnssEphemerisFile
                );
            }
            return {};
        }

        bool IsOver(TestStatusMessage::TestStatus status) {
            return status == TestStatusMessage::TestStatus::TEST_COMPLETED
                || status == TestStatusMessage::TestStatus::TEST_FAILED;
        }

    }

    static TestStatusMessage::TestStatus ReportedStatus(ScheduledTest::State state) {
        switch(state) {
            case ScheduledTest::State::pending:   return TestStatusMessage::TestStatus::TEST_INIT;
            case ScheduledTest::State::preparing: return TestStatusMessage::TestStatus::TEST_PREPARE;
            case ScheduledTest::State::ready:     return TestStatusMessage::TestStatus::TEST_READY;
            case ScheduledTest::State::starting:  return TestStatusMessage::TestStatus::TEST_READY;
            case ScheduledTest::State::playing:   return TestStatusMessage::TestStatus::TEST_RUNNING;
            case ScheduledTest::State::done:      return TestStatusMessage::TestStatus::TEST_COMPLETED;
            case ScheduledTest::State::failed:    return TestStatusMessage::TestStatus::TEST_FAILED;
        }
        Common::Assert(false, "unhandled test state");
        return TestStatusMessage::TestStatus::TEST_FAILED;
    }

    static const char *StateName(ScheduledTest::State state) {
        switch(state) {
            case ScheduledTest::State::pending:   return "pending";
            case ScheduledTest::State::preparing: return "preparing";
            case ScheduledTest::State::ready:     return "ready";
            case ScheduledTest::State::starting:  return "starting";
            case ScheduledTest::State::playing:   return "playing";
            case ScheduledTest::State::done:      return "done";
            case ScheduledTest::State::failed:    return "failed";
        }
        Common::Assert(false, "unhandled test state");
        return "";
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

    void TestScheduler::PublishStatus(const ScheduledTest &test, std::chrono::system_clock::time_point time) {
        onStatus(test.testId, ReportedStatus(test.state), time);
    }

    // every state change after the test is scheduled goes through here. the status is
    // published only when the reported status changes
    void TestScheduler::SetState(ScheduledTest &test, ScheduledTest::State state, std::chrono::system_clock::time_point time) {
        if(test.state == state) {
            return;
        }
        auto reported = ReportedStatus(test.state);
        test.state = state;
        if(ReportedStatus(state) != reported) {
            PublishStatus(test, time);
        }
        SaveState(test);
        LogTests();
    }

    void TestScheduler::LogTests() {
        std::string text = std::format("scheduled tests: {}", tests.size());
        for(auto &t : tests) {
            text += std::format("\n  {} {} {}", t->testId, FormatIsoTime(t->time), StateName(t->state));
        }
        Common::Logger::Info("{}", text);
    }

    void TestScheduler::SaveState(const ScheduledTest &test) {
        SavedTest saved;
        saved.command = test.command;
        saved.status = ReportedStatus(test.state);
        saved.gnss = test.gnss;
        WriteSavedTest(test.directory, saved);
    }

    // tsd.json and the rid of every step are a record of what the test was built from,
    // nothing reads them back
    void TestScheduler::SaveSources(const ScheduledTest &test, const TsdSources &sources) {
        try {
            std::filesystem::path directory(test.directory);
            WriteTextFile(directory / tsdFileName, sources.tsdJson);
            for(std::size_t i = 0; i < sources.ridJsons.size(); ++i) {
                if(sources.ridJsons[i].empty()) {
                    continue;
                }
                WriteTextFile(directory / StepRidFileName(i), sources.ridJsons[i]);
            }
        }
        catch(const std::exception &e) {
            Common::Logger::Warn("could not save the documents of test \"{}\": {}", test.testId, e.what());
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
        Rid rid,
        ScheduledTest::State state,
        std::unique_ptr<ScheduledTest> &out
    ) {
        Common::Logger::Info("validating the merged RID..");
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
        test->gnss.prerecord = config.prerecordGnss;
        test->gnss.mock = config.mockGnss;
        test->gnss.ephemerisFile = config.gnssEphemerisFile;
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

        TsdSources sources;
        SCG_CHECK(FetchTsd(config, command.scenarioUrl, sources));
        SCG_CHECK(ValidateTsdSources(config, sources));

        Rid merged;
        SCG_CHECK(MergeTsd(sources.tsd, sources.rids, merged));

        std::string json;
        {
            Json::JsonIssues issues;
            std::optional<std::string> text = SerializeRid(merged, issues);
            if(!text.has_value()) {
                return std::format("failed to write the merged RID:\n{}", issues.Format());
            }
            json = std::move(text.value());
        }

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(command, time, std::move(merged), ScheduledTest::State::pending, test));
        SCG_CHECK(SaveTest(*test, json));
        SaveSources(*test, sources);

        PublishStatus(*test, std::chrono::system_clock::now());
        tests.push_back(std::move(test));
        Common::Logger::Info("test scheduled: {}", command.testId);
        LogTests();
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
        SetState(**it, ScheduledTest::State::failed, std::chrono::system_clock::now());
        RemoveDirectory(**it);
        tests.erase(it);
        Common::Logger::Info("test cancelled: {}", testId);
        LogTests();
        return {};
    }

    // in the order they start. a test that is over is skipped, a test that cannot be
    // restored is failed
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
            if(!error.empty()) {
                DropSavedTest(f.directory, error);
                continue;
            }
            if(IsOver(f.saved.status)) {
                continue;
            }
            error = ParseStartTime(f.saved.command.startTime, f.time);
            if(error.empty() && f.time <= now) {
                error = "it missed its starting time while the server was down";
            }
            if(!error.empty()) {
                FailSavedTest(f.directory, f.saved, error);
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
                FailSavedTest(f.directory, f.saved, error);
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
                Common::Assert(false, "a test that is over is not restored");
        }

        std::chrono::system_clock::time_point time;
        SCG_CHECK(CheckSetup(saved.command, time));
        std::string json;
        SCG_CHECK(ReadTextFile(directory / ridFileName, json));
        Rid rid;
        SCG_CHECK(ReadRid(json, rid));
        if(state == ScheduledTest::State::ready && HasSpoofer(rid)) {
            SCG_CHECK(CheckPreparedGnss(saved.gnss, config));
        }

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(saved.command, time, std::move(rid), state, test));

        SaveState(*test);
        PublishStatus(*test, std::chrono::system_clock::now());
        Common::Logger::Info("test restored: {}", test->testId);
        tests.push_back(std::move(test));
        LogTests();
        return {};
    }

    // the directory name is the test id, test.json may not be readable
    void TestScheduler::DropSavedTest(const std::filesystem::path &directory, const std::string &error) {
        std::string testId = directory.filename().string();
        onError(std::format("could not restore test \"{}\": {}", testId, error));
        onStatus(testId, TestStatusMessage::TestStatus::TEST_FAILED, std::chrono::system_clock::now());
    }

    void TestScheduler::FailSavedTest(const std::filesystem::path &directory, SavedTest saved, const std::string &error) {
        DropSavedTest(directory, error);
        saved.status = TestStatusMessage::TestStatus::TEST_FAILED;
        WriteSavedTest(directory, saved);
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
                SetState(test, ScheduledTest::State::failed, std::chrono::system_clock::now());
                continue;
            }
            if(running != nullptr) {
                onError(std::format("test \"{}\" cannot start, test \"{}\" is still running", test.testId, running->testId));
                SetState(test, ScheduledTest::State::failed, std::chrono::system_clock::now());
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
            SetState(test, ScheduledTest::State::preparing, std::chrono::system_clock::now());
        }
        else if(test.state == ScheduledTest::State::ready) {
            SetState(test, ScheduledTest::State::starting, std::chrono::system_clock::now());
        }
    }

    void TestScheduler::Transmitting(ScheduledTest &test, std::chrono::system_clock::time_point time) {
        Common::Assert(test.state == ScheduledTest::State::starting, "only a starting test begins transmitting");
        SetState(test, ScheduledTest::State::playing, time);
    }

    void TestScheduler::Finished(ScheduledTest &test, bool success) {
        if(running == &test) {
            running = nullptr;
        }
        Common::Assert(!success || test.state != ScheduledTest::State::starting, "a test that transmitted nothing cannot succeed");
        if(!success) {
            SetState(test, ScheduledTest::State::failed, std::chrono::system_clock::now());
        }
        else if(test.state == ScheduledTest::State::preparing) {
            SetState(test, ScheduledTest::State::ready, std::chrono::system_clock::now());
        }
        else if(test.state == ScheduledTest::State::playing) {
            SetState(test, ScheduledTest::State::done, std::chrono::system_clock::now());
        }
    }

    void TestScheduler::Stopped(ScheduledTest &test) {
        if(running == &test) {
            running = nullptr;
        }
    }

    void TestScheduler::EraseFinished() {
        auto erased = std::erase_if(tests, [](const std::unique_ptr<ScheduledTest> &t) {
            return t->state == ScheduledTest::State::done
                || t->state == ScheduledTest::State::failed;
        });
        if(erased > 0) {
            LogTests();
        }
    }

    void TestScheduler::Prepare(const ScheduledTest &test, std::stop_token stop) {
        std::filesystem::path directory(test.directory);
        std::vector<std::filesystem::path> leftovers;
        for(auto &entry : std::filesystem::directory_iterator(directory)) {
            if(!IsSourceFile(entry.path().filename())) {
                leftovers.push_back(entry.path());
            }
        }
        for(auto &path : leftovers) {
            std::filesystem::remove_all(path);
        }

        {
            Json::JsonIssues issues;
            std::optional<std::string> json = SerializeScenario(test.scenario, issues);
            if(!json.has_value()) {
                Common::Logger::Error("could not write the scenario:\n{}", issues.Format());
                return;
            }
            WriteTextFile(directory / "scenario.json", json.value());
        }

        for(auto &job : test.jobs) {
            if(stop.stop_requested()) {
                return;
            }
            Common::Logger::Info("running {}", job->Describe());
            job->Run(stop);
        }
    }

}
