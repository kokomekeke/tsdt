#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#else
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "Vendored/argparse/argparse.hpp"
#include "Common/Defer.hpp"
#include "Common/Logger.hpp"
#include "Messages.hpp"
#include "MqttClient.hpp"
#include "SCG/Json/Rid.hpp"
#include "SCG/Json/ServerConfig.hpp"
#include "SCG/Utility/SignalHandler.hpp"

using namespace Sagax;
using namespace Sagax::SCG;

struct Parameters {
    bool success;
    std::string error;
    std::string configPath;
};

struct ProgramParser {
    Parameters params;
    argparse::ArgumentParser program;

    ProgramParser() : program("scgcommander.exe", "1.0", argparse::default_arguments::all) {
        program.add_argument("config_path")
            .help("path to configuration .json");
    }

    std::string Help() {
        return program.help().str();
    }

    void Parse(int argc, char **argv) {
        try {
            program.parse_args(argc, argv);
        }
        catch(const std::exception &e) {
            params.success = false;
            params.error = e.what();
            return;
        }

        params.configPath = program.get<std::string>("config_path");
        params.success = true;
    }
};

// the log records and the line being typed share one terminal row. the editor keeps the
// characters that have been entered so far, so a record can erase the row, print itself
// and put the prompt and the text back. every write to the terminal goes through here
struct LineEditor {
    std::mutex mtx;
    std::string buffer;
    bool active = false; // false before the prompt is up and after it is taken down

    // these three assume the caller holds mtx
    void Clear_() {
        if(active) {
            std::cerr << "\r\033[2K" << std::flush;
        }
    }

    void Draw_() {
        if(active) {
            std::cerr << "> " << buffer << std::flush;
        }
    }

    void Write_(const std::string &text) {
        std::cerr << text << std::flush;
    }

    void Start() {
        std::unique_lock lck(mtx);
        active = true;
        Draw_();
    }

    void Stop() {
        std::unique_lock lck(mtx);
        Clear_();
        active = false;
    }

    // called from the logger thread, the record replaces the row and the prompt is
    // printed again under it
    void PutRecord(const std::string &text) {
        std::unique_lock lck(mtx);
        Clear_();
        Write_(text);
        Draw_();
    }

    void Insert(char c) {
        std::unique_lock lck(mtx);
        buffer.push_back(c);
        if(active) {
            std::cerr << c << std::flush;
        }
    }

    void Backspace() {
        std::unique_lock lck(mtx);
        if(buffer.empty()) {
            return;
        }
        buffer.pop_back();
        if(active) {
            std::cerr << "\b \b" << std::flush;
        }
    }

    // the finished line is left on its own row and an empty prompt is started below it
    std::string Take() {
        std::unique_lock lck(mtx);
        std::string line = buffer;
        buffer.clear();
        Clear_();
        Write_("> " + line + "\n");
        Draw_();
        return line;
    }
};

// routes the records into the editor instead of writing them to the stream directly.
// the formatting is the one StdLogStream uses
struct EditorLogStream : public Common::Logger::Stream {
    LineEditor &editor;
    Common::StdLogStream::TagOptions opt;

    EditorLogStream(LineEditor &editor_, Common::Logger::Level level_, Common::StdLogStream::TagOptions opt_)
        : editor(editor_), opt(opt_) {
        this->level = level_;
    }

    void Put(const Common::Logger::Entry &e) override {
        editor.PutRecord(Common::StdLogStream::GetTag(e, opt) + std::string(" ") + e.message + "\n");
    }
};

// the console hands over the characters one at a time and does not echo them, so that the
// editor owns the row. on windows _getch already reads that way and there is nothing to
// set up, on the other platforms the line discipline has to be turned off
struct TerminalRawMode {
#ifdef _WIN32
    TerminalRawMode() = default;
    ~TerminalRawMode() = default;
#else
    termios saved{};
    bool changed = false;

    TerminalRawMode() {
        if(tcgetattr(STDIN_FILENO, &saved) != 0) {
            return;
        }
        termios raw = saved;
        raw.c_lflag &= ~tcflag_t(ICANON | ECHO);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        changed = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
    }

    ~TerminalRawMode() {
        if(changed) {
            tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
    }
#endif

    TerminalRawMode(const TerminalRawMode &) = delete;
    TerminalRawMode &operator=(const TerminalRawMode &) = delete;
};

// returns the character, -1 at the end of the input, -2 for a key that is dropped and
// -3 when nothing arrived before the wait ran out. the signals are taken by a thread of
// their own and are blocked everywhere else, so a read is never interrupted and the wait
// is what brings the loop back to the running flag
static int ReadChar() {
#ifdef _WIN32
    if(!_kbhit()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return -3;
    }
    int c = _getch();
    if(c == 0 || c == 0xE0) {
        _getch(); // the second byte of an arrow or function key
        return -2;
    }
    if(c == EOF) {
        return -1;
    }
    return c;
#else
    pollfd pfd{};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int ready = poll(&pfd, 1, 50);
    if(ready < 0) {
        // a signal that is not blocked here lands as EINTR, a terminal resize for one
        return errno == EINTR ? -3 : -1;
    }
    if(ready == 0) {
        return -3;
    }
    char c = 0;
    if(read(STDIN_FILENO, &c, 1) != 1) {
        return -1;
    }
    return int(static_cast<unsigned char>(c));
#endif
}

static void PrintHelp() {
    Common::Logger::Info("commands:");
    Common::Logger::Info("  testsetup <tsd_url> <time> <test_id>");
    Common::Logger::Info("  testcancel <test_id>");
    Common::Logger::Info("  intstop");
    Common::Logger::Info("  intstart | teststart | testupdate | testend   (read and ignored)");
    Common::Logger::Info("  help");
    Common::Logger::Info("  quit\n");
    Common::Logger::Info("time is given as 2026-09-02T14:30:00+02:00\n");
}

static void DrainIncomingMessages(MqttClient &client, std::atomic<bool> &running) {
    while(true) {
        MqttClient::Message msg;
        // the client writes a line for every message it reads, so this only takes
        // them out of the queue
        if(client.Pop(msg)) {
            continue;
        }

        if(!running.load() && !client.IsRunning()) {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int main(int argc, char **argv) {
    LineEditor editor;
    EditorLogStream logStream(editor, Common::Logger::Level::Trace, {});
    Common::Logger::AddStream(&logStream);
    Common::Defer disposeLogger([&](){
        Common::Logger::RemoveStream(&logStream);
    });

    // the same records without the colour escape sequences. the prompt and the echoed
    // characters go straight to the terminal and never reach the logger, so the file holds
    // the records only. the file is opened in the working directory and is replaced on
    // every run
    std::ofstream logFile("scgcommander.log", std::ios::trunc);
    Common::StdLogStream fileLogStream(logFile, Common::Logger::Level::Trace, Common::StdLogStream::TagOptions{
        .colors = false
    });
    if(logFile.is_open()) {
        Common::Logger::AddStream(&fileLogStream);
    }
    Common::Defer disposeFileLogger([&](){
        if(logFile.is_open()) {
            Common::Logger::RemoveStream(&fileLogStream);
        }
    });
    if(!logFile.is_open()) {
        Common::Logger::Warn("could not open scgcommander.log, logging to the console only");
    }

    ProgramParser parser;
    parser.Parse(argc, argv);
    Parameters params = parser.params;
    if(!params.success) {
        Common::Logger::Error("\nfailed to parse parameters:\n{}", params.error.c_str());
        Common::Logger::Info("\n{}", parser.Help().c_str());
        return 1;
    }

    std::ifstream file(params.configPath);
    if(!file.is_open()) {
        Common::Logger::Error("failed to open config file: {}", params.configPath);
        return 1;
    }

    std::stringstream buf;
    buf << file.rdbuf();

    ServerConfig config;
    {
        Json::JsonIssues issues;
        if(!ParseServerConfig(buf.str(), config, issues)) {
            Common::Logger::Error("\nfailed to parse config:\n{}", issues.Format());
            return 1;
        }
    }

    // watched by the input loop and by the thread draining the incoming messages. it is
    // declared before the handler so that the callback cannot outlive it
    std::atomic<bool> running{true};

    // constructed before the mqtt client and the log thread start, on linux a thread
    // inherits the signal mask and the blocking has to be in place by then. the callback
    // runs on an ordinary thread and only clears the flag, so the loop returns on its own
    // and the destructor of TerminalRawMode puts the terminal back
    SignalHandler signalHandler({ SIGINT, SIGTERM }, [&running](int sig){
        Common::Logger::Info("signal {} received, stopping", sig);
        running.store(false);
    });

    MqttClient::LibraryInit();
    MqttClient client;

    MqttClient::Configuration cfg;
    cfg.host = config.mqtt.brokerHost;
    cfg.port = config.mqtt.brokerPort;
    cfg.keepAlive_s = 60;
    cfg.subscribeTopics = { config.mqtt.signalLogTopic };
    for(const auto &topic : { config.mqtt.testLogTopic, config.mqtt.errorLogTopic }) {
        if(std::find(cfg.subscribeTopics.begin(), cfg.subscribeTopics.end(), topic) == cfg.subscribeTopics.end()) {
            cfg.subscribeTopics.push_back(topic);
        }
    }
    cfg.clientId = "Commander";
    cfg.logMessages = true;
    cfg.reconnectDelays_ms = { 1000, 2000, 5000 };
    cfg.maxQueueSize = 100;
    cfg.onUpdate = []() {};

    client.Configure(std::move(cfg));
    client.Start();
    if(!client.IsRunning()) {
        Common::Logger::Error("failed to start MQTT client");
        MqttClient::LibraryCleanup();
        return 1;
    }

    std::thread logThread([&]() {
        DrainIncomingMessages(client, running);
    });

    PrintHelp();
    Common::Logger::Info("connected command topic: {}", config.mqtt.commandTopic);
    Common::Logger::Info("listening signal log topic: {}", config.mqtt.signalLogTopic);
    Common::Logger::Info("listening test log topic: {}", config.mqtt.testLogTopic);
    Common::Logger::Info("listening error log topic: {}", config.mqtt.errorLogTopic);

    [[maybe_unused]] TerminalRawMode rawMode;
    editor.Start();
    Common::Defer stopEditor([&](){
        editor.Stop();
    });

    std::string line;
    while(running.load()) {
        int c = ReadChar();
        if(c == -2 || c == -3) { // a dropped key, or nothing to read yet
            continue;
        }
        if(c == -1 || c == 3 || c == 4) { // end of input, ctrl+c, ctrl+d
            running.store(false);
            break;
        }
        if(c == 8 || c == 127) { // backspace, delete
            editor.Backspace();
            continue;
        }
        if(c != '\r' && c != '\n') {
            if(c >= 0x20 && c != 0x7f) {
                editor.Insert(char(c));
            }
            continue;
        }

        line = editor.Take();

        std::istringstream iss(line);
        std::string cmd;
        if(!(iss >> cmd)) {
            continue;
        }
        std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return char(std::tolower(c)); });

        if(cmd == "quit" || cmd == "exit") {
            running.store(false);
            break;
        }
        if(cmd == "help") {
            PrintHelp();
            continue;
        }

        CommandMessage cm;
        bool make = true;

        if(cmd == "testsetup") {
            std::string tsdUrl, time, testId;
            iss >> tsdUrl >> time >> testId;
            cm.variant = CommandMessage::TestSetup{tsdUrl, time, testId};
        }
        else if(cmd == "testcancel") {
            std::string testId;
            iss >> testId;
            cm.variant = CommandMessage::TestCancel{testId};
        }
        else if(cmd == "intstop") {
            cm.variant = CommandMessage::IntStop{};
        }
        else if(cmd == "intstart") {
            cm.variant = CommandMessage::IntStart{};
        }
        else if(cmd == "teststart") {
            cm.variant = CommandMessage::TestStart{};
        }
        else if(cmd == "testupdate") {
            cm.variant = CommandMessage::TestUpdate{};
        }
        else if(cmd == "testend") {
            cm.variant = CommandMessage::TestEnd{};
        }
        else {
            Common::Logger::Warn("unknown command: {}", cmd);
            make = false;
        }

        if(!make) {
            continue;
        }

        Json::JsonIssues issues;
        std::optional<std::string> written = Sagax::SCG::SerializeCommandMessage(cm, issues);
        if(!written.has_value()) {
            Common::Logger::Error("could not write the command:\n{}", issues.Format());
            continue;
        }
        const std::string &payload = written.value();

        MqttClient::Message out;
        out.topic = config.mqtt.commandTopic;
        out.bytes.assign(payload.begin(), payload.end());
        out.qos = 0;
        out.retain = false;
        client.Push(std::move(out));

        Common::Logger::Info("sent: {}", payload);
    }

    if(client.IsRunning()) {
        client.Stop();
    }
    if(logThread.joinable()) {
        logThread.join();
    }
    MqttClient::LibraryCleanup();
    return 0;
}
