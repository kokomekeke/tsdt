#pragma once
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <format>
#include <chrono>
#include "Common/Assert.hpp"
#include "Common/Logger.hpp"
#include "SCG/Utility/Resource.hpp"
#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Json/Scenario.hpp"

namespace Sagax::SCG {

    // manages the recording directory
    // the directory will have the output channel recordings,
    // logs, and metadata
    //
    // TODO: make metadata json and add scg version

    struct RecordingDirectoryResource : public Resource {
    private:
        std::filesystem::path path;
        bool writeMode = false;
        std::ofstream logFile;
        std::unique_ptr<Common::StdLogStream> logStream;
        Scenario scenario;

    public:
        void SetupWrite(
            Scenario scenario_,
            const std::string &parentPath_,
            const std::string &recordingName_
        ) {
            Common::Assert(!parentPath_.empty(), "directory path cannot be empty");
            Common::Assert(!recordingName_.empty(), "recording name cannot be empty");

            std::filesystem::path parentPathAbs(parentPath_);
            parentPathAbs = std::filesystem::absolute(parentPathAbs);

            path = parentPathAbs / recordingName_;
            writeMode = true;
            scenario = scenario_;
        }

        void SetupRead(
            const std::string &path_
        ) {
            std::filesystem::path pathAbs(path_);
            pathAbs = std::filesystem::absolute(pathAbs);

            path = pathAbs;
            writeMode = false;

            {
                std::ifstream file(path / "scenario.json");
                std::stringstream buffer;
                buffer << file.rdbuf();
                Json::JsonIssues issues;
                if(!ParseScenario(buffer.str(), std::nullopt, scenario, issues)) {
                    throw std::runtime_error(std::format(
                        "could not parse {}:\n{}",
                        (path / "scenario.json").string(),
                        issues.Format()
                    ));
                }
            }
        }

        const std::filesystem::path &Path() const {
            return path;
        }

        std::string GetOutputFileName(std::size_t iOutput, std::size_t iChannel) const {
            return (path / std::format("OUT_{}_CH_{:02}.wav", scenario.outputs[iOutput].name, iChannel)).string();
        }

        const Scenario &GetScenario() {
            return scenario;
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

                {
                    Json::JsonIssues issues;
                    std::optional<std::string> str = SerializeScenario(scenario, issues);
                    if(!str.has_value()) {
                        throw std::runtime_error(std::format(
                            "could not write the scenario:\n{}", issues.Format()
                        ));
                    }
                    std::ofstream scenFile(path / "scenario.json");
                    scenFile << str.value();
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

