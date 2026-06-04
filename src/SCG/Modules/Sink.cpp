#include <memory>
#include <algorithm>
#include "Common/Assert.hpp"
#include "Pipeline/Pipeline.hpp"
#include "SSG/IO/IQFileOut.hpp"
#include "SCG/Output/OutputDevice.hpp"
#include "MEventTag.hpp"
#include "Packets.hpp"
#include "Sink.hpp"

namespace Sagax::SCG {

    namespace {
        void UpdateEvents(
            std::vector<std::int32_t> &lastEvents,
            std::vector<std::int32_t> &&newEvents,
            std::function<void(std::int32_t)> eventStart,
            std::function<void(std::int32_t)> eventEnd
        ) {
            std::sort(newEvents.begin(), newEvents.end());

            auto it1 = lastEvents.begin();
            auto it2 = newEvents.begin();
            while(true) {
                bool e1 = it1 == lastEvents.end();
                bool e2 = it2 == newEvents.end();
                if(e1 && e2) {
                    break;
                }
                if(!e1 && (e2 || *it1 < *it2)) {
                    if(eventEnd) {
                        eventEnd(*it1);
                    }
                    ++it1;
                }
                else if(!e2 && (e1 || *it2 < *it1)) {
                    if(eventStart) {
                        eventStart(*it2);
                    }
                    ++it2;
                }
                else {
                    ++it1;
                    ++it2;
                }
            }

            lastEvents = std::move(newEvents);
        }
    }


    void StreamSink::Reset() {
        lastEvents.clear();
    }

    void StreamSink::Start() {
        dev->Start();
    }

    void StreamSink::Stop() {
        UpdateEvents(lastEvents, {}, eventStart, eventEnd); // kind of wrong to do it here if we want to pause scenarios
        dev->Stop();
    }

    void StreamSink::Process() {
        if(inP.nItems() == 0) {
            return;
        }
        auto &input = inP.Pop();

        std::vector<std::int32_t> newEvents;
        if(auto p = input.meta.GetEntry<MEventTag>()) {
            newEvents = p->events;
        }
        UpdateEvents(lastEvents, std::move(newEvents), eventStart, eventEnd);

        dev->Take(input);
    }




    void RecordSink::Reset() {
    }

    void RecordSink::Start() {
        SCGIQPacket dummy;
        dummy.Reshape(sampleRate, nChannels, 0, true);
        auto &m = dummy.meta.AddEntry<SSG::Packets::MFrequency>();
        m.frequency = center;
        file.Open(fileName, dummy);
        Common::Assert(file.IsOpen(), "failed to open file");
    }

    void RecordSink::Stop() {
        if(file.IsOpen()) {
            file.Close();
        }
    }

    void RecordSink::Process() {
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

}
