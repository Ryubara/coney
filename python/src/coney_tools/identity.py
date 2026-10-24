# SPDX-License-Identifier: GPL-3.0-or-later
"""Who ran a command, as the Claude Code hooks recorded it (docs/guides/agents.md#identity).

An agent's word about who it is cannot be trusted, so a tool never reads the id, type or model from the environment.
Before a `coney-tools` or `git commit` command runs, the hook dispatcher (`.claude/hooks/dispatch.py`) writes the
caller's identity, from the hook input and the agent's own transcript, to `~/.coney/identity/<hash>.json`, where the
hash is the SHA-256 of a random token, and puts only the token in front of the command as `CONEY_AGENT_TOKEN`. This
module hashes the token and reads that file; a listing of the folder shows hashes, which give no token away. A token
that is typed, changed or expired finds no file, so the answer is "unverified": never another role. The file is read,
not consumed, because one command can run several tools (`coney-tools ... && git commit`); the hook deletes it when
the command ends (not for a background command, whose tool may not have started yet), and a call that issues a token
removes the expired ones. A tool that starts after the file's life (10 minutes) finds none and is unverified.

Creating a file means writing into a folder the hook's rules forbid every session to name, so a forged identity is
visible and deliberate; it is not impossible, since the agent and the tool are the same user. What stays exposed: the
token itself, in the command line of the shell that runs the command, which a process listing (`ps`, Task Manager)
shows for as long as the command runs.

`current()` reads once, as the first thing a tool does, because uv's start-up has already used some of the file's
life and a build can run for half an hour before it needs the answer. The folder is never taken from the environment.
"""

from __future__ import annotations

import hashlib
import json
import os
import re
import time
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

#: The variable the hook puts in front of the command.
TOKEN_VARIABLE = "CONEY_AGENT_TOKEN"
_TOKEN = re.compile(r"[0-9a-f]{32}")
#: The file format the hook writes; a file of another version is not read.
VERSION = 1
UNKNOWN = "unknown"
#: The most a file's issue time may be ahead of this clock (seconds) and still count.
_SKEW = 60


@dataclass(frozen=True)
class Identity:
    """The agent behind a command, and whether the hook vouched for it."""

    id: str = UNKNOWN
    type: str = UNKNOWN
    model: str = UNKNOWN
    #: True only when a valid, unexpired identity file named by the token was found.
    verified: bool = False
    #: The Claude Code session the caller runs in (empty when the hook did not record one). Every main session has the
    #: id `main`, so only the session tells two of them apart; a subagent's id is unique on its own.
    session: str = ""

    def agent(self) -> dict[str, str]:
        """The fields as a stamp's `who.agent` holds them: id, type, model, and the session when it is known."""
        found = {"id": self.id, "type": self.type, "model": self.model}
        if self.session:
            found["session"] = self.session
        return found


def default_folder() -> Path:
    """Where the hook writes identity files: `~/.coney/identity`. Never read from the environment."""
    return Path.home() / ".coney" / "identity"


def file_name(token: str) -> str:
    """The name of the identity file of a token: the SHA-256 of the token, so the folder does not list tokens."""
    return hashlib.sha256(token.encode("ascii")).hexdigest() + ".json"


def _text(value: object) -> str | None:
    """A non-empty string field, else None."""
    return value if isinstance(value, str) and value else None


def load(env: Mapping[str, str] | None = None, folder: Path | None = None, now: float | None = None) -> Identity:
    """The identity the token in `env` names, or the unverified one when there is none, it is not valid or expired."""
    env = os.environ if env is None else env
    token = env.get(TOKEN_VARIABLE, "")
    if not _TOKEN.fullmatch(token):
        return Identity()
    path = (default_folder() if folder is None else folder) / file_name(token)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return Identity()
    moment = time.time() if now is None else now
    if not isinstance(data, dict) or data.get("version") != VERSION:
        return Identity()
    issued, expires = data.get("issued"), data.get("expires")
    if not isinstance(issued, int | float) or not isinstance(expires, int | float):
        return Identity()
    if expires <= moment or issued > moment + _SKEW:
        return Identity()
    fields = [_text(data.get(name)) for name in ("id", "type", "model")]
    if None in fields:
        return Identity()
    agent_id, agent_type, model = fields
    assert agent_id is not None and agent_type is not None and model is not None
    if agent_id == MAIN and not _text(data.get("session")):
        # Every main session is `main`: without a session it names no one, so it is not vouched for.
        return Identity()
    return Identity(agent_id, agent_type, model, verified=True, session=_text(data.get("session")) or "")


#: The id every main session has: it names no one, so it never identifies a person on its own.
MAIN = "main"


def same_agent(first: Mapping[str, object], second: Mapping[str, object]) -> bool:
    """Whether two `agent` records (an identity's `agent()` or a stamp's `who.agent`) are the same caller.

    A subagent's id is unique, so equal ids are the same agent. The id `main` is shared by every main session, so two
    of them are the same only when both name the same session; a record with no session (or no id) is never taken for
    anyone, so an unknown caller is never refused as a namesake.
    """
    first_id, second_id = first.get("id"), second.get("id")
    if not isinstance(first_id, str) or first_id != second_id or first_id in ("", UNKNOWN):
        return False
    if first_id != MAIN:
        return True
    first_session, second_session = first.get("session"), second.get("session")
    return isinstance(first_session, str) and bool(first_session) and first_session == second_session


_current: Identity | None = None


def current() -> Identity:
    """The identity of this process, read once from the environment and the folder at the first call."""
    global _current
    if _current is None:
        _current = load()
    return _current


def reset() -> None:
    """Forget the cached identity (tests)."""
    global _current
    _current = None
