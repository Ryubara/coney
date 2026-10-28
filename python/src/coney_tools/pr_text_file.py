# SPDX-License-Identifier: GPL-3.0-or-later
"""The engineer's description file (`PR.md`): the title, the summary paragraph and the pictures, and the hashes a review
binds to (docs/guides/review/task.md).

Line 1 is the title (`area: Verb the rest`). The `## Summary` section holds the paragraph the reviewer reads against the
change; the pictures are the local images the file shows, relative to the file's folder. The hashes are what `task
review` quotes in a `reviewed` stamp, so an edit to the title or the paragraph, or a replaced picture, makes the review
stale and it is done again. They are of normalised text (a leading BOM dropped, CRLF and CR made LF, trailing spaces
cut), so an editor or a checkout that changes line endings does not.

This module only reads: the rules about what the file may hold are `pull_requests` in `.github/conventions.yml`, run by
`pr check-title` and `pr check-body`.
"""

from __future__ import annotations

import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

from coney_tools.config import ConfigError
from coney_tools.pr_review import local_images

_SUMMARY = re.compile(r"^## Summary[ \t]*$", re.MULTILINE)
_NEXT_SECTION = re.compile(r"^## ", re.MULTILINE)
_COMMENT = re.compile(r"<!--.*?-->", re.DOTALL)
#: Lines of the Summary that are not the paragraph: the closing keywords and the `Docs: none` line.
_NOT_PARAGRAPH = re.compile(r"^(?:(?:Fixes|Refs) #\d+.*|Docs: none.*)$")


@dataclass(frozen=True)
class Image:
    """One local picture the file shows."""

    #: As the file writes it (`./shots/after.png`).
    path: str
    sha256: str


@dataclass(frozen=True)
class TextFile:
    """What the description file says that a review binds to."""

    #: The file read.
    file: Path
    title: str
    paragraph: str
    #: The local pictures that exist, in order of appearance, and the references to ones that do not.
    images: tuple[Image, ...]
    missing: tuple[str, ...]

    @property
    def title_sha256(self) -> str:
        """The hash of the title."""
        return _sha256(self.title)

    @property
    def paragraph_sha256(self) -> str:
        """The hash of the summary paragraph."""
        return _sha256(self.paragraph)

    @property
    def images_sha256(self) -> str:
        """The hash of the sorted lines `<path> <content hash>` (the order they appear in does not matter).

        The path is in it so that swapping the contents of two pictures changes the hash too.
        """
        return _sha256("\n".join(sorted(f"{image.path} {image.sha256}" for image in self.images)))


def _sha256(text: str) -> str:
    """The SHA-256 of `text` as UTF-8."""
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def _normal(text: str) -> str:
    """`text` with a leading BOM dropped, line endings made LF and trailing spaces cut."""
    text = text.removeprefix("﻿").replace("\r\n", "\n").replace("\r", "\n")
    return "\n".join(line.rstrip() for line in text.split("\n"))


def _paragraph(body: str) -> str:
    """The text of the `## Summary` section without comments, closing keywords and the `Docs: none` line."""
    heading = _SUMMARY.search(body)
    if heading is None:
        raise ConfigError("the description has no '## Summary' section")
    rest = body[heading.end() :]
    following = _NEXT_SECTION.search(rest)
    section = _COMMENT.sub("", rest[: following.start()] if following else rest)
    lines = [line for line in section.split("\n") if not _NOT_PARAGRAPH.match(line)]
    text = "\n".join(lines).strip("\n")
    # Runs of blank lines are layout, not content.
    text = re.sub(r"\n{3,}", "\n\n", text).strip()
    if not text:
        raise ConfigError("the Summary has no text")
    return text


def read(path: Path) -> TextFile:
    """The description file at `path`; ConfigError when it cannot be read or has no title or no summary."""
    try:
        raw = path.read_bytes().decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"{path}: cannot be read ({error})") from error
    text = _normal(raw)
    if not text.strip():
        raise ConfigError(f"{path}: the description file is empty")
    title, _, body = text.partition("\n")
    if not title.strip():
        raise ConfigError(f"{path}: line 1 is empty (it holds the title)")
    found: list[Image] = []
    missing: list[str] = []
    for reference in local_images(text):
        try:
            digest = hashlib.sha256((path.parent / reference).read_bytes()).hexdigest()
        except OSError:
            missing.append(reference)
        else:
            found.append(Image(reference, digest))
    try:
        paragraph = _paragraph(body)
    except ConfigError as error:
        raise ConfigError(f"{path}: {error}") from error
    return TextFile(path, title.strip(), paragraph, tuple(found), tuple(missing))
