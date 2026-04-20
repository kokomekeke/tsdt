#pragma once
#include <vector>
#include "Pipeline/Pipeline.hpp"
#include "SSG/Math/Math.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct Tuner : Pipeline::Module {
        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);
		Pipeline::OutPort<SCGIQPacket> outP = AddOutPort(&outP);

        // set before start
        double freqOut;

		double phase = 0.0;

		void Reset() override;
		void Start() override;
		void Stop() override;
		void Process() override;
    };

}