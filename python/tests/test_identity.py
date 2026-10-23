# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.identity, which reads who ran a command from the file the Claude Code hooks wrote.

The hooks put a random token in `CONEY_AGENT_TOKEN` and the identity in `~/.coney/identity/<hash>.json`
(docs/guides/agents.md#identity). A typed or changed token finds no file, so the answer is "unverified", never another
role. The folder is a parameter; nothing here touches the real one.
"""

import json
from pathlib import Path

import pytest

from coney_tools import identity

TOKEN = "0123456789abcdef0123456789abcdef"
NOW = 1_000_000.0


def write(folder: Path, token: str = TOKEN, **fields: object) -> Path:
    """An identity file as the hook writes it."""
    folder.mkdir(parents=True, exist_ok=True)
    data = {
        "version": 1,
        "id": "a1",
        "type": "engineer",
        "model": "claude-sonnet-5-5",
        "session": "s1",
        "tool_use_id": "t1",
        "issued": NOW - 5,
        "expires": NOW + 300,
        **fields,
    }
    path = folder / identity.file_name(token)
    path.write_text(json.dumps(data), encoding="utf-8")
    return path


def test_a_valid_token_gives_the_identity_the_hook_wrote(tmp_path: Path) -> None:
    write(tmp_path)
    found = identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW)
    assert found == identity.Identity(id="a1", type="engineer", model="claude-sonnet-5-5", verified=True)
    assert found.agent() == {"id": "a1", "type": "engineer", "model": "claude-sonnet-5-5"}


def test_reading_does_not_consume_the_file(tmp_path: Path) -> None:
    # `coney-tools a && git commit` and a tool that starts another tool read it more than once.
    path = write(tmp_path)
    for _ in range(3):
        assert identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW).verified
    assert path.exists()


@pytest.mark.parametrize("token", [None, "", "x", "../" + TOKEN, TOKEN.upper(), TOKEN[:-1], TOKEN + "0"])
def test_a_missing_or_malformed_token_is_unverified(tmp_path: Path, token: str | None) -> None:
    write(tmp_path)
    env = {} if token is None else {"CONEY_AGENT_TOKEN": token}
    found = identity.load(env, folder=tmp_path, now=NOW)
    assert not found.verified and (found.id, found.type, found.model) == ("unknown", "unknown", "unknown")


def test_a_token_without_a_file_is_unverified_whatever_else_is_typed(tmp_path: Path) -> None:
    write(tmp_path)
    env = {
        "CONEY_AGENT_TOKEN": "f" * 32,
        "CONEY_AGENT_ID": "a1",
        "CONEY_AGENT_TYPE": "coordinator",
        "CONEY_AGENT_MODEL": "claude-opus-5-5",
        "CONEY_AGENT_INJECTED": "1",
    }
    found = identity.load(env, folder=tmp_path, now=NOW)
    assert not found.verified and found.type == "unknown"


def test_an_expired_or_future_file_is_unverified(tmp_path: Path) -> None:
    write(tmp_path, expires=NOW - 1)
    assert not identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW).verified
    write(tmp_path, issued=NOW + 3600, expires=NOW + 4000)
    assert not identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW).verified


@pytest.mark.parametrize("text", ["", "not json", "[]", '{"id": "a1"}', '{"version": 2}'])
def test_a_file_that_is_not_ours_is_unverified(tmp_path: Path, text: str) -> None:
    (tmp_path / identity.file_name(TOKEN)).write_text(text, encoding="utf-8")
    assert not identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW).verified


def test_fields_that_are_not_text_do_not_pass_as_identity(tmp_path: Path) -> None:
    write(tmp_path, type=["coordinator"], id=7)
    assert not identity.load({"CONEY_AGENT_TOKEN": TOKEN}, folder=tmp_path, now=NOW).verified


def test_current_reads_once_so_a_slow_start_cannot_lose_it(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    path = write(tmp_path, expires=1e18, issued=0)
    monkeypatch.setenv("CONEY_AGENT_TOKEN", TOKEN)
    monkeypatch.setattr(identity, "default_folder", lambda: tmp_path)
    identity.reset()
    first = identity.current()
    path.unlink()  # the hook's PostToolUse or the sweep removed it meanwhile
    assert first.verified and identity.current() is first
    identity.reset()
    assert not identity.current().verified


def test_the_default_folder_is_under_the_home_and_not_settable_by_environment(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setenv("CONEY_IDENTITY_DIR", str(tmp_path))
    assert identity.default_folder() == Path.home() / ".coney" / "identity"


def test_the_command_line_reads_it_before_anything_else(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    from coney_tools import cli

    write(tmp_path, expires=1e18, issued=0)
    monkeypatch.setenv("CONEY_AGENT_TOKEN", TOKEN)
    monkeypatch.setattr(identity, "default_folder", lambda: tmp_path)
    identity.reset()
    with pytest.raises(SystemExit):
        cli.main(["--no-such-option"])  # argument parsing fails at once; the identity was read before it
    assert identity._current is not None and identity._current.verified
