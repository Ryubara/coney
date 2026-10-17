"""Tests for the implementer logs (implog.py), on made-up session events only."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import implog


def tool(name, **args):
    return {"type": "assistant", "message": {"content": [{"type": "tool_use", "name": name, "input": args}]}}


class Describe(unittest.TestCase):
    def test_events(self):
        cases = [
            ({"type": "system", "subtype": "init", "model": "m", "cwd": "/w"}, "start", "model m in /w"),
            (tool("Read", file_path="/w/a.cpp"), "Read", "/w/a.cpp"),
            (tool("Bash", command="ctest", description="run the tests"), "Bash", "run the tests (ctest)"),
            ({"type": "assistant", "message": {"content": "plain text"}}, "text", "plain text"),
            ({"type": "user", "message": {"content": [{"type": "tool_result", "content": "a\nb", "is_error": True}]}},
             "error", "2 lines: a"),
            ({"type": "result", "num_turns": 2, "duration_ms": 3000}, "done", "ok after 2 turns, 3 s"),
        ]  # fmt: skip
        for event, kind, summary in cases:
            with self.subTest(kind):
                [(_, k, s)] = implog.describe(dict(event, timestamp="2026-10-17T01:02:03.456Z"))
                self.assertEqual((k, s), (kind, summary))
        self.assertEqual(implog.describe({"type": "user", "message": {"content": "the prompt"}}), [])
        self.assertEqual(implog.describe("x"), [])

    def test_one_line_cuts_long_text(self):
        self.assertEqual(implog.one_line("a\n  b"), "a b")
        self.assertEqual(len(implog.one_line("x" * 500)), implog.WIDTH)


class Files(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_live_log_and_read_log(self):
        live = implog.LiveLog(self.root, "impl-2", {"task": "t.md", "branch": "b", "started": "2026-10-17T01:00:00Z"})
        live.feed(json.dumps(tool("Edit", file_path="engine/a.cpp")).encode())
        live.feed(b"some plain output")
        live.feed(json.dumps({"type": "result", "result": "Finished.", "num_turns": 1, "duration_ms": 1}).encode())
        self.assertEqual(live.final, "Finished.")
        live.close(ended="2026-10-17T01:05:00Z")
        older = implog.LiveLog(self.root, "impl-1", {"started": "2026-10-16T00:00:00Z"})
        older.close()
        doc = implog.read_log(self.root)
        self.assertEqual(doc["shown"], "impl-2")  # started last
        self.assertEqual([s["name"] for s in doc["implementers"]], ["impl-1", "impl-2"])
        self.assertEqual(len(doc["lines"]), 3)
        self.assertIn("Edit     engine/a.cpp", doc["lines"][0])
        self.assertIn("output   some plain output", doc["lines"][1])
        self.assertEqual(implog.read_log(self.root, "impl-2", tail=1)["lines"][0][9:13], "done")
        none = {"implementers": [], "shown": None, "lines": []}
        self.assertEqual(implog.read_log(self.root / "none"), {**none, "readable": True})

    def test_a_live_folder_this_account_may_not_read(self):
        implog.LiveLog(self.root, "impl-1", {"started": "2026-10-17T01:00:00Z"}).close()
        with mock.patch.object(implog.os, "listdir", side_effect=PermissionError):
            doc = implog.read_log(self.root)
        self.assertEqual(doc, {"implementers": [], "shown": None, "lines": [], "readable": False})

    def test_transcripts(self):
        session = self.root / "projects" / "-work" / "abc.jsonl"
        session.parent.mkdir(parents=True)
        events = [tool("Write", file_path="engine/`b`.cpp"), {"type": "summary"}]
        session.write_text("\n".join(json.dumps(e) for e in events) + "\nnot json\n", encoding="utf-8")
        self.assertEqual(implog.write_transcripts(self.root), 1)
        text = (self.root / "transcripts" / "abc.md").read_text(encoding="utf-8")
        self.assertTrue(text.startswith("# abc\n"))
        self.assertIn("**Write** engine/'b'.cpp", text)


if __name__ == "__main__":
    unittest.main()
