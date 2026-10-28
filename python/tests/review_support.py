# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the `task review` tests: a synthetic review packet and a verdict that answers it. Invented values only."""

import copy
from typing import Any

TIP = "a" * 40
BASE = "b" * 40
FUNCTION = {
    "key": "Brain::think(int)",
    "where": "src/ai/brain.cpp:3",
    "coney": "Brain::think",
    "addresses": ["0x00100010"],
    "names": ["Brain_Think"],
    "body_sha256": "c" * 64,
}
PAGE = {"path": "docs/research/ai/brain.md", "addresses": ["0x00100010", "0x00100020", "0x00100030"]}
IMAGE = {"path": "./shots/after.png", "sha256": "d" * 64}


def make_packet(**changes: Any) -> dict[str, Any]:
    """A packet for a change that touches code, one `@orig` function, one research page and the description."""
    packet: dict[str, Any] = {
        "v": 1,
        "task": "t1",
        "mode": "full",
        "tip": TIP,
        "base": BASE,
        "diff_sha256": "1" * 64,
        "kinds": ["code", "research", "orig", "text", "commits"],
        "files": ["src/ai/brain.cpp", "docs/research/ai/brain.md"],
        "functions": [copy.deepcopy(FUNCTION)],
        "pairs_set_sha256": "2" * 64,
        "pages": [copy.deepcopy(PAGE)],
        "tests": [],
        "text": {
            "file": "PR.md",
            "title": "gameplay: Fix the brain",
            "title_sha256": "3" * 64,
            "paragraph": "Fixes the brain.",
            "paragraph_sha256": "4" * 64,
            "images": [copy.deepcopy(IMAGE)],
            "images_sha256": "5" * 64,
        },
        "rubric_version": 1,
    }
    packet.update(changes)
    return packet


def compared(function: dict[str, Any]) -> list[str]:
    """The addresses a reviewer compares for a packet function: its PS2 ones, then its Xbox ones as `xbox 0x...`."""
    return [*function["addresses"], *(f"xbox {a}" for a in function.get("xbox", []))]


def make_verdict(packet: dict[str, Any], **changes: Any) -> dict[str, Any]:
    """A passing verdict that answers `packet` completely."""
    text = packet["text"]
    verdict: dict[str, Any] = {
        "v": 1,
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
        "verdict": "pass",
        "applied": list(packet["kinds"]),
        "functions": [
            {
                "key": f["key"],
                "body_sha256": f["body_sha256"],
                "addresses": compared(f),
                "class": "clean",
                "evidence": "confirmed (code)",
                "note": "same condition and order",
            }
            for f in packet["functions"]
        ],
        "reviewed": sorted({a for f in packet["functions"] for a in compared(f)}),
        "pages": [{"path": p["path"], "checked": p["addresses"][:2], "unchecked": ""} for p in packet["pages"]],
        "images": [{**image, "shows": "gameplay at the rooftop"} for image in text["images"]],
        "findings": [],
        "sampled": None,
        "not_reviewed": [],
    }
    verdict.update(changes)
    return verdict
