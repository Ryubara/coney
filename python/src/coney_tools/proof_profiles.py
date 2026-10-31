# SPDX-License-Identifier: GPL-3.0-or-later
"""Which checks a change needs before it is opened as a pull request, from the paths it touches.

The rules are `proof` in `.github/conventions.yml`, read from there: a change needs the first profile (they are listed
smallest first) whose `classes` hold the CI scope class of every changed path (`Paths.ci_class`: `docs_only`,
`tools_only`, `engine`, else `unknown`), so one `.cpp` file in a documentation change makes it `full`. A profile lists
the checks `coney-tools task check` runs; `only_when` drops a check that does not apply to the paths at hand. `task
check` writes the profile and the checks it ran into the `checked` stamp, and `pr verify` recomputes both from the
paths with these functions, so the two always agree (docs/guides/stamps.md#api). `pytest_scope` says which Python
tests the `pytest` check runs (`proof.pytest`), which the stamp records and `pr verify` holds it to. Standard library
only.
"""

from __future__ import annotations

from collections.abc import Iterable, Sequence
from dataclasses import dataclass, field

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


# --- which Python tests the pytest check runs (proof.pytest) ---------------------------------------------------------

#: How a stamp records the whole suite (`scope_record`).
WHOLE = "whole"


@dataclass(frozen=True)
class PytestScope:
    """Which Python tests a change needs: the whole suite, or only the tests marked `@pytest.mark.reads(<class>)` for
    one of `classes`. `culprit` is the first changed path that made it the whole suite, if one did."""

    whole: bool
    classes: tuple[str, ...] = ()
    culprit: str = field(default="", compare=False)  # not part of the scope: only why it is whole

    @property
    def text(self) -> str:
        """The scope in words, for `task check`'s output."""
        if not self.whole:
            return "only the tests marked as reading " + ", ".join(self.classes)
        return f"the whole suite ({self.culprit} runs everything)" if self.culprit else "the whole suite"


def pytest_scope(conventions: Conventions, paths: Iterable[str]) -> PytestScope:
    """The Python tests the change to `paths` needs (`proof.pytest`): the whole suite when any path is under a
    `whole_when` prefix or of a class that is not `scoped` (the tools, an unmapped file), or when nothing changed;
    else the tests that read the scoped classes the paths are of, in the order the conventions list them."""
    rules = conventions.proof.pytest
    found: set[str] = set()
    for path in paths:
        kind = conventions.paths.ci_class(path)
        if path.startswith(rules.whole_when) or kind not in rules.scoped:
            return PytestScope(True, culprit=path)
        found.add(kind)
    if not found:
        return PytestScope(True)
    return PytestScope(False, tuple(name for name in rules.scoped if name in found))


def covers(ran: PytestScope, needed: PytestScope) -> bool:
    """Whether a run of the scope `ran` holds every test the scope `needed` asks for."""
    return ran.whole or (not needed.whole and set(needed.classes) <= set(ran.classes))


def scope_record(scope: PytestScope) -> str | list[str]:
    """The scope as the `checked` stamp records it (`detail.pytest_scope`): `whole`, or the list of classes."""
    return WHOLE if scope.whole else list(scope.classes)


def scope_from_record(value: object) -> PytestScope:
    """The scope a stamp recorded; a stamp without one (made before the scope was recorded) ran the whole suite.
    ConventionsError for anything else."""
    if value is None or value == WHOLE:
        return PytestScope(True)
    if isinstance(value, list) and value and all(isinstance(item, str) for item in value):
        return PytestScope(False, tuple(value))
    raise ConventionsError(f"checked stamp: pytest_scope is {value!r}, not {WHOLE!r} or a list of classes")
