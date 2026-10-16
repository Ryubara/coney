"""Clean-room scan: looks through folders for material that must not be there, and reports paths and names only.

The scanner holds no patterns of its own. What it looks for comes from private data files given on the command line:

- markers (JSON): named regular expressions. Kind "pattern" counts matches anywhere; kind "listing" counts lines
  that match (a line format, such as a disassembly or decompilation listing) and needs `min_lines` of them;
- old-file hashes (text, one SHA-256 per line): an exact copy of any of these files is a hit;
- shingles (text, one hash per line): 3 or more shared 5-line windows make a near copy, which is a hit;
- an allow list (JSON, optional): files reviewed and kept on purpose, each with its hash, path, reason and the hash
  of the markers file it was reviewed under. When the markers have changed since, the file is checked again, and any
  marker hit is reported as "allowed under older markers: re-check" until a reviewer renews the entry.

Every finding names the file and the marker's name, never the matched text. Whatever the scan could not check is a
coverage gap. Exit status: 0 no hits and no gaps, 1 hits or gaps, 2 the scan could not run.

Files are scanned by worker processes (--jobs; one per CPU by default) while this process walks the folders; the
report is the same whatever their number. A file that takes longer than --file-timeout seconds is stopped and is a
coverage gap.
"""

import base64
import binascii
import bisect
import bz2
import codecs
import collections
import concurrent.futures as cf
import concurrent.futures.process  # noqa: F401 -- loads cf.process, which the pool code uses
import hashlib
import io
import json
import lzma
import multiprocessing
import multiprocessing.reduction
import os
import pickle
import platform
import re
import re._constants as _rc
import re._parser as _rp
import signal
import stat
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

FULL_MAX = 256_000_000  # larger files: hash and raw markers only, read in chunks
MEMBER_MAX = 100_000_000  # larger archive members, decoded blobs and git objects are not opened (a gap)
CHUNK = 16_000_000  # even, so UTF-16 views of a chunk stay aligned
OVERLAP = 4096  # the widest match a marker may have; chunks overlap by this much
CONTEXT = 256  # bytes kept before a chunk for lookbehinds and word boundaries
NEST_DEPTH = 4  # archives inside archives (and base64, PDF streams, git objects) are opened this deep
EXPAND_MAX = 1_000_000_000  # bytes unpacked from one file, all levels together
MEMBERS_MAX = 100_000  # members, streams and blobs opened from one file
B64_MIN = 24  # shortest base64 run decoded
B64_PIECE = 16  # shortest stretch of base64 letters taken as (part of) a run
FOUND_MIN = 512  # input a damaged or cut-short stream found by its signature must use before it is a gap
GIT_MIN = (2, 44)  # oldest git trusted to read objects (GIT_NO_LAZY_FETCH)
B64_MAX = 64_000_000  # longest
JSON_MAX = 128_000_000  # larger JSON documents are not parsed as a whole (a gap); JSONL lines still are
JSON_DEPTH = 4  # JSON kept inside a JSON string is opened this deep; deeper is a gap
PDF_STREAMS_MAX = 10_000
SNIFF = 65_536
EXACT_MIN = 200  # smaller files are not checked as exact copies (too many trivial matches)
NEAR_MIN = 3  # shared 5-line windows that make a near copy
REPEAT_MAX = 10_000  # largest bound of one repeat in a marker
REPEAT_PRODUCT_MAX = 10_000  # largest product of nested repeat bounds
PROBE_SIZE = 65_536  # each marker is timed over adversarial inputs of this size when it is loaded
PROBE_BUDGET = 0.5  # seconds one marker may take over one such input; a slower marker is refused
GIT_TIMEOUT = 1800  # seconds one git call may take; a call that takes longer is stopped (a gap)
PIECE = 65_536  # compressed input fed per call, so output before damage is kept
PROGRESS_EVERY = 30  # seconds between progress lines on standard error
BATCH_TASKS = 16  # files (or git objects) handed to a worker at once
BATCH_BYTES = 16_000_000  # git object bytes handed to a worker at once
INFLIGHT_PER_JOB = 4  # batches waiting or running per worker
INFLIGHT_BYTES = 512_000_000  # git object bytes waiting or running in all
JOBS_MAX = 61 if os.name == "nt" else 1024  # the most worker processes Windows allows one pool
FILE_TIMEOUT = 600  # seconds one file (or git object) may take in a worker; a slower one is stopped (a gap)
WATCH_EVERY = 0.5  # seconds between looks at what each worker is doing
SLOW_AFTER = 60  # seconds a file must have run before the progress line says how long the slowest has run
BIG_TASK = 8_000_000  # files this large are handed to a worker alone, so no small file waits behind one
RUN_LEAST_MAX = B64_PIECE  # binary data: a text run is kept when it holds this many ASCII characters in a row (or
# fewer: as many as the shortest match any marker can have)
SHINGLE_RUN = 24  # binary data: text runs this long or longer are checked for near copies (5 lines of 4 characters)
RUN_CARRY_MAX = 1_000_000  # bytes of one text run held back between chunks; a longer run is passed on in parts

DATA_NAMES = {
    "markers": "markers.json",
    "hashes": "old-file-hashes.txt",
    "shingles": "old-shingles.txt",
    "allow": "allow.json",
}
FLAGS = {
    "IGNORECASE": re.IGNORECASE,
    "MULTILINE": re.MULTILINE,
    "DOTALL": re.DOTALL,
    "VERBOSE": re.VERBOSE,
    "ASCII": re.ASCII,
}
EXACT = "exact copy of an old file"
NEAR = "near copy of old text (shared 5-line windows)"
RECHECK = "allowed under older markers: re-check"
SHA256 = re.compile(r"[0-9a-f]{64}")
HEX_TEXT = re.compile(rb"[0-9a-fA-F]+")
B64_RUN = re.compile(rb"[A-Za-z0-9+/_-]{%d,1024}={0,2}" % B64_PIECE)
B64_URL = bytes.maketrans(b"-_", b"+/")
JSON_ESCAPE = re.compile(r'\\(?:u([0-9a-fA-F]{4})|(["\\/bfnrt]))')
JSON_ESCAPES = {"b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t", '"': '"', "\\": "\\", "/": "/"}
B64_WRAP = re.compile(rb"[\"']?[ \t]{0,8}[,+]?[ \t]{0,8}\r?\n[ \t]{0,64}\+?[ \t]{0,8}[\"']?")  # quotes, indentation
PDF_STREAM = re.compile(rb"(?<!end)stream(?:\r\n|\r|\n| \r?\n)")
PDF_END = re.compile(rb"endstream")
EMBEDDED = re.compile(  # signatures of what may hide inside a file not opened whole: zip, gzip, zlib, bzip2, xz, 7z,
    # RAR, zstd
    rb"PK\x03\x04|\x1f\x8b\x08|\x78[\x01\x5e\x9c\xda]|BZh[1-9]1AY&SY|\xfd7zXZ\x00|7z\xbc\xaf\x27\x1c|Rar!\x1a\x07"
    rb"|\x28\xb5\x2f\xfd"
)
PDF_FILTER = re.compile(rb"/Filter\s*(\[[^\]]{0,512}\]|/[A-Za-z0-9#]{1,64})")
PDF_FILTER_REF = re.compile(rb"/Filter\s*\d{1,10}\s+\d{1,10}\s+R")
PDF_PREDICTOR = re.compile(rb"/Predictor\s*0{0,8}(?:[2-9]|[1-9]\d)")
# Predictors are routine on cross-reference and image streams (offsets and pixels, not text): no gap for those.
PDF_PIXELS_OR_XREF = re.compile(rb"/Type\s*/XRef\b|/Subtype\s*/Image\b")
PDF_BRACKETS = re.compile(rb"<<|>>")
PDF_DICT_MAX = 4096  # how far back from a stream keyword its dictionary is looked for
PDF_NAME = re.compile(rb"/([A-Za-z0-9#]{1,64})")
PDF_ENCRYPT = re.compile(rb"/Encrypt\s*(?:\d|<<)")
PDF_IMAGE_FILTERS = {"DCTDecode", "DCT", "JPXDecode", "CCITTFaxDecode", "CCF", "JBIG2Decode"}
GIT_HEADER = re.compile(rb"(?:blob|tree|commit|tag) [0-9]{1,12}\x00")
TRIVIAL = {"{", "}", "};", "*/", "/*", "#", "---", "```"}
WS = re.compile(r"\s+")
REPEATS = (_rc.MAX_REPEAT, _rc.MIN_REPEAT, _rc.POSSESSIVE_REPEAT)
ARCHIVES = ("zip", "gzip", "bzip2", "xz", "zlib", "tar", "pack", "bundle")
RAW_ONLY = ("sqlite", "cfb", "ar", "cpio", "cab")  # uncompressed containers: raw and UTF-16 views
UNOPENED = (  # compressed, no standard-library reader: a gap
    "7z",
    "rar",
    "zstd",
    "lz4",
    "cab-compressed",
    "compress (.Z)",
    "lzip",
    "lzop",
    "rpm",
    "squashfs",
    "arj",
)
FULLY_OPENED = ("zip", "gzip", "bzip2", "xz", "zlib", "tar", "pack", "bundle", "pdf", "disc", *UNOPENED)


class DataError(Exception):
    """A data file is missing or invalid: the scan cannot run. The message never holds a pattern."""


class GitError(Exception):
    """git could not list or read objects."""


# ---------- markers ----------


@dataclass
class Marker:
    name: str
    kind: str
    rx: re.Pattern
    min_lines: int = 1
    width: int = 1  # the fewest bytes a match can span (lookarounds and anchors count 0)

    def count(self, data, lo=0, hi=None):
        """Matches (pattern) or matching lines (listing) that start in data[lo:hi]."""
        hi = len(data) if hi is None else hi
        n, line_end = 0, -1
        for m in self.rx.finditer(data, lo):
            s = m.start()
            if s >= hi:
                break
            if self.kind == "pattern":
                n += 1
            elif s > line_end:  # count lines, not matches; linear: each line is counted once
                n += 1
                line_end = data.find(b"\n", s)
                if line_end < 0:
                    line_end = len(data)
        return n

    def hit(self, n):
        return n >= (self.min_lines if self.kind == "listing" else 1)


def lf(data):
    """CRLF and lone CR as LF, so listing markers see lines the same whatever the line endings."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n") if b"\r" in data else data


def count_view(markers, view):
    """{name: count} for one whole view: listing markers over its LF form, pattern markers over the view as it is
    and over its LF form (so anchors see CRLF lines), the larger count."""
    flat = lf(view)
    out = {}
    for m in markers:
        out[m.name] = (
            m.count(flat) if m.kind == "listing" else max(m.count(view), m.count(flat) if flat is not view else 0)
        )
    return out


class StreamCounter:
    """Counts markers over data that arrives in chunks. A match counts in the chunk where it starts, once the bytes
    after it are in (markers are at most OVERLAP wide), so the overlap is never counted twice. Pattern markers run
    over the data as it is and over its LF form (the larger count); listing markers over the LF form."""

    def __init__(self, markers):
        patterns = [m for m in markers if m.kind == "pattern"]
        self.groups = [
            (False, patterns),
            (True, patterns + [m for m in markers if m.kind == "listing"]),
        ]
        self.state = {False: (b"", 0), True: (b"", 0)}
        self.cr = b""  # a CR at the end of a chunk waits for the next chunk (it may start with the LF)
        self.by_view = {False: {m.name: 0 for m in markers}, True: {m.name: 0 for m in markers}}
        self.kinds = {m.name: m.kind for m in markers}

    @property
    def counts(self):
        raw, flat = self.by_view[False], self.by_view[True]
        return {n: flat[n] if k == "listing" else max(raw[n], flat[n]) for n, k in self.kinds.items()}

    def feed(self, chunk, final=False):
        for is_lf, ms in self.groups:
            if not ms:
                continue
            if is_lf:
                chunk2 = self.cr + chunk
                self.cr = b""
                if not final and chunk2.endswith(b"\r"):
                    chunk2, self.cr = chunk2[:-1], b"\r"
                piece = lf(chunk2)
            else:
                piece = chunk
            carry, lo = self.state[is_lf]
            buf = carry + piece
            hi = len(buf) if final else max(lo, len(buf) - OVERLAP)
            for m in ms:
                self.by_view[is_lf][m.name] += m.count(buf, lo, hi)
            cut = max(0, hi - CONTEXT)
            self.state[is_lf] = (buf[cut:], hi - cut)
        return self


# ---------- text in binary data ----------
#
# Binary data is not scanned byte by byte: only the text it can hold is. A text run is a stretch of text characters
# (printable ASCII, tab, line ends, vertical tab and form feed, and well-formed UTF-8 characters) that holds at least
# `least` ASCII text characters in a row; a UTF-16 run is a stretch of 16-bit little-endian code units of printable
# ASCII, tab, line ends and Latin-1 letters, at either byte alignment (so big-endian text is found one byte on).
#
# The runs are joined into a view as they stood in the data: between two runs, where the data held bytes that are
# not text, the view holds one zero byte, and where those bytes held a line end (LF or CR), a zero byte, LF and a
# zero byte. So a run's neighbours are bytes that are not text, as they were, and lines end only where the data's
# lines did.

TEXT_ASCII = bytes(range(9, 14)) + bytes(range(32, 127))
UTF8_UNIT = rb"[\t-\r -~]|[\xc2-\xdf][\x80-\xbf]|[\xe0-\xef][\x80-\xbf]{2}|[\xf0-\xf4][\x80-\xbf]{3}"
TEXT_RUN = re.compile(rb"(?:" + UTF8_UNIT + rb")+")
UTF16_RUN = re.compile(rb"(?:[\t-\r -~\xa0-\xff]\x00)+")
ASCII_MAP = bytes(0x61 if b in TEXT_ASCII else 0 for b in range(256))  # text: "a"; anything else: zero
UTF16_MAP = bytes(0x61 if b in TEXT_ASCII or b >= 0xA0 else 0x7A if b == 0 else 0x2E for b in range(256))
# zero for a byte no text run can hold (not ASCII text, not a UTF-8 lead or continuation byte)
HARD_MAP = bytes(0x61 if b in TEXT_ASCII or 0x80 <= b <= 0xBF or 0xC2 <= b <= 0xF4 else 0 for b in range(256))
UTF8_LEAD = {b: 2 if b < 0xE0 else 3 if b < 0xF0 else 4 for b in range(0xC2, 0xF5)}  # lead byte: character length
RUN_TAIL = 64  # bytes of a long run held back for the next chunk
# What lay between two runs: 0 bytes that are not text only, 1 also a line break only Python's str.splitlines()
# takes (vertical tab, form feed, file, group and record separators; near copies count those), 2 also LF or CR.
LINE_END = re.compile(rb"[\n\r]")
LINE_BREAK = re.compile(rb"[\x0b\x0c\x1c-\x1e]")
LINE_END16 = re.compile(rb"[\n\r]\x00")
MARKER_SEPS = (b"\x00", b"\x00", b"\x00\n\x00")  # by what lay between: markers see LF and CR as line ends
SHINGLE_SEPS = (b"\x00", b"\n", b"\n")  # near copies: lines end where any line break stood


class Runs:
    """Text runs (utf16 False) or UTF-16 runs (True) in data that arrives in chunks; the whole data is one final
    feed. least: the ASCII characters in a row a text run needs, or the code units a UTF-16 run needs. A run that
    may go on into the next chunk is held back (up to RUN_CARRY_MAX bytes; a longer one is passed on in parts).
    UTF-16 runs at even and at odd offsets are two lanes, each with its own view."""

    def __init__(self, utf16, least):
        self.utf16, self.rx = utf16, UTF16_RUN if utf16 else TEXT_RUN
        self.key = (b"az" if utf16 else b"a") * max(1, least)
        self.unit = 2 if utf16 else 4  # the widest unit: a run ending closer than this to the end may go on
        self.buf, self.base, self.open = b"", 0, False
        self.last = [0, 1] if utf16 else [0]  # where each lane's previous run ended (or its view starts)
        self.held = [0, 0] if utf16 else [0]  # what lay between since then, in bytes no longer held

    @property
    def end(self):
        """The offset just past the data fed so far."""
        return self.base + len(self.buf)

    def back(self, buf, t, s, lo):
        """The start of the text run whose first long ASCII stretch starts at s: back over UTF-8 characters and the
        ASCII text between them, no further than lo."""
        while s - 2 >= lo and 0x80 <= buf[s - 1] <= 0xBF:
            j = s - 1  # back to the first continuation byte (three at most), then the lead byte before it
            while j > s - 3 and j > lo and 0x80 <= buf[j - 1] <= 0xBF:
                j -= 1
            j -= 1
            if j < lo or UTF8_LEAD.get(buf[j]) != s - j:
                break
            s = max(lo, t.rfind(b"\x00", lo, j) + 1)  # and back over the ASCII text before that character
        return s

    def between(self, buf, lane, lo, hi):
        """What lay in buf[lo:hi] for a lane, in the terms above (a UTF-16 lane: code units at its offsets)."""
        if self.utf16:
            for m in LINE_END16.finditer(buf, lo, hi):
                if (self.base + m.start()) % 2 == lane:
                    return 2
            return 0
        if LINE_END.search(buf, lo, hi):
            return 2
        return 1 if LINE_BREAK.search(buf, lo, hi) else 0

    def feed(self, chunk, final=False):
        """The runs complete now, in order: (offset in the data, offset after it, bytes, joined, between), where
        joined means the bytes go on from the run before (one run passed on in parts), and between says what lay
        between the lane's previous run and this one."""
        buf = self.buf + chunk if self.buf else chunk
        n, out, match, base, utf16 = len(buf), [], self.rx.match, self.base, self.utf16
        t = buf.translate(UTF16_MAP if utf16 else ASCII_MAP)
        find, key, add, last, held = t.find, self.key, out.append, self.last, self.held
        pos, keep, s = 0, None, 0
        limit = n + 1 if final else n - self.unit  # a run ending after this may go on in the next chunk
        joined = self.open
        if joined:  # the data starts inside a run passed on in parts
            m = match(buf, 0)
            e, joined = (m.end(), True) if m else (0, False)
        while True:
            if not joined:
                s = find(key, pos)
                if s < 0:
                    break
                if not utf16 and s - 2 >= pos and 0x80 <= buf[s - 1] <= 0xBF:
                    s = self.back(buf, t, s, pos)
                e = match(buf, s).end()
                lane = (base + s) % 2 if utf16 else 0
                lo = max(0, last[lane] - base)
                gap = held[lane] if held[lane] == 2 else max(held[lane], self.between(buf, lane, lo, s))
                if e <= limit:
                    add((base + s, base + e, buf[s:e], False, gap))
                    pos, last[lane], held[lane] = e, base + e, 0
                    continue
            else:
                lane, gap = (base + s) % 2 if utf16 else 0, 0
            if e > limit:
                if n - s <= RUN_CARRY_MAX:
                    keep = s
                else:
                    q = n - RUN_TAIL
                    if self.utf16:
                        q -= (q - s) % 2
                    else:
                        while 0x80 <= buf[q] <= 0xBF:
                            q -= 1
                    add((base + s, base + q, buf[s:q], joined, gap))
                    keep, joined, last[lane], held[lane] = q, True, base + q, 0
                break
            add((base + s, base + e, buf[s:e], joined, gap))
            pos, joined, last[lane], held[lane] = e, False, base + e, 0
        self.open = keep is not None and joined
        if final:
            self.buf, self.base = b"", base + n
            return out
        if keep is None:  # nothing pending: keep what could still start a run
            if self.utf16:
                keep = max(pos, n - len(self.key) - 2)
            else:
                lo = max(pos, n - RUN_CARRY_MAX)
                keep = lo + buf[lo:].translate(HARD_MAP).rfind(b"\x00") + 1
        for lane in range(len(last)):  # what lay between in the bytes let go now
            lo = max(0, last[lane] - base)
            if lo < keep:
                held[lane] = max(held[lane], self.between(buf, lane, lo, keep))
                last[lane] = base + keep
        self.buf, self.base = buf[keep:], base + keep
        return out


def width_groups(markers):
    """[(width, markers)]: the markers grouped by the fewest bytes a match can span."""
    groups = {}
    for m in markers:
        groups.setdefault(max(1, m.width), []).append(m)
    return sorted(groups.items(), key=lambda g: g[0])


def run_least(markers):
    """The ASCII characters in a row a text run needs: no more than any marker's shortest match (so none is lost)
    and no more than the shortest stretch base64 is taken from."""
    return max(1, min([RUN_LEAST_MAX, *(m.width for m in markers)]))


def latin1_runs(runs):
    """UTF-16 runs (little-endian ASCII and Latin-1 code units) as UTF-8 text."""
    return [(o, e, utf8(r[::2].decode("latin-1")), j, g) for o, e, r, j, g in runs]


class Joiner:
    """Joins runs at least `least` bytes long into one view as they come (see above), the shorter ones counted as
    what lay between. start: where the view starts in the data (a run there has nothing before it)."""

    def __init__(self, least, seps=MARKER_SEPS, start=0):
        self.least, self.seps, self.start = least, seps, start
        self.started, self.held, self.last = False, 0, start

    def take(self, runs, final=False, end=None):
        """(the view's next bytes, the runs kept, with what lay between them). end: where the view ends in the
        data (with final): bytes that are not text after the last run give one zero byte."""
        parts, kept, seps, held, least = [], [], self.seps, self.held, self.least
        if runs and not held and min(len(x[2]) for x in runs) >= least:  # every run kept: one join
            text = b"".join([y for x in runs for y in (b"" if x[3] else seps[x[4]], x[2])])
            if not self.started and runs[0][0] == self.start and not runs[0][3]:
                text = text[len(seps[runs[0][4]]) :]
            self.started, self.last = True, runs[-1][1]
            if final and end is not None and self.last < end:
                text += b"\x00"
            return text, runs
        for x in runs:
            off, e, r, joined, gap = x
            if not joined and len(r) < least:
                held = max(held, gap)
                if held < 2 and (b"\n" in r or b"\r" in r):
                    held = 2
                elif not held and (b"\x0b" in r or b"\x0c" in r):
                    held = 1
                continue
            if not joined:
                if held > gap:
                    gap = held
                    x = (off, e, r, joined, gap)
                if self.started or off != self.start:
                    parts.append(seps[gap])
            parts.append(r)
            kept.append(x)
            held, self.started, self.last = 0, True, e
        self.held = held
        if final and self.started and end is not None and self.last < end:
            parts.append(b"\x00")
        return b"".join(parts), kept


def join_runs(runs, least=1, seps=MARKER_SEPS, start=0, end=None):
    """One view of whole data's runs."""
    return Joiner(least, seps, start).take(runs, True, end)[0]


class RunCounter:
    """Counts markers over runs as they come (Runs.feed): each group of markers with the same shortest match sees
    only the runs at least that long, so most of what chance bytes give is skipped."""

    def __init__(self, markers, start=0):
        self.groups = [(Joiner(w, MARKER_SEPS, start), StreamCounter(ms)) for w, ms in width_groups(markers)]

    def feed(self, runs, final=False, end=None):
        held = 0
        for joiner, counter in self.groups:  # shortest first: each group's runs are kept from the one before
            text, runs = joiner.take(runs, final, end)
            joiner.held = held = max(joiner.held, held)  # what the group before still holds lies after these runs
            if text or final:
                counter.feed(text, final)
        return self

    @property
    def counts(self):
        out = {}
        for _joiner, counter in self.groups:
            out.update(counter.counts)
        return out


def utf16_least(markers):
    """The code units a UTF-16 run needs: no more than any marker's shortest match can be made of (a Latin-1 letter
    is two bytes in the UTF-8 the markers see)."""
    return max(1, (min((m.width for m in markers), default=RUN_LEAST_MAX) + 1) // 2)


def run_counts(markers, runs, start=0, end=None):
    """{name: count} over whole data's runs."""
    return RunCounter(markers, start).feed(runs, True, end).counts


def view_end(utf16, lane, size):
    """Where a lane's view of data of this size ends: the bytes, or the last whole code unit at its offsets."""
    return lane + max(0, size - lane) // 2 * 2 if utf16 else size


def check_regex(name, src, flags):
    """Refuses a marker whose time could grow faster than the text it scans, or whose match could be wider than the
    chunk overlap."""
    if not src.isascii():
        raise DataError(f"marker {name!r}: the regex must be ASCII (write other characters as escapes)")
    try:
        tree = _rp.parse(src.encode("ascii"), flags)
        rx = re.compile(src.encode("ascii"), flags)
    except (re.error, OverflowError, RecursionError):
        raise DataError(f"marker {name!r}: the regex does not compile") from None

    def walk(items, product):
        for op, av in items:
            if op in (_rc.GROUPREF, _rc.GROUPREF_EXISTS):
                raise DataError(f"marker {name!r}: backreferences and conditionals are not allowed")
            if op in REPEATS:
                _lo, hi, sub = av
                if hi == _rc.MAXREPEAT or hi > REPEAT_MAX:
                    raise DataError(
                        f"marker {name!r}: every repeat needs an upper bound of at most {REPEAT_MAX} "
                        "(no +, * or {n,})"
                    )
                # product: the bounds of the repeats this one sits inside, multiplied
                if product > 1 and product * max(hi, 1) > REPEAT_PRODUCT_MAX:
                    raise DataError(f"marker {name!r}: nested repeats allow over {REPEAT_PRODUCT_MAX} steps")
                walk(sub, product * max(hi, 1))
            elif op == _rc.SUBPATTERN:
                walk(av[-1], product)
            elif op == _rc.ATOMIC_GROUP:
                walk(av, product)
            elif op == _rc.BRANCH:
                for b in av[1]:
                    walk(b, product)
            elif op in (_rc.ASSERT, _rc.ASSERT_NOT):
                walk(av[1], product)

    walk(tree, 1)
    if tree.getwidth()[1] > OVERLAP:
        raise DataError(f"marker {name!r}: a match could be wider than {OVERLAP} bytes")
    return rx, seeds_of(tree), tree.getwidth()[0]


PROBE_COMMON = b"aA0 \n\t.,;:-_/\\()[]{}\"'=*#x\x00"
CATEGORY_SEEDS = {
    _rc.CATEGORY_DIGIT: b"0",
    _rc.CATEGORY_NOT_DIGIT: b"a",
    _rc.CATEGORY_SPACE: b" \n",
    _rc.CATEGORY_NOT_SPACE: b"a",
    _rc.CATEGORY_WORD: b"a_",
    _rc.CATEGORY_NOT_WORD: b" .",
}


def seeds_of(tree):
    """The characters a marker names (literals, class members and range ends) in order, then common characters:
    the bytes its adversarial test inputs are made of."""
    out = []

    def add(c):
        for x in (c, ord(chr(c).lower()), ord(chr(c).upper())):
            if x < 128 and x not in out:
                out.append(x)

    def walk(items):
        for op, av in items:
            if op in (_rc.LITERAL, _rc.NOT_LITERAL):
                add(av)
            elif op == _rc.IN:
                for o, a in av:
                    if o == _rc.LITERAL:
                        add(a)
                    elif o == _rc.RANGE:
                        for c in (a[0], (a[0] + a[1]) // 2, a[1]):
                            add(c)
                    elif o == _rc.CATEGORY:
                        for c in CATEGORY_SEEDS.get(a, b""):
                            add(c)
            elif op in REPEATS:
                walk(av[2])
            elif op == _rc.SUBPATTERN:
                walk(av[-1])
            elif op == _rc.ATOMIC_GROUP:
                walk(av)
            elif op == _rc.BRANCH:
                for b in av[1]:
                    walk(b)
            elif op in (_rc.ASSERT, _rc.ASSERT_NOT):
                walk(av[1])

    walk(tree)
    for c in PROBE_COMMON:
        add(c)
    return out[:48]


# Runs in a separate Python process, so a marker that never finishes can be stopped. For each marker: "m <i>", then
# "t <i> <seconds of CPU time>" per input: each seed repeated, pairs of the first six seeds alternating, and the seeds
# in order (also as lines).
PROBE_CHILD = r"""
import json, re, sys, time
spec = json.load(sys.stdin)
n = spec["size"]
def inputs(seeds):
    chars = [bytes([c]) for c in seeds]
    for c in chars:
        yield c * n
    for a in chars[:6]:
        for b in chars[:6]:
            if a != b:
                yield (a + b) * (n // 2)
    seq = b"".join(chars)
    yield (seq * (n // len(seq) + 1))[:n]
    yield ((seq + b"\n") * (n // len(seq) + 1))[:n]
for i, m in enumerate(spec["markers"]):
    rx = re.compile(m["src"].encode("ascii"), m["flags"])
    print("m", i, flush=True)
    for data in inputs(m["seeds"]):
        t = time.process_time()
        for _ in rx.finditer(data):
            pass
        print("t", i, time.process_time() - t, flush=True)
print("done", flush=True)
"""
_probed = set()


def probe_markers(specs):
    """Times every marker over adversarial inputs in a child process and refuses (DataError, marker name only) one
    that takes over PROBE_BUDGET seconds of CPU time on any input, or that the child has to be stopped on. specs:
    (name, regex, flags, seeds). Markers that passed before in this process are not timed again."""
    todo = [x for x in specs if (x[1], x[2]) not in _probed]
    if not todo:
        return
    if not sys.executable:
        raise DataError("markers cannot be timed: no Python interpreter to run")
    doc = {"size": PROBE_SIZE, "markers": [{"src": src, "flags": fl, "seeds": sd} for _n, src, fl, sd in todo]}
    p = subprocess.Popen(
        [sys.executable, "-I", "-c", PROBE_CHILD],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        creationflags=NO_WINDOW,
    )
    lines = []
    reader = threading.Thread(target=lambda: lines.extend(iter(p.stdout.readline, b"")) or lines.append(None))
    reader.daemon = True
    try:
        p.stdin.write(json.dumps(doc).encode("ascii"))
        p.stdin.close()
        reader.start()
        current, seen, last = None, 0, time.monotonic()
        while True:
            while seen < len(lines):
                line = lines[seen]
                seen += 1
                last = time.monotonic()
                if line is None:
                    raise DataError("markers could not be timed (the timing process stopped)")
                f = line.split()
                if f[:1] == [b"m"]:
                    current = int(f[1])
                elif f[:1] == [b"t"] and float(f[2]) > PROBE_BUDGET:
                    raise DataError(f"marker {todo[int(f[1])][0]!r}: too slow on an adversarial test input")
                elif f[:1] == [b"done"]:
                    _probed.update((x[1], x[2]) for x in todo)
                    return
            if time.monotonic() - last > PROBE_BUDGET * 20:  # wall clock: generous, for a busy machine
                if current is None:
                    raise DataError("markers could not be timed (the timing process did not start)")
                raise DataError(f"marker {todo[current][0]!r}: too slow on an adversarial test input")
            time.sleep(0.01)
    finally:
        p.kill()
        p.wait()
        p.stdout.close()


def load_markers(data):
    try:
        doc = json.loads(data.decode("utf-8-sig"))
    except (UnicodeDecodeError, ValueError) as e:
        raise DataError(f"markers file is not valid JSON ({e.__class__.__name__})") from None
    items = doc.get("markers") if isinstance(doc, dict) else None
    if not isinstance(items, list) or not items:
        raise DataError("markers file holds no markers")
    out, seen, specs = [], set(), []
    for i, m in enumerate(items):
        name = m.get("name") if isinstance(m, dict) else None
        if not isinstance(name, str) or not name.strip() or name in seen:
            raise DataError(f"marker {i + 1}: missing or repeated name")
        seen.add(name)
        kind, src, flags = m.get("kind", "pattern"), m.get("regex"), m.get("flags", [])
        min_lines = m.get("min_lines", 1)
        if kind not in ("pattern", "listing"):
            raise DataError(f"marker {name!r}: unknown kind")
        if not isinstance(src, str) or not src:
            raise DataError(f"marker {name!r}: no regex")
        if not isinstance(flags, list) or any(f not in FLAGS for f in flags):
            raise DataError(f"marker {name!r}: unknown flag")
        if not isinstance(min_lines, int) or isinstance(min_lines, bool) or min_lines < 1:
            raise DataError(f"marker {name!r}: min_lines must be a positive whole number")
        fl = 0
        for f in flags:
            fl |= FLAGS[f]
        if kind == "listing":
            fl |= re.MULTILINE
        rx, seeds, width = check_regex(name, src, fl)
        out.append(Marker(name, kind, rx, min_lines, max(1, width)))
        specs.append((name, src, fl, seeds))
    probe_markers(specs)
    return out


def load_hash_list(data, what):
    try:
        words = data.decode("ascii").split()
    except UnicodeDecodeError:
        raise DataError(f"{what} file is not ASCII text") from None
    if not words:
        raise DataError(f"{what} file is empty")
    size = {"hashes": 64, "shingles": 16}[what]
    for i, w in enumerate(words, 1):
        if len(w) != size or not HEX_TEXT.fullmatch(w.encode("ascii")):
            raise DataError(f"{what} file: entry {i} is not {size} hex digits")
    return {w.lower() for w in words}


def read_allow(data):
    """The allow list document and its entries by hash; any malformed, repeated or pathless entry stops the scan."""
    try:
        doc = json.loads(data.decode("utf-8-sig"))
    except (UnicodeDecodeError, ValueError) as e:
        raise DataError(f"allow list is not valid JSON ({e.__class__.__name__})") from None
    items = doc.get("entries") if isinstance(doc, dict) else doc
    if not isinstance(items, list):
        raise DataError("allow list holds no entries list")
    out = {}
    for i, e in enumerate(items):
        if not isinstance(e, dict):
            raise DataError(f"allow entry {i + 1}: not an object")
        sha, path, reason, msha = e.get("sha256"), e.get("path"), e.get("reason"), e.get("markers_sha256")
        if not isinstance(sha, str) or not SHA256.fullmatch(sha.lower()):
            raise DataError(f"allow entry {i + 1}: sha256 must be 64 hex digits")
        if sha.lower() in out:
            raise DataError(f"allow entry {i + 1}: sha256 {sha.lower()} is listed twice")
        if not isinstance(path, str) or not path.strip():
            raise DataError(f"allow entry {i + 1}: needs a path")
        if not isinstance(reason, str) or not reason.strip():
            raise DataError(f"allow entry {i + 1}: needs a reason")
        if msha not in (None, "") and not (isinstance(msha, str) and SHA256.fullmatch(msha.lower())):
            raise DataError(f"allow entry {i + 1}: markers_sha256 must be 64 hex digits, empty or null")
        out[sha.lower()] = {"path": path, "reason": reason, "markers_sha256": (msha or "").lower()}
    return doc, items, out


def load_allow(data):
    return read_allow(data)[2]


def renew_allow(allow_path, markers_path, shas):
    """Stamps the current markers hash on the named allow entries only (after a human re-review). Returns the count."""
    try:
        raw = Path(allow_path).read_bytes()
        msha = hashlib.sha256(Path(markers_path).read_bytes()).hexdigest()
    except OSError as e:
        raise DataError(f"cannot read the allow list or markers file: {e.__class__.__name__}") from None
    doc, items, entries = read_allow(raw)
    wanted = [s.lower() for s in shas]
    missing = [s for s in wanted if s not in entries]
    if missing:
        raise DataError(f"not on the allow list: {', '.join(missing)}")
    for e in items:
        if e["sha256"].lower() in wanted:
            e["markers_sha256"] = msha
    write_atomic(allow_path, json.dumps(doc, indent=1, ensure_ascii=False) + "\n")
    return len(set(wanted)), msha


def write_atomic(path, text):
    """Writes a file whole or not at all: a temporary file beside it, then a rename."""
    path = os.path.abspath(path)
    fd, tmp = tempfile.mkstemp(prefix=".scan-", dir=os.path.dirname(path))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", errors="backslashreplace", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


# ---------- text ----------


def norm_line(line):
    """Whitespace-insensitive form of a line; blank and trivial lines become empty."""
    s = WS.sub(" ", line).strip()
    return "" if len(s) < 4 or s in TRIVIAL else s


def shingles(text):
    """Short hashes of every window of 5 meaningful lines (the shingle list is built the same way)."""
    lines = [n for n in (norm_line(x) for x in text.splitlines()) if n]
    for i in range(len(lines) - 4):
        yield hashlib.sha1("\n".join(lines[i : i + 5]).encode("utf-8", "surrogatepass")).hexdigest()[:16]


def utf8(text):
    return text.encode("utf-8", "surrogatepass")


def printable_share(text):
    if not text:
        return 0.0
    return sum(1 for c in text if c.isprintable() or c in "\r\n\t") / len(text)


def detect_encoding(data):
    """The encoding the bytes' text is in, or None for binary data."""
    h = data[:4096]
    if h[:4] in (b"\xff\xfe\x00\x00", b"\x00\x00\xfe\xff"):
        return "utf-32"
    if h[:3] == b"\xef\xbb\xbf":
        return "utf-8-sig"
    if h[:2] in (b"\xff\xfe", b"\xfe\xff"):
        return "utf-16"
    n = len(h) - len(h) % 4
    if n >= 8:
        per = n // 4
        z = [h[i:n:4].count(0) for i in range(4)]
        if z[2] == per and z[3] == per and z[1] >= per * 0.9 and z[0] < per * 0.5:
            return "utf-32-le"
        if z[0] == per and z[1] == per and z[2] >= per * 0.9 and z[3] < per * 0.5:
            return "utf-32-be"
    n2 = len(h) - len(h) % 2
    if n2 >= 8:
        half = n2 // 2
        odd, even = h[1:n2:2].count(0), h[0:n2:2].count(0)
        if odd >= half * 0.9 and even <= half * 0.1:
            return "utf-16-le"
        if even >= half * 0.9 and odd <= half * 0.1:
            return "utf-16-be"
        if n2 >= 64 and max(odd, even) > n2 // 4 and min(odd, even) < n2 // 64:
            return "utf-16-le" if odd > even else "utf-16-be"
    if b"\0" not in h:
        try:
            codecs.getincrementaldecoder("utf-8")().decode(h, final=False)
            return "utf-8"
        except UnicodeDecodeError:
            pass
    # UTF-16 text with few zero bytes, such as CJK. Pairs of ASCII bytes also decode to CJK letters, so the bytes
    # must not be mostly ASCII.
    if n2 >= 4 and sum(1 for b in h[:n2] if b >= 0x80) >= n2 // 5:
        for enc in ("utf-16-le", "utf-16-be"):
            try:
                if printable_share(h[:n2].decode(enc)) >= 0.9:
                    return enc
            except UnicodeDecodeError:
                continue
    return None if b"\0" in h else "utf-8"


def unescape_json(text):
    """JSON string escapes decoded in place, for text that is not valid JSON as a whole."""

    def sub(m):
        return chr(int(m.group(1), 16)) if m.group(1) else JSON_ESCAPES[m.group(2)]

    out = JSON_ESCAPE.sub(sub, text)
    return out.encode("utf-16", "surrogatepass").decode("utf-16", "replace")  # joins escaped surrogate pairs


class JsonResult:
    def __init__(self):
        self.strings, self.complete, self.gaps = [], True, set()


def json_strings(text):
    """The strings inside a JSON or JSONL document (keys too, and JSON kept inside strings), walked without
    recursion. complete is False when some part could not be parsed."""
    res = JsonResult()
    docs = []
    whole = len(text) <= JSON_MAX
    if not whole:
        res.gaps.add(f"JSON over {JSON_MAX // 1_000_000} MB: not parsed as one document (JSONL lines still are)")
    try:
        if not whole:
            raise ValueError
        docs.append((json.loads(text), 0))
    except RecursionError:
        res.gaps.add("JSON too deep to parse")
        res.complete = False
    except ValueError:
        any_line = False
        for line in text.splitlines():  # JSONL: one document per line
            line = line.strip()
            if line[:1] not in ("{", "["):
                continue
            any_line = True
            if len(line) > JSON_MAX:
                res.gaps.add(f"a JSONL line over {JSON_MAX // 1_000_000} MB: not parsed")
                res.complete = False
                continue
            try:
                docs.append((json.loads(line), 0))
            except RecursionError:
                res.gaps.add("JSON too deep to parse")
                res.complete = False
            except ValueError:
                res.complete = False
        if not any_line:
            res.complete = False
    stack = docs[::-1]
    while stack:
        obj, level = stack.pop()
        if isinstance(obj, str):
            res.strings.append(obj)
            s = obj.lstrip()
            if s[:1] in ("{", "[") and len(s) > 1:  # JSON kept inside a string, e.g. a tool's input
                try:
                    inner = json.loads(s)
                except RecursionError:
                    res.gaps.add("JSON too deep to parse")
                    continue
                except ValueError:
                    continue
                if level + 1 > JSON_DEPTH:
                    res.gaps.add(f"JSON kept inside JSON strings over {JSON_DEPTH} levels deep: not decoded")
                    continue
                stack.append((inner, level + 1))
        elif isinstance(obj, dict):
            for k, v in obj.items():
                res.strings.append(k)
                stack.append((v, level))
        elif isinstance(obj, list):
            stack.extend((v, level) for v in reversed(obj))
    return res


def base64_runs(text_bytes):
    """Base64 text in a view (one line, or wrapped over lines as MIME does): each run's base64 text."""
    out, cur, end, last = [], [], -1, None
    for m in B64_RUN.finditer(text_bytes):
        s, piece = m.start(), m.group()
        gap = text_bytes[end:s] if end >= 0 else None
        wraps = gap == b"" or (gap is not None and len(gap) <= 96 and B64_WRAP.fullmatch(gap))
        joins = cur and wraps and not last.endswith(b"=") and len(last) % 4 == 0
        if not joins:
            if cur:
                out.append(b"".join(cur))
            cur = []
        cur.append(piece)
        end, last = m.end(), piece
    if cur:
        out.append(b"".join(cur))
    return [r for r in out if len(r) >= B64_MIN and not HEX_TEXT.fullmatch(r)]


# ---------- file types ----------


def sniff(h, size):
    """What a file holds, from its first bytes (never its name)."""
    if h[:8] == b"\x89PNG\r\n\x1a\n":
        return "png"
    if h[:3] == b"\xff\xd8\xff":
        return "jpeg"
    if h[:6] in (b"GIF87a", b"GIF89a"):
        return "image"
    if h[:2] == b"BM" and len(h) >= 14 and int.from_bytes(h[2:6], "little") == size:
        return "image"
    if h[:4] == b"RIFF" and h[8:12] == b"WEBP":
        return "image"
    if h[4:8] == b"ftyp":
        brand = h[8:12]
        return "image" if brand in (b"avif", b"avis", b"heic", b"heix", b"mif1", b"msf1") else "media"
    if h[:4] in (b"\x1a\x45\xdf\xa3", b"OggS", b"fLaC") or h[:3] == b"ID3" or h[:4] == b"\x30\x26\xb2\x75":
        return "media"
    if h[:4] == b"RIFF" and h[8:12] in (b"AVI ", b"WAVE"):
        return "media"
    if h[:8] == b"MComprHD" or h[:4] in (b"CISO", b"ZISO"):
        return "disc"
    for off in (0x8001, 0x9311, 0x9319):  # ISO 9660 volume descriptor: 2048-byte sectors, raw 2352 (mode 1 / 2)
        if h[off : off + 5] == b"CD001":
            return "disc"
    if h[:4] in (b"PK\x03\x04", b"PK\x05\x06", b"PK\x07\x08"):
        return "zip"
    if h[:2] == b"\x1f\x8b":
        return "gzip"
    if h[:3] == b"BZh" and h[4:10] == b"\x31\x41\x59\x26\x53\x59":
        return "bzip2"
    if h[:6] == b"\xfd7zXZ\x00":
        return "xz"
    if len(h) >= 2 and h[0] == 0x78 and h[1] in (0x01, 0x5E, 0x9C, 0xDA) and (h[0] << 8 | h[1]) % 31 == 0:
        return "zlib"
    if h[:6] == b"7z\xbc\xaf\x27\x1c":
        return "7z"
    if h[:6] == b"Rar!\x1a\x07":
        return "rar"
    if h[:2] == b"\x1f\x9d":
        return "compress (.Z)"
    if h[:4] == b"LZIP":
        return "lzip"
    if h[:9] == b"\x89LZO\x00\r\n\x1a\n":
        return "lzop"
    if h[:4] == b"\xed\xab\xee\xdb":
        return "rpm"
    if h[:4] in (b"hsqs", b"sqsh"):
        return "squashfs"
    if h[:2] == b"\x60\xea":
        return "arj"
    if h[:4] == b"\x28\xb5\x2f\xfd":
        return "zstd"
    if h[:4] == b"\x04\x22\x4d\x18":
        return "lz4"
    if h[:4] == b"MSCF":
        return "cab" if cab_uncompressed(h) else "cab-compressed"
    if h[:8] == b"!<arch>\n":
        return "ar"
    if h[:6] in (b"070701", b"070702", b"070707") or h[:2] in (b"\xc7\x71", b"\x71\xc7"):
        return "cpio"
    if h[:8] == b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1":
        return "cfb"
    if h[:16] == b"SQLite format 3\x00":
        return "sqlite"
    if h[:4] == b"PACK" and h[4:8] in (b"\x00\x00\x00\x02", b"\x00\x00\x00\x03"):
        return "pack"
    if h[:16] in (b"# v2 git bundle\n", b"# v3 git bundle\n"):
        return "bundle"
    if h[257:262] == b"ustar":
        return "tar"
    if b"%PDF-" in h[:1024]:
        return "pdf"
    return "other"


def real_header(what, data, at):
    """True when the bytes at a signature found inside another file hold a well-formed header of that format, so a
    chance signature (a constant in a program, say) is not taken for an archive."""
    h = bytes(data[at : at + 64])
    if what == "7z":  # the start header's CRC
        return len(h) >= 32 and zlib.crc32(h[12:32]) == int.from_bytes(h[8:12], "little")
    if what == "RAR":
        if h[6:8] == b"\x01\x00":  # RAR 5: the main header's CRC over its size and body
            size, n = 0, 0
            for i, b in enumerate(h[12:16]):
                size |= (b & 0x7F) << (7 * i)
                n = i + 1
                if not b & 0x80:
                    break
            body = bytes(data[at + 12 : at + 12 + n + size])
            return 0 < size < 1 << 16 and zlib.crc32(body) == int.from_bytes(h[8:12], "little")
        if h[6:7] == b"\x00":  # RAR 4: the archive header's 16-bit CRC
            size = int.from_bytes(h[12:14], "little")
            body = bytes(data[at + 9 : at + 7 + size])
            return h[9] == 0x73 and size >= 7 and (zlib.crc32(body) & 0xFFFF) == int.from_bytes(h[7:9], "little")
        return False
    if what == "zstd":  # frame header descriptor's reserved and unused bits clear, a first block of a known type
        if len(h) < 18:
            return False
        fhd = h[4]
        if fhd & 0x18:
            return False
        pos = 5 + (0 if fhd & 0x20 else 1) + (0, 1, 2, 4)[fhd & 3]
        fcs = fhd >> 6
        pos += (1 if fhd & 0x20 else 0, 2, 4, 8)[fcs] if fcs or fhd & 0x20 else 0
        block = int.from_bytes(h[pos : pos + 3], "little")
        return (block >> 1) & 3 != 3 and block >> 3 <= 128 * 1024
    if what == "zip":  # a local file header with a known method and a printable name
        if len(h) < 30 or h[5] != 0 or h[4] > 63:
            return False
        method, name_len = int.from_bytes(h[8:10], "little"), int.from_bytes(h[26:28], "little")
        name = bytes(data[at + 30 : at + 30 + name_len])
        known = method in (0, 1, 6, 8, 9, 12, 14, 93, 95, 98, 99)
        return known and 0 < name_len <= 1024 and len(name) == name_len and all(32 <= b < 127 or b >= 128 for b in name)
    return True


def looks_real(used, out):
    """True when a damaged or cut-short stream found by its signature looks like real compressed data, not chance
    bytes: it used FOUND_MIN input bytes or more (chance bytes fail within a few dozen, though back-references can
    still give hundreds of output bytes), and its output is not random (a chance stored block copies random bytes)."""
    if used < FOUND_MIN or not out:
        return False
    sample = bytes(out[:65_536])
    return len(zlib.compress(sample, 1)) < len(sample) * 0.95


def run_length_decode(data):
    out, i = bytearray(), 0
    while i < len(data) and len(out) <= MEMBER_MAX:
        n = data[i]
        if n == 128:
            break
        if n < 128:
            out += data[i + 1 : i + 2 + n]
            i += n + 2
        else:
            out += data[i + 1 : i + 2] * (257 - n)
            i += 2
    return bytes(out)


def pdf_decode(data, filters):
    """A PDF stream through its filters, in order: (bytes, why), where why (a gap) is None or says the Flate data
    ends early; (None, why) for an image filter (the raw stream is the content), or with why when a filter is
    unknown, unsupported (LZW) or fails."""
    why = None
    for name in filters:
        try:
            if name in ("FlateDecode", "Fl"):
                d = zlib.decompressobj()
                data = d.decompress(data, MEMBER_MAX + 1)
                if not d.eof and len(data) <= MEMBER_MAX:
                    why = "PDF stream (FlateDecode) ends early"
            elif name in ("ASCIIHexDecode", "AHx"):
                hexes = bytes(c for c in data.split(b">", 1)[0] if c not in b" \t\r\n\f\x00")
                data = binascii.unhexlify(hexes + b"0" * (len(hexes) % 2))
            elif name in ("ASCII85Decode", "A85"):
                body = data.split(b"~>", 1)[0].strip()
                data = base64.a85decode(body.removeprefix(b"<~"), adobe=False, ignorechars=b" \t\r\n\f\x00")
            elif name in ("RunLengthDecode", "RL"):
                data = run_length_decode(data)
            elif name in PDF_IMAGE_FILTERS:
                return None, why
            else:
                return None, f"PDF stream with the {name} filter: not decoded"
        except (zlib.error, binascii.Error, ValueError) as e:
            return None, f"PDF stream could not be decoded ({name}: {e.__class__.__name__})"
    return data, why


def pdf_dict(head):
    """The dictionary that ends just before a stream keyword (its << ... >> found by balancing the brackets back
    from the end), or None when it does not start within head."""
    depth = 0
    for m in reversed(list(PDF_BRACKETS.finditer(head))):
        depth += 1 if m.group() == b">>" else -1
        if depth == 0:
            return head[m.start() :]
    return None


def cab_uncompressed(h):
    """True when every folder of a cabinet is stored without compression."""
    try:
        folders, flags = int.from_bytes(h[26:28], "little"), int.from_bytes(h[30:32], "little")
        off = 36 + (4 + int.from_bytes(h[36:38], "little") if flags & 4 else 0)
        folder_res = h[38] if flags & 4 else 0
        for i in range(folders):
            at = off + i * (8 + folder_res)
            if at + 8 > len(h) or int.from_bytes(h[at + 6 : at + 8], "little") & 0x0F:
                return False
        return folders > 0
    except IndexError:
        return False


def eocd_zip(data):
    """True when a zip's end record sits near the end (a zip after other data: an image, an installer)."""
    return data.rfind(b"PK\x05\x06", max(0, len(data) - 65_557)) >= 0


# ---------- paths ----------


def long_path(p):
    """A path string Windows accepts beyond 260 characters."""
    s = os.path.abspath(str(p))
    if os.name != "nt" or s.startswith("\\\\?\\"):
        return s
    return "\\\\?\\UNC\\" + s[2:] if s.startswith("\\\\") else "\\\\?\\" + s


def shown_path(p):
    s = str(p)
    if s.startswith("\\\\?\\UNC\\"):
        return "\\\\" + s[8:]
    return s.removeprefix("\\\\?\\")


def parts_of(p):
    """A path's components, case-folded the way the file system compares them."""
    s = os.path.normcase(shown_path(p)).replace("\\", "/")
    return tuple(x for x in s.split("/") if x)


def same_file_key(path):
    """The same key for every name of one file (short 8.3 names, links, case): its device and file number."""
    try:
        st = os.stat(path)
        if st.st_ino:
            return (st.st_dev, st.st_ino)
    except OSError:
        pass
    return os.path.normcase(os.path.abspath(shown_path(path)))


def is_link(path):
    """A symlink or junction (any name-surrogate reparse point on Windows)."""
    try:
        st = os.lstat(path)
    except OSError:
        return False
    if stat.S_ISLNK(st.st_mode):
        return True
    return bool(getattr(st, "st_reparse_tag", 0) & 0x20000000)  # the name-surrogate bit: junctions, symlinks


def is_git_dir(path):
    return (
        os.path.isfile(os.path.join(path, "HEAD"))
        and os.path.isdir(os.path.join(path, "objects"))
        and os.path.isdir(os.path.join(path, "refs"))
    )


def under(path, prefix):
    p, e = os.path.normcase(shown_path(path)), os.path.normcase(shown_path(prefix).rstrip("\\/"))
    return p == e or p.startswith((e + os.sep, e + "/", e + "!", e + ":"))


_streams_api = None


def named_streams(path):
    """The names of a file's or folder's NTFS alternate data streams (Windows only; [] elsewhere)."""
    global _streams_api
    if os.name != "nt":
        return []
    import ctypes
    from ctypes import wintypes

    if _streams_api is None:

        class FindStreamData(ctypes.Structure):
            _fields_ = (("size", ctypes.c_longlong), ("name", ctypes.c_wchar * 296))

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        first, nxt, close = k32.FindFirstStreamW, k32.FindNextStreamW, k32.FindClose
        first.argtypes = (wintypes.LPCWSTR, ctypes.c_int, ctypes.POINTER(FindStreamData), wintypes.DWORD)
        first.restype = wintypes.HANDLE
        nxt.argtypes = (wintypes.HANDLE, ctypes.POINTER(FindStreamData))
        nxt.restype = wintypes.BOOL
        close.argtypes = (wintypes.HANDLE,)
        _streams_api = (FindStreamData, first, nxt, close)
    data_type, first, nxt, close = _streams_api
    found = data_type()
    h = first(path, 0, ctypes.byref(found), 0)
    if h in (None, ctypes.c_void_p(-1).value):
        return []
    names = []
    try:
        while True:
            name = found.name  # ":name:$DATA"; the file's own data is "::$DATA"
            if name.endswith(":$DATA") and name != "::$DATA":
                names.append(name[1:-6])
            if not nxt(h, ctypes.byref(found)):
                break
    finally:
        close(h)
    return names


# ---------- git ----------


def git_env():
    """The caller's environment without any GIT_ variable (which could point git at other objects), plus settings
    that keep git from replacing objects, fetching, prompting or reading the user's configuration."""
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(
        GIT_NO_REPLACE_OBJECTS="1",
        GIT_NO_LAZY_FETCH="1",
        GIT_TERMINAL_PROMPT="0",
        GIT_OPTIONAL_LOCKS="0",
        GIT_CONFIG_NOSYSTEM="1",
        GIT_CONFIG_GLOBAL=os.devnull,
    )
    return env


def find_git(avoid=()):
    """git's absolute path from the PATH entries only (see find_program). None when there is none."""
    return find_program("git", avoid)


def find_program(name, avoid=(), windows=(".exe",)):
    """A program's absolute path from the PATH entries only: never the current folder (which Windows would search
    first), an empty or relative entry, or a folder under one of the avoided folders (the scan roots, which may hold
    a planted git). On Windows the name takes each of the `windows` extensions in turn, in each PATH entry (npx is
    npx.cmd there). None when there is none."""
    names = tuple(f"{name}{ext}" for ext in windows) if os.name == "nt" else (name,)
    here = same_file_key(os.getcwd())
    for entry in os.environ.get("PATH", "").split(os.pathsep):
        entry = entry.strip().strip('"')
        if not entry or not os.path.isabs(entry) or same_file_key(entry) == here:
            continue
        real = os.path.realpath(entry)
        if any(under(real, a) or under(entry, a) for a in avoid):
            continue
        for n in names:
            cand = os.path.join(real, n)
            if os.path.isfile(cand) and os.access(cand, os.X_OK):
                return cand
    return None


def git_run(cmd, **kw):
    """subprocess.run for git with GIT_TIMEOUT; a timeout raises GitError."""
    try:
        return subprocess.run(
            cmd, env=git_env(), creationflags=NO_WINDOW, capture_output=True, check=False, timeout=GIT_TIMEOUT, **kw
        )
    except subprocess.TimeoutExpired:
        raise GitError(f"git took over {GIT_TIMEOUT} s and was stopped") from None


def git_version(git):
    """git's version as a tuple of numbers; (0,) when it cannot be read."""
    try:
        r = subprocess.run(
            [git, "--version"], env=git_env(), creationflags=NO_WINDOW, capture_output=True, timeout=60, check=False
        )
        out = r.stdout.decode("ascii", "replace")
    except (subprocess.TimeoutExpired, OSError):
        return (0,)
    m = re.search(r"(\d+)\.(\d+)", out)
    return (int(m.group(1)), int(m.group(2))) if m else (0,)


def git_base(git, gitdir):
    return [git, "--no-replace-objects", "-c", "safe.directory=*", "-c", "core.fsmonitor=false", f"--git-dir={gitdir}"]


NO_WINDOW = getattr(subprocess, "CREATE_NO_WINDOW", 0)


def git_objects(git, gitdir, skip=frozenset()):
    """(id, size, body or None when over MEMBER_MAX) for every object in a git folder but the skipped ids, and
    finally the number listed. Raises GitError when git cannot list or read them."""
    base, env = [*git_base(git, gitdir), "cat-file"], git_env()
    r = git_run([*base, "--batch-all-objects", "--batch-check=%(objectname)", "--unordered"])
    if r.returncode:
        raise GitError("git cat-file could not list the objects")
    listed = r.stdout.decode("ascii", "replace").split()
    wanted = [s for s in listed if s not in skip]
    read = 0
    with tempfile.TemporaryFile() as ids:
        ids.write(("\n".join(wanted) + "\n").encode("ascii"))
        ids.seek(0)
        p = subprocess.Popen(
            [*base, "--batch"],
            stdin=ids,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env=env,
            creationflags=NO_WINDOW,
        )
        timer = threading.Timer(GIT_TIMEOUT, p.kill)  # a stopped git ends the output early: a gap below
        timer.daemon = True
        timer.start()
        try:
            for line in iter(p.stdout.readline, b""):
                head = line.split()
                if len(head) != 3 or not head[2].isdigit():
                    continue  # "<id> missing"
                sha, size = head[0].decode("ascii", "replace"), int(head[2])
                if size > MEMBER_MAX:
                    left = size + 1
                    while left > 0:
                        got = p.stdout.read(min(left, CHUNK))
                        if not got:
                            break
                        left -= len(got)
                    read += 1
                    yield sha, size, None
                    continue
                body = p.stdout.read(size)
                p.stdout.read(1)  # the LF after each object
                read += 1
                yield sha, size, body
        finally:
            timed_out = not timer.is_alive()
            timer.cancel()
            p.kill()
            p.wait()
            p.stdout.close()
    if read < len(wanted):
        why = f" (took over {GIT_TIMEOUT} s and was stopped)" if timed_out else ""
        raise GitError(f"git read {read} of {len(wanted)} objects{why}")
    if not listed:
        raise GitError("packs present but git listed no objects")


# ---------- the scan ----------


@dataclass
class Budget:
    """Limits for everything unpacked from one top-level file (or one packed git object). One worker handles a
    top-level file whole, so the limits are the same whatever the number of jobs."""

    shown: str
    bytes: int = 0
    members: int = 0
    noted: set = field(default_factory=set)
    seen_b64: set = field(default_factory=set)


@dataclass
class Found:
    """What one stretch of the scan found, in the order found: the whole scan's results, what one file or git object
    gave a worker, or what the walk noted between two files. Sent between processes, so plain data only."""

    hits: list = field(default_factory=list)
    gaps: list = field(default_factory=list)
    allowed: list = field(default_factory=list)
    skipped: list = field(default_factory=list)
    notes: list = field(default_factory=list)
    discs: list = field(default_factory=list)
    image_paths: list = field(default_factory=list)
    images: dict = field(default_factory=dict)  # root: images under it
    root_files: dict = field(default_factory=dict)  # root: files read under it
    files: int = 0
    bytes: int = 0

    def empty(self):
        return not (self.hits or self.gaps or self.allowed or self.skipped or self.notes or self.discs) and not (
            self.image_paths or self.images or self.root_files or self.files
        )

    def add(self, other):
        for name in ("hits", "gaps", "allowed", "skipped", "notes", "discs", "image_paths"):
            getattr(self, name).extend(getattr(other, name))
        for name in ("images", "root_files"):
            mine = getattr(self, name)
            for k, v in getattr(other, name).items():
                mine[k] = mine.get(k, 0) + v
        self.files += other.files
        self.bytes += other.bytes


class Batch:
    """Tasks handed to a worker at once, and what each gave back (None until known). `known` holds the results of
    tasks settled before the batch is sent again (a task stopped for taking too long); `sent` lists the tasks the
    worker got, by their index here."""

    __slots__ = ("bytes", "future", "id", "known", "pool", "results", "sent", "tasks")

    def __init__(self, bid):
        self.tasks, self.bytes, self.future, self.pool, self.results = [], 0, None, None, None
        self.id, self.known, self.sent = bid, {}, []


ROW = 4  # a worker's row in a Watch: pid, batch id, task index, start (time.monotonic(); 0 between tasks)


class Watch:
    """Which task each worker of one pool is on and since when: one row per worker in shared memory, claimed by the
    worker when it starts and written by it at each task. Plain numbers only, so a worker stopped part way cannot
    leave anything locked that this process waits on."""

    def __init__(self, ctx, workers):
        self.rows = ctx.RawArray("d", workers * ROW)
        self.claim = ctx.Lock()  # held only while a starting worker picks its row

    def running(self):
        """(pid, batch id, task index, start) for each worker in a task now. A row read while its worker rewrites
        it is skipped (its start changed, or was 0, during the read)."""
        out, rows = [], self.rows
        for r in range(0, len(rows), ROW):
            start = rows[r + 3]
            pid, bid, i = rows[r], rows[r + 1], rows[r + 2]
            if start and pid and rows[r + 3] == start:
                out.append((int(pid), int(bid), int(i), start))
        return out


class _QuietCallQueue(cf.process._SafeQueue):
    """The pool's call queue on Python 3.11. When a worker is stopped, 3.11 closes the broken pool's pipe and then
    still queues one stop sentinel per worker; the queue's feeder thread fails to send those and prints a traceback
    ("handle is closed") on standard error. 3.12 and later send no sentinels to a broken pool. A sentinel that cannot
    reach a worker that is already stopped is harmless, so only that failure is dropped; any other failure is handled
    as before. (The feeder hands over what it failed to send already pickled.)"""

    SENTINEL = bytes(multiprocessing.reduction.ForkingPickler.dumps(None))

    def _on_queue_feeder_error(self, e, obj):
        if isinstance(e, OSError) and isinstance(obj, (bytes, bytearray, memoryview)) and bytes(obj) == self.SENTINEL:
            return
        super()._on_queue_feeder_error(e, obj)


class Pool(cf.ProcessPoolExecutor):
    """Worker processes started afresh (spawn), each sent the scan's state once, reporting to the pool's Watch."""

    def __init__(self, state, workers):
        ctx = multiprocessing.get_context("spawn")
        self.watch = Watch(ctx, workers)
        self.stopped = False  # a worker was stopped for taking too long, so the pool is broken
        super().__init__(
            max_workers=workers,
            mp_context=ctx,
            initializer=worker_start,
            initargs=(state, self.watch.rows, self.watch.claim),
        )
        if sys.version_info < (3, 12):
            self._call_queue.__class__ = _QuietCallQueue  # before its feeder thread starts (on the first put)

    def run(self, tasks, bid):
        """A future for WORKER_BATCH on these tasks; on a pool already broken, a future failed with that."""
        try:
            return self.submit(WORKER_BATCH, tasks, bid)
        except cf.process.BrokenProcessPool as e:
            future = cf.Future()
            future.set_exception(e)
            return future

    def shutdown(self, wait=True, *, cancel_futures=False):
        """As the executor's, and a later call with wait=True also waits for the processes when an earlier call did
        not: the executor forgets its manager thread and processes on the first call, waiting or not, so a pool let
        go with wait=False would otherwise still be ending when the scan ends."""
        if getattr(self, "_executor_manager_thread", None) is not None:
            self.left = (self._executor_manager_thread, list((self._processes or {}).values()))
        super().shutdown(wait=wait, cancel_futures=cancel_futures)
        thread, procs = getattr(self, "left", (None, []))
        if wait:
            if thread is not None:
                thread.join()
            for p in procs:
                p.join()
            self.left = (None, [])

    def stop(self, pid):
        """Stops one worker of this pool; False when it is not one (any longer)."""
        proc = (getattr(self, "_processes", None) or {}).get(pid)
        if proc is None or not proc.is_alive():
            return False
        proc.terminate()
        return True


def timeout_gap(task, limit):
    """What a task stopped for taking longer than `limit` seconds gives: its gap."""
    out = Found()
    out.gaps.append((task_shown(task), f"could not be checked: took longer than {limit} s"))
    return out


def task_shown(task):
    """The path a task's failure is reported under."""
    return shown_path(task[1])


class Scan:
    def __init__(self, markers, hashes, shingle_set, allow, markers_sha, opts):
        self.markers, self.hashes, self.shingles, self.allow = markers, hashes, shingle_set, allow
        self.patterns = [m for m in markers if m.kind == "pattern"]
        self.least, self.least16 = run_least(markers), utf16_least(markers)
        self.markers_sha = markers_sha
        self.opts = opts
        self.list_images = opts.get("list_images", False)
        self.excludes = [(x, parts_of(x), os.path.isabs(x)) for x in opts.get("exclude", [])]
        self.skip_files = {same_file_key(p): why for p, why in opts.get("skip_files", {}).items()}
        self.jobs = max(1, min(int(opts.get("jobs", 1)), JOBS_MAX))
        self.limit = max(0, int(opts.get("file_timeout", 0)))  # seconds one task may take; 0: no limit
        self.parallel = self.jobs > 1 or self.limit > 0  # a limit needs a worker to stop, even with one job
        self.found = self.out = Found()  # out: where findings go now (the walk's or a worker's buffer while parallel)
        self.excluded, self.links, self.missing, self.git_dirs = [], [], [], []
        self.root_parts, self.roots_real, self.git_keys = [], [], set()
        self.git_object_count = 0
        self._git, self.git_why = False, ""
        self.pool = self.solo = self.state = None
        self.dying = []  # broken pools, shut down at the end
        self.queue, self.inflight, self.batch = collections.deque(), {}, Batch(0)  # inflight: batch id: batch
        self.inflight_bytes, self.next_bid = 0, 1
        self.where, self.next_line = "", time.monotonic() + PROGRESS_EVERY
        self.next_watch = 0.0
        self.done = {"files": 0, "objects": 0, "bytes": 0, "hits": 0}

    # results so far (all merged once run() returns)
    hits = property(lambda self: self.found.hits)
    gaps = property(lambda self: self.found.gaps)
    allowed = property(lambda self: self.found.allowed)
    skipped = property(lambda self: self.found.skipped)
    notes = property(lambda self: self.found.notes)
    discs = property(lambda self: self.found.discs)
    image_paths = property(lambda self: self.found.image_paths)
    images = property(lambda self: self.found.images)
    root_files = property(lambda self: self.found.root_files)

    @property
    def counts(self):
        return {"files": self.found.files, "git objects": self.git_object_count}

    def worker_state(self):
        """What a worker needs to scan a file as this process would, pickled once here and sent once to each worker
        when it starts."""
        if self.state is None:
            state = (self.markers, self.hashes, self.shingles, self.allow, self.markers_sha, self.opts, self.roots_real)
            self.state = pickle.dumps(state, protocol=pickle.HIGHEST_PROTOCOL)
        return self.state

    def git(self):
        """git's absolute path, found once on PATH outside the current folder and the scan roots; None when there is
        none or it is older than GIT_MIN (self.git_why says which)."""
        if self._git is False:
            self._git = find_git(self.roots_real)
            self.git_why = "git not found on PATH"
            if self._git:
                v = git_version(self._git)
                if v < GIT_MIN:
                    self.git_why = f"git older than {GIT_MIN[0]}.{GIT_MIN[1]} (cannot turn lazy fetching off)"
                    self._git = None
        return self._git

    def gap(self, path, why):
        self.out.gaps.append((shown_path(path), why))

    # ---------- walking ----------

    def exclusion(self, path):
        p = path if isinstance(path, tuple) else parts_of(path)
        for raw, ex, absolute in self.excludes:
            if ex and (p == ex if absolute else p[-len(ex) :] == ex):
                return raw
        return None

    def covered(self, path):
        """True when a path lies under a root and no excluded folder: the scan reaches it. Both sides are compared as
        real paths, so a short (8.3) name or a link names the same folder."""
        p = parts_of(os.path.realpath(shown_path(path)))
        for rp in self.root_parts:
            if p[: len(rp)] == rp:
                return not any(self.exclusion(p[:i]) for i in range(len(rp), len(p) + 1))
        return False

    def set_roots(self, roots_real):
        self.roots_real = list(roots_real)
        self.root_parts = [parts_of(r) for r in self.roots_real]

    def run(self, roots):
        self.set_roots(os.path.realpath(r) for r in roots)
        if self.parallel:
            self.out = Found()
        try:
            for root in roots:
                self.where = root
                lp = long_path(root)
                if not os.path.lexists(lp):
                    self.missing.append(root)
                    self.gap(root, "missing root")
                    continue
                if os.path.isfile(lp):
                    self.file_task(lp, root)
                    continue
                rule = self.exclusion(lp)
                if rule:
                    self.excluded.append((shown_path(lp), rule))
                    self.gap(root, f"root excluded (--exclude {rule}): nothing under it is scanned")
                    continue
                self.name_hits(lp, os.path.basename(os.path.realpath(root)))  # the root folder's own name
                self.walk(lp, root)
                self.after_files(lambda root=root: self.no_files(root))
            for d, root in self.git_dirs:
                self.where = root
                self.packed_objects(d)
            self.finish()
        except BaseException:
            self.stop_pools()
            raise
        return self

    # ---------- handing out the work ----------
    #
    # With one job and no time limit every file is scanned here, as it is reached. Otherwise the walk stays here and
    # hands files (and packed git objects) to worker processes in batches; each file's findings come back as their own
    # Found, and everything is put together in the order the walk reached it, so the report is the same whatever the
    # number of jobs. Findings the walk makes itself (folder names, unreadable folders) wait in self.out between files.
    #
    # Each worker writes which task it is on, and since when, to its pool's Watch. A task running longer than the
    # limit has its worker stopped: the task is a gap, and since stopping a worker breaks its pool, every batch the
    # pool still held is sent again whole to a fresh pool, less the tasks already settled. A batch whose pool broke
    # any other way (a worker crashed) is scanned again one task at a time in a one-worker pool, which the limit
    # applies to as well.

    def file_task(self, path, root, stream=False):
        task = ("file", path, root, stream, False if stream else self.git_pack(path))
        self.dispatch(task, 0, self.parallel and self.big(path))

    def object_task(self, oshown, body, root):
        self.dispatch(("object", oshown, body, root), len(body), len(body) >= BIG_TASK)

    @staticmethod
    def big(path):
        """True for a file of BIG_TASK bytes or more, which a worker is handed alone."""
        try:
            return os.path.getsize(path) >= BIG_TASK
        except OSError:
            return False

    def do(self, task):
        """Scans one file or git object; any failure is a gap and the scan goes on."""
        if task[0] == "file":
            _k, path, root, stream, in_git_pack = task
            self.guarded(path, root, stream, in_git_pack)
        else:
            _k, oshown, body, root = task
            try:
                self.scan_blob(oshown, body, root, Budget(oshown), 1)
            except Exception as e:  # noqa: BLE001
                self.gap(oshown, f"could not be checked: {e.__class__.__name__}")

    def dispatch(self, task, size, alone=False):
        """Scans a task here (no workers), or puts it in the batch being filled. A large task (alone) goes in a batch
        of its own, so no small one waits behind it in a busy worker."""
        if not self.parallel:
            self.do(task)
            self.progress()
            return
        self.seal()
        if alone and self.batch.tasks:
            self.submit()
        self.queue.append(("task", self.batch, len(self.batch.tasks)))
        self.batch.tasks.append(task)
        self.batch.bytes += size
        if alone or len(self.batch.tasks) >= BATCH_TASKS or self.batch.bytes >= BATCH_BYTES:
            self.submit()
        else:
            self.tick()

    def after_files(self, step):
        """Runs step once every file reached so far is merged (with no workers: now)."""
        if not self.parallel:
            step()
            return
        self.seal()
        self.queue.append(("call", step, 0))

    def seal(self):
        """Puts the walk's own findings since the last file in line."""
        if not self.out.empty():
            self.done["hits"] += len(self.out.hits)
            self.queue.append(("found", self.out, 0))
            self.out = Found()

    def submit(self):
        b, self.batch = self.batch, Batch(self.next_bid)
        self.next_bid += 1
        if not b.tasks:
            return
        self.send(b)
        self.pump(self.jobs * INFLIGHT_PER_JOB)

    def send(self, b):
        """Hands a batch's unsettled tasks to the pool (a fresh one if there is none)."""
        if self.pool is None:
            self.pool = Pool(self.worker_state(), self.jobs)
        b.sent = [i for i in range(len(b.tasks)) if i not in b.known]
        b.pool = self.pool
        b.future = b.pool.run([b.tasks[i] for i in b.sent], b.id)
        self.inflight[b.id] = b
        self.inflight_bytes += b.bytes

    def pump(self, most):
        """Merges what is ready, in order; waits while over `most` batches (or INFLIGHT_BYTES) are in flight."""
        while True:
            for b in [b for b in self.inflight.values() if b.future.done()]:
                self.collect(b)
            self.merge_ready()
            self.tick()
            if len(self.inflight) <= most and (self.inflight_bytes <= INFLIGHT_BYTES or not self.inflight):
                return
            cf.wait(
                [b.future for b in self.inflight.values()],
                timeout=max(0.1, min(WATCH_EVERY, self.next_line - time.monotonic())),
                return_when=cf.FIRST_COMPLETED,
            )

    def collect(self, b):
        """A finished batch's results. A batch whose pool was broken by stopping a task that took too long is sent
        again, less what is settled. A batch that failed any other way (a worker stopped, or something no file check
        catches) is scanned again one task at a time in a worker of its own, so one bad file cannot take the others
        with it; a task that fails again is a gap, as it would be with one job."""
        del self.inflight[b.id]
        self.inflight_bytes -= b.bytes
        try:
            got = b.future.result()
        except Exception as e:  # noqa: BLE001 -- every task is tried again
            broken = isinstance(e, cf.process.BrokenProcessPool)
            if broken and b.pool is self.pool:
                self.retire(self.pool)
                self.pool = None
            if b.pool.stopped:  # broken, or cancelled before it started, by a stop: nothing here is to blame
                if len(b.known) < len(b.tasks):
                    self.send(b)
                    return
                got, b.sent = [], []  # every task is settled (a batch of one stopped task): nothing came back
            else:
                got = [self.alone(b.tasks[i], b.id) for i in b.sent]
        results = dict(b.known)  # a task stopped just as it finished keeps what it found
        results.update(zip(b.sent, got, strict=True))
        b.results = [results[i] for i in range(len(b.tasks))]
        for t, r in zip(b.tasks, b.results, strict=True):
            self.done["files"] += r.files
            self.done["bytes"] += r.bytes
            self.done["hits"] += len(r.hits)
            self.done["objects"] += t[0] == "object"
        b.future = b.pool = None
        b.known, b.sent = {}, []

    def alone(self, task, bid):
        """One task in the one-worker pool, stopped (a gap) if it takes longer than the limit."""
        if self.solo is None:
            self.solo = Pool(self.worker_state(), 1)
        solo, late = self.solo, False
        future = solo.run([task], bid)
        while not late and not cf.wait([future], timeout=WATCH_EVERY).done:
            self.tick()
            now = time.monotonic()
            for pid, _bid, _i, start in solo.watch.running():
                if self.limit and now - start > self.limit and solo.stop(pid):
                    late = True
        if late:
            cf.wait([future], timeout=PROGRESS_EVERY)  # the stopped worker breaks the pool: the future fails at once
        try:
            if not future.done():
                raise cf.process.BrokenProcessPool("the stopped worker did not end")
            return future.result()[0]
        except Exception as e:  # noqa: BLE001 -- reported as the file's gap
            if isinstance(e, cf.process.BrokenProcessPool):
                self.retire(solo)
                self.solo = None
            if late:
                return timeout_gap(task, self.limit)
            out = Found()
            out.gaps.append((task_shown(task), f"could not be checked: {e.__class__.__name__}"))
            return out

    def tick(self):
        """While waiting: stops workers over the time limit and prints the progress line, each when due."""
        self.stop_late()
        self.progress()

    def stop_late(self):
        """Stops each worker of the pool whose task has run longer than the limit; the task is a gap, and the pool,
        broken now, is replaced (its batches are sent again as they fail)."""
        now = time.monotonic()
        pool = self.pool
        if not self.limit or now < self.next_watch or pool is None:
            return
        self.next_watch = now + WATCH_EVERY
        for pid, bid, i, start in pool.watch.running():
            b = self.inflight.get(bid)
            if now - start <= self.limit or b is None or b.pool is not pool or not 0 <= i < len(b.sent):
                continue
            if pool.stop(pid):
                b.known[b.sent[i]] = timeout_gap(b.tasks[b.sent[i]], self.limit)
                pool.stopped = True
        if pool.stopped:
            self.retire(pool)
            self.pool = None

    def retire(self, pool):
        """Lets a broken pool go: no new work, its processes waited for at the end."""
        pool.shutdown(wait=False, cancel_futures=True)
        if pool not in self.dying:
            self.dying.append(pool)

    def merge_ready(self):
        while self.queue:
            kind, x, i = self.queue[0]
            if kind == "task":
                if x.results is None:
                    return
                self.found.add(x.results[i])
                x.results[i] = None
            elif kind == "found":
                self.found.add(x)
            else:
                x()  # merged up to here: an after_files step
            self.queue.popleft()

    def pools(self):
        return [p for p in (self.pool, self.solo, *self.dying) if p is not None]

    def finish(self):
        if self.parallel:
            self.seal()
            self.submit()
            self.pump(0)
            self.merge_ready()
            self.out = self.found
            for pool in self.pools():
                pool.shutdown(wait=True)
            self.pool = self.solo = None
            self.dying = []

    def stop_pools(self):
        """Stops every worker: at the end, or at once on Ctrl+C or an error (queued work is dropped)."""
        pools = self.pools()
        self.pool = self.solo = None
        self.dying = []
        for pool in pools:
            procs = list((getattr(pool, "_processes", None) or {}).values())
            pool.shutdown(wait=False, cancel_futures=True)
            for p in procs:
                if p.is_alive():
                    p.terminate()
            for p in procs:
                p.join(5)

    def progress(self):
        """At most every PROGRESS_EVERY seconds, one line of counts and the root being scanned on standard error:
        never a finding's path or anything a file holds."""
        now = time.monotonic()
        if now < self.next_line:
            return
        self.next_line = now + PROGRESS_EVERY
        slow = ""
        if not self.parallel:
            files, size, hits, objects = self.found.files, self.found.bytes, len(self.found.hits), self.git_object_count
        else:
            d = self.done
            files, size, hits, objects = d["files"], d["bytes"], d["hits"], d["objects"]
            starts = [r[3] for p in (self.pool, self.solo) if p is not None for r in p.watch.running()]
            if starts and now - min(starts) > SLOW_AFTER:
                slow = f", slowest file running {int(now - min(starts)) // 60} min"
        objs = f", {objects:,} git objects" if objects else ""
        where = esc(self.where)
        print(
            f"scanned {files:,} files{objs} ({size / 1e9:.1f} GB), {hits:,} hits so far{slow}, now in {where}",
            file=sys.stderr,
            flush=True,
        )

    def git_pack(self, path):
        """True for a pack inside a git folder the walk found: its objects come through git cat-file with the
        folder's other objects. A pack whose git folder is not walked (a pack or objects folder given as the root)
        is opened as a file. git reads only a pack with its index; any other file there (no .idx, tmp_pack_*,
        *.pack.bak) is opened as a file too."""
        parts = parts_of(path)
        gitdir = os.path.dirname(os.path.dirname(os.path.dirname(path)))
        return (
            len(parts) >= 3
            and parts[-3:-1] == ("objects", "pack")
            and parts[-1].endswith(".pack")
            and os.path.isfile(path[:-5] + ".idx")
            and same_file_key(gitdir) in self.git_keys
        )

    def no_files(self, root):
        if not self.root_files.get(root):
            self.gap(root, "no file scanned under this root")

    def walk(self, top, root):
        def onerror(e):
            self.gap(e.filename or top, "unreadable folder")

        for dirpath, dirs, names in os.walk(top, onerror=onerror):
            keep = []
            for d in dirs:
                full = os.path.join(dirpath, d)
                if is_link(full):
                    self.links.append((shown_path(full), shown_path(os.path.realpath(full))))
                    continue
                rule = self.exclusion(full)
                if rule:
                    self.excluded.append((shown_path(full), rule))
                    continue
                self.name_hits(full, d)
                keep.append(d)
            dirs[:] = keep
            try:
                for s in named_streams(dirpath):
                    self.name_hits(f"{dirpath}:{s}", s)
                    self.file_task(f"{dirpath}:{s}", root, stream=True)
                if is_git_dir(dirpath):
                    self.git_dirs.append((dirpath, root))
                    self.git_keys.add(same_file_key(dirpath))
                    self.git_links(dirpath)
            except Exception as e:  # noqa: BLE001 -- recorded as a gap; the scan goes on
                self.gap(dirpath, f"could not be checked: {e.__class__.__name__}")
            for n in names:
                self.file_task(os.path.join(dirpath, n), root)

    def guarded(self, path, root, stream=False, in_git_pack=False):
        """One file, its alternate data streams and its name; any failure is a gap and the scan goes on."""
        try:
            self.scan_path(path, root, stream, in_git_pack)
        except Exception as e:  # noqa: BLE001
            self.gap(path, f"could not be checked: {e.__class__.__name__}")
        if not stream:
            try:
                for s in named_streams(path):
                    self.name_hits(f"{path}:{s}", s)
                    self.guarded(f"{path}:{s}", root, stream=True)
            except Exception as e:  # noqa: BLE001
                self.gap(path, f"alternate data streams could not be listed: {e.__class__.__name__}")

    def name_hits(self, path, name):
        """File, folder and member names are text too."""
        b = utf8(name)
        for m in self.patterns:
            c = m.count(b)
            if c:
                self.out.hits.append((shown_path(path), f"{m.name} [name]", c))

    def git_links(self, gitdir):
        """History a git folder takes from outside the roots (alternates, a common folder) is a gap."""
        objects = os.path.join(gitdir, "objects")
        alt = os.path.join(objects, "info", "alternates")
        if os.path.isfile(alt):
            with open(alt, "rb") as f:
                for line in f.read().decode("utf-8", "replace").splitlines():
                    line = line.strip()
                    if line and not line.startswith("#"):
                        target = os.path.normpath(os.path.join(shown_path(objects), line))
                        if not self.covered(target):
                            self.gap(alt, f"git objects outside the roots (alternates): {target}")
        common = os.path.join(gitdir, "commondir")
        if os.path.isfile(common):
            with open(common, "rb") as f:
                rel = f.read().decode("utf-8", "replace").strip()
            target = os.path.normpath(os.path.join(shown_path(gitdir), rel))
            if not self.covered(target):
                self.gap(common, f"git history outside the roots (common folder): {target}")

    # ---------- one file ----------

    def scan_path(self, path, root, stream=False, in_git_pack=False):
        shown = shown_path(path)
        why = None if stream else (self.skip_files.get(same_file_key(path)) if self.skip_files else None)
        if why:
            self.out.skipped.append((shown, why))
            return
        name = os.path.basename(path)
        if not stream:
            self.name_hits(path, name)
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                if size > FULL_MAX:
                    head, data = f.read(SNIFF), None
                else:
                    data = f.read()
                    head = data[:SNIFF]
        except OSError:
            self.gap(shown, "unreadable file")
            return
        self.out.files += 1
        self.out.bytes += size
        self.out.root_files[root] = self.out.root_files.get(root, 0) + 1
        if name == ".git" and head.startswith(b"gitdir:"):
            rel = head[7:].decode("utf-8", "replace").strip()
            target = os.path.normpath(os.path.join(os.path.dirname(shown), rel))
            if not self.covered(target):
                self.gap(shown, f"git history outside the roots (worktree): {target}")
        if data is None:
            try:
                self.large_file(path, shown, size, head, root)
            except OSError:
                self.gap(shown, "unreadable file")
            return
        self.scan_blob(shown, data, root, Budget(shown), 0, in_git_pack=in_git_pack)

    def large_file(self, path, shown, size, head, root):
        """A file over FULL_MAX, read in chunks: its hash, the raw markers, and the markers over its text (UTF-16 or
        UTF-32 text decoded). Binary data gives the markers its text runs for the raw bytes, and its UTF-16 runs at
        both byte alignments, as views() does. Anything but an image or video is a gap: JSON, base64, near copies and
        what it holds are not checked."""
        kind = sniff(head, size)
        enc = detect_encoding(head) if kind not in ("image", "png", "jpeg", "media", "disc") else "raw"
        lanes = []  # (label, feed(chunk, final), counters): each counter gives a view's counts
        if enc in (None, "raw"):
            raw, raw_counter = Runs(False, self.least), RunCounter(self.markers)

            def feed_raw(c, final):
                runs = raw.feed(c, final)
                raw_counter.feed(runs, final, raw.end if final else None)

            lanes.append(("", feed_raw, [raw_counter]))
            if enc is None:
                wide, by_offset = Runs(True, self.least16), (RunCounter(self.markers, 0), RunCounter(self.markers, 1))

                def feed_wide(c, final):
                    runs = wide.feed(c, final)
                    for off, counter in enumerate(by_offset):
                        end = view_end(True, off, wide.end) if final else None
                        counter.feed(latin1_runs([r for r in runs if r[0] % 2 == off]), final, end)

                lanes.append((" [UTF-16 view]", feed_wide, list(by_offset)))
        else:
            as_is = StreamCounter(self.markers)
            lanes.append(("", as_is.feed, [as_is]))
            if enc not in ("utf-8", "utf-8-sig"):
                dec, decoded = codecs.getincrementaldecoder(enc)("replace"), StreamCounter(self.markers)
                lanes.append(
                    (
                        f" [{enc.upper()} text]",
                        lambda c, final: decoded.feed(utf8(dec.decode(c, final=final)), final),
                        [decoded],
                    )
                )
        h = hashlib.sha256()
        with open(path, "rb") as f:
            for chunk in iter(lambda: f.read(CHUNK), b""):
                h.update(chunk)
                for _label, feed, _counters in lanes:
                    feed(chunk, False)
        best = {}
        for label, feed, counters in lanes:
            feed(b"", True)
            for counter in counters:
                counts = counter.counts
                for m in self.markers:
                    c = counts[m.name]
                    if m.hit(c) and c > best.get(m.name, (0, ""))[0]:
                        best[m.name] = (c, label)
        digest = h.hexdigest()
        state = self.allow_state(digest)
        if state == "allowed":
            self.out.allowed.append((digest, shown, "allowed"))
            return
        if not state and digest in self.hashes:
            self.out.hits.append((shown, EXACT, 1))
        if kind == "disc":
            self.disc(shown, size)
        else:
            self.kind_note(shown, kind, size, root)
        self.record(shown, digest, state, False, best)
        if kind in ARCHIVES or kind in UNOPENED or kind == "pdf":
            self.gap(shown, f"{kind} over {FULL_MAX // 1_000_000} MB: what it holds is not opened")
        elif kind not in ("image", "png", "jpeg", "media", "disc"):
            done = "raw markers" + (" and text views" if len(lanes) > 1 else "")
            self.gap(shown, f"over {FULL_MAX // 1_000_000} MB: hash and {done} only (no JSON, base64 or near copies)")

    def disc(self, shown, size):
        """A disc image gets the hash and the raw markers; it is listed and is a gap (accept it per path)."""
        self.out.discs.append((shown, size))
        self.gap(shown, f"disc image ({size // 1_000_000} MB): hash and raw markers only")

    def kind_note(self, shown, kind, size, root):
        """Images and video or audio get the raw markers; they are listed, not gaps."""
        if kind in ("image", "png", "jpeg"):
            self.out.images[root] = self.out.images.get(root, 0) + 1
            if self.list_images:
                self.out.image_paths.append(shown)
        elif kind == "media":
            self.out.notes.append((shown, f"{size // 1_000_000} MB video or audio: raw markers only"))
        elif kind in RAW_ONLY:
            self.out.notes.append(
                (
                    shown,
                    f"{kind}: raw bytes and UTF-16 views (compressed data inside is opened)",
                )
            )

    def allow_state(self, digest):
        """None (not on the allow list), "allowed", or "recheck" (allowed under markers that have changed since)."""
        entry = self.allow.get(digest)
        if not entry:
            return None
        return "allowed" if entry["markers_sha256"] == self.markers_sha else "recheck"

    def record(self, shown, digest, state, recheck, found):
        """Hits for one file or member. An allowed file under changed markers: marker hits are flagged for re-review,
        and with none it stays allowed."""
        found = {n: v for n, v in found.items() if not (recheck or state) or n != NEAR}
        if state:
            if found:
                self.out.hits.extend((shown, f"{n}{label} ({RECHECK})", c) for n, (c, label) in found.items())
            else:
                self.out.allowed.append((digest, shown, "allowed (re-checked under the current markers)"))
        elif recheck:
            self.out.hits.extend((shown, f"{n}{label} ({RECHECK})", c) for n, (c, label) in found.items())
        else:
            self.out.hits.extend((shown, n + label, c) for n, (c, label) in found.items())

    def views(self, shown, data, full):
        """{name: (count, view label)} over the data's views, and the text views (for base64 runs).

        Text: the raw bytes, its decoded form (UTF-16, UTF-32), the strings inside JSON and JSONL, JSON escapes
        decoded when the JSON does not parse. Binary (no text encoding found): the text runs it holds stand for the
        raw bytes in every check. Binary data, and text with a zero byte anywhere: UTF-16 runs at both byte
        alignments. Near copies are checked on the text views when full is set."""
        best = {}

        def add(counts, label):
            for name, c in counts.items():
                if c > best.get(name, (0, ""))[0]:
                    best[name] = (c, label)

        def markers_on(view, label):
            counts = count_view(self.markers, view)
            add({m.name: counts[m.name] for m in self.markers if m.hit(counts[m.name])}, label)

        def near_on(view, label):
            if full and self.shingles:
                common = sum(1 for s in shingles(view.decode("utf-8", "replace")) if s in self.shingles)
                if common >= NEAR_MIN:
                    add({NEAR: common}, label)

        def hits_of(counts):
            return {m.name: counts[m.name] for m in self.markers if m.hit(counts[m.name])}

        def escapes_on(label):
            """JSON escapes decoded: in the whole text, or in each text run of binary data (an escape is text, so
            it never spans two runs)."""
            if runs is None:
                view = utf8(unescape_json(text))
                markers_on(view, label)
                texts.append(view)
                return
            if not JSON_ESCAPE.search(text):  # nothing to decode: the view would be the runs' own
                return
            decoded = [
                (o, e, utf8(unescape_json(r.decode("utf-8", "replace"))) if b"\\" in r else r, j, g)
                for o, e, r, j, g in runs
            ]
            add(hits_of(run_counts(self.markers, decoded, 0, len(data))), label)
            texts.append(b"\0".join(x[2] for x in decoded))

        texts, raw_text, parsed, runs = [], None, False, None
        enc = detect_encoding(data)
        if enc is None:
            runs = Runs(False, self.least).feed(data, final=True)
            add(hits_of(run_counts(self.markers, runs, 0, len(data))), "")
        else:
            markers_on(data, "")
        if enc is None or b"\0" in data:  # binary (anywhere, not only at the start): UTF-16 text at either alignment
            for counts in self.utf16_counts(Runs(True, self.least16).feed(data, final=True), len(data)):
                add(hits_of(counts), " [UTF-16 view]")
        if enc is None:
            near_on(join_runs(runs, SHINGLE_RUN, SHINGLE_SEPS, 0, len(data)), "")
            text = join_runs(runs, 1, MARKER_SEPS, 0, len(data)).decode("utf-8", "replace")
            texts.append(b"\0".join(x[2] for x in runs))  # base64 in the raw bytes (never joined across runs)
        elif enc in ("utf-8", "utf-8-sig"):
            text = data.decode(enc, "replace")
            near_on(data, "")
            raw_text = data
        else:
            text = data.decode(enc, "replace")
            view = utf8(text)
            markers_on(view, f" [{enc.upper()} text]")
            near_on(view, f" [{enc.upper()} text]")
            texts.append(view)
        start = text.lstrip("\ufeff\x00 \t\r\n")
        if start[:1] in ("{", "["):
            res = json_strings(start)
            for why in sorted(res.gaps):
                self.gap(shown, why)
            if res.strings:
                view = utf8("\n".join(res.strings))
                markers_on(view, " [decoded JSON]")
                near_on(view, " [decoded JSON]")
                texts.append(view)
            parsed = res.complete
            if not res.complete and "\\" in text:
                escapes_on(" [JSON escapes decoded]")
        elif "\\u" in text:
            escapes_on(" [JSON escapes decoded]")
        if raw_text is not None and not parsed:  # parsed JSON: its strings are the text, escapes decoded
            texts.append(raw_text)
        return best, texts

    def utf16_counts(self, runs, size):
        """{name: count} over the UTF-16 runs at even and at odd offsets (two views) of data of this size."""
        return [
            run_counts(self.markers, latin1_runs([r for r in runs if r[0] % 2 == off]), off, view_end(True, off, size))
            for off in (0, 1)
        ]

    def binary_counts(self, data):
        """{name: count} over the text runs in binary data: what its raw bytes give the markers."""
        return run_counts(self.markers, Runs(False, self.least).feed(data, final=True), 0, len(data))

    def scan_blob(self, shown, data, root, budget, depth, recheck=False, in_git_pack=False):
        """Every check on one file or unpacked member. recheck: inside an allowed file whose markers changed."""
        digest = hashlib.sha256(data).hexdigest()
        state = self.allow_state(digest)
        if state == "allowed":
            self.out.allowed.append((digest, shown, "allowed"))
            return
        recheck = recheck or state == "recheck"
        if not recheck and len(data) >= EXACT_MIN and digest in self.hashes:
            self.out.hits.append((shown, EXACT, 1))
        kind = sniff(data[:SNIFF], len(data))
        if kind == "disc":
            self.disc(shown, len(data))
            counts = self.binary_counts(data)
            self.record(
                shown,
                digest,
                state,
                recheck,
                {m.name: (counts[m.name], "") for m in self.markers if m.hit(counts[m.name])},
            )
            return
        self.kind_note(shown, kind, len(data), root)
        texts = []
        if kind in ("image", "png", "jpeg", "media"):
            counts = self.binary_counts(data)
            found = {m.name: (counts[m.name], "") for m in self.markers if m.hit(counts[m.name])}
        else:
            found, texts = self.views(shown, data, full=not recheck)
        if kind in UNOPENED:
            self.gap(shown, f"{kind.replace('-compressed', ' (compressed)')} archive: not opened (raw scan only)")
        self.record(shown, digest, state, recheck, found)
        for mshown, mdata in self.inner(shown, data, kind, texts, budget, depth, in_git_pack):
            try:
                self.name_hits(mshown, mshown.rsplit("!", 1)[-1])
                self.scan_blob(mshown, mdata, root, budget, depth + 1, recheck=recheck)
            except Exception as e:  # noqa: BLE001 -- recorded as a gap; the scan goes on
                self.gap(mshown, f"could not be checked: {e.__class__.__name__}")

    # ---------- what a file holds inside ----------

    def note_once(self, budget, key, why):
        if key not in budget.noted:
            budget.noted.add(key)
            self.gap(budget.shown, why)

    def take(self, budget, n, count=True):
        """Count n unpacked bytes against the file's budget; False (a gap, noted once) when it is spent."""
        budget.members += count
        budget.bytes += n
        if budget.members > MEMBERS_MAX:
            self.note_once(budget, "members", f"over {MEMBERS_MAX} members, streams or blobs: the rest not opened")
            return False
        if budget.bytes > EXPAND_MAX:
            self.note_once(budget, "bytes", f"over {EXPAND_MAX // 1_000_000} MB unpacked: the rest not opened")
            return False
        return True

    def inner(self, shown, data, kind, texts, budget, depth, in_git_pack):
        """(shown path, bytes) for every archive member, compressed stream, PDF stream, image chunk, base64 blob and
        git object the data holds."""
        gens = []
        if kind == "zip":
            gens.append(self.zip_members(shown, data, strict=True))
        elif eocd_zip(data):
            gens.append(self.zip_members(shown, data, strict=False))
        if kind in ("gzip", "bzip2", "xz", "zlib"):
            gens.append(self.stream(shown, data, kind))
        elif kind == "tar":
            gens.append(self.tar_members(shown, data))
        elif kind == "pdf":
            gens.append(self.pdf_streams(shown, data))
        elif kind == "png":
            gens.append(self.png_parts(shown, data))
        elif kind == "jpeg":
            gens.append(self.jpeg_tail(shown, data))
        elif kind in ("pack", "bundle") and not in_git_pack:
            gens.append(self.pack_objects(shown, data, kind))
        if kind not in FULLY_OPENED:  # images, video, raw-scanned containers, unknown data and text
            gens.append(self.embedded(shown, data))
        runs = [r for t in texts for r in base64_runs(t)]
        if runs:
            gens.append(self.base64_blobs(shown, runs, budget))
        if not gens:
            return
        if depth >= NEST_DEPTH:
            hidden = kind not in FULLY_OPENED and EMBEDDED.search(data, 1)
            if kind in ARCHIVES or kind in ("pdf", "png", "jpeg") or runs or hidden:
                self.gap(shown, f"nested {NEST_DEPTH} levels deep: what it holds is not opened")
            return
        for gen in gens:
            try:
                for mshown, mdata in gen:
                    # base64 runs are bounded by the text they come from: their bytes count, not their number
                    if not self.take(budget, len(mdata), count="!base64-" not in mshown[len(shown) :]):
                        return
                    yield mshown, mdata
            except Exception as e:  # noqa: BLE001 -- a damaged container: a gap, and the next container
                self.gap(shown, f"could not be opened fully: {e.__class__.__name__}")

    def embedded(self, shown, data):
        """Compressed streams and archives found by their signature anywhere in a file that is not opened whole (an
        image, video, raw-scanned container, unknown data or text). Each stream is decompressed (see stream(), found);
        7z, RAR and zstd data, and zip data without the end record a zip needs (with one, the zip is opened whole),
        are gaps."""
        view, noted = memoryview(data), set()
        for m in EMBEDDED.finditer(data, 1):
            sig, at = m.group(), m.start()
            if sig.startswith((b"PK", b"7z", b"Rar", b"\x28\xb5")):
                what = {b"P": "zip", b"7": "7z", b"R": "RAR", b"\x28": "zstd"}[sig[:1]]
                ok = what not in noted and not (what == "zip" and eocd_zip(data))
                if ok and real_header(what, view, at):
                    noted.add(what)
                    self.gap(
                        shown, f"{what} data inside{' without an end record' if what == 'zip' else ''}: not opened"
                    )
            else:
                kind = {0x1F: "gzip", 0x78: "zlib", 0x42: "bzip2", 0xFD: "xz"}[sig[0]]
                if kind == "zlib" and data[at - 4 : at] == b"IDAT":
                    continue  # pixels of a PNG inside (an icon in a program, say), split over chunks: image data

                yield from self.stream(f"{shown}!at-{at}", view[at:], kind, found=True)

    def too_big(self, shown, what):
        self.gap(shown, f"{what} over {MEMBER_MAX // 1_000_000} MB: not opened")

    def zip_members(self, shown, data, strict):
        """A zip's members. strict: the data starts as a zip; else a zip's end record sits near the end of other data.
        Either way a zip that does not open is a gap."""
        try:
            z = zipfile.ZipFile(io.BytesIO(data))
        except Exception as e:  # noqa: BLE001 -- BadZipFile, a bad name's UnicodeDecodeError, NotImplementedError...
            where = "zip archive" if strict else "zip end record found, but the zip"
            self.gap(shown, f"{where} could not be opened: {e.__class__.__name__}")
            return
        with z:
            for info in z.infolist():
                if info.is_dir():
                    continue
                mshown = f"{shown}!{info.filename}"
                if info.file_size > MEMBER_MAX:
                    self.too_big(mshown, "archive member")
                    continue
                if info.flag_bits & 1:
                    self.gap(mshown, "encrypted archive member: not opened")
                    continue
                try:
                    with z.open(info) as f:
                        body = f.read(MEMBER_MAX + 1)
                except Exception as e:  # noqa: BLE001 -- damaged, unsupported method or version
                    self.gap(mshown, f"archive member could not be opened: {e.__class__.__name__}")
                    continue
                if len(body) > MEMBER_MAX:
                    self.too_big(mshown, "archive member")
                    continue
                yield mshown, body

    @staticmethod
    def inflate(d, data, out, replay=True):
        """Feeds data to a decompressor in pieces, adding to out (up to MEMBER_MAX + 1 bytes). Returns (decompressor,
        the input after the stream's end, error or None, input bytes used). On damage the output before it is kept: a
        zlib or gzip decompressor is copied before each piece, and the piece that fails is fed again byte by byte."""
        view, pos, errors = memoryview(data), 0, (OSError, EOFError, zlib.error, lzma.LZMAError, ValueError)
        while pos < len(data) and len(out) <= MEMBER_MAX and not d.eof:
            piece = view[pos : pos + PIECE]
            saved = d.copy() if replay and hasattr(d, "copy") else None
            try:
                out += d.decompress(piece, MEMBER_MAX + 1 - len(out))
            except errors as e:
                if saved is None:
                    return d, b"", e, pos
                d = saved
                for i in range(len(piece)):
                    try:
                        out += d.decompress(piece[i : i + 1], MEMBER_MAX + 1 - len(out))
                    except errors as e2:
                        return d, b"", e2, pos + i
                    if d.eof or len(out) > MEMBER_MAX:
                        return d, bytes(d.unused_data) + bytes(view[pos + i + 1 :]), None, pos + i + 1
                return d, b"", e, pos + len(piece)
            if d.eof:
                return d, bytes(d.unused_data) + bytes(view[pos + len(piece) :]), None, pos + len(piece)
            pos += len(piece)
        return d, b"", None, pos

    def stream(self, shown, data, kind, found=False):
        """A compressed stream, or several one after another, decompressed (up to MEMBER_MAX in all); a damaged or
        cut-short stream keeps the output before the damage, and is a gap. found: a stream found by its signature
        inside another file, where the signature may be chance bytes: its output is always checked, but a damaged or
        cut-short one is a gap only when it looks real (looks_real), or when it is bzip2 or xz that used FOUND_MIN
        input bytes or more without giving any output (those decoders give nothing before a block is complete)."""
        out, rest, parts = bytearray(), data, 0
        while rest and len(out) <= MEMBER_MAX:
            if kind == "gzip":
                d = zlib.decompressobj(47)
            elif kind == "zlib":
                d = zlib.decompressobj()
            elif kind == "bzip2":
                d = bz2.BZ2Decompressor()
            else:
                d = lzma.LZMADecompressor()
            before = len(out)
            d, after, e, used = self.inflate(d, rest, out)
            if len(out) > MEMBER_MAX:
                break
            if e is not None or not d.eof:
                how = "damaged" if e is not None else "cut short"
                if found:
                    silent_block = kind in ("bzip2", "xz") and used >= FOUND_MIN and len(out) == before
                    if looks_real(used, out[before:]) or silent_block:
                        self.gap(shown, f"{kind} data found inside is {how} (the output before that is checked)")
                elif parts == 0 and not out:
                    if kind != "zlib":  # a "zlib" guess from two bytes may be plain data
                        why = e.__class__.__name__ if e is not None else "cut short"
                        self.gap(shown, f"{kind} data could not be opened: {why}")
                    return
                else:
                    self.gap(shown, f"{kind} data {how} in stream {parts + 1} (the output before that is checked)")
                break
            parts += 1
            if kind == "zlib":
                break
            rest = after.lstrip(b"\x00")  # padding between streams
        if found and not out:
            return
        if len(out) > MEMBER_MAX:
            self.too_big(shown, f"{kind} content")
            out = out[:MEMBER_MAX]
        body = bytes(out)
        m = GIT_HEADER.match(body, 0) if kind == "zlib" else None
        if m:
            yield f"{shown}!object", body[m.end() :]
        else:
            yield (
                f"{shown}!{ {'gzip': 'gunzipped', 'bzip2': 'bunzipped', 'xz': 'unxzed', 'zlib': 'inflated'}[kind] }",
                body,
            )

    def tar_members(self, shown, data):
        try:
            with tarfile.open(fileobj=io.BytesIO(data), mode="r:") as t:
                for info in t:
                    if not info.isfile():
                        continue
                    mshown = f"{shown}!{info.name}"
                    if info.size > MEMBER_MAX:
                        self.too_big(mshown, "archive member")
                        continue
                    f = t.extractfile(info)
                    if f is not None:
                        yield mshown, f.read(MEMBER_MAX + 1)
        except (tarfile.TarError, OSError, EOFError, ValueError) as e:
            self.gap(shown, f"tar archive could not be read to the end: {e.__class__.__name__}")

    def pdf_streams(self, shown, data):
        """Each PDF stream, inflated when it is FlateDecode. Linear: stream ends are found once, and each stream's
        slice stops at the next stream's start."""
        starts = [m.end() for m in PDF_STREAM.finditer(data)]
        ends = [m.start() for m in PDF_END.finditer(data)]
        if len(starts) > PDF_STREAMS_MAX:
            self.gap(shown, f"over {PDF_STREAMS_MAX} PDF streams: the rest not opened")
            starts = starts[:PDF_STREAMS_MAX]
        if PDF_ENCRYPT.search(data):
            self.gap(shown, "encrypted PDF: its streams are not decoded")
            return
        view, prev = memoryview(data), 0
        for n, s in enumerate(starts, 1):
            i = bisect.bisect_left(ends, s)
            if i == len(ends):
                break
            e = min(ends[i], starts[n] if n < len(starts) else len(data))
            mshown = f"{shown}!stream-{n}"
            head = pdf_dict(data[max(prev, s - PDF_DICT_MAX) : s])  # the stream's dictionary
            prev = e
            if head is None:
                self.gap(mshown, f"PDF stream dictionary not found within {PDF_DICT_MAX} bytes: its filter is not read")
                head = b""
            if PDF_FILTER_REF.search(head):
                self.gap(mshown, "PDF stream with an indirect filter: not decoded")
                continue
            if PDF_PREDICTOR.search(head) and not PDF_PIXELS_OR_XREF.search(head):
                self.gap(mshown, "PDF stream with a predictor: decoded without it")
            f = PDF_FILTER.findall(head)
            filters = PDF_NAME.findall(f[-1]) if f else None
            if not filters:  # no filter given: try FlateDecode anyway (a dictionary this crude reading missed)
                d = zlib.decompressobj()
                try:
                    out = d.decompress(view[s:e], MEMBER_MAX + 1)
                except zlib.error:
                    continue
                if out and not d.eof and len(out) <= MEMBER_MAX:
                    self.gap(mshown, "PDF stream (FlateDecode) ends early")
            else:
                out, why = pdf_decode(bytes(view[s:e]), [x.decode("ascii", "replace") for x in filters])
                if why:
                    self.gap(mshown, why)
                if out is None:  # an image filter (the raw stream is the content), or it did not decode
                    continue
            if len(out) > MEMBER_MAX:
                self.too_big(mshown, "PDF stream")
                continue
            if out:
                yield mshown, out

    def png_parts(self, shown, data):
        """A PNG's compressed text chunks (zTXt, iTXt) and anything after its end."""
        i, n = 8, 0
        while i + 8 <= len(data):
            size = int.from_bytes(data[i : i + 4], "big")
            kind, body = data[i + 4 : i + 8], data[i + 8 : i + 8 + size]
            i += 12 + size
            if kind == b"zTXt":
                comp = body.partition(b"\x00")[2][1:]
            elif kind == b"iTXt":
                rest = body.partition(b"\x00")[2]
                comp = rest[2:].partition(b"\x00")[2].partition(b"\x00")[2] if rest[:1] == b"\x01" else b""
            elif kind == b"IEND":
                if data[i:].strip(b"\x00"):
                    yield f"{shown}!after-image", data[i:]
                return
            else:
                continue
            if comp:
                n += 1
                try:
                    yield f"{shown}!text-{n}", zlib.decompressobj().decompress(comp, MEMBER_MAX)
                except zlib.error:
                    self.gap(f"{shown}!text-{n}", "compressed PNG text could not be opened")

    def jpeg_tail(self, shown, data):
        """Anything after a JPEG's end of image (segments are skipped by their lengths, so an embedded thumbnail's
        end does not count)."""
        i = 2
        while i + 4 <= len(data):
            if data[i] != 0xFF:
                return
            marker = data[i + 1]
            if marker == 0xFF:
                i += 1
                continue
            if marker in (0x01, *range(0xD0, 0xD8)):
                i += 2
                continue
            if marker == 0xD9:
                end = i + 2
                break
            seg = int.from_bytes(data[i + 2 : i + 4], "big")
            if marker == 0xDA:  # start of scan: entropy-coded data until the end marker
                j = data.find(b"\xff\xd9", i + 2 + seg)
                if j < 0:
                    return
                end = j + 2
                break
            i += 2 + seg
        else:
            return
        if data[end:].strip(b"\x00"):
            yield f"{shown}!after-image", data[end:]

    def pack_objects(self, shown, data, kind):
        """A git pack or bundle file's objects, through git in a temporary repository."""
        git = self.git()
        if not git:
            self.gap(shown, f"git {kind} file: {self.git_why}, not opened")
            return
        if kind == "bundle":
            at = data.find(b"\n\n")
            if at < 0:
                self.gap(shown, "git bundle without a pack: not opened")
                return
            data = data[at + 2 :]
        with tempfile.TemporaryDirectory(prefix="coney-scan-") as tmp:
            try:
                r = git_run([git, "init", "-q", "--bare", "--template=", tmp])
                if r.returncode:
                    self.gap(shown, f"git {kind} file: a temporary repository could not be made")
                    return
                pack = os.path.join(tmp, "objects", "pack", "scan.pack")
                with open(pack, "wb") as f:
                    f.write(data)
                r = git_run([*git_base(git, tmp), "index-pack", pack])
                if r.returncode:
                    self.gap(shown, f"git {kind} file could not be indexed (damaged, or thin: needs objects it lacks)")
                    return
                for sha, _size, body in git_objects(git, tmp):
                    if body is None:
                        self.too_big(f"{shown}!{sha}", "git object")
                        continue
                    yield f"{shown}!{sha}", body
            except GitError as e:
                self.gap(shown, f"git {kind} file: {e}")

    def base64_blobs(self, shown, runs, budget):
        for n, run in enumerate(runs, 1):
            if len(run) > B64_MAX:
                self.gap(shown, f"a base64 run over {B64_MAX // 1_000_000} MB: not decoded")
                continue
            key = hashlib.sha256(run).digest()
            if key in budget.seen_b64:
                continue
            budget.seen_b64.add(key)
            try:  # URL-safe base64 writes - and _ for + and /
                raw = base64.b64decode(run.translate(B64_URL) + b"=" * (-len(run) % 4), validate=True)
            except (binascii.Error, ValueError):
                continue
            yield f"{shown}!base64-{n}", raw

    def packed_objects(self, gitdir):
        """Every object of a git folder that is not a loose file (the walk opens those), through git cat-file."""
        shown = shown_path(gitdir)
        objects = os.path.join(gitdir, "objects")
        try:
            packs = [n for n in os.listdir(os.path.join(objects, "pack")) if n.endswith(".pack")]
        except OSError:
            packs = []
        if not packs:
            return
        git = self.git()
        if not git:
            self.gap(shown, f"{self.git_why}: packed objects not scanned")
            return
        loose = set()
        try:
            for d in os.listdir(objects):
                if len(d) == 2 and all(c in "0123456789abcdef" for c in d):
                    loose.update(d + n for n in os.listdir(os.path.join(objects, d)) if len(n) in (38, 62))
        except OSError:
            pass
        try:
            for sha, _size, body in git_objects(git, shown, loose):
                oshown = f"{shown}!{sha}"
                self.git_object_count += 1
                if body is None:
                    self.too_big(oshown, "git object")
                    continue
                self.object_task(oshown, body, shown)
        except GitError as e:
            self.gap(shown, f"git objects not fully scanned: {e}")
        except OSError as e:
            self.gap(shown, f"git could not be run: {e.__class__.__name__}")


# ---------- worker processes ----------

_worker = None  # this worker process's Scan, made once when the worker starts
_row = None  # (rows, offset): this worker's row in its pool's Watch


def worker_start(blob, rows=None, claim=None):
    """A worker's start: the markers, hash list, shingle list and allow list arrive once, already loaded, and the
    worker takes a free row of its pool's Watch. Ctrl+C is left to the main process, which stops the workers
    itself."""
    global _worker, _row
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    markers, hashes, shingle_set, allow, markers_sha, opts, roots_real = pickle.loads(blob)  # from the main process
    _worker = Scan(markers, hashes, shingle_set, allow, markers_sha, {**opts, "jobs": 1, "file_timeout": 0})
    _worker.set_roots(roots_real)
    if rows is not None:
        with claim:
            free = [r for r in range(0, len(rows), ROW) if not rows[r]]
            if free:
                rows[free[0]] = os.getpid()
                _row = (rows, free[0])


def worker_on(bid=None, i=0):
    """Writes the task this worker starts (batch id, index), or that it is between tasks (no batch id), to its row:
    the start goes to 0 first and is written last, so a reader never takes one task's start for another's."""
    if _row is None:
        return
    rows, r = _row
    rows[r + 3] = 0.0
    if bid is not None:
        rows[r + 1], rows[r + 2] = bid, i
        rows[r + 3] = time.monotonic()


def worker_batch(tasks, bid=0):
    """Each task's findings, as their own Found, in the tasks' order."""
    out = []
    try:
        for i, task in enumerate(tasks):
            worker_on(bid, i)
            _worker.out = Found()
            _worker.do(task)
            out.append(_worker.out)
    finally:
        worker_on()
    return out


WORKER_BATCH = worker_batch  # what workers run on a batch (tests put a failing one here)


# ---------- the report ----------


def report(scan, meta, excepts, accepted):
    exceptions = [h for h in scan.hits if any(under(h[0], e) for e in excepts)]
    hits = [h for h in scan.hits if h not in exceptions]
    ok_gaps = [g for g in scan.gaps if any(under(g[0], a) for a in accepted)]
    gaps = [g for g in scan.gaps if g not in ok_gaps]
    doc = {
        **meta,
        "files scanned": scan.counts["files"],
        "git objects scanned": scan.counts["git objects"],
        "hits": [{"path": p, "marker": k, "count": c} for p, k, c in hits],
        "exceptions": [{"path": p, "marker": k, "count": c} for p, k, c in exceptions],
        "coverage gaps": [{"path": p, "why": w} for p, w in gaps],
        "accepted gaps": [{"path": p, "why": w} for p, w in ok_gaps],
        "missing roots": scan.missing,
        "excluded folders": [{"path": p, "exclude": r} for p, r in scan.excluded],
        "skipped": [{"path": p, "why": w} for p, w in scan.skipped],
        "links not followed": [{"path": p, "target": t} for p, t in scan.links],
        "notes": [{"path": p, "why": w} for p, w in scan.notes],
        "disc images": [{"path": p, "bytes": s} for p, s in scan.discs],
        "images per root": scan.images,
        "images": scan.image_paths,
        "allowed": [{"sha256": d, "path": p, "how": w} for d, p, w in scan.allowed],
    }
    return doc, bool(hits or gaps)


def esc(s):
    """A path or name as one printable line: control characters, newlines and lone surrogates escaped."""
    s = str(s)
    if s.isprintable():
        return s
    return "".join(c if c.isprintable() else repr(c)[1:-1] if c != "\\" else c for c in s)


def summary_lines(d):
    lines = [f"python: {d['python']}", f"scanner: {d['scanner']}"]
    for k in ("markers", "hashes", "shingles", "allow list"):
        v = d[k]
        lines.append(f"{k}: {v['sha256']} ({v['entries']} entries) {esc(v['path'])}" if v else f"{k}: not given")

    def listed(key):
        return ", ".join(esc(x) for x in d[key]) or "nothing"

    lines.append(f"roots: {listed('roots')}")
    lines.append(f"exclude: {listed('exclude')}; except: {listed('except')}; accept gaps: {listed('accept gaps')}")
    t = d["file timeout"]
    lines.append(f"file timeout: {t} s" if t else "file timeout: none")
    lines.append(f"files scanned: {d['files scanned']}; git objects scanned: {d['git objects scanned']}")
    counts = {k: len(d[k]) for k in ("hits", "exceptions", "coverage gaps", "accepted gaps", "allowed")}
    lines.append("; ".join(f"{k}: {n}" for k, n in counts.items()))
    return lines


def text_report(d):
    lines = summary_lines(d)
    lines += [f"  hit: {h['marker']}: {h['count']}  {esc(h['path'])}" for h in d["hits"]]
    lines += [f"exception: {h['marker']}: {h['count']}  {esc(h['path'])}" for h in d["exceptions"]]
    lines += ["coverage gaps:"] if d["coverage gaps"] else []
    lines += [f"  gap: {esc(g['path'])} ({esc(g['why'])})" for g in d["coverage gaps"]]
    lines += [f"accepted gap: {esc(g['path'])} ({esc(g['why'])})" for g in d["accepted gaps"]]
    lines += [f"excluded folder: {esc(x['path'])} (--exclude {esc(x['exclude'])})" for x in d["excluded folders"]]
    lines += [f"skipped: {esc(x['path'])} ({x['why']})" for x in d["skipped"]]
    lines += [f"link not followed: {esc(x['path'])} -> {esc(x['target'])}" for x in d["links not followed"]]
    lines += [f"note: {esc(x['path'])} ({esc(x['why'])})" for x in d["notes"]]
    lines += [f"disc image: {esc(x['path'])} ({x['bytes'] // 1_000_000} MB)" for x in d["disc images"]]
    lines += [f"images under {esc(r)}: {c} (raw markers only; review by hand)" for r, c in d["images per root"].items()]
    lines += [f"image: {esc(p)}" for p in d["images"]]
    lines += [f"allowed: {x['sha256']}  {esc(x['path'])}  ({x['how']})" for x in d["allowed"]]
    return "\n".join(lines) + "\n"


def data_paths(a):
    """The data files to use: explicit options first, then the --data folder's standard names."""
    out = {}
    for key, name in DATA_NAMES.items():
        explicit = getattr(a, key, None)
        if explicit:
            out[key] = explicit
        elif getattr(a, "data", None) and os.path.isfile(os.path.join(a.data, name)):
            out[key] = os.path.join(a.data, name)
    return out


def exclude_is_path(x):
    """An --exclude value names one folder when it is absolute, `.`, `..`, or starts with ./ .\\ ../ or ..\\;
    otherwise (`.venv`, `game`, `tester\\game`) it names every folder whose path ends with those components."""
    return os.path.isabs(x) or x in (".", "..") or x.startswith(("./", ".\\", "../", "..\\"))


def exclude_path(x):
    """An --exclude path made absolute. A \\ separates names on every system, as in the component form (parts_of),
    so .\\b names the same folder on Linux as on Windows."""
    return os.path.abspath(x if os.sep == "\\" else x.replace("\\", "/"))


def jobs_of(a):
    """Worker processes to use: --jobs, else one per CPU."""
    jobs = getattr(a, "jobs", None)
    return jobs if jobs else (os.cpu_count() or 1)


def file_timeout_of(a):
    """Seconds one file may take: --file-timeout, else FILE_TIMEOUT; 0 is no limit."""
    t = getattr(a, "file_timeout", None)
    return FILE_TIMEOUT if t is None else t


def run(a):
    """The scan command: returns (exit status 0 or 1, report document). Raises DataError when it cannot run."""
    paths = data_paths(a)
    for key in ("markers", "hashes", "shingles"):
        if key not in paths:
            raise DataError(f"no {key} file: give --{key} FILE or --data DIR holding {DATA_NAMES[key]}")
    raw = {}
    for key, p in paths.items():
        try:
            raw[key] = Path(p).read_bytes()
        except OSError as e:
            raise DataError(f"cannot read the {key} file {p}: {e.__class__.__name__}") from None
    markers = load_markers(raw["markers"])
    hashes = load_hash_list(raw["hashes"], "hashes")
    shingle_set = load_hash_list(raw["shingles"], "shingles")
    allow = load_allow(raw["allow"]) if "allow" in raw else {}
    sha = {k: hashlib.sha256(v).hexdigest() for k, v in raw.items()}
    skip = {p: f"the scan's own {k} file" for k, p in paths.items()}
    excludes = [exclude_path(x) if exclude_is_path(x) else x for x in a.exclude]
    excepts = [os.path.abspath(x) for x in a.excepts]
    accepted = [os.path.abspath(x) for x in a.accept_gap]
    scan = Scan(
        markers,
        hashes,
        shingle_set,
        allow,
        sha["markers"],
        {
            "list_images": a.list_images,
            "exclude": excludes,
            "skip_files": skip,
            "jobs": jobs_of(a),
            "file_timeout": file_timeout_of(a),
        },
    ).run(a.roots)
    sizes = {"markers": len(markers), "hashes": len(hashes), "shingles": len(shingle_set), "allow": len(allow)}
    meta = {
        "python": f"{platform.python_implementation()} {platform.python_version()} ({sys.platform})",
        "scanner": scanner_sha256(),
        **{
            ("allow list" if k == "allow" else k): (
                {"sha256": sha[k], "entries": sizes[k], "path": shown_path(os.path.abspath(paths[k]))}
                if k in paths
                else None
            )
            for k in DATA_NAMES
        },
        "roots": a.roots,
        "exclude": excludes,
        "except": excepts,
        "accept gaps": accepted,
        "file timeout": file_timeout_of(a),
    }
    doc, failed = report(scan, meta, excepts, accepted)
    return (1 if failed else 0), doc


def scanner_sha256():
    """This module's hash with LF line endings, the same on Windows and Linux checkouts."""
    return hashlib.sha256(Path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest()
