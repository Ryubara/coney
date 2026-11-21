# Crowd rules

What Coney does when a level holds more humans than the original allows. The original was built for 60 human slots,
and every table and budget of its AI and triggers is sized for them: 5 enemy scans an update, lists of 60 humans, 32
gangs of 10, 32 routes. It defines nothing above them. Coney is not bound by those numbers past their range
([The original's fixed limits](../guides/conventions.md#original-limits)): **up to 60 humans every rule below gives
the original's value**, so a level plays as on the PS2 with the same hashes, and **past them each rule is Coney's own**,
chosen by measurement for the best and evenest frames. Each rule is read through one function of
`repo:src/core/crowd_limits.h`, marked `@coney-only limit:`, so the engine-limits test preset the conventions plan can
pin it at the original's number.

## The rules {#rules}

| Limit | The original | Coney up to 60 humans | Coney past 60 humans | Where |
| --- | --- | --- | --- | --- |
| Enemy scans an update | 5 tokens, a refused brain asks again the next update ([The enemy scan](../research/ai-fights.md#enemy-scan)) | 5 | 5 per 60 scanning brains, rounded up, at most 20; a refused brain asks again the next update | `crowd::scanTokens()`, read by `Gangs::update()` |
| Humans round a walker (steering) | at most 60, the table's size ([Help calls](../research/ai-brain-types.md#help-calls), `Humans_FindAhead` `0x002274a8`) | 60, which cannot bind | every human in reach, in table order | `crowd::humanListCap()`, read by `ai::humansAround()` |
| Hearers of a help call | at most 60 ([Help calls](../research/ai-brain-types.md#help-calls)) | 60, which cannot bind | every human in reach | `crowd::humanListCap()`, read by `ai::broadcastHelpCall()` |
| A trigger box's or sphere's occupants, and the humans a sphere finds | 60 handles; 60 found in slot order ([Who is found](../research/trigger-updates.md#found)) | 60 | as many as there are humans | `crowd::humanListCap()`, read by `VolumeBoxes` and `TriggerSpheres` |
| Enemies on a brain's list | 16, the nearest ([The enemy scan](../research/ai-fights.md#enemy-scan)) | 16 | 16 | kept: `ai::kScanMaxEnemies` |
| A gang's members | 10, 16 for the police kinds; a full gang drops its first member ([Gangs](../research/ai-gangs.md)) | 10 / 16 | 10 / 16; a crowd gets more gangs instead (planned) | kept: `ai::kGangMembers` |
| Gangs | 32 records | 32 | planned: lifted by count | `ai::kGangSlots` |
| Humans a level's scripts create | 60 (`HuCreate` returns nil past them) | 60 | planned: lifted by count | `CreatedHumans::kCapacity` |
| Routes | a pool of 32 | 32 | planned: 32 per 60 humans | `ai::kRoutePoolSize` |
| Push spheres | a pool of 30 | none: each human has its own | none | - |

## The enemy scans {#scans}

Every brain in a gang scans for enemies on its own interval (2 s, a cop's 1 s, four times as long in the fight stance),
and an update hands out at most 5 scans across all brains; one refused asks again on the next update
([The enemy scan](../research/ai-fights.md#enemy-scan)). The original's 60 brains ask for about one scan an update, so
its five never run out. A crowd of 1000 asks for about 17, and five tokens starve it: 512 of 1000 brains were
refused every step in the performance sweep (#1542), the brains early in the table were served first and the rest of
the crowd stood unaware.

Coney's rule: **5 tokens an update up to 60 scanning brains; past them 5 per 60 brains, rounded up, and never more
than 20.** A scanning brain is a member of a gang, the only brains that ask (`Gangs::scanningBrains()`, counted at the
start of each gang update). A refused brain asks again on the next update, as in the original.

- **Why 5 per 60.** It keeps the original's headroom (five tokens for its sixty humans) for a crowd just past the
  range, so 61 brains get 6 tokens, not a jump to a new rule.
- **Why at most 20.** Brains created together are due together. With the tokens scaled and no cap, every brain of a
  freshly spawned crowd of 1000 scans in the same step every interval: the sweep measured 3 scans in a median step and
  67 in the worst twentieth, and those steps took 48 ms against a median of 19 ms. A cap with the retry spreads such a
  burst over the next updates. Twenty is just above what a calm crowd of 1000 asks on average (1000 brains over the
  60 updates of a 2 s interval, about 17 an update), so a calm crowd keeps its interval and only a burst waits.
  The sweep measured the cap at 12 and 20 against the scaled rule on the same crowd: as many humans aware, targeting
  and fighting (914 aware, 666 with a target, 318 with a Fight goal at 20, against 924, 697 and 322 scaled) and the
  worst twentieth of steps down from 48 ms to 23 ms. The measurement of this branch is [below](#measured).
- **Inside the range nothing changes**: with 60 brains or fewer the rule gives 5, the original's number, so every
  level and playthrough keeps its hashes.

The original also scans four times as slowly for a human far from the camera (its detail level, `Human_UpdateLod`
`0x0023d660`). Coney leaves that out (`ScanSchedule`'s stand-in): it ties the simulation to a camera, which split screen
and network play cannot have. A detail level that Coney builds may change only what is drawn.

## The lists of humans {#lists}

Four lists in the original hold at most 60 humans, because `Humans_FindAhead` walks the 60 slots and is given a buffer
of 60: the humans round a walker that steering tests, the hearers of a help call, and a trigger box's or sphere's
occupants with the humans a sphere finds ([Who is found](../research/trigger-updates.md#found)). With 60 humans or
fewer none of them can fill (the caller is left out of the first two), so the 60 is the table's size, not a rule of
the game.

Past 60 humans a list cut at 60 in table order goes wrong: a walker ignores the nearer human in a later slot and walks
into him, a help call reaches the first 60 by slot rather than all in reach, and a box with 60 listed does not list the
next human inside, so he "enters" again on every update of the box and its script hears message 3 over and over.
Coney's rule: **a list holds 60 up to 60 humans, and as many as there are humans past them**
(`crowd::humanListCap()`): no list is cut. The order stays the table's and the subjects', as the messages go out in it.

## Kept inside the range {#kept}

Two limits act inside the original's range, so they stay at the original's number at every crowd size:

- **16 enemies on a brain's list.** The scan keeps the nearest 16 it sees, and which members fall off the list decides
  when the brain forgets them. A brain of a crowd fights one enemy at a time, so the 16 nearest are enough past the
  range too.
- **A gang's 10 members** (16 for the police kinds). A full gang drops its first member when another joins, which
  the level scripts see. A crowd is carried by more gangs of at most 16, not by larger gangs (the maintainer's decision
  of 2026-11-21 in the limits sweep).

## Planned lifts {#planned}

The maintainer's limits-sweep decisions of 2026-11-21 set these, each for a pull request of its own:

- **32 gangs and the scripts' 60 humans** (`ai::kGangSlots`, `CreatedHumans::kCapacity`): identical inside the range
  and lifted above it by count, for the story and mods alike. The level scripts call `GangCreate` and `HuCreate` far
  more often than the caps over a level (up to 85 and 215 times), so the lift waits for a count of each level's peak
  of live gangs and humans that shows no shipped level reaches the cap. The gangs' enemy and friend sets are 32-bit
  masks today and grow with them.
- **32 routes** (`ai::kRoutePoolSize`): scaled with the crowd, 32 per 60 humans, so that 32 stays the pool up to 60.

The original's 30 push spheres need nothing: Coney gives each human its own, so any number push at once.

## Measured {#measured}

To be filled with the branch's bench numbers.
