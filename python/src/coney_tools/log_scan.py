# SPDX-License-Identifier: GPL-3.0-or-later
"""The scan of logs attached to issues: possible game data and personal information (`hygiene.attachments`).

An issue's body links its attachments as `https://github.com/user-attachments/files/<id>/<name>`. The hygiene workflow
downloads the ones ending `.txt` or `.log` (`attachment_links`, `download`) and this module looks at their bytes
(`scan_bytes`) for two kinds of trouble:

* **Possible game data** (`LEGAL.md`): content that is not text (a NUL, or too many control characters), a file larger
  than a log should be, or enough lines that look like the game's Lua script source or its data files.
* **Personal information**: a user's home folder (`C:\\Users\\<name>`, `/home/<name>`, `/Users/<name>`; a path written
  from `~` is fine) and email addresses.

The patterns and limits are keys of `.github/conventions.yml`, never constants here. A result holds only the file's
name, counts and line numbers, never the text that matched, so a finding cannot repeat what it warns about. The bytes
stay in memory for the scan and are dropped; nothing is written to disk and nothing downloaded is ever run. The scan is
pure; only `download` touches the network, and the tests replace it.
"""

from __future__ import annotations

import re
import urllib.error
import urllib.parse
import urllib.request
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass, field

from coney_tools.conventions import Attachments

#: A line is matched in its first this many characters, so one huge line cannot make a pattern slow.
LINE_LIMIT = 2000
#: Line numbers named in a message before "and N more".
LINES_SHOWN = 3
#: Control characters that are normal in a log: tab, line feed, carriage return and the escape of terminal colours.
_ALLOWED_CONTROLS = frozenset("\t\n\r\x1b")

#: Downloads at most `limit` bytes of `url` in `timeout` seconds; raises OSError with the reason when it cannot.
Fetch = Callable[[str, int, float], bytes]


@dataclass(frozen=True)
class Link:
    """An attachment linked in an issue's body."""

    url: str
    #: The file name, the link's last part (decoded): what a finding calls the file.
    name: str


@dataclass(frozen=True)
class ScanResult:
    """What the scan found in one attachment: reasons, never content."""

    name: str
    #: Why the file may be game data, one sentence each.
    game_data: tuple[str, ...] = ()
    #: What personal information it shows, one sentence each.
    personal: tuple[str, ...] = ()


@dataclass
class Scans:
    """The scan results of a run by issue number, and what could not be scanned."""

    results: dict[int, tuple[ScanResult, ...]] = field(default_factory=dict)
    #: Downloads that failed or were skipped: reported as notices, not findings.
    notices: list[str] = field(default_factory=list)


def attachment_links(body: str, rules: Attachments) -> list[Link]:
    """The attachments of `body` that the scan reads: under `rules.link`, ending in one of `rules.extensions`.

    Each address once, in the order the body gives them.
    """
    pattern = re.compile(re.escape(rules.link) + r"\d+/[^\s()<>\[\]\"'`]+")
    links: list[Link] = []
    seen: set[str] = set()
    for match in pattern.finditer(body):
        url = match.group(0)
        name = urllib.parse.unquote(url.rsplit("/", 1)[-1])
        if url in seen or not name.lower().endswith(rules.extensions):
            continue
        seen.add(url)
        links.append(Link(url, name))
    return links


def _kb(size: int) -> str:
    """A size in KB, rounded up so that a file over a limit never reads as equal to it."""
    return f"{(size + 1023) // 1024} KB"


def _lines_text(numbers: Sequence[int]) -> str:
    """`line 4`, `lines 1, 7 and 9`, `lines 1, 7, 9 and 12 more`."""
    shown = [str(number) for number in numbers[:LINES_SHOWN]]
    if len(numbers) == 1:
        return f"line {shown[0]}"
    if len(numbers) > LINES_SHOWN:
        return f"lines {', '.join(shown)} and {len(numbers) - LINES_SHOWN} more"
    return f"lines {', '.join(shown[:-1])} and {shown[-1]}"


def _matching(lines: Sequence[str], patterns: Iterable[str]) -> list[int]:
    """The 1-based numbers of the lines that one of `patterns` matches (in its first LINE_LIMIT characters)."""
    compiled = [re.compile(pattern) for pattern in patterns]
    return [
        number
        for number, line in enumerate(lines, start=1)
        if any(regex.search(line[:LINE_LIMIT]) for regex in compiled)
    ]


def _control_share(text: str) -> float:
    """The percentage of `text`'s characters that are control characters a log does not hold (or undecodable)."""
    if not text:
        return 0.0
    odd = sum(
        1
        for character in text
        if character == "\ufffd"
        or ((ord(character) < 32 or ord(character) == 127) and character not in _ALLOWED_CONTROLS)
    )
    return 100.0 * odd / len(text)


def scan_bytes(name: str, data: bytes, rules: Attachments, size: int | None = None) -> ScanResult:
    """Scan one attachment's bytes; `size` is its whole size when only the first part was downloaded."""
    whole = len(data) if size is None else size
    game_data: list[str] = []
    personal: list[str] = []
    if whole > rules.large_kb * 1024:
        game_data.append(f"is {_kb(whole)}, over the {rules.large_kb} KB a log should need")
    text = data.decode("utf-8", errors="replace")
    share = _control_share(text)
    if b"\x00" in data or share > rules.control_percent:
        # Binary content: its lines mean nothing, so the line checks are not run on it.
        game_data.append(f"is not text ({share:.0f}% control or undecodable characters)")
        return ScanResult(name, tuple(game_data), ())
    lines = text.splitlines()
    patterns = rules.patterns
    for kind, found in (
        ("script source", _matching(lines, patterns.script_source)),
        ("game text or data", _matching(lines, patterns.game_text)),
    ):
        if len(found) >= rules.line_threshold:
            game_data.append(f"has {len(found)} lines that look like {kind} ({_lines_text(found)})")
    for kind, found in (
        ("a user folder path", _matching(lines, patterns.user_folder)),
        ("an email address", _matching(lines, patterns.email)),
    ):
        if found:
            personal.append(f"shows {kind} on {_lines_text(found)}")
    return ScanResult(name, tuple(game_data), tuple(personal))


class _HttpsOnly(urllib.request.HTTPRedirectHandler):
    """Follows a redirect only to another https address (GitHub sends attachments on to its storage)."""

    def redirect_request(
        self,
        req: urllib.request.Request,
        fp: object,
        code: int,
        msg: str,
        headers: object,
        newurl: str,
    ) -> urllib.request.Request | None:
        if not newurl.startswith("https://"):
            raise urllib.error.URLError(f"redirect to a non-https address refused ({code})")
        return super().redirect_request(req, fp, code, msg, headers, newurl)  # type: ignore[arg-type]


def download(url: str, limit: int, timeout: float) -> bytes:
    """At most `limit` + 1 bytes of `url` (one more, so a larger file can be told apart), with no credentials.

    The bytes are returned, never written anywhere. OSError (urllib's errors are OSErrors) when it fails.
    """
    if not url.startswith("https://"):
        raise OSError("only https addresses are downloaded")
    opener = urllib.request.build_opener(_HttpsOnly())
    request = urllib.request.Request(url, headers={"User-Agent": "coney-tools-hygiene"})
    try:
        with opener.open(request, timeout=timeout) as response:
            data: bytes = response.read(limit + 1)
            return data
    except (OSError, ValueError) as error:
        raise OSError(str(error)) from error


def scan_link(link: Link, rules: Attachments, fetch: Fetch) -> ScanResult:
    """Download one attachment (up to `rules.download_kb`) and scan it; OSError when the download fails."""
    limit = rules.download_kb * 1024
    data = fetch(link.url, limit, float(rules.timeout_seconds))
    # A file over the download limit is scanned on its first part and counted as bigger than the limit.
    size = len(data) if len(data) <= limit else limit + 1
    return scan_bytes(link.name, data[:limit], rules, size)


def scan_issues(bodies: Mapping[int, str], rules: Attachments, fetch: Fetch = download) -> Scans:
    """Scan the attachments of each issue body (by number), within the per-issue and per-run limits."""
    scans = Scans()
    budget = rules.max_per_run
    for number in sorted(bodies):
        links = attachment_links(bodies[number], rules)
        if len(links) > rules.max_files:
            scans.notices.append(
                f"#{number}: only the first {rules.max_files} of its {len(links)} attachments were scanned"
            )
            links = links[: rules.max_files]
        results: list[ScanResult] = []
        for link in links:
            if budget <= 0:
                scans.notices.append(f"#{number}: not scanned, the run reached its {rules.max_per_run} downloads")
                break
            budget -= 1
            try:
                results.append(scan_link(link, rules, fetch))
            except OSError as error:
                scans.notices.append(f"#{number}: attachment {link.name} could not be downloaded ({error})")
        if results:
            scans.results[number] = tuple(results)
    return scans
