# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/lua_guard.py, the pre-commit check and Claude Code hook that refuse decompiled game Lua.

The script is a standalone, standard-library-only file, so it is loaded by path. Everything here is synthetic: the
"decompiled" lines are our own, fingerprinted into a temporary shared folder.
"""

from __future__ import annotations

import importlib.util
import json
from collections.abc import Mapping
from pathlib import Path
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import DISPATCH, guards_run

SCRIPT = REPO / ".claude" / "hooks" / "lua_guard.py"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("lua_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()

#: Our own lines standing in for a decompiled script.
SCRIPT_LINES = [
    "function Setup()",
    '    SomeBinding("a made-up name", 1, 2)',
    "    local l1 = MakeThing(Objects.first_thing)",
    '    OtherBinding(l1, "another made-up name")',
    "    if l1 then CallSomething(l1, 0.5) end",
    "end",
]


def _checkout(tmp_path: Path) -> Path:
    """A fake checkout in a workspace layout (`<workspace>/repos/coney`) with `lua_dir` set."""
    root = tmp_path / "workspace" / "repos" / "coney"
    root.mkdir(parents=True)
    (root / "coney.local.example.toml").write_text("", encoding="utf-8")
    (root / "coney.local.toml").write_text('lua_dir = "../../shared-lua"\n', encoding="utf-8")
    shared = tmp_path / "workspace" / "shared-lua"
    shared.mkdir()
    prints = {GUARD.fingerprint(line) for line in SCRIPT_LINES} - {None}
    (shared / GUARD.FINGERPRINTS).write_text("\n".join(sorted(prints)) + "\n", encoding="ascii")
    return root


def test_marker_line_is_refused() -> None:
    """A file `lua decompile` wrote carries the marker, which is refused wherever it is in the text."""
    line = GUARD.marker_line("level1.lua", b"chunk bytes")
    assert GUARD.problem(line + "\nx = 1\n", frozenset()) is not None
    assert GUARD.problem("text before\n" + line + "\n", frozenset()) is not None
    # Describing the marker in a guide does not trip it: the hash is not there.
    assert GUARD.problem("Each file starts with `-- lua4 decompile <hash> <name>`.\n", frozenset()) is None


def test_a_pasted_block_is_refused_and_a_quoted_line_passes(tmp_path: Path) -> None:
    """Three consecutive fingerprinted lines are a pasted block; one quoted line, or short lines, are not."""
    root = _checkout(tmp_path)
    prints = GUARD.load_fingerprints(root)
    assert prints
    block = "\n".join(SCRIPT_LINES[1:4])
    assert GUARD.problem("Some prose.\n" + block + "\nMore prose.\n", prints) is not None
    assert GUARD.problem("The script calls `" + SCRIPT_LINES[1].strip() + "` first.\n", prints) is None
    assert GUARD.problem("function Setup()\nend\nend\n", prints) is None
    reindented = "\n".join("\t" + "  ".join(line.split()) for line in SCRIPT_LINES[1:4])
    assert GUARD.problem(reindented, prints) is not None


#: Our own notes, long enough to be distinctive, that a page might put between pasted lines.
NOTES = [
    "this line is a note of our own about the call",
    "and this one says what the script does next",
]


def _wrapped_forms() -> dict[str, str]:
    """The block SCRIPT_LINES[1:5] as it might land in a page or a listing, each form named for the failure message."""
    block = [line.strip() for line in SCRIPT_LINES[1:5]]
    return {
        "quoted": "\n".join("> " + line for line in block),
        "nested quote": "\n".join("> > " + line for line in block),
        "bullets": "\n".join("- " + line for line in block),
        "numbered": "\n".join(f"{index}. {line}" for index, line in enumerate(block, start=1)),
        "inline code bullets": "\n".join("* `" + line + "`" for line in block),
        "table": "| Line | What it does |\n| --- | --- |\n" + "\n".join(f"| `{line}` | {NOTES[0]} |" for line in block),
        "commented out": "\n".join("-- " + line for line in block),
        "commented out, no space": "\n".join("--" + line for line in block),
        "trailing comments": "\n".join(line + "  -- " + NOTES[index % 2] for index, line in enumerate(block)),
        "interleaved notes": "\n".join(line + "\n" + NOTES[index % 2] for index, line in enumerate(block)),
        "quoted bullets with comments": "\n".join("> - " + line + " -- why" for line in block),
    }


@pytest.mark.parametrize("form", list(_wrapped_forms()))
def test_a_wrapped_paste_is_refused(tmp_path: Path, form: str) -> None:
    """A block wrapped in Markdown or comment markup, or broken up by notes of our own, is still a pasted block."""
    prints = GUARD.load_fingerprints(_checkout(tmp_path))
    assert GUARD.problem("Some prose.\n" + _wrapped_forms()[form] + "\nMore prose.\n", prints) is not None, form


def test_scattered_lines_pass(tmp_path: Path) -> None:
    """Fingerprinted lines spread out among more lines of our own than the window allows are not a pasted block, and a
    `--` inside a string is not a comment."""
    prints = GUARD.load_fingerprints(_checkout(tmp_path))
    spread = [SCRIPT_LINES[1], NOTES[0], NOTES[1], SCRIPT_LINES[2], NOTES[0], NOTES[1], SCRIPT_LINES[3]]
    assert GUARD.problem("\n".join(spread), prints) is None
    for line in ('Print("a -- b is not a comment", x)', "Print('a -- b', [[c -- d]], x)"):
        assert GUARD.normalised(line) == line


def test_normalising_keeps_a_decompile_line(tmp_path: Path) -> None:
    """A line as the decompiler writes it (indented code, negative numbers in a list) keeps its text, so the
    fingerprints `--all` writes are the ones a paste is checked against."""
    for line in ("    -1, -2, -3, 4, 5, 6, 7, 8, 9, 10,", "    local l1 = Thing.field[1] * 2 - 3", "x = 'a|b'"):
        assert GUARD.normalised(line) == " ".join(line.split())


def test_lua_dir_matches_coney_tools(tmp_path: Path) -> None:
    """The hook finds the shared folder where coney-tools does: the configured `lua_dir`, else the default."""
    from coney_tools import lua_decompile

    root = _checkout(tmp_path)
    assert GUARD.lua_dir(root) == lua_decompile.lua_dir(root) == (tmp_path / "workspace" / "shared-lua").resolve()
    (root / "coney.local.toml").write_text("", encoding="utf-8")
    assert GUARD.lua_dir(root) == lua_decompile.lua_dir(root) == tmp_path / "workspace" / "scratch" / "lua"


def _hook(payload: Mapping[str, object], capsys: pytest.CaptureFixture[str]) -> tuple[int, str]:
    code = GUARD.hook_main(json.dumps(payload))
    return code, capsys.readouterr().err


def test_hook_blocks_writes_into_a_checkout(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    """Write, Edit and MultiEdit into a checkout are blocked when the new text is refused; outside, they pass."""
    root = _checkout(tmp_path)
    inside = str(root / "docs" / "page.md")
    outside = str(tmp_path / "workspace" / "scratch" / "notes.md")
    block = "\n".join(SCRIPT_LINES)
    code, err = _hook({"tool_name": "Write", "tool_input": {"file_path": inside, "content": block}}, capsys)
    assert code == 2 and "LEGAL.md" in err
    code, _ = _hook({"tool_name": "Write", "tool_input": {"file_path": outside, "content": block}}, capsys)
    assert code == 0
    edit = {"tool_name": "Edit", "tool_input": {"file_path": inside, "old_string": "a", "new_string": block}}
    assert _hook(edit, capsys)[0] == 2
    multi = {"tool_name": "MultiEdit", "tool_input": {"file_path": inside, "edits": [{"new_string": block}]}}
    assert _hook(multi, capsys)[0] == 2
    harmless = {"tool_name": "Write", "tool_input": {"file_path": inside, "content": "x = 1\n"}}
    assert _hook(harmless, capsys)[0] == 0


def test_hook_passes_what_it_cannot_read(capsys: pytest.CaptureFixture[str]) -> None:
    """Bad JSON or another tool's input passes."""
    assert GUARD.hook_main("not json") == 0
    assert GUARD.hook_main(json.dumps({"tool_input": {"command": "ls"}})) == 0
    assert capsys.readouterr().err == ""


def test_files_mode_names_each_refused_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    """The pre-commit mode prints each refused file and exits 1; clean files exit 0."""
    root = _checkout(tmp_path)
    monkeypatch.chdir(root)
    copied = root / "copied.lua"
    copied.write_text(GUARD.marker_line("x.lua", b"x") + "\nx = 1\n", encoding="utf-8")
    clean = root / "clean.md"
    clean.write_text("Nothing to see.\n", encoding="utf-8")
    assert GUARD.files_main([str(copied), str(clean)]) == 1
    assert str(copied) in capsys.readouterr().out
    assert GUARD.files_main([str(clean)]) == 0


def test_pre_commit_runs_the_guard() -> None:
    """The repository's pre-commit configuration runs this script on every text file."""
    config = (REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8")
    assert "id: no-decompiled-lua" in config and ".claude/hooks/lua_guard.py" in config


def test_the_dispatcher_runs_the_hook_for_every_file_write() -> None:
    """`.claude/hooks/dispatch.py` (which `.claude/settings.json` runs for every tool) runs the guard before Write,
    Edit, MultiEdit and NotebookEdit, and not for a shell command or a read."""
    for tool in ("Write", "Edit", "MultiEdit", "NotebookEdit"):
        assert "lua_guard" in guards_run(tool)
    assert "lua_guard" not in guards_run("Bash", "ls") and "lua_guard" not in guards_run("Read")


def test_the_dispatcher_refuses_a_pasted_block(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """Through the dispatcher, a Write of a decompile into a checkout is denied with the guard's reason."""
    root = _checkout(tmp_path)
    monkeypatch.setattr(DISPATCH, "_modules", {})
    payload = {
        "hook_event_name": "PreToolUse",
        "tool_name": "Write",
        "tool_input": {"file_path": str(root / "docs" / "page.md"), "content": "\n".join(SCRIPT_LINES)},
    }
    code, _, err = DISPATCH.dispatch(json.dumps(payload), {})
    assert code == 2 and "decompiled game Lua" in err
