# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools refs ...` commands: render the game reference pages, and refresh their lists from the disc.

`refs render` reads `research/references/*.yaml`, checks each against its schema and writes `docs/references/`;
with `--check` it changes nothing and fails when a page is out of date (CI runs it). `refs extract` reads the
player's disc, merges what it finds into the YAML (keeping every hand-written field) and renders.
`refs compress-images` turns the rendered thumbnails into small palette PNGs before they are committed.

Research: docs/guides/research-workflow.md#reference-lists
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import yaml

from coney_tools import refs, refs_render
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.refs import Topic
from coney_tools.refs_topics import TOPICS, topic
from coney_tools.wad_cli import open_disc

REFS_DIR = Path("research/references")
DOCS_DIR = Path("docs/references")
IMAGES_DIR = DOCS_DIR / "images"
BINDINGS_INDEX = DOCS_DIR / "bindings/index.md"


def _yaml_path(root: Path, item: Topic) -> Path:
    """Where a topic's list lives."""
    return root / REFS_DIR / f"{item.key}.yaml"


def load_all(root: Path) -> list[refs.RefList]:
    """Every topic's list, in index order. Raises ConfigError naming each missing or invalid file."""
    lists, problems = [], []
    for item in TOPICS:
        path = _yaml_path(root, item)
        if not path.is_file():
            problems.append(f"{path.relative_to(root)}: missing (run `coney-tools refs extract`)")
            continue
        try:
            lists.append(refs.load(path, item))
        except refs.RefsError as error:
            problems.append(str(error))
    if problems:
        raise ConfigError("\n".join(problems))
    return lists


def pages(root: Path, lists: list[refs.RefList]) -> dict[Path, str]:
    """Every generated page by path: the index and one page per topic."""
    out = {root / DOCS_DIR / "index.md": refs_render.index(lists, (root / BINDINGS_INDEX).is_file())}
    out.update({root / DOCS_DIR / f"{reflist.topic.key}.md": refs_render.page(reflist) for reflist in lists})
    return out


def run_render(check: bool) -> int:
    """Write the pages, or with `check` report the stale ones and return 1."""
    root = find_repo_root(Path.cwd())
    stale = []
    for path, text in pages(root, load_all(root)).items():
        current = path.read_text(encoding="utf-8") if path.is_file() else None
        if current == text:
            continue
        stale.append(path.relative_to(root).as_posix())
        if not check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding="utf-8", newline="\n")
    if check and stale:
        print("coney-tools: out of date with research/references/ (run `coney-tools refs render`):", file=sys.stderr)
        for name in stale:
            print(f"  {name}", file=sys.stderr)
        return 1
    print(f"{len(stale)} page(s) {'stale' if check else 'written'}; {len(TOPICS)} lists")
    return 0


def _known_names(root: Path, names_file: Path | None) -> list[str]:
    """Names found before: the WAD names list, and a names file (one name per line) when given."""
    names: list[str] = []
    path = _yaml_path(root, topic("wad-names"))
    if path.is_file():
        data = yaml.safe_load(path.read_text(encoding="utf-8")) or {}
        names += [str(entry["name"]) for entry in data.get("entries") or [] if entry.get("name")]
    if names_file is not None:
        try:
            text = names_file.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"{names_file}: cannot be read ({error})") from error
        names += [line.split()[-1] for line in text.splitlines() if line.strip() and not line.startswith("#")]
    return names


def run_extract(disc_arg: str | None, only: list[str] | None, names_file: Path | None) -> int:
    """Read the disc, merge each topic's facts into its YAML (keeping hand-written fields) and render."""
    from coney_tools import refs_extract  # the disc readers are only needed here

    root = find_repo_root(Path.cwd())
    wanted = [topic(key) for key in only] if only else list(TOPICS)
    facts = refs_extract.DiscFacts(open_disc(disc_arg), _known_names(root, names_file))
    for item in wanted:
        fresh = refs_extract.EXTRACTORS[item.key](facts, root / IMAGES_DIR)
        path = _yaml_path(root, item)
        existing = refs.load(path, item) if path.is_file() else new_list(item)
        merged = refs.merge(existing, fresh)
        refs.write(path, merged)
        print(f"{item.key}: {len(fresh)} extracted, {len(merged.entries)} in the list")
    # Re-render only when every list exists (a first `--only` run leaves the others to come).
    if all(_yaml_path(root, item).is_file() for item in TOPICS):
        return run_render(check=False)
    return 0


def compress_image(path: Path) -> tuple[int, int]:
    """Rewrite one PNG as a 256-colour palette image with alpha; returns its sizes before and after.

    `coney --render-references` writes full-colour PNGs of about 26 KB each. The docs keep about 550 of them, so
    they are stored with a palette instead: under a quarter of the size, and indistinguishable at 256 pixels.
    Pillow's octree quantizer is deterministic, so the same render always gives the same bytes.
    """
    from PIL import Image  # only this command needs Pillow

    before = path.stat().st_size
    with Image.open(path) as image:
        palette = image.convert("RGBA").quantize(256, method=Image.Quantize.FASTOCTREE)
    palette.save(path, "PNG", optimize=True)
    return before, path.stat().st_size


def run_compress_images(folder: Path | None) -> int:
    """Compress every PNG below docs/references/images/ (or `folder`) in place."""
    root = find_repo_root(Path.cwd())
    base = folder if folder is not None else root / IMAGES_DIR
    files = sorted(base.rglob("*.png"))
    before = after = 0
    for path in files:
        old, new = compress_image(path)
        before, after = before + old, after + new
    print(f"{len(files)} image(s): {before // 1024} KB -> {after // 1024} KB")
    return 0


def new_list(item: Topic) -> refs.RefList:
    """An empty list with its starting prose and defaults, for a topic that has no YAML yet."""
    start = STARTERS.get(item.key, {})
    return refs.RefList(
        item,
        start.get("title", item.nav or item.key),
        start.get("about", ""),
        start.get("complete", ""),
        {"source": start.get("source", "the scripts on the disc"), "evidence": start.get("evidence", "inferred")},
    )


def topic_keys() -> list[str]:
    """The file stems `--only` accepts."""
    return [item.key for item in TOPICS]


#: The prose and defaults a list starts with. After the first extract they live in the YAML and are edited there.
STARTERS: dict[str, dict[str, Any]] = {
    "characters": {
        "title": "Characters (humans)",
        "source": "config_preload2.lua, CfgChar",
        "about": "Every character type `CfgChar` configures. A level script creates a character (a *human*) with\n"
        "`HuCreate(name, type, position, heading, ...)`, and the type picks everything below: the model, health,\n"
        "damage and attack tables, voice, hat and weapon. See [Characters](../research/characters.md#classes).\n\n"
        "Speeds are what the game uses in play: each clip's root displacement over its length, for the clip the\n"
        "type's character data plays in the walk, jog, run and sprint slots (the generic clip when it has none of\n"
        "its own). The [speed class](speed-classes.md) is only a fallback.",
        "complete": "Every type `CfgChar` configures (449) is listed with every argument. The labels (who a type is)\n"
        "are hand-written and still sparse; the meaning of `v11d` and `flag_14b` is not traced.",
    },
    "character-models": {
        "title": "Character models",
        "source": "warriors.glr, Character List (chunk 0x44)",
        "evidence": "confirmed-code",
        "about": "The Character List in `warriors.glr`: one record per character model, naming its model, texture\n"
        "dictionary and character data (animations) by hash ([Characters](../research/characters.md#files)). A\n"
        "`<model>_a` record is the variant used in levels 60-64.",
        "complete": "Every record (543) is listed. Names are recovered by hashing candidate strings; a record\n"
        "without a name has none of its candidates on the disc yet.",
    },
    "gangs": {
        "title": "Gangs",
        "source": "config_preload2.lua, CfgGang",
        "about": "Gang types: `CfgGang(type, ...)` configures each, `GangCreate(type, name)` creates a gang in a\n"
        "level script and `CfgGangMusic` gives it three music tracks. Humans join a gang when they are created.",
        "complete": "Every gang type (25) is listed with its arguments; the meaning of the byte values is not traced.\n"
        "Labels are hand-written.",
    },
    "speed-classes": {
        "title": "Speed classes",
        "source": "config_preload2.lua, CfgSpeedClass",
        "about": "The fallback speed table `CfgSpeedClass` fills at `0x006b6548` (six floats per class). In play the\n"
        "game uses each human's own clip speeds instead ([Characters](../research/characters.md#speed-classes)).",
        "complete": "All five classes are listed.",
    },
    "objects": {
        "title": "Objects and weapons",
        "source": "config_preload2.lua and config_preload3.lua, CfgObj",
        "about": "Every object type `CfgObj` configures: weapons, hats and masks, pick-ups, doors and props.\n"
        "The name is also the model's name and what scripts pass to create the object.",
        "complete": "Every object type (1,371) is listed with its class, type, physics shape, size and animations.\n"
        "The category is our grouping by class. Arguments 2-5, 11 and 14-17 are not traced.",
    },
    "object-groups": {
        "title": "Object groups",
        "source": "config_preload2.lua, CfgChar",
        "about": "The item groups a character type may carry and drop (`CfgChar`'s drop group).",
        "complete": "Every group named by a character type is listed. No script on the disc calls `CfgObjectGroup`,\n"
        "so where the groups are defined is not known.",
    },
    "levels": {
        "title": "Levels",
        "source": "config_preload3.lua, levelNames",
        "about": "The 111 level records of `levelNames` (`CfgLevelName`), in the order the game keeps them.\n"
        "A level loads from `<name>.lev` and its section packs `<name>_<k>.pak`\n"
        "([Level loading](../research/level-loading.md)).",
        "complete": "Every record is listed with every field. `.lev` and pack counts come from the WAD names list,\n"
        "so a level whose files have no recovered name shows none. Kinds are hand-written: only the front end\n"
        "and the test levels so far.",
    },
    "animations": {
        "title": "Animation clips",
        "source": "character data and .anm resources, clip descriptors",
        "evidence": "confirmed-code",
        "about": "Every distinct animation clip on the disc: its name (from its descriptor), length, root\n"
        "displacement and where it is found ([Animation](../research/formats/animation.md)). Clips that share a\n"
        "name but differ are numbered `#2`, `#3` ...",
        "complete": "Every clip (1,875) is listed. Resource names are recovered by hashing; unnamed ones show as hex.",
    },
    "anim-ids": {
        "title": "Anim ids",
        "source": "royal.lua (names); character data (clips); 0x005105d8 (slots)",
        "about": "The 722 anim ids every character data maps to clips. A character's own slot wins; an unset slot\n"
        "plays the generic character data's clip ([Characters](../research/characters.md#files)). Scripts name ids\n"
        "with the `ANIM_*` constants of `royal.lua`; the locomotion slots are\n"
        "[Characters, Anim slots](../research/characters.md#anim-slots).",
        "complete": "All 722 ids are listed with the generic clip and Rembrandt's own clip where he has one. Names\n"
        "exist only for the ids `royal.lua` defines.",
    },
    "controls": {
        "title": "Controls",
        "source": "pad record and libpad layout (docs/research/frontend.md#input)",
        "about": "The pad as the game reads it: the 16-bit button word, the analog sticks, and the markup tag that\n"
        "draws each button in text. Coney drives everything as a gamepad with analog sticks; on a PC pad the face\n"
        "buttons are positional (south is cross).",
        "complete": "Every button bit and both sticks are listed. What each does on foot is still to be written;\n"
        "the front end's accept and back are known ([Front end](../research/frontend.md#input)).",
    },
    "hud-colours": {
        "title": "HUD colours",
        "source": "config_preload2.lua, CL and CfgHUDColor",
        "about": "The colour table `CL` scripts use in text (`<COLOR rrggbbaa>`) and the HUD slots `CfgHUDColor`\n"
        "assigns from it.",
        "complete": "Every key of `CL` is listed.",
    },
    "text-formatting": {
        "title": "Text formatting",
        "source": "SLUS_212.15, tag table 0x0050d718",
        "evidence": "confirmed-code",
        "about": "The markup tags text strings may carry, in the order of the executable's tag table\n"
        "([GUI, Markup tags](../research/gui.md#markup)). A tag with a trailing space takes an argument.",
        "complete": "All 66 tags are listed; the effects are written from the layout code.",
    },
    "enums": {
        "title": "Script enums",
        "source": "the preload scripts and global.lua",
        "about": "Constant tables (`MATERIAL.GLASS`) and loose constants (`LT_PLAY`) the preload scripts and\n"
        "`global.lua` define, as mods and level scripts pass them to bindings. The 722 `ANIM_*` ids are in\n"
        "[Anim ids](anim-ids.md).",
        "complete": "Every numeric constant of those scripts is listed. Meanings are hand-written and sparse.",
    },
    "sound": {
        "title": "Sound and music",
        "source": "config_preload.lua and config_preload2.lua",
        "about": "The music tracks `SndCfgMusicInfo` configures, the interface sounds of `SoundCfgInterfaceSound`,\n"
        "the sound matrices scripts load and the sounds of inventory items. Gang music is in [Gangs](gangs.md).",
        "complete": "Every configured sound is listed by name; the two numbers of a music track are not traced.",
    },
    "script-events": {
        "title": "Script events",
        "source": "every script, SetMsgHandler",
        "about": "The messages scripts subscribe to with `SetMsgHandler(object, message, callback)`. The meaning of\n"
        "a message is read from the names of the callbacks scripts give it.",
        "complete": "Every message number a script uses is listed; meanings are inferred from callback names.",
    },
    "wad-names": {
        "title": "WAD entry names",
        "source": "name recovery (coney-tools wad names and the WAD survey)",
        "evidence": "confirmed-code",
        "about": "The file names recovered for `WARRIORS.WAD`'s entries. The archive stores only a CRC-32 of\n"
        "`./ee_files/<name>` ([WARRIORS.DIR](../research/formats/wad-dir.md)), so a name is known once a candidate\n"
        "string hashes to an entry. Each name here was checked against its hash.",
        "complete": "3,990 of 10,701 names are known; the streamed world and most early entries are still unnamed.",
    },
}
