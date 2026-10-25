# SPDX-License-Identifier: GPL-3.0-or-later
"""The reviewer's verdict file for `task review` (`REVIEW.json`, docs/guides/review/task.md#verdict).

`task review` prepares a **packet**: what the change is (tip, base, the hash of the change), which checklists apply,
the `@orig` functions, the research pages and the description's hashes. The reviewer judges and writes the verdict,
copying the packet's values into it. `task review --record` calls `problems` to find out whether the verdict answers
the packet as it is now: every value still the packet's (else the review is stale), every checklist applied, every
function, page and picture covered, and the verdict itself consistent. A verdict with a problem is not recorded.

The classes of an `@orig` function and the rubric are `docs/guides/review/orig.md`; the severities are
`docs/guides/review/index.md#severity`. Nothing here reads the original or the repository.
"""

from __future__ import annotations

import json
from collections.abc import Mapping
from typing import Any

from coney_tools.config import ConfigError
from coney_tools.conventions import REVIEW_KINDS
from coney_tools.orig_review import CLASSES, PASSING

#: The version of this file's format.
VERSION = 1
#: The task's copy of the newest recorded verdict, which the `reviewed` stamp's `review_sha256` covers. Not
#: `review.json`: on a case-insensitive file system (Windows, macOS) that is the description review's `REVIEW.json`
#: in the same folder, and writing one would overwrite the other.
RECORDED = "reviewed.json"
#: The name the copy had before, still read for a review recorded under it.
RECORDED_BEFORE = "review.json"
#: How serious a finding is. Critical and important hold the change back; minor is written down for later.
SEVERITIES = ("critical", "important", "minor")
#: The severities that make a change fail its review.
BLOCKING = frozenset({"critical", "important"})
#: What a reviewer's evidence for a function can be (docs/guides/review/orig.md).
EVIDENCE = ("confirmed (code)", "inferred")
#: The verdicts.
VERDICTS = ("pass", "fail")
#: One letter per class, for the stamp's small `detail`.
CLASS_LETTERS = {
    "clean": "c",
    "diff-deliberate-ok": "d",
    "unclear": "u",
    "diff-uncommented": "a",
    "wrong-address": "w",
    "transcription": "t",
    "diff-bug": "b",
}
#: The keys of a verdict, exactly.
KEYS = (
    "v",
    "task",
    "mode",
    "tip",
    "base",
    "diff_sha256",
    "pairs_set_sha256",
    "title_sha256",
    "paragraph_sha256",
    "images_sha256",
    "rubric_version",
    "verdict",
    "applied",
    "functions",
    "reviewed",
    "pages",
    "images",
    "findings",
    "sampled",
    "not_reviewed",
)
#: The values of the verdict that must be the packet's, each with the words the problem uses for it.
BOUND = (
    ("task", "the task"),
    ("mode", "the mode"),
    ("tip", "the tip"),
    ("base", "the base"),
    ("diff_sha256", "the change"),
    ("pairs_set_sha256", "the pairs"),
    ("title_sha256", "the title"),
    ("paragraph_sha256", "the paragraph"),
    ("images_sha256", "the pictures"),
    ("rubric_version", "the rubric version"),
)
#: A stamp's `detail` may hold this many bytes (stamps.MAX_DETAIL); a longer function list is left to the verdict file.
_DETAIL_LIMIT = 2048


#: The keys an entry of `functions`, `pages` and `images` may have (`path`/`key` is required).
_FUNCTION_KEYS = frozenset({"key", "body_sha256", "addresses", "class", "evidence", "note"})
_PAGE_KEYS = frozenset({"path", "checked", "unchecked"})
_IMAGE_KEYS = frozenset({"path", "sha256", "shows"})


class VerdictError(ConfigError):
    """The verdict file cannot be read as a verdict at all."""


def parse(text: str) -> dict[str, Any]:
    """The verdict in `text`; VerdictError when it is not a JSON object."""
    try:
        data = json.loads(text)
    except ValueError as error:
        raise VerdictError(f"the verdict is not JSON ({error})") from error
    if not isinstance(data, dict):
        raise VerdictError("the verdict is not a JSON object")
    return data


def _findings(verdict: Mapping[str, Any]) -> list[dict[str, Any]]:
    """The finding objects of a verdict (those that are objects)."""
    found = verdict.get("findings")
    return [item for item in found if isinstance(item, dict)] if isinstance(found, list) else []


def blocking(verdict: Mapping[str, Any]) -> list[dict[str, Any]]:
    """The findings that hold the change back."""
    return [item for item in _findings(verdict) if item.get("severity") in BLOCKING]


def _shape(verdict: Mapping[str, Any]) -> list[str]:
    """What is wrong with the keys and the types of the verdict; empty when it can be read further."""
    problems = [f"the verdict is missing the key '{key}'" for key in KEYS if key not in verdict]
    problems += [f"the verdict has an unknown key '{key}'" for key in verdict if key not in KEYS]
    if problems:
        return problems
    if verdict["v"] != VERSION or isinstance(verdict["v"], bool):
        problems.append(f"v: this tool reads version {VERSION}")
    if verdict["verdict"] not in VERDICTS:
        problems.append(f"verdict: expected one of {', '.join(VERDICTS)}, got {verdict['verdict']!r}")
    for key in ("applied", "functions", "reviewed", "pages", "images", "findings", "not_reviewed"):
        if not isinstance(verdict[key], list):
            problems.append(f"{key}: expected a list")
    if verdict["sampled"] is not None and not isinstance(verdict["sampled"], str):
        problems.append("sampled: expected null or the reason some functions were not reviewed")
    for number, finding in enumerate(verdict["findings"] if isinstance(verdict["findings"], list) else []):
        if not isinstance(finding, dict) or set(finding) != {"severity", "where", "text"}:
            problems.append(f"findings[{number}]: expected exactly the keys severity, where and text")
        elif finding["severity"] not in SEVERITIES:
            problems.append(f"findings[{number}]: severity must be one of {', '.join(SEVERITIES)}")
        elif not all(isinstance(finding[key], str) and finding[key].strip() for key in ("where", "text")):
            problems.append(f"findings[{number}]: where and text need words")
    return problems


def _strings(value: object) -> bool:
    """Whether `value` is a list of strings."""
    return isinstance(value, list) and all(isinstance(item, str) for item in value)


def _listed(
    verdict: Mapping[str, Any], field: str, id_key: str, allowed: frozenset[str], known: set[str], what: str
) -> list[str]:
    """Problems of the entries of `verdict[field]`: each an object with exactly the allowed keys, naming one of `known`
    once. `what` says what the ids are (a function key, a page, a picture)."""
    problems: list[str] = []
    seen: set[str] = set()
    for number, item in enumerate(verdict[field]):
        where = f"{field}[{number}]"
        if not isinstance(item, dict):
            problems.append(f"{where}: expected an object")
            continue
        name = item.get(id_key)
        if not isinstance(name, str):
            problems.append(f"{where}: '{id_key}' must be text")
            continue
        extra = sorted(str(key) for key in item if key not in allowed)
        if extra:
            problems.append(f"{where}: unknown keys {', '.join(extra)}")
        if name not in known:
            problems.append(f"{where}: {name!r} is not {what} of this review")
        elif name in seen:
            problems.append(f"{where}: {name!r} is listed twice")
        seen.add(name)
    return problems


def _entries(verdict: Mapping[str, Any], packet: Mapping[str, Any]) -> list[str]:
    """Problems of the shape of the lists: entries that are not objects, that name nothing of the packet, have keys or
    types of the wrong kind, or (a `text` review) are listed at all. Checked before anything reads an entry."""
    keys = {str(item["key"]) for item in packet["functions"]}
    pages = {str(item["path"]) for item in packet["pages"]}
    images = {str(item["path"]) for item in packet["text"]["images"]}
    found = _listed(verdict, "functions", "key", _FUNCTION_KEYS, keys, "an @orig function")
    found += _listed(verdict, "pages", "path", _PAGE_KEYS, pages, "a research page")
    found += _listed(verdict, "images", "path", _IMAGE_KEYS, images, "a picture")
    for field in ("applied", "reviewed", "not_reviewed"):
        if not _strings(verdict[field]):
            found.append(f"{field}: expected a list of text")
    for number, item in enumerate(verdict["functions"]):
        if isinstance(item, dict) and "addresses" in item and not _strings(item["addresses"]):
            found.append(f"functions[{number}]: 'addresses' must be a list of text")
    for number, item in enumerate(verdict["pages"]):
        if not isinstance(item, dict):
            continue
        if "checked" in item and not _strings(item["checked"]):
            found.append(f"pages[{number}]: 'checked' must be a list of text")
        if item.get("unchecked") is not None and not isinstance(item["unchecked"], str):
            found.append(f"pages[{number}]: 'unchecked' must be text or null")
    if _strings(verdict["not_reviewed"]):
        found += [
            f"not_reviewed: {key!r} is not an @orig function of this review"
            for key in verdict["not_reviewed"]
            if key not in keys
        ]
    if packet["mode"] == "text":
        for field in ("functions", "pages", "reviewed", "not_reviewed"):
            if verdict[field]:
                found.append(f"{field}: a text review has none (it reviews the description alone)")
    return found


def _stale(verdict: Mapping[str, Any], packet: Mapping[str, Any]) -> list[str]:
    """Each bound value of the verdict that is no longer the packet's: the review is of another state."""
    text = packet["text"]
    now = {
        "task": packet["task"],
        "mode": packet["mode"],
        "tip": packet["tip"],
        "base": packet["base"],
        "diff_sha256": packet["diff_sha256"],
        "pairs_set_sha256": packet["pairs_set_sha256"],
        "title_sha256": text["title_sha256"],
        "paragraph_sha256": text["paragraph_sha256"],
        "images_sha256": text["images_sha256"],
        "rubric_version": packet["rubric_version"],
    }
    return [
        f"stale: {words} is {str(verdict[key])[:12]!r} in the verdict and {str(now[key])[:12]!r} now"
        for key, words in BOUND
        if verdict[key] != now[key]
    ]


def _applied(verdict: Mapping[str, Any], packet: Mapping[str, Any]) -> list[str]:
    """Checklists the packet needs that the verdict did not apply, and kinds that are none."""
    applied = [item for item in verdict["applied"] if isinstance(item, str)]
    problems = [
        f"applied: '{item}' is not a review kind ({', '.join(REVIEW_KINDS)})"
        for item in applied
        if item not in REVIEW_KINDS
    ]
    left = [kind for kind in packet["kinds"] if kind not in applied]
    if left:
        problems.append(f"applied: the checklists {', '.join(left)} were not applied")
    return problems


def _functions(verdict: Mapping[str, Any], packet: Mapping[str, Any]) -> tuple[list[str], list[str]]:
    """Problems of the `@orig` functions' entries, and the classes among them that fail the review."""
    reviewed: dict[str, Mapping[str, Any]] = {}
    for item in verdict["functions"]:
        if isinstance(item, dict) and isinstance(item.get("key"), str):
            reviewed[item["key"]] = item
    listed = {str(a).lower() for a in verdict["reviewed"]}
    skipped = {key for key in verdict["not_reviewed"] if isinstance(key, str)}
    problems: list[str] = []
    failing: list[str] = []
    for function in packet["functions"]:
        name = f"{function['coney']} ({function['where']})"
        item = reviewed.get(function["key"])
        if item is None:
            if function["key"] not in skipped:
                problems.append(f"{name} is neither reviewed nor listed as not reviewed")
            continue
        cls = item.get("class")
        if cls not in CLASSES:
            problems.append(f"{name}: class {cls!r} is not one of {', '.join(CLASSES)}")
        elif cls not in PASSING:
            failing.append(cls)
        if item.get("evidence") not in EVIDENCE:
            problems.append(f"{name}: evidence must be one of {', '.join(EVIDENCE)}")
        if item.get("body_sha256") != function["body_sha256"]:
            problems.append(f"{name}: reviewed at another body (body_sha256 differs)")
        compared = item.get("addresses")
        own = {a.lower() for a in function["addresses"]}
        if not isinstance(compared, list) or not compared:
            problems.append(f"{name}: no compared `addresses`")
        elif any(not isinstance(a, str) or a.lower() not in own or a.lower() not in listed for a in compared):
            problems.append(f"{name}: `addresses` must be its own and listed in `reviewed`")
    if skipped and not (isinstance(verdict["sampled"], str) and verdict["sampled"].strip()):
        problems.append("functions are listed as not reviewed but `sampled` gives no reason")
    return problems, failing


def _pages(verdict: Mapping[str, Any], packet: Mapping[str, Any], minimum: int) -> list[str]:
    """Problems of the research pages' entries: each touched page, its Ghidra spot checks or a blocking reason."""
    entries: dict[str, Mapping[str, Any]] = {}
    for item in verdict["pages"]:
        if isinstance(item, dict) and isinstance(item.get("path"), str):
            entries[item["path"]] = item
    pages = {page["path"]: page for page in packet["pages"]}
    problems = [f"{path}: not a page the change touches" for path in entries if path not in pages]
    blocked = [str(item.get("where", "")) for item in blocking(verdict)]
    for path, page in pages.items():
        entry = entries.get(path)
        if entry is None:
            problems.append(f"{path}: not reviewed (the verdict does not list the page)")
            continue
        known = {a.lower() for a in page["addresses"]}
        checked = (
            [a for a in entry.get("checked", []) if isinstance(a, str)]
            if isinstance(entry.get("checked"), list)
            else []
        )
        problems += [f"{path}: {a} is not on the page" for a in checked if a.lower() not in known]
        needed = min(minimum, len(known))
        unchecked = entry.get("unchecked")
        if len({a.lower() for a in checked}) >= needed:
            continue
        if isinstance(unchecked, str) and unchecked.strip():
            if not any(where.startswith(path) for where in blocked):
                problems.append(
                    f"{path}: unchecked ({unchecked.strip()}) needs a critical or important finding at the page"
                )
        else:
            noun = "address" if needed == 1 else "addresses"
            problems.append(f"{path}: {needed} {noun} must be checked in Ghidra (or `unchecked` says why not)")
    return problems


def _images(verdict: Mapping[str, Any], packet: Mapping[str, Any]) -> list[str]:
    """Problems of the pictures' entries: each one opened, with the hash it has now and what it shows."""
    entries: dict[str, Mapping[str, Any]] = {}
    for item in verdict["images"]:
        if isinstance(item, dict) and isinstance(item.get("path"), str):
            entries[item["path"]] = item
    problems: list[str] = []
    for image in packet["text"]["images"]:
        entry = entries.get(image["path"])
        if entry is None:
            problems.append(f"{image['path']}: not reviewed (the verdict does not list it)")
        elif entry.get("sha256") != image["sha256"]:
            problems.append(f"{image['path']}: changed since the review (stale verdict)")
        elif not (isinstance(entry.get("shows"), str) and entry["shows"].strip()):
            problems.append(f"{image['path']}: `shows` must say what the picture shows")
    return problems


def problems(verdict: Mapping[str, Any], packet: Mapping[str, Any], pages_min_addresses: int) -> list[str]:
    """Why `verdict` cannot be recorded for `packet` as it is now; empty when it can.

    Every problem is listed, not the first. A `fail` verdict is recorded like a `pass` one: what is refused here is a
    verdict that is stale, incomplete or contradicts itself.
    """
    shape = _shape(verdict)
    if shape:
        return shape
    entries = _entries(verdict, packet)
    if entries:
        return entries
    found = _stale(verdict, packet) + _applied(verdict, packet)
    function_problems, failing = _functions(verdict, packet)
    found += function_problems + _pages(verdict, packet, pages_min_addresses) + _images(verdict, packet)
    stop = blocking(verdict)
    if verdict["verdict"] == "pass":
        if failing:
            found.append(
                f"the verdict is pass but a function is {', '.join(sorted(set(failing)))}: that fails the review"
            )
        if stop:
            severities = ", ".join(sorted({str(item["severity"]) for item in stop}))
            found.append(f"the verdict is pass but lists a {severities} finding")
    elif not failing and not stop:
        found.append("the verdict is fail but gives no reason: a failing class or a critical or important finding")
    return found


def stamp_detail(verdict: Mapping[str, Any]) -> dict[str, Any]:
    """What the `reviewed` stamp keeps of a recorded verdict: small, with a pointer when the function list is long.

    The whole verdict is kept beside the log and covered by the stamp's `review_sha256`.
    """
    mode = str(verdict["mode"])
    functions = {
        address: CLASS_LETTERS[str(item["class"])]
        for item in verdict["functions"]
        if str(item.get("class")) in CLASS_LETTERS
        for address in sorted(item["addresses"])
    }
    stop = blocking(verdict)
    detail: dict[str, Any] = {
        "verdict": verdict["verdict"],
        "scope": "text" if mode == "text" else "full",
        "kinds": list(verdict["applied"]),
        "functions": functions,
        "pages": [item["path"] for item in verdict["pages"]],
        "blockers": len(stop),
        "notes": len(_findings(verdict)) - len(stop),
        "not_reviewed": len(verdict["not_reviewed"]),
    }
    if len(json.dumps(detail, sort_keys=True, separators=(",", ":"))) > _DETAIL_LIMIT - 400:
        # The reviewer's identity and the pointer are added by the caller: leave room for them.
        detail["function_count"] = len(functions)
        del detail["functions"]
    return detail
