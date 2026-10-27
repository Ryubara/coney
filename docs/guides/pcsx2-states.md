# PCSX2 states

A save state is the quickest way back to a place in the original: the level loaded, the player standing in front of a
fence, a pedestrian in reach. Every engineer needs the same few places again and again (a pedestrian to grab, a fence
to jump, a window to break), and driving the original to each one costs minutes and a PCSX2 copy. So the states are
kept in one shared library, with a catalog that says what each one shows and what it is set up for. **Check the library
first; add any state you make that others could use.**

The library is a folder beside the repository, never in it: a save state is a snapshot of the game's memory, and
nothing of the game goes into git ([LEGAL.md](repo:LEGAL.md#no-game-data)). Its path is `pcsx2_states_dir` in
`coney.local.toml` ([the workspace](workspace.md)); without it, `state-library` in the workspace's `emulators/` folder
(`<clone>-workspace/emulators/` for a plain clone). One workspace has one library, whichever worktree you are in. Only
the names and our own descriptions below are in the repository, no state data.

## Using a state

```sh
uv run --project python coney-tools pcsx2 states list [--level LEVEL] [--tag TAG]
uv run --project python coney-tools pcsx2 states show ID
uv run --project python coney-tools pcsx2 claim --agent ME
uv run --project python coney-tools pcsx2 states hand-out ID --copy COPY --agent ME [--patch NAME ...] [--launch]
```

`hand-out` writes a **copy** of the state into the claimed copy's `library-states/` folder (the library's file is
checked against its hash first and is never played on), with any patches your recipe needs applied
([`states patch`](coney-tools-pcsx2.md#pcsx2) does the same for a file of your own), and prints its path. With
`--launch` it also starts PCSX2 on it. Then run recipes, record a trace or take pictures as usual
([Driving PCSX2](research-tools.md#driving-pcsx2)). A state that already holds a patch lists it in `show`; do not
apply that patch again, the copy would be refused.

A recipe that needs a particular scene names its state among its preconditions ("start state" in
[PCSX2 recipes](pcsx2-recipes.md)). The first states were saved by PCSX2 2.9.94 and the copies run the 2.9.129 fork;
it loaded two of them (checked 2026-10-21, `pedestrian-grab` and `window-run-jump`, and read the same level, checkpoint
and position the catalog has). `show` says which version saved each one; a state PCSX2 ever refuses is made again from
its `made_by` steps.

## Adding a state

Got a place others would want? Save it, then add it, whoever you are:

1. Get the game there, with a recipe or by hand, and note the steps: they go into the entry, so the state can be made
   again when a PCSX2 update refuses it. Save the state with the debug server's `pcsx2_save_state` to a file in your
   scratch folder (never a quick-save slot, and move any `.p2s` PCSX2 puts in `sstates/` out of it).
2. Add it:

   ```sh
   uv run --project python coney-tools pcsx2 states add FILE --id pedestrian-grab --tag grab --tag pedestrian \
       --desc "Facing a pedestrian: set up for grabs, attacks and a robbery." \
       --made-by "claimed copy, level99 street, walked to the first pedestrian with the position recipe" --author ME
   ```

   `FILE` is a `.p2s` file, or `slot:N` for quick-save slot N (only read). The **id** is a short slug that is also the
   file's name, and is never reused. The **description** says what the scene shows and what it is set up for, in our
   own words. The level, checkpoint, the player's position and heading come from the state itself: `add` runs the
   [`where`](pcsx2-recipes/levels-and-missions.md#where) and
   [`position`](pcsx2-recipes/moving-and-editing-humans.md#position) recipes over the state's own memory, which gives
   what the game held when it was saved. With `--copy COPY --agent ME` it reads a running
   copy over PINE instead, when you hold the claim (the copy must be running the state you add, as the level and
   position are the live game's, while the version, date and patches are the file's); flags (`--level`,
   `--checkpoint`, `--position`, `--heading`) win over both, and `unknown` is allowed for a level or a checkpoint
   nobody can tell. The PCSX2 version and the date come
   from the file, and the patches it holds (`scripted-pad`, `call-brains`, ...) are found by comparing its memory with
   `research/traces/patches/` (a patch that a later one overwrote in part is not listed; `--no-read` skips all of
   this, and then "patches: none" means unchecked).
3. Check it with `states verify`.

`add` refuses an id that exists and a library folder inside a repository checkout, copies the file in with its
SHA-256 checked, and makes the copy read-only. A wrong
entry is not edited in place: add the corrected state under a new id and ask a maintainer to remove the old one (delete
its file and its catalog row together). `states verify [ID ...]` hashes the files against the catalog, and lists any
file that is missing, changed or not in the catalog; it exits 1 when it finds one.

## The catalog

`catalog.json` in the library folder holds one entry per state. Each has:

- `id`, `level` and `checkpoint` (the number the game holds, the one `SetCheckPoint` takes; `unknown` and null when
  nobody has read it);
- `description` (what the scene shows, what it is set up for) and `tags` (to find it: `fence`, `window`, `grab`, ...);
- `position` (x, y, z in metres) and `heading_deg` (0 faces +y, anticlockwise) of the player;
- `pcsx2` (the version that saved the file) and `patches` (the patches and hooks its memory holds);
- `author`, `made` and `added` (who, when it was made, when it went in);
- `sha256` and `size` of the file;
- `made_by`: the recipe or steps that produced it.

## The states

This list is written by `coney-tools pcsx2 states render` from the catalog on the maintainer's machine, so it holds
what was in the library when it was last rendered; `states list` shows the library you have. The first ten are the
maintainer's quick-save slots 1 to 10, copied in unchanged on 2026-10-21 (the slots themselves are never touched);
every one is in level 99. The checkpoint column is what the game's state held when the file was saved (the maintainer
called slot 1 checkpoint 3; it reads 2).

<!-- states:begin -->

| Id | Level | Checkpoint | Shows, and set up for | Tags |
| --- | --- | --- | --- | --- |
| `street-end-run` | level99 | 2 | The end of the level99 street with a clear path ahead: set up for running. | street, run |
| `vehicle-side-window` | level99 | 2 | Facing the side of a vehicle: set up to hit its window and to climb onto it. | vehicle, window, climb |
| `store-door-no-lockpick` | level99 | 2 | At a store door with no lockpick: set up for the locked-door checks. | door, store, lockpick |
| `glass-shelf-loot` | level99 | 2 | At a glass shelf with loot behind the glass: set up to break the glass and take the loot. | glass, loot, store |
| `vehicle-front-radio` | level99 | 2 | Facing the front window of a vehicle with a radio in it: set up to break the window and take the radio. | vehicle, window, loot |
| `pedestrian-grab` | level99 | 2 | Facing a pedestrian: set up for grabs, attacks and a robbery. | pedestrian, grab, attack, robbery |
| `fence-run-jump` | level99 | 3 | In front of a fence: set up to run up to it and jump over it. | fence, jump, traversal |
| `trash-can-roof` | level99 | 3 | At a trash can: set up to climb onto it and from it onto the roof. | climb, traversal |
| `window-run-jump` | level99 | 3 | Set up for a run and jump through a window. | window, jump, traversal |
| `breakable-fence-flash` | level99 | 3 | In front of a breakable fence (L2 to sprint, X to charge through it), with the flash dealer beyond. | fence, charge, flash |

<!-- states:end -->
