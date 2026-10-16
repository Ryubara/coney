"""Tests for the scan's third review round, on synthetic data with made-up markers only."""

import base64
import bz2
import gzip
import json
import random
import shutil
import sys
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_scan as ts
from test_scan_review2 import Patched, hit_paths

WORD = ts.WORD
PAYLOAD = (f" {WORD} " * 20).encode()
BIG = "".join(f"line {i} {WORD} with some words\n" for i in range(400)).encode()  # compresses well, inflates big


def names(doc):
    return sorted({Path(p.split("!")[0]).name for p in hit_paths(doc)})


class Embedded(ts.Base):
    def test_compressed_data_inside_files_not_opened_whole(self):
        self.put("blob.bin", b"\x00\x01\x02binary" + bytes(30) + gzip.compress(PAYLOAD) + bytes(10))
        self.put("a.ar", b"!<arch>\n" + bytes(20) + zlib.compress(PAYLOAD))
        self.put("a.db", b"SQLite format 3\0" + bytes(40) + bz2.compress(PAYLOAD))
        self.put("a.doc", b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1" + bytes(40) + gzip.compress(PAYLOAD))
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["a.ar", "a.db", "a.doc", "blob.bin"], out)
        self.assertTrue(all("!at-" in p for p in hit_paths(doc)), out)
        self.assertEqual(doc["coverage gaps"], [])

    def test_unopened_formats_inside_and_at_the_start_are_gaps(self):
        self.put("x.bin", b"\x00\x01binary" + bytes(20) + b"\x28\xb5\x2f\xfd" + bytes(40))
        doc = self.assertGap("zstd data inside")
        for p in self.root.iterdir():
            p.unlink()
        starts = {
            "a.Z": b"\x1f\x9d\x90",
            "a.lz": b"LZIP\x01",
            "a.lzo": b"\x89LZO\x00\r\n\x1a\n",
            "a.rpm": b"\xed\xab\xee\xdb",
            "a.sqfs": b"hsqs",
            "a.arj": b"\x60\xea",
        }
        for name, magic in starts.items():
            self.put(name, magic + bytes(64))
        _, doc, out = self.scan_json()
        self.assertEqual(len(doc["coverage gaps"]), len(starts), out)

    def test_signatures_inside_need_a_well_formed_header(self):
        rar5_body = b"\x03\x01\x00\x00"  # header size 3: type 1 (main), flags, archive flags
        rar5 = b"Rar!\x1a\x07\x01\x00" + zlib.crc32(rar5_body).to_bytes(4, "little") + rar5_body
        rar4_body = b"\x73\x00\x00\x0d\x00" + bytes(6)  # archive header: type, flags, size 13, reserved
        rar4 = b"Rar!\x1a\x07\x00" + (zlib.crc32(rar4_body) & 0xFFFF).to_bytes(2, "little") + rar4_body
        local = b"PK\x03\x04\x14\x00\x00\x00\x08\x00" + bytes(16) + b"\x05\x00\x00\x00" + b"f.txt" + bytes(20)
        real = {"rar5.bin": rar5, "rar4.bin": rar4, "zip.bin": local}
        for name, sig in real.items():
            self.put(name, b"\x00\x01binary" + bytes(20) + sig + bytes(40))
        # the same signatures as constants in a program (a compiled module, say): chance bytes follow them
        chance = b"Rar!\x1a\x07\x00|Rar!\x1a\x07\x01\x00|7z\xbc\xaf\x27\x1c|PK\x03\x04|\x28\xb5\x2f\xfd\xff\xff\xff"
        self.put("module.pyc", b"\xf3\x0d\x0d\x0a" + bytes(12) + chance * 3 + bytes(40))
        _, doc, out = self.scan_json()
        self.assertEqual(sorted(Path(g["path"]).name for g in doc["coverage gaps"]), sorted(real), out)

    def test_png_pixels_inside_a_program_are_image_data(self):
        z = zlib.compress(bytes(range(256)) * 64)
        half = len(z) // 2
        idat = b"\x00\x00\x01\x00IDAT" + z[:half] + b"\x00" * 4 + b"\x00\x00\x01\x00IDAT" + z[half:]
        self.put("tool.exe", b"MZ\x90\x00" + bytes(60) + b"\x89PNG\r\n\x1a\n" + bytes(25) + idat + bytes(40))
        self.assertClean()

    def test_damaged_cut_short_or_large_found_streams(self):
        z = zlib.compress(BIG)
        self.put("cut.bin", b"\x00\x01binary" + bytes(20) + z[: len(z) * 2 // 3])
        bad = bytearray(z)
        bad[len(bad) * 2 // 3] ^= 0xFF
        bad[len(bad) * 2 // 3 + 1] ^= 0xFF
        self.put("bad.bin", b"\x00\x01binary" + bytes(20) + bytes(bad))
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["bad.bin", "cut.bin"], out)
        self.assertEqual(len(self.gaps(doc)), 2, out)
        self.assertTrue(all("found inside is" in g for g in self.gaps(doc)), out)

    def test_short_chance_streams_are_dropped_silently(self):
        z = zlib.compress(b"tiny clean text")
        self.put("chance.bin", b"\x00\x01binary" + bytes(20) + z[:-6] + bytes(20))
        self.assertClean()

    def test_a_short_damaged_found_stream_keeps_its_output(self):
        z = gzip.compress(PAYLOAD)
        self.put("short.bin", b"\x00\x01binary" + bytes(20) + z[:-6] + bytes(20))  # well under FOUND_MIN
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["short.bin"], out)
        self.assertEqual(doc["coverage gaps"], [], out)

    def test_a_found_bzip2_with_no_output_yet_is_a_gap(self):
        noise = random.Random(1).randbytes(40_000)
        z = bz2.compress(noise)
        self.assertGreater(len(z), 4000)
        self.put("cut.bin", b"\x00\x01binary" + bytes(20) + z[:3000])  # no block complete: the decoder gives nothing
        self.assertGap("bzip2 data found inside is")

    def test_a_found_stream_over_the_member_limit_is_a_gap(self):
        self.put("big.bin", b"\x00\x01binary" + bytes(20) + zlib.compress(BIG))
        with Patched(MEMBER_MAX=2000):
            code, doc, out = self.scan_json()
        self.assertEqual(code, 1)
        self.assertTrue(any("over" in g and "not opened" in g for g in self.gaps(doc)), out)
        self.assertTrue(hit_paths(doc), out)


class Packs(ts.Base):
    def test_packs_git_ignores_are_opened(self):
        if not ts.GIT:
            self.skipTest("git not on PATH")
        src = ts.make_repo(self.tmp / "src", WORD + "\n")
        ts.git(src, "gc", "-q")
        pack = next((src / ".git" / "objects" / "pack").glob("*.pack"))
        repo = ts.make_repo(self.root / "repo", "clean\n")
        ts.git(repo, "gc", "-q")
        dest = repo / ".git" / "objects" / "pack"
        for name in ("tmp_pack_abc", "pack-old.pack.bak", "pack-noidx.pack"):
            shutil.copy(pack, dest / name)
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["pack-noidx.pack", "pack-old.pack.bak", "tmp_pack_abc"], out)

    def test_old_git_is_a_gap(self):
        if not ts.GIT:
            self.skipTest("git not on PATH")
        repo = ts.make_repo(self.root / "repo", "clean\n")
        ts.git(repo, "gc", "-q")
        with Patched(git_version=lambda git: (2, 30)):
            self.assertGap("git older than 2.44")


class DataLists(ts.Base):
    def test_empty_or_malformed_hash_and_shingle_lists_stop_the_scan(self):
        self.put("a.txt", "x")
        good_h = (self.data / "old-file-hashes.txt").read_text()
        good_s = (self.data / "old-shingles.txt").read_text()
        for name, text in (
            ("old-file-hashes.txt", ""),
            ("old-file-hashes.txt", good_h + "abc123\n"),
            ("old-file-hashes.txt", good_h + "g" * 64 + "\n"),
            ("old-shingles.txt", " \n"),
            ("old-shingles.txt", good_s + "0" * 64 + "\n"),
        ):
            with self.subTest(name=name, text=text[-20:]):
                (self.data / "old-file-hashes.txt").write_text(good_h)
                (self.data / "old-shingles.txt").write_text(good_s)
                (self.data / name).write_text(text)
                code, _, err = ts.run("scan", str(self.root), "--data", str(self.data))
                self.assertEqual(code, 2, err)
                self.assertNotIn("Traceback", err)


class Views(ts.Base):
    def test_utf16_after_a_nul_beyond_the_first_4kb(self):
        self.put("late.txt", b"a" * 5000 + b"\0\0" + f" {WORD} ".encode("utf-16-le"))
        self.assertHit("late.txt", "made-up word [UTF-16 view]")

    def test_base64_in_binary_bytes_short_runs_and_quoted_wraps(self):
        enc = base64.b64encode(f" {WORD} ".encode() + b"padding.x").decode()  # 18 bytes: the shortest run decoded
        self.assertEqual(len(enc), 24)
        self.put("bin.dat", b"\x00\x01\x02\xff" + enc.encode() + b"\x00\xfe")
        self.put("short.txt", f"key: {enc}\n")
        long = base64.b64encode(PAYLOAD).decode()
        lines = [long[i : i + 40] for i in range(0, len(long), 40)]
        self.put("wrap.py", "DATA = (\n" + "".join(f'    "{x}"\n' for x in lines) + ")\n")
        self.put("wrap.json", json.dumps({"d": lines}, indent=2))
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["bin.dat", "short.txt", "wrap.json", "wrap.py"], out)


class Pdf(ts.Base):
    def pdf(self, dictionary, body, start=b"stream\n"):
        return b"%PDF-1.4\n1 0 obj " + dictionary + b"\n" + start + body + b"\nendstream\nendobj\n%%EOF"

    def test_pdf_gaps(self):
        z = zlib.compress(PAYLOAD)
        cases = {
            "ref.pdf": (self.pdf(b"<< /Filter 5 0 R >>", z), "indirect filter"),
            "pred.pdf": (self.pdf(b"<< /Filter /FlateDecode /DecodeParms << /Predictor 12 >> >>", z), "predictor"),
            "long.pdf": (self.pdf(b"<< /Pad (" + b"x" * 5000 + b") /Filter /FlateDecode >>", z), "not found within"),
            "cut.pdf": (self.pdf(b"<< /Filter /FlateDecode >>", z[:-8]), "ends early"),
            "nofilter.pdf": (self.pdf(b"<< /Length 9 >>", z[:-8]), "ends early"),
        }
        for name, (data, why) in cases.items():
            with self.subTest(name):
                for p in self.root.iterdir():
                    p.unlink()
                self.put(name, data)
                self.assertGap(why)

    def test_predictors_on_xref_and_image_streams_are_not_gaps(self):
        z = zlib.compress(b"clean offsets and pixels " * 20)
        parms = b"/Filter /FlateDecode /DecodeParms << /Predictor 12 >>"
        self.put("xref.pdf", self.pdf(b"<< /Type /XRef " + parms + b" >>", z))
        self.put("img.pdf", self.pdf(b"<< /Subtype /Image " + parms + b" >>", z))
        self.assertClean()

    def test_cr_and_space_after_the_stream_keyword(self):
        z = zlib.compress(PAYLOAD)
        self.put("cr.pdf", self.pdf(b"<< /Filter /FlateDecode >>", z, b"stream\r"))
        self.put("sp.pdf", self.pdf(b"<< /Filter /FlateDecode >>", z, b"stream \n"))
        _, doc, out = self.scan_json()
        self.assertEqual(names(doc), ["cr.pdf", "sp.pdf"], out)
        self.assertTrue(all("!stream-1" in p for p in hit_paths(doc)), out)


class Roots(ts.Base):
    def test_an_excluded_root_is_a_gap(self):
        self.put("a.txt", "x")
        doc = self.assertGap("root excluded", "--exclude", self.root.name)
        self.assertEqual(len(doc["excluded folders"]), 1)


if __name__ == "__main__":
    unittest.main()
