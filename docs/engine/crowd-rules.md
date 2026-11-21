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
and an update hands out at most 5 scans across all brains; one refused asks again on the next update ([The enemy
scan](../research/ai-fights.md#enemy-scan)). The original's 60 brains ask for about one scan an update, so its five
never run out. A crowd of 1000 asks for about 17, and five tokens starve it: 512 of 1000 brains were refused every step
in the performance sweep ([#1542](issue:1542)), the brains early in the table were served first and the rest of the
crowd stood unaware.

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
  in the limits sweep).

## Planned lifts {#planned}

The maintainer's limits-sweep decisions set these, each for a pull request of its own:

- **32 gangs and the scripts' 60 humans** (`ai::kGangSlots`, `CreatedHumans::kCapacity`): identical inside the range
  and lifted above it by count, for the story and mods alike. The level scripts call `GangCreate` and `HuCreate` far
  more often than the caps over a level (up to 85 and 215 times), so the lift waits for a count of each level's peak
  of live gangs and humans that shows no shipped level reaches the cap. The gangs' enemy and friend sets are 32-bit
  masks today and grow with them.
- **32 routes** (`ai::kRoutePoolSize`): scaled with the crowd, 32 per 60 humans, so that 32 stays the pool up to 60.

The original's 30 push spheres need nothing: Coney gives each human its own, so any number push at once.

## Measured {#measured}

The scan rule was measured on the performance sweep's bench ([#1542](issue:1542)): `level95`, 500 enemies and 500 allies
in gangs of 100 (the sweep's measurement switch, since 32 gangs of 10 hold only 320: [#1566](issue:1566)), rows of 25,
headless, lockstep, the main thread's CPU time over 300 steps after 450 of warm-up, a `playtest` build, best of two runs
(the machine 66 to 100 % busy):

| Engaged 1000 | 5 tokens (the original's) | 5 per 60 brains, no cap (the sweep's scenario) | at most 12 | **at most 20 (Coney's rule)** | at most 30 |
| --- | --- | --- | --- | --- | --- |
| aware / with a target / with a Fight goal | 396 / 300 / 128 | 920 / 680 / 332 | 984 / 865 / 353 | 998 / 857 / 355 | 983 / 867 / 348 |
| step, median (ms) | 112.5 | 130.2 | 163.0 | 157.0 | 157.5 |
| step, mean | 112.3 | 148.3 | 163.9 | 151.8 | 161.1 |
| step, worst twentieth (95th percentile) | 118.8 | 262.6 | 181.2 | 181.6 | 197.7 |
| step, 99th percentile | 120.6 | 345.0 | 191.6 | 200.6 | 205.1 |
| the brains' share, median / mean | 11.5 / 11.8 | 12.8 / 31.9 | 32.7 / 33.4 | 34.1 / 32.8 | 28.3 / 33.7 |

- The original's five tokens are cheap because most of the crowd never notices the fight: 396 of 1000 aware.
- Scaled with no cap, the scans come in bursts: the worst twentieth of steps is twice the median (263 against 130 ms).
  Capped at 20 it is 1.16 times the median (182 against 157 ms) and the 99th percentile falls from 345 to 201 ms, with
  more of the crowd engaged (857 with a target against 680).
- The cap of 20 has the lowest mean of the three caps and the worst twentieth of the cap of 12.
- The median rises, against the sweep's figure (19 to 14 ms with the cap), because these builds lack the sweep's other
  experiments: on `main` a scan still casts a sight ray at every member of every enemy gang (up to 500), about 1.7 ms a
  scan, until the scan casts them nearest first ([#1555](issue:1555)); twenty scans in every step are then the brains'
  steady 33 ms, where the scaled rule spends the same mean in bursts. With cheap scans the sweep measured the cap at 20
  against the scaled rule: the worst twentieth from 48 to 23 ms, the mean from 22.4 to 14.6 ms.

Up to 60 humans nothing changes. The all-humans trace, the event log and the lockstep digest hash the same on `main`
and with these rules for a brawl of 22 + 22 fighters (58 humans with the level's own, 500 steps, headless and in a
window), the same in gangs of 100 (700 steps) and 46 neutrals (60 humans). The sweep's identity scenarios are past
60 (the level's 14 humans are added to the bench's 300), so they change by design: brawl 300 (500 steps) and engaged
300 (700 steps) give other hashes, with more events logged as more of the crowd fights (6,590 to 8,852 and 8,030 to
12,181).
