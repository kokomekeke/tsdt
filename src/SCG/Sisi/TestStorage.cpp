#include <format>
#include <fstream>
#include <sstream>
#include "Common/Assert.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/Scenario.hpp"
#include "SCG/Json/Tsd.hpp"
#include "TestStorage.hpp"

namespace Sagax::SCG {

    namespace {

        constexpr const char *testFileName = "test.json";
        constexpr const char *tsdFileName = "tsd.json";

        std::string StepRidFileName(std::size_t step) {
            return std::format("rid_{:03}.json", step);
        }

        bool IsSourceFile(const std::filesystem::path &name) {
            std::string text = name.string();
            if(text == testFileName || text == tsdFileName) {
                return true;
            }
            return text.starts_with("rid_") && text.ends_with(".json");
        }

        // a crash leaves either the old or the new content
        void WriteTextFile(const std::filesystem::path &path, const std::string &content) {
            auto temporary = path;
            temporary += ".tmp";
            {
                std::ofstream file(temporary, std::ios::binary);
                file << content;
                file.close();
                Common::Assert(!file.fail(), std::format("could not write {}", temporary.string()));
            }
            std::filesystem::rename(temporary, path);
        }

        CommandError ReadTextFile(const std::filesystem::path &path, std::string &out) {
            std::ifstream file(path, std::ios::binary);
            if(!file.is_open()) {
                return std::format("could not open {}", path.string());
            }
            std::stringstream buf;
            buf << file.rdbuf();
            out = buf.str();
            return {};
        }

    }

    void TestStorage::Configure(const std::string &recordingDir) {
        root = std::filesystem::absolute(recordingDir);
    }

    std::string TestStorage::Directory(const std::string &testId) const {
        return (root / testId).string();
    }

    std::vector<std::string> TestStorage::SavedTestIds() const {
        std::vector<std::string> ids;
        std::error_code ec;
        if(!std::filesystem::is_directory(root, ec)) {
            return ids;
        }
        for(auto &entry : std::filesystem::directory_iterator(root, ec)) {
            if(std::filesystem::exists(entry.path() / testFileName, ec)) {
                ids.push_back(entry.path().filename().string());
            }
        }
        return ids;
    }

    CommandError TestStorage::SaveSources(const std::string &testId, const TsdSources &sources) const {
        try {
            std::filesystem::path directory = root / testId;
            std::filesystem::create_directories(directory);
            WriteTextFile(directory / tsdFileName, sources.tsdJson);
            for(std::size_t i = 0; i < sources.ridJsons.size(); ++i) {
                if(!sources.tsd.steps[i].interference.has_value()) {
                    continue;
                }
                WriteTextFile(directory / StepRidFileName(i), sources.ridJsons[i]);
            }
        }
        catch(const std::exception &e) {
            return std::format("could not save test \"{}\": {}", testId, e.what());
        }
        return {};
    }

    CommandError TestStorage::LoadSources(const std::string &testId, TsdSources &out) const {
        std::filesystem::path directory = root / testId;
        TsdSources sources;
        SCG_CHECK(ReadTextFile(directory / tsdFileName, sources.tsdJson));
        {
            Json::JsonIssues issues;
            if(!ParseTsd(sources.tsdJson, sources.tsd, issues)) {
                return std::format("failed to parse {}:\n{}", tsdFileName, issues.Format());
            }
        }

        sources.rids.assign(sources.tsd.steps.size(), std::nullopt);
        sources.ridJsons.assign(sources.tsd.steps.size(), std::string());
        for(std::size_t i = 0; i < sources.tsd.steps.size(); ++i) {
            if(!sources.tsd.steps[i].interference.has_value()) {
                continue;
            }
            std::string fileName = StepRidFileName(i);
            SCG_CHECK(ReadTextFile(directory / fileName, sources.ridJsons[i]));
            Json::JsonIssues issues;
            if(!ParseRid(sources.ridJsons[i], sources.rids[i].emplace(), issues)) {
                return std::format("{}: failed to parse RID:\n{}", fileName, issues.Format());
            }
        }

        out = std::move(sources);
        return {};
    }

    void TestStorage::SaveState(const SavedTest &saved) const {
        try {
            Json::JsonIssues issues;
            std::optional<std::string> json = SerializeSavedTest(saved, issues);
            if(!json.has_value()) {
                Common::Logger::Warn(lh, "could not save the state of test \"{}\":\n{}", saved.command.testId, issues.Format());
                return;
            }
            WriteTextFile(root / saved.command.testId / testFileName, json.value());
        }
        catch(const std::exception &e) {
            Common::Logger::Warn(lh, "could not save the state of test \"{}\": {}", saved.command.testId, e.what());
        }
    }

    CommandError TestStorage::LoadState(const std::string &testId, SavedTest &out) const {
        std::string json;
        SCG_CHECK(ReadTextFile(root / testId / testFileName, json));
        Json::JsonIssues issues;
        if(!ParseSavedTest(json, out, issues)) {
            return std::format("could not parse {}:\n{}", testFileName, issues.Format());
        }
        if(out.command.testId != testId) {
            return std::format("the test id in {} is \"{}\"", testFileName, out.command.testId);
        }
        return {};
    }

    void TestStorage::ClearPreparation(const std::string &testId) const {
        std::vector<std::filesystem::path> leftovers;
        for(auto &entry : std::filesystem::directory_iterator(root / testId)) {
            if(!IsSourceFile(entry.path().filename())) {
                leftovers.push_back(entry.path());
            }
        }
        for(auto &path : leftovers) {
            std::filesystem::remove_all(path);
        }
    }

    CommandError TestStorage::SaveScenario(const std::string &testId, std::size_t iPart, const Scenario &scenario) const {
        Json::JsonIssues issues;
        std::optional<std::string> json = SerializeScenario(scenario, issues);
        if(!json.has_value()) {
            return std::format("could not write the scenario of part {}:\n{}", iPart, issues.Format());
        }
        WriteTextFile(root / testId / std::format("scenario_{:03}.json", iPart), json.value());
        return {};
    }

    void TestStorage::Remove(const std::string &testId) const {
        std::error_code ec;
        std::filesystem::remove_all(root / testId, ec);
        if(ec) {
            Common::Logger::Warn(lh, "could not remove {}: {}", Directory(testId), ec.message());
        }
    }

}
