# SPDX-License-Identifier: GPL-3.0-or-later
"""The optional downloads of `coney-tools workspace init --fetch`: the research tools at pinned versions.

Every tool comes from its own project's GitHub releases (or, for ghidra-mcp and PCSX2-MCP, its repository at a fixed
commit; for Lua, its authors' own site) and every archive is checked against the SHA-256 written here before it is
unpacked. Lua 4.0.1's `luac` is built here from its C source with the machine's compiler (`lua decompile --roundtrip`
compiles the decompiler's output with it). PCSX2 itself is not
fetched: Coney's copies run the PCSX2-MCP fork's own build, which is made from that repository by hand and spread
over the copies by `coney-tools pcsx2 setup-copies`. Nothing here fetches firmware or game files: the player supplies
their own disc, and PCSX2's BIOS is dumped from their own console (LEGAL.md). See docs/guides/workspace.md#bootstrap.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
import zipfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: The platforms the downloads know, as `workspace init` names them.
PLATFORMS: tuple[str, ...] = ("windows", "linux", "macos")

#: The tools `--fetch` accepts, in the order they are fetched.
TOOLS: tuple[str, ...] = ("ghidra", "ghidra-mcp", "pcsx2-mcp", "pcsx2-mcp-release", "ccache", "lua")

#: The tools installed as a git clone at a pinned commit rather than from a release archive.
CLONED: frozenset[str] = frozenset({"ghidra-mcp", "pcsx2-mcp"})

#: Downloads `url` to the file given.
Downloader = Callable[[str, Path], None]
#: Builds Lua's `luac` from the source folder into the bin folder on a platform (`build_luac`).
LuaBuilder = Callable[[Path, Path, str], None]


@dataclass(frozen=True)
class Pin:
    """One download at a fixed version."""

    name: str
    version: str
    url: str
    #: The file's SHA-256 (hex); for a git repository, the commit to check out.
    digest: str


#: A fork build's release folder, `release/PCSX2-MCP-coney-v<PCSX2 version>.<build>-win64` in the PCSX2-MCP clone.
RELEASE_FOLDER = re.compile(r"^PCSX2-MCP-coney-v(\d+)\.(\d+)\.(\d+)\.(\d+)-win64$")


def newest_release(clone: Path) -> Path | None:
    """The newest fork build in the PCSX2-MCP clone's `release/` folder (highest version, then build number), or None
    when there is none."""
    folder = clone / "release"
    found = [
        (tuple(int(part) for part in match.groups()), entry)
        for entry in (folder.iterdir() if folder.is_dir() else [])
        if entry.is_dir() and (match := RELEASE_FOLDER.match(entry.name))
    ]
    return max(found)[1] if found else None


_GH = "https://github.com"

GHIDRA_VERSION = "12.1.4"
GHIDRA = Pin(
    "Ghidra",
    GHIDRA_VERSION,
    f"{_GH}/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.1.4_build/ghidra_12.1.4_PUBLIC_20260921.zip",
    "ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db",
)
#: The Emotion Engine (PS2 CPU) extension, built for this Ghidra version.
GHIDRA_EE = Pin(
    "ghidra-emotionengine-reloaded",
    "2.1.38",
    f"{_GH}/chaoticgd/ghidra-emotionengine-reloaded/releases/download/v2.1.38/"
    "ghidra_12.1.4_PUBLIC_20261004_ghidra-emotionengine-reloaded.zip",
    "e389ff3e921d9f7d4db2ece9906f808545ce4ff519d9e25a019b72ea509f240b",
)
#: ghidra-mcp is built from source (Maven or Gradle, against the Ghidra above); the commit Coney was set up with.
GHIDRA_MCP = Pin(
    "ghidra-mcp", "7.0.0 (pre-release)", f"{_GH}/bethington/ghidra-mcp", "d66702c63b7149453d0686e58f7fb29f3140d5d3"
)
#: The maintainer's PCSX2-MCP fork (a per-copy debug server port, screenshots, controller input, frame advance,
#: capture, GS dumps, input recordings) has no release archive: the clone holds the PCSX2 patch and the Node.js
#: server's source, and its README builds PCSX2 2.9.129 with the patch into release/PCSX2-MCP-coney-v2.9.129.N-win64
#: (docs/guides/workspace.md#pcsx2-copies).
PCSX2_MCP = Pin(
    "PCSX2-MCP (fork, source)", "2.9.129", f"{_GH}/Ryubara/PCSX2-MCP", "974cf222e0104a93facf8f70d4e5f4f64a7a23e5"
)

#: The compiler cache `coney-tools build` puts in front of the compiler (compiler_cache.py). Only Windows has no
#: package of it, so only Windows fetches it: the official release, the same version and digest as CI's
#: (.github/actions/ccache/action.yml; a test keeps the two equal). Linux and macOS install it with apt or Homebrew.
CCACHE_VERSION = "4.14.1"
CCACHE_WINDOWS = Pin(
    "ccache",
    CCACHE_VERSION,
    f"{_GH}/ccache/ccache/releases/download/v{CCACHE_VERSION}/ccache-{CCACHE_VERSION}-windows-x86_64.zip",
    "6219f3865ca59aec41ee4b678df171d5d35855ecb2b6dbbbd20690b3a68af7b4",
)

#: The fork's public release (Windows only): its PCSX2 2.9.129 build with the debug server, and the MCP server with
#: its dependencies beside it, the same shape as a build of the clone's `release/` folder, so `pcsx2_mcp_dir` can
#: name it and `pcsx2 setup-copies` spreads it over the copies. This is the newcomer's way; building from the clone
#: is for changing the fork (docs/guides/workspace.md#pcsx2-copies).
PCSX2_MCP_RELEASE_VERSION = "1.1.0"
PCSX2_MCP_RELEASE = Pin(
    "PCSX2-MCP (fork, release)",
    PCSX2_MCP_RELEASE_VERSION,
    f"{_GH}/Ryubara/PCSX2-MCP/releases/download/v{PCSX2_MCP_RELEASE_VERSION}/"
    f"PCSX2-MCP-v{PCSX2_MCP_RELEASE_VERSION}-win64.zip",
    "98cee4215f928d4e6148ad955a78454ef70b9a6850ab64cee657f5e2edc69a11",
)

#: Lua 4.0.1, the version the game's scripts were compiled with (docs/research/formats/lua-bytecode.md): its C source,
#: from which `luac` is built on every platform (no package manager has a Lua this old). Only the source is unpacked.
LUA_VERSION = "4.0.1"
LUA = Pin(
    "Lua (source, luac built from it)",
    LUA_VERSION,
    f"https://www.lua.org/ftp/lua-{LUA_VERSION}.tar.gz",
    "df746e149cf6939e90009d2e540eee918d585b4d1bc6d68b19316a050d484d2a",
)

#: The pins of each tool for a platform (none: the platform's package manager installs it).
PINS: dict[str, Callable[[str], list[Pin]]] = {
    "ghidra": lambda platform: [GHIDRA, GHIDRA_EE],
    "ghidra-mcp": lambda platform: [GHIDRA_MCP],
    "pcsx2-mcp": lambda platform: [PCSX2_MCP],
    "pcsx2-mcp-release": lambda platform: [PCSX2_MCP_RELEASE] if platform == "windows" else [],
    "ccache": lambda platform: [CCACHE_WINDOWS] if platform == "windows" else [],
    "lua": lambda platform: [LUA],
}

#: Where each tool goes, relative to the workspace (docs/guides/workspace.md#the-layout), and so what
#: coney.local.toml's defaults point at.
_DESTINATIONS = {
    "ghidra": f"tools/ghidra_{GHIDRA_VERSION}_PUBLIC",
    "ghidra-mcp": "tools/ghidra-mcp",
    "pcsx2-mcp": "tools/pcsx2-mcp",
    "pcsx2-mcp-release": f"tools/PCSX2-MCP-v{PCSX2_MCP_RELEASE_VERSION}-win64",
    "ccache": f"tools/ccache-{CCACHE_VERSION}",
    "lua": f"tools/lua-{LUA_VERSION}",
}


def tool_destination(tool: str, workspace: Path) -> Path:
    """The folder `tool` is installed into in `workspace`."""
    return workspace / _DESTINATIONS[tool]


def download_url(url: str, out: Path) -> None:
    """Download `url` to `out` over HTTPS."""
    with urllib.request.urlopen(url, timeout=60) as response, out.open("wb") as handle:
        shutil.copyfileobj(response, handle, length=1 << 20)


def _sha256(path: Path) -> str:
    """The SHA-256 of the file at `path`, read in pieces (Ghidra's archive is half a gigabyte)."""
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_file(pin: Pin, cache: Path, download: Downloader) -> Path:
    """The pinned file, downloaded into `cache` unless a copy with the right hash is already there.

    Raises ConfigError when the download's SHA-256 differs from the pin. The partial download (`.part`) is deleted
    whenever it does not become the cached file: a wrong hash, a network error, a timeout or Ctrl-C.
    """
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / pin.url.rsplit("/", 1)[-1]
    if target.is_file() and _sha256(target) == pin.digest:
        return target
    partial = target.with_name(target.name + ".part")
    print(f"  downloading {pin.url}", flush=True)
    try:
        download(pin.url, partial)
        actual = _sha256(partial)
        if actual != pin.digest:
            raise ConfigError(f"{pin.url}: SHA-256 is {actual}, expected {pin.digest}; nothing was installed")
        partial.replace(target)
    except BaseException:
        partial.unlink(missing_ok=True)
        raise
    return target


def _extract(archive: Path, into: Path) -> None:
    """Extract `archive` (zip, tar.xz/gz or 7z) into the empty folder `into`, refusing paths that leave it."""
    name = archive.name.lower()
    if name.endswith(".zip"):
        with zipfile.ZipFile(archive) as zipped:
            for member in zipped.namelist():
                if not (into / member).resolve().is_relative_to(into.resolve()):
                    raise ConfigError(f"{archive}: entry {member!r} points outside the folder")
            zipped.extractall(into)
    elif name.endswith((".tar.xz", ".tar.gz")):
        with tarfile.open(archive) as tarred:
            try:
                # The "data" filter refuses entries (and links) that leave `into`, absolute links and devices.
                tarred.extractall(into, filter="data")
            except tarfile.FilterError as error:
                raise ConfigError(f"{archive}: an entry points outside the folder or is unsafe ({error})") from error
    elif name.endswith(".7z"):
        # Python cannot read 7z; Windows 11's `tar` (bsdtar) reads it, an older Windows 10 tar may not, and then the
        # user extracts it by hand (docs/guides/workspace.md#fetch).
        done = subprocess.run(
            ["tar", "-xf", str(archive), "-C", str(into)], capture_output=True, text=True, check=False
        )
        if done.returncode != 0:
            raise ConfigError(f"{archive}: tar could not extract it ({done.stderr.strip()}); extract it by hand")
    else:
        raise ConfigError(f"{archive}: unknown archive type")


def unpack(archive: Path, dest: Path, subfolder: str | None = None) -> None:
    """Unpack `archive` into the new folder `dest`.

    A single top-level folder in the archive is stripped (`ghidra_12.1.4_PUBLIC/...` lands in `dest` itself), unless
    it is a macOS app bundle (`PCSX2-v2.9.94.app`), which only works whole. With `subfolder`, only that folder of
    the archive is kept, wherever it sits below the top.
    """
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent, prefix=f".{dest.name}-") as temp:
        root = Path(temp)
        _extract(archive, root)
        entries = list(root.iterdir())
        single = entries[0] if len(entries) == 1 and entries[0].is_dir() else None
        if single is not None and single.name != subfolder and not single.name.endswith(".app"):
            root = single
        if subfolder is not None:
            root = root / subfolder
            if not root.is_dir():
                raise ConfigError(f"{archive}: no {subfolder}/ folder in it")
        shutil.move(str(root), str(dest))


def _clone(pin: Pin, dest: Path) -> None:
    """Clone the pinned repository into `dest` and check out the pinned commit."""
    dest.parent.mkdir(parents=True, exist_ok=True)
    for command in (
        ["git", "clone", "--quiet", "--", pin.url, str(dest)],
        ["git", "-C", str(dest), "checkout", "--quiet", "--detach", pin.digest],
    ):
        done = subprocess.run(command, capture_output=True, text=True, check=False)
        if done.returncode != 0:
            raise ConfigError(f"{' '.join(command)}: {done.stderr.strip()}")


def fetch_tool(
    tool: str,
    dest: Path,
    platform: str,
    cache: Path,
    download: Downloader,
    pins: Sequence[Pin] | None = None,
    build_lua: LuaBuilder | None = None,
) -> None:
    """Install `tool` into `dest` (which must not exist yet) from its pins (`PINS` unless given); Lua's `luac` is
    built with `build_lua` (`build_luac` unless given).

    `dest` appears whole or not at all: `init` skips a tool whose folder exists, so a half-installed one (Ghidra
    without its extension, Lua's source without its `luac`) would never be repaired. Every file is downloaded and
    checked before anything is unpacked, the tool is built in a hidden folder beside `dest`, and that folder is
    renamed to `dest` at the end.
    """
    pins = list(pins) if pins is not None else PINS[tool](platform)
    files = [] if tool in CLONED else [fetch_file(pin, cache, download) for pin in pins]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent, prefix=f".{dest.name}-", ignore_cleanup_errors=True) as temp:
        staging = Path(temp) / dest.name
        if tool == "lua":
            unpack_lua_source(files[0], staging / "source")
            (build_lua or build_luac)(staging / "source", staging / "bin", platform)
        else:
            _install(tool, pins, files, staging, platform)
        move_into_place(staging, dest)


#: How often, and how long apart, the final rename is tried when Windows refuses it.
MOVE_ATTEMPTS = 20
MOVE_WAIT_SECONDS = 1.5


def move_into_place(
    staging: Path,
    dest: Path,
    rename: Callable[[Path, Path], object] = Path.replace,
    wait: Callable[[float], None] = time.sleep,
) -> None:
    """Rename the finished `staging` folder to `dest`. On Windows a virus scanner still reading the files just
    unpacked (the PCSX2-MCP release holds thousands, in node_modules) makes the rename fail with "Access is denied"
    for a few seconds, so it is tried again, at most MOVE_ATTEMPTS times (about half a minute)."""
    for attempt in range(1, MOVE_ATTEMPTS + 1):
        try:
            rename(staging, dest)
            return
        except PermissionError as error:
            if attempt == MOVE_ATTEMPTS:
                raise ConfigError(
                    f"could not move {staging} to {dest} after {MOVE_ATTEMPTS} tries ({error}); "
                    "something holds its files open, such as a virus scanner: run the command again"
                ) from error
            wait(MOVE_WAIT_SECONDS)


def _install(tool: str, pins: Sequence[Pin], files: Sequence[Path], dest: Path, platform: str) -> None:
    """Build `tool` in the new folder `dest` from its checked downloads `files` (one per pin)."""
    if tool in CLONED:
        _clone(pins[0], dest)
        return
    if tool == "ghidra":
        unpack(files[0], dest)
        unpack(files[1], dest / "Ghidra" / "Extensions" / "ghidra-emotionengine-reloaded")
        return
    if tool == "pcsx2-mcp-release" and files:
        unpack(files[0], dest)  # flat: pcsx2-qt.exe and pcsx2-mcp-server/ at the top
        return
    if tool == "ccache" and files:
        unpack(files[0], dest)  # the release's one folder is stripped: dest/ccache.exe
        return
    raise ConfigError(f"unknown tool {tool!r} on {platform}")


#: The parts of Lua's source archive `luac` is built from (below its top folder): the core, luac and the licence.
LUA_KEPT = ("src/", "include/", "COPYRIGHT")


def unpack_lua_source(archive: Path, dest: Path) -> None:
    """Unpack the regular files of LUA_KEPT from Lua's source archive into the new folder `dest`. The archive also
    holds symbolic links that point outside it (`bin/lua -> ../test/lua`), which Windows cannot make without special
    rights and which `luac` does not need, so links and every other kind of entry are skipped."""
    dest.mkdir(parents=True)
    with tarfile.open(archive) as tarred:
        for member in tarred.getmembers():
            inner = member.name.partition("/")[2]
            if not member.isfile() or not inner.startswith(LUA_KEPT) or ".." in Path(inner).parts:
                continue
            target = dest / inner
            target.parent.mkdir(parents=True, exist_ok=True)
            source = tarred.extractfile(member)
            if source is not None:
                target.write_bytes(source.read())


#: One step of the `luac` build: the command and the folder it runs in.
BuildStep = tuple[list[str], Path]


def lua_build_commands(source: Path, bin_dir: Path, obj: Path, platform: str) -> list[BuildStep]:
    """The commands that build `luac` from Lua 4.0.1's `source` into `bin_dir`, with the objects in `obj`: the core
    (`src/*.c`) into a static library, then luac's own files (`src/luac/*.c`) linked against it. Linking every object
    directly fails, as `stubs.c` defines functions the core's `lundump.c` defines too, and a library gives only what
    is still missing. The standard libraries (`src/lib/`) and the interpreter are not needed.

    MSVC on Windows (`cl`, `lib`), `cc` and `ar` elsewhere; the code is read as C89 (`-std=gnu89`), since newer
    compilers refuse its implicit declarations in their default mode."""
    core = [str(path) for path in sorted((source / "src").glob("*.c"))]
    luac = [str(path) for path in sorted((source / "src" / "luac").glob("*.c"))]
    includes = [str(source / "include"), str(source / "src")]
    luac_obj = obj / "luac"
    if platform == "windows":
        flags = ["/nologo", "/O2", "/W1", "/DNDEBUG", *(f"/I{folder}" for folder in includes)]
        library = obj / "lua.lib"
        objects = [str(obj / (Path(name).stem + ".obj")) for name in core]
        return [
            (["cl", *flags, "/c", f"/Fo{obj}{os.sep}", *core], obj),
            (["lib", "/nologo", f"/out:{library}", *objects], obj),
            (["cl", *flags, f"/Fo{luac_obj}{os.sep}", *luac, str(library), f"/Fe:{bin_dir / 'luac.exe'}"], luac_obj),
        ]
    flags = ["-std=gnu89", "-O2", "-w", "-DNDEBUG", *(f"-I{folder}" for folder in includes)]
    library = obj / "liblua.a"
    objects = [str(obj / (Path(name).stem + ".o")) for name in core]
    return [
        (["cc", *flags, "-c", *core], obj),
        (["ar", "rcs", str(library), *objects], obj),
        (["cc", *flags, *luac, str(library), "-lm", "-o", str(bin_dir / "luac")], luac_obj),
    ]


def build_luac(source: Path, bin_dir: Path, platform: str) -> None:
    """Build `luac` from Lua's `source` into `bin_dir` (`lua_build_commands`), with the objects in a folder beside
    the source that is removed afterwards. On Windows the Visual Studio C++ environment is loaded as `coney-tools
    build` loads it. Raises ConfigError, with the compiler's last lines, when a step fails."""
    from coney_tools import build_run  # the MSVC environment; build_run is large and only this step needs it

    obj = source.parent / "obj"
    env = build_run.load_msvc_environment(os.environ, build_run.run_text) if platform == "windows" else dict(os.environ)
    hint = "Visual Studio's C++ tools" if platform == "windows" else "a C compiler (cc) and ar"
    try:
        for command, folder in lua_build_commands(source, bin_dir, obj, platform):
            folder.mkdir(parents=True, exist_ok=True)
            bin_dir.mkdir(parents=True, exist_ok=True)
            # Windows looks a program up on this process's PATH, not the one given, so find it on the loaded one.
            program = shutil.which(command[0], path=build_run.env_get(env, "PATH")) or command[0]
            try:
                done = subprocess.run(
                    [program, *command[1:]], cwd=folder, env=env, capture_output=True, text=True, check=False
                )
            except OSError as error:
                raise ConfigError(f"{command[0]}: cannot run ({error}); building luac needs {hint}") from error
            if done.returncode != 0:
                tail = "\n".join((done.stdout + done.stderr).strip().splitlines()[-15:])
                raise ConfigError(f"building luac failed at `{command[0]}` (exit {done.returncode}):\n{tail}")
    finally:
        shutil.rmtree(obj, ignore_errors=True)


def describe(tool: str, platform: str) -> str:
    """One line naming what `tool` fetches on `platform`."""
    return ", ".join(f"{pin.name} {pin.version}" for pin in PINS[tool](platform))


#: Tools built for Windows only: elsewhere `--fetch` says so instead of pointing at a package manager.
WINDOWS_ONLY: tuple[str, ...] = ("pcsx2-mcp-release",)


def packaged(tool: str, platform: str) -> bool:
    """Whether `tool` comes from the platform's package manager there, so `--fetch` has nothing to download."""
    return tool not in CLONED and not PINS[tool](platform)


def platform_name() -> str:
    """This machine's platform as the pins name it."""
    if sys.platform == "win32":
        return "windows"
    return "macos" if sys.platform == "darwin" else "linux"
