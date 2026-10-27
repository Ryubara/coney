---
kind: feature
owns:
  functions: ["0x0010b6e0", "0x0010b858", "0x001047d0", "0x00176d60", "0x00223440", "0x002234f8", "0x00223548",
              "0x002235a0", "0x00259368", "0x0028bbf0"]
---
# The face: talk states and face clips

Verified against: `SLUS_212.15` (NTSC-U); runtime claims cite run records.

## Summary

In play a character's mouth opens and closes while he says a line, and a face at rest blinks and frowns. Every human
whose face shows plays a looping **face clip** on its character instance's queue, under the body's clips: anim ids
698-721 (`ANIM_FACE_*`), three to each of eight **talk states**, four moods each with a speaking state. A line that
starts playing turns the state to its speaking half and its end turns it back; the brain sets the mood every fifth
update. The face clip poses the face bones of the default pose, which every bone the body's clip leaves out takes, so
a body clip with face channels of its own (an attack's shout) wins while it plays.

Not covered here: the scenes, whose role clips carry their own face channels frame by frame (the cutscenes' lip-sync
is baked into them, [Scenes](scenes.md)); the clip format, the cursor and the fades ([Animation (character
clips)](formats/animation.md)); the speech lines themselves ([Sound](sound.md#speech)).

## Rules

- The talk state is 0-7: an even state is a mood (0 angry, 2 neutral, 4 scared, 6 excited), the odd one above it
  the same mood speaking. State `s` plays a random one of ids `698 + 3s` to `700 + 3s`, one after another.
- A new human starts at state 0 with no face clip. A mood set while a line plays keeps the speaking half.
- A change of state starts the new clips at once while the old ones fade out over 1/30 s; into a speaking state there
  is no fade.
- Only a face that shows changes or is drawn: a human in no scene, not down or dead, that is an AI Warrior or within
  17.5 m of the nearest camera.
- Of the face clip's pose, only bones 6-14 (the head to the eyelid) reach the default pose; the upper lip (15) stays
  at the reference pose.

## Map

| Behaviour | Function | Section | Evidence |
| --- | --- | --- | --- |
| The record cleared: state 0, no face clip | `0x00218008` `Human_Init` | [The talk state](#talk-state) | confirmed (code) |
| Set the mood | `0x002235a0` `Human_SetTalkState` | [The talk state](#talk-state) | confirmed (code) |
| Begin and end the speaking half | `0x002234f8` `Human_BeginTalkPose`, `0x00223548` `Human_EndTalkPose` | [The talk state](#talk-state) | confirmed (code) |
| A line's stream starts and ends | `0x0021e940` `Human_UpdateSpeech`, `0x0021ec38` `Human_StopSpeech` | [The talk state](#talk-state) | confirmed (code) |
| The mood from the brain | `0x0028bbf0` `Brain_UpdateAlertness` | [The mood](#mood) | confirmed (code) |
| Queue the state's face clips | `0x00259368` `Human_UpdateFaceClip` | [The face clips](#face-clips) | confirmed (code) |
| The face task: make, loop | `0x0010b608` `FaceTask_Construct`, `0x0010b6e0` `FaceTask_Update` | [The face clips](#face-clips) | confirmed (code) |
| The face fade: make, run | `0x0010b7c8` `FaceBlendOut_Construct`, `0x0010b858` `FaceBlendOut_Update` | [The face clips](#face-clips) | confirmed (code) |
| Whose face shows | `0x00223440` `Human_UsesQueuedPose` | [Where the face shows](#shown) | confirmed (code) |
| Sample the face into the default pose | `0x00176d60` `CharacterInstance_Sample`, `0x001047d0` `Instance_CopyRootSlots` | [Where the face shows](#shown) | confirmed (code) |

Entry points: `Human_UpdateFaceClip` is called early in `Human_UpdateActions`; `Brain_UpdateAlertness` first thing in
`PlayerBrain_Update` (`0x003035d8`) and early in `Brain_UpdateGoals` (`0x0028fbb0`); `CharacterInstance_Sample`
when a character is drawn. Confirmed (code).

| Field | Meaning | Evidence |
| --- | --- | --- |
| record (human `+0xd4`) `+0x1c` | the talk state, 0-7 | confirmed (code) at `0x002235a0`; confirmed (runtime), run r-20261026-face-talk-state |
| record `+0x24` | the first id of the face clips queued, -1 for none | confirmed (code) at `0x00259368`; confirmed (runtime), run r-20261026-face-talk-state |
| human `+0x170` | 1 while a line's stream plays and its speaking half is begun | confirmed (code) at `0x0021e940` |
| human `+0x334` | the distance to the nearest camera, written by `Human_UpdateLod` (`0x0023d660`) | confirmed (code) |
| `0x0050cc60` | the humans' draw distance, 70 m; a quarter of it is the face's 17.5 m | confirmed (code) at `0x00223440` |
| `0x005106d0` | a switch the shown test reads; 1 in the executable and written by nothing | confirmed (code) |
| `0x005fd040` | the face's default pose buffer, 34 rotations, shared by every human | confirmed (code) at `0x00176d60` |
| `0x006eb870` | the random stream the face clips draw from | confirmed (code) at `0x0010b6e0` |

## Behaviour

### The talk state {#talk-state}

Confirmed (code) at the cited addresses unless a line says otherwise.

| State | Mood | Face clips |
| --- | --- | --- |
| 0, 1 | angry | 698-700, speaking 701-703 |
| 2, 3 | neutral | 704-706, speaking 707-709 |
| 4, 5 | scared | 710-712, speaking 713-715 |
| 6, 7 | excited | 716-718, speaking 719-721 |

- `Human_Init` (`0x00218008`) clears the human's record, so the state starts at 0, and sets `+0x24` to -1.
- `Human_SetTalkState` (`0x002235a0`) writes the mood it is given; while the state is odd it writes the mood plus one.
- `Human_BeginTalkPose` (`0x002234f8`) adds one to an even state; `Human_EndTalkPose` (`0x00223548`) takes one from
  an odd state. Neither touches a state of the other kind.
- `Human_UpdateSpeech` (`0x0021e940`), each update, begins the pose when the line's prepared stream starts playing and
  marks `+0x170`; when the stream is gone it clears the mark and ends the pose. `Human_StopSpeech` (`0x0021ec38`) ends
  it when it stops a line. Every line a human says goes through these: `HuSpeak`, `HuSpeakNI`, the speech commands and
  the vocal ids.

Running (level99): the humans near the camera were at state 2 with clip 704 and a bum at state 6 with 716; a
`HuSpeak` line turned PoizoCiv's and Rembrandt's state to 3 and clip to 707 while it played, and both back to 2 and
704 after it. Confirmed (runtime), run r-20261026-face-talk-state.

### The mood {#mood}

Confirmed (code) at the cited addresses.

`Brain_UpdateAlertness` (`0x0028bbf0`) sets the mood through `Human_SetTalkState` when the brain's update count
(`+0x34`) divides by 5. `Brain_Update` (`0x0028f8b8`) adds one to the count before it calls the brain's handler that
runs this test, so the count includes the current update: the mood is set on the 5th, 10th, 15th update and so on, and
every other check the handler makes on the count (the goals' periodic ones) reads it the same way. The player's brain
calls it first thing; an AI brain calls it after its move is stopped and before the test that skips a suspended brain's
goals. In order:

1. **Scared** when the human is down or dead (`Human_IsDownOrDead`, `0x00227e60`), cuffed (`Human_IsCuffed`,
   `0x00223b70`) or burning (`Human_IsOnFire`, `0x00227fb8`).
2. With a **gang tactic** (the brain's gang `+0x20c`, its tactic `+0x40`): **angry** when the tactic alerts the gang
   (its vtable `+0x3c`); else by the tactic's type (vtable `+0x34`): scared under 0x13, 0x20 and 0x25 unless a pad
   drives the human (the per-player record's `+0x1b`, then neutral), excited under 0x18, 0x1b and 0x24, angry under
   0x14, 0x15, 0x1c-0x1e, 0x22, 0x23 and 0x26, neutral otherwise.
3. Without one: **angry** with a fight goal (`Brain_HasFightGoal`) or a chase goal (`Brain_HasChaseGoal`,
   `0x002903b0`: goal 0x0c or 0x3f) on the stack, else **neutral**; then the top goal's type may decide: excited for
   0x4f and 0x50 (the bums), 0x54, 0x62, 0x63, 0x71 and 0x80 (the dealer); angry for 0x09, 0x4a, 0x52, 0x72, 0x7c,
   0x8b and 0x98; scared for 0x02, 0x07, 0x11, 0x20, 0x49, 0x6b (a fleeing pedestrian), 0x6d, 0x6e and 0x79.

### The face clips {#face-clips}

Confirmed (code) at the cited addresses.

`Human_UpdateFaceClip` (`0x00259368`) does nothing unless the face shows ([below](#shown)) and `698 + 3 × state`
differs from `+0x24`. Then it makes a type-15 face task (`FaceTask_Construct`, `0x0010b608`) of ids `698 + 3s` to
`700 + 3s`, starting on the first at that id's playback rate, and writes the first id to `+0x24`. The first time
(`+0x24` -1) it appends the task to the instance's queue, and that is all. On a change it puts the new task first on
the queue and appends a type-16 fade (`FaceBlendOut_Construct`, `0x0010b7c8`) of the old one, over 1/30 s, or 0 when
the new state is odd, and never longer than the new first clip takes to play (its length over its rate,
`Anim_GetPlayTime`). It calls the old task's end slot (vtable `+0x7c`), which for a face task is a default that does
nothing, so the old task plays on under the fade.

- `FaceTask_Update` (`0x0010b6e0`) advances its clip at its rate. On the step the cursor says the clip has ended (its
  rounded frame, `uint(t × 30 + 0.5)`, reaches the frame count: `AnimCursor_Advance`, `0x001044a0`) it does not move
  the time; it binds a random id of its range (`Random_IntRange`, stream `0x006eb870`) from frame 0, keeping the rate
  the first id gave it, and runs its callback (the face has none).
- `FaceBlendOut_Update` (`0x0010b858`) adds the step to its time and gives back itself and the old task on the first
  update its time is past its length, or when the queue holds more than 4 tasks. Its sample weighs the old pose as a
  type-9 fade does, `(1 + cos(π t / d)) / 2` ([Fades](formats/animation.md#fades)).

### Where the face shows {#shown}

Confirmed (code) at the cited addresses unless a line says otherwise.

`Human_UsesQueuedPose` (`0x00223440`) says yes when the human has a character instance, the switch `0x005106d0` is
set, it is in no scene (`Human_IsInSceneState`, `0x00227d28`), it is not down or dead, and its brain is an AI Warrior
(type 3) or its distance to the nearest camera (`+0x334`) is under a quarter of the draw distance `0x0050cc60` (70 m):
17.5 m. The same test gates `Human_UpdateFaceClip`, so a face far off keeps the clips it last had until it comes near.
Running, a civilian 32.4 m from the camera had no face clip (`+0x24` -1): confirmed (runtime), run
r-20261026-face-talk-state. No Warrior that far off was read, so the Warrior half of the rule rests on the code.

`CharacterInstance_Sample` (`0x00176d60`), when the queue holds tasks and the test passes, makes `0x005fd040` the
instance's default pose, samples the queue's tasks onto the pose stack over it, and has `Instance_CopyRootSlots`
(`0x001047d0`) copy that sample's rotations of bones 6-14 into the buffer (nine 16-byte rows from the sample's
`+0x80` to the buffer's `+0x60`, the loop running while the destination is under `+0xf0`; the sample starts with two
translations, the buffer does not). The body's tasks are then sampled over that default. So the head, brow, jaw, lower
lip, mouth corners and eyelid take the face clip wherever the body's clip has no channel, and the upper lip stays at
the [reference pose](formats/animation.md#reference-pose). When the test fails the default is the reference pose itself
(`0x00598420`).

### The face bones {#face-bones}

Pose bones 7-15 are children of the head (6). The original's bone name table at `0x00546480` (34 fixed-width names;
Ghidra's string list misses the short `bip_jaw`) names them the left brow (7), the jaw (8), the lower lip (9), the left
mouth corner (10) and its pivot (11), the right mouth corner (12) and its pivot (13), the eyelid (14) and the upper lip
(15): confirmed (code) for the table. That its order is the pose bones' is inferred from the parent table
(`Skeleton_InitParents`, `0x00101120`: 9 under 8, 11 under 10, 13 under 12, the rest under 6).

## In Coney

- `repo:src/human/face.h`: `FaceState` is the talk state (`setMood`, `beginTalk`, `endTalk`); `FaceLayer` the face
  task and its fade (`update`, `advance`, `defaults`).
- `repo:src/human/human.h`: `Human::pose` samples the body over the face's defaults while `faceShown()`;
  `Human::setSpeaking` begins and ends the speaking half; `updateActions` updates the face first and `animate`
  advances it. A new `Human` starts with the face cleared, as `Human_Init` leaves it.
- `repo:src/human/face.h` `FaceRules`, which `repo:src/human/humans.h` owns and gives every human it adds: the face's
  random stream over the game's random table, and the shown rule, which the play mode sets
  (`repo:src/platform/play_level_mode.cpp`): no human a scene holds, an AI Warrior, or within 17.5 m of player 1's
  camera.
- `repo:src/ai/face_mood.h`: `Brain_UpdateAlertness`, called by `Brain::update` every fifth update.
- `repo:src/gamemodes/gameplay_mode.cpp`: each step hands every human whether a line of his is alive in the sound.

Stand-ins and choices:

- Nobody burns in Coney yet, so the mood's burning test is false ([q-burning](#q-burning)).
- Coney measures the 17.5 m to player 1's camera; the original to the nearest camera (one in Coney's single player).
- The original's buffer `0x005fd040` is shared by every human, so a bone of 6-14 a face clip has no channel for keeps
  the last human's value; Coney gives it the reference pose ([q-shared-buffer](#q-shared-buffer)).
- The rest of `CharacterInstance_Sample` is not ported: the head look and torso twist within 30 m and the flag it
  gives the bone matrices from the distance (#963).
- The speaking half begins when Coney's speech line is alive, which has no wait for a stream to be prepared.

Tested by `repo:tests/human/face_test.cpp`, `repo:tests/ai/face_mood_test.cpp` and, on the disc,
`repo:tests/platform/disc_face_test.cpp` (2026-10-26, #926).

## Open questions

### Burning {#q-burning}

Coney has no burning humans yet; when it does, the mood's test reads its flag (`Human_IsOnFire`, state flag
`0x4000`). Until then the mood's burning test is a stand-in that is always false.

### The shared default buffer {#q-shared-buffer}

Some face clips do leave bones out. Counted on the disc (a local probe of the generic face clips): the left mouth
corner's pivot (11) has a channel only in 698, 706 and 716-718, and the right one (13) has none in 699, 700, 704, 705,
710-712 and 716-718. `AnimTaskList_Create` (`0x001048a0`) fills the buffer `0x005fd040` with the reference pose once, at
start (confirmed (code)), and `CharacterInstance_Sample` samples the face over that same buffer, so in the original
those bones keep the last value any human's face clip wrote there: inferred. Those clips turn the two pivots by at most
7.4 and 12.1 degrees. Whether Coney should keep one value for every human, as the original does, is open. Until then
Coney gives such a bone the reference pose.

## How this was checked

The runtime claims: PCSX2, level99 from the state library's `pedestrian-grab` state with the `call-brains` patch;
every human's `+0x1c`, `+0x24` and `+0x334` read over PINE, then `HuSpeak` run through the `lua` recipe on PoizoCiv
and on Rembrandt and the same fields read about once a second (run r-20261026-face-talk-state, 2026-10-26).
