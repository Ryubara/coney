// SPDX-License-Identifier: GPL-3.0-or-later

// Network play proved without a person (docs/guides/multiplayer.md#two-processes): runs `coney` as separate processes
// and compares the digests they write.
//
//   net_pair_check pair    --coney EXE --work DIR --host-script FILE --guest-script FILE --frames N --guest-frames N
//                          [--guest-after TEXT] [-- ARGS...]
//   net_pair_check offline --coney EXE --work DIR --host-script FILE --frames N [-- ARGS...]
//
// `pair` starts a headless host on a free port of 127.0.0.1 and, once it says it is hosting, a headless guest that
// joins it, each with its input script; then replays the host's session record headless with no session. It checks
// that every snapshot the guest decoded has the digest of what the host's snapshot left it holding, that the host
// played the guest's pad on port 2, and that the replay gives the host's digest on every step. With --guest-after the
// guest starts only once the host has also printed TEXT (a level's first frame of play).
//
// `offline` runs the host's script offline and as a host nobody joins, and checks that the two digests are the same on
// every step (a session changes nothing in the game) and that the pads the offline run played are the script's, from
// the step the script starts on.
//
// ARGS go to the host's runs (the disc and a level, for the disc-backed checks); the guest needs neither. Prints counts
// and hashes only. Exits 0 when every check passes, 1 when one fails, 2 for a bad command line.

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_process.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/input_script.h"
#include "net/session_record.h"
#include "net/state_digest.h"
#include "platform/error_dialogs.h"

namespace {

namespace fs = std::filesystem;
using coney::net::DigestLine;

// The command line.
struct Args {
    std::string mode;
    std::string coney;
    fs::path work;
    std::string hostScript;
    std::string guestScript;
    std::string frames;
    std::string guestFrames;
    std::string guestAfter;         // the host's output to wait for before the guest starts; empty: its hosting line
    std::vector<std::string> extra; // after `--`: the host's runs only
};

// Prints a line of the check's own output.
void say(const std::string& line) {
    std::fputs((line + "\n").c_str(), stdout);
    std::fflush(stdout);
}

// Reads the command line; nothing when it is malformed.
std::optional<Args> parseArgs(int argc, char** argv) {
    if (argc < 2) {
        return std::nullopt;
    }
    Args args;
    args.mode = argv[1];
    std::map<std::string_view, std::string*> values{
        {"--coney", &args.coney},   {"--host-script", &args.hostScript},   {"--guest-script", &args.guestScript},
        {"--frames", &args.frames}, {"--guest-frames", &args.guestFrames}, {"--guest-after", &args.guestAfter},
    };
    std::string work;
    values.emplace("--work", &work);
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--") {
            args.extra.assign(argv + i + 1, argv + argc);
            break;
        }
        const auto found = values.find(arg);
        if (found == values.end() || i + 1 == argc) {
            return std::nullopt;
        }
        *found->second = argv[++i];
    }
    args.work = work;
    const bool pair = args.mode == "pair";
    if ((!pair && args.mode != "offline") || args.coney.empty() || work.empty() || args.hostScript.empty() ||
        args.frames.empty() || (pair && (args.guestScript.empty() || args.guestFrames.empty()))) {
        return std::nullopt;
    }
    return args;
}

// A running `coney`, its output echoed with `tag` in front by a thread of its own, so a full pipe never stops it.
class Run {
  public:
    // Starts `exe` with `args`; `watches` are pieces of output to wait for with waitFor(), by their index.
    Run(const std::string& exe, const std::vector<std::string>& args, std::string tag,
        const std::vector<std::string>& watches = {})
        : m_tag(std::move(tag)), m_watches(watches.size()) {
        for (std::size_t i = 0; i < watches.size(); ++i) {
            m_watches[i].text = watches[i];
        }
        std::vector<std::string> all{exe};
        all.insert(all.end(), args.begin(), args.end());
        say(std::format("{}: starting with {} arguments", m_tag, args.size()));
        std::vector<const char*> pointers;
        pointers.reserve(all.size() + 1);
        for (const std::string& arg : all) {
            pointers.push_back(arg.c_str());
        }
        pointers.push_back(nullptr);
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetPointerProperty(props, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, static_cast<void*>(pointers.data()));
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDIN_NUMBER, SDL_PROCESS_STDIO_NULL);
        SDL_SetNumberProperty(props, SDL_PROP_PROCESS_CREATE_STDOUT_NUMBER, SDL_PROCESS_STDIO_APP);
        SDL_SetBooleanProperty(props, SDL_PROP_PROCESS_CREATE_STDERR_TO_STDOUT_BOOLEAN, true);
        m_process = SDL_CreateProcessWithProperties(props);
        SDL_DestroyProperties(props);
        if (m_process == nullptr) {
            say(std::format("{}: could not start: {}", m_tag, SDL_GetError()));
            for (Watch& watch : m_watches) {
                watch.seen.set_value(false);
            }
            return;
        }
        m_reader = std::thread([this] { readOutput(); });
    }
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    Run(Run&&) = delete;
    Run& operator=(Run&&) = delete;
    ~Run() {
        if (m_reader.joinable()) {
            m_reader.join();
        }
        if (m_process != nullptr) {
            SDL_DestroyProcess(m_process);
        }
    }

    // Waits for watched text `index`; false when the process ended without printing it. The text's line is kept.
    bool waitFor(std::size_t index) { return m_watches.at(index).future.get(); }
    // The line watched text `index` was in.
    [[nodiscard]] const std::string& watchedLine(std::size_t index) const { return m_watches.at(index).line; }
    // Waits for the process to end; its exit code, or -1 when it never started.
    int finish() {
        if (m_process == nullptr) {
            return -1;
        }
        int code = -1;
        SDL_WaitProcess(m_process, true, &code);
        if (m_reader.joinable()) {
            m_reader.join();
        }
        say(std::format("{}: exited with {}", m_tag, code));
        return code;
    }

  private:
    // Echoes the output line by line until it ends, and reports the watched text the first time it shows.
    void readOutput() {
        SDL_IOStream* out = SDL_GetProcessOutput(m_process);
        std::string pending;
        std::array<char, 4096> buffer{};
        while (out != nullptr) {
            const std::size_t got = SDL_ReadIO(out, buffer.data(), buffer.size());
            if (got == 0) {
                // The pipe does not block: nothing yet waits for more; the end or an error stops.
                if (SDL_GetIOStatus(out) == SDL_IO_STATUS_NOT_READY) {
                    SDL_Delay(1);
                    continue;
                }
                break;
            }
            pending.append(buffer.data(), got);
            for (std::size_t end = pending.find('\n'); end != std::string::npos; end = pending.find('\n')) {
                std::string line = pending.substr(0, end);
                pending.erase(0, end + 1);
                std::fputs(std::format("[{}] {}\n", m_tag, line).c_str(), stdout);
                for (Watch& watch : m_watches) {
                    if (!watch.reported && line.find(watch.text) != std::string::npos) {
                        watch.line = line;
                        watch.reported = true;
                        watch.seen.set_value(true);
                    }
                }
            }
        }
        std::fflush(stdout);
        for (Watch& watch : m_watches) {
            if (!watch.reported) {
                watch.seen.set_value(false);
            }
        }
    }

    // A piece of output waited for, and the line it showed in.
    struct Watch {
        std::string text;
        std::string line;
        bool reported = false;
        std::promise<bool> seen;
        std::future<bool> future = seen.get_future();
    };

    std::string m_tag;
    std::vector<Watch> m_watches;
    SDL_Process* m_process = nullptr;
    std::thread m_reader;
};

// Every line of a digest file, read strictly; nothing (said why) when the file is missing or a line is malformed.
std::optional<std::vector<DigestLine>> readDigests(const fs::path& path) {
    std::ifstream file(path);
    if (!file) {
        say(std::format("cannot read {}", path.filename().string()));
        return std::nullopt;
    }
    std::vector<DigestLine> lines;
    std::string text;
    while (std::getline(file, text)) {
        auto line = coney::net::parseDigestLine(text);
        if (!line) {
            say(std::format("{}: {}", path.filename().string(), line.error().message));
            return std::nullopt;
        }
        lines.push_back(*line);
    }
    return lines;
}

// Whether two runs' digests are the same on every step; says the first step that differs.
bool sameDigests(const std::vector<DigestLine>& first, const std::vector<DigestLine>& second, std::string_view what) {
    if (first.size() != second.size()) {
        say(std::format("{}: {} steps against {}", what, first.size(), second.size()));
        return false;
    }
    for (std::size_t i = 0; i < first.size(); ++i) {
        if (first[i] != second[i]) {
            say(std::format("{}: first different at step {}: '{}' against '{}'", what, first[i].step,
                            coney::net::formatDigestLine(first[i]), coney::net::formatDigestLine(second[i])));
            return false;
        }
    }
    say(std::format("{}: {} steps, the same on every one", what, first.size()));
    return true;
}

// The arguments every headless run of the host's script takes.
std::vector<std::string> hostRun(const Args& args, std::vector<std::string> more) {
    std::vector<std::string> all{"--headless", "--frames", args.frames, "--input-script", args.hostScript};
    all.insert(all.end(), more.begin(), more.end());
    all.insert(all.end(), args.extra.begin(), args.extra.end());
    return all;
}

// The port in the host's "hosting on port N as NAME" line; 0 when there is none.
unsigned portIn(const std::string& line) {
    constexpr std::string_view kSays = "hosting on port ";
    const std::size_t at = line.find(kSays);
    if (at == std::string::npos) {
        return 0;
    }
    return static_cast<unsigned>(std::strtoul(line.c_str() + at + kSays.size(), nullptr, 10));
}

// The two-process check and the replay.
int checkPair(const Args& args) {
    const fs::path hostDigest = args.work / "host_digest.txt";
    const fs::path hostNet = args.work / "host_net.txt";
    const fs::path guestNet = args.work / "guest_net.txt";
    const fs::path record = args.work / "session.txt";
    const fs::path replayDigest = args.work / "replay_digest.txt";

    // The host, on a free port; the guest once the host says which, and, with --guest-after, once its game is where the
    // guest's pads should land (a level's play, after a load that may take longer than the guest's whole run).
    std::vector<std::string> watches{"hosting on port "};
    if (!args.guestAfter.empty()) {
        watches.push_back(args.guestAfter);
    }
    Run host(args.coney,
             hostRun(args, {"--host", "127.0.0.1:0", "--net-name", "Host", "--digest", hostDigest.string(),
                            "--digest-net", hostNet.string(), "--net-record", record.string()}),
             "host", watches);
    if (!host.waitFor(0)) {
        host.finish();
        say("FAIL: the host never said it was hosting");
        return 1;
    }
    const unsigned port = portIn(host.watchedLine(0));
    if (watches.size() > 1 && !host.waitFor(1)) {
        host.finish();
        say("FAIL: the host never printed the --guest-after text");
        return 1;
    }
    Run guest(args.coney,
              {"--headless", "--frames", args.guestFrames, "--input-script", args.guestScript, "--join",
               std::format("127.0.0.1:{}", port), "--net-name", "Guest", "--digest-net", guestNet.string()},
              "guest");
    const int guestCode = guest.finish();
    const int hostCode = host.finish();
    if (hostCode != 0 || guestCode != 0) {
        say("FAIL: a game did not exit cleanly");
        return 1;
    }

    bool ok = true;
    // Every snapshot the guest decoded holds what the host's left it holding at that step.
    const auto sent = readDigests(hostNet);
    const auto decoded = readDigests(guestNet);
    if (!sent || !decoded) {
        return 1;
    }
    const coney::net::DigestMatch match = coney::net::matchDigests(*sent, *decoded);
    say(std::format("snapshots: {} sent, {} decoded, {} matched", sent->size(), decoded->size(), match.matched));
    if (decoded->empty() || match.matched != decoded->size()) {
        say(match.firstMismatch ? std::format("FAIL: the guest's digest differs at step {}", *match.firstMismatch)
            : match.firstUnknown
                ? std::format("FAIL: the guest decoded step {}, which the host never sent", *match.firstUnknown)
                : std::string("FAIL: the guest decoded no snapshot"));
        ok = false;
    }

    // The host played the guest's pad on port 2: some recorded step holds a button.
    std::ifstream recordFile(record);
    std::size_t pressed = 0;
    std::size_t events = 0;
    for (std::string text; std::getline(recordFile, text);) {
        if (const auto line = coney::net::parseRecordLine(text); line) {
            pressed += line->kind == coney::net::RecordLine::Kind::Pad && line->pad.connected && line->pad.buttons != 0;
            events += line->kind == coney::net::RecordLine::Kind::Event;
        }
    }
    say(std::format("record: {} steps with a button held on port 2, {} events", pressed, events));
    if (pressed == 0) {
        say("FAIL: the host never played the guest's buttons on port 2");
        ok = false;
    }

    // The replay: the host's script and the recorded port 2, with no session, give the host's digest again.
    Run replay(args.coney, hostRun(args, {"--net-replay", record.string(), "--digest", replayDigest.string()}),
               "replay");
    if (replay.finish() != 0) {
        say("FAIL: the replay did not exit cleanly");
        return 1;
    }
    const auto hostLines = readDigests(hostDigest);
    const auto replayLines = readDigests(replayDigest);
    if (!hostLines || !replayLines || !sameDigests(*hostLines, *replayLines, "replay against the host")) {
        ok = false;
    }
    say(ok ? "net pair check: ok" : "net pair check: FAILED");
    return ok ? 0 : 1;
}

// The offline check: no session against a session nobody joins, and the pads against the script.
int checkOffline(const Args& args) {
    const fs::path offlineDigest = args.work / "offline_digest.txt";
    const fs::path aloneDigest = args.work / "alone_digest.txt";
    Run offline(args.coney, hostRun(args, {"--digest", offlineDigest.string()}), "offline");
    Run alone(args.coney,
              hostRun(args, {"--host", "127.0.0.1:0", "--net-name", "Host", "--digest", aloneDigest.string()}),
              "alone");
    if (offline.finish() != 0 || alone.finish() != 0) {
        say("FAIL: a run did not exit cleanly");
        return 1;
    }
    const auto offlineLines = readDigests(offlineDigest);
    const auto aloneLines = readDigests(aloneDigest);
    if (!offlineLines || !aloneLines) {
        return 1;
    }
    bool ok = sameDigests(*offlineLines, *aloneLines, "offline against a host nobody joined");

    // The pads each step played are the script's: networking puts nothing between the script and the game. A level
    // played from the command line starts its script at the first frame of play (DeferredInput), so the script may
    // start some steps in: the pads before are unchanged, and from there every step is the script's.
    auto events = coney::loadInputScript(args.hostScript);
    if (!events) {
        say(std::format("FAIL: {}", events.error().message));
        return 1;
    }
    const std::vector<DigestLine>& lines = *offlineLines;
    std::optional<std::size_t> start;
    for (std::size_t k = 0; k < lines.size() && !start; ++k) {
        if (k > 0 && lines[k - 1].pads != lines[0].pads) {
            break; // the pads changed before this step, so the script started earlier
        }
        coney::ScriptedInput script(*events);
        bool all = true;
        for (std::size_t i = k; i < lines.size() && all; ++i) {
            all = lines[i].pads == coney::net::padsDigest(script.sample(i - k));
        }
        if (all) {
            start = k;
        }
    }
    if (start) {
        say(std::format("pads: the script's on every step from step {} of {}", *start, lines.size()));
    } else {
        say("FAIL: the pads played are not the script's from any step");
        ok = false;
    }
    say(ok ? "net offline check: ok" : "net offline check: FAILED");
    return ok ? 0 : 1;
}

} // namespace

// Runs the check the command line names.
int main(int argc, char** argv) {
    coney::platform::reportErrorsToConsole();
    const std::optional<Args> args = parseArgs(argc, argv);
    if (!args) {
        std::fputs("usage: net_pair_check pair|offline --coney EXE --work DIR --host-script FILE [--guest-script FILE] "
                   "--frames N [--guest-frames N] [--guest-after TEXT] [-- ARGS...]\n",
                   stderr);
        return 2;
    }
    std::error_code error;
    fs::create_directories(args->work, error);
    return args->mode == "pair" ? checkPair(*args) : checkOffline(*args);
}
