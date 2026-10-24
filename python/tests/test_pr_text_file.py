# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for reading the engineer's description file (PR.md): the title, the summary paragraph and the pictures, and
the hashes a review binds to. Invented text only."""

from pathlib import Path

import pytest

from coney_tools import pr_text_file
from coney_tools.config import ConfigError

TEXT = """tools: Add a thing

## Summary

Adds the thing so the tools can do the other thing.

Fixes #12

## Testing

Runtime: none (tooling only)

## Screenshots

![before](./shots/before.png)
![after](./shots/after.png)
"""


def write(folder: Path, text: str = TEXT, shots: tuple[str, ...] = ("before.png", "after.png")) -> Path:
    """A description file with pictures beside it (each picture's name is also its content)."""
    (folder / "shots").mkdir(parents=True, exist_ok=True)
    for name in shots:
        (folder / "shots" / name).write_bytes(name.encode())
    path = folder / "PR.md"
    path.write_text(text, encoding="utf-8")
    return path


def test_the_title_the_paragraph_and_the_pictures_are_read(tmp_path: Path) -> None:
    text = pr_text_file.read(write(tmp_path))
    assert text.title == "tools: Add a thing"
    assert text.paragraph == "Adds the thing so the tools can do the other thing."
    assert [image.path for image in text.images] == ["./shots/before.png", "./shots/after.png"]
    assert all(image.sha256 for image in text.images) and text.missing == ()


def test_fixes_and_docs_lines_are_not_the_paragraph(tmp_path: Path) -> None:
    body = TEXT.replace("Fixes #12", "Docs: none\nFixes #12\nRefs #3")
    assert pr_text_file.read(write(tmp_path, body)).paragraph == "Adds the thing so the tools can do the other thing."


def test_a_missing_picture_is_listed_not_hashed(tmp_path: Path) -> None:
    text = pr_text_file.read(write(tmp_path, shots=("before.png",)))
    assert text.missing == ("./shots/after.png",)
    assert [image.path for image in text.images] == ["./shots/before.png"]


def test_an_uploaded_picture_is_not_a_local_one(tmp_path: Path) -> None:
    body = TEXT.replace("./shots/after.png", "https://example.org/user-attachments/assets/x")
    text = pr_text_file.read(write(tmp_path, body, shots=("before.png",)))
    assert [image.path for image in text.images] == ["./shots/before.png"] and text.missing == ()


def test_line_endings_and_trailing_spaces_do_not_change_the_hashes(tmp_path: Path) -> None:
    plain = pr_text_file.read(write(tmp_path / "a"))
    crlf = pr_text_file.read(write(tmp_path / "b", TEXT.replace("\n", "  \r\n")))
    assert (plain.title_sha256, plain.paragraph_sha256) == (crlf.title_sha256, crlf.paragraph_sha256)


def test_an_edited_paragraph_or_a_replaced_picture_changes_its_hash(tmp_path: Path) -> None:
    first = pr_text_file.read(write(tmp_path / "a"))
    second = pr_text_file.read(write(tmp_path / "b", TEXT.replace("Adds the thing", "Adds a thing")))
    assert second.paragraph_sha256 != first.paragraph_sha256 and second.images_sha256 == first.images_sha256
    folder = tmp_path / "c"
    third_path = write(folder)
    (folder / "shots" / "after.png").write_bytes(b"another")
    third = pr_text_file.read(third_path)
    assert third.images_sha256 != first.images_sha256 and third.paragraph_sha256 == first.paragraph_sha256


def test_swapping_the_contents_of_two_pictures_changes_the_hash(tmp_path: Path) -> None:
    path = write(tmp_path)
    first = pr_text_file.read(path).images_sha256
    before, after = tmp_path / "shots" / "before.png", tmp_path / "shots" / "after.png"
    before_bytes, after_bytes = before.read_bytes(), after.read_bytes()
    before.write_bytes(after_bytes)
    after.write_bytes(before_bytes)
    assert pr_text_file.read(path).images_sha256 != first


def test_the_pictures_hash_does_not_depend_on_their_order(tmp_path: Path) -> None:
    swapped = TEXT.replace(
        "![before](./shots/before.png)\n![after](./shots/after.png)",
        "![after](./shots/after.png)\n![before](./shots/before.png)",
    )
    assert (
        pr_text_file.read(write(tmp_path / "a")).images_sha256
        == pr_text_file.read(write(tmp_path / "b", swapped)).images_sha256
    )


def test_no_pictures_hash_the_empty_list(tmp_path: Path) -> None:
    text = pr_text_file.read(
        write(tmp_path, TEXT.split("## Screenshots")[0] + "## Screenshots\n\nScreenshots: none (tool)\n")
    )
    assert text.images == () and len(text.images_sha256) == 64


@pytest.mark.parametrize(
    ("body", "problem"),
    [
        ("", "empty"),
        ("tools: Add a thing\n\n## Testing\n\nRuntime: none (x)\n", "no '## Summary' section"),
        ("tools: Add a thing\n\n## Summary\n\n\n## Testing\n", "the Summary has no text"),
        ("\n\n## Summary\n\ntext\n", "line 1 is empty"),
    ],
)
def test_a_file_without_a_title_or_a_summary_is_refused(tmp_path: Path, body: str, problem: str) -> None:
    with pytest.raises(ConfigError, match=problem):
        pr_text_file.read(write(tmp_path, body))


def test_an_unreadable_file_is_an_error(tmp_path: Path) -> None:
    with pytest.raises(ConfigError, match="cannot be read"):
        pr_text_file.read(tmp_path / "missing.md")
