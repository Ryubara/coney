"""Tests for cleanroom scan, on synthetic folders with made-up markers only (no real marker, hash or shingle)."""

import base64
import bz2
import gzip
import hashlib
import io
import json
import lzma
import os
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest
import zipfile
import zlib
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from coney_tools.cleanroom import scan
from coney_tools.cli import main

WORD = "zorb" + "lax"  # a made-up marker word
MARKERS = [
    {"name": "made-up word", "kind": "pattern", "regex": r"\bzorb" + r"lax\b", "flags": ["IGNORECASE"]},
    {"name": "made-up listing", "kind": "listing", "regex": r"^QQ[0-9]{4} [a-z]{2,8}$", "min_lines": 3},
]
LISTING_LINE = "QQ" + "1234 frob\n"
OLD_TEXT = "".join(f"old line number {i} of a made-up file\n" for i in range(12))
GIT = shutil.which("git")
GIT_ENV = {**os.environ, "GIT_CONFIG_GLOBAL": os.devnull, "GIT_CONFIG_NOSYSTEM": "1"}


def run(*argv):
    """The command in this process. A scan runs with --jobs 1 and no file time limit (so in this process) unless a
    test gives --jobs, so the tests that patch the scan module's values see them used (worker processes load the
    module afresh)."""
    if argv[:1] == ("scan",) and "--jobs" not in argv:
        argv = (*argv, "--jobs", "1")
        if "--file-timeout" not in argv:
            argv = (*argv, "--file-timeout", "0")
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = main(["cleanroom", *argv])
    return code, out.getvalue(), err.getvalue()


def zip_of(members):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        for name, body in members.items():
            z.writestr(name, body)
    return buf.getvalue()


def git(repo, *args):
    cmd = [
        GIT,
        "-C",
        str(repo),
        "-c",
        "user.name=t",
        "-c",
        "user.email=t@example.invalid",
        "-c",
        "commit.gpgsign=false",
    ]
    return subprocess.run([*cmd, *args], check=True, env=GIT_ENV, capture_output=True)


def make_repo(path, text):
    path.mkdir(parents=True)
    git(path, "init", "-q")
    (path / "f.txt").write_text(text)
    git(path, "add", "f.txt")
    git(path, "commit", "-q", "-m", "c")
    return path


class Base(unittest.TestCase):
    def setUp(self):
        t = tempfile.TemporaryDirectory()
        self.addCleanup(t.cleanup)
        self.tmp = Path(t.name)
        self.root = self.tmp / "root"
        self.root.mkdir()
        self.data = self.tmp / "data"
        self.data.mkdir()
        self.old_copy = ("an old file that was copied whole, long enough to be checked\n" * 8).encode()
        self.write_data(MARKERS)

    def write_data(self, markers, allow=None):
        (self.data / "markers.json").write_text(json.dumps({"markers": markers}), encoding="utf-8")
        (self.data / "old-file-hashes.txt").write_text(hashlib.sha256(self.old_copy).hexdigest() + "\n")
        (self.data / "old-shingles.txt").write_text("\n".join(scan.shingles(OLD_TEXT)) + "\n")
        if allow is not None:
            (self.data / "allow.json").write_text(json.dumps({"entries": allow}), encoding="utf-8")

    def markers_sha(self):
        return hashlib.sha256((self.data / "markers.json").read_bytes()).hexdigest()

    def put(self, rel, data):
        p = self.root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
        return p

    def scan_json(self, *extra, roots=None):
        code, out, err = run("scan", *(roots or [str(self.root)]), "--data", str(self.data), "--json", *extra)
        self.assertEqual(err, "")
        return code, json.loads(out), out

    def gaps(self, doc):
        return [g["why"] for g in doc["coverage gaps"]]

    def assertHit(self, rel, marker="made-up word", *extra):
        code, doc, out = self.scan_json(*extra)
        self.assertEqual(code, 1, out)
        self.assertTrue(
            any(h["path"].replace("\\", "/").find(rel) >= 0 and h["marker"].startswith(marker) for h in doc["hits"]),
            out,
        )
        self.assertNotIn(WORD, out.lower().replace(WORD + ".txt", ""))  # never the matched text
        return doc

    def assertClean(self, *extra):
        code, doc, out = self.scan_json(*extra)
        self.assertEqual(code, 0, out)
        return doc

    def assertGap(self, words, *extra):
        code, doc, out = self.scan_json(*extra)
        self.assertEqual(code, 1, out)
        self.assertTrue(any(words in g for g in self.gaps(doc)), out)
        return doc


class Basics(Base):
    def test_clean_folder_passes_and_records_the_run(self):
        self.put("a.txt", "nothing to see\n")
        doc = self.assertClean()
        self.assertEqual(doc["files scanned"], 1)
        for key in ("python", "scanner", "markers", "hashes", "shingles", "roots"):
            self.assertTrue(doc[key], key)
        self.assertIsNone(doc["allow list"])
        self.assertEqual(doc["scanner"], scan.scanner_sha256())

    def test_pattern_hit_names_the_marker_not_the_text(self):
        self.put("notes.txt", f"some {WORD.upper()} here\n")
        code, out, _ = run("scan", str(self.root), "--data", str(self.data))
        self.assertEqual(code, 1)
        self.assertIn("made-up word", out)
        self.assertNotIn(WORD, out.lower())

    def test_listing_kind_needs_min_lines(self):
        self.put("two.txt", LISTING_LINE * 2)
        self.assertClean()
        self.put("three.txt", "intro\n" + LISTING_LINE * 3)
        doc = self.assertHit("three.txt", "made-up listing")
        self.assertEqual([h["count"] for h in doc["hits"]], [3])

    def test_listing_lines_with_crlf(self):
        self.put("crlf.txt", LISTING_LINE.replace("\n", "\r\n") * 3)
        self.assertHit("crlf.txt", "made-up listing")

    def test_exact_and_near_copies(self):
        self.put("copy.bin", self.old_copy)
        self.put("near.md", "new intro\n" + OLD_TEXT[:400] + "new end\n")
        _, doc, _ = self.scan_json()
        self.assertEqual(
            {(Path(h["path"]).name, h["marker"]) for h in doc["hits"]},
            {("copy.bin", scan.EXACT), ("near.md", scan.NEAR)},
        )

    def test_near_copy_despite_a_nul(self):
        self.put("near.txt", "\0" + OLD_TEXT)
        self.assertHit("near.txt", scan.NEAR)

    def test_single_file_root(self):
        p = self.put("one.txt", WORD)
        self.assertEqual(self.scan_json(roots=[str(p)])[0], 1)

    def test_text_report_and_out_file(self):
        self.put("a.txt", WORD)
        out_file = self.tmp / "report.txt"
        code, out, _ = run("scan", str(self.root), "--data", str(self.data), "--out", str(out_file))
        self.assertEqual(code, 1)
        self.assertIn("hits: 1; exceptions: 0; coverage gaps: 0; accepted gaps: 0", out)
        report = out_file.read_text(encoding="utf-8")
        self.assertIn("hit: made-up word: 1", report)
        self.assertIn("python: ", report)
        self.assertEqual([p.name for p in self.tmp.iterdir() if p.name.startswith(".scan-")], [])

    def test_report_that_cannot_be_written(self):
        self.put("a.txt", "x")
        code, _, err = run("scan", str(self.root), "--data", str(self.data), "--out", str(self.tmp / "no" / "r.txt"))
        self.assertEqual(code, 2)
        self.assertIn("cannot write", err)
        self.assertNotIn("Traceback", err)

    def test_control_characters_in_names_are_escaped(self):
        self.put("a.zip", zip_of({"line\nbreak.txt": WORD}))
        code, out, _ = run("scan", str(self.root), "--data", str(self.data))
        self.assertEqual(code, 1)
        self.assertIn("line\\nbreak.txt", out)
        self.assertFalse(any(line.startswith("break.txt") for line in out.splitlines()))
        self.assertEqual(scan.esc("a\ud800b\tc"), "a\\ud800b\\tc")

    def test_names_are_checked(self):
        self.put(f"{WORD}.txt", "innocent")
        self.put(f"{WORD}-folder/a.txt", "innocent")
        self.put("a.zip", zip_of({f"x/{WORD}.md": "innocent"}))
        _, doc, _ = self.scan_json()
        named = [h for h in doc["hits"] if h["marker"].endswith("[name]")]
        self.assertEqual(len(named), 3)


class ExitStatus(Base):
    def test_gaps_fail_and_can_be_accepted(self):
        self.put("a.7z", b"7z\xbc\xaf\x27\x1c" + bytes(64))
        doc = self.assertGap("7z archive")
        code, doc, _ = self.scan_json("--accept-gap", str(self.root / "a.7z"))
        self.assertEqual(code, 0)
        self.assertEqual(len(doc["accepted gaps"]), 1)
        self.assertEqual(doc["coverage gaps"], [])

    def test_missing_root_is_a_gap(self):
        self.put("a.txt", "x")
        gone = str(self.tmp / "gone")
        code, doc, _ = self.scan_json(roots=[str(self.root), gone])
        self.assertEqual(code, 1)
        self.assertEqual(doc["missing roots"], [gone])
        self.assertEqual(self.gaps(doc), ["missing root"])
        code, doc, _ = self.scan_json("--accept-gap", gone, roots=[str(self.root), gone])
        self.assertEqual(code, 0)

    def test_empty_root_is_a_gap(self):
        self.assertGap("no file scanned")

    def test_relative_paths_are_resolved(self):
        self.put("a.7z", b"7z\xbc\xaf\x27\x1c" + bytes(64))
        self.put("b.txt", WORD)
        old = os.getcwd()
        os.chdir(self.root)
        try:
            code, doc, _ = self.scan_json("--accept-gap", "a.7z", "--except", "b.txt")
        finally:
            os.chdir(old)
        self.assertEqual(code, 0)
        self.assertEqual(doc["accepted gaps"][0]["path"], str(self.root / "a.7z"))
        self.assertEqual(len(doc["exceptions"]), 1)

    def test_an_error_in_one_file_is_a_gap_and_the_scan_goes_on(self):
        self.put("bad.txt", "boom")
        self.put("good.txt", WORD)
        real = scan.sniff

        def sniff(h, size):
            if h.startswith(b"boom"):
                raise MemoryError
            return real(h, size)

        scan.sniff = sniff
        try:
            code, doc, out = self.scan_json()
        finally:
            scan.sniff = real
        self.assertEqual(code, 1)
        self.assertIn("could not be checked: MemoryError", self.gaps(doc))
        self.assertEqual(len(doc["hits"]), 1)
        self.assertNotIn("Traceback", out)


class DataFiles(Base):
    def refused(self, regex):
        self.write_data([{"name": "secret-name", "regex": regex}])
        code, out, err = run("scan", str(self.root), "--data", str(self.data))
        self.assertEqual(code, 2, regex)
        self.assertIn("secret-name", err)
        self.assertNotIn(regex, err + out)
        self.assertNotIn("Traceback", err)

    def test_unbounded_markers_refused_without_showing_them(self):
        for rx in ("qq" + "x+y", "a(?=b*)", "(ab){2,}", "(?:a|b.*)", "a{1,20000}", "(?>a+)", "(?>(?:b|c*))"):
            self.refused(rx)

    def test_backreferences_conditionals_nesting_width_and_non_ascii_refused(self):
        for rx in (
            r"(a)\1",
            r"(a)?(?(1)b|c)",
            "(?=(?:a{0,200}){0,200})b",  # narrow (a lookahead has no width), but 40,000 steps
            "a{0,5000}",
            "x{0,3000}y{0,3000}",
            "caf\xe9",
        ):
            self.refused(rx)

    def test_bounded_markers_load(self):
        m = scan.load_markers(json.dumps({"markers": [{"name": "ok", "regex": r"(?:ab){1,40}c{0,100}"}]}).encode())
        self.assertEqual(m[0].name, "ok")

    def test_missing_or_broken_data(self):
        (self.data / "markers.json").unlink()
        self.assertEqual(run("scan", str(self.root), "--data", str(self.data))[0], 2)
        (self.data / "markers.json").write_text("{not json")
        self.assertEqual(run("scan", str(self.root), "--data", str(self.data))[0], 2)
        self.write_data([{"name": "m", "regex": "a", "kind": "other"}])
        self.assertEqual(run("scan", str(self.root), "--data", str(self.data))[0], 2)
        self.write_data([{"name": "m", "regex": "(unclosed"}])
        self.assertEqual(run("scan", str(self.root), "--data", str(self.data))[0], 2)

    def test_explicit_files_override_data(self):
        other = self.tmp / "other.json"
        other.write_text(json.dumps({"markers": [{"name": "other", "regex": "plover"}]}))
        self.put("a.txt", "plover " + WORD)
        _, doc, _ = self.scan_json("--markers", str(other))
        self.assertEqual({h["marker"] for h in doc["hits"]}, {"other"})

    def test_data_files_under_a_root_are_skipped_and_listed(self):
        self.put("a.txt", "x")
        code, doc, _ = self.scan_json(roots=[str(self.tmp)])
        self.assertEqual(code, 0)
        self.assertEqual(len(doc["skipped"]), 3)

    def test_same_file_key_ignores_a_zero_file_number(self):
        p = self.put("a.txt", "x")
        real = scan.os.stat

        class Fake:
            st_dev, st_ino = 1, 0

        scan.os.stat = lambda path: Fake()
        try:
            self.assertEqual(scan.same_file_key(str(p)), os.path.normcase(str(p)))
        finally:
            scan.os.stat = real


class Folders(Base):
    def test_tool_folders_are_scanned(self):
        for d in (".venv", "node_modules", "__pycache__", ".git"):
            self.put(f"{d}/inner/f.txt", WORD)
        _, doc, _ = self.scan_json()
        self.assertEqual(len(doc["hits"]), 4)
        self.assertEqual(doc["excluded folders"], [])

    def test_exclude_matches_whole_components_and_lists_them(self):
        self.put("a/game/f.txt", WORD)
        self.put("a/gamex/f.txt", WORD)
        self.put("b/tester/game/f.txt", WORD)
        self.put("b/other/game/f.txt", "x")
        code, doc, _ = self.scan_json("--exclude", "tester\\game", "--exclude", str(self.root / "a" / "game"))
        self.assertEqual(code, 1)
        self.assertEqual(sorted(Path(h["path"]).parent.name for h in doc["hits"]), ["gamex"])
        self.assertEqual(len(doc["excluded folders"]), 2)
        _, doc, _ = self.scan_json("--exclude", "game")
        self.assertEqual(len(doc["excluded folders"]), 3)

    def test_exceptions_do_not_count(self):
        p = self.put("own/transcript.jsonl", json.dumps({"m": WORD}) + "\n")
        code, doc, _ = self.scan_json("--except", str(p.parent))
        self.assertEqual(code, 0)
        self.assertEqual(len(doc["exceptions"]), 1)

    def test_folder_links_are_listed_not_followed(self):
        target = self.tmp / "elsewhere"
        target.mkdir()
        (target / "f.txt").write_text(WORD)
        self.put("clean.txt", "x")
        link = self.root / "link"
        try:
            os.symlink(target, link, target_is_directory=True)
        except (OSError, NotImplementedError):
            if os.name != "nt":
                self.skipTest("cannot make a folder link")
            r = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(target)], capture_output=True, check=False)
            if r.returncode:
                self.skipTest("cannot make a junction")
        doc = self.assertClean()
        self.assertEqual(len(doc["links not followed"]), 1)

    @unittest.skipUnless(os.name == "nt", "NTFS alternate data streams")
    def test_alternate_data_streams(self):
        p = self.put("a.txt", "clean")
        try:
            with open(str(p) + ":hidden", "w") as f:
                f.write(WORD)
        except OSError:
            self.skipTest("no alternate data streams here")
        doc = self.assertHit("a.txt:hidden")
        self.assertEqual(doc["files scanned"], 2)


class Git(Base):
    def test_loose_git_objects_by_content_anywhere(self):
        body = zlib.compress(f"blob 20\0{WORD} in history\n".encode())
        self.put(".git/objects/ab/" + "c" * 38, body)
        self.put("backup/objs/q", body)
        self.put("repo.zip", zip_of({".git/objects/ab/" + "c" * 38: body}))
        _, doc, _ = self.scan_json()
        self.assertEqual(len(doc["hits"]), 3)
        self.assertTrue(all(h["path"].endswith("!object") for h in doc["hits"]))

    def git_or_skip(self):
        if not GIT:
            self.skipTest("git not on PATH")

    def test_packed_git_objects(self):
        self.git_or_skip()
        repo = make_repo(self.root / "repo", WORD + "\n")
        git(repo, "rm", "-q", "f.txt")
        git(repo, "commit", "-q", "-m", "d")
        git(repo, "gc", "-q", "--prune=now")
        self.assertFalse(any(p.is_file() for p in (repo / ".git" / "objects").glob("[0-9a-f][0-9a-f]/*")))
        code, doc, out = self.scan_json()
        self.assertEqual(code, 1, out)
        self.assertGreater(doc["git objects scanned"], 0)
        self.assertEqual(len(doc["hits"]), 1)  # the pack file itself is not opened a second time
        self.assertEqual(doc["coverage gaps"], [])

    def test_replace_refs_and_git_variables_do_not_hide_objects(self):
        self.git_or_skip()
        repo = make_repo(self.root / "repo", WORD + "\n")
        blob = git(repo, "rev-parse", "HEAD:f.txt").stdout.decode().strip()
        clean = subprocess.run(
            [GIT, "-C", str(repo), "hash-object", "-w", "--stdin"], input=b"clean\n", capture_output=True, check=True
        )
        git(repo, "replace", blob, clean.stdout.decode().strip())
        git(repo, "gc", "-q", "--prune=now")
        (repo / "f.txt").unlink()
        empty = self.tmp / "empty-objects"
        empty.mkdir()
        os.environ["GIT_OBJECT_DIRECTORY"] = str(empty)
        try:
            doc = self.assertHit("repo")
        finally:
            del os.environ["GIT_OBJECT_DIRECTORY"]
        self.assertEqual(doc["coverage gaps"], [])

    def test_packs_with_no_objects_read_are_a_gap(self):
        self.git_or_skip()
        self.put("bare/HEAD", "ref: refs/heads/main\n")
        self.put("bare/refs/heads/.keep", "")
        self.put("bare/objects/pack/pack-1.pack", b"PACK\0\0\0\2\0\0\0\0")
        self.assertGap("git objects not fully scanned")

    def test_packed_objects_without_git_are_a_gap(self):
        self.put("bare/HEAD", "ref: refs/heads/main\n")
        self.put("bare/refs/heads/.keep", "")
        self.put("bare/objects/pack/pack-1.pack", b"PACK")
        old = scan.find_git
        scan.find_git = lambda avoid=(): None
        try:
            self.assertGap("git not found")
        finally:
            scan.find_git = old

    def test_pack_and_bundle_files(self):
        self.git_or_skip()
        repo = make_repo(self.tmp / "src", WORD + "\n")
        git(repo, "bundle", "create", str(self.root / "x.bundle"), "HEAD")
        git(repo, "gc", "-q")
        pack = next((repo / ".git" / "objects" / "pack").glob("*.pack"))
        shutil.copy(pack, self.root / "copied.pack")
        _, doc, out = self.scan_json()
        self.assertEqual({Path(h["path"].split("!")[0]).name for h in doc["hits"]}, {"x.bundle", "copied.pack"}, out)

    def test_history_outside_the_roots_is_a_gap(self):
        self.git_or_skip()
        repo = make_repo(self.tmp / "main", "clean\n")
        git(repo, "worktree", "add", "-q", "--detach", str(self.root / "wt"))
        self.assertGap("git history outside the roots (worktree)")
        shared = self.root / "shared"
        subprocess.run([GIT, "clone", "-q", "--shared", str(repo), str(shared)], check=True, env=GIT_ENV)
        shutil.rmtree(self.root / "wt")
        self.assertGap("alternates")


class Contents(Base):
    def test_images_get_raw_markers_and_are_listed(self):
        self.put("text-named.png", WORD)
        png = b"\x89PNG\r\n\x1a\n" + b"\0" * 32 + f" {WORD} ".encode()
        self.put("picture.txt", png)
        _, doc, _ = self.scan_json("--list-images")
        self.assertEqual({Path(h["path"]).name for h in doc["hits"]}, {"text-named.png", "picture.txt"})
        self.assertEqual(len(doc["images"]), 1)
        self.assertTrue(doc["images"][0].endswith("picture.txt"))

    def test_what_follows_an_image(self):
        jpeg = b"\xff\xd8\xff\xe0\x00\x04ab" + b"\xff\xda\x00\x02" + b"scan data" + b"\xff\xd9"
        self.put("a.jpg", jpeg + gzip.compress(f"x {WORD}".encode()))
        chunk = b"IEND"
        png = b"\x89PNG\r\n\x1a\n" + struct.pack(">I", 0) + chunk + zlib.crc32(chunk).to_bytes(4, "big")
        self.put("a.png", png + zip_of({"f.txt": WORD}))
        text = b"Comment\0\0" + zlib.compress(f"x {WORD}".encode())
        ztxt = struct.pack(">I", len(text)) + b"zTXt" + text + b"\0\0\0\0"
        self.put("b.png", b"\x89PNG\r\n\x1a\n" + ztxt + png[8:])
        _, doc, _ = self.scan_json()
        self.assertEqual({h["path"].split("!", 1)[0][-5:] for h in doc["hits"]}, {"a.jpg", "a.png", "b.png"})

    def test_media_gets_raw_markers(self):
        self.put("v.mkv", b"\x1a\x45\xdf\xa3" + f" {WORD} ".encode())
        _, doc, _ = self.scan_json()
        self.assertEqual(len(doc["hits"]), 1)
        self.assertIn("raw markers only", doc["notes"][0]["why"])

    def test_disc_image_is_a_gap_with_raw_markers(self):
        iso = bytearray(0x8001 + 64)
        iso[0x8001:0x8006] = b"CD001"
        iso[-20:] = WORD.encode().ljust(20)
        p = self.put("game.dat", bytes(iso))
        doc = self.assertGap("disc image")
        self.assertEqual(len(doc["hits"]), 1)
        self.assertEqual(len(doc["disc images"]), 1)  # listed whatever its size
        iso[-20:] = bytes(20)
        p.write_bytes(bytes(iso))
        code, doc, _ = self.scan_json("--accept-gap", str(p))
        self.assertEqual(code, 0)
        self.assertEqual(len(doc["accepted gaps"]), 1)

    def test_compressed_formats_without_a_reader_are_gaps(self):
        for name, magic in [
            ("a.7z", b"7z\xbc\xaf\x27\x1c"),
            ("a.rar", b"Rar!\x1a\x07\x01\x00"),
            ("a.zst", b"\x28\xb5\x2f\xfd"),
            ("a.lz4", b"\x04\x22\x4d\x18"),
            ("a.cab", b"MSCF" + bytes(22) + b"\x01\x00" + bytes(8) + bytes(6) + b"\x01\x00"),
        ]:
            self.put(name, magic + bytes(64))
        _, doc, _ = self.scan_json()
        self.assertEqual(len(doc["coverage gaps"]), 5)

    def test_uncompressed_containers_get_a_raw_scan(self):
        u16 = f" {WORD} ".encode("utf-16-le")
        self.put("a.db", b"SQLite format 3\0" + bytes(32) + u16)
        self.put("a.ar", b"!<arch>\n" + f" {WORD} ".encode())
        self.put("a.cpio", b"070701" + bytes(10) + u16)
        self.put("a.doc", b"\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1" + bytes(9) + u16)
        code, doc, _ = self.scan_json()
        self.assertEqual(code, 1)
        self.assertEqual(len(doc["hits"]), 4)
        self.assertEqual(sum("raw bytes and UTF-16 views" in n["why"] for n in doc["notes"]), 4)

    def test_encodings(self):
        escaped = json.dumps({"text": "line one\n" + WORD})  # only visible once the JSON string is decoded
        cases = [
            ("le-bom.json", b"\xff\xfe" + escaped.encode("utf-16-le")),
            ("be.json", (escaped + " " * 40).encode("utf-16-be")),
            ("le.json", (escaped + " " * 40).encode("utf-16-le")),
            ("utf8-bom.json", b"\xef\xbb\xbf" + escaped.encode()),
            ("short16.txt", WORD.encode("utf-16-le")),
            ("cjk16.txt", ("\u65e5" * 300 + " " + WORD).encode("utf-16-le")),
            ("u32le.txt", b"\xff\xfe\0\0" + WORD.encode("utf-32-le")),
            ("u32be.txt", (WORD + " " * 10).encode("utf-32-be")),
            ("nul-first.json", b"\0" + escaped.encode()),
        ]
        for name, data in cases:
            with self.subTest(name):
                for p in self.root.iterdir():
                    p.unlink()
                self.put(name, data)
                self.assertHit(name)

    def test_lone_surrogates_do_not_stop_the_scan(self):
        self.put("a.jsonl", '{"t":"x\\ud83d end"}\n' + json.dumps({"t": WORD}) + "\n")
        self.put("b.json", '{"t":"x\\ud83d"}')
        doc = self.assertHit("a.jsonl")
        self.assertEqual(doc["coverage gaps"], [])

    def test_escapes_in_invalid_json(self):
        self.put("a.json", '{"t": "\\u007a' + WORD[1:] + '",}\n')
        self.assertHit("a.json", "made-up word [JSON escapes decoded]")

    def test_json_inside_json_too_deep_is_a_gap(self):
        inner = {"t": WORD}
        for _ in range(scan.JSON_DEPTH + 1):
            inner = {"s": json.dumps(inner)}
        self.put("deep.json", json.dumps(inner))
        self.assertGap("JSON kept inside JSON strings")

    def test_json_nested_too_deep_to_parse_is_a_gap(self):
        self.put("deep.json", "[" * 100_000 + "]" * 100_000)
        self.assertGap("JSON too deep")

    def test_json_over_the_size_cap_is_a_gap(self):
        self.put("big.json", json.dumps({"t": WORD}))
        old = scan.JSON_MAX
        scan.JSON_MAX = 5
        try:
            self.assertGap("JSON over")
        finally:
            scan.JSON_MAX = old

    def test_base64(self):
        blob = base64.b64encode(f"hidden {WORD} text, padded to be long enough for the check".encode()).decode()
        mime = base64.encodebytes(f"{WORD} wrapped over lines as MIME does it, long enough to wrap twice".encode() * 2)
        self.put("t.json", json.dumps({"image": {"data": blob}}))
        self.put("u.json", json.dumps({"url": "data:text/plain;base64," + blob}))
        self.put("m.json", json.dumps({"mime": mime.decode()}))
        self.put("plain.txt", "attachment:\n" + mime.decode())
        _, doc, _ = self.scan_json()
        names = sorted(Path(h["path"].split("!")[0]).name for h in doc["hits"])
        self.assertEqual(names, ["m.json", "plain.txt", "t.json", "u.json"])
        self.assertTrue(all("!base64-" in h["path"] for h in doc["hits"]))

    def test_long_hex_is_not_base64(self):
        self.put("h.json", json.dumps({"sha": "ab" * 32}))
        self.assertClean()

    def test_pdf_flate_streams(self):
        stream = zlib.compress(f"BT ({WORD}) Tj ET".encode())
        pdf = b"%PDF-1.4\n1 0 obj << /Filter /FlateDecode >>\nstream\n" + stream + b"\nendstream\nendobj\n%%EOF"
        self.put("doc.pdf", pdf)
        doc = self.assertHit("doc.pdf")
        self.assertTrue(doc["hits"][0]["path"].endswith("!stream-1"))

    def test_pdf_streams_take_linear_time(self):
        self.put("p.pdf", b"%PDF-1.4\n" + b"stream\n" * 9000 + b"endstream\n")
        start = time.monotonic()
        self.scan_json()  # streams without a dictionary are gaps; what matters here is the time
        self.assertLess(time.monotonic() - start, 10)
        self.put("q.pdf", b"%PDF-1.4\n" + b"stream\nx" * (scan.PDF_STREAMS_MAX + 1) + b"endstream\n")
        self.assertGap("PDF streams")

    def nested(self):
        inner = zip_of({"deep/f.txt": WORD})
        tar = io.BytesIO()
        with tarfile.open(fileobj=tar, mode="w") as t:
            info = tarfile.TarInfo("inner.zip")
            info.size = len(inner)
            t.addfile(info, io.BytesIO(inner))
        return zip_of({"x.tar.gz": gzip.compress(tar.getvalue())})

    def test_nested_archives(self):
        self.put("outer.zip", self.nested())
        doc = self.assertHit("outer.zip")
        self.assertTrue(doc["hits"][0]["path"].endswith("deep/f.txt"))

    def test_nesting_depth_limit_is_a_gap(self):
        self.put("outer.zip", self.nested())
        old = scan.NEST_DEPTH
        scan.NEST_DEPTH = 2
        try:
            self.assertGap("nested 2 levels deep")
        finally:
            scan.NEST_DEPTH = old

    def test_zip_after_other_data(self):
        self.put("setup.exe", b"MZ" + bytes(100) + zip_of({"f.txt": WORD}))
        self.assertHit("setup.exe!f.txt")

    def test_damaged_and_unsupported_zips_are_gaps(self):
        b = bytearray(zip_of({"f.txt": WORD}))
        i = b.find(b"PK\x01\x02")
        b[i + 6] = 80  # needs version 8.0 to extract
        self.put("a.zip", bytes(b))
        self.put("ok.txt", WORD)
        doc = self.assertGap("NotImplementedError")
        self.assertTrue(any(h["path"].endswith("ok.txt") for h in doc["hits"]))

    def test_concatenated_streams(self):
        for name, comp in (("a.gz", gzip.compress), ("a.bz", bz2.compress), ("a.x", lzma.compress)):
            self.put(name, comp(b"clean ") + comp(f" {WORD} ".encode()))
        _, doc, _ = self.scan_json()
        unpacked = {Path(h["path"]).name for h in doc["hits"] if "!" in h["path"]}
        self.assertEqual(unpacked, {"a.gz!gunzipped", "a.bz!bunzipped", "a.x!unxzed"})

    def test_member_size_limit_is_a_gap(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
            z.writestr("big.txt", "a" * 5000)
        self.put("b.zip", buf.getvalue())
        old = scan.MEMBER_MAX
        scan.MEMBER_MAX = 1000
        try:
            self.assertGap("not opened")
        finally:
            scan.MEMBER_MAX = old

    def test_large_files_are_chunked_without_double_counting(self):
        self.put("big.log", "".join(f"{'x ' * 700}{WORD} " for _ in range(10)))
        olds = scan.FULL_MAX, scan.CHUNK
        scan.FULL_MAX, scan.CHUNK = 1000, 1000
        try:
            code, doc, _ = self.scan_json()
        finally:
            scan.FULL_MAX, scan.CHUNK = olds
        self.assertEqual(code, 1)
        self.assertEqual(doc["hits"][0]["count"], 10)
        self.assertIn("raw markers only", self.gaps(doc)[0])

    def test_stream_counter_counts_each_match_once(self):
        m = scan.load_markers(json.dumps({"markers": MARKERS}).encode())
        data = ("x " * 50 + WORD + " " + LISTING_LINE) * 400
        whole = scan.count_view(m, data.encode())
        for size in (7, 100, 5000):
            c = scan.StreamCounter(m)
            b = data.encode()
            for i in range(0, len(b), size):
                c.feed(b[i : i + size])
            c.feed(b"", final=True)
            self.assertEqual(c.counts, whole, size)


class AllowList(Base):
    def entry(self, data, markers_sha):
        return {
            "sha256": hashlib.sha256(data).hexdigest(),
            "path": "kept.txt",
            "reason": "reviewed",
            "markers_sha256": markers_sha,
        }

    def test_allowed_under_current_markers(self):
        data = f"{WORD} kept on purpose".encode()
        self.put("kept.txt", data)
        self.write_data(MARKERS, allow=[self.entry(data, self.markers_sha())])
        doc = self.assertClean()
        self.assertEqual(doc["allowed"][0]["how"], "allowed")

    def test_rechecked_when_markers_changed_until_renewed(self):
        data = f"{WORD} kept on purpose".encode()
        self.put("kept.txt", data)
        self.write_data(MARKERS, allow=[self.entry(data, "0" * 64)])
        code, doc, _ = self.scan_json()
        self.assertEqual(code, 1)
        self.assertIn(scan.RECHECK, doc["hits"][0]["marker"])
        sha = hashlib.sha256(data).hexdigest()
        code, out, _ = run("allow-renew", sha, "--data", str(self.data))
        self.assertEqual(code, 0)
        self.assertIn("renewed 1", out)
        self.assertClean()

    def test_renew_touches_only_named_entries(self):
        a, b = b"first kept file", b"second kept file"
        self.write_data(MARKERS, allow=[self.entry(a, ""), {**self.entry(b, ""), "path": "other.txt"}])
        self.assertEqual(run("allow-renew", hashlib.sha256(a).hexdigest(), "--data", str(self.data))[0], 0)
        entries = json.loads((self.data / "allow.json").read_text())["entries"]
        self.assertEqual([e["markers_sha256"] for e in entries], [self.markers_sha(), ""])
        code, _, err = run("allow-renew", "f" * 64, "--data", str(self.data))
        self.assertEqual(code, 2)
        self.assertIn("not on the allow list", err)

    def test_rechecked_and_clean_stays_allowed(self):
        self.put("kept.txt", self.old_copy)
        self.write_data(MARKERS, allow=[self.entry(self.old_copy, "")])
        doc = self.assertClean()
        self.assertIn("re-checked", doc["allowed"][0]["how"])

    def test_bad_allow_entries(self):
        good = self.entry(b"x", "")
        for entries in (
            [{"sha256": "xyz", "path": "p", "reason": "r"}],
            [{**good, "path": ""}],
            [{**good, "reason": " "}],
            [good, {**good, "path": "again"}],
        ):
            self.write_data(MARKERS, allow=entries)
            self.assertEqual(run("scan", str(self.root), "--data", str(self.data))[0], 2, entries)


if __name__ == "__main__":
    unittest.main()
