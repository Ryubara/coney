# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the tests of the Claude Code hooks: the dispatcher as a module and which guards it runs for a call."""

import importlib.util
import json
import sys
from types import ModuleType
from typing import Any

from conventions_support import REPO

HOOKS = REPO / ".claude" / "hooks"


def load_hook_module(name: str) -> ModuleType:
    """A module of the hooks folder (`hooklib`, `roles`, `identity`) by its import name, as the hooks import it."""
    if str(HOOKS) not in sys.path:
        sys.path.insert(0, str(HOOKS))
    return importlib.import_module(name)


def load_dispatch() -> ModuleType:
    """`.claude/hooks/dispatch.py` as a module (it puts its folder on the import path for its sibling modules)."""
    load_hook_module("hooklib")
    spec = importlib.util.spec_from_file_location("coney_dispatch", HOOKS / "dispatch.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


DISPATCH = load_dispatch()


def settings() -> dict[str, Any]:
    """The committed `.claude/settings.json`."""
    data: dict[str, Any] = json.loads((REPO / ".claude" / "settings.json").read_text(encoding="utf-8"))
    return data


def guards_run(tool: str, command: str | None = None, event: str = "PreToolUse") -> list[str]:
    """The names of the guard scripts the dispatcher runs for a call of `tool` (with a shell `command`)."""
    payload: dict[str, Any] = {"tool_name": tool, "tool_input": {} if command is None else {"command": command}}
    table = DISPATCH.PRE_GUARDS if event == "PreToolUse" else DISPATCH.POST_GUARDS
    return [name for name, applies in table if applies(payload)]
