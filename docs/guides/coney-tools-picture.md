# The coney-tools picture commands

The commands that compare pictures with a tolerance: [`picture`](#picture) counts the pixels two pictures differ by,
checks Coney's synthetic scenes against reference pictures and runs the GPU renderer's [parity suite](#parity) on the
disc's frames. The other groups are listed in
[the command line overview](coney-tools.md#groups).

## picture {#picture}

A GPU's output differs between GPUs and drivers, and CI has no GPU, so no test hashes a picture a GPU drew: pictures
are compared with a **tolerance**, a per-channel one and a share of the pixels allowed over it. A pixel is *over* the
tolerance when any of its four channels (red, green, blue, alpha) differs by more than `--channel-tolerance` (of 255),
and a picture passes when at most `--max-share` percent of its pixels are over. The limits are 2 of 255 and 0.1 %,
marked stand-ins until the values measured from repeated captures replace them
([How pictures are compared](../engine/renderer.md#comparing) has the design and the measured values).

```sh
uv run --project python coney-tools picture compare FIRST SECOND [--channel-tolerance N] [--max-share PERCENT] [--diff-out DIR]
```

Compares two PNG files, or two folders of them (paired by their path inside the folder), and prints one line for each
picture and a total:

```text
sandbox-default-start.png: 31 of 71680 pixels over 2 (0.043 %), largest difference 9: ok
2 pictures: 31 pixels over 2, largest difference 9; limit 0.1 % of the pixels of each picture: ok
```

The counts are the pixels over the tolerance and the largest difference in any channel; they are what a pull request
that changes drawing reports ([Picture counts](testing.md#picture-counts)). It exits with 0 when every picture is within
the limit, 1 when one is past it, has a different size or is in one folder only, and 2 when a path cannot be read.
`--diff-out DIR` writes `NAME.diff.png` for each picture with a pixel over the tolerance: the first picture dimmed,
the pixels over in magenta, so a reader sees where they are.

```sh
uv run --project python coney-tools picture scenes --out DIR [--coney EXE] [--launcher COMMAND] [--scenes FILE]
    [--references DIR] [--repeat N] [--update] [--allow-no-references] [--channel-tolerance N] [--max-share PERCENT]
    [--timeout SECONDS] [--only NAME]...
```

Draws the synthetic scenes of `tests/pictures/scenes.toml` and compares each with the reference of the same name in
`tests/pictures/reference/`. A scene is one `coney` command line that needs no game data: the sandbox's layouts
(Coney's own worlds, drawn with CC0 grid textures) at a pinned view, and the 2D [test
cards](running-viewers.md#test-cards), three frames at 320x224. The list holds the tolerance (`channel_tolerance`,
`max_share_percent`), the looser one across renderers (`cross_channel_tolerance`, `cross_max_share_percent`, the
first two when unset) and each scene's `name`, `args`, `frames`, `render_size` and, for a scene drawn by more than one
renderer, `renderers` (two or more of `gl3` and `gpu`); the command adds `--frames`, `--render-size`, `--screenshot`
and `--renderer` itself. The drawn pictures go to `--out`, which stays outside
the repository. `--coney` defaults to `build/dev/src/platform/coney`; `--launcher` puts a command before it, which is how
CI runs Coney under a virtual display in a container (`xvfb-run`, `docker run`). Coney runs in the repository root, so a
path in a scene's `args` and a relative `--coney` mean the same from any folder of the checkout; `--out` is read from
the folder you start the command in.

* **`--repeat N`** draws every scene N times and compares each repeat with the first draw: that is the measurement of
  one build's repeated captures that sets the tolerance, and a repeat past the limit fails the run.
* **`--update`** replaces the references with what was drawn, after printing the counts against the old ones (a missing
  reference is made and says `new reference`). It is for the pull request that moves the Mesa pin, which reports the old
  and the new counts ([The software pictures](testing.md#software-pictures)).
* **`--allow-no-references`** is the bootstrap for a folder with no reference yet: when `tests/pictures/reference/`
  holds no picture at all, the scenes are still drawn (and kept in `--out`, and drawn again for `--repeat`), the run
  says `no references yet ... nothing was compared` and exits 0 unless the repeats differ. CI does not pass it: its
  references are in the repository ([The software pictures](testing.md#software-pictures)), so a scene without one
  fails. A folder with some references but not all of them fails either way.
* **Coney's output** of every run (standard output, then standard error) is kept beside its picture as `NAME.log`
  (`repeats/N/NAME.log` for a repeat), whether the run drew, failed or hung, so CI's uploaded pictures carry the log of
  each scene.
* **`--timeout SECONDS`** (default 180) is how long one run of Coney may take before it counts as hung: the run is
  stopped, and the command ends with exit code 2, naming the scene and showing the last lines Coney printed (all of
  them in its `NAME.log`). A few frames of a sandbox take seconds, so CI passes 60.
* **A scene with `renderers`** is drawn once by each (`--renderer`), as `NAME.gl3.png` and `NAME.gpu.png`, and each
  picture after the first is compared with the first by the cross-renderer tolerance (`NAME: gpu against gl3: ...`,
  totalled on an `across renderers:` line); it has no reference, and `--update` leaves it alone. A run asked for `gpu`
  whose output does not say `renderer: gpu on` (Coney fell back to GL3) ends the command with exit code 2.
* **`--only NAME`** (repeatable) draws only the scenes it names, such as the test cards on a real GPU; a name the list
  does not hold ends the command with exit code 2.
* A scene whose reference is missing fails (`no reference`), and a run of Coney that fails or draws nothing ends the
  command with exit code 2, its log's path and the last lines of its output.

Disc frames are compared with `picture compare`: take the named frames from the branch and from its base commit with
`--screenshot` at `--render-size 640x448` and a pinned camera ([Matched views](running-play-level.md#matched-views)),
one machine, two folders, and keep the output in the scratch folder.

### The parity suite {#parity}

```sh
uv run --project python coney-tools picture parity --out DIR [--coney EXE] [--launcher COMMAND] [--disc DISC]
    [--only NAME]... [--diff-out DIR] [--channel-tolerance N] [--max-share PERCENT] [--timeout SECONDS]
```

The disc-backed half of the picture checks, run on a machine with the disc and a GPU: the SDL3 GPU renderer's whole
frames against GL3's, which is its reference until the switch ([Migration](../engine/renderer.md#migration)). Each frame
is drawn twice, with `--renderer gl3` and with `--renderer gpu`, saved with `--screenshot` as `NAME.gl3.png` and
`NAME.gpu.png` in `--out` (with each run's output as `NAME.RENDERER.log`), and the GPU's picture compared with GL3's by
the cross-renderer tolerance of `tests/pictures/scenes.toml` (`cross_channel_tolerance`, `cross_max_share_percent`;
`--channel-tolerance` and `--max-share` override them). The frames are whole, effects and screen passes included, not
`--scene-only`:

| Frame | What it is |
| --- | --- |
| `hub`, `hub-crowd`, `street`, `hub-640`, `hub-2560` | each [benchmark scene](../engine/renderer-performance.md#benchmark) that draws (the two headless ones measure the simulation and draw nothing): `perf bench`'s own command, stopped at frame 300, after the crowd spawns and the 60 frames of warm-up, so a scene is the same work in both tools |
| `level99-street`, `level95-hub` | the named `--play-level` frames of [the draw-stream hashes](../engine/renderer-pictures.md#comparing) (`--freeze-world`, 640x448, 90 frames) |
| `level99-line-blend` | `level99-street` with `--line-blend on`, so a screen pass is in a level frame |

It prints a Markdown table, one row each (the pixels over the tolerance, their share, the largest difference and `within`
or `past the limit`), and a summary line, which is what a pull request pastes
([Picture counts](testing.md#picture-counts)). A frame whose two pictures are each one colour all over says `blank`: two
blank pictures match and prove nothing (`level95-hub` is black in every build). `--only NAME` (repeatable) draws just the
frames it names and `--diff-out DIR` writes `NAME.diff.png` (the picture dimmed, the pixels over in magenta) for each
frame with a pixel over.

A run that fails, hangs or draws nothing, and a GPU run whose log does not say `renderer: gpu on` (Coney fell back to
GL3), end the command with exit code 2, as in `picture scenes`: a comparison never passes by comparing GL3 with itself.
It exits 0 when every frame is within the limit and 1 when one is past it. `--coney` defaults to the playtest build's
(`--bench` needs its debug tools) and `--disc` to `game_dir` of `coney.local.toml`. The pictures are game frames: they
stay in the scratch folder and are never committed. What the suite found, and the differences left with their reasons,
are on [Renderer pictures](../engine/renderer-pictures.md#parity).

<!-- command-reference:begin picture -->
## Parameters of `picture` {#picture-parameters}

Written by `coney-tools docs render` from the command line's own definitions; never edited by hand.

### `picture compare` {#picture-compare-usage}

Count the pixels over the tolerance and the largest difference; exit 1 past the limit.

```text
coney-tools picture compare FIRST SECOND [--channel-tolerance N] [--max-share PERCENT] [--diff-out DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `FIRST` | path | required | a PNG, or a folder of them |
| `SECOND` | path | required | a PNG, or a folder of them (pictures are paired by name) |
| `--channel-tolerance N` | integer | none | the most a channel may differ, of 255, before its pixel counts as over (default 2) |
| `--max-share PERCENT` | number | none | the share of a picture's pixels allowed over the tolerance, in percent (default 0.1) |
| `--diff-out DIR` | path | none | write NAME.diff.png here for each picture that has pixels over |

### `picture scenes` {#picture-scenes-usage}

Draw the synthetic scenes with coney and compare them with their reference pictures.

```text
coney-tools picture scenes [--coney CONEY] [--launcher COMMAND] [--scenes SCENES] [--references REFERENCES] --out OUT
    [--repeat N] [--update] [--allow-no-references] [--channel-tolerance N] [--max-share PERCENT] [--timeout SECONDS]
    [--only NAME]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--coney CONEY` | text | `build/dev/src/platform/coney` | the coney executable (default build/dev/src/platform/coney) |
| `--launcher COMMAND` | text | none | a command put before coney, such as 'xvfb-run -a' |
| `--scenes SCENES` | path | none | the scene list (default tests/pictures/scenes.toml) |
| `--references REFERENCES` | path | none | the reference pictures (default tests/pictures/reference) |
| `--out OUT` | path | required | where the drawn pictures go (outside the repository) |
| `--repeat N` | integer | `1` | draw each scene N times and compare the repeats with the first |
| `--update` | flag | off | replace the references with the drawn pictures, after printing the counts |
| `--allow-no-references` | flag | off | pass, with a notice, when the references folder holds no picture yet; the scenes are still drawn |
| `--channel-tolerance N` | integer | none | override the scene list's tolerance |
| `--max-share PERCENT` | number | none | override the scene list's share |
| `--timeout SECONDS` | number | `180` | how long one run of coney may take before it counts as hung (default 180) |
| `--only NAME` | text | none | draw only this scene (repeatable; default all) |

### `picture parity` {#picture-parity-usage}

Draw the benchmark scenes and the named play-level frames with both renderers and compare them whole.

```text
coney-tools picture parity [--coney CONEY] [--launcher COMMAND] [--disc DISC] --out OUT [--channel-tolerance N]
    [--max-share PERCENT] [--timeout SECONDS] [--only NAME] [--diff-out DIR]
```

| Parameter | Type | Default | Meaning |
| --- | --- | --- | --- |
| `--coney CONEY` | text | none | the coney executable (default: the playtest build's, as perf bench: --bench needs the debug tools) |
| `--launcher COMMAND` | text | none | a command put before coney, such as 'xvfb-run -a' |
| `--disc DISC` | text | none | the disc; default: game_dir from coney.local.toml |
| `--out OUT` | path | required | where the pictures and logs go (outside the repository) |
| `--channel-tolerance N` | integer | none | override the cross-renderer tolerance |
| `--max-share PERCENT` | number | none | override the cross-renderer share |
| `--timeout SECONDS` | number | `600` | how long one run of coney may take before it counts as hung (default 600) |
| `--only NAME` | text | none | draw only this frame (repeatable; default all) |
| `--diff-out DIR` | path | none | write NAME.diff.png here for each frame that has pixels over |
<!-- command-reference:end -->
