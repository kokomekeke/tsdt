#pragma once
#include <vector>
#include <functional>
#include "Wrappers/JsonSane.hpp"
#include "Scenario.hpp"
#include "Parser/Parser.hpp"
#include "Parser/JsonParser.hpp"
#include "Pipeline/PipelineBuilder.hpp"

namespace Sagax::SCG {

    struct OutputParser : public Sagax::Parser::Json::JsonParser<Output> {
        virtual ~OutputParser() = default;

        Parser::Json::BasicParameter<std::string> nameP = AddParameter(&nameP, "string", "name");
        Parser::Json::BasicParameter<double> sampleRateP = AddParameter(&sampleRateP, "double", "samplerate");
        Parser::Json::BasicParameter<std::size_t> nChannelsP = AddParameter(&nChannelsP, "u64", "nChannels");
        Sagax::Parser::Json::ArrayParameter<Sagax::Parser::Json::BasicParameter<double>> freqP = AddParameter(&freqP, "array<double>", "frequencies");

        virtual Sagax::Pipeline::PipelineBuilder SetupRadio() = 0;

        // void Parse(const nlohmann::json &j) override;
        Output Get() override;
    };

    struct SignalParser : public Sagax::Parser::Json::JsonParser<Signal> {
        Parser::Json::BasicParameter<std::string> nameP = AddParameter(&nameP, "string", "name");

        virtual ~SignalParser() = default;

        virtual Sagax::Pipeline::PipelineBuilder Setup() = 0; // protected xd

        // void Parse(const nlohmann::json &j) override;
        Signal Get() override;
    };

    struct EventParser : public Sagax::Parser::Json::JsonParser<Event> {
        Parser::Json::BasicParameter<std::string> signalP = AddParameter(&signalP, "string", "signal");
        Parser::Json::BasicParameter<std::string> outputP = AddParameter(&outputP, "string", "output");
        Parser::Json::BasicParameter<double> amplitudeP = AddParameter(&amplitudeP, "double", "amplitude");
        Parser::Json::BasicParameter<double> frequencyP = AddParameter(&frequencyP, "double", "frequency");
        Parser::Json::BasicParameter<double> startP = AddParameter(&startP, "double", "start");
        Parser::Json::BasicParameter<double> endP = AddParameter(&endP, "double", "end");

        Scenario &scen;
        EventParser(Scenario &scen_);

        // void Parse(const nlohmann::json &j) override;
        Event Get();
    };

    struct ScenarioParser {
        Scenario Parse(std::string name, std::string str, Parser::Registry<SignalParser> signalParserRegistry, Parser::Registry<OutputParser> outputParserRegistry);
    };

};