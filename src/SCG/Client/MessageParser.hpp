#pragma once
#include <string>
#include "Messages.hpp"

namespace Sagax::SCG {

    // these parsers here and the ones in "JsonParsing/" should have some common abstraction.
    // (SSGEXE and SDD have the same)
    // the existing attempt at that is so bad that i wasnt willing to reuse it.
    // these are basically placeholders that will throw nondescriptive exceptions coming from nlohmann/json
    // whenever there is any kind of problem.

    struct CommandMessageParser {
        CommandMessage Parse(const std::string &s);
        std::string Serialize(const CommandMessage &t);
    };

    struct LogMessageParser {
        LogMessage Parse(const std::string &s);
        std::string Serialize(const LogMessage &t);
    };
}


