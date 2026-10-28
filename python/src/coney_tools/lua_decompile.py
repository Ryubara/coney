# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools lua decompile`: the game's compiled Lua 4.0 scripts back to readable Lua, outside the repository.

The decompiler itself is the self-contained `lua4` package (python/src/lua4/); this module is Coney's side of it:
where the chunks come from (the player's PS2 disc, the player's Xbox disc, or a folder of chunks `wad extract` wrote),
where the output goes, the guard's marks on it, the per-script index (`lua_index.py`) and the round trip through a
stock `luac`.

* **Output stays outside the repository.** Like Ghidra's, a decompile is a research aid: the game's script text never
  enters the repository, an issue or CI (LEGAL.md#no-game-data). Every file written ends with the guard's marker line
  and `--all` writes the guard's fingerprints, so `.claude/hooks/lua_guard.py` (pre-commit and the Claude Code hooks)
  refuses a copied file or a pasted block. That script is the one definition of both marks; it is loaded from here.
* **One shared folder.** `--all` writes into `lua_dir` (coney.local.toml; default `<workspace>/scratch/lua`), so every
  worktree and agent reads the same copy.
* **The Xbox disc** (`--xbox-disc`) holds its scripts in `XBoxWad.idx`'s volumes, hashed under `ee_files\\`; a script is
  named from the checkout's WAD names list and the file names its own chunks and the executable mention, so the PS2
  and Xbox decompiles of one script share a file name (docs/research/xbox-assets.md#scripts).
* **The round trip** compiles each written file with `luac_program` (default: the one `workspace init --fetch lua`
  builds from Lua 4.0.1's source; `--luac` may name Coney's own compiler, `coney_luac`) and compares the chunk with
  the original, function by function, and the natives each function calls. It prints counts, the kind of each first
  difference and hashes, never a constant or a line of the script.

Guide: docs/guides/coney-tools-fidelity.md#lua. Research: docs/research/formats/lua-bytecode.md.
"""

from __future__ import annotations

import collections
import hashlib
import importlib.util
import os
import re
import sys
import tempfile
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from functools import cache
from pathlib import Path
from types import ModuleType
from typing import TYPE_CHECKING

import lua4
from coney_tools import lua_hashes, lua_index, natives, wad, workspace_fetch
from coney_tools.config import ConfigError, load_config, local_config_file, workspace_folder, workspace_root
from coney_tools.lua4 import LuaError

if TYPE_CHECKING:
    from coney_tools.refs_extract import DiscFacts

#: The checkout this package was loaded from (python/src/coney_tools/ -> the repository root).
REPO = Path(__file__).resolve().parents[3]
#: The guard that refuses decompiled Lua in the repository; it defines the marker and the fingerprints.
GUARD_SCRIPT = REPO / ".claude" / "hooks" / "lua_guard.py"
#: The first bytes of every compiled Lua chunk (ESC "Lua").
SIGNATURE = b"\x1bLua"
#: The shared folder in the workspace when coney.local.toml sets no `lua_dir`.
DEFAULT_DIR = "scratch/lua"
#: Where `workspace init --fetch lua` puts the programs, inside the tool's folder.
BIN = "bin"
_HASH_NAME = re.compile(r"^(?:0x)?([0-9a-fA-F]{8})(?:\.lua)?$")
#: An identifier in a chunk or the executable, tried as a script's name (`<word>.lua`).
_WORD = re.compile(rb"[A-Za-z_][A-Za-z0-9_]{2,63}")


@cache
def guard() -> ModuleType:
    """`.claude/hooks/lua_guard.py` as a module: the marker line and the fingerprints are defined there only."""
    spec = importlib.util.spec_from_file_location("coney_lua_guard", GUARD_SCRIPT)
    if spec is None or spec.loader is None or not GUARD_SCRIPT.is_file():
        raise ConfigError(f"{GUARD_SCRIPT}: missing; the decompiler writes the marks this guard defines")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


# --- settings -------------------------------------------------------------------------------------------------------


def _setting(root: Path, key: str) -> Path | None:
    """A path key of the checkout's coney.local.toml, or None when there is no file or the key is not set."""
    return load_config(root).paths.get(key) if local_config_file(root) is not None else None


def default_luac(root: Path) -> Path:
    """The `luac` that `workspace init --fetch lua` builds, in the workspace's tools folder."""
    name = "luac.exe" if sys.platform == "win32" else "luac"
    return workspace_fetch.tool_destination("lua", workspace_root(root)) / BIN / name


def luac_program(root: Path, required: bool = True) -> Path | None:
    """The stock Lua 4.0 `luac` for the round trip: `luac_program` in coney.local.toml, else the one `workspace init
    --fetch lua` built. Raises ConfigError when there is none (None instead when not `required`)."""
    configured = _setting(root, "luac_program")
    if configured is not None:
        if configured.is_file():
            return configured
        if required:
            raise ConfigError(f"luac_program {configured} is not a file (coney.local.toml)")
        return None
    built = default_luac(root)
    if built.is_file():
        return built
    if required:
        raise ConfigError(
            f"no Lua 4.0 luac at {built}: build it with `coney-tools workspace init --fetch lua`, or set luac_program "
            "in coney.local.toml (docs/guides/coney-tools-fidelity.md#lua)"
        )
    return None


def lua_dir(root: Path) -> Path:
    """The shared folder of decompiled scripts: `lua_dir` in coney.local.toml, else `scratch/lua` in the workspace.
    `.claude/hooks/lua_guard.py` finds the same folder (a test holds the two together)."""
    return _setting(root, "lua_dir") or workspace_folder(root, DEFAULT_DIR)


# --- decompiling ----------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Decompiled:
    """One chunk's decompile: the text written (the marker line last), the same text without the marker (`body`, the
    text the frozen form hashes), its function count and the reasons of the functions that could not be read (these
    name a pc and an opcode, never a constant)."""

    name: str
    text: str
    functions: int
    failures: list[str]
    body: str = ""


def decompile_chunk(name: str, data: bytes) -> Decompiled:
    """Decompile the chunk `data` named `name`. The marker goes last, so every line the decompiler placed (a function
    on the line it was defined on) keeps its number."""
    main = lua4.load(data)
    tree = lua4.decompile_function(main)
    body = lua4.render(tree)
    text = body + guard().marker_line(name, data) + "\n"
    return Decompiled(name, text, len(main.walk()), lua4.failures(tree), body)


def output_path(folder: Path, name: str) -> Path:
    """Where the chunk `name` is written in `folder`: its name, with `.lua` added when it has none. A name that would
    leave the folder is refused."""
    parts = Path(name).parts
    if not parts or Path(name).is_absolute() or Path(name).anchor or ".." in parts:
        raise ConfigError(f"{name!r}: not a chunk name that stays inside the output folder")
    path = folder.joinpath(*parts)
    return path if path.suffix else path.with_name(path.name + ".lua")


def _write(path: Path, text: str) -> None:
    """Write `text` one byte per character (the game's strings are Latin-1) with LF line ends, whole or not at all."""
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}-")
    try:
        with os.fdopen(handle, "wb") as out:
            out.write(text.encode("latin-1", errors="replace"))
        Path(temp).replace(path)
    except BaseException:
        Path(temp).unlink(missing_ok=True)
        raise


# --- where chunks come from -----------------------------------------------------------------------------------------


def chunks_from_folder(folder: Path) -> list[tuple[str, bytes]]:
    """Every compiled chunk in `folder` (`wad extract` output, say), by file name; other files are skipped."""
    if not folder.is_dir():
        raise ConfigError(f"{folder}: no such folder")
    found = []
    for path in sorted(folder.iterdir()):
        if path.is_file():
            data = path.read_bytes()
            if data.startswith(SIGNATURE):
                found.append((path.name, data))
    return found


def _disc_facts(root: Path, disc: str | None) -> DiscFacts:
    """The disc's WAD, read once (refs_extract.DiscFacts): its entries, the names of the entries (the checkout's WAD
    names list, then those recovered from the disc) and the entry reader."""
    from coney_tools import refs_cli, refs_extract  # the disc readers, loaded only when a disc is used
    from coney_tools.wad_cli import open_disc

    return refs_extract.DiscFacts(open_disc(disc), refs_cli.known_names(root, None))


def chunks_from_disc(root: Path, disc: str | None) -> list[tuple[str, bytes]]:
    """Every compiled chunk in the disc's WAD, by its file name (the entry's hash in hex when the name is not known),
    in name order. Each entry's first bytes are read to find the chunks; only those are read whole."""
    facts = _disc_facts(root, disc)
    found = []
    with facts.disc.open(wad.WAD_FILE) as handle:
        for entry in facts.entries:
            handle.seek(entry.offset)
            if handle.read(len(SIGNATURE)) == SIGNATURE:
                found.append((facts.names.get(entry.hash, f"{entry.hash:08x}"), facts.read(entry)))
    return sorted(found)


def _xbox_script_names(root: Path, hashes: set[int], texts: Iterable[bytes]) -> dict[int, str]:
    """Names of the Xbox scripts whose entry hashes are `hashes`: each candidate (the checkout's WAD names list, then
    every file name in `texts`, the scripts and the executable) whose `ee_files\\` hash is one of them and that ends
    in `.lua`. A name is the path below `ee_files\\` with `/` separators."""
    from coney_tools import refs_cli, xbox  # loaded only when an Xbox disc is used

    candidates = list(refs_cli.known_names(root, None))
    for data in texts:
        candidates += sorted(xbox.harvest(data))
        # A level list names its levels without an extension, and a level's script is `<level>.lua`.
        candidates += sorted({word.decode("ascii") + ".lua" for word in _WORD.findall(data)})
    names: dict[int, str] = {}
    for hashed, path in xbox.hash_names(candidates).items():
        if hashed in hashes and path.lower().endswith(".lua"):
            names[hashed] = path.replace("\\", "/")
    return names


def chunks_from_xbox(root: Path, disc_path: Path) -> list[tuple[str, bytes]]:
    """Every compiled chunk in the Xbox disc's archive (`XBoxWad.idx` and its volumes; an image, an XISO or a
    folder), by its file name (the entry's hash in hex when the name is not known), in name order. Each entry's first
    bytes are read to find the chunks; only those are read whole."""
    from coney_tools import xbox  # loaded only when an Xbox disc is used
    from coney_tools.xdvdfs import XboxDisc

    disc = XboxDisc(disc_path)
    index = xbox.load_index(disc)
    found: list[tuple[int, bytes]] = []
    for entry, handle in xbox.iter_entries(disc, index):
        if entry.size > len(SIGNATURE) and handle.read(len(SIGNATURE)) == SIGNATURE:
            found.append((entry.hash, SIGNATURE + handle.read(entry.size - len(SIGNATURE))))
    texts = [data for _, data in found]
    if disc.has(xbox.XBE_FILE):
        with disc.open(xbox.XBE_FILE) as xbe:
            texts.append(xbe.read())
    names = _xbox_script_names(root, {hashed for hashed, _ in found}, texts)
    return sorted((names.get(hashed, f"{hashed:08x}"), data) for hashed, data in found)


def _source_chunks(root: Path, disc: str | None, options: Options) -> list[tuple[str, bytes]]:
    """The chunks the options name: `--chunks DIR`, else `--xbox-disc XBOX_DISC`, else the PS2 disc."""
    if options.chunks is not None:
        return chunks_from_folder(options.chunks)
    if options.xbox is not None:
        return chunks_from_xbox(root, options.xbox)
    return chunks_from_disc(root, disc)


def one_chunk(root: Path, name: str, disc: str | None, folder: Path | None, xbox_disc: Path | None = None) -> bytes:
    """The chunk `name` from `folder`, else from the Xbox disc `xbox_disc`, else from the PS2 disc (by file name, or
    by the entry's hash in hex)."""
    if xbox_disc is not None:
        match = _HASH_NAME.match(name)
        wanted = {name.lower(), name.lower() + ".lua"}
        for found, data in chunks_from_xbox(root, xbox_disc):
            if found.lower() in wanted or (match is not None and found == match.group(1).lower()):
                return data
        raise ConfigError(
            f"no chunk named {name!r} on the Xbox disc (a file name such as level1.lua, or a hash in hex)"
        )
    if folder is not None:
        wanted = {name, name + ".lua"}
        for found, data in chunks_from_folder(folder):
            if found in wanted:
                return data
        raise ConfigError(f"no chunk named {name!r} in {folder}")
    facts = _disc_facts(root, disc)
    entry = facts.by_name(name)
    match = _HASH_NAME.match(name)
    if entry is None and match is not None:
        key = int(match.group(1), 16)
        entry = next((item for item in facts.entries if item.hash == key), None)
    data = facts.read(entry) if entry is not None else b""
    if not data.startswith(SIGNATURE):
        raise ConfigError(f"no chunk named {name!r} on the disc (a file name such as level1.lua, or a hash in hex)")
    return bytes(data)


# --- the round trip -------------------------------------------------------------------------------------------------


@dataclass
class RoundTrip:
    """How many chunks compile back to the same code, and how the others failed (kinds and names only)."""

    total: int = 0
    same: int = 0
    #: luac refused the text.
    refused: int = 0
    #: It compiled, to different code or constants.
    differ: int = 0
    #: Each failure's kind ("code: pc N: JMP vs JMPF", "luac refused it (line 7)"), counted.
    kinds: collections.Counter[str] = field(default_factory=collections.Counter)
    failed: list[str] = field(default_factory=list)
    #: Chunks whose compiled form calls the same natives as the original, function by function; of how many.
    natives_same: int = 0
    natives_compared: int = 0
    #: Native uses in the originals (each native once per function that calls it).
    native_uses: int = 0
    #: What the originals and the compiled chunks call, a line per function (`chunk path native,...`), for the hashes.
    natives_original: list[str] = field(default_factory=list)
    natives_compiled: list[str] = field(default_factory=list)


def _natives_called(name: str, data: bytes, known: frozenset[str]) -> tuple[list[str], int] | None:
    """The natives each function of chunk `data` calls (the index's reading), one line per function, with the number
    of native uses; None when the index cannot read the chunk."""
    try:
        script = lua_index.index_script(name, data, known)
    except (LuaError, lua4.ChunkError):
        return None
    lines = [f"{name} {function.path} {','.join(function.natives)}" for function in script.functions]
    return lines, sum(len(function.natives) for function in script.functions)


def round_trip(
    items: Iterable[tuple[str, bytes, str]],
    compile_text: Callable[[str], bytes],
    known: frozenset[str] = frozenset(),
) -> RoundTrip:
    """Compile each (name, original chunk, decompiled text) with `compile_text` and compare it with the original: the
    code, function by function, and the natives (`known`, the binding names) each function calls."""
    result = RoundTrip()
    for name, data, text in items:
        result.total += 1
        try:
            compiled = compile_text(text)
            again = lua4.load(compiled)
        except lua4.LuacError as error:
            result.refused += 1
            result.failed.append(name)
            result.kinds[f"luac refused it (line {error.line})"] += 1  # luac's message may quote the source
            continue
        original_natives = _natives_called(name, data, known)
        compiled_natives = _natives_called(name, compiled, known)
        if original_natives is not None and compiled_natives is not None:
            result.natives_compared += 1
            result.natives_same += int(original_natives[0] == compiled_natives[0])
            result.native_uses += original_natives[1]
            result.natives_original += original_natives[0]
            result.natives_compiled += compiled_natives[0]
        differences = lua4.compare(lua4.load(data), again)
        if differences:
            result.differ += 1
            result.failed.append(name)
            first = differences[0]
            result.kinds[f"{first.kind}: {re.sub(r'pc \d+', 'pc N', first.detail)}"] += 1
        else:
            result.same += 1
    return result


def print_round_trip(result: RoundTrip) -> None:
    """The round trip's counts, each kind of first difference, and the chunks that failed (names only)."""
    print(f"round trip: {result.same} of {result.total} chunks compile back to the same code", end="")
    print(f" ({result.refused} refused by luac, {result.differ} compile to other code)")
    for kind, count in result.kinds.most_common():
        print(f"  {count:4d}  {kind}")
    if result.failed:
        print(f"  failed: {', '.join(result.failed)}")
    if result.natives_compared:
        original = hashlib.sha256("\n".join(result.natives_original).encode()).hexdigest()
        compiled = hashlib.sha256("\n".join(result.natives_compiled).encode()).hexdigest()
        print(
            f"natives: {result.natives_same} of {result.natives_compared} chunks call the same natives, function by "
            f"function ({result.native_uses} native uses; sha256 {original[:16]} original, {compiled[:16]} compiled)"
        )


# --- the commands ---------------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Options:
    """The command's options: `--chunks DIR` (a folder of chunks instead of the disc), `--xbox-disc XBOX_DISC` (the Xbox
    disc instead of the PS2 one), `--out DIR` (for `--all`; default `lua_dir`), `--luac PATH` and `--roundtrip`; for
    `--all` also `--hashes FILE` (the list of decompile hashes to check; default the checkout's),
    `--update-hashes` and `--new-form` (lua_hashes.py)."""

    chunks: Path | None = None
    xbox: Path | None = None
    out: Path | None = None
    luac: Path | None = None
    roundtrip: bool = False
    hashes: Path | None = None
    update_hashes: bool = False
    new_form: bool = False


def _compiler(root: Path, options: Options) -> Callable[[str], bytes]:
    """The round trip's compiler: the given `luac`, else the configured one; stripped, as the game's chunks are."""
    luac = options.luac or luac_program(root)
    assert luac is not None
    if not luac.is_file():
        raise ConfigError(f"--luac {luac}: not a file")
    return lambda text: lua4.compile_source(text, luac, strip=True)


def run_one(root: Path, disc: str | None, name: str, out_dir: Path, options: Options) -> int:
    """`lua decompile [DISC] NAME OUT_DIR`: write one chunk's decompile into OUT_DIR; with `--roundtrip`, check it.
    1 when a function could not be read or the round trip fails."""
    wad.refuse_inside_repo(out_dir)
    compile_text = _compiler(root, options) if options.roundtrip else None
    data = one_chunk(root, name, disc, options.chunks, options.xbox)
    try:
        result = decompile_chunk(name, data)
    except lua4.ChunkError as error:  # its reason names sizes and offsets only, never a constant
        raise ConfigError(f"{name}: not loaded ({error})") from error
    path = output_path(out_dir, name)
    _write(path, result.text)
    unread = len(result.failures)
    print(f"{name}: {result.functions} functions, {unread} could not be read -> {path}")
    status = 1 if unread else 0
    if compile_text is not None:
        trip = round_trip([(name, data, result.text)], compile_text, _binding_names(root))
        print_round_trip(trip)
        status = status or int(trip.same != trip.total)
    return status


def _binding_names(root: Path) -> frozenset[str]:
    """The game's binding names (research/bindings), which the index lists as natives."""
    try:
        return frozenset(binding.name for binding in natives.load(root).bindings)
    except ConfigError as error:
        print(f"coney-tools: the index lists no natives ({error})", file=sys.stderr)
        return frozenset()


def run_all(root: Path, disc: str | None, options: Options, known_natives: frozenset[str] | None = None) -> int:
    """`lua decompile [DISC] --all`: every chunk into the shared folder (or `--out`), with the guard's fingerprints and
    the index; with `--roundtrip`, check every one. Prints counts only. A chunk the loader refuses is named on stderr
    with the reason, and the rest still go through, so the shared folder's fingerprints and index cover every script
    that was written. Writes the decompile hashes beside them and checks them against the committed list (or writes
    the list: `--update-hashes`, `--new-form`; lua_hashes.py). 1 when a chunk could not be loaded, a script's text
    changed from the list, a list write was refused or the round trip fails."""
    out_dir = options.out or lua_dir(root)
    wad.refuse_inside_repo(out_dir)
    if options.update_hashes and options.new_form:
        raise ConfigError("--update-hashes adds scripts to the list; --new-form rewrites it: use one, not both")
    listed = None if options.new_form else _listed_hashes(root, options)
    form = _form_of_run(root, options, listed)
    compile_text = _compiler(root, options) if options.roundtrip else None
    chunks = _source_chunks(root, disc, options)
    if not chunks:
        raise ConfigError("no compiled Lua chunks found")
    known = known_natives if known_natives is not None else _binding_names(root)
    marks = guard()
    prints: set[str] = set()
    written: list[tuple[str, bytes, str]] = []
    hashed: list[lua_hashes.Entry] = []
    scripts: list[lua_index.Script] = []
    functions = 0
    reasons: collections.Counter[str] = collections.Counter()
    not_loaded: list[str] = []
    for count, (name, data) in enumerate(chunks, start=1):
        try:
            result = decompile_chunk(name, data)
        except lua4.ChunkError as error:  # its reason names sizes and offsets only, never a constant
            print(f"coney-tools: {name}: not loaded ({error})", file=sys.stderr)
            not_loaded.append(name)
            continue
        _write(output_path(out_dir, name), result.text)
        written.append((name, data, result.text))
        hashed.append(lua_hashes.entry_for(name, data, result.body))
        functions += result.functions
        reasons.update(re.sub(r"\d+", "N", reason) for reason in result.failures)
        prints.update(found for line in result.text.splitlines() if (found := marks.fingerprint(line)))
        try:
            scripts.append(lua_index.index_script(name, data, known))
        except LuaError as error:  # the index's reader takes only the game's chunk layout (4-byte sizes)
            print(f"coney-tools: {name} is not in the index ({error})", file=sys.stderr)
        if count % 100 == 0:
            print(f"coney-tools: decompiled {count} of {len(chunks)} chunks", file=sys.stderr)
    _write(out_dir / marks.FINGERPRINTS, "".join(f"{value}\n" for value in sorted(prints)))
    lua_index.write_index(out_dir, scripts, source=f"{len(written)} chunks")
    lua_hashes.write(out_dir / lua_hashes.OUTPUT, form, hashed)
    unread = sum(reasons.values())
    print(f"decompiled {len(written)} of {len(chunks)} chunks, {functions} functions -> {out_dir}")
    if not_loaded:
        print(f"{len(not_loaded)} chunks could not be loaded: {', '.join(not_loaded)}")
    print(f"{unread} functions could not be read" + (":" if unread else ""))
    for reason, count in reasons.most_common():
        print(f"  {count:4d}  {reason}")
    print(f"fingerprints: {len(prints)} lines; index: {lua_index.INDEX_MD}, {lua_index.INDEX_JSON}")
    hash_status = _check_hashes(root, options, listed, hashed, form, bool(not_loaded))
    status = int(bool(not_loaded)) or hash_status
    if compile_text is None:
        return status
    trip = round_trip(written, compile_text, known)
    print_round_trip(trip)
    return status or int(trip.same != trip.total)


# --- the frozen form ------------------------------------------------------------------------------------------------


def _listed_hashes(root: Path, options: Options) -> lua_hashes.HashList | None:
    """The committed list of decompile hashes to check against. None when the run writes it (`--update-hashes` with no
    list yet); a missing list is otherwise an error."""
    path = lua_hashes.list_path(root, options.hashes)
    if options.update_hashes and not path.is_file():
        return None
    return lua_hashes.load(path)


def _form_of_run(root: Path, options: Options, listed: lua_hashes.HashList | None) -> int:
    """The form this run's hashes are in: the list's, the next one with `--new-form`, else form 1 for a new list."""
    if options.new_form:
        path = lua_hashes.list_path(root, options.hashes)
        return (lua_hashes.load(path).form if path.is_file() else 0) + 1
    return listed.form if listed is not None else lua_hashes.FORM


def _check_hashes(
    root: Path,
    options: Options,
    listed: lua_hashes.HashList | None,
    found: list[lua_hashes.Entry],
    form: int,
    partial: bool,
) -> int:
    """Check this run's decompile hashes against the list, or write the list (`--update-hashes`, `--new-form`).
    Prints counts and the names of the scripts whose text changed, never a line of text. 1 when a script's text
    changed or a write was refused."""
    path = lua_hashes.list_path(root, options.hashes)
    writing = options.new_form or options.update_hashes
    if writing and partial:
        print("coney-tools: a chunk could not be loaded; the list of decompile hashes is not written", file=sys.stderr)
        return 1
    if options.new_form or listed is None:  # --new-form, or --update-hashes and no list yet: this run is the list
        lua_hashes.write(path, form, found)
        print(f"decompile hashes: wrote form {form} with {len(found)} scripts -> {path}")
        if form != lua_hashes.FORM:
            print(
                f"coney-tools: set FORM = {form} in python/src/coney_tools/lua_hashes.py (a test holds them together)"
            )
        return 0
    result = lua_hashes.compare(listed, found)
    print(
        f"decompile hashes: {result.same} of {len(found)} match the list (form {listed.form}); "
        f"{len(result.unlisted)} not in the list, {result.absent} listed but not here"
    )
    if result.changed:
        print(f"coney-tools: text changed: {', '.join(result.changed)}", file=sys.stderr)
        if options.update_hashes:
            print(
                "coney-tools: the list is frozen: a change to the output is a new form (--new-form; "
                "docs/guides/coney-tools-fidelity.md#lua-form)",
                file=sys.stderr,
            )
        return 1
    if options.update_hashes:
        merged = lua_hashes.merge(listed, found)
        lua_hashes.write(path, listed.form, merged)
        print(f"decompile hashes: added {len(merged) - len(listed.entries)} scripts to {path}")
    return 0
