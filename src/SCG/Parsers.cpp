#include <variant>
#include <stdexcept>
#include <string>
#include "Parsers.hpp"
#include "SSG/Modules/IQSink.hpp"
#include "SSG/Modules/PacketSource.hpp"
#include "SSG/Modules/IQFileSink.hpp"
#include "SSG/Modules/IQFileSource.hpp"


namespace Sagax::SCG {
    using namespace Sagax::Pipeline;
    using namespace Sagax::SSG::Modules;
    using namespace Sagax::Common;

    EventParser::EventParser(Scenario &scen_) : scen(scen_) {
    }

    Event EventParser::Get() {
        auto &signals = scen.signals;
        auto &outputs = scen.outputs;

        Common::Assert(!signalP.defaulted, "signal must be specified");
        Common::Assert(!outputP.defaulted, "output must be specified");
        Common::Assert(!amplitudeP.defaulted, "amplitude must be specified");
        Common::Assert(!frequencyP.defaulted, "frequency must be specified");
        Common::Assert(!startP.defaulted, "start must be specified");
        Common::Assert(!endP.defaulted, "end must be specified");

        Event e;
        e.start = startP.data;
        e.end = endP.data;
        e.amplitude = amplitudeP.data;
        e.frequency = frequencyP.data;

        {
            auto it_s = std::find_if(signals.begin(), signals.end(), [&](const auto& s) {
                return s.name == signalP.data;
            });
            Common::Assert(it_s != signals.end(), std::format("signal {} not found", signalP.data));
            e.iSignal = it_s - signals.begin();
        }

        {
            Assert(outputP.data.find('.') != std::string::npos, "output specifier must have a \'.\' as separator");
            auto oName = outputP.data.substr(0, outputP.data.find('.'));
            auto cName = outputP.data.substr(outputP.data.find('.') + 1);

            auto it = std::find_if(outputs.begin(), outputs.end(), [&](const auto &o) {
                return o.name == oName;
            });
            Common::Assert(it != outputs.end(), std::format("Output no such output: {}", oName));
            e.iOutput = it - outputs.begin();

            int iChannel = std::stoi(cName);

            Common::Assert(iChannel >= 0 && iChannel < int(outputs[e.iOutput].nChannels), std::format("channel index out of range: {}", iChannel));
            e.iChannel = std::size_t(iChannel);
        }

        {
            double fStart = scen.outputs[e.iOutput].frequencies[e.iChannel] - scen.outputs[e.iOutput].sampleRate / 2.0;
            double fEnd = scen.outputs[e.iOutput].frequencies[e.iChannel] + scen.outputs[e.iOutput].sampleRate / 2.0;
            Common::Assert(e.frequency >= fStart && e.frequency <= fEnd, "frequency out of range for this channel");
        }

        {
            Common::Assert(e.start >= 0, "start must not be negative");
            Common::Assert(e.start < e.end, "start must be before end");
            Common::Assert(e.end <= scen.duration, "interval exceeds scenario duration");
        }

        return e;
    };


    Scenario ScenarioParser::Parse(
        std::string scen_name,
        std::string str,
        Sagax::Parser::Registry<SignalParser> signalParserRegistry,
        Sagax::Parser::Registry<OutputParser> outputParserRegistry
    ) {
        Common::Assert(!scen_name.empty(), "Scenario name must not be empty");
        Scenario scen;
        scen.name = scen_name;
        auto j = nlohmann::json::parse(
            str,
            nullptr,
            true, // allow exceptions
            true, // ignore comments
            true // allow trailing commas
        );
        scen.duration = j.at("duration").get<double>();

        auto outputs = j["outputs"];
        scen.outputs.resize(outputs.size());
        std::set<std::string> output_names;
        for (size_t i = 0; i < outputs.size(); i++) {
            auto output = outputs[i];
            try {
                Common::Assert(output.contains("type"), "type must be specified");
                Common::Assert(output.contains("name"), "name must be specified");
                std::string name = output.at("name").get<std::string>();
                std::string type = output.at("type").get<std::string>();

                Common::Assert(name.length() > 0, "output name cannot be empty");
                Common::Assert(name.find('.') == std::string::npos, "output name cannot contain a \'.\'");
                Common::Assert(!output_names.contains(name), "output names must be unique");
                output_names.insert(name);
                Common::Assert(outputParserRegistry.entries.contains(type), format("unknown output type \"{}\"", type));

                std::unique_ptr<OutputParser> p(outputParserRegistry.entries[type].ctor());
                p->Parse(outputs[i]);
                scen.outputs[i] = p->Get();
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format(
                    "could not parse output [{}]: {}",
                    i,
                    e.what()
                ));
            }
        }

        auto signals = j["signals"];
        scen.signals.reserve(signals.size());
        std::set<std::string> signal_names;
        for (size_t i = 0; i < signals.size(); i++) {
            auto signal = signals[i];
            try {
                Common::Assert(signal.contains("type"), "type must be specified");
                Common::Assert(signal.contains("name"), "name must be specified");
                std::string type = signal.at("type").get<std::string>();
                std::string name = signal.at("name").get<std::string>();

                Common::Assert(name.length() > 0, "signal name cannot be empty");
                Common::Assert(!signal_names.contains(name), "signal names must be unique");
                signal_names.insert(name);
                Common::Assert(signalParserRegistry.entries.contains(type), format("unknown signal type \"{}\"", type));
                std::unique_ptr<SignalParser> p(signalParserRegistry.entries[type].ctor());

                p->Parse(signal);
                Signal s = p->Get();
                scen.signals.push_back(std::move(s));
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format(
                    "could not parse signal [{}]: {}",
                    i,
                    e.what()
                ));
            }
        }

        auto events = j["events"];
        scen.events.resize(events.size());
        for (size_t i = 0; i < events.size(); i++) {
            try {
                EventParser p(scen);
                p.Parse(events[i]);
                scen.events[i] = p.Get();
            }
            catch(const std::exception &e) {
                throw std::runtime_error(std::format(
                    "could not parse event [{}]: {}",
                    i,
                    e.what()
                ));
            }
        }
        return scen;
    };

    Signal SignalParser::Get() {
        Common::Assert(!nameP.defaulted, "Signal name must be specified");
        Signal s;
        s.builder = this->Setup();
        s.name = nameP.data;
        return s;
    }

    Output OutputParser::Get() {
        Common::Assert(!nameP.defaulted, "Name must be set for Output");
        Common::Assert(!sampleRateP.defaulted, "Sample rate must be set for Output");
        Common::Assert(!nChannelsP.defaulted, "nChannels must be specified");
        Common::Assert(!freqP.defaulted, "frequencies must be specified");
        Common::Assert(freqP.data.size() == nChannelsP.data, "frequency count mismatch");
        Output o;
        o.name = nameP.data;
        o.nChannels = nChannelsP.data;
        o.sampleRate = sampleRateP.data;
        o.frequencies.clear();
        for(auto &f : freqP.data) {
            o.frequencies.push_back(f.data);
        }
        o.builder = SetupRadio();
	    return o;
    }
};