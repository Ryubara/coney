# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the scan of logs attached to issues (`coney_tools.log_scan`, `hygiene.attachments`).

Every log here is made up in the test: invented user names, invented script lines, no real log and no game text.
"""

import dataclasses
from typing import Any

import pytest
import yaml
from audit_support import CONVENTIONS, bug_body, context, found, issue
from conventions_support import REPO

from coney_tools.audit_github import issue_attachment_data, issue_attachment_personal
from coney_tools.config import ConfigError
from coney_tools.conventions import parse_conventions
from coney_tools.log_scan import (
    Link,
    ScanResult,
    attachment_links,
    download,
    scan_bytes,
    scan_issues,
    scan_link,
)

RULES = CONVENTIONS.hygiene.attachments
LINK = "https://github.com/user-attachments/files/101/coney.txt"
#: A log in the shape Coney writes, with the home folder already written as ~.
CLEAN_LOG = (
    "[info] Coney 0.1 (abc123)\n"
    "[info] disc: ~/Games/disc.iso\n"
    "[info] profile: ~/AppData/Roaming/Coney\n"
    "[warn] pad 0: no rumble\n"
    "[info] level99 checkpoint 2 loaded in 812 ms\n"
)


def scan(text: str, name: str = "coney.txt") -> ScanResult:
    """Scan `text` as an attachment."""
    return scan_bytes(name, text.encode("utf-8"), RULES)


# --- finding the attachments ----------------------------------------------------------------------------------------


def test_only_text_attachments_are_picked_once_each() -> None:
    body = (
        f"[coney.txt]({LINK}) and again {LINK}\n"
        "[run.LOG](https://github.com/user-attachments/files/102/run.LOG)\n"
        '<img src="https://github.com/user-attachments/assets/aa-bb" />\n'
        "[save.zip](https://github.com/user-attachments/files/103/save.zip)\n"
        "https://example.invalid/user-attachments/files/104/other.txt\n"
        "[my log.txt](https://github.com/user-attachments/files/105/my%20log.txt)\n"
    )
    links = attachment_links(body, RULES)
    assert [link.name for link in links] == ["coney.txt", "run.LOG", "my log.txt"]
    assert links[0] == Link(LINK, "coney.txt")


# --- the scan -------------------------------------------------------------------------------------------------------


def test_a_clean_coney_log_has_no_findings() -> None:
    assert scan(CLEAN_LOG) == ScanResult("coney.txt")


@pytest.mark.parametrize(
    "line",
    [
        r"[info] disc: C:\Users\Jane Doe\Downloads\disc.iso",
        "[info] disc: c:/users/jdoe/disc.iso",
        r"[info] home C:\Documents and Settings\jdoe",
        "[info] disc: /home/jdoe/games/disc.iso",
        "[info] disc: /Users/jdoe/Games/disc.iso",
    ],
)
def test_a_user_folder_is_personal_information(line: str) -> None:
    result = scan(CLEAN_LOG + line + "\n")
    assert result.personal == ("shows a user folder path on line 6",) and result.game_data == ()


@pytest.mark.parametrize(
    "line",
    [
        r"[info] C:\Users\Public\Documents\disc.iso",
        "[info] /Users/Shared/disc.iso",
        "[info] ~/home/jdoe-is-not-a-home",
        r"[info] D:\Games\disc.iso",
    ],
)
def test_shared_folders_and_tilde_paths_are_not_personal(line: str) -> None:
    assert scan(CLEAN_LOG + line + "\n").personal == ()


def test_an_email_address_is_personal_information() -> None:
    result = scan("[info] signed in as jane.doe@example.com\n" + CLEAN_LOG)
    assert result.personal == ("shows an email address on line 1",)


def test_a_finding_names_lines_never_the_text() -> None:
    lines = "".join(f"[info] C:\\Users\\jdoe{n}\\x\n" for n in range(6))
    result = scan(lines)
    assert result.personal == ("shows a user folder path on lines 1, 2, 3 and 3 more",)
    assert "jdoe" not in " ".join(result.personal)


def test_binary_content_is_not_text() -> None:
    result = scan_bytes("coney.txt", b"\x7fELF\x00\x01\x02" + b"C:\\Users\\jdoe\\" * 4, RULES)
    assert len(result.game_data) == 1 and result.game_data[0].startswith("is not text")
    assert result.personal == ()


def test_many_control_characters_are_not_text() -> None:
    result = scan_bytes("coney.log", bytes(range(1, 32)) * 4 + b"text", RULES)
    assert len(result.game_data) == 1 and result.game_data[0].startswith("is not text")


def test_terminal_colours_and_tabs_are_text() -> None:
    assert scan("\x1b[31m[error]\x1b[0m\tfailed\r\n" * 20).game_data == ()


def test_a_large_file_may_be_game_data() -> None:
    result = scan(CLEAN_LOG * (RULES.large_kb * 1024 // len(CLEAN_LOG) + 1))
    assert result.game_data == (f"is {RULES.large_kb + 1} KB, over the {RULES.large_kb} KB a log should need",)


def test_lines_of_script_source_from_the_threshold() -> None:
    script = "local speed = 4\nfunction Walk(who)\n  if who then\n    Move(who, speed)\n  end\nend\n"
    assert scan(CLEAN_LOG + "local a = 1\nend\n").game_data == ()
    result = scan(CLEAN_LOG + script)
    assert result.game_data == ("has 5 lines that look like script source (lines 6, 7, 8 and 2 more)",)


def test_lines_of_a_placed_objects_file_from_the_threshold() -> None:
    line = "{crate {1.5,2,-3}, {0,0,0,1}, -1, 0, 0, 0, none )\n"
    assert scan(line * (RULES.line_threshold - 1)).game_data == ()
    assert "game text or data" in scan(line * RULES.line_threshold).game_data[0]


# --- downloading ----------------------------------------------------------------------------------------------------


class FakeFetch:
    """Downloads from a table of made-up files, recording what was asked."""

    def __init__(self, files: dict[str, bytes], fail: frozenset[str] = frozenset()) -> None:
        self.files = files
        self.fail = fail
        self.asked: list[tuple[str, int, float]] = []

    def __call__(self, url: str, limit: int, timeout: float) -> bytes:
        self.asked.append((url, limit, timeout))
        if url in self.fail:
            raise OSError("timed out")
        return self.files[url][: limit + 1]


def test_a_download_is_bounded_and_a_longer_file_counts_as_large() -> None:
    limit = RULES.download_kb * 1024
    fetch = FakeFetch({LINK: b"[info] x\n" * (limit // 9 + 10)})
    result = scan_link(Link(LINK, "coney.txt"), RULES, fetch)
    assert fetch.asked == [(LINK, limit, float(RULES.timeout_seconds))]
    assert result.game_data[0].startswith(f"is {RULES.download_kb + 1} KB")


def test_issues_are_scanned_within_the_limits() -> None:
    rules = dataclasses.replace(RULES, max_files=2, max_per_run=3)
    links = [f"https://github.com/user-attachments/files/{n}/log{n}.txt" for n in range(1, 5)]
    fetch = FakeFetch({url: b"[info] C:\\Users\\jdoe\\x\n" for url in links}, fail=frozenset({links[1]}))
    scans = scan_issues({7: " ".join(links), 8: links[2], 9: links[3]}, rules, fetch)
    assert [url for url, _, _ in fetch.asked] == [links[0], links[1], links[2]]
    assert list(scans.results) == [7, 8] and scans.results[7][0].name == "log1.txt"
    assert scans.notices == [
        "#7: only the first 2 of its 4 attachments were scanned",
        "#7: attachment log2.txt could not be downloaded (timed out)",
        "#9: not scanned, the run reached its 3 downloads",
    ]


def test_only_https_is_downloaded() -> None:
    with pytest.raises(OSError, match="only https"):
        download("http://example.invalid/a.txt", 10, 1.0)


# --- the audit checks -----------------------------------------------------------------------------------------------


def test_the_checks_report_open_issues_with_findings_and_never_the_text() -> None:
    results = {
        7: (ScanResult("coney.txt", ("is not text (40% control or undecodable characters)",), ()),),
        8: (ScanResult("coney.txt", (), ("shows a user folder path on line 1",)),),
        9: (ScanResult("coney.txt", (), ("shows an email address on line 2",)),),
    }
    ctx = dataclasses.replace(
        context(issues=[issue(7, body=bug_body()), issue(8, body=bug_body()), issue(9, state="CLOSED")]),
        attachments=results,
    )
    [data] = found(issue_attachment_data, ctx)
    assert data.item == "#7" and "coney.txt is not text" in data.message and "LEGAL.md" in data.message
    [personal] = found(issue_attachment_personal, ctx)
    assert personal.item == "#8" and "coney.txt shows a user folder path on line 1" in personal.message


def test_without_a_scan_the_checks_find_nothing() -> None:
    assert found(issue_attachment_personal, context(issues=[issue(7)])) == []


# --- the conventions ------------------------------------------------------------------------------------------------


def real_data() -> Any:
    """The conventions file as plain YAML data."""
    return yaml.safe_load((REPO / ".github/conventions.yml").read_text(encoding="utf-8"))


@pytest.mark.parametrize(
    ("key", "value", "message"),
    [
        ("large_kb", 4096, "must not be more than download_kb"),
        ("link", "http://example.invalid/", "https://"),
        ("extensions", ["log"], "lower-case ending"),
        ("control_percent", 101, "100 or less"),
    ],
)
def test_bad_attachment_settings_are_refused(key: str, value: object, message: str) -> None:
    data = real_data()
    data["hygiene"]["attachments"][key] = value
    with pytest.raises(ConfigError, match=message):
        parse_conventions(data)


def test_a_pattern_that_does_not_compile_is_refused() -> None:
    data = real_data()
    data["hygiene"]["attachments"]["patterns"]["email"] = ["(unclosed"]
    with pytest.raises(ConfigError, match=r"patterns\.email\[0\]: not a valid regular expression"):
        parse_conventions(data)


def test_the_attachment_settings_are_read() -> None:
    assert RULES.link == "https://github.com/user-attachments/files/"
    assert RULES.extensions == (".txt", ".log") and RULES.large_kb <= RULES.download_kb


def test_a_package_pin_and_an_asset_name_are_not_an_email_address() -> None:
    # The pattern is shared with the commit-msg check (`pr check-commit`), where these appear in messages.
    result = scan("[info] using markdownlint-cli2@0.18.1 and icon@2x.png\n" + CLEAN_LOG)
    assert result.personal == ()
    result = scan("[info] icon@1.5x.png and logo@2X.PNG\n" + CLEAN_LOG)
    assert result.personal == ()


@pytest.mark.parametrize(
    "address",
    [
        "jane@2x.com",
        "ops@10x.dev",
        "x@0x.ai",
        "bob@4x.co.uk",
        "JANE@2X.COM",
        "jane@3x.mail.example.com",
        "jane@2x.png.example.com",
        "jane@2x.svg.io",
        "jane@2x.png-cdn.com",
    ],
)
def test_an_address_whose_domain_starts_like_a_scale_suffix_is_still_personal(address: str) -> None:
    result = scan(f"[info] signed in as {address}\n" + CLEAN_LOG)
    assert result.personal == ("shows an email address on line 1",)
