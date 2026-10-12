// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// One line of Coney's run log and how it is written: a category, a level, the simulation step and checkpoint it
// happened in, and its text. Coney's own (the original has no such log); how to read and add lines:
// docs/guides/run-log.md.

namespace coney::logging {

/// What part of the game a line is about; `--log-verbose` names them.
enum class Category : std::uint8_t {
    Player, ///< What player 1 does: pick-ups, drops, throws, tags, thefts.
    Ai,     ///< What the AI decides: goals, targets, gangs.
    Combat, ///< Hits, damage, knock-downs, deaths.
    Script, ///< The level scripts: errors, missing bindings.
    World,  ///< Levels, checkpoints, cutscenes, objects, cars.
    Engine, ///< Everything else: start-up, sound, pads, summaries.
};

/// How many categories there are.
inline constexpr std::size_t kCategoryCount = 6;

/// How much a line matters, most first. A log file keeps Info and up unless the category is verbose.
enum class Level : std::uint8_t {
    Error,  ///< Something failed: the run goes on without it.
    Warn,   ///< Something is missing or odd but handled.
    Info,   ///< What happened, at the rate a person can read it.
    Detail, ///< Every step of it; kept in the ring, in a file only with `--log-verbose`.
};

/// Whether a line's text may go into a file. Logs are attached to public issues, so text the game itself supplies
/// (what the scripts print) stays on the console; files and the ring keep its length only.
enum class Privacy : std::uint8_t {
    Public,    ///< Names, ids, numbers and positions: fine anywhere.
    LocalOnly, ///< May hold the game's own text: the console only (withheld()).
};

/// The checkpoint a line is stamped with before any level has set one (`cp-`).
inline constexpr int kNoCheckpoint = 0;
/// Simulation steps in a second of play time: the fixed 1/30 s step (docs/guides/conventions.md#update-and-render).
inline constexpr std::uint64_t kStepsPerSecond = 30;

/// One line of the log.
struct Record {
    std::uint64_t step = 0;         ///< The simulation step it happened in (steps run before it, since start-up).
    int checkpoint = kNoCheckpoint; ///< The level's checkpoint then; kNoCheckpoint outside a level.
    Category category = Category::Engine;
    Level level = Level::Info;
    Privacy privacy = Privacy::Public;
    std::string text{}; ///< One line, no newline.
};

/// The name of `category` as the log writes it and `--log-verbose` takes it (`player`, `ai`, ...).
[[nodiscard]] std::string_view categoryName(Category category);
/// The name of `level` as the log writes it (`error`, `warn`, `info`, `detail`).
[[nodiscard]] std::string_view levelName(Level level);
/// The category named `name` (lowercase, as categoryName() gives it); nothing for another name.
[[nodiscard]] std::optional<Category> parseCategory(std::string_view name);
/// The categories of a comma-separated list (`ai,combat`), or every category for `all`. An empty list, an empty
/// item or an unknown name is ErrorCode::InvalidArgument naming it and the valid names.
[[nodiscard]] std::expected<std::vector<Category>, Error> parseCategoryList(std::string_view list);

/// The play time `step` stands for, `mm:ss.s` (tenths rounded down; the minutes go past 59).
[[nodiscard]] std::string playTime(std::uint64_t step);
/// The stamp a line starts with: `[step N | mm:ss.s | <level> cp<k>]`, `cp-` for kNoCheckpoint.
[[nodiscard]] std::string stamp(std::uint64_t step, Level level, int checkpoint);
/// The whole line: the stamp, the category and the text, `[step 90 | 00:03.0 | info cp3] combat: damage: 12`.
[[nodiscard]] std::string formatLine(const Record& record);
/// `record` as a file may hold it: a LocalOnly record keeps its prefix (the text up to the first `: `) and the length
/// of the rest; a Public one is unchanged.
[[nodiscard]] Record withheld(const Record& record);

} // namespace coney::logging
