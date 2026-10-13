# From research to code

This page is the hand-over between the two roles of the [clean room](how-we-work.md#two-roles-the-clean-room): what
an analyst puts on a research page so that an implementer can write the code from it alone, what the implementer
does with it, and three real features traced from the page to the merged result. The rules behind it are in
[Research workflow](research-workflow.md#clean-room) and [Testing](testing.md); this page shows them working.

## What the analyst prepares

The analyst writes a research page in `docs/research/`. Every claim on it carries an
[evidence level](research-workflow.md#evidence-levels) and a cited address. A page meant to be coded from contains:

- **What each function does, in prose.** Its inputs and outputs, and when it is called; its branches and
  conditions, in order; its edge cases; its constants, with units, such as "radius 0.75 m with a slot, 2.5 m
  without".
- **Data layouts.** Each structure's fields as offset, size, meaning and who reads or writes them, such as "human
  `+0xd0` is the character type".
- **Short snippets only.** A line or two of disassembly next to an address, never a whole function, so nobody can
  transcribe it ([Writing up a finding](research-workflow.md#writing-up-a-finding)).
- **Runtime numbers from the real game.** The analyst hooks addresses in PCSX2 and logs values per update
  ([Recording a trace](research-workflow.md#recording-a-trace)). The fence work's "the cut comes 15-44 updates
  after the hit" is one example. These become the implementer's test values.
- **A fix list.** Numbered, ordered units, each with "done" spelled out, so an implementer can take one at a time.

## What the implementer does with it

The implementer writes fresh code from the page and tags each function `@orig 0x<addr>` with a link to the page
([Conventions](conventions.md#comments-and-orig)). Tests are written first, from the page's numbers; that is how
"correctly" gets checked ([Testing](testing.md#done)). For whole behaviours we also run both games through the same
scenario and diff their traces ([Comparing with Coney](research-workflow.md#comparing-with-coney), and
[Diff against the original](testing.md#diff-against-the-original)); the trace tooling records the same columns from
each game and `coney-tools trace diff` reports the first update where they part.

If a page doesn't say enough, the implementer writes the question down and an analyst answers it; the implementer
never opens the disassembly
([When a research page isn't enough](research-workflow.md#when-a-research-page-isnt-enough)).
Where the code must go on without the answer, it uses a marked [stand-in](testing.md#stand-ins).

## Worked examples

Each example follows one feature from the page, through the code and its test, to the pull request that merged it.
All the code and tests named are in the repository; open them beside this page.

### The revive of a downed Warrior

**The page.** [Reviving a downed Warrior](../research/combat-rage.md#revive) describes the `Player_TryRevive` function
(`0x00286ba8`) as an ordered list of refusals, then the way test, then the flash. It gives:

- **A constant with its unit:** the way test sweeps a capsule of radius 0.2 m, as tall as the reviver's own.
- **Branches in order:** a downed AI human is refused when the way is not clear, and a downed player accepts only a
  Warrior as the reviver.
- **A rule about who pays:** at the end of clip 351 the downed human's flash is spent if he has one, else the
  reviver's.
- **Runtime numbers**, in [At runtime](../research/combat-rage.md#revive-runtime): clip 351 lasts 21 updates (0.70 s),
  353 lasts 33, 389 lasts 25 and 354 lasts 72; the reviver ends about 0.70 m from the downed human and 11-13 degrees
  to the left of the line to him.

**The code.** The rules are a small function with no game state, so they can be tested on their own:
[`mayRevive`](repo:src/combat/revive.h) is tagged `@orig 0x00286ba8 Player_TryRevive` and its header points at the
page. The clips, the flash and the heal are wired in [the game mode](repo:src/gamemodes/gameplay_revive.cpp), whose
declarations in [`gameplay_mode.h`](repo:src/gamemodes/gameplay_mode.h) carry the tags for `Revive_Start`
(`0x002752a0`) and `Revive_Finish` (`0x00274ee0`). The 0.2 m is
`kWayClearRadius` in [`way_clear.h`](repo:src/human/way_clear.h).

**The tests.** Written first, in layers. The branches of the page become
[`revive_test.cpp`](repo:tests/combat/revive_test.cpp): a case for the refusals, one for the flash, one for "the way is
not clear, so only a Warrior reviving a player goes on" and one for whose flash is spent. The runtime numbers become
the bounds of a disc playthrough,
[`disc_revive_test.cpp`](repo:tests/platform/disc_revive_test.cpp), which presses d-pad left beside a
downed Warrior in the real mission and checks that 351 lasts 20-22 updates, 353 lasts 32-34, 354 lasts 71-73, the
stand-off is 0.6-0.9 m, the steer ends 8-14 degrees off the line, the flash count goes from 1 to 0 and the health
is full. Each bound allows a margin of an update (or a few degrees) round the measured value, which is the centre.

**The result.** Pull request #169 added the revive. It also did what the process asks when the page is silent: the
crew Warriors' revivable flag was not set by anything in Coney, so the pull request said so and put "what sets that
flag in the original" on the page as a new open question, instead of guessing in code.

### The crew's steal command

**The page.** [WarriorSteal](../research/ai-code.md#t2-warrior-steal) lists each function of the tactic by address
with what it does. Among them: `WarriorStealTactic_PickStore` (`0x00321028`) keeps its store while the chief is
within 12 m, otherwise takes the nearest store flag of activity `0xe` within 12 m; `WarriorSteal_ClaimObject`
(`0x00321540`) claims an object for the Warrior's gang for 5 s unless another member stands within 1.5 m of it. The
page also lists how the store's objects are grouped (1.0 m clusters) and in what order they are walked, step by
step.

**The code.** [`steal_goals.h`](repo:src/ai/steal_goals.h) names the page's numbers as constants with their units
(`kStealStoreReach` is 12.0 m, `kStealClusterRadius` 1.0 m) and tags each function with its address, for example
`@orig 0x00321028 WarriorStealTactic_PickStore` on the store pick. The same file shows the gap rule: the page gives
the original's reachability test only by name, so Coney checks the distance alone, marks that as a **Coney
stand-in** in the comment, and [the page](../research/ai-code.md#t2-warrior-steal) lists each stand-in as an open
question beside the original behaviour.

**The tests.** [`steal_goals_test.cpp`](repo:tests/ai/steal_goals_test.cpp) builds a store with two groups of two
objects half a metre apart and eight metres from each other, and checks the page: the store front within 12 m is
taken, two clusters of two are found, every crew member but the chief gets the steal goal. A second case puts the
store front beyond 12 m and checks that nothing is handed out. A third lets two Warriors fetch four objects and
checks that both take part and that each stands within reach of what he takes. A fourth checks that a claimed object
is not handed out again until its 5 s run out.

**The result.** Pull request #168 made mission 2 (`level80`) get past its store checkpoint: the ten thefts the mission
script waits for now happen. It also left a short list of "Remaining" questions (message 7's receivers, clustering
details, the pick-up animation), each of them a request to an analyst rather than a hole filled with a guess.

### From a runtime trace to a fix list: the fence and the dealer

This one shows the analyst's half in detail, because its numbers were measured rather than read.

**The page.** In [Player 2, Vermin's runs and the fence](../research/scripting.md#level99-p2-vermin-fence), item 44,
the analyst hooked the fence's hit, its removal and the scripted cut to the dealer's camera in PCSX2 and ran the
scenario twice, putting the update number of each moment in a table. The result is a range, not a single figure: the
removal comes 0-29 updates after the breaking hit (the fence's 60-tick phase), the cut comes a fixed 14-15 updates
after the removal, so the hit-to-cut gap is **15-44 updates**, and nothing (no fade, no letterbox, no camera change)
happens in between. The scenario that produced it is checked in as
[`fence_dealer.toml`](repo:research/traces/scenarios/fence_dealer.toml), so anybody can run it again.

**The fix list.** The page ends with numbered fixes (`Coney fixes (item 44)`) in the order to take them, the last of
them a test with its pass condition spelled out: with the hit at update H, the fence is removed at an update between H
and H + 30, and the dealer camera is current 14 or 15 updates after that, so the cut lands within H + 14 ... H + 45.
An implementer takes that item as it stands: the test is the bounds, written first, and the fix is whatever makes it
pass.

**Why this is the model.** A report that said "the cut is about a second after the hit" could not be tested; a range
with a cause (the barrier's phase) and the exact moment it is measured from can. And when the first reading of the
timing was corrected by the second run (3-17 became 0-29), the page changed in place, so the test value an
implementer reads is always the current one.

## Writing the page so it can be coded from

A checklist for the analyst, before calling a page ready:

1. Could someone write the function without the disassembly? If a branch, a constant or a unit is missing, add it.
2. Is every number's unit stated, and is it clear whether a time is in updates, ticks or seconds?
3. For each structure field the code reads, is there an offset, a size and a meaning?
4. Is there a runtime number for every behaviour that has a duration, a distance or an order? Those are the tests.
5. Are the unknowns listed as open questions, so the implementer's stand-ins have a page to point at?
6. Are the fixes numbered, in order, each with how to tell it is done?

The page kinds and the skeleton are in [Writing these docs](writing-docs.md#subsystem-page).
