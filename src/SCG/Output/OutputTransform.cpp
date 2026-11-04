#include <vector>
#include <variant>
#include <format>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Output/FileOutputDevice.hpp"
#include "SCG/Output/MockOutputDevice.hpp"
#include "SCG/Output/UsrpB200Device.hpp"
#include "SCG/Output/SidekiqDevice.hpp"
#include "OutputTransform.hpp"

namespace Sagax::SCG {

    std::tuple<
        std::unique_ptr<OutputDevice>,
        std::vector<Resource*>
    >
    GetOutputDevice(
        const PipelineConfig &config,
        const OutputVariant &v
    ) {
        std::unique_ptr<OutputDevice> basePtr;
        std::vector<Resource*> resources;
        if([[maybe_unused]] auto *s = std::get_if<OutputVariant::Null>(&v.variant)) {
            Common::Assert(false, "null output");
        }
        if(auto *s = std::get_if<OutputVariant::FileIQ>(&v.variant)) {
            auto p = std::make_unique<FileOutputDevice>();
            p->fileName = s->fileName;
            p->nChannels = s->nChannels;
            p->sampleRate = s->sampleRate;
            p->center = s->center;
            basePtr = std::move(p);
        }

        if(auto *s = std::get_if<OutputVariant::RadioUsrpB200>(&v.variant)) {
            if(config.mockRadios) {
                auto p = std::make_unique<MockOutputDevice>();
                p->serial = s->serial;
                p->sampleRate = s->sampleRate;
                p->nChannels = 1;
                p->centers = { s->center };
                p->bufferSize = mockBufferSizeUsrpB200;
                basePtr = std::move(p);
            }
            else {
#ifdef HAVE_USRP
                auto p = std::make_unique<UsrpB200Device>();
                Common::Logger::Debug("s->serial:{}",s->serial);
                p->serial = s->serial;
                p->subdevSpec = s->subdevSpec;
                p->sampleRate = s->sampleRate;
                p->center = s->center;
                p->gain = s->gain;
                {
                    auto &u = config.preinit.usrp;
                    auto it = u.devices.find(s->serial);
                    if(it == u.devices.end()) {
                        Common::Assert(!u.usePreinitOnly, "device was not preinitialized");
                    }
                    else {
                        p->devPreinit = it->second.get();
                    }
                }
                resources.push_back((Resource*)(p.get()));
                basePtr = std::move(p);
#else
                Common::Assert(false, "usrp output device not available");
#endif
            }
        }
        if(auto *s = std::get_if<OutputVariant::RadioSidekiq>(&v.variant)) {
            if(config.mockRadios) {
                auto p = std::make_unique<MockOutputDevice>();
                p->serial = s->serial;
                p->sampleRate = s->sampleRate;
                p->nChannels = s->nChannels;
                p->centers = s->centers;
                p->bufferSize = mockBufferSizeSidekiq;
                basePtr = std::move(p);
            }
            else {
#ifdef HAVE_SIDEKIQ
                auto p = std::make_unique<SidekiqDevice>();
                p->serial = s->serial;
                p->nChannels = s->nChannels;
                p->sampleRate = s->sampleRate;
                p->centers = s->centers;
                p->gains = s->gains;
                p->handlers = s->handlers;
                {
                    auto &u = config.preinit.sidekiq;
                    auto it = u.devices.find(s->serial);
                    if(it == u.devices.end()) {
                        Common::Assert(!u.usePreinitOnly, "device was not preinitialized");
                    }
                    else {
                        p->devPreinit = it->second.get();
                    }
                }
                resources.push_back((Resource*)(p.get()));
                basePtr = std::move(p);
#else
                Common::Assert(false, "sidekiq output device not available");
#endif
            }
        }
        Common::Assert(bool(basePtr), "failed to create output device");
        return { std::move(basePtr), resources };
    }

}
