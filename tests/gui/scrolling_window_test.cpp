// SPDX-License-Identifier: GPL-3.0-or-later
// The window of the statistics screen's list (docs/research/game-stats.md#scroll): a move past the middle row scrolls
// it by hiding one entry while revealing another, a quarter an update; the clip draws the entries that fit (the
// focused one past it too) and scrolls the window to keep a focused entry that runs past it in, and back when there is
// room again. The numbers follow the original's run in PCSX2 (research/runs/, the statistics screen scrolled) with
// synthetic entry heights.
// Each new case here failed first on the unit's stubbed source (coney-tools original fails-first).
#include "gui/scrolling_window.h"

#include <cstddef>
#include <map>
#include <optional>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using coney::gui::ScrollingWindow;
using coney::gui::ScrollingWindowSetup;
using coney::gui::ScrollState;

namespace {

// Entries of given heights at reveal 1, one of them focused; remembers where each was last placed.
struct Entries {
    std::vector<float> heights;
    std::size_t focused = 0;
    std::map<std::size_t, float> offsets{};

    ScrollingWindow::Entries callbacks() {
        return ScrollingWindow::Entries{
            .layOut =
                [this](std::size_t index, std::optional<float> offset, float /*reveal*/) {
                    if (offset) {
                        offsets[index] = *offset;
                    }
                    return heights.at(index);
                },
            .focused = [this](std::size_t index) { return index == focused; },
        };
    }
};

// A header's height and an open entry's (a header and its block), as the original's run measured them.
constexpr float kHeader = 0.09F;
constexpr float kOpen = 0.3519F;

} // namespace

TEST_CASE("scrolling window: a move down past the middle row hides the top entry a quarter an update while the next "
          "one shows at the bottom",
          "[scrolling_window]") {
    // Expected: game-stats.md#scroll, confirmed (runtime) and confirmed (code) at 0x001e1d70 and 0x001e21d8
    ScrollingWindow window;
    window.setup(ScrollingWindowSetup{}, 7);
    Entries entries{.heights = std::vector<float>(7, kHeader)};
    window.update(entries.callbacks());
    CHECK(window.count() == 5);
    CHECK(window.drawn() == 5);

    // The second row is not past the middle (the middle row of five is the second with evenRows): still.
    window.moveDown(1);
    CHECK_FALSE(window.scrolling());
    // The third is: the window takes one more entry and the top one goes.
    window.moveDown(2);
    CHECK(window.state() == ScrollState::Down);
    CHECK(window.count() == 6);
    const float tops[] = {0.75F, 0.5F, 0.25F};
    for (const float top : tops) {
        window.update(entries.callbacks());
        CHECK(window.reveal(0) == top);
        CHECK(window.reveal(5) == 1.0F - top);
        // The entries below are placed at the stacked heights, the top one's × its reveal.
        CHECK(entries.offsets.at(1) == Approx(kHeader * top));
    }
    window.update(entries.callbacks());
    CHECK(window.reveal(0) == 0.0F);
    CHECK(window.reveal(5) == 1.0F);
    CHECK_FALSE(window.scrolling());
    CHECK(window.first() == 1);
    CHECK(window.count() == 5);

    // Down again to the window's last entry; then the list's end stops the window.
    window.moveDown(5);
    CHECK(window.state() == ScrollState::Down);
    for (int update = 0; update < 4; ++update) {
        window.update(entries.callbacks());
    }
    CHECK(window.first() == 2);
    window.moveDown(6);
    CHECK_FALSE(window.scrolling());
}

TEST_CASE("scrolling window: a move up above the middle row reveals the entry above at the top while the bottom one "
          "goes",
          "[scrolling_window]") {
    // Expected: game-stats.md#scroll, confirmed (runtime) and confirmed (code) at 0x001e1cb8 and 0x001e21d8
    ScrollingWindow window;
    window.setup(ScrollingWindowSetup{}, 7);
    Entries entries{.heights = std::vector<float>(7, kHeader)};
    for (const std::size_t selected : {std::size_t{1}, std::size_t{2}, std::size_t{3}}) {
        window.moveDown(selected);
        for (int update = 0; update < 5; ++update) {
            window.update(entries.callbacks());
        }
    }
    REQUIRE(window.first() == 2);
    CHECK(window.reveal(1) == 0.0F);

    // Up to the window's first row: above the middle, with entries above, so the window starts one higher.
    window.moveUp(2);
    CHECK(window.state() == ScrollState::Up);
    CHECK(window.first() == 1);
    CHECK(window.count() == 6);
    const float tops[] = {0.25F, 0.5F, 0.75F};
    for (const float top : tops) {
        window.update(entries.callbacks());
        CHECK(window.reveal(1) == top);
        CHECK(window.reveal(6) == 1.0F - top);
    }
    window.update(entries.callbacks());
    CHECK_FALSE(window.scrolling());
    CHECK(window.reveal(1) == 1.0F);
    CHECK(window.reveal(6) == 0.0F);
    CHECK(window.first() == 1);
    CHECK(window.count() == 5);

    // A move up to an entry above the window always scrolls; at the top it cannot.
    window.moveUp(0);
    CHECK(window.state() == ScrollState::Up);
    CHECK(window.first() == 0);
}

TEST_CASE("scrolling window: the clip draws the entries that fit and the focused one past it, hides the top entries "
          "while a focused one runs past it and reveals them again when there is room",
          "[scrolling_window]") {
    // Expected: game-stats.md#scroll, confirmed (runtime) and confirmed (code) at 0x001e21d8 and 0x001e2e40
    ScrollingWindow window;
    window.setup(ScrollingWindowSetup{}, 7);
    Entries entries{.heights = std::vector<float>(7, kHeader)};

    // The first entry open: 0.3519 and 0.09 fit under 0.5, the third header does not and is not focused.
    entries.heights.at(0) = kOpen;
    window.update(entries.callbacks());
    CHECK(window.drawn() == 2);
    CHECK_FALSE(window.scrolling());

    // To the list's last entry: the window stops at the end (entries 2-6).
    entries.heights.at(0) = kHeader;
    for (std::size_t selected = 1; selected < 7; ++selected) {
        entries.focused = selected;
        window.moveDown(selected);
        for (int update = 0; update < 5; ++update) {
            window.update(entries.callbacks());
        }
    }
    REQUIRE(window.first() == 2);
    REQUIRE(window.count() == 5);
    CHECK(window.drawn() == 5);

    // Its entry opens past the clip: it is drawn all the same, being focused, and the top entries go one by one, a
    // quarter an update, until it fits.
    entries.heights.at(6) = kOpen;
    window.update(entries.callbacks());
    CHECK(window.drawn() == 5);
    CHECK(window.state() == ScrollState::HideTop);
    window.update(entries.callbacks());
    CHECK(window.reveal(2) == 0.75F);
    for (int update = 0; update < 3; ++update) {
        window.update(entries.callbacks());
    }
    CHECK(window.first() == 3);
    CHECK(window.count() == 4);
    for (int update = 0; update < 20; ++update) {
        window.update(entries.callbacks());
    }
    CHECK_FALSE(window.scrolling());
    CHECK(window.first() == 5);
    CHECK(window.count() == 2);
    CHECK(window.drawn() == 2);

    // It closes: there is room for the entries above again, revealed one by one at the top.
    entries.heights.at(6) = kHeader;
    window.update(entries.callbacks());
    CHECK(window.state() == ScrollState::RevealTop);
    CHECK(window.first() == 4);
    CHECK(window.reveal(4) == 0.0F);
    // While it is revealed, it is drawn as well.
    CHECK(window.drawn() == 3);
    for (int update = 0; update < 30; ++update) {
        window.update(entries.callbacks());
    }
    CHECK_FALSE(window.scrolling());
    CHECK(window.first() == 2);
    CHECK(window.count() == 5);
}
