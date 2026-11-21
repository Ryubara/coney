# Coney Studio: hub chapters that mod missions open

How the hub plays a chapter that a mod's mission opens: a mission placed after hub chapter *n*
(`after = "hub:<n>"`, [Anchors](mission-mods.md#anchors)) is offered from a new chapter, `hub:<n>.<mod>.<id>`, that
shares chapter *n*'s checkpoint. This page is which chapter the hub plays, what that chapter offers, Coney's default
chapter, a mod's own `chapter` script, and what is built. It is part of [the story graph](mission-mods.md#story-graph)
of [Coney Studio: mission mods](mission-mods.md), and **Coney's own**: the original has no mods, and its hub always
plays the chapter its checkpoint names. What the original's hub scripts do, which this rests on, is
[Story progress: the hub](../../research/story-progress.md#hub).

**Status: built.** The rule, the plan and the change to the hub's scripts are built ([#1302](issue:1302)), and the
hub load calls them ([#1300](issue:1300)); Coney starts no mods yet, so in the game the hub plays the original
chapters.

## Contents {#contents}

1. [Which chapter the hub plays](#which-chapter).
2. [The default chapter](#default-chapter).
3. [A mod's chapter script](#chapter-script).
4. [What is built](#built).

## Which chapter the hub plays {#which-chapter}

A new chapter shares chapter *n*'s checkpoint, so the checkpoint alone cannot say which of the two the hub is in: the
story's progress does. The hub plays the chapter that `runNextMission` enters after the latest story mission
complete, when the hub was entered at that chapter's checkpoint; anywhere else (a jump to the hub, a checkpoint that
is not the story's place) it plays the original chapter. Chapter *n* offers the first mission placed after it, and
each new chapter offers the step after it: the next mod mission on the same chapter, or chapter *n*'s original
mission. A new chapter after chapter 11 or 12 offers nothing, as those chapters offer nothing in the original.

The offer goes into `fRunMission[n].level`, as the natives write it for every chapter
([The natives](mission-mods.md#graph-natives)); a mod's mission without a level number yet loads nothing there,
rather than the mission chapter *n* offered before.

## The default chapter {#default-chapter}

A chapter's file sets its crew, radio, chatter and idle animations when it runs, and leaves its actions, cast, scenes,
scene extras and side mission to functions and tables the hub reads afterwards, in `RunLevel`
([Story progress: the hub](../../research/story-progress.md#hub)). Coney's default chapter keeps chapter *n*'s file,
so the crew and the story's time are chapter *n*'s, and, before `RunLevel` runs:

- drops the chapter's cast (`CfgMissionHumans`), scenes (`CfgSceneTables`), scene extras (`SceneExtrasTable`) and
  side mission (`CfgMissionTable`);
- sets its actions (`CfgMissionActions`) to the shape of the original's chapter 11: a red circle at chapter *n*'s
  giver (`fRunMission[n].flag`), then the load of the new chapter's offer, with no cut-scene. As a chapter's own
  does, it removes itself once it has run, so a second clubhouse set-up keeps the actions it made.

## A mod's chapter script {#chapter-script}

A mod names a script for its new chapter with the mission's `chapter` key
([The manifest's `[story]` section](mission-mods.md#story-manifest)). It runs over the default chapter, after chapter
*n*'s file and before `RunLevel`, and may set any of the default's functions and tables itself: its own actions, a
cast, scenes.

A chapter script that is missing or fails does not stop the hub. Its error is logged and counted as any script's is,
the default's own functions and tables are put back in place of whatever the script had replaced before it stopped
(any other global the script set stays), and a further log line names the chapter and the script and says the default
chapter stays. The player still gets the offer, from
the default chapter's red circle.

## What is built {#built}

- `story::StoryGraph` (`repo:src/core/story_graph.h`) answers by step: `nextStep`, `offerOf` and
  `hubChapterAt`. The latest story mission complete is not asked of the graph: the hub load passes
  `latestMission(...)->id()` (`repo:src/warriors/mod_story_progress.h`, which walks both kinds of progress) to
  `planHubChapter`. `hubOffers()` keeps the 12 rows of the original chapters.
- `story::planHubChapter` (`repo:src/core/story_chapter.h`) gives the chapter the hub plays, its offer, its mod and its
  `chapter` script.
- `script::applyHubChapter` (`repo:src/scripting/hub_chapter.h`) applies a plan to the hub's script state after
  `level95.lua`'s main chunk.

- `script::enterHub` (`repo:src/scripting/story_natives.h`) is the hub load: after `level95.lua`'s main chunk it
  plans the chapter at the game state's checkpoint with the session's progress and applies the plan
  ([#1300](issue:1300)).
`repo:tests/core/story_chapter_test.cpp` walks a merged story through a new chapter at chapter 6's checkpoint to
`hub:11`; `repo:tests/scripting/hub_chapter_test.cpp` plays the default chapter, a mod's chapter script and a failing
one over a synthetic hub.
