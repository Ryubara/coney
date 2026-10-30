# The coney-tools picture commands

The commands that compare pictures with a tolerance: [`picture`](#picture) counts the pixels two pictures differ by and
checks Coney's synthetic scenes against reference pictures. The other groups are listed in
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
```

Draws the synthetic scenes of `tests/pictures/scenes.toml` and compares each with the reference of the same name in
`tests/pictures/reference/`. A scene is one `coney` command line that needs no game data: today the sandbox's layouts
(Coney's own worlds, drawn with CC0 grid textures) at a pinned view, three frames at 320x224. The list holds the
tolerance (`channel_tolerance`, `max_share_percent`) and each scene's `name`, `args`, `frames` and `render_size`; the
command adds `--frames`, `--render-size` and `--screenshot` itself. The drawn pictures go to `--out`, which stays outside
the repository. `--coney` defaults to `build/dev/src/platform/coney`; `--launcher` puts a command before it, which is how
CI runs Coney under a virtual display in a container (`xvfb-run`, `docker run`). Coney runs in the repository root, so a
path in a scene's `args` and a relative `--coney` mean the same from any folder of the checkout; `--out` is read from
the folder you start the command in.

* **`--repeat N`** draws every scene N times and compares each repeat with the first draw: that is the measurement of
  one build's repeated captures that sets the tolerance, and a repeat past the limit fails the run.
* **`--update`** replaces the references with what was drawn, after printing the counts against the old ones (a missing
  reference is made and says `new reference`). It is for the pull request that moves the Mesa pin, which reports the old
  and the new counts ([The software pictures](testing.md#software-pictures)).
* **`--allow-no-references`** is the bootstrap: when `tests/pictures/reference/` holds no picture at all, the scenes are
  still drawn (and kept in `--out`, and drawn again for `--repeat`), the run says `no references yet ... nothing was
  compared` and exits 0 unless the repeats differ. CI passes it until the first references are added from its
  uploaded pictures ([The software pictures](testing.md#software-pictures)); the task that adds them drops it. A folder
  with some references but not all of them still fails.
* A scene whose reference is missing fails (`no reference`), and a run of Coney that fails or draws nothing ends the
  command with exit code 2 and the last lines of its output.

Disc frames are compared with `picture compare`: take the named frames from the branch and from its base commit with
`--screenshot` at `--render-size 640x448` and a pinned camera ([Matched views](running-play-level.md#matched-views)),
one machine, two folders, and keep the output in the scratch folder.

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
    [--repeat N] [--update] [--allow-no-references] [--channel-tolerance N] [--max-share PERCENT]
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
<!-- command-reference:end -->
