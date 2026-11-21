# SPDX-License-Identifier: GPL-3.0-or-later
"""A C++ test that saves a capture gives it a path of its own, never a fixed name in the shared temp folder
(docs/guides/testing.md#capture-paths).

`tests/platform/imgui_overlay_test.cpp` failed once in a full check with "the frame was not captured" while other
runs were using the machine (#1588). Nothing was wrong with the GPU: every run of the test wrote
`<temp>/coney-overlay-capture.png`, the engine removes a file before it writes one, and another process's run did the
same to it between the write and the check that the file was there, so the capture "could not be written". Five runs
at once failed 3 times in 100. The tests that read the PNG back (`gpu_renderer_test.cpp`, `gpu_scene_test.cpp`) could
read another run's picture instead. A `coney::test::TempDir` is unique to its process, so a capture goes in one.
Nothing here needs a compiler: it reads the sources."""

import re
from pathlib import Path

import pytest

# Reads the checkout's own files of these classes (python/tests/repo_reads.py).
pytestmark = pytest.mark.reads("engine")

REPO = Path(__file__).resolve().parents[2]
#: A path built in the shared temp folder and named with a string literal: `temp_directory_path() / "name.png"`.
_FIXED_PICTURE = re.compile(r'temp_directory_path\(\)\s*/\s*"[^"]*\.png"')


def _fixed_picture_paths(text: str) -> list[str]:
    """Each fixed-name picture path in the C++ source `text`."""
    return _FIXED_PICTURE.findall(text)


def test_a_fixed_picture_name_in_the_temp_folder_is_found() -> None:
    fixed = 'const std::string path = (std::filesystem::temp_directory_path() / "coney-x-capture.png").string();'
    assert _fixed_picture_paths(fixed) == ['temp_directory_path() / "coney-x-capture.png"']
    # A path in a folder the test made (a TempDir) is the fix, and a non-picture name is not this rule's.
    assert _fixed_picture_paths('(folder.path() / "capture.png").string()') == []
    assert _fixed_picture_paths('temp_directory_path() / "coney-tunables-test.ini"') == []


def test_no_cpp_test_saves_a_picture_to_a_fixed_temp_name() -> None:
    found = {
        str(path.relative_to(REPO)): hits
        for path in sorted((REPO / "tests").rglob("*.cpp"))
        if (hits := _fixed_picture_paths(path.read_text(encoding="utf-8")))
    }
    assert found == {}, "give each capture a path inside a coney::test::TempDir (tests/support/fixtures.h)"
