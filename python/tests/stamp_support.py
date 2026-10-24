# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the stamp tests: the rules of this checkout."""

from conventions_support import REPO

from coney_tools.conventions import Stamps, load_conventions


def rules() -> Stamps:
    """The stamp rules of this checkout's `.github/conventions.yml`."""
    return load_conventions(REPO).stamps
