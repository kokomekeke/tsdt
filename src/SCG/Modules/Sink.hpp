#pragma once
#include <memory>
#include "Common/Assert.hpp"
#include "Pipeline/Pipeline.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "Packets.hpp"

namespace Sagax::SCG {

    struct StreamSink : public Sagax::Pipeline::Module {
        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);

        std::unique_ptr<OutputDevice> dev;

        void Reset() override {
        }

        void Start() override {
            dev->Start();
        }

        void Stop() override {
            dev->Stop();
        }

        void Process() override {
            if(inP.nItems() == 0) {
                return;
            }

            // TODO log event tags

            auto &input = inP.Pop();
            dev->Take(input);
        }
    };

    struct RecordSink : public Sagax::Pipeline::Module {
        Pipeline::InPort<SCGIQPacket> inP = AddInPort(&inP);

        std::string fileName;
        double center;
        double sampleRate;
        std::size_t nChannels;
        SSG::IO::IQFileOut<SCGSampleBase> file;

        void Reset() override {
        }

        void Start() override {
            SCGIQPacket dummy;
            dummy.Reshape(sampleRate, nChannels, 0, true);
            auto &m = dummy.meta.AddEntry<SSG::Packets::MFrequency>();
            m.frequency = center;
            file.Open(fileName, dummy);
            Common::Assert(file.IsOpen(), "failed to open file");
        }

        void Stop() override {
            if(file.IsOpen()) {
                file.Close();
            }
        }

        void Process() override {
            if(inP.nItems() == 0) {
                return;
            }
            auto &packet = inP.Pop();

            Common::Assert(file.IsOpen(), "file not open");
            Common::Assert(packet.nChannels == nChannels, "wrong number of channels");
            // assert sample rate

            // TODO save event tags?

            file.WritePacket(packet);
        }
    };

}
