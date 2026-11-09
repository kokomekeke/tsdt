#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "SCG/Utility/Job.hpp"
#include "CommandError.hpp"
#include "Messages.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"
#include "Rid.hpp"
#include "SavedTest.hpp"
#include "ServerConfig.hpp"
#include "TestStorage.hpp"
#include "TsdTransform.hpp"

namespace Sagax::SCG {

    struct ScheduledTest {
        // pending: waits for its preparation
        // preparing: the directory is set up and the jobs run
        // ready: prepared, waits for its playback
        // starting: holds the playback, the time of the test has not come yet
        // playing: from the time of the test until its end, while a part plays and between them
        // done, failed: erased from the list by EraseFinished, together with the directory
        //
        // a preparing, starting or playing test is the active one. there is at most one, and
        // nothing else is prepared or played while it is there
        enum struct State {
            pending,
            preparing,
            ready,
            starting,
            playing,
            done,
            failed
        };

        std::string testId;
        CommandMessage::TestSetup command;
        // the start and the end of the tsd
        std::chrono::system_clock::time_point time;
        std::chrono::system_clock::time_point end;
        // there is at least one part. the pipeline of a part starts start_ahead before the time
        // of the part, the pipeline is moved out to play
        TsdScenario scenario;
        std::size_t nextPart = 0;
        SavedTest::Gnss gnss;
        std::string directory;
        State state = State::pending;
        // TEST_START came. the pipeline of a part hands no sample to the radios before it,
        // and a test that is not authorized by its time fails
        bool authorized = false;
    };

    // the tests from their setup until they are over. the caller runs the preparation and
    // the playback, asks the queries what to do and reports what it did with the setters.
    // the queries change nothing. a state change is published with onStatus and saved
    struct TestScheduler {
    private:
        Common::Logger::Handle lh = Common::Logger::Handle("TestScheduler");
        ServerConfig config;
        const PipelineConfig *pipelineConfig = nullptr;
        TestStorage storage;
        std::chrono::system_clock::time_point now;
        // pointers to the tests stay valid while the list changes
        std::vector<std::unique_ptr<ScheduledTest>> tests;

        void SetState(ScheduledTest &test, ScheduledTest::State state, std::chrono::system_clock::time_point time);
        void PublishStatus(const ScheduledTest &test, std::chrono::system_clock::time_point time);
        void SaveState(const ScheduledTest &test);

        CommandError CheckSetup(const CommandMessage::TestSetup &command, std::chrono::system_clock::time_point &time);
        CommandError Accept(const CommandMessage::TestSetup &command);
        CommandError CheckOverlap(const ScheduledTest &test);
        CommandError CheckPreparationQueue(const ScheduledTest &test);
        CommandError CreateTest(
            const CommandMessage::TestSetup &command,
            std::chrono::system_clock::time_point time,
            const TsdSources &sources,
            ScheduledTest::State state,
            std::unique_ptr<ScheduledTest> &out
        );

        CommandError RestoreTest(const SavedTest &saved);
        void DropSavedTest(const std::string &testId, const std::string &error);
        void LogTests();

    public:
        std::function<void(const std::string &testId, TestStatusMessage::TestStatus status, std::chrono::system_clock::time_point time)> onStatus;
        std::function<void(const std::string &testId, const std::string &text)> onError;

        // pipelineConfig_ has to outlive the tests, their pipelines point into it
        void Configure(const ServerConfig &config_, const PipelineConfig &pipelineConfig_);

        // the time of every query, check and published status. the system clock can jump, so
        // it may be earlier than the one set before
        void SetNow(std::chrono::system_clock::time_point now_);

        // a refused setup is published as failed, unless its test id is already scheduled
        CommandError Schedule(const CommandMessage::TestSetup &command);
        // until the time of the test
        CommandError Authorize(const std::string &testId);
        // the caller stops the preparation or the playback of the test first
        CommandError Cancel(const std::string &testId);
        void Restore();

        // queries

        ScheduledTest *Active();
        // the pending test that starts first, when no test is active and its preparation
        // would end before the start_ahead of every ready test
        ScheduledTest *DueForPreparation();
        // the ready test whose start_ahead has come, when no test is active
        ScheduledTest *DueForPlayback();
        // the next part of the active test, once its start_ahead has come
        bool PartDue(const ScheduledTest &test);
        // every part has started and the end of the active test has come
        bool PlaybackOver(const ScheduledTest &test);
        // the earliest time after now at which a query above changes its answer or
        // FailMissed fails a test, by the clock alone
        std::optional<std::chrono::system_clock::time_point> NextTime();

        // setters
        // each one asserts the state it comes from, which is the one before it in State
        // SetPlaying and SetDone publish the time and the end of the test instead of now
        // SetFailed works from any state

        // the pending and ready tests whose time has come are failed
        void FailMissed();
        void SetPreparing(ScheduledTest &test);
        void SetReady(ScheduledTest &test);
        void SetStarting(ScheduledTest &test);
        void SetPlaying(ScheduledTest &test);
        void SetPartStarted(ScheduledTest &test);
        void SetDone(ScheduledTest &test);
        void SetFailed(ScheduledTest &test);
        void EraseFinished();

        // only reads the test and the storage, it can run on another thread
        void Prepare(const ScheduledTest &test, std::stop_token stop) const;
    };

}
