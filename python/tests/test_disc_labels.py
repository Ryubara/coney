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


#: A registration of one test case per item (tests/support/each_case.h): `registerEach(items, "tags", ...)`, the tags
#: being the second argument and the items one identifier (a constant list).
_EACH = re.compile(r'registerEach\(\s*[\w:]+\s*,\s*"([^"]*)"')


def _each_tags(text: str) -> list[str]:
    """The tags of each `registerEach` call in a source file."""
    return _EACH.findall(text)


def test_the_tags_of_a_registration_are_found() -> None:
    text = 'bool k = test::registerEach(\n    kSeeds, "[disc][story]",\n    [](int) { return "n"; }, body);'
    assert _each_tags(text) == ["[disc][story]"]
    assert _each_tags('registerEach(a, "[x]", n, b); registerEach(c, "[disc]", n, b);') == ["[x]", "[disc]"]
    # An inline list as the items is not read: the file check counts the calls and fails on the difference.
    assert _each_tags('registerEach(std::array{1, 2}, "[x]", n, b);') == []


def test_every_registered_case_that_needs_the_disc_is_tagged_disc() -> None:
    # A case registered by registerEach is no TEST_CASE, so the check above does not see it: a file that names
    # CONEY_DISC (its skip message) tags every case it registers [disc].
    missing = []
    for path in sorted((REPO / "tests").rglob("*.cpp")):
        text = path.read_text(encoding="utf-8")
        if "CONEY_DISC" in text:
            # Fail closed: a call whose tags the pattern cannot read (an inline list as the items) is not skipped.
            if text.count("registerEach(") != len(_each_tags(text)):
                missing.append(
                    f"{path.relative_to(REPO).as_posix()}: a registerEach call of a shape the check cannot read"
                )
            missing += [
                f"{path.relative_to(REPO).as_posix()}: {tags}" for tags in _each_tags(text) if "[disc]" not in tags
            ]
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
