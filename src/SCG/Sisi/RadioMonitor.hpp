#pragma once
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "Common/ThreadPool.hpp"
#include "SCG/Scenario/PipelineConfig.hpp"
#include "ServerConfig.hpp"
#include "TsdTransform.hpp"

namespace Sagax::SCG {

    // a task on the thread pool that works on the radios of the server from Start to Stop,
    // between the tests as well
    // it sets the gain updates at their times, at most radioMonitorPrecision late
    // every radioMonitorTick it reads the temperature of every radio. the radios are not ready
    // from the moment one is at or above its upper temperature until each one that was has
    // been read below its lower temperature. a radio read at or above its lower temperature
    // after a read below it gets a warning, taken with TakeWarnings. a failed read keeps the
    // state of the radio
    // the temperatures read are logged about every radioTemperatureLogInterval
    // a radio without a device (mock_radios) only logs its updates and is never hot
    // mtx guards the radios and the updates and is held while a device is called, so once
    // SetGainUpdates has returned no update of the list before it is being set
    struct RadioMonitor {
    private:
        struct Radio {
#ifdef HAVE_USRP
            SSG::UsrpB200::Device *usrp = nullptr;
#endif
#ifdef HAVE_SIDEKIQ
            SSG::Sidekiq::Device *sidekiq = nullptr;
#endif
            double upperTemperature_celsius;
            double lowerTemperature_celsius;
            double temperature_celsius;
            bool warm; // at or above the lower temperature
            bool hot;
            double hotTemperature_celsius; // the read that made the radio hot
        };

        Common::Logger::Handle lh = Common::Logger::Handle("RadioMonitor");
        std::map<std::string, Radio> radios; // radio id -> radio
        std::mutex mtx;
        std::condition_variable cv;
        bool running = false;
        // ordered by time, the ones before iNextGainUpdate are set
        std::vector<TsdScenario::GainUpdate> gainUpdates;
        std::size_t iNextGainUpdate = 0;
        std::vector<std::string> warnings;
        std::future<void> task;

        void Run();
        void SetGain(const TsdScenario::GainUpdate &update);
        bool CheckTemperatures(bool log);

    public:
        // called on the task without mtx held, when the radios turn not ready
        std::function<void()> onNotReady;

        // threadPool and the devices of pipelineConfig have to outlive Stop
        void Start(Common::ThreadPool &threadPool, const ServerConfig &config, const PipelineConfig &pipelineConfig);
        // does nothing if the task is not running
        void Stop();
        // replaces the updates that are not set yet, an empty list clears them. an update
        // whose time has passed is set at once
        void SetGainUpdates(std::vector<TsdScenario::GainUpdate> updates);
        // hotRadios lists the radios that keep them from being ready, with the temperature that
        // made each one hot
        bool RadiosReady(std::string &hotRadios);
        // the warnings since the last call
        std::vector<std::string> TakeWarnings();
    };

}
