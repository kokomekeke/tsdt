#pragma once
#include <numbers>
#include "Parser/Parser.hpp"
#include "Parsers.hpp"

namespace Sagax::SCG {

    Parser::Registry<SignalParser> GetSignalParserRegistry();

}
