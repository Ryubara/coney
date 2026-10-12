// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/log.h"

#include <array>
#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <utility>

namespace coney::logging {

namespace {

// The process's logger: one per process, as a run has one game.
struct Logger {
    // Recursive: a sink that writes a line itself (a warning about its own file) must not deadlock.
    std::recursive_mutex mutex;
    Sink sink;
    DumpSink dumpSink;
    LogFilter filter;
    LogRing ring;
    std::uint64_t step = 0;
    int checkpoint = kNoCheckpoint;
    // Set once a failure has dumped the ring, so the abort that follows does not dump it again.
    std::atomic<bool> failureDumped{false};
};

// The logger, made on first use.
Logger& theLogger() {
    static Logger logger;
    return logger;
}

// A prefix routeOf() knows, and where its lines go.
struct PrefixRoute {
    std::string_view prefix;
    Route route;
};

// Where the subsystems' prefixed lines go. A prefix matches only with a colon right after it, so `script` does not
// take `script error:` lines. docs/guides/run-log.md#categories lists the same table.
constexpr std::array kPrefixRoutes{
    PrefixRoute{"script error", {Category::Script, Level::Error, Privacy::Public}},
    // What the scripts print is the game's own text: the console only.
    PrefixRoute{"script print", {Category::Script, Level::Detail, Privacy::LocalOnly}},
    PrefixRoute{"script", {Category::Script, Level::Info, Privacy::Public}},
    PrefixRoute{"preload", {Category::Script, Level::Info, Privacy::Public}},
    PrefixRoute{"CfgLevelName", {Category::Script, Level::Warn, Privacy::Public}},
    PrefixRoute{"pickup", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"throw", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"theft", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"swap", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"tag", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"uncuff", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"mugging", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"deal", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"flash", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"lock pick", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"game over", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"player", {Category::Player, Level::Info, Privacy::Public}},
    PrefixRoute{"scout", {Category::Ai, Level::Info, Privacy::Public}},
    PrefixRoute{"revive", {Category::Ai, Level::Info, Privacy::Public}},
    PrefixRoute{"fighters", {Category::Ai, Level::Info, Privacy::Public}},
    PrefixRoute{"targets", {Category::Ai, Level::Info, Privacy::Public}},
    PrefixRoute{"damage", {Category::Combat, Level::Info, Privacy::Public}},
    PrefixRoute{"knock-down", {Category::Combat, Level::Info, Privacy::Public}},
    PrefixRoute{"death", {Category::Combat, Level::Info, Privacy::Public}},
    PrefixRoute{"level flow", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"level", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"gameplay", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"checkpoint", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"cutscene", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"scene sound", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"scene", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"scenes", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"cast", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"objects", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"cars", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"humans", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"world", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"water", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"lighting", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"room smoke", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"path data", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"missions", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"mission complete", {Category::World, Level::Info, Privacy::Public}},
    PrefixRoute{"mission failed", {Category::World, Level::Info, Privacy::Public}},
};

// Whether `line` starts with `prefix` and a colon.
bool hasPrefix(std::string_view line, std::string_view prefix) {
    return line.size() > prefix.size() && line.starts_with(prefix) && line[prefix.size()] == ':';
}

// Calls `each` with every non-empty line of `text`, without its line break (and a carriage return before it).
template <typename Each> void forEachLine(std::string_view text, Each each) {
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        std::string_view line = text.substr(0, end);
        if (line.ends_with('\r')) {
            line.remove_suffix(1);
        }
        if (!line.empty()) {
            each(line);
        }
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end + 1);
    }
}

// Writes one line: into the ring (withheld when local-only), and to the sink when the filter passes it.
void writeLine(Logger& logger, Category category, Level level, std::string_view text, Privacy privacy) {
    Record record{.step = logger.step,
                  .checkpoint = logger.checkpoint,
                  .category = category,
                  .level = level,
                  .privacy = privacy,
                  .text = std::string(text)};
    if (logger.sink && logger.filter.passes(category, level)) {
        logger.sink(record, formatLine(record));
    }
    logger.ring.add(privacy == Privacy::Public ? std::move(record) : withheld(record));
}

// The dump itself, with the lock (if any) already taken.
std::size_t dumpLocked(Logger& logger, std::string_view reason) {
    const std::size_t count = logger.ring.size();
    if (!logger.dumpSink) {
        return count;
    }
    // The framing lines are stamped now; they go to the dump only, not into the ring.
    const auto frame = [&logger](std::string text) {
        return formatLine(Record{.step = logger.step,
                                 .checkpoint = logger.checkpoint,
                                 .category = Category::Engine,
                                 .level = Level::Info,
                                 .privacy = Privacy::Public,
                                 .text = std::move(text)});
    };
    logger.dumpSink(frame(std::format("{}: the last {} events, every level, oldest first", reason, count)));
    for (const Record& record : logger.ring.records()) {
        logger.dumpSink(formatLine(record));
    }
    logger.dumpSink(frame(std::format("{}: end of the last {} events", reason, count)));
    return count;
}

} // namespace

void setSink(Sink sink) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.sink = std::move(sink);
}

void setDumpSink(DumpSink sink) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.dumpSink = std::move(sink);
}

void setFilter(const LogFilter& filter) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.filter = filter;
}

LogFilter filter() {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    return logger.filter;
}

void setStep(std::uint64_t step) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.step = step;
}

std::uint64_t step() {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    return logger.step;
}

void setCheckpoint(int checkpoint) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.checkpoint = checkpoint;
}

void clearCheckpoint() { setCheckpoint(kNoCheckpoint); }

int checkpoint() {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    return logger.checkpoint;
}

void write(Category category, Level level, std::string_view text, Privacy privacy) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    forEachLine(text, [&](std::string_view line) { writeLine(logger, category, level, line, privacy); });
}

Route routeOf(std::string_view line) {
    for (const PrefixRoute& entry : kPrefixRoutes) {
        if (hasPrefix(line, entry.prefix)) {
            return entry.route;
        }
    }
    return Route{};
}

void writeRouted(std::string_view text) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    forEachLine(text, [&](std::string_view line) {
        const Route route = routeOf(line);
        writeLine(logger, route.category, route.level, line, route.privacy);
    });
}

std::size_t dump(std::string_view reason) {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    return dumpLocked(logger, reason);
}

const LogRing& ring() { return theLogger().ring; }

void reset() {
    Logger& logger = theLogger();
    const std::scoped_lock lock(logger.mutex);
    logger.sink = {};
    logger.dumpSink = {};
    logger.filter = LogFilter{};
    logger.ring.clear();
    logger.step = 0;
    logger.checkpoint = kNoCheckpoint;
    logger.failureDumped = false;
}

void dumpOnFailure(const char* reason) noexcept {
    Logger& logger = theLogger();
    if (logger.failureDumped.exchange(true)) {
        return;
    }
    // Another thread may hold the lock for ever (it is the one that crashed): dump without it then, at worst a torn
    // line, rather than hang the dying process.
    const bool locked = logger.mutex.try_lock();
    dumpLocked(logger, reason);
    if (locked) {
        logger.mutex.unlock();
    }
}

} // namespace coney::logging
