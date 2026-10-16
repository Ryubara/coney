"""Tests for `cleanroom disclosure-check`. Made-up wording only: invented commits, pull request bodies and names."""

import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools.cleanroom import disclosure
from coney_tools.cli import main


def run(*args, stdin=b""):
    out, err = io.StringIO(), io.StringIO()
    data = stdin.encode("utf-8") if isinstance(stdin, str) else stdin
    with (
        mock.patch.object(sys, "stdin", mock.Mock(buffer=io.BytesIO(data))),
        redirect_stdout(out),
        redirect_stderr(err),
    ):
        code = main(["cleanroom", "disclosure-check", *args])
    return code, out.getvalue(), err.getvalue()


def rules(text):
    return {(f.rule, f.line) for f in disclosure.check_text(text)}


NEUTRAL = """Add the frame pacer's step counter

Coney was restarted in October 2026 with a stricter clean-room process.
The breach watch runs every five minutes, and the contamination response is described on the clean-room page.
An exposed container or session is reset, and everything it signed since the exposure
is checked again. After the breach is contained, the wall is fixed and tested.
Open a private incident record. Anything not independent is removed from history.
Remove an unused option from the tool. Drop the old flag. Rewrite the README's build section.
Fix a memory leak in the asset cache; the memory leak was fixed by freeing each buffer.
Delete temporary files after the build. Clean up the test helpers.
Released on 2026-10-14 with the new launcher; the README covers what changed in the menus.
The original's behaviour: a knocked-down character gets up after 2.5 s unless hit again.
"""


class Rules(unittest.TestCase):
    def test_neutral_process_and_engineering_wording_passes(self):
        self.assertEqual(disclosure.check_text(NEUTRAL), [])

    def test_each_rule_refuses_its_wording(self):
        cases = [
            ("incident", "The leak was found in a review of the pages."),
            ("incident", "A breach occurred in the analysis zone."),
            ("incident", "Two pages were contaminated."),
            ("incident", "Since the leak, every page is reviewed twice."),
            ("incident", "This fixes a clean-room violation."),
            ("incident", "An agent violated the clean room."),
            ("incident", "Replace the tainted helpers."),
            ("slip", "A dump was committed by mistake."),
            ("slip", "Accidentally pushed a private note."),
            ("slip", "Some text slipped through review."),
            ("slip", "These files should never have been published."),
            ("removal", "Remove the remnants of the first engine."),
            ("removal", "Scrub the loader module."),
            ("removal", "We rewrote the git history to drop them."),
            ("removal", "Ran filter-repo over main."),
            ("removal", "Removed the decompiled helpers from the tree."),
            ("removal", "The repository was taken down."),
            ("old-repo", "Ported from the old repository."),
            ("old-repo", "Matches the previous codebase's layout."),
            ("old-repo", "Unlike old Coney, the menus are data."),
            ("problem-date", "On 2026-09-30 a problem in the page gate let a page through."),
            ("problem-date", "Files removed on 3 October are back."),
            ("problem-date", "The mistake from last week is fixed."),
        ]
        for rule, text in cases:
            with self.subTest(text=text):
                self.assertIn(rule, {f.rule for f in disclosure.check_text(text)})

    def test_wording_split_across_lines_is_one_sentence(self):
        self.assertEqual(rules("intro line\nthe breach\nwas found in the pages\n"), {("incident", 2)})
        # the same paragraph split, read as the process: "since the exposure is checked" is not a report
        self.assertEqual(rules("everything since the exposure\nis checked again.\n"), set())

    def test_line_numbers_point_at_the_wording(self):
        text = "Add a counter\n\nBody line one.\nRemove the remnants here.\n\nPorted from the old repo.\n"
        self.assertEqual(rules(text), {("removal", 4), ("old-repo", 6)})

    def test_markup_and_look_alikes_do_not_hide_wording(self):
        for text in (
            "the le&#97;k was found",
            "the **leak** was found",
            "the le\u200bak was found",
            "the l\u0435ak was found",
        ):
            with self.subTest(text=text):
                self.assertIn("incident", {f.rule for f in disclosure.check_text(text)})

    def test_commit_msg_mode_skips_git_comments_and_the_diff(self):
        text = (
            "Add the counter\n\n# Please enter the commit message. The leak was found\n"
            "# ------------------------ >8 ------------------------\n"
            "diff: Ported from the old repo.\n"
        )
        self.assertEqual(disclosure.check_text(text, commit_msg=True), [])
        self.assertTrue(disclosure.check_text(text))

    def test_text_that_cannot_be_checked_does_not_pass(self):
        self.assertEqual([f.rule for f in disclosure.check_bytes(b"\xff\xfe bad")], ["not-text"])
        self.assertEqual([f.rule for f in disclosure.check_bytes(b"a\x00b")], ["not-text"])
        self.assertEqual([f.rule for f in disclosure.check_bytes(b"a" * (disclosure.MAX_TEXT + 1))], ["too-large"])

    def test_rule_ids_are_unique(self):
        ids = [rid for rid, _, _ in disclosure.RULES]
        self.assertEqual(len(ids), len(set(ids)))


class Command(unittest.TestCase):
    def test_rules_lists_every_rule(self):
        code, out, _ = run("--rules")
        self.assertEqual(code, 0)
        for rid, _, _ in disclosure.RULES:
            self.assertIn(rid, out)

    def test_stdin_passes_and_fails_without_echoing_the_text(self):
        self.assertEqual(run("-", stdin=NEUTRAL)[0], 0)
        code, out, _ = run("-", stdin="Fix the menu\n\nThe Quillwort leak was found yesterday.\n")
        self.assertEqual(code, 1)
        self.assertIn("-:3: incident:", out)
        self.assertIn("-:3: problem-date:", out)
        for word in ("Quillwort", "leak was", "yesterday"):
            self.assertNotIn(word, out)

    def test_files_and_json(self):
        with tempfile.TemporaryDirectory() as d:
            good, bad = Path(d, "good.md"), Path(d, "bad.md")
            good.write_text(NEUTRAL, encoding="utf-8")
            bad.write_text("Body\nRemoved the decompiled helpers.\n", encoding="utf-8")
            self.assertEqual(run(str(good))[0], 0)
            code, out, _ = run("--json", str(good), str(bad))
            self.assertEqual(code, 1)
            result = json.loads(out)
            self.assertFalse(result["ok"])
            self.assertEqual([s["ok"] for s in result["sources"]], [True, False])
            self.assertEqual(result["sources"][1]["findings"][0]["rule"], "removal")
            self.assertEqual(result["sources"][1]["findings"][0]["line"], 2)
            self.assertNotIn("decompiled helpers", out)
            self.assertEqual(run(str(Path(d, "missing.md")))[0], 2)

    def test_usage_errors_cannot_run(self):
        self.assertEqual(run()[0], 2)
        self.assertEqual(run("-", "--commits", "HEAD")[0], 2)
        self.assertEqual(run("--commits=--output=x")[0], 2)

    @unittest.skipUnless(shutil.which("git"), "git is not on PATH")
    def test_commits_in_a_range(self):
        with tempfile.TemporaryDirectory() as d:
            env = {
                **os.environ,
                "GIT_AUTHOR_NAME": "Test",
                "GIT_AUTHOR_EMAIL": "test@example.invalid",
                "GIT_COMMITTER_NAME": "Test",
                "GIT_COMMITTER_EMAIL": "test@example.invalid",
                "GIT_CONFIG_NOSYSTEM": "1",
                "GIT_CONFIG_GLOBAL": os.devnull,
            }

            def git(*args):
                subprocess.run(["git", "-C", d, *args], check=True, capture_output=True, env=env)

            git("init", "-q")
            git("commit", "-q", "--allow-empty", "-m", "Start the tree")
            git("commit", "-q", "--allow-empty", "-m", "Add the counter\n\nCounts frames on a fixed step.")
            git("commit", "-q", "--allow-empty", "-m", "Tidy the docs\n\nScrub the old notes.")
            cwd = os.getcwd()
            try:
                os.chdir(d)
                self.assertEqual(run("--commits", "HEAD~2..HEAD~1")[0], 0)
                code, out, _ = run("--commits", "HEAD~2..HEAD")
                self.assertEqual(code, 1)
                self.assertIn(":3: removal:", out)
                self.assertIn("2 texts", out)
                self.assertEqual(run("--commits", "no-such-rev..HEAD")[0], 2)
            finally:
                os.chdir(cwd)


if __name__ == "__main__":
    unittest.main()
