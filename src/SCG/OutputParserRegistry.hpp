#pragma once
#include "Parser/JsonParser.hpp"
#include "Parser/Parser.hpp"
#include "Parsers.hpp"
#include "Pipeline/PipelineBuilder.hpp"
#include "SSG/Modules/IQFileSink.hpp"
#include "SSG/Modules/Radio.hpp"
#include "Common/Assert.hpp"

namespace Sagax::SCG {

#ifdef HAVE_USRP
struct UsrpB200OutputParser : public OutputParser {
	Parser::Json::BasicParameter<std::string> serialP = AddParameter(&serialP, "string", "serial", "Serial number of the USRPB200 device");
    Sagax::Parser::Json::BasicParameter<std::string> subdevP = AddParameter(&subdevP, "string", "subdevSpec");
    Sagax::Parser::Json::ArrayParameter<Sagax::Parser::Json::BasicParameter<double>> gainP = AddParameter(&gainP, "array<double>", "gains");

	Sagax::Pipeline::PipelineBuilder SetupRadio() override {
        Sagax::Pipeline::PipelineBuilder b;
        Common::Assert(!serialP.defaulted, "Serial number must be specified for USRP Output");
        Common::Assert(!subdevP.defaulted, "subdevSpec must be specified");
        Common::Assert(!gainP.defaulted, "gains must be specified");
        Common::Assert(gainP.data.size() == nChannelsP.data, "gain count mistmatch");
		b.AddVertex(
            [=, *this](){
                using namespace SSG::Modules;
                std::unique_ptr<Radio> mdl = std::make_unique<Radio>();
                mdl->serial = serialP.data;
                mdl->subdevSpec = subdevP.data;
                mdl->nChannels = nChannelsP.data;
                mdl->sampleRate = sampleRateP.data;
                mdl->frequencies.clear();
                for(auto &f : freqP.data) {
                    mdl->frequencies.push_back(f.data);
                }
                mdl->gains.clear();
                for(auto &g : gainP.data) {
                    mdl->gains.push_back(g.data);
                }
                return mdl;
            },
            1,
            0
        );
        return b;
    }
};
#endif

Sagax::Parser::Registry<OutputParser> GetOutputRegistry() {
	Sagax::Parser::Registry<OutputParser> registry;
#ifdef HAVE_USRP
	registry.Register<UsrpB200OutputParser>("usrpb200");
#endif
	return registry;
}

}
