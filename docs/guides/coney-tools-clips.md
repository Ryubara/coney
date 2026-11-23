# The coney-tools clip commands

The commands of in-game video clips for issues and pull requests: [`clips`](#clips) records a clip of the original
with PCSX2's own capture (`clips record`), records one run of Coney before and after a change and stacks the two side
by side ([`clips pair`](#clips-pair)), encodes a clip to fit GitHub (`clips encode`), and checks, publishes and cleans
up clips, whether made in PCSX2 or by `coney --record`. The rules every clip keeps are in [Clips](testing.md#clips).
The PCSX2 commands are on [their page](coney-tools-pcsx2.md#pcsx2); the other groups are listed in
[the command line overview](coney-tools.md#groups).

## clips {#clips}

```sh
uv run --project python coney-tools clips record STATE --out CLIP --agent ID [--seconds N] [--iso DISC] [--scratch DIR]
```

Records a short clip of the original (`--seconds`, 1 to 10, default 10) with **PCSX2's own video capture of its GS
output**: nothing on the desktop is read. For the run, the claimed copy's ini switches the capture on (with sound),
points PCSX2's videos folder at a staging folder beside the clip, turns every on-screen display element off (FPS,
messages, the recording indicator) and binds "Toggle Video Capture" to F12, which is posted to PCSX2's window by handle
to start and stop it; the ini is put back when PCSX2 closes. The capture is then re-encoded into `CLIP` as Coney's
clips are (960 x 720, H.264 and AAC, no metadata, cut at the length) through `clips.run_ffmpeg`, and the staging
folder deleted. PCSX2 2.9.94 tears the first ~1.15 s of its capture (a black band along the bottom), so the capture
runs 1.5 s longer (`CAPTURE_START_TRIM`) and the clip starts after that, still lasting `--seconds`.
`--out` is a path under the scratch folder, `<workspace>/scratch/<task>/clips/NAME.mp4`; give it absolute (the
folder `clips folder TASK` prints), because a relative `../../scratch/...` is wrong from a worktree, which sits three
levels below the workspace. The game comes from `game_dir` in `coney.local.toml`; without it pass `--iso DISC`,
and likewise `--scratch DIR` (an absolute path) without `scratch_dir`.
`CLIP` follows the clips rules below. It needs FFmpeg as `clips` finds it, and Windows (keys by
handle). PCSX2 starts on `STATE` under `--agent`'s claim, released at the end when `clips record` made it.

```sh
uv run --project python coney-tools clips folder TASK
uv run --project python coney-tools clips check CLIP [CLIP...]
uv run --project python coney-tools clips publish (--pr N | --issue N) [--edit] (--body TEXT | --body-file FILE) CLIP [CLIP...]
uv run --project python coney-tools clips clean [--older-than HOURS] [--apply]
```

In-game video clips for issues and pull requests, made by `coney --record` ([Recording a
clip](running-options.md#record)) or `clips record` (above). The rules and why: [Clips](testing.md#clips); the limits
are `workspace.clips` in `.github/conventions.yml`.

* `folder` makes and prints a task's clips folder, `<scratch>/<task>/clips/`, where `<scratch>` is the workspace's
  shared scratch folder (`../../scratch` beside the main checkout).
* `check` runs ffprobe on each clip (an `.mp4`, or a `.gif`) and prints its length, size, picture and whether it has
  sound. It refuses a clip outside a task's clips folder or inside a git checkout, one longer than `max_seconds` (10),
  and one with any tag beyond the fixed ones an MP4's boxes carry (`major_brand`, `minor_version`, `compatible_brands`,
  `language`, `handler_name`, `vendor_id` and a version-free `encoder`); one bigger than `warn_mb` (10 MB) gets a
  warning. `pr check-body --local-images`, `pr text` and `pr verify` run the same checks on the clips a description
  shows.
* `publish` makes the same checks, then uploads every clip in one gh command, `gh pr|issue comment N` (or `edit` with
  `--edit`, into the description) with one `--attach` per clip, and **deletes the clips once gh succeeds**. When gh
  fails, it keeps them and says so (exit 1). A body may place a clip with a `./clips/<name>.mp4` reference, which gh
  replaces with the upload when the clip is named the same way. A pull request's before and after clips go into its
  description through [`pr open`](pr.md#open) instead, which uploads them the same way, puts each in the form GitHub
  plays and deletes it ([Clips](testing.md#clips)); `publish` is for an issue and for a comment. These two are the only
  ways clips are uploaded.
* `clean` lists every file in the task clips folders older than `keep_hours` (24; `--older-than` overrides it),
  including what a failed recording left (part files, a `clips record` staging folder), and deletes them with `--apply`
  (a dry run without it, as `workspace clean` is; its old `--dry-run` is accepted with a notice until the cutover).

FFmpeg and ffprobe are found in `ffmpeg_dir` of `coney.local.toml`, else beside `CONEY_FFMPEG` (the variable Coney's
`--record` reads), else on `PATH`. Every FFmpeg command the tools run goes through `clips.run_ffmpeg`, which refuses a
capture device or any input that is not a file.

### Before and after: clips pair {#clips-pair}

```sh
uv run --project python coney-tools clips pair SCENARIO --agent ID [--original STATE] [--gif] [--slow N] [--prefix P] [--without COMMIT | --before REF]
uv run --project python coney-tools clips pair SCENARIO --agent ID --renderer gpu --before-renderer gl3
uv run --project python coney-tools clips pair --level L --input-script FILE [--seconds N] [--start-frame N] --agent ID
uv run --project python coney-tools clips encode CLIP --out NAME.gif|NAME.mp4 [--start-frame N] [--seconds N] [--slow N]
uv run --project python coney-tools clips scenarios
```

`clips pair` records one run of Coney twice with the same arguments and puts the two side by side, so a pull request
shows motion, timing, AI, the camera or an effect before and after its change. Run it from the task's worktree. It
first removes the last pair's clips with the same `--prefix` (an older compare must never be published as this one),
then, in order, it:

1. builds the task's worktree as it stands (`coney-tools build --target coney`, preset `workspace.clips.pair.preset`,
   under the build slots: a build that gives up waiting ends the pair with its exit code, 75);
2. finds the branch's **merge base** with `origin/main` (`--base` for another branch; never `origin/main` itself, which
   carries other pull requests' changes the branch does not have; a fetch is yours to run first) and builds it in its
   own detached worktree, `worktrees/coney/clips-base-<commit>`, holding its busy marker meanwhile (it names the
   pair's process, so a pair killed hard does not protect the base for ever). The next pair from the same base, any
   task's, finds it built, and each pair that uses it counts as activity. [`workspace clean`](workspace.md#cleanup)
   removes it once it has been idle as long as a build folder may be, a base from `--base` that `main` never gets
   too (a branch holds its commit). It counts toward the [cap on build folders](agents.md#build-cap) like any build,
   with no exemption: a pair from a base without a build folder needs one free build place, or the base build is
   refused and the pair ends with its exit code. The worktree made before the refusal stays, small and without a
   build: the next pair builds in it, and `workspace clean` removes it once idle. With `--without COMMIT` the before
   is that merge base with `COMMIT` taken out instead, for the clips of a change already merged that its pull request
   lacked (`--without` the fix's squash commit): the same code as the after but for that change, so a Coney option the
   run needs that came later (`--record-from-frame`) is on both sides, where a base from before the change would lack
   it. The tools make the tree with `git merge-tree` (refused when `COMMIT` is not in the base or a later commit edits
   its lines), commit it on top of the base under the base's own author, with no branch, and keep it in
   `worktrees/coney/clips-base-<base>-without-<commit>`; `workspace clean` removes it once idle like any base;
   a commit a later change edits the lines of is refused with a line saying so: take the change out by hand instead, a
   commit on a branch of the merge base that reverts it with the conflict resolved, and give the branch as `--before REF`
   (not with `--without`). `REF` must hold the merge base, so the before differs from the after by that one commit; it is
   built in `worktrees/coney/clips-base-<commit>` like any base, `pair.json` names it (`ref`), and the same rules for the
   Coney option the run needs apply;
3. records the run on the base (`before.mp4`) and on the task (`after.mp4`) with `coney --record`
   ([Recording a clip](running-options.md#record)), from the scenario's start frame on: test mode, the task's own
   input script on both sides, each run from its build's checkout, FFmpeg from `CONEY_FFMPEG` or the one the tools
   find. `--renderer gl3|gpu` draws both with that renderer (Coney's own choice, GL3, without it); `--before-renderer`
   gives the before another one. A change to the GPU renderer records `--renderer gpu --before-renderer gl3`: its
   picture after, against GL3's before, the reference it matches
   ([How pictures are compared](../engine/renderer.md#comparing)), since a merge base older than the GPU renderer's
   clips cannot record on it. A side drawn by a named renderer is
   labelled with it ("After GPU"), and `pair.json` names each side's;
4. checks the two are the same run: both recorded as many frames, or it stops (`not the same run`). It prints both
   frame hashes and writes `pair.json` beside the clips (the run, both commits, frame counts and hashes; no game data).
   Equal hashes mean the change shows nothing in this run: it says so, makes no compare and exits 1, so pick a run
   that shows the change;
5. with `--original STATE`, records the original from that PCSX2 state through [`clips record`](#clips) under
   `--agent`'s claim (`game_dir` and `scratch_dir` of `coney.local.toml` are needed), for faithful work;
6. writes `compare.mp4`: Original (when asked), Before and After from left to right, each labelled in its top left
   corner, cut to the clip's seconds from its start frame, without sound (the two clips keep theirs). `--gif` adds
   `compare.gif`.

The files go in the task's clips folder (`clips folder TASK`); `--prefix P` names them `P-before.mp4` and so on, for a
second pair. They follow the clip rules ([Clips](testing.md#clips)): the coordinator publishes them, and `clips clean`
removes what is left.

**Scenarios.** A named scenario is a TOML file in `tools/clips/` (`workspace.clips.pair.scenarios`), so a reviewer
reruns the same pair; `clips scenarios` lists them. Its keys are `description`, `level` (`--play-level`), `input` (an
input script, a path in the checkout, played from the level's first frame of play) and `seconds` (how long the clips
show), and optionally `start_frame` (the frame of play the clips start at), `checkpoint`, `ai_seed`, `skip_movies`
(default true), `record_debug_draw` (default false: `--record-debug-draw`, which keeps the Debug draw page's switches
the run sets, as a clip drops them otherwise), `complete_levels` (a list of level numbers, Coney's
[`--complete-levels`](running-play-level.md#complete-levels): the story save the level starts from), and, for
[a guest's view](#clips-pair-guest), `host_input` and `join_after`; any other key is refused. Without a scenario,
`--level` and `--input-script` give the same run (`--seconds` defaults to 10). The clips are at most
`workspace.clips.max_seconds` (10) long, but the run before them is not: Coney plays the script from the level's first
frame of play and starts recording at `start_frame` through [`--record-from-frame`](running-options.md#record), so a
scenario can punch a cabinet 70 s into the level (`level99-cabinet`) and still show 8 s of it. `start_frame` and the
input script's frames count the same way, from the level's first frame of play.

`level87-stealth-kill` is the scenario for the stealth kill and for hiding: from level87's checkpoint 5 it takes the
player into the tutorial's hint volume, which warps him behind the first guard (S01), where he hides. The tutorial
holds the pad for about 9 s, so the clip starts at frame 600; the player then holds L1, creeps up behind the guard,
and square at frame 680 plays the kill (clips 637 and 638). Its before for the sneak attack (#1504) is `--without`
that change's commit, or `--before` a hand-made commit where later commits edit its lines. `level87-hide` is the same
route without the kill.

`level99-sparring` is the scenario for a fighter's run-in and charge: the last lesson of level99's combat tutorial,
which the story reaches only by finishing the lessons before it, so its input script calls the lesson's set-up through
the developer console's [`lua`](console.md#catalog) (`300 console lua P1.SetupWarriors()`), as the sparring disc test
does. Its before needs that command too: a merge base older than it is given as `--before` a commit of the merge base
with the command's commit put on top. At AI seed 4 the three sparring Warriors run in at the player, who stands still,
and the first to arrive stands beside him about 4 s into the clip: where a change to the run-in shows (the charge on
arrival, #606).

`level5-full-ring` is the scenario for a full health ring: at level5's boss fight (checkpoint 4) cross skips the
opening scene, the debug menu's Player page makes the player Diego's character type (120) and SELECT shows his rings.
Only a power class whose ring byte is 100 fills the whole circle (Rembrandt's stops at 35 %), and type 120's class 6
is one; a player is never drawn with a boss's colour bands, so his outer ring is green with the black lost colour, and a
lost wedge on a full ring shows (#1630).

`level95-armies-cabinet` is the scenario for the hub's arcade cabinet: from a story save with the story and the five
flashbacks complete it walks Swan to the cabinet and presses triangle, and Armies of the Night's first stage loads. Its
before for the change that made the cabinet work (#569) is `--before` a commit that takes that fix out, as the base
has no `--complete-levels`.

`level99-world-parts` is the scenario for the World page's part switches: it opens the pad menu in the level99 street
and hides the streamed world, then shows it and hides the objects, for a change to the mesh views' hook or to what a
renderer records for a hidden kind; record it on `--renderer gpu`.

`level99-mesh-views` is the scenario for the Debug draw page's mesh views: it opens the pad menu in the level99 street
and sets the model wireframe on every human before the clips, then the mesh view to flat by material and, near the
end, to wireframe only, for a change to what a renderer draws for the views; it keeps the debug draw
(`record_debug_draw`), and record it on `--renderer gpu` against GL3 (`--before-renderer gl3`) or against the base,
where the switches did nothing. The normals and bone axes need an Entity inspector selection, which a clip never
keeps, so its clips show the wireframe and the flat views only.

**The encodes** port the ideas of GD Studio's capture tools to Coney's clips. `clips encode` re-encodes any clip the
same way: a GIF for an `--out` ending in `.gif`, else a silent MP4. A bare `--out` name goes beside the clip, never
in the current folder (usually the checkout, where clips may not be written); a path with a folder is used as given.

* **An MP4 fits GitHub's limit** (`upload_max_mb`, 10 MB): a compare is encoded with 720-pixel panels and again at
  each height of `mp4_heights` (540, 432, 360) while it is over; still over at the last, it fails and says so.
* **A GIF has a budget per second** (`gif_kb_per_second`, 512 KB, counted for `gif_min_seconds` to `max_seconds`, so
  1 to 5 MB): a GIF plays while it downloads, so the budget is a rate. It has a 128-colour palette, is at most 800
  pixels wide (GitHub's description column is 814) and is encoded again at each width of `gif_widths` (640, 480)
  while it is over. Still over at the last, it is kept with a `warning:` line naming the MP4 (and fails over 10 MB).
  Its frame rate is 10 (`gif_fps`), since a GIF's delays are whole hundredths of a second and 30 frames a second
  cannot be; a slowed clip whose rate is already whole keeps every frame (`--slow 3` plays all 30 a second at 10).
* **`--slow N`** plays the clip N times slower. An MP4 keeps every frame (a fast pose change, a one-frame flash); a
  GIF keeps every frame only when the slowed rate is whole hundredths of a second, and otherwise prints how many it
  keeps (`the GIF keeps 100 of 150 frames at --slow 2`), so use the MP4 when one frame matters. The slowed clip
  counts against the 10-second cap: what does not fit is cut, with a line such as `clip cut at 10 s: 75
  frames (2.50 s) of 150 left out at --slow 4`. Read it before using the clip; the moment meant may be in the cut.
* **Every encoding prints its size**, and goes to a part file first, so a failed encode leaves the file that was there.

Which one a description shows: a GIF for a short, silent change that loops well (a pose, a motion, a fade), as it plays
inline the moment the page opens; the MP4 for detail a 128-colour palette loses (fog, gradients, light), for sound
(`before.mp4` and `after.mp4` keep theirs) and for a slowed clip.

Watch out for:

* **A pair is only as good as its run.** Look at both clips before using them: the level, the place, gameplay rather
  than a loading screen, and the moment the change touches inside the seconds shown. A scenario whose start lands
  after the change, or whose input script no longer reaches the spot, shows nothing useful even when the hashes differ.
* **The base is the merge base.** A before taken from `origin/main` after other merges shows their changes as part of
  this one (GD Studio's pairs did, before they used the merge base).

### A guest's view: a host and a guest {#clips-pair-guest}

A change to what a guest draws of a host's game (its camera, its scene, the humans it draws from the snapshots) cannot
be recorded by one process, so a scenario with a `host_input` records two on this machine. `level` is the host's level,
`host_input` the host's input script (a path in the checkout) and `input` the **guest's** pad. For each side the tools
start that build's host, headless, on a free port of the loopback (`--host 127.0.0.1:0`, with `--play-level LEVEL`, its
script and a frame limit far past the clip, as a guard), read the port from its `hosting on port N` line and wait, when
`join_after` is given, for the host to print that text too (the first frame of its level: a guest that joins during the
host's load can time out, as the [two-process checks](multiplayer.md#two-processes) found). Then they start the guest,
`--join 127.0.0.1:N --net-view` with `--record`, the clip's seconds and its `start_frame`, and stop the host when the
guest ends.

The guest's frames and its input script count from the first frame its client game has the host's level
([`--record-from-frame`](running-options.md#record)), and the host's loading barrier holds the host's steps until the
guest has the level, so the two scripts start close together. The two runs are not frame for frame the same as a
one-process run is (which of the host's steps a guest draws depends on the real time the two take), so the pair's check
is the frame count, and the hashes differ even for no change: read the clips. A before that cannot record a guest (a
base without `--record-from-frame` for `--net-view`) is `--before` a hand-made commit that adds those options to the
merge base, as for any change the base lacks an option for.

`level99-guest-camera` is the scenario for the guest's follow camera (#1333): the host's player skips the intro, stands
while the view fades in, then walks and starts to run through the level99 street while the guest's right stick turns
its own view. The host plays its own script (`tools/clips/level99-guest-camera-host.txt`), not the one the one-process
scenarios use: the loading barrier holds the host's steps for the guest's load while its script keeps counting, so the
intro scene has fewer updates than the script has frames, and a scene takes a skip only after 60 of its updates, so
a skip tapped at frame 70 is refused and the host's player stands in the scene for the whole clip. Check a guest
scenario's clips for a human who moves. Its before is `--before` a commit of the merge base with the guest recording's
options only, where the guest's view is the old stand-in: a camera fixed behind the lowest-id human, which follows him
but rigidly, turning with his heading and not lagging behind it.

<!-- command-reference:begin clips -->
## Parameters of `clips` {#clips-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `clips record` {#clips-record-usage}

Record a short clip of the original with PCSX2's own capture of its game output (never the screen).

```text
coney-tools clips record STATE --out OUT [--seconds SECONDS] [--agent AGENT] [--pcsx2-dir PCSX2_DIR] [--iso ISO]
    [--scratch SCRATCH]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `STATE` | path | required | the .p2s state to start from (a copy outside sstates/) |
| `--out OUT` | path | required | the clip: \<scratch\>/\<task\>/clips/\<name\>.mp4 |
| `--seconds SECONDS` | integer | `10` | how long, 1 to the cap of 10 (default 10) |
| `--agent AGENT` | text | none | your id; runs under your claim (made, and released at the end, if none) |
| `--pcsx2-dir PCSX2_DIR` | path | none | the portable PCSX2 folder; default: pcsx2_dir |
| `--iso ISO` | path | none | the disc image (or a folder with one); default: game_dir |
| `--scratch SCRATCH` | path | none | where state copies and the disc link go; default: scratch_dir |

### `clips folder` {#clips-folder-usage}

Make and print a task's clips folder (\<scratch\>/\<task\>/clips).

```text
coney-tools clips folder TASK
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `TASK` | text | required | the task's scratch folder name (your branch or track) |

### `clips check` {#clips-check-usage}

Check clips as publish does (place, length, metadata), upload nothing.

```text
coney-tools clips check CLIP [CLIP ...]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `CLIP [CLIP ...]` | path | required | the clip files to check |

### `clips publish` {#clips-publish-usage}

Upload clips with gh --attach to a pull request or issue, then delete them.

Exactly one of `--pr`, `--issue` is required.

```text
coney-tools clips publish CLIP [CLIP ...] (--pr PR | --issue ISSUE) [--edit] [--body BODY | --body-file BODY_FILE]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `CLIP [CLIP ...]` | path | required | the clip files to upload |
| `--pr PR` | integer | none | the pull request's number. Not with `--issue`. |
| `--issue ISSUE` | integer | none | the issue's number. Not with `--pr`. |
| `--edit` | flag | off | put them into the description instead of a comment |
| `--body BODY` | text | none | the comment's (or description's) text. Not with `--body-file`. |
| `--body-file BODY_FILE` | path | none | a file with the text; ./clips/\<name\>.mp4 references are replaced. Not with `--body`. |

### `clips clean` {#clips-clean-usage}

Delete clips left in the scratch clips folders past the age limit (a dry run unless --apply).

```text
coney-tools clips clean [--older-than HOURS] [--apply]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--older-than HOURS` | number | none | default: workspace.clips.keep_hours (24) |
| `--apply` | flag | off | delete; without it, only list what would go |

### `clips pair` {#clips-pair-usage}

Record one run before (the merge base's build) and after (the task's), and stack them side by side.

```text
coney-tools clips pair [SCENARIO] --agent AGENT [--level LEVEL] [--input-script INPUT_SCRIPT] [--seconds SECONDS]
    [--start-frame START_FRAME] [--checkpoint CHECKPOINT] [--ai-seed AI_SEED] [--original STATE] [--gif] [--slow SLOW]
    [--prefix PREFIX] [--disc DISC] [--base BASE] [--without COMMIT] [--before REF] [--task TASK] [--renderer RENDERER]
    [--before-renderer BEFORE_RENDERER]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `[SCENARIO]` | text | none | a named scenario (`clips scenarios` lists them) |
| `--agent AGENT` | text | required | your id: the builds and the original's PCSX2 claim are yours |
| `--level LEVEL` | text | none | without a scenario: the level to play (--play-level) |
| `--input-script INPUT_SCRIPT` | path | none | without a scenario: the input script to play |
| `--seconds SECONDS` | integer | none | without a scenario: how many seconds the clips show (default 10) |
| `--start-frame START_FRAME` | integer | none | without a scenario: the frame of play the clips start at |
| `--checkpoint CHECKPOINT` | integer | none | without a scenario: the checkpoint to start at |
| `--ai-seed AI_SEED` | integer | none | without a scenario: the AI's seed |
| `--original STATE` | path | none | a .p2s state: add the original's panel first |
| `--gif` | flag | off | also write compare.gif, sized for a description |
| `--slow SLOW` | number | `1.0` | play the compare N times slower (an MP4 keeps every frame) |
| `--prefix PREFIX` | text | none | name the files PREFIX-before.mp4 and so on (several pairs) |
| `--disc DISC` | text | none | the disc for Coney; default: game_dir of coney.local.toml |
| `--base BASE` | text | none | the branch whose merge base is the before (default origin/main) |
| `--without COMMIT` | text | none | take this commit (a change already merged) out of the before, for the clips its pull request lacked |
| `--before REF` | text | none | record the before on this commit, built on the merge base, instead of --without's: the change taken out by hand where --without cannot (a later commit edits its lines) |
| `--task TASK` | text | none | the task whose clips folder takes the clips (default: the checkout's) |
| `--renderer RENDERER` | choice | none | the renderer both sides draw with (default: Coney's own, GL3). One of `gl3`, `gpu`. |
| `--before-renderer BEFORE_RENDERER` | choice | none | the renderer the before draws with, in place of --renderer (a GPU after against GL3's before). One of `gl3`, `gpu`. |

### `clips encode` {#clips-encode-usage}

Re-encode a clip for GitHub: a GIF for a .gif, else a silent MP4, fitted to the size limits.

```text
coney-tools clips encode CLIP --out OUT [--start-frame START_FRAME] [--seconds SECONDS] [--slow SLOW] [--label LABEL]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `CLIP` | path | required | the clip to encode, in a task's clips folder |
| `--out OUT` | path | required | the new file, NAME.gif or NAME.mp4: a bare name goes beside the clip |
| `--start-frame START_FRAME` | integer | `0` | the clip's frame to start at (30 a second) |
| `--seconds SECONDS` | number | none | how many seconds of the clip (default: the rest of it) |
| `--slow SLOW` | number | `1.0` | play it N times slower (an MP4 keeps every frame) |
| `--label LABEL` | text | none | a label in the top left corner |

### `clips scenarios` {#clips-scenarios-usage}

List the named scenarios clips pair records.

```text
coney-tools clips scenarios
```

Takes no parameters.
<!-- command-reference:end -->
