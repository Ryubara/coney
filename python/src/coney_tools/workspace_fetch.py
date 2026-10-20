# SPDX-License-Identifier: GPL-3.0-or-later
"""The optional downloads of `coney-tools workspace init --fetch`: the research tools at pinned versions.

Every tool comes from its own project's GitHub releases (or, for ghidra-mcp and PCSX2-MCP, its repository at a fixed
commit) and every archive is checked against the SHA-256 written here before it is unpacked. PCSX2 itself is not
fetched: Coney's copies run the PCSX2-MCP fork's own build, which is made from that repository by hand and spread
over the copies by `coney-tools pcsx2 setup-copies`. Nothing here fetches firmware or game files: the player supplies
their own disc, and PCSX2's BIOS is dumped from their own console (LEGAL.md). See docs/guides/workspace.md#bootstrap.
"""

from __future__ import annotations

import hashlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError

#: The platforms the downloads know, as `workspace init` names them.
PLATFORMS: tuple[str, ...] = ("windows", "linux", "macos")

#: The tools `--fetch` accepts, in the order they are fetched.
TOOLS: tuple[str, ...] = ("ghidra", "ghidra-mcp", "pcsx2-mcp")

#: The tools installed as a git clone at a pinned commit rather than from a release archive.
CLONED: frozenset[str] = frozenset({"ghidra-mcp", "pcsx2-mcp"})

#: Downloads `url` to the file given.
Downloader = Callable[[str, Path], None]


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

#: The pins of each tool for a platform.
PINS: dict[str, Callable[[str], list[Pin]]] = {
    "ghidra": lambda platform: [GHIDRA, GHIDRA_EE],
    "ghidra-mcp": lambda platform: [GHIDRA_MCP],
    "pcsx2-mcp": lambda platform: [PCSX2_MCP],
}

#: Where each tool goes, relative to the workspace (docs/guides/workspace.md#the-layout), and so what
#: coney.local.toml's defaults point at.
_DESTINATIONS = {
    "ghidra": f"tools/ghidra_{GHIDRA_VERSION}_PUBLIC",
    "ghidra-mcp": "tools/ghidra-mcp",
    "pcsx2-mcp": "tools/pcsx2-mcp",
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
    tool: str, dest: Path, platform: str, cache: Path, download: Downloader, pins: Sequence[Pin] | None = None
) -> None:
    """Install `tool` into `dest` (which must not exist yet) from its pins (`PINS` unless given).

    `dest` appears whole or not at all: `init` skips a tool whose folder exists, so a half-installed one (Ghidra
    without its extension) would never be repaired. Every file is downloaded and checked before anything is
    unpacked, the tool is built in a hidden folder beside `dest`, and that folder is renamed to `dest` at the end.
    """
    pins = list(pins) if pins is not None else PINS[tool](platform)
    files = [] if tool in CLONED else [fetch_file(pin, cache, download) for pin in pins]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=dest.parent, prefix=f".{dest.name}-") as temp:
        staging = Path(temp) / dest.name
        _install(tool, pins, files, staging, platform)
        staging.replace(dest)


def _install(tool: str, pins: Sequence[Pin], files: Sequence[Path], dest: Path, platform: str) -> None:
    """Build `tool` in the new folder `dest` from its checked downloads `files` (one per pin)."""
    if tool in CLONED:
        _clone(pins[0], dest)
        return
    if tool == "ghidra":
        unpack(files[0], dest)
        unpack(files[1], dest / "Ghidra" / "Extensions" / "ghidra-emotionengine-reloaded")
        return
    raise ConfigError(f"unknown tool {tool!r} on {platform}")


def describe(tool: str, platform: str) -> str:
    """One line naming what `tool` fetches on `platform`."""
    return ", ".join(f"{pin.name} {pin.version}" for pin in PINS[tool](platform))


def platform_name() -> str:
    """This machine's platform as the pins name it."""
    if sys.platform == "win32":
        return "windows"
    return "macos" if sys.platform == "darwin" else "linux"
