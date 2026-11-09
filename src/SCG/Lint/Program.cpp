#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>
#include <string>

#include "Common/Logger.hpp"
#include "Common/Defer.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"

#include "SCG/Sisi/ServerConfig.hpp"
#include "SCG/Json/ServerConfig.hpp"

#include "SCG/Sisi/Rid.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Sisi/Tsd.hpp"
#include "SCG/Json/Tsd.hpp"
#include "SCG/Sisi/TsdTransform.hpp"
#include "SCG/Utility/Fetch.hpp"

#include "Program.hpp"

namespace Sagax::SCG::Lint {

    namespace {

        constexpr int EXIT_OK = 0;
        constexpr int EXIT_VALIDATION_FAILED = 1;
        constexpr int EXIT_ERROR = 2;

        std::string ReadFile(const std::string &pathStr) {
            std::filesystem::path path = std::filesystem::absolute(pathStr);
            if(!std::filesystem::exists(path)) {
                throw std::runtime_error(std::format("file missing: {}", path.string()));
            }
            std::ifstream file(path);
            if(!file.is_open()) {
                throw std::runtime_error(std::format("failed to open file: {}", path.string()));
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }

        template<typename Result>
        bool Report(const std::string &label, const Result &res) {
            for(auto &w : res.warnings) {
                Common::Logger::Warn("{}", w.c_str());
            }
            for(auto &e : res.errors) {
                Common::Logger::Error("{}", e.c_str());
            }
            if(res.errors.empty()) {
                Common::Logger::Info("{}: valid ({} warning(s))", label.c_str(), res.warnings.size());
                return true;
            }
            else {
                Common::Logger::Error("{}: invalid ({} error(s), {} warning(s))", label.c_str(), res.errors.size(), res.warnings.size());
                return false;
            }
        }

        bool ReadConfig(const std::string &configPath, ServerConfig &out) {
            std::string jsonStr;
            try {
                jsonStr = ReadFile(configPath);
            }
            catch(const std::exception &e) {
                Common::Logger::Error("failed to read config file:\n{}", e.what());
                return false;
            }

            Json::JsonIssues issues;
            if(!ParseServerConfig(jsonStr, out, issues)) {
                Common::Logger::Error("failed to parse config file:\n{}", issues.Format());
                return false;
            }
            return true;
        }

        int LintScenario(const std::string &scenarioPath) {
            std::string jsonStr;
            std::string scenName;
            try {
                jsonStr = ReadFile(scenarioPath);
                scenName = std::filesystem::absolute(scenarioPath).filename().stem().string();
            }
            catch(const std::exception &e) {
                Common::Logger::Error("failed to read scenario file:\n{}", e.what());
                return EXIT_ERROR;
            }

            Scenario scenario;
            Json::JsonIssues issues;
            if(!ParseScenario(jsonStr, scenName, scenario, issues)) {
                Common::Logger::Error("failed to parse scenario file:\n{}", issues.Format());
                return EXIT_ERROR;
            }

            auto res = ValidateScenario(scenario);
            return Report("scenario", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
        }

        int LintConfig(const std::string &configPath) {
            ServerConfig config;
            if(!ReadConfig(configPath, config)) {
                return EXIT_ERROR;
            }

            auto res = ValidateConfig(config);
            return Report("config", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
        }

        int LintRid(const std::string &configPath, const std::string &ridPath) {
            ServerConfig config;
            if(!ReadConfig(configPath, config)) {
                return EXIT_ERROR;
            }

            {
                auto res = ValidateConfig(config);
                if(!Report("config", res)) {
                    Common::Logger::Error("config is invalid; skipping rid validation");
                    return EXIT_VALIDATION_FAILED;
                }
            }

            std::string jsonStr;
            {
                std::string error = Fetch(ridPath, config.scenarioFetchTimeout_s, jsonStr);
                if(!error.empty()) {
                    Common::Logger::Error("failed to read the rid:\n{}", error.c_str());
                    return EXIT_ERROR;
                }
            }

            Rid rid;
            {
                Json::JsonIssues issues;
                if(!ParseRid(jsonStr, rid, issues)) {
                    Common::Logger::Error("failed to parse rid file:\n{}", issues.Format());
                    return EXIT_ERROR;
                }
            }

            auto res = ValidateRid(config, rid);
            return Report("rid", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
        }

        int LintTsd(const std::string &configPath, const std::string &tsdPath) {
            ServerConfig config;
            if(!ReadConfig(configPath, config)) {
                return EXIT_ERROR;
            }

            {
                auto res = ValidateConfig(config);
                if(!Report("config", res)) {
                    Common::Logger::Error("config is invalid; skipping tsd validation");
                    return EXIT_VALIDATION_FAILED;
                }
            }

            // the tsd functions log through a handle, the linter has none of its own
            Common::Logger::Handle lh;
            TsdSources sources;
            {
                CommandError error = FetchTsd(lh, config, tsdPath, sources);
                if(!error.empty()) {
                    Common::Logger::Error("failed to read the tsd:\n{}", error.c_str());
                    return EXIT_ERROR;
                }
            }

            {
                CommandError error = ValidateTsdSources(lh, config, sources);
                if(!error.empty()) {
                    Common::Logger::Error("{}", error.c_str());
                    return EXIT_VALIDATION_FAILED;
                }
            }

            MergedTsd merged;
            {
                CommandError error = MergeTsd(sources.tsd, sources.rids, config.startAhead_s, merged);
                if(!error.empty()) {
                    Common::Logger::Error("the steps cannot be merged: {}", error.c_str());
                    return EXIT_VALIDATION_FAILED;
                }
            }

            Common::Logger::Info(
                "{} step(s), {} part(s), {}s in all",
                sources.tsd.steps.size(),
                merged.parts.size(),
                merged.duration_s
            );

            return EXIT_OK;
        }

    }

    int Program::Main(Parameters params) {
        Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
            .file = false,
            .function = false
        });
        Common::Logger::AddStream(&logStream);
        Common::Defer disposeLogger([&]() {
            Common::Logger::RemoveStream(&logStream);
        });

        switch(params.command) {
            case Parameters::Command::scenario: return LintScenario(params.scenarioPath);
            case Parameters::Command::config: return LintConfig(params.configPath);
            case Parameters::Command::rid: return LintRid(params.configPath, params.ridPath);
            case Parameters::Command::tsd: return LintTsd(params.configPath, params.tsdPath);
            case Parameters::Command::none: {
                Common::Logger::Error("\n{}", params.error);
                Common::Logger::Info("\n{}", params.help);
                return EXIT_ERROR;
            }
        }
        return EXIT_ERROR;
    }

}
