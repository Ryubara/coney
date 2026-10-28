# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared pytest fixtures: every test starts in the same environment, whatever shell runs the suite."""

from __future__ import annotations

from collections.abc import Iterator

import pytest
from ambient_env import CLEARED

from coney_tools import identity


@pytest.fixture(autouse=True)
def clean_environment(monkeypatch: pytest.MonkeyPatch) -> Iterator[None]:
    """Remove the variables the tools read (`ambient_env.CLEARED`), so a test sees the same thing in a task's shell,
    an agent tool, CI and a clean terminal. A test that needs one sets it with `monkeypatch.setenv`, which this
    fixture's `monkeypatch` is shared with, so the value is put back afterwards either way. The cached identity is
    forgotten on both sides: it is read from `CONEY_AGENT_TOKEN` once per process."""
    for name in CLEARED:
        monkeypatch.delenv(name, raising=False)
    identity.reset()
    yield
    identity.reset()
