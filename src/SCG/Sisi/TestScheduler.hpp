#pragma once
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Utility/Job.hpp"
#include "CommandError.hpp"
#include "Messages.hpp"
#include "Rid.hpp"
#include "SavedTest.hpp"
#include "ServerConfig.hpp"
#include "TsdMerge.hpp"

namespace Sagax::SCG {

    struct ScheduledTest {
        // preparing: the directory is set up and the jobs run
        // done, failed: erased from the list by EraseFinished
        enum struct State {
            pending,
            preparing,
            ready,
            playing,
            done,
            failed
        };

        std::string testId;
        CommandMessage::TestSetup command;
        std::chrono::system_clock::time_point time;
        Rid rid;
        // sink event id -> index into rid.interferenceSources
        std::vector<std::int32_t> eventMap;
        Scenario scenario;
        std::vector<std::unique_ptr<Job>> jobs;
        std::string directory;
        State state = State::pending;
    };

    // the tests from their setup until they are over. state changes are published with
    // onStatus and saved to the test directory. the caller runs the preparation and the
    // playback and reports them with Started, Finished and Stopped
    struct TestScheduler {
    private:
        ServerConfig config;
        // held by pointer so that running stays valid while the list changes
        std::vector<std::unique_ptr<ScheduledTest>> tests;
        ScheduledTest *running = nullptr;

        void SetState(ScheduledTest &test, ScheduledTest::State state);
        void PublishStatus(const ScheduledTest &test);
        void SaveState(const ScheduledTest &test);
        CommandError SaveTest(const ScheduledTest &test, const std::string &json);
        void SaveSources(const ScheduledTest &test, const TsdSources &sources);

        CommandError CheckSetup(const CommandMessage::TestSetup &command, std::chrono::system_clock::time_point &time);
        CommandError CheckOverlap(const std::string &testId, std::chrono::system_clock::time_point time, double duration);
        CommandError CheckPreparationQueue(const std::string &testId, std::chrono::system_clock::time_point time, double work);
        CommandError CreateTest(
            const CommandMessage::TestSetup &command,
            std::chrono::system_clock::time_point time,
            Rid rid,
            ScheduledTest::State state,
            std::unique_ptr<ScheduledTest> &out
        );

        CommandError RestoreTest(const std::filesystem::path &directory, const SavedTest &saved);
        void DropSavedTest(const std::filesystem::path &directory, const std::string &error);
        void FailSavedTest(const std::filesystem::path &directory, SavedTest saved, const std::string &error);
        void LogTests();

    public:
        std::function<void(const std::string &testId, TestStatusMessage::TestStatus status)> onStatus;
        std::function<void(const std::string &text)> onError;

        void Configure(const ServerConfig &config_);

        CommandError Schedule(const CommandMessage::TestSetup &command);
        // a running test has to be stopped first
        CommandError Cancel(const std::string &testId);
        void Restore();

        // the test being prepared or played, there is at most one
        ScheduledTest *Running();

        // the ready test to play now, the ready tests that can no longer start are failed
        ScheduledTest *DueForPlayback(std::chrono::system_clock::time_point now, std::chrono::system_clock::duration lead);
        // the next pending test, while nothing is running
        ScheduledTest *DueForPreparation();

        // pending -> preparing, ready -> playing
        void Started(ScheduledTest &test);
        // preparing -> ready, playing -> done, or failed from any state
        void Finished(ScheduledTest &test, bool success);
        // the test keeps its state
        void Stopped(ScheduledTest &test);
        void EraseFinished();

        // only reads the test, it can run on another thread
        static void Prepare(const ScheduledTest &test, std::stop_token stop);
    };

}
