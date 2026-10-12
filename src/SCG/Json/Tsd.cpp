#include "Json/JsonParser.hpp"
#include "Tsd.hpp"

using namespace Sagax::Json;

namespace Sagax::SCG {
    namespace {

        struct StepParser : public JsonObjectParser<Tsd::Step> {
            StepParser() {
                AddParam(&Tsd::Step::duration_s, JsonPrimitiveParser<double>{}, "duration_seconds");
                AddParamNull(&Tsd::Step::interference, JsonPrimitiveParser<std::string>{}, "interference");
            }
        };

        struct TsdParser : public JsonObjectParser<Tsd> {
            TsdParser() {
                AddParam(&Tsd::name, JsonPrimitiveParser<std::string>{}, "name");
                AddParamDef(&Tsd::description, JsonPrimitiveParser<std::string>{}, std::string(""), "description");
                auto steps = AddParam(&Tsd::steps, JsonArrayParser<Tsd::Step>{}, "steps");
                steps->SetItemParser(StepParser{});
            }
        };

        TsdParser tsdParser;

    }

    bool ParseTsd(const std::string &s, Tsd &out, Json::JsonIssues &e) {
        return Json::ParseDocument<Tsd>(tsdParser, s, out, e);
    }

    std::optional<std::string> SerializeTsd(const Tsd &t, Json::JsonIssues &e) {
        return Json::SerializeDocument<Tsd>(tsdParser, t, true, e);
    }

}
