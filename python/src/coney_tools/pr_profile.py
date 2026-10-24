# SPDX-License-Identifier: GPL-3.0-or-later
"""The Check profile a change needs: the smallest of `proof.profiles` that takes the CI scope class of every path.

The classes are `paths.docs_only`, `paths.tools_only` and `paths.engine` of `.github/conventions.yml`, the ones CI's
scope script sorts a pull request by, and `unknown` for a path in none of them (it runs everything). A change that
touches a documentation file and an engine file needs the engine's profile, the biggest.
"""

from __future__ import annotations

from collections.abc import Sequence

from coney_tools.conventions import Conventions, Paths, Profile


def path_classes(paths: Sequence[str], rules: Paths) -> dict[str, list[str]]:
    """The paths sorted into their CI scope classes (`docs_only`, `tools_only`, `engine`, `unknown`), in the order the
    paths came; a class with no path is left out."""
    found: dict[str, list[str]] = {}
    for path in paths:
        found.setdefault(rules.ci_class(path), []).append(path)
    return found


def profile_of(paths: Sequence[str], conventions: Conventions) -> Profile:
    """The Check profile a change needs: the smallest of `proof.profiles` that takes the class of every path."""
    return conventions.proof.profile_for(path_classes(paths, conventions.paths))
