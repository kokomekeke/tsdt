#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <format>
#include <string>

#include "Vendored/argparse/argparse.hpp"
#include "Common/Logger.hpp"
#include "Common/Defer.hpp"

#include "SCG/Scenario/Scenario.hpp"
#include "SCG/Scenario/Validate.hpp"
#include "SCG/Json/Scenario.hpp"

#include "SCG/Sisi/ServerConfig.hpp"
#include "SCG/Json/ServerConfig.hpp"

#include "SCG/Sisi/Rid.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Sisi/RidTransform.hpp"
#include "SCG/Utility/Fetch.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

// scglint: parses and validates scenario/config/rid json files without touching any
// hardware (USRP/Sidekiq) or MQTT broker, and without building or running a pipeline.
// It only exercises the same Parse*/Validate* calls the real tools (scg.exe, scgserver.exe)
// run before they go anywhere near hardware.

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

    // Prints warnings/errors from a ValidationResult-shaped struct.
    // Returns true iff there were no errors.
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

    int LintScenario(const std::string &scenarioPath) {
        Scenario scenario;
        try {
            std::string jsonStr = ReadFile(scenarioPath);
            std::string scenName = std::filesystem::absolute(scenarioPath).filename().stem().string();
            scenario = ParseScenario(jsonStr, scenName);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("failed to parse scenario file:\n{}", e.what());
            return EXIT_ERROR;
        }

        auto res = ValidateScenario(scenario);
        return Report("scenario", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
    }

    int LintConfig(const std::string &configPath) {
        ServerConfig config;
        try {
            std::string jsonStr = ReadFile(configPath);
            config = ParseServerConfig(jsonStr);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("failed to parse config file:\n{}", e.what());
            return EXIT_ERROR;
        }

        auto res = ValidateConfig(config);
        return Report("config", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
    }

    int LintRid(const std::string &configPath, const std::string &ridPath) {
        ServerConfig config;
        try {
            std::string jsonStr = ReadFile(configPath);
            config = ParseServerConfig(jsonStr);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("failed to parse config file:\n{}", e.what());
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
        try {
            rid = ParseRid(jsonStr);
        }
        catch(const std::exception &e) {
            Common::Logger::Error("failed to parse rid file:\n{}", e.what());
            return EXIT_ERROR;
        }

        auto res = ValidateRid(config, rid);
        return Report("rid", res) ? EXIT_OK : EXIT_VALIDATION_FAILED;
    }

}

int main(int argc, char **argv) {
    Common::StdLogStream logStream(std::cerr, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
        .file = false,
        .function = false
    });
    Common::Logger::AddStream(&logStream);
    Common::Defer disposeLogger([&]() {
        Common::Logger::RemoveStream(&logStream);
    });

    argparse::ArgumentParser program("scglint.exe", "1.0", argparse::default_arguments::all);
    program.add_description(
        "Parses and validates SCG scenario/config/rid json files without requiring hardware "
        "or executing anything. Exit codes: 0 = valid, 1 = validation errors, 2 = parse/usage error."
    );

    argparse::ArgumentParser scenarioCmd("scenario");
    scenarioCmd.add_description("parse and validate a scenario json file");
    scenarioCmd.add_argument("scenario_path").help("path to scenario .json");

    argparse::ArgumentParser configCmd("config");
    configCmd.add_description("parse and validate a server config json file");
    configCmd.add_argument("config_path").help("path to server config .json");

    argparse::ArgumentParser ridCmd("rid");
    ridCmd.add_description("parse and validate a rid json file against a server config json");
    ridCmd.add_argument("config_path").help("path to server config .json");
    ridCmd.add_argument("rid_path").help("url of the rid .json, file:// or http://");

    program.add_subparser(scenarioCmd);
    program.add_subparser(configCmd);
    program.add_subparser(ridCmd);

    try {
        program.parse_args(argc, argv);
    }
    catch(const std::exception &e) {
        Common::Logger::Error("\n{}", e.what());
        Common::Logger::Info("\n{}", program.help().str());
        return EXIT_ERROR;
    }

    if(program.is_subcommand_used(scenarioCmd)) {
        return LintScenario(scenarioCmd.get<std::string>("scenario_path"));
    }
    else if(program.is_subcommand_used(configCmd)) {
        return LintConfig(configCmd.get<std::string>("config_path"));
    }
    else if(program.is_subcommand_used(ridCmd)) {
        return LintRid(ridCmd.get<std::string>("config_path"), ridCmd.get<std::string>("rid_path"));
    }
    else {
        Common::Logger::Error("no subcommand given");
        Common::Logger::Info("\n{}", program.help().str());
        return EXIT_ERROR;
    }
}
