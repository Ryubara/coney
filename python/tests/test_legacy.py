"""Tests for the legacy-copy scan of `zones check --mode full`, on made-up trees and a made-up hash list."""

import hashlib
import io
import json
import os
import sys
import unittest
import zipfile
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
sys.path.insert(0, str(Path(__file__).resolve().parent))

from test_zones import Base

from coney_tools import legacy, roles, workspace, zones

OLD_A = b"made-up old file A, line of text\n" * 20
OLD_B = b"made-up old file B: another line\n" * 20


def zipped(name, data):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(name, data)
    return buf.getvalue()


class Tree(Base):
    """An analysis zone with copies of two made-up old files: one in notes, one inside a zip in drafts, and copies in
    the quarantine and the game folder, which are skipped."""

    def setUp(self):
        super().setUp()
        ana = self.r["ana"]
        self.files = {
            "notes/copy.bin": OLD_A,
            "notes/fresh.txt": b"new notes in the analyst's own words\n" * 10,
            "drafts/bundle.zip": zipped("inner/old.txt", OLD_B),
            "drafts/small.txt": b"short",
            "import/sweep/batch-1/old.bin": OLD_A,
            "game/data.bin": OLD_A,
        }
        for rel, data in self.files.items():
            (ana / rel).parent.mkdir(parents=True, exist_ok=True)
            (ana / rel).write_bytes(data)
        self.hashes = self.r["pub"] / "plan" / "old-file-hashes.txt"
        self.hashes.parent.mkdir(parents=True)
        self.write_hashes(OLD_A, OLD_B)
        self.cfg = {
            "root": "analyst:",
            "hashes": "public:plan\\old-file-hashes.txt",
            "cache": "machine:state\\legacy-cache.json",
            "skip": [
                {"path": "analyst:import\\sweep", "why": "the research sweep's quarantine"},
                {"path": "analyst:game", "why": "the game's files"},
            ],
            "budget_seconds": 600,
        }
        self.cache = self.r["mach"] / "state" / "legacy-cache.json"

    def write_hashes(self, *blobs):
        self.hashes.write_text("".join(hashlib.sha256(b).hexdigest() + "\n" for b in blobs), encoding="ascii")

    def ws(self, **over):
        return self.load(legacy_scan={**self.cfg, **over})

    def run_scan(self, ws=None, **kw):
        ws = ws or self.ws()
        return legacy.run(ws.legacy_scan, ws.resolve, jobs=1, **kw)


class Scan(Tree):
    def test_finds_copies_outside_the_skipped_folders(self):
        res = self.run_scan()
        ana = self.r["ana"]
        self.assertEqual(res.hits, {str(ana / "notes"): 1, str(ana / "drafts"): 1})
        self.assertEqual((res.files, res.scanned, res.cached, res.left), (4, 4, 0, 0))
        self.assertEqual(
            res.skipped,
            [
                (os.path.join("import", "sweep"), "the research sweep's quarantine"),
                ("game", "the game's files"),
            ],
        )

    def test_cache_reuses_verdicts_and_names_no_file(self):
        self.run_scan()
        res = self.run_scan()
        self.assertEqual((res.scanned, res.cached), (0, 4))
        self.assertEqual(len(res.hits), 2)
        text = self.cache.read_text(encoding="utf-8")
        for name in ("copy.bin", "fresh.txt", "bundle.zip", "small.txt", str(self.r["ana"])):
            self.assertNotIn(name, text)
        # a changed file is scanned again; a removed copy drops out
        (self.r["ana"] / "notes" / "fresh.txt").write_bytes(b"longer notes, rewritten\n" * 12)
        (self.r["ana"] / "drafts" / "bundle.zip").unlink()
        res = self.run_scan()
        self.assertEqual((res.files, res.scanned, res.cached), (3, 1, 2))
        self.assertEqual(res.hits, {str(self.r["ana"] / "notes"): 1})
        self.assertEqual(len(json.loads(self.cache.read_text())["files"]), 3)

    def test_new_hash_list_starts_over(self):
        self.run_scan()
        self.write_hashes(OLD_B)
        res = self.run_scan()
        self.assertEqual((res.scanned, res.cached), (4, 0))
        self.assertEqual(res.hits, {str(self.r["ana"] / "drafts"): 1})

    def test_budget_leaves_the_rest_for_the_next_run(self):
        ticks = iter(range(0, 1000, 2))
        with mock.patch.object(legacy, "CHUNK_FILES", 1):
            res = self.run_scan(self.ws(budget_seconds=5), clock=lambda: next(ticks))
            self.assertEqual((res.scanned, res.left), (2, 2))
            res = self.run_scan(self.ws(budget_seconds=5), clock=lambda: next(ticks))
            self.assertEqual((res.scanned, res.cached, res.left), (2, 2, 0))
        self.assertEqual(len(res.hits), 2)

    def test_links_are_not_followed(self):
        target = self.tmp / "outside"
        target.mkdir()
        (target / "old.bin").write_bytes(OLD_A)
        try:
            os.symlink(target, self.r["ana"] / "link", target_is_directory=True)
        except OSError:
            try:
                import _winapi  # a junction needs no special right

                _winapi.CreateJunction(str(target), str(self.r["ana"] / "link"))
            except (ImportError, OSError):
                self.skipTest("this account cannot make links")
        res = self.run_scan()
        self.assertEqual(res.links, 1)
        self.assertNotIn(str(self.r["ana"] / "link"), res.hits)

    def test_bad_hash_list_and_missing_root(self):
        self.hashes.write_text("not a hash\n")
        with self.assertRaisesRegex(legacy.LegacyError, "not 64 hex digits"):
            self.run_scan()
        self.hashes.unlink()
        with self.assertRaisesRegex(legacy.LegacyError, "unreadable"):
            self.run_scan()
        self.write_hashes(OLD_A)
        res = self.run_scan(self.ws(root="analyst:absent"))
        self.assertEqual((res.files, res.hits), (0, {}))


class Checks(Tree):
    def test_copies_fail_as_legacy_not_breach(self):
        ws = self.ws()
        checks, warnings = zones.legacy_checks(ws, jobs=1)
        failed = [c for c in checks if not c.ok]
        self.assertEqual([c.path for c in failed], [str(self.r["ana"] / "drafts"), str(self.r["ana"] / "notes")])
        self.assertTrue(all(c.legacy and not c.widens and c.what == "legacy copy" for c in failed))
        self.assertEqual(failed[0].got, "1 exact copy")
        summary = checks[0]
        self.assertTrue(summary.ok)
        self.assertIn("2 exact copies in 2 folders; 4 files (4 scanned, 0 cached)", summary.got)
        self.assertIn("skipped: ", summary.got)
        self.assertIn("game (the game's files)", summary.got)
        self.assertEqual(warnings, [])
        doc = zones.result("full", "SYSTEM", checks, warnings)
        self.assertFalse(doc["breach"])
        self.assertEqual(doc["legacy"], [c.path for c in failed])
        self.assertEqual([c.get("legacy", False) for c in doc["checks"]], [False, True, True])
        text = zones.text_report(doc)
        self.assertIn("FAIL: 2 of 3 checks failed (legacy copy", text)
        self.assertNotIn("copy.bin", text)
        # the result is written; no breach flag
        zones.write_state(ws, doc, checks, may_clear=True)
        self.assertTrue(ws.where("check_result").exists())
        self.assertFalse(ws.where("breach_flag").exists())

    def test_zone_status_shows_the_legacy_folders(self):
        ws = self.ws()
        checks, warnings = zones.legacy_checks(ws, jobs=1)
        zones.write_state(ws, zones.result("full", "SYSTEM", checks, warnings), checks, may_clear=True)
        w = roles.walls(ws)
        self.assertEqual((w["breach"], len(w["legacy_folders"])), (False, 2))
        self.assertEqual((w["passed"], w["checks"]), (1, 1))  # the walls only: the scan's summary

    def test_clean_zone_passes(self):
        self.write_hashes(b"a made-up file that is nowhere in the tree\n" * 10)
        checks, _ = zones.legacy_checks(self.ws(), jobs=1)
        self.assertEqual([(c.what, c.ok) for c in checks], [("legacy scan", True)])
        self.assertTrue(checks[0].got.startswith("none; 4 files"))

    def test_warnings_for_left_files(self):
        ws = self.ws()
        res = legacy.Result(str(self.r["ana"]), files=10, scanned=4, left=6, links=1)
        checks, warnings = zones.legacy_checks(ws, run=lambda *a: res)
        self.assertIn("6 left for the next run", checks[0].got)
        self.assertEqual(
            warnings, ["legacy scan: 6 files left for the next run (budget spent)", "legacy scan: 1 link not followed"]
        )

    def test_unconfigured_or_unusable_fails_without_legacy(self):
        checks, _ = zones.legacy_checks(self.load())
        self.assertEqual([(c.what, c.got, c.ok, c.legacy) for c in checks], [("legacy copy", "missing", False, False)])
        self.hashes.unlink()
        checks, _ = zones.legacy_checks(self.ws(), jobs=1)
        self.assertFalse(checks[0].ok or checks[0].legacy)
        self.assertIn("unreadable", checks[0].got)


class Config(Tree):
    def test_validation(self):
        cases = [
            ({"root": "nowhere:"}, "unknown role"),
            ({"hashes": "plain-name"}, "not '<role>:<path>'"),
            ({"skip": {}}, "skip must be a list"),
            ({"skip": [{"path": "analyst:game"}]}, r"skip\[0\] needs"),
            ({"skip": [{"path": "analyst:..\\x", "why": "w"}]}, "stay under"),
            ({"budget_seconds": 0}, "budget_seconds"),
            ({"budget_seconds": True}, "budget_seconds"),
        ]
        for over, msg in cases:
            with self.subTest(msg=msg), self.assertRaisesRegex(workspace.WorkspaceError, msg):
                self.ws(**over)
        with self.assertRaisesRegex(workspace.WorkspaceError, "'legacy_scan' must be an object"):
            self.load(legacy_scan=[])
        self.assertIsNone(self.load().legacy_scan)


if __name__ == "__main__":
    unittest.main()


class GameAndAllowed(Tree):
    """Hashes of the game's own files (the `original` folders) and of allowed files leave the list."""

    def test_a_file_also_in_an_original_folder_is_the_games(self):
        res = self.run_scan(self.ws(original=[{"path": "analyst:game", "why": "the game's files"}]))
        self.assertEqual(res.hits, {str(self.r["ana"] / "drafts"): 1})

    def test_an_allowed_file_is_not_a_copy(self):
        allow = self.r["pub"] / "plan" / "allow.json"
        entry = {"sha256": hashlib.sha256(OLD_B).hexdigest(), "path": "x", "reason": "kept", "markers_sha256": ""}
        allow.write_text(json.dumps({"format": 1, "entries": [entry]}), encoding="utf-8")
        res = self.run_scan(self.ws(allow="public:plan\\allow.json"))
        self.assertEqual(res.hits, {str(self.r["ana"] / "notes"): 1})

    def test_a_new_original_file_rescans(self):
        self.run_scan()
        (self.r["ana"] / "game" / "more.bin").write_bytes(OLD_B)
        res = self.run_scan(self.ws(original=[{"path": "analyst:game", "why": "the game's files"}]))
        self.assertEqual(res.hits, {})
        self.assertEqual(res.cached, 0)

    def test_original_needs_a_why(self):
        with self.assertRaises(workspace.WorkspaceError):
            self.ws(original=[{"path": "analyst:game"}])
