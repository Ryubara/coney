# SPDX-License-Identifier: GPL-3.0-or-later
"""The `disc` label of the engine's tests: `ctest -L disc` must reach every test that needs the player's disc, and the
tests that run without it (`-LE disc`) must not secretly need it.

A Catch2 test case is a ctest test, and its tags become ctest labels (`catch_discover_tests(... ADD_TAGS_AS_LABELS)` in
tests/CMakeLists.txt). A test case that skips itself without `CONEY_DISC` therefore has to carry the tag `[disc]`;
the checks registered by hand in src/platform/CMakeLists.txt when `CONEY_DISC` is set at configure time carry the label
themselves. Nothing here needs a compiler or a disc: it reads the sources."""

import re
from itertools import pairwise
from pathlib import Path

import pytest

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

REPO = Path(__file__).resolve().parents[2]
#: The start of a test case; its header (name and tags) runs to the opening brace of its body.
_CASE = re.compile(r"^(?:TEST_CASE|TEST_CASE_METHOD|SCENARIO|TEMPLATE_TEST_CASE)\s*\(", re.MULTILINE)


def _cases(text: str) -> list[tuple[str, str]]:
    """(header, body) of each test case in a source file: the body runs to the next test case or the file's end."""
    starts = [m.start() for m in _CASE.finditer(text)] + [len(text)]
    cases = []
    for begin, end in pairwise(starts):
        block = text[begin:end]
        brace = block.find("{")
        cases.append((block[:brace], block[brace:]))
    return cases


def test_every_test_case_that_needs_the_disc_is_tagged_disc() -> None:
    missing = []
    for path in sorted((REPO / "tests").rglob("*.cpp")):
        for header, body in _cases(path.read_text(encoding="utf-8")):
            if "CONEY_DISC" in body and "[disc]" not in header:
                missing.append(f"{path.relative_to(REPO).as_posix()}: {header.strip()[:90]}")
    assert missing == [], "test cases that skip without CONEY_DISC need the tag [disc]:\n" + "\n".join(missing)


#: A registration of a test case from code (tests/support/each_case.h): `registerEach(items, "tags", ...)`, the items
#: one identifier (a constant list), or `registerCase(name, "tags", ...)`, the name a string literal or one identifier.
#: Both are matched with optional template arguments and a space before the parenthesis, as `registerEach<T>(` and
#: `registerEach (` compile; the tags are the second argument either way.
_TEMPLATE_ARGS = r"(?:<[^;()]*>)?"
_EACH = re.compile(rf'\bregisterEach\b\s*{_TEMPLATE_ARGS}\s*\(\s*[\w:]+\s*,\s*"([^"]*)"')
_CASE_CALL = re.compile(rf'\bregisterCase\b\s*{_TEMPLATE_ARGS}\s*\(\s*(?:"(?:[^"\\]|\\.)*"|[\w:.]+)\s*,\s*"([^"]*)"')
#: Any call of the two, readable or not: the file check compares this count with the tags it could read.
_REGISTRATION_CALL = re.compile(rf"\bregister(?:Each|Case)\b\s*{_TEMPLATE_ARGS}\s*\(")


def _each_tags(text: str) -> list[str]:
    """The tags of each `registerEach` and `registerCase` call in a source file, for the calls whose shape is read."""
    return _EACH.findall(text) + _CASE_CALL.findall(text)


def _disc_problems(name: str, text: str) -> list[str]:
    """What is wrong with the registrations of one source file: nothing for a file that does not name `CONEY_DISC`."""
    if "CONEY_DISC" not in text:
        return []
    problems = []
    # Fail closed: a call whose tags the patterns cannot read (an inline list as the items) is not skipped.
    if len(_REGISTRATION_CALL.findall(text)) != len(_each_tags(text)):
        problems.append(f"{name}: a registration call of a shape the check cannot read")
    problems += [f"{name}: {tags}" for tags in _each_tags(text) if "[disc]" not in tags]
    return problems


def test_the_tags_of_a_registration_are_found() -> None:
    text = 'bool k = test::registerEach(\n    kSeeds, "[disc][story]",\n    [](int) { return "n"; }, body);'
    assert _each_tags(text) == ["[disc][story]"]
    assert _each_tags('registerEach(a, "[x]", n, b); registerEach(c, "[disc]", n, b);') == ["[x]", "[disc]"]
    # An inline list as the items is not read: the file check counts the calls and fails on the difference.
    assert _each_tags('registerEach(std::array{1, 2}, "[x]", n, b);') == []


def test_every_form_of_a_registration_call_is_found() -> None:
    # Template arguments, a space before the parenthesis and a direct registerCase all compile, and all register a
    # case that a missing [disc] would let skip under -LE disc.
    assert _each_tags('registerEach<std::uint32_t>(kSeeds, "[a]", n, b);') == ["[a]"]
    assert _each_tags('registerEach<std::array<int, 2>> (kSeeds, "[a]", n, b);') == ["[a]"]
    assert _each_tags('registerEach (kSeeds, "[a]", n, b);') == ["[a]"]
    assert _each_tags('coney::test::registerEach\n(kSeeds, "[a]", n, b);') == ["[a]"]
    assert _each_tags('coney::test::registerCase("one case", "[b]", body);') == ["[b]"]
    assert _each_tags('coney::test::registerCase (kName, "[b]", body);') == ["[b]"]
    assert _each_tags('coney::test::registerCase<void>("a, b", "[b]", body);') == ["[b]"]


def test_a_registration_without_disc_is_reported_in_every_form() -> None:
    for call in (
        'coney::test::registerEach(kSeeds, "[story]", n, b);',
        'coney::test::registerEach<std::uint32_t>(kSeeds, "[story]", n, b);',
        'coney::test::registerEach (kSeeds, "[story]", n, b);',
        'coney::test::registerCase("one", "[story]", body);',
    ):
        text = f"// skips without CONEY_DISC\n{call}\n"
        assert _disc_problems("x.cpp", text) == ["x.cpp: [story]"], call
        assert _disc_problems("x.cpp", text.replace("[story]", "[disc][story]")) == [], call


def test_a_file_with_an_unreadable_call_fails_the_count_comparison() -> None:
    unreadable = 'coney::test::registerEach(std::array{1, 2}, "[disc]", n, b);'
    readable = 'coney::test::registerEach(kSeeds, "[disc]", n, b);'
    text = f"// skips without CONEY_DISC\n{readable}\n{unreadable}\n"
    assert _disc_problems("x.cpp", text) == ["x.cpp: a registration call of a shape the check cannot read"]
    # The same holds for a direct call whose name is an expression the pattern does not read.
    direct = 'CONEY_DISC\nregisterCase(std::format("n{}", 1), "[disc]", body);\n'
    assert _disc_problems("x.cpp", direct) == ["x.cpp: a registration call of a shape the check cannot read"]
    # A file that does not name CONEY_DISC is not this check's.
    assert _disc_problems("x.cpp", f"{unreadable}\n") == []


def test_every_registered_case_that_needs_the_disc_is_tagged_disc() -> None:
    # A case registered from code is no TEST_CASE, so the check above does not see it: a file that names CONEY_DISC
    # (its skip message) tags every case it registers [disc].
    missing = []
    for path in sorted((REPO / "tests").rglob("*.cpp")):
        missing += _disc_problems(path.relative_to(REPO).as_posix(), path.read_text(encoding="utf-8"))
    assert missing == [], "registered cases that skip without CONEY_DISC need the tag [disc]:\n" + "\n".join(missing)


def test_the_tags_become_ctest_labels() -> None:
    text = (REPO / "tests" / "CMakeLists.txt").read_text(encoding="utf-8")
    assert re.search(r"catch_discover_tests\(coney_tests[^)]*\bADD_TAGS_AS_LABELS\b", text)


def test_the_disc_checks_registered_by_hand_carry_the_label() -> None:
    text = (REPO / "src" / "platform" / "CMakeLists.txt").read_text(encoding="utf-8")
    block = text[
        text.index("if(DEFINED ENV{CONEY_DISC}") : text.index("endif()", text.index("if(DEFINED ENV{CONEY_DISC}"))
    ]
    registered = {name for name in re.findall(r"add_test\(NAME (coney\.[a-z0-9_]+)", block) if not name.endswith("_")}
    registered |= {
        f"coney.exits_cleanly_{level}" for level in re.findall(r"foreach\(level ([^)]*)\)", block)[0].split()
    }
    labelled = re.search(r"set_tests_properties\(([^)]*)PROPERTIES LABELS disc\)", block)
    assert labelled is not None
    assert registered == set(labelled.group(1).split()), "every add_test inside the CONEY_DISC block needs the label"
