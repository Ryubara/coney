"""Tests for the scan's second review round, on synthetic data with made-up markers only."""

import base64
import gzip
import io
import json
import os
import stat
import struct
import sys
import unittest
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_scan as ts

from coney_tools.cleanroom import scan

WORD = ts.WORD
PAYLOAD = (f" {WORD} " * 20).encode()


class Patched:
    """Sets module attributes of scan for the length of a with block."""

    def __init__(self, **values):
        self.values, self.old = values, {}

    def __enter__(self):
        for k, v in self.values.items():
            self.old[k] = getattr(scan, k)
            setattr(scan, k, v)

    def __exit__(self, *exc):
        for k, v in self.old.items():
            setattr(scan, k, v)


def hit_paths(doc):
    return [h["path"].replace("\\", "/") for h in doc["hits"]]


class GitLookup(ts.Base):
    def setUp(self):
        super().setUp()
        self.old_path, self.old_cwd = os.environ.get("PATH", ""), os.getcwd()
        self.addCleanup(self.restore)

    def restore(self):
        os.environ["PATH"] = self.old_path
        os.chdir(self.old_cwd)

    def test_git_is_never_taken_from_the_current_folder_a_relative_entry_or_a_root(self):
        for name in ("git.exe", "git.bat", "git.cmd", "git"):
            p = self.put(f"bin/{name}", "planted, never run")
            p.chmod(p.stat().st_mode | stat.S_IXUSR)
            self.put(name, "planted, never run")
        real = scan.find_git()
        os.chdir(self.root)
        os.environ["PATH"] = os.pathsep.join(
            ["", ".", "bin", str(self.root), str(self.root / "bin"), *([os.path.dirname(real)] if real else [])]
        )
        found = scan.find_git([str(self.root)])
        self.assertEqual(found, real)
        if found:
            self.assertTrue(os.path.isabs(found))
            self.assertFalse(scan.under(found, str(self.root)))

    def test_a_git_timeout_is_a_gap(self):
        if not ts.GIT:
            self.skipTest("git not on PATH")
        repo = ts.make_repo(self.root / "repo", WORD + "\n")
        ts.git(repo, "gc", "-q")
        with Patched(GIT_TIMEOUT=1e-6):
            self.assertGap("was stopped")
        pack = next((repo / ".git" / "objects" / "pack").glob("*.pack"))
        loose = self.tmp / "loose"
        loose.mkdir()
        (loose / "copied.pack").write_bytes(pack.read_bytes())
        with Patched(GIT_TIMEOUT=1e-6):
            code, doc, out = self.scan_json(roots=[str(loose)])
        self.assertEqual(code, 1)
        self.assertTrue(any("was stopped" in g for g in self.gaps(doc)), out)

    def test_a_pack_or_objects_folder_given_as_the_root_is_opened(self):
        if not ts.GIT:
            self.skipTest("git not on PATH")
        repo = ts.make_repo(self.root / "repo", WORD + "\n")
        ts.git(repo, "gc", "-q", "--prune=now")
        (repo / "f.txt").unlink()
        pack = next((repo / ".git" / "objects" / "pack").glob("*.pack"))
        for root in (pack, repo / ".git" / "objects"):
            with self.subTest(str(root)):
                code, doc, out = self.scan_json(roots=[str(root)])
                self.assertEqual(code, 1, out)
                self.assertTrue(any(".pack!" in p for p in hit_paths(doc)), out)


class Base64(ts.Base):
    def test_letters_only_and_url_safe_base64_in_text_and_json(self):
        letters = base64.b64encode(f" {WORD} {'aaa' * 30}".encode()).decode()
        self.assertTrue(letters.isalpha())
        urlsafe = base64.urlsafe_b64encode(b"\xfb\xff\xfe" * 8 + f" {WORD} padded long enough".encode()).decode()
        self.assertTrue("-" in urlsafe and "_" in urlsafe)
        self.put("l.txt", f"x {letters} y")
        self.put("l.json", json.dumps({"d": letters}))
        self.put("u.txt", f"x {urlsafe} y")
        self.put("u.json", json.dumps({"d": urlsafe}))
        _, doc, out = self.scan_json()
        self.assertEqual(
            sorted(Path(p.split("!")[0]).name for p in hit_paths(doc)), ["l.json", "l.txt", "u.json", "u.txt"]
        )
        self.assertTrue(all("!base64-" in p for p in hit_paths(doc)), out)


class LargeFiles(ts.Base):
    def scan_small_limit(self):
        with Patched(FULL_MAX=1000, CHUNK=1000):
            return self.scan_json()

    def test_large_utf16_text_is_decoded_in_chunks_and_is_a_gap(self):
        self.put("big.txt", b"\xff\xfe" + ("ordinary text line\n" * 200 + f" {WORD} ").encode("utf-16-le"))
        code, doc, out = self.scan_small_limit()
        self.assertEqual(code, 1)
        self.assertEqual([h["marker"] for h in doc["hits"]], ["made-up word [UTF-16 text]"], out)
        self.assertTrue(any("text views only" in g for g in self.gaps(doc)), out)

    def test_large_binary_gets_both_utf16_views(self):
        self.put("big.bin", bytes(2001) + f" {WORD} ".encode("utf-16-le") + bytes(500))
        _, doc, out = self.scan_small_limit()
        self.assertEqual([h["marker"] for h in doc["hits"]], ["made-up word [UTF-16 view]"], out)

    def test_large_utf8_text_is_a_gap(self):
        self.put("big.txt", "plain words\n" * 200)
        code, doc, _ = self.scan_small_limit()
        self.assertEqual(code, 1)
        self.assertTrue(any("raw markers only" in g for g in self.gaps(doc)))

    def test_large_disc_image_is_a_gap_with_raw_markers(self):
        iso = bytearray(0x8001 + 64)
        iso[0x8001:0x8006] = b"CD001"
        self.put("game.bin", bytes(iso) + f" {WORD} ".encode())
        with Patched(FULL_MAX=1000, CHUNK=1000):
            doc = self.assertGap("disc image")
        self.assertEqual(len(doc["hits"]), 1)


class MarkerTiming(ts.Base):
    def test_slow_markers_are_refused_by_name_only(self):
        for rx in ("(?:a|a){1,20}b", ".{0,200}.{0,200}.{0,200}x"):
            with self.subTest(rx), Patched(PROBE_BUDGET=0.1):
                self.write_data([{"name": "slow-one", "regex": rx}])
                code, out, err = ts.run("scan", str(self.root), "--data", str(self.data))
                self.assertEqual(code, 2)
                self.assertIn("slow-one", err)
                self.assertIn("too slow", err)
                self.assertNotIn(rx, out + err)

    def test_the_inputs_use_the_markers_own_characters(self):
        tree = scan._rp.parse(b"(?:Q|Q){1,20}[k-m]\\d", 0)
        seeds = scan.seeds_of(tree)
        self.assertEqual(seeds[:5], [ord("Q"), ord("q"), ord("k"), ord("K"), ord("l")])
        self.assertIn(ord("0"), seeds)


class Containers(ts.Base):
    def test_a_zip_end_record_that_does_not_open_is_a_gap(self):
        good = ts.zip_of({"f.txt": WORD})
        i = good.find(b"PK\x01\x02")
        self.put("setup.exe", b"MZ" + bytes(100) + good[:i] + good[i + 10 :])
        self.assertGap("zip end record found")

    def test_zlib_with_a_bad_checksum_keeps_its_output(self):
        z = bytearray(zlib.compress(PAYLOAD))
        z[-1] ^= 0xFF
        self.put("a.bin", bytes(z))
        g = gzip.compress(PAYLOAD)
        self.put("a.gz", g[:-8] + b"\0\0\0\0" + g[-4:])
        doc = self.assertGap("damaged")
        self.assertEqual(sorted(Path(p).name for p in hit_paths(doc)), ["a.bin!inflated", "a.gz!gunzipped"])

    def test_payloads_inside_images_and_video(self):
        gz = gzip.compress(PAYLOAD)
        bmp_body = bytes(40) + gz
        cases = {
            "a.gif": b"GIF89a" + bytes(20) + gz,
            "a.bmp": b"BM" + struct.pack("<I", 14 + len(bmp_body)) + bytes(8) + bmp_body,
            "a.webp": b"RIFF\0\0\0\0WEBPVP8 " + bytes(20) + gz,
            "a.mp4": bytes(4) + b"ftypisom" + bytes(20) + gz,
            "b.mp4": bytes(4) + b"ftypisom" + bytes(20) + zlib.compress(PAYLOAD) + bytes(9),
            "a.jpg": b"\xff\xd8\xff\xe0\x00\x10JFIF" + bytes(10) + b"garbage" + gz,
        }
        for name, data in cases.items():
            self.put(name, data)
        _, doc, out = self.scan_json()
        self.assertEqual({Path(p.split("!")[0]).name for p in hit_paths(doc)}, set(cases), out)
        self.assertEqual(doc["coverage gaps"], [])

    def test_7z_inside_an_image_is_a_gap(self):
        start = bytes(range(20))  # a 7z start header, with its CRC
        head = b"7z\xbc\xaf\x27\x1c\x00\x04" + zlib.crc32(start).to_bytes(4, "little") + start
        self.put("a.gif", b"GIF89a" + bytes(20) + head + bytes(32))
        self.assertGap("7z data inside")

    def pdf(self, filt, body):
        return b"%PDF-1.4\n1 0 obj << /Filter " + filt + b" >>\nstream\n" + body + b"\nendstream\nendobj\n%%EOF"

    def test_pdf_ascii_filters_are_decoded(self):
        self.put("hex.pdf", self.pdf(b"/ASCIIHexDecode", PAYLOAD.hex().encode() + b">"))
        self.put("a85.pdf", self.pdf(b"/ASCII85Decode", base64.a85encode(PAYLOAD) + b"~>"))
        chained = base64.a85encode(zlib.compress(PAYLOAD)) + b"~>"
        self.put("chain.pdf", self.pdf(b"[/ASCII85Decode /FlateDecode]", chained))
        _, doc, out = self.scan_json()
        names = sorted(Path(p).name for p in hit_paths(doc))
        self.assertEqual(names, ["a85.pdf!stream-1", "chain.pdf!stream-1", "hex.pdf!stream-1"], out)

    def test_pdf_lzw_unknown_filters_and_encryption_are_gaps(self):
        self.put("lzw.pdf", self.pdf(b"/LZWDecode", b"\x80\x0b\x60\x50"))
        doc = self.assertGap("LZWDecode filter")
        self.put("enc.pdf", b"%PDF-1.4\ntrailer << /Encrypt 5 0 R >>\n" + self.pdf(b"/FlateDecode", b"x")[9:])
        self.assertGap("encrypted PDF")
        self.put("img.pdf", self.pdf(b"/DCTDecode", b"\xff\xd8 image"))
        _, doc, _ = self.scan_json()
        self.assertFalse(any("img.pdf" in g["path"] for g in doc["coverage gaps"]))


class Names(ts.Base):
    def test_the_root_folders_own_name_is_checked(self):
        root = self.tmp / f"{WORD}-root"
        root.mkdir()
        (root / "a.txt").write_text("clean")
        _, doc, _ = self.scan_json(roots=[str(root)])
        self.assertEqual([h["marker"] for h in doc["hits"]], ["made-up word [name]"])

    @unittest.skipUnless(os.name == "nt", "NTFS alternate data streams")
    def test_alternate_stream_names_are_checked(self):
        p = self.put("a.txt", "clean")
        try:
            with open(f"{p}:{WORD}", "w") as f:
                f.write("clean")
        except OSError:
            self.skipTest("no alternate data streams here")
        _, doc, _ = self.scan_json()
        self.assertEqual([h["marker"] for h in doc["hits"]], ["made-up word [name]"])

    @unittest.skipUnless(os.name == "nt", "8.3 short names")
    def test_short_and_long_names_of_a_root_match(self):
        import ctypes

        buf = ctypes.create_unicode_buffer(1024)
        folder = self.root / "a long folder name"
        folder.mkdir()
        ctypes.windll.kernel32.GetShortPathNameW(str(folder), buf, 1024)
        short = buf.value
        ctypes.windll.kernel32.GetLongPathNameW(str(folder), buf, 1024)
        long = buf.value
        if not short or short.lower() == long.lower():
            self.skipTest("no short names on this volume")
        for root, path in ((short, long), (long, short)):
            s = scan.Scan([], set(), set(), {}, "", {})
            s.roots_real = [os.path.realpath(root)]
            s.root_parts = [scan.parts_of(r) for r in s.roots_real]
            self.assertTrue(s.covered(os.path.join(path, "x")), (root, path))


class Lines(ts.Base):
    def test_pattern_anchors_see_crlf_lines(self):
        self.write_data([{"name": "anchored", "regex": r"^zorb" + r"lax$", "flags": ["MULTILINE"]}])
        self.put("a.txt", f"a\r\n{WORD}\r\nb\r\n")
        self.assertHit("a.txt", "anchored")
        with Patched(FULL_MAX=1000, CHUNK=1000):
            self.put("a.txt", "filler line\r\n" * 100 + f"{WORD}\r\n" + "x\r\n" * 100)
            self.assertHit("a.txt", "anchored")

    def test_a_cr_lf_split_between_chunks(self):
        m = scan.load_markers(json.dumps({"markers": [{"name": "l", "kind": "listing", "regex": "^x$"}]}).encode())
        c = scan.StreamCounter(m)
        c.feed(b"x\r")
        c.feed(b"\nx\r\n", final=True)
        self.assertEqual(c.counts["l"], 2)


class Excludes(ts.Base):
    def test_dot_names_are_components_and_dot_slash_is_a_path(self):
        self.put("a/.venv/f.txt", WORD)
        self.put("b/.venv/f.txt", WORD)
        self.put("c/f.txt", WORD)
        _, doc, _ = self.scan_json("--exclude", ".venv")
        self.assertEqual(len(doc["excluded folders"]), 2)
        self.assertTrue(all("/c/" in p for p in hit_paths(doc)))
        old = os.getcwd()
        os.chdir(self.root)
        try:
            _, doc, _ = self.scan_json("--exclude", "./a/.venv", "--exclude", ".\\b")
        finally:
            os.chdir(old)
        self.assertEqual(len(doc["excluded folders"]), 2)
        self.assertTrue(doc["hits"] and all("/c/" in p for p in hit_paths(doc)))

    def test_exclude_is_path(self):
        for x in (".", "..", "./a", ".\\a", "../a", "..\\a", os.path.abspath("x")):
            self.assertTrue(scan.exclude_is_path(x), x)
        for x in (".venv", ".git", "game", "tester\\game", "..a"):
            self.assertFalse(scan.exclude_is_path(x), x)


class Zips(unittest.TestCase):
    def test_zip_member_damage_in_a_prefixed_zip_is_a_gap(self):
        b = bytearray(ts.zip_of({"f.txt": WORD}))
        i = b.find(b"PK\x01\x02")
        b[i + 6] = 80
        buf = io.BytesIO(b"MZ" + bytes(100) + bytes(b))
        self.assertTrue(scan.eocd_zip(buf.getvalue()))
        s = scan.Scan([], set(), set(), {}, "", {})
        list(s.zip_members("x", buf.getvalue(), strict=False))
        self.assertTrue(s.gaps)
        self.assertTrue(zipfile.is_zipfile(buf))


if __name__ == "__main__":
    unittest.main()
