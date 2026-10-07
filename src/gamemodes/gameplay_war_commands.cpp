// SPDX-License-Identifier: GPL-3.0-or-later
// The gameplay mode's Warrior command menu (docs/research/hud.md#warrior-command-menu): R2 held opens it for player
// 1, the right stick picks a slot, R2's release gives the order through the dispatcher, and the camera's right stick
// is off while it is up.
#include <cstdint>
#include <optional>

#include "camera/cameras.h"
#include "camera/follow_camera.h"
#include "core/pad.h"
#include "core/pads.h"
#include "gamemodes/gameplay_mode.h"
#include "hud/hint_box.h"
#include "hud/hud.h"
#include "scripting/story_bindings.h"

namespace coney {

void GameplayMode::updateWarCommandMenu(const Pads& pads, std::uint64_t nowMs) {
    const HumanCreation* player = m_humans.player(1);
    if (m_context.hud == nullptr || player == nullptr) {
        return;
    }
    hud::Hud& hud = *m_context.hud;
    hud::WarCommandDisplay& display = hud.warCommands(0);
    const Pad& pad = pads.port(0);
    // What the menu reads of its chief. **Coney's reading**: player 1 is the war chief (the human's `+0x3ac` is not
    // kept); his state flags (`0x80040000`, `0x20000`) do not hold the menu back.
    hud::WarCommandDisplay::Chief chief;
    chief.allowed = m_state.story.commandDisplay.at(0);
    chief.menuLocked = m_state.story.menuLocked.at(0);
    if (hud.services().commandString) {
        chief.nameDisplayMs = hud::markupTimesOf(hud.services().commandString(display.highlight())).displayMs;
    }
    // Release is tested before hold (Player_UseItemCommand); an order goes to the dispatcher unforced.
    if (pad.released(pad::kR2) && !chief.menuLocked) {
        if (const std::optional<int> command = display.issue(); command) {
            script::giveWarriorCommand(m_scripts, m_context, player->handle, *command);
        }
    } else if (pad.held(pad::kR2) && !chief.menuLocked) {
        display.open(chief, nowMs);
    }
    // A HUD that is not drawn gives the open menu's order at once (HUD_Render's hidden branch).
    if (!hud.visible() && display.shown() && !display.issued()) {
        if (const std::optional<int> command = display.issue(); command) {
            script::giveWarriorCommand(m_scripts, m_context, player->handle, *command);
        }
    }
    // The right stick's raw bytes (right x, right y) steer it.
    const auto& sticks = pad.rawSticks();
    display.update(sticks.at(0), sticks.at(1), chief, nowMs, hud.services().sound);
    if (m_cameras != nullptr && m_cameras->follow() != nullptr) {
        m_cameras->follow()->enablePadStick(display.cameraStickOn());
    }
}

} // namespace coney
