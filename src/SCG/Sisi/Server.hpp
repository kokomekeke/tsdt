#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>

#include "Common/ThreadPool.hpp"
#include "Json/JsonParser.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"
#include "SCG/Scenario/ScenarioPipeline.hpp"
#include "CommandError.hpp"
#include "Messages.hpp"
#include "MqttClient.hpp"
#include "ServerConfig.hpp"
#include "TestScheduler.hpp"

namespace Sagax::SCG::Server {

    struct Parameters {
        bool success;
        // --help or --version was given, its text is already written
        bool helpRequested;
        std::string error;
        // written after a parse error
        std::string help;
        std::string configPath;
    };

    struct Program {
    private:
        // cleared by the signal callback under mtx. it starts true, so a signal that arrives
        // before the loop still stops it
        bool programRunning = true;
        std::mutex mtx;
        std::condition_variable cv;

        MqttClient client;
        SCG::ServerConfig config;
        SCG::PipelineConfig pipelineConfig;

        // declared before threadPool, the preparation running there reads the tests
        TestScheduler scheduler;
        std::stop_source preparationStop;
        std::future<std::string> preparation;

        // declared before ctx, so it outlives the engine in ctx. Main starts the threads
        // after the signal handler is in place
        Common::ThreadPool threadPool;
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

        void Configure();
        void SetEventActive(std::int32_t eventId, bool active);
        void ClearActiveEvents();
        void SetTransmissionStart(std::chrono::system_clock::time_point time);
        std::optional<std::chrono::system_clock::time_point> GetTransmissionStart();
        void ClearTransmissionStart();
        SignalStatusMessage::SignalStatus CurrentSignalStatus();
        void ReleaseRadios();
        bool StopRequested();
        void Publish(const std::string &topic, const std::string &payload);
        void PublishPayload(
            const std::string &topic,
            const std::optional<std::string> &payload,
            const Json::JsonIssues &issues
        );
        void PublishError(const std::string &text);
        void PublishTestStatus(const std::string &testId, TestStatusMessage::TestStatus status, std::chrono::system_clock::time_point time);
        void PublishSignalStatus();
        CommandError CancelTest(const CommandMessage::TestCancel &command);
        CommandError StopTransmission();
        void StartPreparation(ScheduledTest &test);
        bool PreparationDone();
        void FinishPreparation(ScheduledTest &test);
        void StopRunning();
        void StartPipeline(ScheduledTest &test);
        void StopPipeline();
        void FinishPipeline(ScheduledTest &test);
        void UpdateSchedule();
        void Interpreted(const std::string &topic, const char *command);
        void Ignored(const std::string &topic, const char *command);
        void HandleCommand(const MqttClient::Message &msg);
        void HandleCommands();
        bool Setup(Parameters params);
        void Run();

    public:
        // sets up logging, then reports a parse error or runs
        int Main(Parameters params);
    };

}
