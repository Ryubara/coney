# Coney Director: the Rockstar Editor survey

What GTA V's Rockstar Editor does, for Coney Director to model: how the player records, what a recording holds, the
words it uses, the editor's features, what players praise and dislike, and briefly how a few other games do replays.
It ends with the words Director uses, the feature list of Director's first version and the open questions. The survey
is [#1185](issue:1185), under Director's parent issue [#1184](issue:1184); the recording format and the editor are
designed on the pages that follow it ([Coney Director](index.md#pages)).

**Status: survey and design input.** Nothing here is built. This is a design in the sense of [Page
kinds](../writing-docs.md#page-kinds): the survey says what the Rockstar Editor does, and the last three sections say
what Director takes from it.

**How this was gathered, and how sure it is.** From published material only: Rockstar's support pages, the press, the
fan wikis, player threads and the pages of mods that change the editor. No copy of GTA V was run for it, and no
screenshot of the editor is in this repository or on this page; its menus are described in our own words. Each fact
names its source in brackets, and the kind of source says how far to trust it:

| Kind | Trust |
| --- | --- |
| Rockstar | Rockstar's own support pages; the most reliable, but they say little beyond the controls |
| Press | Articles from the 2015 launch and later; reliable for what was shown, sometimes from a demonstration only |
| Wiki | The community wikis; detailed and mostly right, with no citations of their own |
| Players | Forum threads; good for what players meet and dislike, poor for exact numbers |
| Mod | The pages of mods that change the editor; their authors read the editor's code, so they are the best source on how it works inside, but they describe it to sell their change |

## Contents {#contents}

- [What it is and where it runs](#what-it-is)
- [Recording](#recording): the controls, Action Replay, clip length, what a clip stores
- [The words Rockstar uses](#words)
- [The editor](#editor): projects and the timeline, markers, cameras and target tracking, blends, speed, depth of
  field and effects, scrubbing, audio, text and transitions
- [Export](#export)
- [Limits and restrictions](#limits)
- [What players praise and dislike](#reception), and [what Coney can do better](#better)
- [Other games' replays](#other-games)
- [Sources](#sources)
- [The words Director uses](#terms), [Director's first version](#first-version), [Open questions](#questions)

## What it is and where it runs {#what-it-is}

The Rockstar Editor is GTA V's built-in recorder and video editor. The player records play in the game; the editor,
opened from the pause menu, plays the recordings back with a camera the player places, and turns them into a video. It
came with the PC release on 14 April 2015, and reached PlayStation 4 and Xbox One with the Freemode Events Update on
15 September 2015, together with **Director Mode** [Press: Trusted Reviews; Wiki: gta.wiki]. Director Mode is a
separate feature: the player casts characters, sets the weather, time and place, spawns vehicles and props, and plays
the scene to record it [Wiki: GTABase]. It does not edit recordings.

The editor opens only from Story Mode; clips recorded in GTA Online appear there too [Rockstar: How to record clips on
PC]. It needs the player to be free: no wanted level, not in a vehicle, a mission or a ragdoll [Wiki: gta.wiki].

On 20 February 2024 Rockstar removed the editor from the PlayStation 4 and Xbox One versions and deleted every clip
and project on them; players had to export what they wanted to keep first. Rockstar said this was needed to keep
updating the game on those consoles [Press: VGC]. The PC, PlayStation 5 and Xbox Series versions keep it.

GTA IV on PC had its predecessor, the **Video Editor**: F2 started and ended a recording of about 20 seconds to over a
minute, saved as a file of 30 to 105 MB; the editor set filters, speed, audio volumes and preset or free camera
angles at markers, added songs, text and fades, and rendered WMV at Web, 720p or 1080p sizes [Wiki: gta.wiki, Video
Editor].

## Recording {#recording}

### The controls {#controls}

Holding the character-switch control (Alt on a keyboard, down on the pad's d-pad) shows the recording choices; on PC,
F-keys also start a recording, save it, cancel it, and turn Action Replay on and off, and all of them can be rebound
under the key bindings [Rockstar: How to record clips on PC; Wiki: gta.wiki]. The guides disagree on which F-key does
which, so the defaults are left out here. While a recording runs, a counter above the radar shows its time
[Wiki: gta.wiki]; while Action Replay runs, a red dot blinks [Press: dor.gg].

There are two ways to record [Wiki: GTABase]:

- **Recording** starts when the player asks and runs until the player saves or cancels it.
- **Action Replay** keeps recording into a buffer in the background and saves nothing until the player asks; saving
  keeps the last stretch of play, so a crash or a stunt can be kept after it happened. A setting saves a clip by itself
  when the player dies [Players: Steam, the folders].

### Clip length {#clip-length}

A clip's length is not fixed: it depends on how much happens. The figure given most often is 30 to 90 seconds,
shorter when the scene is busy [Players: Steam, quoting a guide; Press: PC Games]; players report clips from a few
seconds in crowded places to about three minutes in the empty desert [Wiki: gta.wiki; Players: Steam]. When a long
recording fills a clip, the editor starts a new one by itself, losing about half a second between them [Wiki:
gta.wiki; Players: Steam]. The disc space for clips is capped by a setting (25 GB by default on PC), and raising it does
not lengthen a clip [Players: Steam].

### What a clip stores {#clip-data}

A clip is **replay data, not video**. It holds the state of the game over time: the positions and rotations of the
characters, vehicles and objects, the state of the world, the sounds started, the game camera and the player's actions
[Players: Steam]. It plays only inside the editor; a video file exists only after a project is exported [Press:
dor.gg]. Because a clip is the scene itself, the editor can show it from any angle, within limits
([Limits](#limits)) [Press: PC Games].

The length rule follows from this. On PC the game's settings file sizes the recorder as a fixed number of fixed-size
blocks (one player's file shows 36 blocks of 9,000,000 bytes, about 324 MB) [Players: GitHub gist, Steam]: the buffer
holds a fixed number of bytes, a busy scene writes more bytes a second, so the same buffer holds fewer seconds
(inferred from the settings and the reported lengths; Rockstar does not say so). GTA IV's clips, at 30 to 105 MB for
under two minutes, give the order of size: one or two megabytes a second [Wiki: gta.wiki, Video Editor].

On PC, clips are kept under `Documents\Rockstar Games\GTA V\videos\clips` and exported videos under
`%LOCALAPPDATA%\Rockstar Games\GTA V\videos\rendered`; the game offers no way to move either folder [Press: dor.gg;
Players: Steam].

## The words Rockstar uses {#words}

| Rockstar's word | What it names |
| --- | --- |
| Clip | One recording, replay data; named by date and number, such as "Jan 13 2015 Clip 0001" [Wiki: GTABase] |
| Recording | Making a clip on demand, from start to save |
| Action Replay | The background buffer, and the clip saved from it |
| Clip Management | The list of clips: preview, favourites, sort by name, size or use in projects, delete; a mark on clips a project uses [Wiki: GTABase] |
| Project | The container for clips: clips placed and trimmed on a timeline with music, sound, text and their settings; saved, and saved under a new name to keep versions [Wiki: GTABase] |
| Marker | A point on a clip that holds edits (camera, effects, speed, depth of field, audio) from that point on [Wiki: GTABase] |
| Anchor | A point on the project's timeline where text or a music, score or ambient track is placed [Wiki: GTABase, gta.wiki] |
| Video | A project exported to a video file |
| Video Gallery | The list of exported videos, from where they are watched or uploaded [Wiki: GTABase] |
| Director Mode | The separate feature that stages scenes with actors ([above](#what-it-is)) |
| Snapmatic | The game's photo gallery; a frame at a marker can be saved to it [Wiki: GTABase] |

**The container for clips is the project.** Rockstar never calls it a movie or a film: the player builds a project and
exports it as a video.

## The editor {#editor}

### Projects and the timeline {#timeline}

A project holds clips in order on a timeline; each is trimmed to the part wanted, and the project adds radio tracks,
score tracks with five levels of intensity, ambient tracks (rain, the city) and one-off sound effects (horns,
explosions), and text [Wiki: GTABase]. The timeline colours its rows by kind: clips blue, radio purple, score orange
[Wiki: gta.wiki]. Clips are joined in nearly any number, so a long film is many short clips [Press: PC Games].

### Markers {#markers}

All editing of a clip happens at markers. The player plays or scrubs the clip, adds a marker, and sets what holds from
it on: the camera, depth of field, effects, audio, speed, the thumbnail and a transition at the first or last marker
[Wiki: gta.wiki]. A new marker takes the settings of the one before it; shoulder buttons jump from marker to marker; a
marker's settings can be copied to another; a marker can be moved [Wiki: GTABase; Press: libertycity]. Markers are the
editor's keyframes: what changes between two markers is either a cut or a blend ([Blends](#blends)).

### Cameras and target tracking {#cameras}

At each marker the camera is one of [Wiki: GTABase; Players: eklipse]:

- **The game camera**, as it was during play.
- **Preset angles** around the player.
- **The free camera**, placed by the player, with its field of view and an amount of shake.

The free camera can **look at a target** (the player, another character or a vehicle) so it turns to keep it framed,
and can be **attached to a target** so it rides with it, moving in the target's own frame [Wiki: GTABase; Mod:
Rockstar Editor+]. With both, a camera can follow a running character or a car at a set offset. Clips recorded in
first person cannot be given another camera [Press: PC Gamer, via dor.gg].

### Blends between markers {#blends}

Each marker says how the camera reaches it from the one before: a cut, or a blend that moves the camera from the first
marker's position and angle to the second's over the time between them [Players: eklipse]. A mod's author, who reads
the editor's code, describes the stock smooth blend as a straight line between the markers with a damped spring on
top: the path cannot curve, and the spring undershoots at corners, so the camera never quite reaches the marker where
it was placed [Mod: Rockstar Editor+]. The same author says the speed of playback jumps at each marker rather than
easing, and that zooms can overshoot.

### Speed {#speed}

Each marker sets the playback speed from it on, slower or faster than real time, for slow motion and fast motion over
the stretch between two markers [Wiki: GTABase; Press: libertycity]. Speed changes at the marker in one step [Mod:
Rockstar Editor+], so a speed ramp is a run of markers with steps between them. The published material gives no exact
list of speeds.

### Depth of field and effects {#effects}

Depth of field is set per marker: its kind and strength, and a focus target that it keeps sharp as it moves [Wiki:
GTABase]. Effects per marker are a filter (a set of colour looks) and its intensity, with saturation, brightness,
contrast and vignette [Wiki: GTABase, gta.wiki].

### Scrubbing and preview {#scrubbing}

The player plays, pauses and scrubs a clip in the editor, and plays the whole project in full screen [Wiki: GTABase].
Because a clip is replay data, every seek makes the game rebuild the scene at that moment, which waits for the world
around it to load; on heavily modded games this wait lasts seconds after each seek, cut or marker move [Mod:
Rockstar Editor+]. The world loads around the player's recorded position, not the free camera, so a distant shot can
show low detail or missing parts of the map [Mod: Rockstar Editor+].

### Audio {#audio}

Per marker, the volumes of the game's sound effects, dialogue and music, the score's intensity, and added one-off
sounds; music from the game's radio stations; score and ambient tracks placed on the project timeline [Wiki: GTABase,
gta.wiki]. A player cannot add music of their own [Press: PC Games].

### Text and transitions {#text}

Text is placed on the project timeline at an anchor, with a choice of fonts (tripled in a 2015 update) [Wiki:
gta.wiki, GTABase]. Transitions are fades at a clip's first or last marker; the joins in the middle of a project are cuts
or camera blends [Wiki: gta.wiki]. GTA IV's editor set a text's font, size, colour and duration, and had four
transitions: cut, fade in, fade out, fade in and out [Wiki: gta.wiki, Video Editor].

## Export {#export}

A project is exported to a video in the Video Gallery, which uploads to YouTube and once to Rockstar's Social Club
[Wiki: GTABase, gta.wiki]. On PC the export is an MP4 file on disc ([What a clip stores](#clip-data)); the player picks
the frame rate and the quality (PC Gamer advises 60 frames a second and a high bitrate), and the game raises its
graphics settings while it renders [Press: PC Gamer, dor.gg]. On consoles the export is 1080p [Wiki: GTABase]. The
video is rendered frame by frame, so it takes longer the longer and the higher in quality it is [Press: dor.gg]; no
published figure gives how long. The PC export is at the game's own resolution; mods exist only to export above it, at
other frame rates, with motion blur or in other formats, which shows what players missed [Mod: Extended Video Export,
Rockstar Editor+].

## Limits and restrictions {#limits}

- **The camera's range:** the free camera moves only within a radius of the recorded player [Press: PC Games;
  Players: eklipse; Mod: Rockstar Editor+].
- **Places and scenes:** no camera edits in parts of Fort Zancudo, the prologue and one mission's scene; first-person
  clips cannot be re-angled; entering such a place or switching to first person locks the whole clip [Wiki:
  gta.wiki].
- **Length and gaps:** clips of unpredictable length, with a gap where one clip ends and the next starts
  ([Clip length](#clip-length)).
- **Storage:** the cap on clip storage; on the old consoles, the deletion of every clip and project in 2024.

## What players praise and dislike {#reception}

**Praised:** the free camera, which turns a moment of play into a shot from anywhere near it [Press: PC Games];
Action Replay, which keeps what already happened; more creative tools than GTA IV's editor, enough for stunt montages
with fast camera switches and blur [Press: PC Games]; a whole film made inside the game, with its own music and
sound. The editor became a common way to make GTA films, and mods that improve it are still made in 2026 [Mod:
Rockstar Editor+].

**Disliked** [Players: Steam; Press: Dexerto; Mod: Rockstar Editor+]:

- Clips too short, of varying length, and with gaps between them.
- The camera's limited range, the locked clips and the world loading around the player instead of the camera.
- Blends that undershoot and do not curve, speed that jumps at markers.
- Slow seeking and long waits on large or modded games.
- Export at the game's resolution, at most 1080p on consoles; no 4K, no choice of format; players ask for colour
  grading tools.
- Storage limits on long recordings, and work lost when the editor was removed from the old consoles.
- The editor only in Story Mode, and only when the player is free.

### What Coney can do better {#better}

- **Fixed clips, no gap.** Director's clips are 60 seconds whatever happens, saved at once, with the next one starting
  on the very next step ([#1184](issue:1184)). That needs the recorder to write to disc as it goes, at a bounded rate,
  rather than fill a buffer whose seconds depend on the scene.
- **Small, measured data.** A recording builds on the state the engine replicates for co-op
  ([Snapshots](../network-architecture.md#snapshots)), so its size can be measured and kept small; the format is
  [#1186](issue:1186).
- **Fast, exact seeking.** Full states at regular steps, so a seek starts from the nearest one; Unreal's replays do the
  same ([Other games](#other-games)). Where a level is loaded whole, a seek waits on no streaming; how much of a level
  Director must load is for [#1186](issue:1186).
- **No range circle and no locked places** in Director, unless a reason is found; the camera goes where the level is.
- **Blends that pass through every marker**, with eased speed, and speed ramps without steps: what the mods add.
- **Export by rendering each frame offline**, at the chosen resolution and frame rate, independent of the window and
  of how fast the machine is, and the same every time (the [test mode](../testing.md) holds for it).
- **Local and kept:** recordings and videos stay in the install folder; nothing is uploaded or deleted by a service.
- **Open whenever the game is paused**, not only in some states.

## Other games' replays {#other-games}

Two ways to record a replay appear, and they decide what survives an update:

| Game | What a replay stores | Notes |
| --- | --- | --- |
| Halo (Theater) | The players' and characters' inputs, played again through the game [Wiki: Halopedia] | Films are recorded for every match; a clip is a saved part of a film; free camera within the map, none in cutscenes; Halo 3 saved up to 100 films |
| StarCraft II | The players' commands | An old replay opens by loading the old version of the game [Players: Blizzard forums] |
| Fortnite | The game's network data (Unreal's replay system) | Gameplay, third-person, drone follow, drone attach and drone free cameras, speeds from a quarter up; replays play only in the version that recorded them [Press: Epic, via devtrackers; Players: finalboss] |
| Overwatch | Match data | Replays are deleted at the next patch [Players: Blizzard forums] |
| Rocket League | The game's network data (Unreal Engine 3) | The format changes between versions; replay viewers outside the game must be updated after patches [Players: devtrackers] |
| Forza Motorsport | Race data | Replay cameras and photo mode; saved replays are kept in a small number (ten in FM7) and shared in game; no editor, which players ask for, naming the Rockstar Editor [Players: Forza forums; Forza support] |

Unreal's replay system, which Fortnite and Rocket League build on, records the network data its game already sends,
writes a **checkpoint** (a full state) every 30 seconds by default so a seek starts from the nearest one, and trades
fewer checkpoints for smaller files against slower seeking backwards; it can stream to a file or keep a running window
in memory, as Action Replay does [Engine: Unreal documentation].

**What it means for Director:** an input replay depends on every rule of the game staying the same, so a change to
the game breaks old recordings (Halo, StarCraft, Overwatch); a state replay plays back without the game's rules and
breaks only when the state's format changes. Coney changes often while it is built, so a recording of state, with a
format version and a converter where the format changes, keeps the player's recordings working; this is the question
of [#1186](issue:1186).

## Sources {#sources}

- Rockstar: [How to Record Clips and Access the Rockstar Editor in GTAV on
  PC](https://support.rockstargames.com/articles/4gZPiqdUZD8ZhaJ81IPDJr/how-to-record-clips-and-access-the-rockstar-editor-in-gtav-on-pc),
  [Updates to the Rockstar Editor on PlayStation 4 and Xbox
  One](https://support.rockstargames.com/articles/PcRLMkrm7BVrJN7AzeZej/updates-to-the-rockstar-editor-in-grand-theft-auto-v-for-playstation-4-and-xbox-one).
- Press: [Trusted Reviews](https://www.trustedreviews.com/news/rockstar-editor-for-gtv-5-is-exclusive-to-pc) (the PC
  launch), [VGC](https://www.videogameschronicle.com/news/gta-5s-rockstar-editor-is-shutting-down-on-ps4-xbox-one-with-all-clips-and-projects-to-be-deleted)
  (the 2024 removal), [PC Games](https://www.pcgames.de/GTA-5-Grand-Theft-Auto-5-Spiel-4795/Specials/GTA-5-PC-vor-dem-Test-In-4K-gespielt-Ein-Must-Have-fuer-PC-Spieler-1154980/3/)
  (a 2015 demonstration), [PC Gamer](https://www.pcgamer.com/getting-the-best-out-of-gta-vs-rockstar-editor),
  [dor.gg](https://clip.dor.gg/en/blog/gta-5-rockstar-editor-recording) (a guide: keys, folders, export),
  [libertycity](https://libertycity.net/rockstar_editor.html),
  [Dexerto](https://www.dexerto.com/gta/gta-6-rockstar-editor-changes-fan-wishlist-1905592/) (a players' wish list).
- Wiki: [GTABase](https://www.gtabase.com/grand-theft-auto-v/rockstar-editor/) (the console guide),
  [gta.wiki, Rockstar Editor](https://gta.wiki/w/Rockstar_Editor), [gta.wiki, Video
  Editor](https://gta.wiki/w/Video_Editor) (GTA IV), [Halopedia, Theater](https://www.halopedia.org/Theater).
- Players: [Steam, clip length](https://steamcommunity.com/app/271590/discussions/0/611703999967440454),
  [Steam, clip length and gaps](https://steamcommunity.com/app/271590/discussions/0/611703999973663359),
  [Steam, the folders](https://steamcommunity.com/app/271590/discussions/0/611704730314668918),
  [a settings file](https://gist.github.com/ConnorMcF/a2836af43f9bf7d06b24),
  [eklipse](https://blog.eklipse.gg/streaming-tips-2/rockstar-editor.html) (a guide), [Blizzard forums,
  Overwatch](https://us.forums.blizzard.com/en/overwatch/t/old-replays-from-the-previous-version/773382),
  [Blizzard forums, StarCraft II](https://us.forums.blizzard.com/en/sc2/t/cant-load-replays-from-the-last-patch/28323),
  [Fortnite's replay notes](https://v2.devtrackers.gg/fortnite/p/a9e861b8-fortnite-battle-royale-replay-system),
  [finalboss](https://finalboss.io/youre-not-using-fortnite-chapter-6-replays-right) (Fortnite's speeds),
  [Rocket League's replay format](https://devtrackers.gg/rocket-league/p/0832c911-psyonix-help-me-please-how-are-rotations-encoded-in-v1-45-replay-files),
  [Forza forums, a replay editor](https://forums.forza.net/t/please-improve-photo-mode-replay-mode-for-cinematic-creators/811836),
  [Forza support, FM7 replays](https://support.forza.net/hc/en-us/articles/360005365674-Replays-and-Video-Clips-FM7).
- Mod: [Rockstar Editor+](https://www.gta5-mods.com/scripts/rockstar-editor) (version 1.6.1),
  [Extended Video Export](https://www.gta5-mods.com/scripts/extended-video-export).
- Engine: [Unreal, DemoNetDriver and
  streamers](https://dev.epicgames.com/documentation/en-us/unreal-engine/demonetdriver-and-streamers-in-unreal-engine).

## The words Director uses {#terms}

A maintainer decision: the tool is **Coney Director**. GTA V's own *Director Mode* is a different feature, the one that
stages scenes with actors ([What it is](#what-it-is)); Director has no such part, because scenes are made with Coney
Studio and its scripts and recorded in game ([#1184](issue:1184)). The other words follow Rockstar's, as the brief asks:

| Word | Meaning in Director |
| --- | --- |
| Recorder | The part of the game that records; started and stopped by the player, or by a script |
| Recording | One run of the recorder, from start to stop: a chain of clips with no gap between them |
| Clip | One saved piece of a recording, 60 seconds by default (the last one shorter); replay data, not video |
| **Project** | The container for clips: clips in order on a timeline, trimmed, with their markers and the project's audio and text; Rockstar's word, recommended over "movie" (see [Open questions](#questions)) |
| Marker | A point on a clip that holds the camera, speed and effects from it on; the editor's keyframe |
| Blend | How the camera and speed move from one marker to the next: a cut, or a smooth change |
| Target | A character or object a camera looks at or rides with, and that depth of field keeps in focus |
| Video | A project exported to an MP4 file |
| Clips, Projects, Videos | Director's three lists, all in the one fixed folder in the install folder ([The install folder](../asset-and-script-architecture.md#install-folder)) |

"Clip" also names an animation clip in the engine's code; Director's code keeps its clips in its own namespace so the
two never meet.

## Director's first version {#first-version}

**Must** marks what the first version ships with (it covers the parent issue's done line: 60-second clips without a
gap, opened in Director, a camera keyframed over a clip with a speed change, an MP4 export); **should** what follows it
closely; **later** what waits.

| Feature | Priority | Note |
| --- | --- | --- |
| Start and stop recording from the keyboard and the pad, with an indicator and the time | must | |
| 60-second clips, each saved as it ends, the next starting on the next step with no gap | must | Coney's own; Rockstar's vary and leave gaps |
| Clips as replay data, small and bounded in size a second | must | The format: [#1186](issue:1186) |
| Recording from a script, and in the test mode (fixed step, seeded randomness) | must | So Studio scenes are recorded, and the recorder is tested |
| The Clips list: thumbnail, name by date and number, length, preview, delete | must | Rockstar's Clip Management |
| Projects: new, open, save, save as; clips added, ordered and trimmed on a timeline | must | |
| Play, pause, step a frame, and scrub with an immediate picture | must | Full states at regular steps |
| Markers: add, move, delete, copy settings, jump between them; a new one takes the last one's settings | must | |
| Cameras per marker: the game camera, the free camera (position, angle, field of view) | must | No range circle |
| Target tracking: look at a target, and ride with a target at an offset | must | In the brief |
| Blends: a cut, or a smooth curve that passes through every marker | must | Better than Rockstar's undershoot |
| Speed per marker, slow and fast, eased between markers so a ramp has no steps | must | |
| The game's sound played back with the clip, following the speed | must | Pitched or muted in slow motion: [Open questions](#questions) |
| Export to MP4, rendered frame by frame offline, with a choice of resolution and frame rate; progress, cancel | must | MP4 only, as the brief sets |
| Undo and redo, and the project saved by itself | must | Polish |
| The editor driven fully by keyboard and mouse, and by the pad | must | |
| Action Replay: a buffer of the last stretch of play, kept on demand | should | |
| Depth of field per marker, with a focus target | should | |
| Effects per marker: brightness, contrast, saturation, vignette, a few filters | should | |
| Fades at the start and end of a clip | should | |
| Text on the project timeline | should | |
| Camera shake per marker | should | |
| Audio volumes per marker (effects, voices, music) | should | |
| A still frame saved as an image | should | |
| The Videos list, playing an export in Director | should | |
| A camera path editor showing the curve in the level | later | |
| Music tracks and the player's own audio on the timeline | later | [Open questions](#questions) |
| Recording a co-op session | later | Waits for networking |
| Other formats, uploads | later | MP4 only for now; nothing uploaded |

## Open questions {#questions}

For the user who set Director's brief, or a maintainer:

1. **The container's name.** Rockstar's word is *project*, and this page recommends it. Coney Studio also says
   *project* for the folder it creates ([Studio's terms](../studio/index.md#terms)). Is *project* right for both,
   since the two tools never share a screen, or should Director say *movie* to keep them apart?
2. **Marker or keyframe.** Rockstar says *marker*; the brief says keyframes. Is *marker* the word on screen?
3. **The recorder's default.** Is the recorder off until the player starts it, and is Action Replay (a buffer always
   running) wanted in the first version or after it?
4. **Storage.** What cap on the recordings folder, and what happens when it is full: stop recording, or delete the
   oldest clips that no project uses?
5. **Sound in slow motion.** Pitched down with the speed, kept at pitch, or muted?
6. **Music.** May a video carry the game's own music from the player's disc, or music files of the player's own?
   This may be a question for the legal side ([LEGAL.md](repo:LEGAL.md)).
7. **Movies and cutscenes.** The game's pre-rendered movies are video, and cannot be shown from another camera: are
   they recorded as they are, or left out of recordings?
8. **Old recordings.** When a Coney update changes the recording format, must every old clip still open (converted on
   load), or may an old clip say it is too old?
9. **Where Director opens.** From the pause menu and the main menu of the game, as a screen of the game itself, or as
   a program of its own beside Coney Studio?
10. **Export sizes.** Which resolutions and frame rates are offered (up to 4K, 60 frames a second?), and which is the
    default?
