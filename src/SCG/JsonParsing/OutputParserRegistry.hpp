#pragma once
#include "Parser/JsonParser.hpp"
#include "Parser/Parser.hpp"
#include "Parsers.hpp"

namespace Sagax::SCG {

    Parser::Registry<OutputParser> GetOutputParserRegistry();

}
