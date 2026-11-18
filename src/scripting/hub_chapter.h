// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/story_chapter.h"

// The hub chapters that mod missions open, in the hub's scripts: the chapter's offer written into `fRunMission`, and a
// new chapter's actions (the mod's `chapter` script, or Coney's default chapter) put in place of chapter n's. Coney's
// own: the original has no mods, and its hub always plays the chapter its checkpoint names.
//
// Research: docs/guides/studio/hub-chapters.md, docs/research/story-progress.md#hub

namespace coney::script {

class ScriptSystem;

/// Applies `plan` to the hub's script state, called once `level95.lua`'s main chunk has run (its `Main` has asked for
/// `level95_chapter<n>` with the callback `RunLevel`, and `fRunMission` exists):
///
/// - `fRunMission[checkpoint].level` becomes the plan's offer: its level number, or nil when it offers none (or the
///   offer is a mod mission not yet given a number). For an original chapter of the unmodded story that is the value
///   already there.
/// - For a new chapter, the global `RunLevel` is wrapped, so that once chapter n's script has run (its crew, chatter,
///   radio and idle animations: the chapter's time) and before its own `RunLevel` does, chapter n's actions are
///   replaced: its scene, cast, scene-extra and side-mission configuration go (no cut-scene), its actions become a
///   red circle at the chapter's giver flag (`fRunMission[checkpoint].flag`) then the mission load, the shape of the
///   original's chapter 11, and the mod's `chapter` script, when there is one, runs over that default and may replace
///   any of it.
/// - A mod's `chapter` script that is missing or fails does not stop the hub: its error is logged and counted as any
///   script's is (ScriptSystem::runFile()), the default's own functions and tables are put back in place of whatever
///   the script had replaced before it stopped (any other global the script set stays), and a further log line names
///   the chapter and says the default stays.
///
/// Does nothing without a script state, or when the state has no `fRunMission` row for the checkpoint.
/// @coney-only the hub chapters that mod missions open (docs/guides/studio/hub-chapters.md)
void applyHubChapter(ScriptSystem& scripts, const story::HubChapterPlan& plan);

} // namespace coney::script
