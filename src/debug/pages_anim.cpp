// SPDX-License-Identifier: GPL-3.0-or-later
// The Animation player page: every clip the player's anim sets can play, by set and by category with its length,
// played on the player once or looping at a speed, then his own animation back (docs/guides/debug-menu.md#animation-
// player). Coney's own tooling: the original has nothing like it.
#include <cstddef>
#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "debug/anim_catalog.h"
#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"

namespace coney::debug {

namespace {

// What the page shows in place of its items when no player plays.
constexpr std::string_view kNoPlayer = "no player: --play-level, or a sandbox from the Levels page";

// The slowest and fastest speed the page offers, and its step.
constexpr double kMinSpeed = 0.1;
constexpr double kMaxSpeed = 4.0;
constexpr double kSpeedStep = 0.05;

// The menu callbacks below copy strings and call through std::function; all they can throw is a failed allocation,
// which ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// One line on what the Animation player plays: the id, the clip, where it is in it, and whether it loops.
std::string playbackText(const AnimPlayback& now) {
    if (!now.playing) {
        return "nothing";
    }
    return std::format("{} {} {:.2f} / {:.2f} s{}", now.id, now.clip, now.time, now.duration, now.loop ? ", loop" : "");
}

// The label of a clip's item: its id, name and length, unique on its page by the id.
std::string clipLabel(const AnimClipChoice& clip) {
    return std::format("{} {} {:.2f} s", clip.id, clip.clip, clip.seconds);
}

// Fills `page` with the clips of one category of set `set`: each an action that plays it on the player with the
// page's options.
void fillCategory(MenuPage& page, DebugSession& session, std::size_t set, const AnimCategoryClips& category,
                  const std::shared_ptr<AnimPlayOptions>& options) {
    for (const AnimClipChoice& clip : category.clips) {
        MenuItem item = actionItem(clipLabel(clip), [&session, set, clip, options] {
            PlayControls* play = session.play();
            if (play == nullptr) {
                session.print(std::string(kNoPlayer));
                return;
            }
            if (auto played = play->playAnim(set, clip.id, *options); !played) {
                session.print(std::format("anim: {} not played: {}", clip.id, played.error().message));
                return;
            }
            session.print(std::format("anim: {} {} {}, {:.2f} s at rate {:.2f} x {:.2f}", clip.id, clip.clip,
                                      options->loop ? "looping" : "once", clip.playSeconds(options->speed), clip.rate,
                                      options->speed));
        });
        item.withHelp(std::format("{}: {:.2f} s, {:.2f} s at its rate {:.2f}{}.", animIdName(clip.id), clip.seconds,
                                  clip.playSeconds(), clip.rate, clip.generic ? "; from the generic set" : ""));
        item.keepAlive = options;
        page.add(std::move(item));
    }
}

// Fills `page` with anim set `set`'s clips by category, loading the set first when it is not loaded yet.
void fillSet(MenuPage& page, DebugSession& session, std::size_t set, const std::shared_ptr<AnimPlayOptions>& options) {
    PlayControls* play = session.play();
    if (play == nullptr) {
        page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
        return;
    }
    auto clips = play->animClips(set);
    if (!clips) {
        const std::string message = clips.error().message;
        session.print(std::format("anim: set not loaded: {}", message));
        page.add(watchItem("Not loaded", [message] { return std::string{message}; }));
        return;
    }
    // The set's size, then one submenu per category tagged with its count.
    float total = 0.0F;
    for (const AnimClipChoice& clip : *clips) {
        total += clip.seconds;
    }
    page.add(watchItem("Clips", [count = clips->size(), total] { return std::format("{}, {:.0f} s", count, total); }));
    auto categories = std::make_shared<const std::vector<AnimCategoryClips>>(byCategory(*clips));
    for (std::size_t i = 0; i < categories->size(); ++i) {
        const AnimCategoryClips& category = (*categories)[i];
        MenuItem item = submenuItem(category.name, [&session, set, categories, i, options] {
            const AnimCategoryClips& opened = (*categories)[i];
            auto clipsPage = std::make_shared<MenuPage>(opened.name);
            fillCategory(*clipsPage, session, set, opened, options);
            return clipsPage;
        });
        item.detail = std::to_string(category.clips.size());
        item.help = std::format("The set's {} clips of the {} anim ids.", category.clips.size(), category.name);
        page.add(std::move(item));
    }
}

// Fills the Animation player page: what plays, the options, stop, and one submenu per anim set.
void fillAnimPlayer(MenuPage& page, DebugSession& session, const std::shared_ptr<AnimPlayOptions>& options) {
    PlayControls* play = session.play();
    if (play == nullptr) {
        page.add(watchItem("No player", [] { return std::string(kNoPlayer); }));
        return;
    }
    page.add(watchItem("Playing", [&session] {
                 const PlayControls* p = session.play();
                 return p != nullptr ? playbackText(p->animPlayback()) : std::string("-");
             }).withHelp("The clip the player plays for this page, and how far into it he is."));
    MenuItem loop = toggleItem("Loop", [options] { return options->loop; }, [options](bool on) { options->loop = on; });
    loop.withHelp("Plays the next clip chosen over and over until Stop, rather than once.");
    loop.keepAlive = options;
    page.add(std::move(loop));
    MenuItem speed = numberItem(
        "Speed", [options] { return static_cast<double>(options->speed); },
        [options](double v) { options->speed = static_cast<float>(v); }, kMinSpeed, kMaxSpeed, kSpeedStep, false);
    speed.units = "x";
    speed.defaultValue = 1.0;
    speed.withHelp("Multiplies the clip's own rate for the next clip chosen.");
    speed.keepAlive = options;
    page.add(std::move(speed));
    MenuItem inPlace =
        toggleItem("In place", [options] { return options->inPlace; }, [options](bool on) { options->inPlace = on; });
    inPlace.withHelp("The next clip chosen does not move or turn the player by its root motion.");
    inPlace.keepAlive = options;
    page.add(std::move(inPlace));
    page.add(actionItem("Stop", [&session] {
                 if (PlayControls* p = session.play(); p != nullptr) {
                     p->stopAnim();
                     session.print("anim: stopped");
                 }
             }).withHelp("Ends the clip playing and gives the player his own animation back."));

    const std::vector<std::string> sets = play->animSets();
    if (sets.empty()) {
        page.add(watchItem("Anim sets", [] { return std::string("none here"); }));
        return;
    }
    for (std::size_t set = 0; set < sets.size(); ++set) {
        MenuItem item = submenuItem(sets[set], [&session, set, title = sets[set], options] {
            auto setPage = std::make_shared<MenuPage>(title);
            fillSet(*setPage, session, set, options);
            return setPage;
        });
        item.help = set == 0 ? "The player's own clips, with the generic ones his character leaves to them."
                             : "Another anim set the player's skeleton can play; loaded from the disc when opened.";
        page.add(std::move(item));
    }
}

// NOLINTEND(bugprone-exception-escape)

} // namespace

void addAnimPlayerPage(DebugSession& session) {
    // The options outlive each opening of the page, so a speed set once stays for the next clip.
    auto options = std::make_shared<AnimPlayOptions>();
    session.model().addPage(
        "Animation player", [&session, options](MenuPage& page) { fillAnimPlayer(page, session, options); },
        "Any clip of the anim sets played on the player.");
}

} // namespace coney::debug
