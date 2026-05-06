#pragma once
#include <mutex>
#include <algorithm>
#include "Common/Assert.hpp"
#include "Common/TS.hpp"
#include "Common/RingBuffer.hpp"
#include "SSG/IO/SampleFile.hpp"
#include "SSG/IO/PlaybackAdapter.hpp"

namespace Sagax::SCG {

    template<typename T>
    struct FileReaderWorker {
    private:
        std::mutex mtx;
        std::condition_variable cv;
        Common::RingBuffer<SSG::Packets::SamplePacket<T>> buf;
        Common::TSWorker worker;

        SSG::IO::PlaybackAdapter<T> fileAdapter;
        std::size_t nPackets; // in buffer

        bool headValid = false;
        SSG::Packets::SamplePacket<T> head;

        void Update() {
            if(!headValid) {
                auto plan = fileAdapter.GetReadPlan();
                if(!plan.doRead) {
                    return;
                }
                head.data.clear();
                head.meta.Clear();
                fileAdapter.CommitReadPlan(plan, head);
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
        void Setup(SSG::IO::SampleFileIn<T> *file_, SSG::IO::FilePlaybackMode::Variant mode_, std::size_t nPackets_, std::size_t nSamples_, bool doLog_) {
            fileAdapter.file = file_;
            fileAdapter.filename = file_->GetMeta().fileName;
            fileAdapter.mode = mode_;
            fileAdapter.nSamples = nSamples_;
            fileAdapter.doLog = doLog_;
            nPackets = nPackets_;
            worker.start = [this](){
                this->fileAdapter.Reset();
                this->fileAdapter.Start();
            };
            worker.stop = [](){}; // dont close here, the file is managed from the outside
            worker.update = [this](){
                this->Update();
            };
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
            fileAdapter.Reset();
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


