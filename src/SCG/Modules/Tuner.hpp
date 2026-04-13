#pragma once
#include <vector>
#include "Pipeline/Pipeline.hpp"
#include "SSG/Packets/Packets.hpp"
#include "SSG/Math/Math.hpp"

namespace Sagax::SCG {

    struct Tuner : Pipeline::Module {
        Pipeline::InPort<SSG::Packets::IQPacket> inP = AddInPort(&inP);
		Pipeline::OutPort<SSG::Packets::IQPacket> outP = AddOutPort(&outP);

        // set before start
        double freqOut;

		double phase = 0.0;

		void Reset() override;
		void Start() override;
		void Stop() override;
		void Process() override;
    };

}