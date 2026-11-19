// SPDX-License-Identifier: GPL-3.0-or-later
// The rows of the Mods screen's story view (docs/guides/studio/mission-mods.md#graph-mods-screen): Coney's own, with no
// counterpart in the original. Every mod here is synthetic.
#include "core/story_view.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/story_choices.h"
#include "core/story_graph.h"
#include "core/story_merge.h"

using coney::story::buildStoryView;
using coney::story::GroupKind;
using coney::story::mergeStory;
using coney::story::StoryChoice;
using coney::story::StoryGraph;
using coney::story::StoryMerge;
using coney::story::StoryMission;
using coney::story::StoryMod;
using coney::story::StoryViewRow;
using Kind = coney::story::StoryViewRow::Kind;
using Action = coney::story::StoryViewRow::Action;

namespace {

// A mission of `id` and `title` after `anchor`.
StoryMission after(std::string id, std::string anchor, std::string title = {}) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.title = std::move(title);
    mission.level = "levels/a.clevel";
    mission.after = std::move(anchor);
    return mission;
}

// A mission of `id` and `title` that replaces `target`.
StoryMission replacing(std::string id, std::string target, std::string title = {}) {
    StoryMission mission;
    mission.id = std::move(id);
    mission.title = std::move(title);
    mission.level = "levels/a.clevel";
    mission.replace = std::move(target);
    return mission;
}

// A story mod named `name`, called `title` on the screen, whose package hash is `hash`.
StoryMod mod(std::string name, std::string title, std::string hash, std::vector<StoryMission> missions) {
    StoryMod made{.name = std::move(name), .story = {}, .title = std::move(title), .packageHash = std::move(hash)};
    made.story.missions = std::move(missions);
    return made;
}

// The rows of the merge of `mods` over the original's chain, with the player's `choices`.
std::vector<StoryViewRow> rowsOf(const std::vector<StoryMod>& mods, const std::vector<StoryChoice>& choices = {}) {
    const StoryMerge merge = mergeStory(StoryGraph::original(), mods, choices);
    return buildStoryView(merge, mods);
}

// The rows from the one with id `from` up to, not including, the one with id `to`.
std::vector<StoryViewRow> between(const std::vector<StoryViewRow>& rows, std::string_view from, std::string_view to) {
    std::vector<StoryViewRow> out;
    bool inside = false;
    for (const StoryViewRow& row : rows) {
        if (row.id == from) {
            inside = true;
        }
        if (row.id == to) {
            break;
        }
        if (inside) {
            out.push_back(row);
        }
    }
    return out;
}

// The row with story id `id` that is not a refusal.
const StoryViewRow* placed(const std::vector<StoryViewRow>& rows, std::string_view id) {
    const auto found =
        std::ranges::find_if(rows, [id](const StoryViewRow& row) { return row.id == id && row.kind != Kind::Refusal; });
    return found == rows.end() ? nullptr : &*found;
}

} // namespace

TEST_CASE("story view: the original's chain is one row a step, in the game's order", "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (the merged chain in order, original steps in the
    // game's colours), Coney's own design (no counterpart in the original)
    const StoryMerge merge = mergeStory(StoryGraph::original(), {});
    const std::vector<StoryViewRow> rows = buildStoryView(merge, {});
    REQUIRE(rows.size() == merge.graph.steps().size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        CHECK(rows[i].kind == Kind::Original);
        CHECK(rows[i].id == merge.graph.steps()[i].id());
        CHECK(rows[i].action == Action::None);
        CHECK(rows[i].selectable());
    }
}

TEST_CASE("story view: a mod's missions sit under its title, a run of them under one", "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (mod missions under their mod's title), Coney's own
    // design (no counterpart in the original)
    const std::vector<StoryMod> mods{
        mod("yard-brawl", "Yard Brawl", "hash-y",
            {after("alley", "level80", "The Alley"), after("roof", "level87", "The Roof")}),
        mod("quiet", {}, "hash-q", {after("hush", "hub:6", "Hush")})};
    const std::vector<StoryViewRow> rows = rowsOf(mods);
    const auto first = between(rows, "level80", "level87");
    REQUIRE(first.size() == 3);
    CHECK(first[0].kind == Kind::Original);
    CHECK(first[1].kind == Kind::ModTitle);
    CHECK(first[1].text == "Yard Brawl");
    CHECK(first[1].mod == "yard-brawl");
    CHECK_FALSE(first[1].selectable());
    CHECK(first[2].kind == Kind::ModMission);
    CHECK(first[2].id == "yard-brawl.alley");
    CHECK(first[2].text == "The Alley");
    CHECK(first[2].mod == "yard-brawl");
    // A mod with no title is called by its name; a mission after a hub chapter opens a new chapter under the same
    // title.
    const auto hub = between(rows, "hub:6", "level86");
    REQUIRE(hub.size() == 4);
    CHECK(hub[1].kind == Kind::ModTitle);
    CHECK(hub[1].text == "quiet");
    CHECK(hub[2].id == "quiet.hush");
    CHECK(hub[3].id == "hub:6.quiet.hush");
    CHECK(hub[3].kind == Kind::ModMission);
    CHECK(hub[3].mod == "quiet");
    // The title comes once for a run, and again when the same mod's mission follows another mod's.
    const std::vector<StoryMod> run{
        mod("aaa", "AAA", "ha", {after("x", "level80"), after("w", "bbb.y"), after("v", "aaa.w")}),
        mod("bbb", "BBB", "hb", {after("y", "aaa.x")})};
    const auto chain = between(rowsOf(run), "level80", "level87");
    REQUIRE(chain.size() == 8);
    CHECK(chain[1].kind == Kind::ModTitle);
    CHECK(chain[2].id == "aaa.x");
    CHECK(chain[3].kind == Kind::ModTitle);
    CHECK(chain[3].text == "BBB");
    CHECK(chain[4].id == "bbb.y");
    CHECK(chain[5].kind == Kind::ModTitle);
    CHECK(chain[5].text == "AAA");
    CHECK(chain[6].id == "aaa.w");
    CHECK(chain[7].id == "aaa.v");
}

TEST_CASE("story view: two missions on one anchor can be swapped", "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (shared anchor: two or more missions placed at one
    // anchor, with Swap), Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods{mod("aaa", "AAA", "ha", {after("x", "level80", "X")}),
                                     mod("bbb", "BBB", "hb", {after("y", "level80", "Y"), after("z", "level87", "Z")})};
    const std::vector<StoryViewRow> rows = rowsOf(mods);
    const StoryViewRow* x = placed(rows, "aaa.x");
    const StoryViewRow* y = placed(rows, "bbb.y");
    const StoryViewRow* z = placed(rows, "bbb.z");
    REQUIRE(x != nullptr);
    REQUIRE(y != nullptr);
    REQUIRE(z != nullptr);
    CHECK(x->action == Action::Swap);
    CHECK(x->anchor == "level80");
    CHECK(y->action == Action::Swap);
    CHECK(y->anchor == "level80");
    CHECK_FALSE(x->chosen);
    // A mission alone on its anchor has nothing to swap, and nor has an original step.
    CHECK(z->action == Action::None);
    CHECK(z->anchor.empty());
    CHECK(placed(rows, "level80")->action == Action::None);
}

TEST_CASE("story view: a swapped group says the order is the player's", "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen, Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods{mod("aaa", "AAA", "ha", {after("x", "level80")}),
                                     mod("bbb", "BBB", "hb", {after("y", "level80")})};
    const std::vector<StoryChoice> swapped{
        {.kind = GroupKind::Anchor, .anchor = "level80", .members = {{"bbb.y", "hb"}, {"aaa.x", "ha"}}}};
    const std::vector<StoryViewRow> rows = rowsOf(mods, swapped);
    const auto chain = between(rows, "level80", "level87");
    REQUIRE(chain.size() == 5);
    CHECK(chain[2].id == "bbb.y");
    CHECK(chain[4].id == "aaa.x");
    CHECK(chain[2].chosen);
    CHECK(chain[4].chosen);
}

TEST_CASE("story view: a replacement says what it stands in for, and with two the loser can be chosen",
          "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (replaced: an original mission another mission
    // stands in for, and, for two replacements, the choice between them), Coney's own design (no counterpart in the
    // original)
    const std::vector<StoryMod> mods{mod("aaa", "AAA", "ha", {replacing("p", "level86", "P")}),
                                     mod("bbb", "BBB", "hb", {replacing("q", "level86", "Q")}),
                                     mod("ccc", "CCC", "hc", {replacing("r", "level70", "R")})};
    const std::vector<StoryViewRow> rows = rowsOf(mods);
    const StoryViewRow* winner = placed(rows, "aaa.p");
    REQUIRE(winner != nullptr);
    CHECK(winner->kind == Kind::ModMission);
    CHECK(winner->replaces == "level86");
    CHECK(winner->rivals == std::vector<std::string>{"bbb.q"});
    CHECK(winner->action == Action::None);
    // The loser is a refusal row with the merge's message, from which the player can choose it.
    const auto loser = std::ranges::find_if(rows, [](const StoryViewRow& row) { return row.id == "bbb.q"; });
    REQUIRE(loser != rows.end());
    CHECK(loser->kind == Kind::Refusal);
    CHECK(loser->text == "bbb.q: 'level86' is already replaced by 'aaa.p'");
    CHECK(loser->action == Action::Choose);
    CHECK(loser->replaces == "level86");
    CHECK(loser->mod == "bbb");
    CHECK(loser->selectable());
    // A mission that replaces one that is not there is only refused, with nothing to choose, and a replacement nobody
    // else makes has no rivals.
    const auto missing = std::ranges::find_if(rows, [](const StoryViewRow& row) { return row.id == "ccc.r"; });
    REQUIRE(missing != rows.end());
    CHECK(missing->kind == Kind::Refusal);
    CHECK(missing->action == Action::None);
    CHECK(missing->replaces.empty());
    const std::vector<StoryMod> single{mod("aaa", "AAA", "ha", {replacing("p", "level86", "P")})};
    const std::vector<StoryViewRow> singleRows = rowsOf(single);
    REQUIRE(placed(singleRows, "aaa.p") != nullptr);
    CHECK(placed(singleRows, "aaa.p")->replaces == "level86");
    CHECK(placed(singleRows, "aaa.p")->rivals.empty());
}

TEST_CASE("story view: refused missions are listed last with their messages", "[story_view]") {
    // Expected: ../guides/studio/mission-mods.md#graph-mods-screen (refused: each refused mission with its message, so
    // a player sees why a mission is missing without a log), Coney's own design (no counterpart in the original)
    const std::vector<StoryMod> mods{
        mod("yard-brawl", "Yard Brawl", "hy", {after("alley", "level70"), after("roof", "yard-brawl.alley")}),
        mod("loop", "Loop", "hl", {after("a", "loop.b"), after("b", "loop.a")})};
    const std::vector<StoryViewRow> rows = rowsOf(mods);
    const auto header =
        std::ranges::find_if(rows, [](const StoryViewRow& row) { return row.kind == Kind::RefusedHeader; });
    REQUIRE(header != rows.end());
    CHECK_FALSE(header->selectable());
    std::vector<std::string> messages;
    for (auto row = header + 1; row != rows.end(); ++row) {
        REQUIRE(row->kind == Kind::Refusal);
        CHECK(row->selectable());
        CHECK(row->action == Action::None);
        messages.push_back(row->id + " | " + row->text);
    }
    CHECK(messages ==
          std::vector<std::string>{"yard-brawl.alley | yard-brawl.alley: no story step 'level70'",
                                   "yard-brawl.roof | yard-brawl.roof: story step 'yard-brawl.alley' was refused",
                                   "loop.a | story cycle: loop.a, loop.b, loop.a",
                                   "loop.b | story cycle: loop.a, loop.b, loop.a"});
    // Nothing refused, no heading.
    const std::vector<StoryMod> fine{mod("aaa", "AAA", "ha", {after("x", "level80")})};
    CHECK(std::ranges::none_of(rowsOf(fine), [](const StoryViewRow& row) { return row.kind == Kind::RefusedHeader; }));
}
