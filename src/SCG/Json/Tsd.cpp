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

    Tsd ParseTsd(const std::string &s) {
        Json::JsonHandle j = Json::ParseJson(s);
        Tsd tsd;
        tsdParser.Parse(j, tsd);
        return tsd;
    }

    std::string SerializeTsd(const Tsd &t) {
        Json::JsonHandle j;
        tsdParser.Serialize(t, j);
        return Json::DumpJson(j, true);
    }

}
