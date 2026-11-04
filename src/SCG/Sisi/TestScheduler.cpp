#include <algorithm>
#include <chrono>
#include <format>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "TestScheduler.hpp"
#include "SCG/Utility/TimeFormat.hpp"
#include "TsdTransform.hpp"

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

        std::chrono::system_clock::duration Seconds(double seconds) {
            return std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration<double>(seconds)
            );
        }

        double JobEstimate_s(const ScheduledTest &test) {
            double total = 0.0;
            for(auto &job : test.scenario.jobs) {
                total += job->Estimate_s();
            }
            return total;
        }

        bool HasSpoofer(const StepRids &rids) {
            for(auto &rid : rids) {
                if(!rid.has_value()) {
                    continue;
                }
                for(auto &s : rid.value().interferenceSources) {
                    if(std::holds_alternative<Rid::InterferenceSource::Spoofing>(s.variant)) {
                        return true;
                    }
                }
            }
            return false;
        }

        void LogSummary(const Common::Logger::Handle &lh, const ScheduledTest &test, const Tsd &tsd) {
            std::string text = std::format("test \"{}\" set up from TSD \"{}\"", test.testId, tsd.name);
            text += std::format("\n  {}  test start", FormatIsoTime(test.time));
            auto end = test.time;
            for(auto &part : test.scenario.parts) {
                if(part.time > end) {
                    text += std::format("\n  {}  gap", FormatIsoTime(end));
                }
                text += std::format("\n  {}  transmission:", FormatIsoTime(part.time));
                for(std::size_t i = 0; i < part.steps.size(); ++i) {
                    text += std::format(
                        "{} \"{}\"",
                        i == 0 ? "" : ",",
                        tsd.steps[std::size_t(part.steps[i])].interference.value()
                    );
                }
                end = part.time + Seconds(part.duration_s);
            }
            if(test.end > end) {
                text += std::format("\n  {}  gap", FormatIsoTime(end));
            }
            text += std::format("\n  {}  test end", FormatIsoTime(test.end));
            Common::Logger::Info(lh, "{}", text);
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

        bool IsActive(ScheduledTest::State state) {
            return state == ScheduledTest::State::preparing
                || state == ScheduledTest::State::starting
                || state == ScheduledTest::State::playing;
        }

        TestStatusMessage::TestStatus ReportedStatus(ScheduledTest::State state) {
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

        const char *StateName(ScheduledTest::State state) {
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

    }

    void TestScheduler::Configure(const ServerConfig &config_, const PipelineConfig &pipelineConfig_) {
        config = config_;
        pipelineConfig = &pipelineConfig_;
        storage.Configure(config.recordingDir);
    }

    void TestScheduler::PublishStatus(const ScheduledTest &test, std::chrono::system_clock::time_point time) {
        onStatus(test.testId, ReportedStatus(test.state), time);
    }

    // every state change after scheduling goes through here. the status is published only
    // when the reported status changes
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
        Common::Logger::Info(lh, "{}", text);
    }

    void TestScheduler::SaveState(const ScheduledTest &test) {
        SavedTest saved;
        saved.command = test.command;
        saved.status = ReportedStatus(test.state);
        saved.gnss = test.gnss;
        storage.SaveState(saved);
    }

    // a test holds the playback from start_ahead before its time until its end. no part
    // starts its pipeline earlier than that
    CommandError TestScheduler::CheckOverlap(const ScheduledTest &test) {
        auto startAhead = Seconds(config.startAhead_s);

        auto begin = test.time - startAhead;
        for(auto &t : tests) {
            auto otherBegin = t->time - startAhead;
            if(begin < t->end && otherBegin < test.end) {
                double overlap_s = std::chrono::duration<double>(
                    std::min(test.end, t->end) - std::max(begin, otherBegin)
                ).count();
                return std::format(
                    "test \"{}\" overlaps test \"{}\" by {:.1f} s",
                    test.testId, t->testId, overlap_s
                );
            }
        }
        return {};
    }

    // every preparation has to finish before the earliest playback, a running one is
    // counted whole
    CommandError TestScheduler::CheckPreparationQueue(const ScheduledTest &test, double work) {
        auto startAhead = Seconds(config.startAhead_s);

        auto earliest = test.time - startAhead;
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
                test.testId, late_s
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
        const TsdSources &sources,
        ScheduledTest::State state,
        std::unique_ptr<ScheduledTest> &out
    ) {
        std::string directory = storage.Directory(command.testId);

        auto test = std::make_unique<ScheduledTest>();
        try {
            SCG_CHECK(TransformTsd(*pipelineConfig, config, sources.tsd, sources.rids, time, directory, test->scenario));
        }
        catch(const std::exception &e) {
            return std::format("could not build the pipelines of the TSD: {}", e.what());
        }
        test->testId = command.testId;
        test->command = command;
        test->time = time;
        test->end = time + Seconds(test->scenario.duration_s);
        test->directory = directory;
        test->gnss.prerecord = config.prerecordGnss;
        test->gnss.mock = config.mockGnss;
        test->gnss.ephemerisFile = config.gnssEphemerisFile;
        test->state = state;

        double work = 0.0;
        if(state == ScheduledTest::State::pending) {
            work = JobEstimate_s(*test);
        }
        SCG_CHECK(CheckOverlap(*test));
        SCG_CHECK(CheckPreparationQueue(*test, work));

        out = std::move(test);
        return {};
    }

    CommandError TestScheduler::Schedule(const CommandMessage::TestSetup &command) {
        std::chrono::system_clock::time_point time;
        SCG_CHECK(CheckSetup(command, time));

        TsdSources sources;
        SCG_CHECK(FetchTsd(lh, config, command.scenarioUrl, sources));
        SCG_CHECK(ValidateTsdSources(lh, config, sources));

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(command, time, sources, ScheduledTest::State::pending, test));
        SCG_CHECK(storage.SaveSources(test->testId, sources));
        SaveState(*test);

        PublishStatus(*test, std::chrono::system_clock::now());
        LogSummary(lh, *test, sources.tsd);
        tests.push_back(std::move(test));
        Common::Logger::Info(lh, "test scheduled: {}", command.testId);
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
        SetState(**it, ScheduledTest::State::failed, std::chrono::system_clock::now());
        storage.Remove(testId);
        tests.erase(it);
        Common::Logger::Info(lh, "test cancelled: {}", testId);
        LogTests();
        return {};
    }

    // in the order they start. a test that is over is skipped, a test that cannot be
    // restored is failed
    void TestScheduler::Restore() {
        struct Found {
            SavedTest saved;
            std::chrono::system_clock::time_point time;
        };
        std::vector<Found> found;
        auto now = std::chrono::system_clock::now();
        for(const std::string &testId : storage.SavedTestIds()) {
            Found f;
            CommandError error = storage.LoadState(testId, f.saved);
            if(!error.empty()) {
                DropSavedTest(testId, error);
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
                FailSavedTest(f.saved, error);
                continue;
            }
            found.push_back(std::move(f));
        }

        std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
            return a.time < b.time;
        });
        for(auto &f : found) {
            CommandError error = RestoreTest(f.saved);
            if(!error.empty()) {
                FailSavedTest(f.saved, error);
            }
        }
    }

    // a preparing test is prepared again, a running one is played again
    CommandError TestScheduler::RestoreTest(const SavedTest &saved) {
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
        TsdSources sources;
        SCG_CHECK(storage.LoadSources(saved.command.testId, sources));
        if(state == ScheduledTest::State::ready && HasSpoofer(sources.rids)) {
            SCG_CHECK(CheckPreparedGnss(saved.gnss, config));
        }
        SCG_CHECK(ValidateTsdSources(lh, config, sources));

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(saved.command, time, sources, state, test));

        SaveState(*test);
        PublishStatus(*test, std::chrono::system_clock::now());
        Common::Logger::Info(lh, "test restored: {}", test->testId);
        tests.push_back(std::move(test));
        LogTests();
        return {};
    }

    // test.json may not be readable, the id is the name of the directory
    void TestScheduler::DropSavedTest(const std::string &testId, const std::string &error) {
        onError(testId, std::format("could not restore test \"{}\": {}", testId, error));
        onStatus(testId, TestStatusMessage::TestStatus::TEST_FAILED, std::chrono::system_clock::now());
    }

    void TestScheduler::FailSavedTest(SavedTest saved, const std::string &error) {
        DropSavedTest(saved.command.testId, error);
        saved.status = TestStatusMessage::TestStatus::TEST_FAILED;
        storage.SaveState(saved);
    }

    ScheduledTest *TestScheduler::Active() {
        for(auto &t : tests) {
            if(IsActive(t->state)) {
                return t.get();
            }
        }
        return nullptr;
    }

    ScheduledTest *TestScheduler::DueForPreparation() {
        if(Active() != nullptr) {
            return nullptr;
        }
        ScheduledTest *first = nullptr;
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::pending && (first == nullptr || t->time < first->time)) {
                first = t.get();
            }
        }
        return first;
    }

    ScheduledTest *TestScheduler::DueForPlayback(std::chrono::system_clock::time_point now) {
        if(Active() != nullptr) {
            return nullptr;
        }
        auto startAhead = Seconds(config.startAhead_s);
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::ready && now >= t->time - startAhead) {
                return t.get();
            }
        }
        return nullptr;
    }

    bool TestScheduler::PartDue(const ScheduledTest &test, std::chrono::system_clock::time_point now) {
        return test.nextPart < test.scenario.parts.size()
            && now >= test.scenario.parts[test.nextPart].time - Seconds(config.startAhead_s);
    }

    bool TestScheduler::PlaybackOver(const ScheduledTest &test, std::chrono::system_clock::time_point now) {
        return test.nextPart == test.scenario.parts.size() && now >= test.end;
    }

    // the times in the past are left out, the queries already answer for them
    std::optional<std::chrono::system_clock::time_point> TestScheduler::NextTime(std::chrono::system_clock::time_point now) {
        auto startAhead = Seconds(config.startAhead_s);
        std::optional<std::chrono::system_clock::time_point> next;
        auto consider = [&](std::chrono::system_clock::time_point time) {
            if(time > now && (!next.has_value() || time < next.value())) {
                next = time;
            }
        };

        for(auto &t : tests) {
            switch(t->state) {
                case ScheduledTest::State::pending:
                    consider(t->time);
                    break;
                case ScheduledTest::State::ready:
                    consider(t->time - startAhead);
                    consider(t->time);
                    break;
                case ScheduledTest::State::starting:
                case ScheduledTest::State::playing:
                    consider(t->time);
                    if(t->nextPart < t->scenario.parts.size()) {
                        consider(t->scenario.parts[t->nextPart].time - startAhead);
                    }
                    consider(t->end);
                    break;
                default:
                    break;
            }
        }
        return next;
    }

    void TestScheduler::FailMissed(std::chrono::system_clock::time_point now) {
        for(auto &t : tests) {
            if((t->state == ScheduledTest::State::pending || t->state == ScheduledTest::State::ready) && now >= t->time) {
                onError(t->testId, std::format("test \"{}\" missed its starting time", t->testId));
                SetState(*t, ScheduledTest::State::failed, std::chrono::system_clock::now());
            }
        }
    }

    void TestScheduler::SetPreparing(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::pending, "only a pending test is prepared");
        SetState(test, ScheduledTest::State::preparing, std::chrono::system_clock::now());
    }

    void TestScheduler::SetReady(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::preparing, "only a preparing test becomes ready");
        SetState(test, ScheduledTest::State::ready, std::chrono::system_clock::now());
    }

    void TestScheduler::SetStarting(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::ready, "only a ready test starts");
        test.nextPart = 0;
        SetState(test, ScheduledTest::State::starting, std::chrono::system_clock::now());
    }

    void TestScheduler::SetPlaying(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::starting, "only a starting test begins playing");
        SetState(test, ScheduledTest::State::playing, test.time);
    }

    void TestScheduler::SetPartStarted(ScheduledTest &test) {
        Common::Assert(test.nextPart < test.scenario.parts.size(), "every part has started");
        ++test.nextPart;
    }

    void TestScheduler::SetDone(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::playing, "only a playing test is done");
        SetState(test, ScheduledTest::State::done, test.end);
    }

    void TestScheduler::SetFailed(ScheduledTest &test) {
        SetState(test, ScheduledTest::State::failed, std::chrono::system_clock::now());
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

    void TestScheduler::Prepare(const ScheduledTest &test, std::stop_token stop) const {
        storage.ClearPreparation(test.testId);

        for(auto &job : test.scenario.jobs) {
            if(stop.stop_requested()) {
                return;
            }
            Common::Logger::Info(lh, "running {}", job->Describe());
            job->Run(stop);
        }
    }

}
