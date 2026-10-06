// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"

namespace coney::script {

/// The AI bindings Coney implements: the goals and actions the level scripts give a human's brain. All real;
/// installBindings() registers them.
inline constexpr std::array<std::string_view, 2> kAiBindings{"ActLookAt", "GoalMoveToFlag"};

/// `GoalMoveToFlag(human, flag, gait, angle, distance, radius, intervalMs, faceFlag, option)` as the binding reads it
/// (docs/references/bindings/ai.md#goalmovetoflag).
struct MoveToFlagCall {
    double human = 0;             ///< The human's handle.
    double flag = 0;              ///< The flag's handle.
    int gait = 0;                 ///< Truncated.
    float angle = 0.0F;           ///< Degrees.
    float distance = 0.0F;        ///< Metres.
    float radius = 0.0F;          ///< Metres.
    std::uint32_t intervalMs = 0; ///< Truncated to an unsigned integer.
    bool faceFlag = false;        ///< nil and 0 are false.
    bool option = false;          ///< nil and 0 are false.
};

/// `ActLookAt(human, target, turnSpeed, timeMs)` as the binding reads it (docs/references/bindings/ai.md#actlookat).
struct LookAtCall {
    double human = 0;          ///< The human's handle.
    double target = 0;         ///< The handle of what it turns to.
    float turn = 0.0F;         ///< The turn value.
    std::int16_t delayMs = -1; ///< The start delay; -1 (the default) is a random 0-500 ms.
};

/// What the AI bindings ask of the game: the brains of the humans the scripts name by handle. A handle that names no
/// brain is the host's to ignore, as the original's wrappers do with a handle that is not a human.
class AiBindingHost {
  public:
    AiBindingHost() = default;
    AiBindingHost(const AiBindingHost&) = delete;
    AiBindingHost& operator=(const AiBindingHost&) = delete;
    AiBindingHost(AiBindingHost&&) = delete;
    AiBindingHost& operator=(AiBindingHost&&) = delete;
    virtual ~AiBindingHost() = default;

    /// Gives the human the move-to-flag goal.
    virtual void goalMoveToFlag(const MoveToFlagCall& call) = 0;
    /// Queues the look-at turn action on the human.
    virtual void actLookAt(const LookAtCall& call) = 0;
};

/// Registers kAiBindings in `vm`, handing each call to `context.ai` (a null one does nothing). They return nothing.
///
/// Research: docs/research/ai.md#scripted, docs/references/bindings/ai.md
void addAiBindings(LuaVm& vm, const BindingContext& context);

} // namespace coney::script
