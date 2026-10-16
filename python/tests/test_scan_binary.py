"""Tests for the scan's text runs in binary data, on random bytes and made-up markers only."""

import gzip
import json
import random
import sys
import time
import unittest
import zlib
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_scan as ts
from test_scan import LISTING_LINE, OLD_TEXT, WORD
from test_scan_review2 import Patched

from coney_tools.cleanroom import scan

TEXT = f"a line with {WORD} in it, and {WORD} again; {WORD} at the end\n"  # three matches


def noise(n, seed=1):
    """Random bytes without a zero byte at the start, so nothing but the scan's own reading finds the text."""
    return random.Random(seed).randbytes(n)


def counts(doc):
    return {(Path(h["path"].split("!")[0]).name, h["marker"]): h["count"] for h in doc["hits"]}


def views(runs, utf16, size):
    """Every view the scan can make of whole data's runs: per lane, per shortest run kept, per separator set."""
    out = []
    for lane in (0, 1) if utf16 else (0,):
        mine = [r for r in runs if r[0] % 2 == lane] if utf16 else runs
        mine = scan.latin1_runs(mine) if utf16 else mine
        for least in (1, 5, 9, 30):
            for seps in (scan.MARKER_SEPS, scan.SHINGLE_SEPS):
                out.append(scan.join_runs(mine, least, seps, lane, scan.view_end(utf16, lane, size)))
    return out


MADE_UP = [  # synthetic markers: one anchored at line starts, one bounded by words, one listing
    {"name": "line start", "kind": "pattern", "regex": r"^\s{0,8}[a-z]{2}\s", "flags": ["MULTILINE"]},
    {"name": "word", "kind": "pattern", "regex": r"\b[a-z]{3}[0-9]\b", "flags": []},
    {"name": "listing", "kind": "listing", "regex": r"^[ -~]{2,4}$", "min_lines": 1},
]


class TextInBinary(ts.Base):
    def test_ascii_text_in_random_bytes_keeps_its_count(self):
        self.put("a.bin", noise(300_000) + TEXT.encode() + noise(300_000, 2))
        self.put("l.bin", noise(5000) + b"\n" + (LISTING_LINE * 4).encode() + noise(5000, 3))
        _, doc, out = self.scan_json()
        c = counts(doc)
        self.assertEqual(c.get(("a.bin", "made-up word")), 3, out)
        self.assertEqual(c.get(("l.bin", "made-up listing")), 4, out)

    def test_utf16_text_at_both_offsets_and_big_endian_keeps_its_count(self):
        le = TEXT.encode("utf-16-le")
        self.put("even.bin", noise(4000) + le + noise(4000, 2))
        self.put("odd.bin", noise(4001) + le + noise(4000, 2))
        self.put("be.bin", noise(4000) + TEXT.encode("utf-16-be") + b"\x00\x00" + noise(4000, 2))
        _, doc, out = self.scan_json()
        c = counts(doc)
        for name in ("even.bin", "odd.bin", "be.bin"):
            self.assertEqual(c.get((name, "made-up word [UTF-16 view]")), 3, (name, out))

    def test_text_in_a_compressed_stream_inside_random_bytes_keeps_its_count(self):
        self.put("z.bin", noise(70_000) + zlib.compress(TEXT.encode() * 2) + noise(70_000, 2))
        self.put("g.bin", noise(70_000) + gzip.compress(TEXT.encode()) + noise(70_000, 2))
        _, doc, out = self.scan_json()
        hits = {(Path(h["path"].split("!")[0]).name, h["path"].rsplit("!", 1)[-1]): h["count"] for h in doc["hits"]}
        self.assertEqual(hits.get(("z.bin", "inflated")), 6, out)
        self.assertEqual(hits.get(("g.bin", "gunzipped")), 3, out)

    def test_near_copies_base64_and_utf8_text_in_random_bytes_are_found(self):
        import base64

        self.put("near.bin", noise(20_000) + OLD_TEXT.replace("line", "line — ü").encode() + noise(100))
        self.write_data(ts.MARKERS)
        (self.data / "old-shingles.txt").write_text(
            "\n".join(scan.shingles(OLD_TEXT.replace("line", "line — ü"))) + "\n"
        )
        self.put("b64.bin", noise(20_000) + b"\x01" + base64.b64encode(TEXT.encode()) + b"\x02" + noise(100))
        self.put("utf8.bin", noise(20_000) + f"— éé {WORD} 日".encode() + noise(100))
        _, doc, out = self.scan_json()
        markers = {(Path(h["path"].split("!")[0]).name, h["marker"]) for h in doc["hits"]}
        self.assertIn(("near.bin", scan.NEAR), markers, out)
        self.assertIn(("b64.bin", "made-up word"), markers, out)
        self.assertIn(("utf8.bin", "made-up word"), markers, out)

    def test_large_files_find_runs_across_chunk_boundaries(self):
        def put_at(name, *placed):
            body = bytearray(noise(12_000))
            for at, piece in placed:
                body[at : at + len(piece)] = piece
            self.put(name, bytes(body))

        put_at("ascii.bin", (1995, TEXT.encode()), (6990, TEXT.encode()))  # across chunk ends at 2000 and 7000
        put_at("even.bin", (4990, (TEXT * 2).encode("utf-16-le")))
        put_at("odd.bin", (8991, TEXT.encode("utf-16-le")))
        long_run = (f"{WORD} " * 400).encode()  # one run longer than the run carry: passed on in parts
        self.put("long.bin", noise(1500) + long_run + noise(1500, 2))
        with Patched(FULL_MAX=1000, CHUNK=1000, RUN_CARRY_MAX=700):
            _, doc, out = self.scan_json()
        c = counts(doc)
        self.assertEqual(c.get(("ascii.bin", "made-up word")), 6, out)
        self.assertEqual(c.get(("even.bin", "made-up word [UTF-16 view]")), 6, out)
        self.assertEqual(c.get(("odd.bin", "made-up word [UTF-16 view]")), 3, out)
        self.assertEqual(c.get(("long.bin", "made-up word")), 400, out)

    def test_chunked_runs_are_the_whole_data_runs(self):
        rng = random.Random(5)
        pieces = [b"\x00\x01", b"plain text\n", "—é日 x".encode(), b"\xc3", b"\xbf\x80", b"\x01\n\x02", b"\x1c"]
        pieces += [TEXT.encode("utf-16-le"), b"\x00" + TEXT.encode("utf-16-le")]
        for trial in range(60):
            data = b"".join(rng.choice(pieces) + noise(rng.randint(0, 60), trial) for _ in range(rng.randint(1, 200)))
            for utf16 in (False, True):
                least = rng.randint(1, 8)
                whole = scan.Runs(utf16, least).feed(data, final=True)
                runs, chunked, i = scan.Runs(utf16, least), [], 0
                while i < len(data):
                    step = rng.randint(1, 700)
                    chunked += runs.feed(data[i : i + step])
                    i += step
                chunked += runs.feed(b"", final=True)
                with self.subTest(trial=trial, utf16=utf16):
                    self.assertEqual(views(whole, utf16, len(data)), views(chunked, utf16, len(data)))
                    if utf16:
                        ref = [m.group() for m in scan.UTF16_RUN.finditer(data) if len(m.group()) >= 2 * least]
                    else:
                        key = b"a" * least
                        ref = [
                            m.group()
                            for m in scan.TEXT_RUN.finditer(data)
                            if key in m.group().translate(scan.ASCII_MAP)
                        ]
                    self.assertEqual([x[2] for x in whole], ref)

    def test_a_text_run_starts_at_the_utf8_text_before_its_ascii(self):
        data = b"\x01\xe2\x80\x94ab\xc3\xa9 longer ascii\x02\xbf\x80 tail"
        self.assertEqual(
            [x[2] for x in scan.Runs(False, 6).feed(data, final=True)], [b"\xe2\x80\x94ab\xc3\xa9 longer ascii"]
        )

    def test_runs_keep_bytes_that_are_not_text_and_line_ends_around_them(self):
        def joined(data, least=4, seps=scan.MARKER_SEPS):
            return scan.join_runs(scan.Runs(False, least).feed(data, final=True), least, seps, 0, len(data))

        self.assertEqual(joined(b"abcd\x01\x02efgh"), b"abcd\x00efgh")
        self.assertEqual(joined(b"\x01abcd\x02\n\x03efgh\x04"), b"\x00abcd\x00\n\x00efgh\x00")
        self.assertEqual(joined(b"abcd\x01\rxy\x02efgh"), b"abcd\x00\n\x00efgh")  # a dropped run's CR too
        self.assertEqual(joined(b"abcd\x01\x1c\x02efgh"), b"abcd\x00efgh")
        self.assertEqual(joined(b"abcd\x01\x1c\x02efgh", seps=scan.SHINGLE_SEPS), b"abcd\nefgh")
        self.assertEqual(joined(b"abcd\x01\x02efgh", seps=scan.SHINGLE_SEPS), b"abcd\x00efgh")

    def test_random_bytes_give_no_more_hits_than_their_raw_bytes(self):
        markers = scan.load_markers(json.dumps({"markers": MADE_UP}).encode())
        s = scan.Scan(markers, set(), set(), {}, "", {})
        totals = {"raw": {}, "runs": {}}
        for seed in range(3):
            data = noise(2_000_000, 100 + seed)
            raw = scan.count_view(markers, data)
            mine = s.binary_counts(data)
            old16 = [scan.count_view(markers, scan.utf8(data[o:].decode("utf-16-le", "replace"))) for o in (0, 1)]
            mine16 = s.utf16_counts(scan.Runs(True, s.least16).feed(data, final=True), len(data))
            for m in markers:
                with self.subTest(seed=seed, marker=m.name):
                    self.assertLessEqual(mine[m.name], raw[m.name])
                    for a, b in zip(mine16, old16, strict=True):
                        self.assertLessEqual(a[m.name], b[m.name])
                for k, v in (("raw", raw), ("runs", mine)):
                    totals[k][m.name] = totals[k].get(m.name, 0) + v[m.name]
        self.assertGreater(totals["raw"]["line start"], 0, totals)  # the reference does find chance matches
        self.assertGreater(totals["runs"]["word"], 0, totals)  # and the runs still hold the text in between

    def test_text_keeps_its_views(self):
        self.put("t.txt", TEXT)
        self.put("n.txt", TEXT + "\x00\n" + TEXT)  # text with a zero byte: still read as text
        _, doc, out = self.scan_json()
        c = counts(doc)
        self.assertEqual(c.get(("t.txt", "made-up word")), 3, out)
        self.assertEqual(c.get(("n.txt", "made-up word")), 6, out)

    def test_random_bytes_scan_fast(self):
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        import bench_scan

        s = bench_scan.make_scan()
        data = noise(8_000_000, 9)
        t = time.perf_counter()
        s.scan_blob("r", data, "root", scan.Budget("r"), 0)
        took = time.perf_counter() - t
        self.assertLess(took, 8 / 4, f"{8 / took:.1f} MB/s")  # about 60 MB/s here; 4 MB/s leaves room for load


class Batches(ts.Base):
    def test_a_large_file_is_a_batch_of_its_own(self):
        for i in range(30):
            self.put(f"d/f{i:02}.txt", f"small file {i}\n")
        self.put("d/f15-big.bin", noise(3000))
        sent = []
        real = scan.Scan.send

        def send(self, b):
            sent.append([Path(t[1]).name for t in b.tasks])
            return real(self, b)

        with Patched(BIG_TASK=2000), mock.patch.object(scan.Scan, "send", send):
            code, out, err = ts.run("scan", str(self.root), "--data", str(self.data), "--json", "--jobs", "2")
        self.assertEqual((code, err), (0, ""), out)
        self.assertIn(["f15-big.bin"], sent, sent)
        self.assertTrue(all(len(b) == 1 or "f15-big.bin" not in b for b in sent), sent)
        self.assertEqual(sum(map(len, sent)), 31)
        self.assertEqual(json.loads(out)["files scanned"], 31)


if __name__ == "__main__":
    unittest.main()
