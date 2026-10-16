"""Scan throughput on synthetic data, with a made-up marker set shaped like a real one (alternations, word
boundaries, hex-digit classes, listing lines). Not part of the unit run (its name does not start with "test"):

    cd python && python -I -B tests/bench_scan.py [MB] [EXTRA_REGEX]

EXTRA_REGEX adds one more pattern marker, say a short one: binary data costs more when a marker can match only a few
bytes, since shorter text runs are then kept.

Prints MB/s for random bytes scanned whole (in memory), random bytes read in chunks (a file over FULL_MAX) and
text, each in this process with one job and no time limit.
"""

import json
import os
import random
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from coney_tools.cleanroom import scan

WORDS = "(?:zorb|quax|blorp|frizz|wimble|snark|glomp|trazz|vunk)"
MARKERS = [
    ("pattern", rf"\b{WORDS}\s{{1,8}}[a-z]{{2,4}}[0-9]{{1,2}},\s{{0,4}}-?[0-9]{{1,6}}\b", ["IGNORECASE"]),
    ("pattern", r"\bQZ_[0-9a-fA-F]{8}\b", []),
    ("pattern", r"\b(?:qz|vx)[A-Z][a-z]{2,12}_[0-9a-f]{6,8}\b", []),
    ("pattern", r"\b(?:blip|blop|blup)[0-9]{1,3}\s{0,4}=\s{0,4}\(\s{0,4}[a-z]{3,8}\s{0,4}\*\)", []),
    ("pattern", r"\b0x[0-9a-fA-F]{6,8}\b\s{0,4}(?:\+|-)\s{0,4}0x[0-9a-fA-F]{1,4}\b", []),
    ("pattern", r"\bwib_(?:[a-z]{1,8}_){1,4}[a-z]{1,8}\(", []),
    ("pattern", rf"\b{WORDS}\.(?:s|d|w|l)\b", ["IGNORECASE"]),
    ("pattern", r"(?:zz|yy|xq)[0-9a-f]{4}\s{0,3}:\s{0,3}(?:quix|quax|quox)\b", []),
    ("pattern", r"\bglorp(?:Int|Float|Ptr){1,3}\b", []),
    ("listing", r"^\s{0,8}[0-9a-fA-F]{8}\s{1,4}[0-9a-fA-F]{8}\s{1,8}" + WORDS + r"\b", ["IGNORECASE"]),
    ("listing", r"^\s{0,8}QQ[0-9]{4}\s{1,8}[a-z]{2,8}\s{0,8}$", []),
    ("listing", r"^[0-9a-f]{6,8}:\s{0,4}(?:[0-9a-f]{2}\s){2,8}\s{0,8}" + WORDS, []),
    ("listing", r"^\s{0,8}(?:zub|zab|zib)\s{1,8}[a-z][a-z0-9]{1,3}\s{0,4}(?:,|$)", []),
    ("listing", r"^\s{0,8}LBL_[0-9a-f]{4,8}:\s{0,8}$", []),
    ("listing", r"^\s{0,8}/\*\s{0,4}[0-9a-f]{8}\s{0,4}\*/\s{0,8}" + WORDS, ["IGNORECASE"]),
]


def make_scan(extra=None):
    markers = MARKERS + ([("pattern", extra, [])] if extra else [])
    doc = {
        "markers": [
            {"name": f"m{i}", "kind": k, "regex": r, "flags": f, "min_lines": 3} for i, (k, r, f) in enumerate(markers)
        ]
    }
    markers = scan.load_markers(json.dumps(doc).encode())
    return scan.Scan(markers, {"0" * 64}, {"0" * 16}, {}, "", {"jobs": 1, "file_timeout": 0})


def timed(what, size, fn):
    t = time.perf_counter()
    fn()
    dt = time.perf_counter() - t
    print(f"{what}: {size / 1e6:.0f} MB in {dt:.2f} s = {size / 1e6 / dt:.1f} MB/s", flush=True)


def main():
    mb = int(sys.argv[1]) if len(sys.argv) > 1 else 16
    s = make_scan(sys.argv[2] if len(sys.argv) > 2 else None)
    print(f"shortest match {min(m.width for m in s.markers)} bytes: text runs need {s.least} in a row", flush=True)
    rnd = random.Random(7).randbytes(mb * 1_000_000)
    timed("random, in memory", len(rnd), lambda: s.scan_blob("r", rnd, "root", scan.Budget("r"), 0))
    line = "the quick brown fox jumps over the lazy dog, again and again: line {}\n"
    text = "".join(line.format(i) for i in range(mb * 1_000_000 // len(line))).encode()
    timed("text, in memory", len(text), lambda: s.scan_blob("t", text, "root", scan.Budget("t"), 0))
    with tempfile.TemporaryDirectory() as tmp:
        p = os.path.join(tmp, "big.bin")
        with open(p, "wb") as f:
            f.writelines(random.Random(i).randbytes(mb * 1_000_000) for i in range(4))
        size = os.path.getsize(p)
        old = scan.FULL_MAX
        scan.FULL_MAX = mb * 1_000_000
        try:
            with open(p, "rb") as f:
                head = f.read(scan.SNIFF)
            timed("random, in chunks", size, lambda: s.large_file(p, p, size, head, "root"))
        finally:
            scan.FULL_MAX = old
    print(f"findings: {len(s.out.hits)} hits, {len(s.out.gaps)} gaps", flush=True)


if __name__ == "__main__":
    main()
