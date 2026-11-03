#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stop_token>
#include <string>

#include "Common/Logger.hpp"
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
        std::string help;
        std::string configPath;
    };

    struct Server {
    private:
        Common::Logger::Handle lh = Common::Logger::Handle("Server");

        struct SignalEvent {
            std::int32_t eventId;
            bool active;
        };

        // mtx guards programRunning and signalEvents. the main loop holds it while it waits
        // the event callbacks take mtx while a sink stage holds its stage mutex, mtx is never held
        // while the engine is stopped or joined
        // signalEvents is filled by the event callbacks ahead of the event times
        std::mutex mtx;
        std::condition_variable cv;
        bool programRunning = true;
        std::multimap<std::chrono::system_clock::time_point, SignalEvent> signalEvents;

        MqttClient client;
        SCG::ServerConfig config;
        SCG::PipelineConfig pipelineConfig;

        // scheduler outlives threadPool, the preparation running there reads the tests
        TestScheduler scheduler;
        std::stop_source preparationStop;
        std::future<std::string> preparation;

        // threadPool outlives the engine in ctx
        Common::ThreadPool threadPool;
        SCG::ScenarioPipeline ctx;

        // only the main loop touches these
        std::optional<std::size_t> playingPart;
        std::set<std::int32_t> activeEventIds;
        std::optional<SignalStatusMessage::SignalStatus> lastSignalStatus;
        std::chrono::steady_clock::duration statusPeriod{};
        std::chrono::steady_clock::time_point nextSignalStatus;

        void Configure();
        void ConfigureMqtt();
        void QueueSignalEvent(std::int32_t eventId, bool active, std::chrono::system_clock::time_point time);
        void ApplySignalEvents();
        void ClearSignalEvents();
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
        void StopActive();
        void StartPart(ScheduledTest &test);
        void StopPipeline();
        bool FinishPart(ScheduledTest &test);
        void UpdatePlayback(ScheduledTest &test, std::chrono::system_clock::time_point now);
        void UpdateSchedule();
        void Wait();
        void Interpreted(const std::string &topic, const char *command);
        void Ignored(const std::string &topic, const char *command);
        void HandleCommand(const MqttClient::Message &msg);
        void HandleCommands();
        bool Setup(Parameters params);
        void Run();

    public:
        int Main(Parameters params);
    };

}
