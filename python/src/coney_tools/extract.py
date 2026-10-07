# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools extract <disc> <out-dir>`: every asset of the player's disc, in open formats, in a folder outside the
repository.

The folder is what Coney's install step will produce from the player's disc and what the engine and mods will
read. Each asset type is a stage with its own top folder (`textures/`, `audio/` ...); `manifest.json` lists every
file with its SHA-256, and per type a count and a digest, so a run can be checked against the expected counts
(`--verify`) and two runs compared. The output is deterministic: the same disc gives the same bytes.

Research: docs/research/formats/inventory.md (every file type, where it is documented and how it is extracted)
"""

from __future__ import annotations

import hashlib
import os
import sys
import time
from pathlib import Path

from coney_tools import (
    extract_anims,
    extract_audio,
    extract_data,
    extract_levels,
    extract_models,
    extract_raw,
    extract_scenes,
    extract_wad,
    extract_worlds,
    lua4,
    movies,
    refs_extract,
    wad,
)
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.disc import Disc
from coney_tools.extract_output import Output, Report
from coney_tools.extract_types import TYPES
from coney_tools.wad_cli import open_disc

#: The WAD stages, in the order they see each entry.
_WAD_TYPES = ("index", "scripts", "textures", "models", "animations", "levels", "worlds", "scenes", "data", "raw")
#: Disc files other stages read; `disc` copies the rest.
_HANDLED_FILES = {wad.WAD_FILE, wad.DIR_FILE, "IOP/BFW.SND", "IOP/MUSIC.SND"}

#: Expected per-type file counts for the NTSC-U disc (SLUS_212.15, SHA-1 e9cb2cc4...); `--verify` checks them.
EXPECTED: dict[str, dict[str, int]] = {
    "e9cb2cc49aa046b9e494313dce2f5038ed17b2f4": {
        "disc": 10,
        "movies": 17,
        "audio": 25743,
        "scripts": 468,
        "textures": 26309,
        "models": 2955,
        "animations": 1929,
        "levels": 1025,
        "worlds": 825,
        "scenes": 2766,
        "data": 79,
        "index": 1,
        "raw": 10,
    },
}
_PIECE = 8 << 20


def _sha1(disc: Disc, name: str) -> str:
    """SHA-1 of one disc file, streamed."""
    digest = hashlib.sha1()
    with disc.open(name) as handle:
        for piece in wad.read_chunks(handle, disc.size(name), _PIECE):
            digest.update(piece)
    return digest.hexdigest()


def extract_disc_files(disc: Disc, output: Output) -> Report:
    """Copy the disc files no other stage reads (the executable, the IOP modules and image, SYSTEM.CNF) into
    `disc/`, and list every file of the disc with its size and SHA-1 in `disc/files.json`."""
    report = Report("disc")
    output.start("disc")
    listing = []
    for name in disc.names():
        record: dict[str, object] = {"name": name, "size": disc.size(name)}
        folder = name.split("/", 1)[0] if "/" in name else ""
        if name in _HANDLED_FILES or folder == movies.MOVIE_FOLDER:
            record["extracted_by"] = "movies" if folder == movies.MOVIE_FOLDER else "audio, index and others"
        else:
            with disc.open(name) as handle:
                pieces = wad.read_chunks(handle, disc.size(name), _PIECE)
                record["file"] = output.write_stream("disc", f"disc/{name}", pieces)
            report.count("files copied")
        record["sha1"] = _sha1(disc, name)
        listing.append(record)
    output.write_json("disc", "disc/files.json", {"files": listing})
    return report


def extract_movies(disc: Disc, output: Output) -> Report:
    """Copy every `PSS/*.BIK` as is, with its header values in `movies/index.json`."""
    report = Report("movies")
    output.start("movies")
    listing = []
    for name in disc.names():
        if not name.startswith(f"{movies.MOVIE_FOLDER}/") or not name.endswith(".BIK"):
            continue
        size = disc.size(name)
        with disc.open(name) as handle:
            head = handle.read(64)
            handle.seek(0)
            head = handle.read(movies.header_size(head))
        record: dict[str, object] = {"name": name, "size": size}
        try:
            header = movies.parse_header(head, size, name)
        except movies.MovieError as error:
            report.problem(str(error))
        else:
            record.update(
                {
                    "width": header.width,
                    "height": header.height,
                    "frames": header.frames,
                    "fps": [header.fps_num, header.fps_den],
                    "audio": [{"rate": t.rate, "channels": t.channels, "codec": t.codec} for t in header.audio],
                }
            )
        with disc.open(name) as handle:
            pieces = wad.read_chunks(handle, size, _PIECE)
            record["file"] = output.write_stream("movies", f"movies/{name.split('/', 1)[1].lower()}", pieces)
        listing.append(record)
        report.count("movies")
    output.write_json("movies", "movies/index.json", {"movies": listing})
    return report


def _known_names() -> list[str]:
    """Names found before (the WAD names list of the checkout, when there is one) and the streamed world's names,
    which the world loader builds from the level names (docs/research/world.md#file-names)."""
    names: list[str] = []
    try:
        from coney_tools import refs_cli  # the reference lists live in the checkout

        names += refs_cli.known_names(find_repo_root(Path(__file__).parent), None)
    except ConfigError:
        pass
    worlds = [f"level{n}{side}" for n in range(200) for side in ("", "s", "d")] + ["objarena"]
    for world in worlds:
        names += [f"{world}_sec.wld", f"{world}_sec.mem", *(f"{world}_ms{i}.sec" for i in range(1, 200))]
    return names


def _note(message: str) -> None:
    """Progress on stderr."""
    print(f"coney-tools: {message}", file=sys.stderr, flush=True)


def run(disc_arg: str | None, out: Path, only: list[str] | None, verify: bool, workers: int | None) -> int:
    """Extract the types asked for (all by default) into `out`; print the summary; with `verify`, compare the counts
    with the expected ones and return 1 on a difference."""
    wad.refuse_inside_repo(out)
    wanted = list(TYPES) if not only else [t for t in TYPES if t in only]
    unknown = sorted(set(only or ()) - set(TYPES))
    if unknown:
        raise ConfigError(f"--only {', '.join(unknown)}: not an asset type (one of {', '.join(TYPES)})")
    disc = open_disc(disc_arg)
    output = Output(out)
    started = time.monotonic()
    identity = {name: _sha1(disc, name) for name in (wad.ELF_FILE, wad.DIR_FILE) if disc.has(name)}
    reports: list[Report] = []
    facts = refs_extract.DiscFacts(disc, _known_names())
    if "disc" in wanted:
        _note("copying the disc's other files")
        reports.append(extract_disc_files(disc, output))
    if "movies" in wanted:
        _note("copying the movies")
        reports.append(extract_movies(disc, output))
    wad_types = [t for t in _WAD_TYPES if t in wanted]
    if wad_types:
        _note("naming the WAD's entries (a full pass over the disc)")
        names = facts.names
        stages: list[extract_wad.Stage] = []
        for kind in wad_types:
            if kind == "index":
                stages.append(extract_wad.IndexStage(output))
            elif kind == "scripts":
                stages.append(extract_wad.ScriptsStage(output))
            elif kind == "textures":
                stages.append(extract_wad.TexturesStage(output))
            elif kind == "models":
                textures = next((s for s in stages if isinstance(s, extract_wad.TexturesStage)), None)
                stages.append(extract_models.ModelsStage(output, textures))
            elif kind == "animations":
                stages.append(extract_anims.AnimationsStage(output))
            elif kind == "levels":
                textures = next((s for s in stages if isinstance(s, extract_wad.TexturesStage)), None)
                stages.append(extract_levels.LevelsStage(output, textures))
            elif kind == "worlds":
                textures = next((s for s in stages if isinstance(s, extract_wad.TexturesStage)), None)
                stages.append(extract_worlds.WorldsStage(output, textures))
            elif kind == "scenes":
                stages.append(extract_scenes.ScenesStage(output))
            elif kind == "data":
                stages.append(extract_data.DataStage(output, lambda h: names.get(h) or facts.resource_name(h)))
            elif kind == "raw":
                stages.append(extract_raw.RawStage(output))
        _note(f"reading the WAD for {', '.join(wad_types)}")
        extract_wad.walk(disc, facts.entries, names, facts.resource_name, stages)
        reports += [stage.report for stage in stages]
    if "audio" in wanted:
        _note("decoding the audio")
        strings = set(facts.strings) if wad_types else _lua_strings(facts)
        jobs = workers if workers is not None else max(1, min(4, (os.cpu_count() or 2) - 1))
        reports.append(extract_audio.extract(disc, output, strings, facts.speech_command_names, jobs))
    output.save_manifest(identity)
    _note(f"done in {time.monotonic() - started:.0f} s")
    _print_summary(output, reports, wanted)
    if verify:
        return _verify(output, identity, wanted)
    return 0


def _lua_strings(facts: refs_extract.DiscFacts) -> set[str]:
    """Every string constant of every compiled script (without the names pass `facts.strings` adds)."""
    found: set[str] = set()
    for entry in facts.entries:
        data = facts.read(entry)
        if data.startswith(lua4.HEADER[:4]):
            try:
                found.update(s for s in lua4.all_strings(lua4.parse_chunk(data)) if s)
            except lua4.LuaError:
                continue
    return found


def _print_summary(output: Output, reports: list[Report], wanted: list[str]) -> None:
    """One block per type: files, bytes and digest, then the stage's counts and its first problems."""
    by_kind = {report.kind: report for report in reports}
    for kind in wanted:
        files = output.files.get(kind, {})
        size = sum(output.sizes.get(kind, {}).values())
        print(f"{kind}: {len(files)} files, {size} bytes, sha256 {output.type_digest(kind)}")
        report = by_kind.get(kind)
        if report is None:
            continue
        for what, number in sorted(report.counts.items()):
            print(f"  {what}: {number}")
        for problem in report.problems[:10]:
            print(f"  problem: {problem}")
        if len(report.problems) > 10:
            print(f"  ... {len(report.problems) - 10} more problems")


def _verify(output: Output, identity: dict[str, str], wanted: list[str]) -> int:
    """Compare each type's file count with the expected one for this disc; 1 when one differs or none is known."""
    expected = EXPECTED.get(identity.get(wad.ELF_FILE, ""))
    if not expected:
        print("verify: no expected counts for this disc (only the NTSC-U disc's are known)")
        return 1
    failed = False
    for kind in wanted:
        if kind not in expected:
            print(f"verify: {kind}: no expected count yet")
            failed = True
            continue
        have = len(output.files.get(kind, {}))
        if have != expected[kind]:
            print(f"verify: {kind}: {have} files, expected {expected[kind]}")
            failed = True
        else:
            print(f"verify: {kind}: {have} files, as expected")
    return 1 if failed else 0
