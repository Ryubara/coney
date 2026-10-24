# SPDX-License-Identifier: GPL-3.0-or-later
"""Which checks a change needs before it is opened as a pull request, from the paths it touches.

The rules are `proof` in `.github/conventions.yml`, read from there: a change needs the first profile (they are listed
smallest first) whose `classes` hold the CI scope class of every changed path (`Paths.ci_class`: `docs_only`,
`tools_only`, `engine`, else `unknown`), so one `.cpp` file in a documentation change makes it `full`. A profile lists
the checks `coney-tools task check` runs; `only_when` drops a check that does not apply to the paths at hand. `task
check` writes the profile and the checks it ran into the `checked` stamp, and `pr verify` recomputes both from the
paths with these functions, so the two always agree (docs/guides/stamps.md#api). Standard library only.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence

from coney_tools.conventions import Conventions, ConventionsError, ProofProfile


def _profile(conventions: Conventions, name: str) -> ProofProfile:
    """The profile called `name`; ConventionsError when there is none."""
    for profile in conventions.proof.profiles:
        if profile.name == name:
            return profile
    known = ", ".join(profile.name for profile in conventions.proof.profiles)
    raise ConventionsError(f"proof.profiles: no profile {name!r} (the profiles are {known})")


def required_profile(conventions: Conventions, paths: Iterable[str]) -> str:
    """The name of the smallest profile that covers every path (repository-relative, forward slashes); the smallest
    profile for no paths at all."""
    classes = {conventions.paths.ci_class(path) for path in paths}
    for profile in conventions.proof.profiles:
        if classes <= set(profile.classes):
            return profile.name
    return conventions.proof.profiles[-1].name  # the last profile holds every class (the file is refused otherwise)


def at_least(name: str, needed: str, conventions: Conventions) -> bool:
    """Whether the profile `name` is the profile `needed` or a bigger one."""
    order = [profile.name for profile in conventions.proof.profiles]
    _profile(conventions, name)
    _profile(conventions, needed)
    return order.index(name) >= order.index(needed)


def required_checks(conventions: Conventions, profile: str, paths: Sequence[str]) -> list[str]:
    """The checks of `profile`, in the order the file lists them, without those `only_when` says do not apply to
    `paths`."""
    only_when = conventions.proof.only_when
    return [
        check
        for check in _profile(conventions, profile).checks
        if check not in only_when or any(path.startswith(only_when[check]) for path in paths)
    ]
