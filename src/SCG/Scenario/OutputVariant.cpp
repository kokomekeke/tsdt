#include "Common/Assert.hpp"
#include "OutputVariant.hpp"

namespace Sagax::SCG {
    OutputVariant::CommonParams OutputVariant::GetCommonParams() {
        CommonParams params;
        bool found = false;
        if(auto *s = std::get_if<Null>(&variant)){
            found = true;
            Common::Assert(false, "null output");
        }
        if(auto *s = std::get_if<RadioUsrpB200>(&variant)) {
            found = true;
            params.nChannels = 1;
            params.centers = { s->center };
            params.sampleRate = s->sampleRate;
        }
        if(auto *s = std::get_if<FileIQ>(&variant)) {
            found = true;
            params.nChannels = s->nChannels;
            params.sampleRate = s->sampleRate;
            params.centers.assign(s->nChannels, s->center);
        }
        Common::Assert(found, "failed to get common parameters");
        return params;
    }
}

