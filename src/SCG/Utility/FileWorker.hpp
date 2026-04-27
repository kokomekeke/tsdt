#pragma once
#include <mutex>
#include <algorithm>
#include "Common/Assert.hpp"
#include "Common/TS.hpp"
#include "Common/RingBuffer.hpp"
#include "SSG/IO/SampleFile.hpp"

namespace Sagax::SCG {

    template<typename T>
    struct FileReaderWorker {
    private:
        std::mutex mtx;
        std::condition_variable cv;
        Common::RingBuffer<SSG::Packets::SamplePacket<T>> buf;
        Common::TSWorker worker;

        SSG::IO::SampleFileIn<T> *file;
        SSG::IO::FilePlaybackMode::variant mode;
        std::size_t nSamples; // per packet;
        std::size_t nPackets; // in buffer

        std::size_t nSamplesRead = 0;
        std::size_t nPacketsRead = 0;

        bool headValid = false;
        SSG::Packets::SamplePacket<T> head;

        void Update() {
            if(!headValid) {
                std::size_t nRead = SSG::IO::FilePlaybackMode::GetNSamples(mode, file->Eof(), nSamples, nPacketsRead, nSamplesRead);
                head.data.clear();
                file->ReadPacket(head, nRead);
                nSamplesRead += head.nSamples;
                nPacketsRead += 1;

                if(file->Eof()) {
                    file->WrapAround();
                }
            }

            {
                std::unique_lock lck(mtx);
                cv.wait(lck, [&](){
                    return buf.Count() < nPackets || worker.doStop.load();
                });
                if(buf.Count() < nPackets) {
                    buf.PushRight(1);
                    std::swap(buf.GetRight1(), head);
                    headValid = false;
                }
            }
        }

    public:
        void Setup(SSG::IO::SampleFileIn<T> *file_, SSG::IO::FilePlaybackMode::variant mode_, std::size_t nPackets_, std::size_t nSamples_) {
            file = file_;
            mode = mode_;
            nSamples = nSamples_;
            nPackets = nPackets_;
            worker.start = [](){};
            worker.stop = [](){};
            worker.update = [this](){ this->Update(); };
        }

        void Start() {
            worker.Start();
            buf.Resize(std::int32_t(nPackets));
        }

        void Stop() {
            {
                std::unique_lock lck(mtx);
                worker.Stop();
            }
            cv.notify_all();
            worker.Join();
            head.SetNull(true);
            buf.Resize(0);
        }

        void Reset() {
            nSamplesRead = 0;
            nPacketsRead = 0;
        }

        bool SwapPacket(SSG::Packets::SamplePacket<T> &packet) {
            std::unique_lock lck(mtx);
            if(buf.Count() == 0) {
                return false;
            }
            std::swap(packet, buf.GetLeft1());
            buf.PopLeft(1);
            lck.unlock();
            cv.notify_all();
            return true;
        }

    };

    // TODO filewriterworker

}


