// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A Rumble menu (gui::RumbleMenu) over the synthetic chunks of rumble_fixtures.h and a synthetic font, played frame
// by frame from an input script as two pads: port 1 the HUD player's, port 2 the second player's
// (docs/research/frontend.md#rumble-screens).

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "graphics/font.h"
#include "graphics/sprite_batch.h"
#include "gui/global_strings.h"
#include "gui/rumble_mode_gui/rumble_menu.h"
#include "gui/widget.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "support/font_fixtures.h"
#include "support/rumble_fixtures.h"
#include "warriors/game_state.h"

namespace coney::test {

/// A host for the bindings that asks nothing of a front end.
class QuietRumbleHost final : public script::BindingHost {
  public:
    void showProfileManager(std::string_view /*onRumble*/, std::string_view /*onStartGame*/) override {}
    void showRumbleModeInterface(std::string_view /*onCancel*/, std::string_view /*onStart*/,
                                 double /*players*/) override {}
    void menuLoadLevel(std::string_view /*level*/) override {}
    void playMovie(std::string_view /*name*/) override {}
    void playMusic(std::string_view /*track*/) override {}
    void stopMusic() override {}
    void queueScreenEffect(int /*type*/, double /*seconds*/) override {}
};

/// The Rumble menu over the synthetic level table and chunks (a test may replace a chunk in `files` and start the
/// menu again), its Game Type strings invented, playing an input script from frame 0.
struct RumbleMenuHarness {
    graphics::Font font = testFont();
    graphics::SpriteBatch textBatch{font.sheet(), 4096, 9000.0F};
    gui::GuiCanvas canvas;
    GameState state;
    gui::GlobalStrings strings;
    gui::RumbleData data;
    QuietRumbleHost host;
    script::RecordedCalls recorded;
    script::BindingContext context{&state, &strings, &host, &recorded, nullptr, nullptr, &data};
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::vector<std::string> chunksRun;
    script::ScriptSystem scripts{
        [this](std::string_view name) -> std::expected<std::vector<std::byte>, Error> {
            const auto found = files.find(name);
            if (found == files.end()) {
                return fail(ErrorCode::NotFound, "no such script");
            }
            return found->second;
        },
        [this](script::ScriptSystem& system, script::LuaVm& vm) { script::installBindings(system, vm, context); },
        {}};
    std::unique_ptr<gui::RumbleMenu> menu;
    std::unique_ptr<ScriptedInput> input;
    Pads pads;
    std::uint64_t frame = 0;
    gui::RumbleMenuResult last = gui::RumbleMenuResult::Stay;

    /// The menu playing `script`, started at frame 0.
    explicit RumbleMenuHarness(std::string_view script) {
        canvas.fonts = [this](int /*slot*/) { return &font; };
        canvas.textBatch = [this](int /*slot*/) { return &textBatch; };
        addRumbleChunks(files);
        files["config_preload3.lua"] = rumbleLevelTableChunk();
        strings.set(gui::RumbleMenu::kOnePlayerString, "ALONE");
        strings.set(gui::RumbleMenu::kCoopString, "TOGETHER");
        strings.set(gui::RumbleMenu::kVersusString, "AGAINST");
        strings.set(gui::RumbleMenu::kPlayerTwoString, "SECOND PAD");
        scripts.create();
        scripts.runFile("config_preload3.lua");
        menu = std::make_unique<gui::RumbleMenu>(gui::RumbleMenuServices{
            .state = &state,
            .data = &data,
            .strings = &strings,
            .runChunk =
                [this](std::string_view chunk) {
                    chunksRun.emplace_back(chunk);
                    scripts.runFile(chunk);
                },
        });
        input = std::make_unique<ScriptedInput>(parseInputScript(script).value());
        menu->start(msOf(frame));
    }

    /// The game time of frame `n` on the 1/30 s step, in milliseconds.
    static std::uint64_t msOf(std::uint64_t n) { return n * 100 / 3; }

    /// Runs frames up to and including `lastFrame`; stops early at a frame that ends the menu, keeping its result.
    gui::RumbleMenuResult to(std::uint64_t lastFrame) {
        while (frame <= lastFrame) {
            pads.update(input->sample(frame));
            textBatch.clear();
            const gui::GuiFrame guiFrame{.timeMs = msOf(frame), .pad = &pads.port(0)};
            last = menu->update(guiFrame, pads.connectedCount(), &pads.port(1));
            menu->render(canvas);
            ++frame;
            if (last != gui::RumbleMenuResult::Stay) {
                break;
            }
        }
        return last;
    }

    /// The set-up the menu writes.
    [[nodiscard]] const RumbleSetup& setup() const { return state.rumble; }
};

} // namespace coney::test
