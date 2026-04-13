#include <variant>
#include <stdexcept>
#include <string>
#include "Parsers.hpp"
#include "SSG/Modules/IQSink.hpp"
#include "SSG/Modules/PacketSource.hpp"
#include "SSG/Modules/IQFileSink.hpp"
#include "SSG/Modules/IQFileSource.hpp"


namespace Sagax::SCG {

    Scenario::Output OutputParser::Get() {
        Common::Assert(!nameP.defaulted, "name must be specified");
        Common::Assert(nameP.data.length() > 0, "name cannot be empty");
        Common::Assert(nameP.data.find('.') == std::string::npos, "name cannot contain a \'.\'");

        Scenario::Output res;
        res.name = nameP.data;
        res.variant = GetVariant();
        return res;
    }

    Scenario::Signal SignalParser::Get() {
        Common::Assert(!nameP.defaulted, "name must be specified");
        Common::Assert(nameP.data.length() > 0, "name cannot be empty");
        Common::Assert(nameP.data.find('.') == std::string::npos, "name cannot contain a \'.\'");

        Scenario::Signal res;
        res.name = nameP.data;
        res.variant = GetVariant();
        return res;
    }

    Scenario::Event EventParser::Get() {
        Common::Assert(!signalP.defaulted, "signal must be specified");
        Common::Assert(!outputP.defaulted, "output must be specified");
        Common::Assert(!amplitudeP.defaulted, "amplitude must be specified");
        Common::Assert(!frequencyP.defaulted, "frequency must be specified");
        Common::Assert(!startP.defaulted, "start must be specified");
        Common::Assert(!endP.defaulted, "end must be specified");

        Scenario::Event e;
        e.start = startP.data;
        e.end = endP.data;
        e.amplitude = amplitudeP.data;
        e.frequency = frequencyP.data;
        e.signalName = signalP.data;

        {
            Common::Assert(outputP.data.find('.') != std::string::npos, "output specifier must have a \'.\' as separator");
            std::string oName = outputP.data.substr(0, outputP.data.find('.'));
            std::string cName = outputP.data.substr(outputP.data.find('.') + 1);
            int iChannel = std::stoi(cName);
            Common::Assert(iChannel >= 0, std::format("negative channel index", iChannel));

            e.outputName = oName;
            e.iChannel = std::size_t(iChannel);
        }

        return e;
    };


    Scenario ParseScenario(
        std::string str,
        std::optional<std::string> scenarioNameFallback,
        Sagax::Parser::Registry<SignalParser> &signalParserRegistry,
        Sagax::Parser::Registry<OutputParser> &outputParserRegistry
    ) {
        Scenario scen;

        auto j = nlohmann::json::parse(
            str,
            nullptr,
            true, // allow exceptions
            true, // ignore comments
            true // allow trailing commas
        );
        Common::Assert(j.is_object(), "scenario must be an object");

        if(j.contains("name")) {
            scen.name = j.at("name").get<std::string>();
        }
        else {
            scen.name = scenarioNameFallback.value_or("unnamed");
        }

        Common::Assert(j.contains("duration"), "duration must be specified");
        scen.duration = j.at("duration").get<double>();

        auto outputs = j["outputs"];
        Common::Assert(outputs.is_array(), "outputs has to be an array");
        scen.outputs.resize(outputs.size());
        for(size_t i = 0; i < outputs.size(); i++) {
            auto output = outputs[i];
            Common::Assert(output.is_object(), "outputs list element has to be an object");
            try {
                Common::Assert(output.contains("type"), "type must be specified");
                std::string type = output.at("type").get<std::string>();

                Common::Assert(outputParserRegistry.entries.contains(type), format("unknown output type \"{}\"", type));
                std::unique_ptr<OutputParser> p(outputParserRegistry.entries[type].ctor());
                p->Parse(output);
                scen.outputs[i] = p->Get();
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format("could not parse output [{}]: {}", i, e.what()));
            }
        }

        auto signals = j["signals"];
        Common::Assert(signals.is_array(), "signals has to be an array");
        scen.signals.resize(signals.size());
        for(size_t i = 0; i < signals.size(); i++) {
            auto signal = signals[i];
            Common::Assert(signal.is_object(), "signals list element has to be an object");
            try {
                Common::Assert(signal.contains("type"), "type must be specified");
                std::string type = signal.at("type").get<std::string>();

                Common::Assert(signalParserRegistry.entries.contains(type), format("unknown signal type \"{}\"", type));
                std::unique_ptr<SignalParser> p(signalParserRegistry.entries[type].ctor());
                p->Parse(signal);
                scen.signals[i] = p->Get();
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format("could not parse signal [{}]: {}", i, e.what()));
            }
        }

        auto events = j["events"];
        Common::Assert(events.is_array(), "events has to be an array");
        scen.events.resize(events.size());
        for(size_t i = 0; i < events.size(); i++) {
            auto &event = events[i];
            Common::Assert(event.is_object(), "events list element has to be an object");
            try {
                EventParser p;
                p.Parse(events[i]);
                scen.events[i] = p.Get();
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format("could not parse event [{}]: {}", i, e.what()));
            }
        }
        return scen;
    };
};