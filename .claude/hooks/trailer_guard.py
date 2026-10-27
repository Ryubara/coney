# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PostToolUse hook: tell an agent whose commit credits another model than the one it runs on to amend it.

An agent ends its commits with `Co-Authored-By: <its model> <noreply@anthropic.com>` (AGENTS.md). The coordinator's
session gets a system reminder naming the coordinator's own model for that line, and subagents see the reminder too
and copy it: on 2026-10-13 a Sonnet agent twice credited Opus. The pull request's Model line then followed the wrong
trailer, and `coney-tools pr check-body --worktree` passed because the body and the trailers agreed with each other.

Claude Code puts no model in a hook's input, so this hook reads it from the transcript of the agent that ran the
command (the fields it reads or ignores are listed in docs/guides/agents.md#trailer-guard):

* every hook gets `transcript_path`, always the *main* session's transcript;
* inside a subagent it also gets `agent_id`, and that agent's own transcript is
  `<transcript_path without .jsonl>/subagents/agent-<agent_id>.jsonl` (reading the main one would name the wrong
  model, which is the very mistake being caught), and `agent_type`, the agent's role (`engineer`), which this hook
  does not need: the transcript names the model, the role does not;
* every assistant entry of a transcript has `message.model`, such as `claude-sonnet-5-5`; the latest one is the model
  running now. The display name (`Claude Sonnet 5.5`) is derived from the id, so no table drifts when a model ships.

After a shell command the hook reads the commits `git commit` printed (`[branch 1a2b3c4] subject`, in the tool's
output), finds their `Co-Authored-By` trailers whose address is the vendor's (`@anthropic.com`) and, when one names
another model, exits 2: the reason on stderr is shown to the agent, which amends the commit. It runs after the
commit because that is the only point where the final message exists whatever the way it was given (`-m`, `-F`, an
editor, `--amend`, a here-document). The hook dispatcher (`dispatch.py`) runs it after a shell command that runs
`git commit` (also `git -C ... commit`, `git -c ... commit`, `git --no-pager commit`, `git.exe commit` and a full path
to git), so ordinary commands, and commands that only mention the word (`git config commit.gpgsign`), never start it.
A commit made by a program the command only starts (a script) prints nothing it can read.

A commit that printed nothing (`git commit -q`, a pipe to Out-Null) has no hash to read, so when the whole output is
empty the hook looks at the HEAD of the working directory and of every directory the command names, and judges it
only if it was committed in the last two minutes: an old HEAD, perhaps another agent's, is not blamed on a commit
that never happened.

It fails open on everything it cannot establish: no transcript or model, an id that is not a Claude model, no
trailer, a trailer from another vendor, a commit it cannot find. A person committing outside Claude Code never runs
it. Standard library only.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

#: How much of the end of a transcript to read first; it doubles until a model is found or the file is all read.
_TAIL = 256 * 1024
#: How old (seconds) a HEAD may be and still count as the commit a silent command just made.
_RECENT = 120
#: The most of a transcript ever read (a transcript is a few megabytes, so this is never reached in practice).
_MAX_READ = 64 * 1024 * 1024
#: A version of a model id that is a build date (`claude-opus-4-1-20250805`), not part of the version.
_DATE = re.compile(r"\d{8}")
#: A Co-Authored-By line: the name, then the address in angle brackets.
_TRAILER = re.compile(
    r"^[ \t]*co-authored-by:[ \t]*(?P<name>[^<\n]+?)[ \t]*<(?P<email>[^>\n]+)>[ \t]*$",
    re.IGNORECASE | re.MULTILINE,
)
#: The first line git prints for a commit it made: `[branch 1a2b3c4] subject`, `[main (root-commit) 1a2b3c4] x`.
_COMMIT_LINE = re.compile(r"^\[[^\]\n]*? (?P<hash>[0-9a-f]{7,40})\]", re.MULTILINE)
#: A directory the command moves to (`cd X`, `cd /d X`) or names for git (`git -C X`), quoted or not.
_DIR = re.compile(
    r"""(?:\bcd[ \t]+(?:/d[ \t]+)?|\bgit(?:\.exe)?[ \t]+(?:-\S+[ \t]+)*?-C[ \t]+)"""
    r"""(?P<dir>"[^"\n]+"|'[^'\n]+'|[^\s;&|]+)"""
)
#: An agent id as Claude Code writes it, so it cannot name a path outside the transcript folder.
_AGENT_ID = re.compile(r"[A-Za-z0-9_-]+")
#: A path in the style Git Bash prints (`/c/Users/x`), which Windows programs need as `c:/Users/x`.
_MSYS = re.compile(r"^/(?P<drive>[A-Za-z])/(?P<rest>.*)$")
#: A Windows drive path (`C:/x`): a directory of its own on any system, not a name under the working directory.
#: A copy of `hooklib._DRIVE`: this guard stands alone and must work when `hooklib` does not import.
_DRIVE_PATH = re.compile(r"^[A-Za-z]:[\\/]")


def display_name(model: str) -> str | None:
    """The name a trailer uses for a model id (`claude-sonnet-5-5` gives `Claude Sonnet 5.5`); None if not a Claude id.

    The family is the one alphabetic part and the version its numeric parts joined with dots, whichever side of the
    family they sit (`claude-3-5-sonnet-20241022` and `claude-sonnet-4-5` both work); a build date and a context
    suffix (`[1m]`) are dropped. An id of any other shape has no name, so the hook passes it.
    """
    text = re.sub(r"\[[^\]]*\]|@.*$", "", model.strip().lower())
    if not text.startswith("claude-"):
        return None
    parts = [p for p in text[len("claude-") :].split("-") if p and p != "latest" and not _DATE.fullmatch(p)]
    families = [p for p in parts if p.isalpha()]
    versions = [p for p in parts if p.isdigit()]
    if len(families) != 1 or not versions or len(families) + len(versions) != len(parts):
        return None
    return f"Claude {families[0].capitalize()} {'.'.join(versions)}"


def same_model(trailer_name: str, expected: str) -> bool:
    """Whether a trailer's name is the expected one, ignoring case, spacing and a parenthetical like `(1M context)`."""

    def normal(name: str) -> str:
        return " ".join(re.sub(r"\([^)]*\)", " ", name).casefold().split())

    return normal(trailer_name) == normal(expected)


def anthropic_trailers(message: str) -> list[str]:
    """The names in a commit message's Co-Authored-By trailers that carry the vendor's address, as written."""
    return [
        match.group("name").strip()
        for match in _TRAILER.finditer(message)
        if match.group("email").strip().lower().endswith("@anthropic.com")
    ]


def committed_hashes(output: str) -> list[str]:
    """The abbreviated hashes of the commits a command's output says it made, in order."""
    return [match.group("hash") for match in _COMMIT_LINE.finditer(output)]


def latest_model(transcript: Path) -> str | None:
    """The `message.model` of the latest assistant entry of a transcript; None if there is none or it cannot be read.

    Reads from the end, since a long agent's transcript is megabytes and its latest model is in the last lines.
    Placeholder models (`<synthetic>`, written for a refused or interrupted turn) are not a running model.
    """
    try:
        size = transcript.stat().st_size
        with transcript.open("rb") as handle:
            window = _TAIL
            while True:
                start = max(0, size - window)
                handle.seek(start)
                lines = handle.read(size - start).decode("utf-8", errors="replace").splitlines()
                if start > 0:
                    lines = lines[1:]  # the first line is cut off mid-way
                for line in reversed(lines):
                    model = _assistant_model(line)
                    if model is not None:
                        return model
                if start == 0 or window >= _MAX_READ:
                    return None
                window *= 2
    except OSError:
        return None


def _assistant_model(line: str) -> str | None:
    """The model of one transcript line if it is an assistant entry that names a real one."""
    if '"model"' not in line:
        return None
    try:
        entry = json.loads(line)
    except ValueError:
        return None
    message = entry.get("message") if isinstance(entry, dict) else None
    if not isinstance(message, dict) or message.get("role") != "assistant":
        return None
    model = message.get("model")
    return model if isinstance(model, str) and model and not model.startswith("<") else None


def transcript_for(payload: dict[str, object]) -> Path | None:
    """The transcript of the agent that ran the command: its own file inside a subagent, else the main session's."""
    path = payload.get("transcript_path")
    if not isinstance(path, str) or not path:
        return None
    main = Path(path)
    agent = payload.get("agent_id")
    if agent is None or agent == "":
        return main
    if not isinstance(agent, str) or not _AGENT_ID.fullmatch(agent):
        return None
    return main.with_suffix("") / "subagents" / f"agent-{agent}.jsonl"


def candidate_dirs(command: str, cwd: str) -> list[str]:
    """Where the command's commits may be: the working directory, then every directory the command names.

    A relative name is taken from the working directory; commits are found by hash, and worktrees share their
    objects, so a close guess is enough and a wrong one only costs a failed lookup.
    """
    found = [os.path.normpath(cwd)] if cwd else []
    for match in _DIR.finditer(command):
        target = match.group("dir").strip("\"'")
        drive = _MSYS.match(target) if os.name == "nt" else None
        if drive:
            target = f"{drive.group('drive')}:/{drive.group('rest')}"
        path = os.path.normpath(Path(cwd) / target if cwd and not _DRIVE_PATH.match(target) else target)
        if path not in found:
            found.append(path)
    return found


def commit_message(commit: str, directories: list[str]) -> str | None:
    """The full message of a commit, looked up in the first directory that has it; None if none does."""
    for directory in directories:
        if not Path(directory).is_dir():
            continue
        try:
            done = subprocess.run(
                ["git", "-C", directory, "show", "-s", "--format=%B", commit],
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=5,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            continue
        if done.returncode == 0:
            return done.stdout
    return None


def recent_heads(directories: list[str], now: float) -> list[str]:
    """The abbreviated HEADs of the directories that were committed less than two minutes before `now`."""
    found: list[str] = []
    for directory in directories:
        if not Path(directory).is_dir():
            continue
        try:
            done = subprocess.run(
                ["git", "-C", directory, "log", "-1", "--format=%h %ct"],
                capture_output=True,
                text=True,
                timeout=5,
                check=False,
            )
        except (OSError, subprocess.SubprocessError):
            continue
        fields = done.stdout.split()
        if (
            done.returncode == 0
            and len(fields) == 2
            and fields[1].isdigit()
            and abs(now - int(fields[1])) <= _RECENT
            and fields[0] not in found
        ):
            found.append(fields[0])
    return found


def response_text(payload: dict[str, object]) -> str:
    """What the shell command printed, from the tool's response (an object with stdout and stderr, or a string)."""
    response = payload.get("tool_response")
    if isinstance(response, str):
        return response
    if isinstance(response, dict):
        return "\n".join(str(response.get(key) or "") for key in ("stdout", "stderr"))
    return ""


def advice(commit: str, credited: list[str], expected: str, model: str) -> str:
    """The reason shown to an agent whose commit names another model than the one it runs on."""
    return (
        f"Blocked: commit {commit} credits {', '.join(credited)} in its Co-Authored-By trailer, but you run on "
        f"{expected} ({model}). Amend it now with `git commit --amend`, leaving exactly one trailer, "
        f"`Co-Authored-By: {expected} <noreply@anthropic.com>`: name only the model you run on. The attribution "
        "line in a system reminder names the coordinator's model, which is not yours. Also make the pull request's "
        f"AI use Model line in PR.md name {expected} (and every other model in the branch's trailers).\n"
    )


def check(payload: dict[str, object]) -> str | None:
    """The reason to send back for a hook payload, or None when the commit is fine or cannot be judged."""
    tool_input = payload.get("tool_input")
    command = tool_input.get("command") if isinstance(tool_input, dict) else None
    if not isinstance(command, str) or "commit" not in command:
        return None
    output = response_text(payload)
    hashes = committed_hashes(output)
    if not hashes and output.strip():
        return None  # it printed something but no commit line: the commit failed or was not what ran
    cwd = payload.get("cwd")
    directories = candidate_dirs(command, cwd if isinstance(cwd, str) else "")
    if not hashes:
        hashes = recent_heads(directories, time.time())
    if not hashes:
        return None
    transcript = transcript_for(payload)
    model = latest_model(transcript) if transcript else None
    expected = display_name(model) if model else None
    if model is None or expected is None:
        return None
    for commit in hashes:
        message = commit_message(commit, directories)
        wrong = [name for name in anthropic_trailers(message or "") if not same_model(name, expected)]
        if wrong:
            return advice(commit, wrong, expected, model)
    return None


def main() -> int:
    """Read the hook's JSON from stdin; 2 (with the reason on stderr) on a wrong trailer, else 0."""
    try:
        payload = json.loads(sys.stdin.buffer.read().decode("utf-8", errors="replace") or "{}")
        reason = check(payload) if isinstance(payload, dict) else None
    except Exception:  # a hook must never break a command: anything unexpected passes
        return 0
    if reason:
        sys.stderr.write(reason)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
