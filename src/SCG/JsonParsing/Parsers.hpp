#pragma once
#include <vector>
#include <functional>
#include "Parser/Parser.hpp"
#include "Parser/JsonParser.hpp"
#include "SCG/Scenario/Scenario.hpp"

namespace Sagax::SCG {

    struct OutputParser : public Sagax::Parser::Json::JsonParser<Scenario::Output> {
        Parser::Json::BasicParameter<std::string> nameP = AddParameter(&nameP, "string", "name");

        virtual ~OutputParser() = default;
        Scenario::Output Get() override;

        virtual OutputVariant GetVariant() = 0;
    };

    struct SignalParser : public Sagax::Parser::Json::JsonParser<Scenario::Signal> {
        Parser::Json::BasicParameter<std::string> nameP = AddParameter(&nameP, "string", "name");

        virtual ~SignalParser() = default;
        Scenario::Signal Get() override;

        virtual SignalVariant GetVariant() = 0;
    };

    struct EventParser : public Sagax::Parser::Json::JsonParser<Scenario::Event> {
        Parser::Json::BasicParameter<std::string> signalP = AddParameter(&signalP, "string", "signal");
        Parser::Json::BasicParameter<std::string> outputP = AddParameter(&outputP, "string", "output");
        Parser::Json::BasicParameter<double> amplitudeP = AddParameter(&amplitudeP, "double", "amplitude");
        Parser::Json::BasicParameter<double> frequencyP = AddParameter(&frequencyP, "double", "frequency");
        Parser::Json::BasicParameter<double> startP = AddParameter(&startP, "double", "start");
        Parser::Json::BasicParameter<double> endP = AddParameter(&endP, "double", "end");

        Scenario::Event Get() override;
    };

    Scenario ParseScenario(
        std::string str,
        std::optional<std::string> scenarioNameFallback,
        Parser::Registry<SignalParser> &signalParserRegistry,
        Parser::Registry<OutputParser> &outputParserRegistry
    );

};