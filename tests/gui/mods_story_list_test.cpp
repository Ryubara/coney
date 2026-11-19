// SPDX-License-Identifier: GPL-3.0-or-later
// The Mods screen's Story list (docs/guides/studio/mission-mods.md#graph-mods-screen), driven with scripted pad input
// through the menu's own input dispatch: Coney's own, with no counterpart in the original. Every mod here is synthetic.
#include "gui/mods_story_list.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"
#include "core/story_graph.h"
#include "core/story_merge.h"
#include "core/story_view.h"
#include "gui/menu_input.h"
#include "support/fixtures.h"
#include "support/optional_value.h"

using coney::Pad;
using coney::PadSample;
using coney::gui::MenuCommand;
using coney::gui::MenuInput;
using coney::gui::ModsStoryList;
using coney::story::StoryGraph;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryViewRow;
using Result = coney::gui::ModsStoryList::Result;
using Kind = coney::story::StoryViewRow::Kind;

namespace {

namespace fs = std::filesystem;

// A mission of `id` after `anchor`.
StoryMission after(std::string id, std::string anchor) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    return mission;
}

// A mission that replaces `target`.
StoryMission replacing(std::string id, std::string target) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.level = "levels/a.clevel";
    mission.replace = std::move(target);
    return mission;
}

// A story mod named `name` whose package hash is `hash`.
StoryMod mod(const std::string& name, std::string hash, std::vector<StoryMission> missions) {
    StoryMod made{.name = name, .story = {}, .title = name + " title", .packageHash = std::move(hash)};
    made.story.missions = std::move(missions);
    return made;
}

// Two mods with a mission each on the anchor `level80`.
std::vector<StoryMod> sharedAnchor() {
    return {mod("aaa", "hash-a", {after("x", "level80")}), mod("bbb", "hash-b", {after("y", "level80")})};
}

// A pad held by a person, a frame every 200 ms: what the menu's input dispatch turns into commands.
class ScriptedPad {
  public:
    // Presses and lets go of `buttons`, and gives what the dispatch made of it: d-pad directions at the press, accept
    // and back at the release.
    std::optional<MenuCommand> tap(std::uint16_t buttons) {
        std::optional<MenuCommand> command = frame(buttons);
        if (const auto released = frame(0)) {
            command = released;
        }
        return command;
    }

  private:
    // One pad frame holding `buttons`.
    std::optional<MenuCommand> frame(std::uint16_t buttons) {
        PadSample sample;
        sample.connected = true;
        sample.buttons = buttons;
        sample.sticks = {coney::pad::kStickCentre, coney::pad::kStickCentre, coney::pad::kStickCentre,
                         coney::pad::kStickCentre};
        sample.pressure.fill(255);
        m_pad.update(sample);
        m_nowMs += 200;
        return m_input.dispatch(m_pad, m_nowMs);
    }

    Pad m_pad;
    MenuInput m_input;
    std::uint64_t m_nowMs = 1000;
};

// Taps `buttons` on `pad` and gives the command to `list`.
Result press(ScriptedPad& pad, ModsStoryList& list, std::uint16_t buttons) {
    const std::optional<MenuCommand> command = pad.tap(buttons);
    REQUIRE(command.has_value());
    return list.handle(coney::test::got(command));
}

// Moves the cursor down until it is on the row with story id `id` (not a refusal when `refusal` is false).
void goTo(ScriptedPad& pad, ModsStoryList& list, std::string_view id) {
    for (std::size_t steps = 0; steps < list.rows().size(); ++steps) {
        const std::optional<std::size_t> cursor = list.cursor();
        if (cursor.has_value() && list.rows()[coney::test::got(cursor)].id == id) {
            return;
        }
        press(pad, list, coney::pad::kDown);
    }
    FAIL("no row " << id);
}

// The merged chain's step ids from `from` to before `to`.
std::vector<std::string> chain(const ModsStoryList& list, std::string_view from, std::string_view to) {
    std::vector<std::string> ids;
    bool inside = false;
    for (const auto& step : list.merge().graph.steps()) {
        const std::string id = step.id();
        if (id == from) {
            inside = true;
        }
        if (id == to) {
            break;
        }
        if (inside) {
            ids.push_back(id);
        }
    }
    return ids;
}

// The text of the file `file`.
std::string textOf(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

TEST_CASE("mods story list: swapping two missions on one anchor reorders the chain and is kept across a restart",
          "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (shared anchor with Swap, kept in enabled.toml with
    // both package hashes), Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "mods" / "enabled.toml";
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "# mods in load order\n[[mod]]\nname = \"aaa\"\nhash = \"hash-a\"\n";
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), sharedAnchor(), file);
    REQUIRE(list.shown());
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    // 1. Down to the first mission on the anchor and accept: they change places, the cursor stays on the mission.
    goTo(pad, list, "aaa.x");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Swapped);
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    REQUIRE(list.cursor().has_value());
    CHECK(list.rows()[coney::test::got(list.cursor())].id == "aaa.x");
    CHECK(list.message().empty());
    // 2. The choice is in enabled.toml with both package hashes, and the file's own lines stay.
    const std::string written = textOf(file);
    CHECK(written.starts_with("# mods in load order\n[[mod]]\nname = \"aaa\"\nhash = \"hash-a\"\n"));
    CHECK(written.contains("[[story.choice]]"));
    CHECK(written.contains("missions = [\"bbb.y\", \"aaa.x\"]"));
    CHECK(written.contains("packages = [\"hash-b\", \"hash-a\"]"));
    // 3. A restart (a new list over the same mods and file) has the same chain, and the rows say the order is chosen.
    ModsStoryList restarted(StoryGraph::original(), sharedAnchor(), file);
    CHECK(chain(restarted, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    CHECK(restarted.merge().graph.hash() == list.merge().graph.hash());
    const auto chosen =
        std::ranges::find_if(restarted.rows(), [](const StoryViewRow& row) { return row.id == "bbb.y"; });
    REQUIRE(chosen != restarted.rows().end());
    CHECK(chosen->chosen);
    // 4. Swapping back drops the choice from the file, and the restart has the load order again.
    ScriptedPad again;
    goTo(again, restarted, "bbb.y");
    CHECK(press(again, restarted, coney::pad::kCross) == Result::Swapped);
    CHECK(chain(restarted, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    CHECK_FALSE(textOf(file).contains("story.choice"));
    CHECK(textOf(file).starts_with("# mods in load order\n"));
    const ModsStoryList last(StoryGraph::original(), sharedAnchor(), file);
    CHECK(chain(last, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
}

TEST_CASE("mods story list: a swapped order is not kept for a mod that changed", "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#two-mods (a choice is kept with both package hashes), Coney's own
    // design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    {
        ScriptedPad pad;
        ModsStoryList list(StoryGraph::original(), sharedAnchor(), file);
        goTo(pad, list, "bbb.y");
        REQUIRE(press(pad, list, coney::pad::kCross) == Result::Swapped);
        CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    }
    // The player updated `aaa`: its package hash is another, so the choice made between the old packages is ignored.
    std::vector<StoryMod> updated = sharedAnchor();
    updated[0].packageHash = "hash-a2";
    const ModsStoryList list(StoryGraph::original(), updated, file);
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
}

TEST_CASE("mods story list: the winner of two replacements is chosen on the refused one", "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (replaced, and for two replacements the choice
    // between them), Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    const std::vector<StoryMod> mods{mod("aaa", "hash-a", {replacing("p", "level86")}),
                                     mod("bbb", "hash-b", {replacing("q", "level86")})};
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), mods, file);
    CHECK(chain(list, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "aaa.p", "level93"});
    // Accept on the placed replacement does nothing; on the refused one it takes the place.
    goTo(pad, list, "aaa.p");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Nothing);
    goTo(pad, list, "bbb.q");
    CHECK(list.rows()[coney::test::got(list.cursor())].kind == Kind::Refusal);
    CHECK(press(pad, list, coney::pad::kCross) == Result::Chose);
    CHECK(chain(list, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "bbb.q", "level93"});
    CHECK(list.rows()[coney::test::got(list.cursor())].id == "bbb.q");
    CHECK(list.rows()[coney::test::got(list.cursor())].kind == Kind::ModMission);
    CHECK(textOf(file).contains("kind = \"replace\""));
    CHECK(textOf(file).contains("anchor = \"level86\""));
    CHECK(textOf(file).contains("missions = [\"bbb.q\", \"aaa.p\"]"));
    // The choice is kept across a restart, and the other mission is now the one to choose.
    ModsStoryList restarted(StoryGraph::original(), mods, file);
    CHECK(chain(restarted, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "bbb.q", "level93"});
    ScriptedPad again;
    goTo(again, restarted, "aaa.p");
    CHECK(restarted.rows()[coney::test::got(restarted.cursor())].kind == Kind::Refusal);
    CHECK(press(again, restarted, coney::pad::kCross) == Result::Chose);
    CHECK(chain(restarted, "hub:6", "hub:7") == std::vector<std::string>{"hub:6", "aaa.p", "level93"});
    CHECK_FALSE(textOf(file).contains("story.choice"));
}

TEST_CASE("mods story list: the cursor stops on steps, missions and refusals and not on titles", "[mods_story_list]") {
    // Expected: ../guides/studio/ui.md#mods-screen (a scrolling list with the profile manager's look), Coney's own
    // design (no counterpart in the original)
    const coney::test::TempDir dir;
    const std::vector<StoryMod> mods{mod("aaa", "hash-a", {after("x", "level80"), after("lost", "level70")})};
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), mods, dir.path() / "enabled.toml");
    REQUIRE(list.cursor().has_value());
    CHECK(coney::test::got(list.cursor()) == 0);
    // Up at the top does nothing; down walks every row it can stop on, never a title or the heading.
    CHECK(press(pad, list, coney::pad::kUp) == Result::Nothing);
    CHECK(coney::test::got(list.cursor()) == 0);
    std::size_t stops = 1;
    while (press(pad, list, coney::pad::kDown) == Result::Moved) {
        ++stops;
        CHECK(list.rows()[coney::test::got(list.cursor())].selectable());
    }
    const auto selectable = static_cast<std::size_t>(
        std::ranges::count_if(list.rows(), [](const StoryViewRow& row) { return row.selectable(); }));
    CHECK(stops == selectable);
    // The last row is the refusal with its message; accept there does nothing, and back closes the list.
    const StoryViewRow& last = list.rows()[coney::test::got(list.cursor())];
    CHECK(last.kind == Kind::Refusal);
    CHECK(last.text == "aaa.lost: no story step 'level70'");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Nothing);
    CHECK(press(pad, list, coney::pad::kCircle) == Result::Closed);
    // Going back up crosses the same rows.
    CHECK(press(pad, list, coney::pad::kUp) == Result::Moved);
    CHECK(list.rows()[coney::test::got(list.cursor())].selectable());
}

TEST_CASE("mods story list: with no story mod there is no list", "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (a Story list when any enabled mod has a [story]
    // section), Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    ModsStoryList none(StoryGraph::original(), {}, file);
    CHECK_FALSE(none.shown());
    CHECK_FALSE(fs::exists(file));
    // A story mod with no missions still makes it a modded session, so the list is there (with the original's chain).
    const ModsStoryList quiet(StoryGraph::original(), {mod("quiet", "hash-q", {})}, file);
    CHECK(quiet.shown());
    CHECK(quiet.rows().size() == quiet.merge().graph.steps().size());
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("mods story list: a file that cannot be written keeps the choice for the session and says so",
          "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen, Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path blocker = dir.path() / "blocker";
    std::ofstream(blocker) << "not a folder";
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), sharedAnchor(), blocker / "enabled.toml");
    goTo(pad, list, "aaa.x");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Swapped);
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    CHECK_FALSE(list.message().empty());
}

TEST_CASE("mods story list: a file with a broken story choice leaves the load order and says so", "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen, Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    std::ofstream(file, std::ios::binary) << "[[story.choice]]\nkind = \"sideways\"\n";
    const ModsStoryList list(StoryGraph::original(), sharedAnchor(), file);
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "aaa.x", "bbb.y", "level87"});
    CHECK(list.message().contains("enabled.toml"));
}

TEST_CASE("mods story list: a file with a broken story choice is not written over, and the swap holds for the session",
          "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen, Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    // A valid choice the player kept for another anchor, and a table that is broken.
    const std::string before =
        "[[mod]]\nname = \"aaa\"\n\n[[story.choice]]\nkind = \"after\"\nanchor = \"level70\"\n"
        "missions = [\"q.a\", \"r.b\"]\npackages = [\"h\", \"i\"]\n\n[[story.choice]]\nkind = \"sideways\"\n";
    std::ofstream(file, std::ios::binary) << before;
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), sharedAnchor(), file);
    goTo(pad, list, "aaa.x");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Swapped);
    // The session has the swap, the file is as the player left it, and the list says why.
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    CHECK(textOf(file) == before);
    CHECK(list.message().contains("enabled.toml"));
}

TEST_CASE("mods story list: a file broken while the list is open is not written over either", "[mods_story_list]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen, Coney's own design (no counterpart in the original)
    const coney::test::TempDir dir;
    const fs::path file = dir.path() / "enabled.toml";
    std::ofstream(file, std::ios::binary) << "[[mod]]\nname = \"aaa\"\n";
    ScriptedPad pad;
    ModsStoryList list(StoryGraph::original(), sharedAnchor(), file);
    CHECK(list.message().empty());
    // The player edits the file in an editor while the screen is open, and leaves a story choice broken.
    const std::string edited = "[[mod]]\nname = \"aaa\"\n\n[[story.choice]]\nkind = \"sideways\"\n";
    std::ofstream(file, std::ios::binary | std::ios::trunc) << edited;
    goTo(pad, list, "aaa.x");
    CHECK(press(pad, list, coney::pad::kCross) == Result::Swapped);
    CHECK(chain(list, "level80", "hub:1") == std::vector<std::string>{"level80", "bbb.y", "aaa.x", "level87"});
    CHECK(textOf(file) == edited);
    CHECK(list.message().contains("Mods screen"));
}
