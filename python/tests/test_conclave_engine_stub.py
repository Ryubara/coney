# SPDX-License-Identifier: GPL-3.0-or-later
"""The stub of the engine's types (`conclave_engine_stub.d.ts`) gives the boundary check the same verdicts as the
generated types, and cannot drift from them unseen.

CI has no Claude Code, so no generated types: there the boundary check reads the stub (conclave_boundary.engine_types).
Where the generated types are present (a checkout whose mod a session has loaded), these tests hold the stub against
them: the names the mod imports, the nouns and methods it calls, and the verdict on every case of the boundary tests.
Where they are absent the comparisons skip, and the stub's own tests (test_conclave_mod.py runs on it) still run."""

import re
from typing import Any

import pytest
from conclave_boundary import (
    API_CALLS,
    API_PROPERTIES,
    ENGINE_STUB,
    ENGINE_TYPES,
    cannot_run,
    describe_types,
    mod_sources,
    run_check,
    unavailable,
)
from test_conclave_mod import ALLOWED, CROSSINGS, all_snippets

IMPORT = re.compile(r"import\s+(?:type\s+)?\{([^}]*)\}\s+from\s+'claude-code'")
#: What the check itself relies on, besides what the mod imports.
CHECKED = ["EngineInterface", "On"]


def imported_names() -> list[str]:
    """Every name the mod's sources import from `claude-code`, and the ones the check relies on."""
    names = set(CHECKED)
    for path in mod_sources():
        for group in IMPORT.findall(path.read_text(encoding="utf-8")):
            names |= {part.strip().removeprefix("type ").strip() for part in group.split(",") if part.strip()}
    return sorted(names)


def need(*, engine: bool) -> None:
    """Ends the test unless the stub (and, with `engine`, the generated types) and Node and TypeScript are present.

    The comparison with the generated types skips when they are absent, as they always are in CI; a missing stub,
    Node or TypeScript is a failure under CI, like the check itself."""
    if engine and not ENGINE_TYPES.is_file():
        pytest.skip("needs the engine's generated types, which only a session that loads the mod writes")
    if not ENGINE_STUB.is_file():
        cannot_run("the stub is not in the repository yet (python/tests/conclave_engine_stub.d.ts)")
    reason = unavailable()
    if reason is not None:
        cannot_run(reason)


def test_the_stub_names_what_the_mod_imports() -> None:
    need(engine=False)
    kinds = describe_types(imported_names(), "stub")["kinds"]
    assert {name: kind for name, kind in kinds.items() if kind is None} == {}


def test_the_names_the_mod_imports_are_found_in_its_sources() -> None:
    # The list is read from the sources; were the pattern to stop matching, the comparison would compare nothing.
    assert {"EngineInterface", "Register", "atom", "read", "update", "RenderElement"} <= set(imported_names())


def test_the_stub_agrees_with_the_engines_types_on_names_and_members() -> None:
    need(engine=True)
    names = imported_names()
    real = describe_types(names, "engine")
    stub: dict[str, Any] = describe_types(names, "stub")
    assert stub["kinds"] == real["kinds"]
    for noun, methods in stub["nouns"].items():
        assert noun in real["nouns"], f"the stub has a noun the engine lacks: {noun}"
        assert set(methods) <= set(real["nouns"][noun]), f"{noun}: {methods} against {real['nouns'][noun]}"
    # Every call the mod may make exists in both: the check's list, the stub and the engine say the same.
    for call in sorted(API_CALLS | API_PROPERTIES):
        noun, member = call.split(".")
        assert member in stub["nouns"][noun], f"the stub lacks $.{call}"
        assert member in real["nouns"][noun], f"the engine lacks $.{call}"


def test_the_stub_gives_the_verdicts_of_the_engines_types() -> None:
    need(engine=True)
    snippets = all_snippets()

    def read(types: str) -> dict[str, dict[str, Any]]:
        verdict = run_check(snippets, mod_sources(), types)
        # The stub reports no type errors (it is not the engine), so they are left out of both.
        return {
            where: {
                name: {"errors": [e for e in entry["errors"] if ": type error TS" not in e], "events": entry["events"]}
                for name, entry in found.items()
            }
            for where, found in verdict.items()
            if where in {"files", "snippets"}
        }

    assert read("stub") == read("engine")


def test_the_stub_finds_every_crossing() -> None:
    need(engine=False)
    verdict = run_check(all_snippets(), mod_sources(), "stub")
    for snippet, word in CROSSINGS:
        found = verdict["snippets"][snippet]["errors"]
        assert any(word in line for line in found), f"{word!r} not in {found} for {snippet}"
    for name in ALLOWED:
        assert verdict["snippets"][f"allowed: {name}"]["errors"] == []
    for path in mod_sources():
        assert verdict["files"][str(path)]["errors"] == []
