#include "Common/Assert.hpp"
#include "OutputVariant.hpp"
#include <iostream>

namespace Sagax::SCG {
    OutputVariant::CommonParams OutputVariant::GetCommonParams() const {
        CommonParams params;
        bool found = false;
        if(auto *s = std::get_if<FileIQ>(&variant)) {
            found = true;
            params.nChannels = s->nChannels;
            params.sampleRate = s->sampleRate;
            params.centers.assign(s->nChannels, s->center);
        }
        if(auto *s = std::get_if<RadioUsrpB200>(&variant)) {
            found = true;
            params.nChannels = 1;
            params.sampleRate = s->sampleRate;
            params.centers = { s->center };
        }
        if(auto *s = std::get_if<RadioSidekiq>(&variant)) {
            found = true;
            params.nChannels = s->nChannels;
            params.sampleRate = s->sampleRate;
            params.centers = s->centers;
        }
        if([[maybe_unused]] auto *s = std::get_if<Null>(&variant)){
            found = true;
            Common::Assert(false, "null output");
        }
        Common::Assert(found, "failed to get common parameters");
        return params;
    }
}

