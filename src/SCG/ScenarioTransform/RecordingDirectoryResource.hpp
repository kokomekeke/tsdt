#pragma once
#include <filesystem>
#include <iostream>
#include <fstream>
#include <memory>
#include <format>
#include <chrono>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    // manages the recording directory
    // the directory will have the output channel recordings,
    // logs, and metadata
    //
    // TODO: it should also have the scg version and the scenario in json format

    struct RecordingDirectoryResource : public Resource {
    private:
        std::filesystem::path path;
        bool writeMode = false;
        std::ofstream logFile;
        std::unique_ptr<Common::StdLogStream> logStream;
        Scenario scenario;

    public:
        void Setup(
            Scenario scenario_,
            const std::string &parentPath_,
            const std::string &recordingName_,
            bool writeMode_
        ) {
            Common::Assert(!parentPath_.empty(), "directory path cannot be empty");
            Common::Assert(!recordingName_.empty(), "recording name cannot be empty");

            std::filesystem::path parentPathAbs(parentPath_);
            parentPathAbs = std::filesystem::absolute(parentPathAbs);

            path = parentPathAbs / recordingName_;
            writeMode = writeMode_;
            scenario = scenario_;
        }

        const std::filesystem::path &Path() const {
            return path;
        }

        std::string GetOutputFileName(std::size_t iOutput, std::size_t iChannel) const {
            return (path / std::format("{}_{:03}_{:03}.wav", scenario.name, iOutput, iChannel)).string();
        }

    protected:
        void Init_() override {
            if(writeMode) {
                if(std::filesystem::exists(path) && !std::filesystem::is_empty(path)) {
                    Common::Logger::Warn("recording directory '{}' not empty, contents will be removed", path.string());
                }

                std::filesystem::remove_all(path);
                std::filesystem::create_directories(path);

                {
                    auto now = std::chrono::system_clock::now();
                    std::ofstream metaFile(path / "meta.txt");
                    metaFile << std::format(
                        "~~~ scg recording ~~~\n"
                        "scenario name: {}\n"
                        "created: {:%Y-%m-%d %H:%M:%S}\n",
                        scenario.name,
                        now
                    );
                }

                logFile.open(path / "log.txt");
                logStream = std::make_unique<Common::StdLogStream>(
                    logFile,
                    Common::Logger::Level::Trace,
                    Common::StdLogStream::TagOptions{ .colors = false }
                );
                Common::Logger::AddStream(logStream.get());
            }
            else {
                Common::Assert(std::filesystem::exists(path), "recording directory does not exist");
            }
        }

        void Dispose_() override {
            if(logStream) {
                Common::Logger::RemoveStream(logStream.get());
                logStream.reset();
            }
            logFile.close();
        }
    };

}

