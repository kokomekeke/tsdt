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

        CommandError CheckInFuture(const std::string &s, std::chrono::system_clock::time_point t, std::chrono::system_clock::time_point now) {
            if(t <= now) {
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

    void TestScheduler::SetNow(std::chrono::system_clock::time_point now_) {
        now = now_;
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
        saved.authorized = test.authorized;
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

    // the preparations run one at a time in the order the tests start, from now on. a
    // preparation waits for every playback it would run into, which is that of the active
    // test, of a ready test and of a test prepared before it, like DueForPreparation does.
    // every preparation has to end before the start_ahead of its test. a running
    // preparation is counted whole
    CommandError TestScheduler::CheckPreparationQueue(const ScheduledTest &test) {
        auto startAhead = Seconds(config.startAhead_s);
        auto free = now;

        std::vector<const ScheduledTest *> queue{ &test };
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::done || t->state == ScheduledTest::State::failed) {
                continue;
            }
            if(t->state == ScheduledTest::State::preparing) {
                free += Seconds(JobEstimate_s(*t));
            }
            queue.push_back(t.get());
        }
        std::sort(queue.begin(), queue.end(), [](const ScheduledTest *a, const ScheduledTest *b) {
            return a->time < b->time;
        });

        for(std::size_t i = 0; i < queue.size(); ++i) {
            const ScheduledTest &p = *queue[i];
            if(p.state != ScheduledTest::State::pending) {
                continue;
            }
            auto work = Seconds(JobEstimate_s(p));
            bool moved = true;
            while(moved) {
                moved = false;
                for(std::size_t j = 0; j < queue.size(); ++j) {
                    const ScheduledTest &b = *queue[j];
                    if(j == i || (j > i && b.state == ScheduledTest::State::pending)) {
                        continue;
                    }
                    if(b.time - startAhead < free + work && b.end > free) {
                        free = b.end;
                        moved = true;
                    }
                }
            }
            free += work;

            if(free > p.time - startAhead) {
                double late_s = std::chrono::duration<double>(free - (p.time - startAhead)).count();
                if(&p == &test) {
                    return std::format(
                        "test \"{}\" cannot be prepared in time, its preparation would end {:.1f} s after "
                        "its playback starts",
                        test.testId, late_s
                    );
                }
                return std::format(
                    "test \"{}\" would delay the preparation of test \"{}\", which would end {:.1f} s after "
                    "its playback starts",
                    test.testId, p.testId, late_s
                );
            }
        }
        return {};
    }

    CommandError TestScheduler::CheckSetup(const CommandMessage::TestSetup &command, std::chrono::system_clock::time_point &time) {
        SCG_CHECK(ParseStartTime(command.startTime, time));
        SCG_CHECK(CheckInFuture(command.startTime, time, now));
        SCG_CHECK(CheckTestId(command.testId));
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

        SCG_CHECK(CheckOverlap(*test));
        SCG_CHECK(CheckPreparationQueue(*test));

        out = std::move(test);
        return {};
    }

    CommandError TestScheduler::Schedule(const CommandMessage::TestSetup &command) {
        for(auto &t : tests) {
            if(t->testId == command.testId) {
                return std::format("test id already scheduled: \"{}\"", command.testId);
            }
        }
        CommandError error = Accept(command);
        if(!error.empty()) {
            onStatus(command.testId, TestStatusMessage::TestStatus::TEST_FAILED, now);
        }
        return error;
    }

    CommandError TestScheduler::Accept(const CommandMessage::TestSetup &command) {
        std::chrono::system_clock::time_point time;
        SCG_CHECK(CheckSetup(command, time));

        TsdSources sources;
        SCG_CHECK(FetchTsd(lh, config, command.scenarioUrl, sources));
        SCG_CHECK(ValidateTsdSources(lh, config, sources));

        std::unique_ptr<ScheduledTest> test;
        SCG_CHECK(CreateTest(command, time, sources, ScheduledTest::State::pending, test));
        SCG_CHECK(storage.SaveSources(test->testId, sources));
        SaveState(*test);

        PublishStatus(*test, now);
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
        SetState(**it, ScheduledTest::State::failed, now);
        storage.Remove(testId);
        tests.erase(it);
        Common::Logger::Info(lh, "test cancelled: {}", testId);
        LogTests();
        return {};
    }

    CommandError TestScheduler::Authorize(const std::string &testId) {
        auto it = std::find_if(tests.begin(), tests.end(), [&](const std::unique_ptr<ScheduledTest> &t) {
            return t->testId == testId;
        });
        if(it == tests.end()) {
            return std::format("unknown test id: \"{}\"", testId);
        }
        ScheduledTest &test = **it;
        if(now >= test.time) {
            return std::format("test \"{}\" cannot be authorized after its starting time", testId);
        }
        test.authorized = true;
        SaveState(test);
        Common::Logger::Info(lh, "test authorized: {}", testId);
        return {};
    }

    // in the order they start. the directory of a test that is over is removed, a test that
    // cannot be restored is failed
    void TestScheduler::Restore() {
        struct Found {
            SavedTest saved;
            std::chrono::system_clock::time_point time;
        };
        std::vector<Found> found;
        for(const std::string &testId : storage.SavedTestIds()) {
            Found f;
            CommandError error = storage.LoadState(testId, f.saved);
            if(!error.empty()) {
                DropSavedTest(testId, error);
                continue;
            }
            if(IsOver(f.saved.status)) {
                storage.Remove(testId);
                continue;
            }
            error = ParseStartTime(f.saved.command.startTime, f.time);
            if(error.empty() && f.time <= now) {
                error = "it missed its starting time while the server was down";
            }
            if(!error.empty()) {
                DropSavedTest(testId, error);
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
                DropSavedTest(f.saved.command.testId, error);
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
        test->authorized = saved.authorized;

        SaveState(*test);
        PublishStatus(*test, now);
        Common::Logger::Info(lh, "test restored: {}", test->testId);
        tests.push_back(std::move(test));
        LogTests();
        return {};
    }

    // test.json may not be readable, the id is the name of the directory
    void TestScheduler::DropSavedTest(const std::string &testId, const std::string &error) {
        onError(testId, std::format("could not restore test \"{}\": {}", testId, error));
        onStatus(testId, TestStatusMessage::TestStatus::TEST_FAILED, now);
        storage.Remove(testId);
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
        if(first == nullptr) {
            return nullptr;
        }
        auto end = now + Seconds(JobEstimate_s(*first));
        auto startAhead = Seconds(config.startAhead_s);
        for(auto &t : tests) {
            if(t->state == ScheduledTest::State::ready && t->time - startAhead < end) {
                return nullptr;
            }
        }
        return first;
    }

    ScheduledTest *TestScheduler::DueForPlayback() {
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

    bool TestScheduler::PartDue(const ScheduledTest &test) {
        return test.nextPart < test.scenario.parts.size()
            && now >= test.scenario.parts[test.nextPart].time - Seconds(config.startAhead_s);
    }

    bool TestScheduler::PlaybackOver(const ScheduledTest &test) {
        return test.nextPart == test.scenario.parts.size() && now >= test.end;
    }

    // the times in the past are left out, the queries already answer for them
    std::optional<std::chrono::system_clock::time_point> TestScheduler::NextTime() {
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

    void TestScheduler::FailMissed() {
        for(auto &t : tests) {
            if((t->state == ScheduledTest::State::pending || t->state == ScheduledTest::State::ready) && now >= t->time) {
                onError(t->testId, std::format("test \"{}\" missed its starting time", t->testId));
                SetState(*t, ScheduledTest::State::failed, now);
            }
        }
    }

    void TestScheduler::SetPreparing(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::pending, "only a pending test is prepared");
        SetState(test, ScheduledTest::State::preparing, now);
    }

    void TestScheduler::SetReady(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::preparing, "only a preparing test becomes ready");
        SetState(test, ScheduledTest::State::ready, now);
    }

    void TestScheduler::SetStarting(ScheduledTest &test) {
        Common::Assert(test.state == ScheduledTest::State::ready, "only a ready test starts");
        test.nextPart = 0;
        SetState(test, ScheduledTest::State::starting, now);
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
        SetState(test, ScheduledTest::State::failed, now);
    }

    void TestScheduler::EraseFinished() {
        auto isOver = [](const std::unique_ptr<ScheduledTest> &t) {
            return t->state == ScheduledTest::State::done
                || t->state == ScheduledTest::State::failed;
        };
        for(auto &t : tests) {
            if(isOver(t)) {
                storage.Remove(t->testId);
            }
        }
        auto erased = std::erase_if(tests, isOver);
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
