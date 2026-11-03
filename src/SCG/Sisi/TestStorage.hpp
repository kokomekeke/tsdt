#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>
#include "Common/Logger.hpp"
#include "CommandError.hpp"
#include "SavedTest.hpp"
#include "TsdTransform.hpp"

namespace Sagax::SCG {

    // the directories of the tests under the recording directory, each one named by its
    // test id. test.json holds the setup command and the last status of the test, tsd.json
    // the tsd the command named and rid_000.json the rid of every step that named one. a
    // restored test is built again from these. everything else in a directory belongs to
    // the preparation
    //
    // the functions only read the members, they can run on any thread once Configure is done
    struct TestStorage {
    private:
        Common::Logger::Handle lh = Common::Logger::Handle("TestStorage");
        std::filesystem::path root;

    public:
        void Configure(const std::string &recordingDir);

        std::string Directory(const std::string &testId) const;
        // the directories that hold a test.json
        std::vector<std::string> SavedTestIds() const;

        // creates the directory
        CommandError SaveSources(const std::string &testId, const TsdSources &sources) const;
        // the counterpart of FetchTsd
        CommandError LoadSources(const std::string &testId, TsdSources &out) const;

        // a failure is only logged
        void SaveState(const SavedTest &saved) const;
        // the test id in test.json has to be the name of the directory
        CommandError LoadState(const std::string &testId, SavedTest &out) const;

        // removes everything the preparation wrote
        void ClearPreparation(const std::string &testId) const;

        // a failure is only logged
        void Remove(const std::string &testId) const;
    };

}
