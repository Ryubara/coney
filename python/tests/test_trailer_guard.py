# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for .claude/hooks/trailer_guard.py, the Claude Code hook that checks a commit's `Co-Authored-By` trailer.

The script is a standalone, standard-library-only file (the hook runs after shell commands), so it is loaded by path.
The input is synthetic: a hook payload shaped like a measured one (docs/guides/agents.md#trailer-guard:
`session_id`, `transcript_path`, `cwd`, `tool_input`, `tool_response`, and `agent_id` inside a subagent), a
transcript with `message.model` on the assistant entries, and a temporary git repository.
"""

import importlib.util
import io
import json
import os
import subprocess
from pathlib import Path
from types import ModuleType

import pytest
from conventions_support import REPO
from hooks_support import guards_run

SCRIPT = REPO / ".claude" / "hooks" / "trailer_guard.py"
SESSION = "11111111-2222-3333-4444-555555555555"
AGENT = "a3c1bc153ef108d66"
SONNET = "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
OPUS = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"


def load() -> ModuleType:
    """The hook script as a module."""
    spec = importlib.util.spec_from_file_location("trailer_guard", SCRIPT)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


GUARD = load()


@pytest.mark.parametrize(
    ("model", "name"),
    [
        ("claude-sonnet-5-5", "Claude Sonnet 5.5"),
        ("claude-opus-5-5", "Claude Opus 5.5"),
        ("claude-haiku-5-5", "Claude Haiku 5.5"),
        ("claude-opus-4-1-20250805", "Claude Opus 4.1"),
        ("claude-3-5-sonnet-20241022", "Claude Sonnet 3.5"),
        ("claude-opus-4", "Claude Opus 4"),
        ("claude-sonnet-5-5[1m]", "Claude Sonnet 5.5"),
        ("claude-fable-6", "Claude Fable 6"),
    ],
)
def test_the_display_name_is_derived_from_the_model_id(model: str, name: str) -> None:
    assert GUARD.display_name(model) == name


@pytest.mark.parametrize("model", ["", "<synthetic>", "sonnet", "gpt-5", "claude-", "claude-5-5", "claude-opus"])
def test_an_id_that_is_not_a_claude_model_has_no_name(model: str) -> None:
    assert GUARD.display_name(model) is None


def write_transcript(path: Path, models: list[str | None]) -> Path:
    """A transcript with one assistant entry per model (None: a user entry), plus noise the reader must skip."""
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = ["not json", json.dumps({"type": "summary"}), json.dumps(["list"])]
    for model in models:
        if model is None:
            lines.append(json.dumps({"type": "user", "message": {"role": "user", "content": "hi"}}))
        else:
            lines.append(json.dumps({"type": "assistant", "message": {"role": "assistant", "model": model}}))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return path


def test_the_latest_assistant_model_wins_and_synthetic_entries_are_skipped(tmp_path: Path) -> None:
    transcript = write_transcript(
        tmp_path / "t.jsonl", ["claude-opus-5-5", None, "claude-sonnet-5-5", "<synthetic>", None]
    )
    assert GUARD.latest_model(transcript) == "claude-sonnet-5-5"


def test_a_long_transcript_is_read_from_its_end(tmp_path: Path) -> None:
    transcript = write_transcript(tmp_path / "t.jsonl", ["claude-opus-5-5"] + [None] * 5)
    padding = json.dumps({"type": "user", "message": {"role": "user", "content": "x" * 5000}}) + "\n"
    with transcript.open("a", encoding="utf-8") as handle:
        handle.write(padding * 200)  # about a megabyte of user entries after the only assistant entry
    assert GUARD.latest_model(transcript) == "claude-opus-5-5"


def test_a_transcript_with_no_model_or_no_file_has_none(tmp_path: Path) -> None:
    assert GUARD.latest_model(write_transcript(tmp_path / "t.jsonl", [None, None])) is None
    assert GUARD.latest_model(tmp_path / "missing.jsonl") is None


def test_the_main_session_reads_its_own_transcript_and_an_agent_its_own_file(tmp_path: Path) -> None:
    main = tmp_path / "proj" / f"{SESSION}.jsonl"
    payload = {"session_id": SESSION, "transcript_path": str(main)}
    assert GUARD.transcript_for(payload) == main
    assert GUARD.transcript_for({**payload, "agent_id": AGENT}) == (
        tmp_path / "proj" / SESSION / "subagents" / f"agent-{AGENT}.jsonl"
    )
    assert GUARD.transcript_for({"session_id": SESSION}) is None
    assert GUARD.transcript_for({**payload, "agent_id": "../escape"}) is None


def test_trailers_are_read_from_the_end_of_a_message_and_matched_on_the_vendor_address() -> None:
    message = f"area: Do it\n\nBody.\n\n{OPUS}\nCo-authored-by: Someone <someone@example.org>\n"
    assert GUARD.anthropic_trailers(message) == ["Claude Opus 5.5"]
    assert GUARD.anthropic_trailers("title\n\nno trailer here\n") == []
    assert GUARD.anthropic_trailers("Co-Authored-By: Claude Opus 5.5 (1M context) <NOREPLY@anthropic.com>") == [
        "Claude Opus 5.5 (1M context)"
    ]


@pytest.mark.parametrize(
    "name", ["Claude Opus 5.5", "claude  opus 5.5", "Claude Opus 5.5 (1M context)", " Claude Opus 5.5 "]
)
def test_names_compare_without_case_spacing_or_a_parenthetical(name: str) -> None:
    assert GUARD.same_model(name, "Claude Opus 5.5")
    assert not GUARD.same_model(name, "Claude Sonnet 5.5")


def test_the_commits_a_command_made_are_read_from_gits_own_output() -> None:
    out = "[agents-trailer-model 1a2b3c4] agents: Add it\n 2 files changed\n[main (root-commit) deadbeef01] x\n"
    assert GUARD.committed_hashes(out) == ["1a2b3c4", "deadbeef01"]
    assert GUARD.committed_hashes("nothing to commit, working tree clean") == []
    assert GUARD.committed_hashes("[ok] fine\n[1/2 steps 100%] built") == []


def test_the_directories_to_look_in_are_the_cwd_then_the_commands_own() -> None:
    cwd = str(Path("work").resolve())
    dirs = GUARD.candidate_dirs("cd sub && git commit -m x; git -C 'other dir' commit", cwd)
    assert dirs[0] == cwd
    assert str(Path(cwd) / "sub") in dirs and str(Path(cwd) / "other dir") in dirs


# --- end to end, in a temporary repository ---------------------------------------------------------------------


def git(repo: Path, *args: str) -> str:
    """Run git in the repository with no signing and a fixed identity; the output."""
    cmd = ["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgsign=false"]
    done = subprocess.run([*cmd, *args], capture_output=True, text=True, check=True)
    return done.stdout


def commit(repo: Path, message: str) -> tuple[str, str]:
    """Make an empty commit; the hash and git's own output (what the hook is given as the tool's stdout)."""
    out = git(repo, "commit", "--allow-empty", "-m", message)
    return git(repo, "rev-parse", "HEAD").strip(), out


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """An empty repository."""
    path = tmp_path / "repo"
    path.mkdir()
    git(path, "init", "-q")
    return path


def make_payload(
    tmp_path: Path, repo: Path, stdout: str, model: str | None, *, agent: bool, command: str = "git commit -m x"
) -> dict[str, object]:
    """A PostToolUse payload as Claude Code sends it, with the transcript the running agent would have."""
    main = tmp_path / "projects" / "p" / f"{SESSION}.jsonl"
    # The main session runs on something else, so reading the wrong transcript would be caught.
    write_transcript(main, ["claude-opus-5-5"])
    payload: dict[str, object] = {
        "session_id": SESSION,
        "transcript_path": str(main),
        "cwd": str(repo),
        "hook_event_name": "PostToolUse",
        "tool_name": "Bash",
        "tool_input": {"command": command},
        "tool_response": {"stdout": stdout, "stderr": "", "interrupted": False},
    }
    if agent:
        payload["agent_id"] = AGENT
        payload["agent_type"] = "engineer"
        write_transcript(main.parent / SESSION / "subagents" / f"agent-{AGENT}.jsonl", [None, model])
    elif model is not None:
        write_transcript(main, [model])
    return payload


def test_a_subagent_crediting_the_coordinators_model_is_told_to_amend(tmp_path: Path, repo: Path) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    message = GUARD.check(payload)
    assert message is not None
    assert "Claude Opus 5.5" in message and "Claude Sonnet 5.5" in message and "claude-sonnet-5-5" in message
    assert "git commit --amend" in message and "PR.md" in message and "system reminder" in message


def test_a_subagent_crediting_its_own_model_passes(tmp_path: Path, repo: Path) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{SONNET}")
    assert GUARD.check(make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)) is None


def test_the_main_session_is_checked_against_its_own_transcript(tmp_path: Path, repo: Path) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    assert GUARD.check(make_payload(tmp_path, repo, out, "claude-opus-5-5", agent=False)) is None
    assert GUARD.check(make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=False)) is not None


def test_the_agents_model_is_not_confused_with_the_main_sessions(tmp_path: Path, repo: Path) -> None:
    # The main transcript says Opus; the Sonnet subagent crediting Opus must still be caught.
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    assert GUARD.latest_model(GUARD.transcript_for(payload)) == "claude-sonnet-5-5"
    assert GUARD.check(payload) is not None


def test_every_commit_a_command_made_is_checked(tmp_path: Path, repo: Path) -> None:
    _, first = commit(repo, f"one\n\n{SONNET}")
    _, second = commit(repo, f"two\n\n{OPUS}")
    assert GUARD.check(make_payload(tmp_path, repo, first + second, "claude-sonnet-5-5", agent=True)) is not None


def test_a_commit_in_another_repository_is_found_through_the_commands_own_cd(tmp_path: Path, repo: Path) -> None:
    other = tmp_path / "elsewhere"
    other.mkdir()
    git(other, "init", "-q")
    _, out = commit(other, f"two\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True, command=f'cd "{other}" && git commit')
    assert GUARD.check(payload) is not None


@pytest.mark.parametrize("case", ["no trailer", "other vendor", "unknown model", "no model", "failed commit"])
def test_it_fails_open(tmp_path: Path, repo: Path, case: str) -> None:
    message = {
        "no trailer": "agents: Do it",
        "other vendor": "agents: Do it\n\nCo-Authored-By: Gemini <noreply@google.com>",
    }.get(case, f"agents: Do it\n\n{OPUS}")
    _, out = commit(repo, message)
    model = {"unknown model": "mystery-model", "no model": None}.get(case, "claude-sonnet-5-5")
    if case == "failed commit":
        out = "nothing to commit, working tree clean"
    payload = make_payload(tmp_path, repo, out, model, agent=True)
    assert GUARD.check(payload) is None


def test_it_fails_open_without_a_transcript_or_outside_a_repository(tmp_path: Path, repo: Path) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    del payload["transcript_path"]
    assert GUARD.check(payload) is None
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    payload["cwd"] = str(tmp_path / "not-a-repo")
    assert GUARD.check(payload) is None
    payload["tool_response"] = None
    assert GUARD.check(payload) is None


def test_the_tool_response_may_be_a_plain_string(tmp_path: Path, repo: Path) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    payload["tool_response"] = out
    assert GUARD.check(payload) is not None


def run(monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str) -> tuple[int, str]:
    """Run main() on the given stdin; the exit code and stderr."""
    # Claude Code writes UTF-8 bytes; the hook must not depend on the console's code page.
    monkeypatch.setattr("sys.stdin", io.TextIOWrapper(io.BytesIO(stdin.encode("utf-8")), encoding="cp1252"))
    code = GUARD.main()
    return code, capsys.readouterr().err


def test_a_mismatch_exits_2_with_the_reason_on_stderr(
    tmp_path: Path, repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    code, err = run(monkeypatch, capsys, json.dumps(payload))
    assert code == 2 and "Claude Sonnet 5.5" in err


def test_a_matching_commit_exits_0_silently(
    tmp_path: Path, repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    _, out = commit(repo, f"agents: Do it\n\n{SONNET}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    assert run(monkeypatch, capsys, json.dumps(payload)) == (0, "")


@pytest.mark.parametrize(
    "stdin", ["", "not json", "[]", "{}", '{"tool_input": null}', '{"tool_input": {"command": 5}}', '{"cwd": 3}']
)
def test_input_the_hook_cannot_read_never_blocks(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], stdin: str
) -> None:
    assert run(monkeypatch, capsys, stdin) == (0, "")


def test_a_command_that_never_mentions_commit_is_not_inspected(
    tmp_path: Path, repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # A command's output that looks like git's commit line must not start any work when the command is no commit.
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True, command="echo hello")
    assert run(monkeypatch, capsys, json.dumps(payload)) == (0, "")


def test_the_dispatcher_runs_the_hook_after_commit_commands_only() -> None:
    for command in ("git commit -m x", "git -C repo commit -m x", "git --no-pager commit -m x"):
        assert guards_run("Bash", command, "PostToolUse") == ["trailer_guard"]
        assert guards_run("PowerShell", command, "PostToolUse") == ["trailer_guard"]
    assert guards_run("Bash", "git status", "PostToolUse") == []
    assert guards_run("Read", None, "PostToolUse") == []


def test_a_non_ascii_path_in_the_input_survives_a_legacy_console_code_page(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    folder = tmp_path / "wärriors ☃"
    folder.mkdir()
    repo = folder / "repo"
    repo.mkdir()
    git(repo, "init", "-q")
    _, out = commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True)
    assert run(monkeypatch, capsys, json.dumps(payload, ensure_ascii=False))[0] == 2


def test_the_output_of_an_amend_names_the_new_commit(tmp_path: Path, repo: Path) -> None:
    commit(repo, f"agents: Do it\n\n{SONNET}")
    out = git(repo, "commit", "--amend", "--allow-empty", "-m", f"agents: Do it\n\n{OPUS}")
    assert GUARD.committed_hashes(out), out
    assert (
        GUARD.check(make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True, command="git commit --amend"))
        is not None
    )
    out = git(repo, "commit", "--amend", "--allow-empty", "-m", f"agents: Do it\n\n{SONNET}")
    assert (
        GUARD.check(make_payload(tmp_path, repo, out, "claude-sonnet-5-5", agent=True, command="git commit --amend"))
        is None
    )


@pytest.mark.parametrize("output", ["", "  \n"])
def test_a_quiet_commit_is_found_from_the_head_of_the_working_directory(
    tmp_path: Path, repo: Path, output: str
) -> None:
    # `git commit -q` and a pipe to Out-Null print nothing, so a just-made HEAD is the commit to check.
    commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, output, "claude-sonnet-5-5", agent=True, command="git commit -q -m x")
    assert GUARD.check(payload) is not None
    payload["tool_response"] = {"stdout": "", "stderr": ""}
    assert GUARD.check(payload) is not None


def test_a_quiet_commit_is_found_through_the_commands_own_directory(tmp_path: Path, repo: Path) -> None:
    other = tmp_path / "elsewhere"
    other.mkdir()
    git(other, "init", "-q")
    commit(other, f"agents: Do it\n\n{OPUS}")
    command = f'cd "{other}" && git commit -q -m x'
    assert GUARD.check(make_payload(tmp_path, repo, "", "claude-sonnet-5-5", agent=True, command=command)) is not None


def test_empty_output_does_not_blame_an_old_head(tmp_path: Path, repo: Path) -> None:
    # A commit that did not happen leaves an old HEAD, perhaps another agent's: only a commit made just now counts.
    old = "2020-01-01T00:00:00"
    env = {**os.environ, "GIT_COMMITTER_DATE": old, "GIT_AUTHOR_DATE": old}
    subprocess.run(
        ["git", "-C", str(repo), "-c", "user.name=t", "-c", "user.email=t@example.org", "-c", "commit.gpgsign=false",
         "commit", "--allow-empty", "-m", f"old\n\n{OPUS}"],
        check=True, capture_output=True, env=env,
    )  # fmt: skip
    payload = make_payload(tmp_path, repo, "", "claude-sonnet-5-5", agent=True, command="git commit -q -m x")
    assert GUARD.check(payload) is None


def test_output_that_is_not_a_commit_line_is_not_replaced_by_head(tmp_path: Path, repo: Path) -> None:
    commit(repo, f"agents: Do it\n\n{OPUS}")
    payload = make_payload(tmp_path, repo, "nothing to commit, working tree clean", "claude-sonnet-5-5", agent=True)
    assert GUARD.check(payload) is None
