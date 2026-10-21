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
from coney_tools.workspace_fetch import (
    CCACHE_VERSION,
    CLONED,
    PINS,
    PLATFORMS,
    TOOLS,
    Pin,
    fetch_file,
    fetch_tool,
    newest_release,
    packaged,
    tool_destination,
    unpack,
)


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
                assert len(pin.digest) == 40 if tool in CLONED else len(pin.digest) == 64, pin
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


def test_pcsx2_comes_from_the_forks_release_never_a_stock_one() -> None:
    # The copies run the PCSX2-MCP fork's build (pcsx2 setup-copies), never a stock PCSX2: a newcomer downloads the
    # fork's release, and the clone stays for changing the fork.
    assert "pcsx2" not in TOOLS
    assert [pin.url for pin in PINS["pcsx2-mcp"]("windows")] == ["https://github.com/Ryubara/PCSX2-MCP"]
    (release,) = PINS["pcsx2-mcp-release"]("windows")
    assert release.url.startswith("https://github.com/Ryubara/PCSX2-MCP/releases/download/")
    assert release.url.endswith("-win64.zip")
    # Windows only: there is no Linux or macOS build of the fork.
    assert PINS["pcsx2-mcp-release"]("linux") == []
    assert tool_destination("pcsx2-mcp-release", Path("ws")).parent == Path("ws") / "tools"


def test_the_fork_release_unpacks_flat_with_its_server(tmp_path: Path) -> None:
    data = make_zip({"pcsx2-qt.exe": b"exe", "pcsx2-mcp-server/dist/index.js": b"js"})
    pin = pin_for("PCSX2-MCP-v9-win64.zip", data)
    dest = tmp_path / "tools" / "PCSX2-MCP-v9-win64"
    fetch_tool("pcsx2-mcp-release", dest, "windows", tmp_path / "cache", FakeWeb({pin.url: data}), [pin])
    assert (dest / "pcsx2-qt.exe").is_file()
    assert (dest / "pcsx2-mcp-server" / "dist" / "index.js").is_file()


def test_the_newest_fork_build_is_found_by_version_then_build(tmp_path: Path) -> None:
    assert newest_release(tmp_path) is None
    for name in (
        "PCSX2-MCP-coney-v2.9.129.7-win64",
        "PCSX2-MCP-coney-v2.9.129.10-win64",
        "PCSX2-MCP-coney-v2.9.94.20-win64",
        "PCSX2-MCP-coney-v2.9.200.1-win64.sha256",
        "something-else",
    ):
        (tmp_path / "release" / name).mkdir(parents=True)
    (tmp_path / "release" / "PCSX2-MCP-coney-v2.9.300.1-win64").write_text("a file", encoding="utf-8")
    assert newest_release(tmp_path) == tmp_path / "release" / "PCSX2-MCP-coney-v2.9.129.10-win64"


@pytest.mark.parametrize("tool", sorted(CLONED))
def test_a_cloned_tool_is_checked_out_at_the_pinned_commit(tmp_path: Path, tool: str) -> None:
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

    dest = tmp_path / "tools" / tool
    pins = [Pin(tool, "test", str(upstream), pinned)]
    fetch_tool(tool, dest, "linux", tmp_path / "cache", FakeWeb({}), pins)
    assert (dest / "pom.xml").read_text(encoding="utf-8") == "first"


def test_destinations_match_the_workspace_layout(tmp_path: Path) -> None:
    assert tool_destination("ghidra", tmp_path) == tmp_path / "tools" / "ghidra_12.1.4_PUBLIC"
    assert tool_destination("ghidra-mcp", tmp_path) == tmp_path / "tools" / "ghidra-mcp"
    assert tool_destination("pcsx2-mcp", tmp_path) == tmp_path / "tools" / "pcsx2-mcp"


def test_ccache_is_fetched_on_windows_only_and_pinned_like_ci() -> None:
    # One version for local builds and CI: the ccache action pins the same release and digest.
    action = (Path(__file__).parents[2] / ".github" / "actions" / "ccache" / "action.yml").read_text(encoding="utf-8")
    (pin,) = PINS["ccache"]("windows")
    assert f"$version = '{CCACHE_VERSION}'" in action and f"$expected = '{pin.digest}'" in action
    assert pin.version == CCACHE_VERSION and CCACHE_VERSION in pin.url
    assert PINS["ccache"]("linux") == [] and packaged("ccache", "linux") and not packaged("ccache", "windows")


def test_ccache_unpacks_to_a_folder_holding_the_program(tmp_path: Path) -> None:
    archive = make_zip({"ccache-9-windows-x86_64/ccache.exe": b"x", "ccache-9-windows-x86_64/NEWS.md": b"n"})
    pin = pin_for("ccache.zip", archive)
    dest = tmp_path / "tools" / "ccache-9"
    fetch_tool("ccache", dest, "windows", tmp_path / "cache", FakeWeb({pin.url: archive}), [pin])
    assert (dest / "ccache.exe").is_file()
    assert tool_destination("ccache", tmp_path) == tmp_path / "tools" / f"ccache-{CCACHE_VERSION}"
