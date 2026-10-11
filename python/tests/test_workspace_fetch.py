# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.workspace_fetch: the optional tool downloads of `coney-tools workspace init`.

No test touches the network: downloads come from a dictionary of made-up archives.
"""

import hashlib
import io
import os
import subprocess
import tarfile
import zipfile
from pathlib import Path

import pytest

from coney_tools.config import ConfigError
from coney_tools.workspace_fetch import PINS, PLATFORMS, TOOLS, Pin, fetch_file, fetch_tool, tool_destination, unpack


def make_zip(files: dict[str, bytes]) -> bytes:
    """A zip archive holding `files` (archive path -> contents)."""
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in files.items():
            archive.writestr(name, data)
    return buffer.getvalue()


def make_tar_xz(files: dict[str, bytes]) -> bytes:
    """A .tar.xz archive holding `files`."""
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:xz") as archive:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            archive.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def pin_for(name: str, data: bytes) -> Pin:
    """A pin for made-up `data` served at a made-up URL ending in `name`."""
    return Pin(name, "1.0", f"https://example.invalid/{name}", hashlib.sha256(data).hexdigest())


class FakeWeb:
    """A downloader serving fixed bytes per URL and counting the downloads."""

    def __init__(self, files: dict[str, bytes]) -> None:
        self.files = files
        self.count = 0

    def __call__(self, url: str, out: Path) -> None:
        self.count += 1
        out.write_bytes(self.files[url])


def test_unpack_strips_a_single_top_folder(tmp_path: Path) -> None:
    archive = tmp_path / "a.zip"
    archive.write_bytes(make_zip({"top/bin/tool": b"x", "top/readme": b"y"}))
    unpack(archive, tmp_path / "out")
    assert (tmp_path / "out" / "bin" / "tool").read_bytes() == b"x"
    assert not (tmp_path / "out" / "top").exists()


def test_unpack_takes_only_the_named_subfolder(tmp_path: Path) -> None:
    archive = tmp_path / "a.zip"
    archive.write_bytes(make_zip({"rel/emulator.exe": b"e", "rel/server/dist/index.js": b"js"}))
    unpack(archive, tmp_path / "out", subfolder="server")
    assert (tmp_path / "out" / "dist" / "index.js").read_bytes() == b"js"
    assert not (tmp_path / "out" / "emulator.exe").exists()


def test_unpack_reads_tar_xz(tmp_path: Path) -> None:
    archive = tmp_path / "a.tar.xz"
    archive.write_bytes(make_tar_xz({"top/bin/tool": b"t"}))
    unpack(archive, tmp_path / "out")
    assert (tmp_path / "out" / "bin" / "tool").read_bytes() == b"t"


def test_unpack_keeps_a_single_top_level_app_bundle(tmp_path: Path) -> None:
    # macOS PCSX2's archive is one PCSX2-v2.9.94.app folder: stripping it would leave a bare Contents/.
    archive = tmp_path / "a.tar.xz"
    archive.write_bytes(make_tar_xz({"PCSX2-v2.app/Contents/Info.plist": b"p"}))
    unpack(archive, tmp_path / "out")
    assert (tmp_path / "out" / "PCSX2-v2.app" / "Contents" / "Info.plist").read_bytes() == b"p"


@pytest.mark.parametrize("entry", ["../escaped", "/absolute", "C:/absolute"])
def test_unpack_refuses_a_zip_entry_outside_the_folder(tmp_path: Path, entry: str) -> None:
    if entry.startswith("C:") and os.name != "nt":
        pytest.skip("a drive letter is only absolute on Windows")
    archive = tmp_path / "a.zip"
    archive.write_bytes(make_zip({"top/ok": b"x", entry: b"bad"}))
    with pytest.raises(ConfigError, match="outside"):
        unpack(archive, tmp_path / "work" / "out")
    assert not (tmp_path / "work" / "out").exists()
    assert not (tmp_path / "escaped").exists()


@pytest.mark.parametrize("entry", ["../escaped", "top/../../escaped"])
def test_unpack_refuses_a_tar_entry_outside_the_folder(tmp_path: Path, entry: str) -> None:
    archive = tmp_path / "a.tar.xz"
    archive.write_bytes(make_tar_xz({"top/ok": b"x", entry: b"bad"}))
    with pytest.raises(ConfigError, match="outside"):
        unpack(archive, tmp_path / "work" / "out")
    assert not (tmp_path / "work" / "out").exists()
    assert not (tmp_path / "escaped").exists()


def test_fetch_file_checks_the_hash_and_reuses_the_download(tmp_path: Path) -> None:
    data = b"archive"
    pin = pin_for("a.zip", data)
    web = FakeWeb({pin.url: data})
    first = fetch_file(pin, tmp_path, web)
    second = fetch_file(pin, tmp_path, web)
    assert first == second and first.read_bytes() == data
    assert web.count == 1


def test_fetch_file_refuses_a_wrong_hash_and_keeps_nothing(tmp_path: Path) -> None:
    pin = pin_for("a.zip", b"expected")
    with pytest.raises(ConfigError, match="SHA-256"):
        fetch_file(pin, tmp_path, FakeWeb({pin.url: b"tampered"}))
    assert list(tmp_path.iterdir()) == []


def test_fetch_file_leaves_no_part_file_when_the_download_fails(tmp_path: Path) -> None:
    pin = pin_for("a.zip", b"expected")

    def broken(url: str, out: Path) -> None:
        out.write_bytes(b"half")
        raise TimeoutError("timed out")

    with pytest.raises(TimeoutError):
        fetch_file(pin, tmp_path, broken)
    assert list(tmp_path.iterdir()) == []


def test_every_pin_is_an_upstream_release_with_a_hash() -> None:
    for tool in TOOLS:
        for platform in PLATFORMS:
            for pin in PINS[tool](platform):
                assert pin.url.startswith("https://github.com/"), pin
                assert len(pin.digest) == 40 if tool == "ghidra-mcp" else len(pin.digest) == 64, pin
                # LEGAL.md: the bootstrapper never fetches firmware or game files.
                assert "bios" not in pin.url.lower()


def test_ghidra_installs_the_extension_into_ghidra(tmp_path: Path) -> None:
    ghidra = make_zip({"ghidra_9_PUBLIC/support/analyzeHeadless": b"g"})
    extension = make_zip({"ghidra-emotionengine-reloaded/extension.properties": b"e"})
    pins = [pin_for("ghidra.zip", ghidra), pin_for("ee.zip", extension)]
    web = FakeWeb({pins[0].url: ghidra, pins[1].url: extension})
    dest = tmp_path / "tools" / "ghidra_9_PUBLIC"
    fetch_tool("ghidra", dest, "linux", tmp_path / "cache", web, pins)
    assert (dest / "support" / "analyzeHeadless").is_file()
    assert (dest / "Ghidra" / "Extensions" / "ghidra-emotionengine-reloaded" / "extension.properties").is_file()


def test_a_failed_extension_download_leaves_no_ghidra_behind(tmp_path: Path) -> None:
    # A half-installed Ghidra would be skipped as "exists" by every later run, so it must never be left.
    ghidra = make_zip({"ghidra_9_PUBLIC/support/analyzeHeadless": b"g"})
    pins = [pin_for("ghidra.zip", ghidra), pin_for("ee.zip", b"extension")]
    web = FakeWeb({pins[0].url: ghidra, pins[1].url: b"tampered"})
    dest = tmp_path / "tools" / "ghidra_9_PUBLIC"
    with pytest.raises(ConfigError, match="SHA-256"):
        fetch_tool("ghidra", dest, "linux", tmp_path / "cache", web, pins)
    assert not (tmp_path / "tools").exists() or list((tmp_path / "tools").iterdir()) == []


def test_a_failed_unpack_leaves_nothing_in_the_destination(tmp_path: Path) -> None:
    ghidra = make_zip({"ghidra_9_PUBLIC/support/analyzeHeadless": b"g"})
    extension = make_zip({"../escaped": b"e"})
    pins = [pin_for("ghidra.zip", ghidra), pin_for("ee.zip", extension)]
    web = FakeWeb({pins[0].url: ghidra, pins[1].url: extension})
    dest = tmp_path / "tools" / "ghidra_9_PUBLIC"
    with pytest.raises(ConfigError, match="outside"):
        fetch_tool("ghidra", dest, "linux", tmp_path / "cache", web, pins)
    assert list((tmp_path / "tools").iterdir()) == []


def test_pcsx2_on_macos_keeps_its_app_bundle(tmp_path: Path) -> None:
    data = make_tar_xz({"PCSX2-v2.app/Contents/MacOS/PCSX2": b"bin"})
    pins = [pin_for("pcsx2-macos-Qt.tar.xz", data)]
    dest = tmp_path / "emulators" / "pcsx2"
    fetch_tool("pcsx2", dest, "macos", tmp_path / "cache", FakeWeb({pins[0].url: data}), pins)
    assert (dest / "PCSX2-v2.app" / "Contents" / "MacOS" / "PCSX2").read_bytes() == b"bin"


def test_pcsx2_on_linux_is_an_executable_appimage(tmp_path: Path) -> None:
    data = b"\x7fELF"
    pins = [pin_for("pcsx2-x64-Qt.AppImage", data)]
    dest = tmp_path / "emulators" / "pcsx2"
    fetch_tool("pcsx2", dest, "linux", tmp_path / "cache", FakeWeb({pins[0].url: data}), pins)
    appimage = dest / "pcsx2-x64-Qt.AppImage"
    assert appimage.read_bytes() == data
    if os.name != "nt":
        assert os.access(appimage, os.X_OK)
    # Nothing but the emulator: the player dumps their own BIOS (LEGAL.md).
    assert not (dest / "bios").exists()


def test_pcsx2_mcp_keeps_only_the_node_server(tmp_path: Path) -> None:
    data = make_zip({"pcsx2-qt.exe": b"exe", "pcsx2-mcp-server/dist/index.js": b"js"})
    pins = [pin_for("mcp.zip", data)]
    dest = tmp_path / "tools" / "pcsx2-mcp"
    fetch_tool("pcsx2-mcp", dest, "macos", tmp_path / "cache", FakeWeb({pins[0].url: data}), pins)
    assert (dest / "dist" / "index.js").is_file()
    assert not (dest / "pcsx2-qt.exe").exists()


def test_ghidra_mcp_is_cloned_at_the_pinned_commit(tmp_path: Path) -> None:
    upstream = tmp_path / "upstream"
    upstream.mkdir()
    env = {
        **os.environ,
        "GIT_AUTHOR_NAME": "t",
        "GIT_AUTHOR_EMAIL": "t@example.invalid",
        "GIT_COMMITTER_NAME": "t",
        "GIT_COMMITTER_EMAIL": "t@example.invalid",
    }

    def git(*args: str) -> str:
        done = subprocess.run(["git", "-C", str(upstream), *args], check=True, capture_output=True, text=True, env=env)
        return done.stdout.strip()

    git("init", "-q")
    (upstream / "pom.xml").write_text("first", encoding="utf-8")
    git("add", "pom.xml")
    git("commit", "-q", "-m", "first")
    pinned = git("rev-parse", "HEAD")
    (upstream / "pom.xml").write_text("second", encoding="utf-8")
    git("commit", "-q", "-am", "second")

    dest = tmp_path / "tools" / "ghidra-mcp"
    pins = [Pin("ghidra-mcp", "test", str(upstream), pinned)]
    fetch_tool("ghidra-mcp", dest, "linux", tmp_path / "cache", FakeWeb({}), pins)
    assert (dest / "pom.xml").read_text(encoding="utf-8") == "first"


def test_destinations_match_the_workspace_layout(tmp_path: Path) -> None:
    assert tool_destination("ghidra", tmp_path) == tmp_path / "tools" / "ghidra_12.1.4_PUBLIC"
    assert tool_destination("pcsx2", tmp_path) == tmp_path / "emulators" / "pcsx2"
    assert tool_destination("ghidra-mcp", tmp_path) == tmp_path / "tools" / "ghidra-mcp"
    assert tool_destination("pcsx2-mcp", tmp_path) == tmp_path / "tools" / "pcsx2-mcp"
