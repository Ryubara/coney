"""Tests for cleanroom scan's per-file time limit (--file-timeout), on synthetic folders with made-up markers only."""

import argparse
import json
import multiprocessing
import os
import re
import sys
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))

import test_scan as ts
from test_scan import WORD
from test_scan_parallel import failing_batch
from test_scan_review2 import Patched

from coney_tools import cli
from coney_tools.cleanroom import scan

SLOW = "slow-me-"  # a file named slow-me-<seconds>.txt takes that long in a worker


def slow_batch(tasks, bid=0):
    """A worker's batch where slow-me-<n>.txt sleeps n seconds before it is scanned (runs in the worker process);
    crash-me and raise-me fail as in failing_batch."""
    if not getattr(scan.Scan, "made_slow", False):
        real = scan.Scan.do

        def do(self, task):
            name = os.path.basename(str(task[1]))
            if name.startswith(SLOW):
                time.sleep(int(name[len(SLOW) :].split(".")[0]))
            return real(self, task)

        scan.Scan.do, scan.Scan.made_slow = do, True
    return failing_batch(tasks, bid)


def timeout_why(limit):
    return f"could not be checked: took longer than {limit} s"


class FileTimeout(ts.Base):
    def tree(self):
        for i in range(40):
            self.put(f"d{i % 4}/f{i}.txt", f"{WORD} {i}\n")
        for i in range(20):
            self.put(f"e/plain{i}.txt", f"nothing here {i}\n")
        self.put("e/bad.zip", b"PK\x03\x04 not really a zip")  # an ordinary gap, kept through the retries

    def scan_doc(self, *extra, jobs="4", batch=4):
        args = ("scan", str(self.root), "--data", str(self.data), "--json", "--jobs", jobs, *extra)
        with Patched(WORKER_BATCH=slow_batch, BATCH_TASKS=batch):
            started = time.monotonic()
            code, out, err = ts.run(*args)
            took = time.monotonic() - started
        self.assertEqual(err, "", out)
        self.assertEqual(multiprocessing.active_children(), [])
        return code, json.loads(out), took

    def without(self, doc, path):
        """The report less the gaps on one path, and that path's gaps."""
        doc = dict(doc)
        gaps = doc["coverage gaps"]
        doc["coverage gaps"] = [g for g in gaps if g["path"] != str(path)]
        return doc, [g["why"] for g in gaps if g["path"] == str(path)]

    def test_a_slow_file_is_a_gap_and_everything_else_is_scanned(self):
        self.tree()
        for jobs in ("4", "1"):
            with self.subTest(jobs=jobs):
                slow = self.put(f"d1/{SLOW}60.txt", f"{WORD}\n")
                with mock.patch.object(scan.Scan, "alone", autospec=True, side_effect=scan.Scan.alone) as alone:
                    code, doc, took = self.scan_doc("--file-timeout", "1", jobs=jobs)
                self.assertLess(took, 40)
                self.assertEqual(alone.call_count, 0)  # the broken pool's batches went whole to a fresh pool
                self.assertEqual(code, 1)
                rest, why = self.without(doc, slow)
                self.assertEqual(why, [timeout_why(1)])
                slow.unlink()
                _, base, _ = self.scan_doc("--file-timeout", "1", jobs=jobs)
                self.assertEqual(rest, base)  # every other file found what it finds with no slow file there
                self.assertEqual(len(base["hits"]), 40)
                self.assertEqual(base["file timeout"], 1)

    def test_the_limit_also_holds_when_a_batch_is_scanned_again_one_file_at_a_time(self):
        self.tree()
        slow = self.put(f"d2/{SLOW}60.txt", f"{WORD}\n")
        crash = self.put("d2/crash-me.txt", "fine text\n")
        # one batch: the crash breaks the pool at once and sends every file to the one-worker pool
        code, doc, took = self.scan_doc("--file-timeout", "1", batch=1000)
        self.assertLess(took, 60)
        self.assertEqual(code, 1)
        why = {g["path"]: g["why"] for g in doc["coverage gaps"]}
        self.assertEqual(why.get(str(slow)), timeout_why(1))
        self.assertEqual(why.get(str(crash)), "could not be checked: BrokenProcessPool")
        self.assertEqual(len(doc["hits"]), 40)

    def test_a_slow_file_alone_in_its_batch_is_a_gap(self):
        # a large file goes to a worker alone; when it is stopped, its batch has nothing left to send again
        self.tree()
        slow = self.put(f"d3/{SLOW}60.txt", f"{WORD}\n")
        code, doc, took = self.scan_doc("--file-timeout", "1", batch=1)
        self.assertLess(took, 40)
        self.assertEqual(code, 1)
        rest, why = self.without(doc, slow)
        self.assertEqual(why, [timeout_why(1)])
        self.assertEqual(len(rest["hits"]), 40)

    def test_file_timeout_0_lets_a_slow_file_finish(self):
        self.tree()
        slow = self.put(f"d1/{SLOW}3.txt", f"{WORD}\n")
        for jobs in ("2", "1"):
            with self.subTest(jobs=jobs):
                code, doc, _ = self.scan_doc("--file-timeout", "0", jobs=jobs)
                self.assertEqual(code, 1)
                self.assertNotIn(str(slow), [g["path"] for g in doc["coverage gaps"]])
                self.assertIn(str(slow), [h["path"] for h in doc["hits"]])
                self.assertEqual(doc["file timeout"], 0)

    def test_the_report_header_records_the_limit(self):
        self.put("a.txt", "clean\n")
        for extra, line in (((), "file timeout: 600 s"), (("--file-timeout", "0"), "file timeout: none")):
            code, out, err = ts.run("scan", str(self.root), "--data", str(self.data), "--jobs", "2", *extra)
            self.assertEqual((code, err), (0, ""), out)
            self.assertIn(line, out.splitlines())

    def test_the_progress_line_gives_the_slowest_file_in_minutes_and_no_path(self):
        self.put(f"a/{SLOW}2.txt", f"{WORD}\n")
        with Patched(PROGRESS_EVERY=0, SLOW_AFTER=0.5, WORKER_BATCH=slow_batch):
            code, _, err = ts.run("scan", str(self.root), "--data", str(self.data), "--jobs", "2")
        self.assertEqual(code, 1)
        line = re.compile(
            r"scanned [\d,]+ files \(\d+\.\d GB\), [\d,]+ hits so far(, slowest file running \d+ min)?, now in (.*)"
        )
        slow = 0
        for x in err.splitlines():
            m = line.fullmatch(x)
            self.assertTrue(m, x)
            self.assertEqual(m.group(2), str(self.root))
            slow += bool(m.group(1))
            self.assertNotIn("slow-me", x)
        self.assertGreater(slow, 0, err)

    def test_only_whole_seconds_0_or_more_are_taken(self):
        self.assertEqual((cli.seconds_arg("0"), cli.seconds_arg("90")), (0, 90))
        for bad in ("-1", "1.5", "soon"):
            with self.assertRaises(argparse.ArgumentTypeError):
                cli.seconds_arg(bad)


if __name__ == "__main__":
    unittest.main()
