"""Tests for cleanroom scan with worker processes (--jobs), on synthetic folders with made-up markers only."""

import base64
import contextlib
import gzip
import hashlib
import io
import json
import multiprocessing
import os
import re
import stat
import sys
import tarfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_scan as ts
from test_scan import GIT, LISTING_LINE, OLD_TEXT, WORD, git, make_repo, zip_of
from test_scan_review2 import Patched

from coney_tools.cleanroom import scan

PNG = b"\x89PNG\r\n\x1a\n" + bytes(64)


def failing_batch(tasks, bid=0):
    """A worker's batch that stops the worker on one file and raises on another (runs in the worker process)."""
    for task in tasks:
        name = os.path.basename(str(task[1]))
        if name.startswith("crash-me"):
            os._exit(3)
        if name.startswith("raise-me"):
            raise RuntimeError("a made-up failure")
    return scan.worker_batch(tasks, bid)


@contextlib.contextmanager
def unreadable(path):
    """Holds a file so the scan cannot read it: open with no sharing on Windows, no permissions elsewhere."""
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateFileW.restype = wintypes.HANDLE
        k32.CreateFileW.argtypes = (
            wintypes.LPCWSTR,
            wintypes.DWORD,
            wintypes.DWORD,
            ctypes.c_void_p,
            wintypes.DWORD,
            wintypes.DWORD,
            wintypes.HANDLE,
        )
        h = k32.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)  # GENERIC_READ, no sharing, existing
        if h in (None, ctypes.c_void_p(-1).value):
            raise OSError("could not hold the file")
        try:
            yield
        finally:
            k32.CloseHandle(h)
    else:
        path.chmod(0)
        try:
            if os.access(path, os.R_OK):
                raise unittest.SkipTest("files stay readable here (running as root?)")
            yield
        finally:
            path.chmod(stat.S_IRUSR | stat.S_IWUSR)


def left_running(before=()):
    """The worker processes still alive that were not in `before`, after waiting up to ten seconds for them to end
    (a loaded runner can be slow to reap a worker that was stopped)."""
    for _ in range(100):
        procs = [p for p in multiprocessing.active_children() if p not in before]
        if not procs:
            return []
        procs[0].join(0.1)
    return [p for p in multiprocessing.active_children() if p not in before]


class Parallel(ts.Base):
    def setUp(self):
        super().setUp()
        # Every test leaves no worker of its own behind, so a leak fails in the test that made it, not in a later one.
        before = multiprocessing.active_children()
        self.addCleanup(lambda: self.assertEqual(left_running(before), [], "a worker outlived the scan"))

    def fixture(self):
        """A tree with a hit of every kind, archives, a git repository, an allowed file, excludes and excepts."""
        for i in range(120):
            self.put(f"plain/d{i % 7}/f{i}.txt", f"nothing to see in file {i}\n" * 3)
        self.put("hits/word.txt", f"a line with {WORD} in it\n")
        self.put("hits/listing.txt", LISTING_LINE * 4)
        self.put("hits/old-copy.bin", self.old_copy)
        self.put("hits/near.txt", "a new first line\n" + OLD_TEXT)
        self.put(f"hits/{WORD}.txt", "only the name\n")
        self.put("hits/utf16.txt", f"﻿{WORD}\n".encode("utf-16-le"))
        self.put("hits/doc.json", json.dumps({"k": base64.b64encode(f"hidden {WORD} text".encode()).decode()}))
        self.put("archives/a.zip", zip_of({"in/a.txt": WORD, "in/b.zip": zip_of({"deep.txt": WORD})}))
        tar = io.BytesIO()
        with tarfile.open(fileobj=tar, mode="w") as t:
            body = f"{WORD}\n".encode()
            info = tarfile.TarInfo("t/x.txt")
            info.size = len(body)
            t.addfile(info, io.BytesIO(body))
        self.put("archives/a.tar.gz", gzip.compress(tar.getvalue()))
        self.put("archives/bad.zip", b"PK\x03\x04 not really a zip")
        self.put("images/p.png", PNG)
        self.put("excluded/.venv/f.txt", WORD)
        self.put("excepted/f.txt", WORD)
        kept = self.put("allowed/kept.txt", f"kept on purpose: {WORD}\n")
        self.write_data(
            ts.MARKERS,
            allow=[
                {
                    "sha256": hashlib.sha256(kept.read_bytes()).hexdigest(),
                    "path": "allowed/kept.txt",
                    "reason": "made up",
                    "markers_sha256": hashlib.sha256(json.dumps({"markers": ts.MARKERS}).encode("utf-8")).hexdigest(),
                }
            ],
        )
        if GIT:
            repo = make_repo(self.root / "repo", f"tracked {WORD}\n" + OLD_TEXT)
            for i in range(30):
                (repo / f"g{i}.txt").write_text(f"history file {i}\n" * 4)
            git(repo, "add", ".")
            git(repo, "commit", "-q", "-m", "more")
            git(repo, "repack", "-a", "-d", "-q")
            (repo / "late.txt").write_text(f"loose {WORD}\n")
            git(repo, "add", "late.txt")
            git(repo, "commit", "-q", "-m", "late")
        return self.put("locked/held.txt", "cannot be read\n")

    def scan_both(self, *extra):
        out = {}
        for jobs in ("1", "4"):
            args = (str(self.root), str(self.tmp / "missing-root"), "--data", str(self.data), *extra)
            code_t, text, err_t = ts.run("scan", *args, "--jobs", jobs)
            code_j, js, err_j = ts.run("scan", *args, "--json", "--jobs", jobs)
            self.assertEqual((err_t, err_j), ("", ""))
            self.assertEqual(code_t, code_j)
            out[jobs] = (code_t, text, js)
        return out

    def test_jobs_1_and_jobs_4_give_the_same_report(self):
        held = self.fixture()
        extra = ("--exclude", ".venv", "--except", str(self.root / "excepted"), "--list-images")
        with unreadable(held):
            out = self.scan_both(*extra)
        self.assertEqual(out["1"], out["4"])
        code, text, js = out["4"]
        self.assertEqual(code, 1)
        doc = json.loads(js)
        why = {g["path"]: g["why"] for g in doc["coverage gaps"]}
        self.assertEqual(why.get(str(held)), "unreadable file", text)
        markers = {h["marker"] for h in doc["hits"]}
        for m in ("made-up word", "made-up listing", scan.EXACT, scan.NEAR, "made-up word [name]"):
            self.assertTrue(any(x.startswith(m) for x in markers), (m, text))
        paths = [h["path"].replace("\\", "/") for h in doc["hits"]]
        for part in ("a.zip!in/a.txt", "b.zip!deep.txt", "a.tar.gz!gunzipped!t/x.txt", "doc.json!base64-"):
            self.assertTrue(any(part in p for p in paths), (part, text))
        self.assertTrue(doc["exceptions"] and doc["excluded folders"] and doc["allowed"] and doc["images"])
        self.assertTrue(doc["missing roots"])
        if GIT:
            self.assertGreater(doc["git objects scanned"], 30)
            self.assertTrue(any("/repo/.git!" in p for p in paths), text)
        self.assertNotIn(WORD + " ", text.lower())  # never the matched text

    def test_a_clean_tree_exits_0_with_any_number_of_jobs(self):
        for i in range(40):
            self.put(f"d{i % 3}/f{i}.txt", f"clean {i}\n")
        for jobs in ("1", "3"):
            code, _, err = ts.run("scan", str(self.root), "--data", str(self.data), "--jobs", jobs)
            self.assertEqual((code, err), (0, ""))

    def test_a_worker_failure_is_a_gap_not_a_lost_file(self):
        for i in range(40):
            self.put(f"d/f{i}.txt", f"{WORD} {i}\n")
        crash = self.put("d/crash-me.txt", "fine text\n")
        boom = self.put("d/raise-me.txt", "fine text\n")
        with Patched(WORKER_BATCH=failing_batch):
            code, out, err = ts.run("scan", str(self.root), "--data", str(self.data), "--json", "--jobs", "4")
        self.assertEqual((code, err), (1, ""), out)
        doc = json.loads(out)
        why = {g["path"]: g["why"] for g in doc["coverage gaps"]}
        self.assertEqual(why.get(str(crash)), "could not be checked: BrokenProcessPool", out)
        self.assertEqual(why.get(str(boom)), "could not be checked: RuntimeError", out)
        self.assertEqual(len(doc["hits"]), 40, out)  # every other file in the failed batches still scanned
        self.assertEqual(doc["files scanned"], 40)
        self.assertEqual(multiprocessing.active_children(), [])

    def test_ctrl_c_stops_the_workers(self):
        for i in range(200):
            self.put(f"d{i % 20}/f{i}.txt", f"text {i}\n")
        real, calls = scan.is_git_dir, []
        before = multiprocessing.active_children()  # workers of an earlier module's scan are not this test's

        def interrupt(path):
            calls.append(path)
            if len(calls) == 15:
                raise KeyboardInterrupt
            return real(path)

        with Patched(is_git_dir=interrupt, BATCH_TASKS=2):
            code, out, err = ts.run("scan", str(self.root), "--data", str(self.data), "--jobs", "4")
        self.assertEqual((code, out), (2, ""))
        self.assertIn("stopped", err)
        self.assertEqual([p for p in multiprocessing.active_children() if p not in before], [])

    def test_progress_lines_hold_counts_and_root_names_only(self):
        self.put(f"a/{WORD}.txt", f"{WORD}\n")
        for i in range(5):
            self.put(f"a/f{i}.txt", f"{WORD} {i}\n")
        for jobs in ("1", "2"):
            with Patched(PROGRESS_EVERY=0):
                code, _, err = ts.run("scan", str(self.root), "--data", str(self.data), "--jobs", jobs)
            self.assertEqual(code, 1)
            lines = err.splitlines()
            self.assertTrue(lines, jobs)
            line = re.compile(
                r"scanned [\d,]+ files(, [\d,]+ git objects)? \(\d+\.\d GB\), [\d,]+ hits so far, now in (.*)"
            )
            for x in lines:
                m = line.fullmatch(x)
                self.assertTrue(m, x)
                self.assertEqual(m.group(2), str(self.root))
                self.assertNotIn(WORD, x.lower())


if __name__ == "__main__":
    unittest.main()
