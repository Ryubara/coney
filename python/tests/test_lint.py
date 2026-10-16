"""Tests for markdownlint in the page gate (lint.py), on synthetic pages and config files only.

The real markdownlint needs the network on its first run, so these tests use a stand-in runner; one optional test
runs the real one (set CONEY_TEST_NPX=1).
"""

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from coney_tools import lint, signing
from coney_tools.cleanroom import scan

WORD = "Zanzibarquux"  # only ever in page text
GIT = scan.find_program("git")
HOOKS = """repos:
  - repo: https://github.com/pre-commit/pre-commit-hooks
    rev: v6.0.0
    hooks:
      - id: trailing-whitespace
  - repo: https://github.com/DavidAnson/markdownlint-cli2
    rev: {rev}
    hooks:
      - id: markdownlint-cli2
"""
RULES = b"default: true\nMD013:\n  line_length: 120\n"


def output(name, *extra):
    """markdownlint-cli2's output for a page with two findings, the page's text in their context."""
    return "\n".join(
        [
            "markdownlint-cli2 v0.23.2 (markdownlint v0.41.1)",
            f"Finding: {name}",
            "Linting: 1 file",
            "Summary: 2 issues in 1 file",
            f'{name}:3:121 error MD013/line-length Line length [Expected: 120; Actual: 150] [Context: "{WORD} a"]',
            f'{name}:7 error MD025/single-title/single-h1 Multiple top-level headings [Context: "{WORD}"]',
            *extra,
        ]
    )


class Findings(unittest.TestCase):
    def test_line_and_rule_only(self):
        found = lint.findings(output("p.md"), "p.md")
        self.assertEqual([str(f) for f in found], ["line 3: MD013/line-length", "line 7: MD025/single-title"])
        self.assertNotIn(WORD, repr(found))

    def test_only_lines_naming_the_file(self):
        text = output(
            "p.md",
            f"other.md:9 error MD001/{WORD.lower()} x",
            f'p.md:4 error MD022/blanks-around-headings x [Context: "p.md:9 error MD001/{WORD.lower()} y"]',
            f"  p.md:5 MD041/{WORD.lower()}-",  # not a rule's name: dropped
            "\x1b[31mp.md:6:1 error MD009/no-trailing-spaces\x1b[0m Trailing spaces",
        )
        found = [str(f) for f in lint.findings(text, "p.md")]
        self.assertEqual(
            found,
            [
                "line 3: MD013/line-length",
                "line 4: MD022/blanks-around-headings",
                "line 6: MD009/no-trailing-spaces",
                "line 7: MD025/single-title",
            ],
        )
        self.assertNotIn(WORD.lower(), " ".join(found).lower())

    def test_pinned_version(self):
        self.assertEqual(lint.pinned_version(HOOKS.format(rev="v0.23.2"), "x"), "0.23.2")
        self.assertEqual(lint.pinned_version(HOOKS.format(rev='"0.23.2"  # pinned'), "x"), "0.23.2")
        for bad in (
            HOOKS.format(rev="main"),
            HOOKS.format(rev="v0.23.2 && echo"),
            HOOKS.replace("DavidAnson", "someone"),
            HOOKS.format(rev="v0.23.2")
            + "  - repo: https://github.com/DavidAnson/markdownlint-cli2\n    rev: v1.0.0\n",
        ):
            with self.assertRaises(lint.LintError):
                lint.pinned_version(bad, "x")


class Running(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)
        self.calls = []
        for name, value in (
            ("source", lambda ref, checkout: ("0.23.2", RULES)),
            ("npx_program", lambda given=None: "fake-npx"),
        ):
            p = mock.patch.object(lint, name, value)
            p.start()
            self.addCleanup(p.stop)

    def linter(self, code, text):
        def run(cmd, cwd):
            self.calls.append((cmd, Path(cwd), (Path(cwd) / cmd[-1]).read_bytes(), (Path(cwd) / cmd[-2]).read_bytes()))
            return code, text

        p = mock.patch.object(lint, "run", run)
        p.start()
        self.addCleanup(p.stop)
        return lint.Linter(self.tmp, "main")

    def test_command_and_copy(self):
        found = self.linter(1, output("page-1.md")).check("page-1", f"# A\r\n\r\n{WORD}\r\n".encode())
        self.assertEqual([str(f) for f in found], ["line 3: MD013/line-length", "line 7: MD025/single-title"])
        cmd, cwd, page, rules = self.calls[0]
        self.assertEqual(cmd, ["fake-npx", "--yes", "markdownlint-cli2@0.23.2", "--config", lint.RULES, "page-1.md"])
        self.assertEqual(cwd, self.tmp)
        self.assertEqual(page, f"# A\n\n{WORD}\n".encode())  # LF bytes
        self.assertEqual(rules, RULES)
        self.assertFalse((self.tmp / "page-1.md").exists())  # the copy is gone

    def test_clean(self):
        self.assertEqual(self.linter(0, "Summary: 0 issues").check("page-1", b"# A\n"), [])

    def test_could_not_run(self):
        for code, text in ((1, f"Error: {WORD}"), (2, output("page-1.md")), (1, output("other.md")), (-1, "")):
            with self.assertRaises(lint.LintError) as cm:
                self.linter(code, text).check("page-1", b"# A\n")
            self.assertNotIn(WORD, str(cm.exception))

    def test_bad_page_id(self):
        with self.assertRaises(lint.LintError):
            self.linter(0, "").check("../x", b"# A\n")

    def test_config_read_once(self):
        n = []
        with mock.patch.object(lint, "source", lambda ref, checkout: n.append(1) or ("0.23.2", RULES)):
            linter = self.linter(0, "")
            linter.check("page-1", b"# A\n")
            linter.check("page-2", b"# B\n")
        self.assertEqual(len(n), 1)


class Source(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)
        self.addCleanup(self._tmp.cleanup)

    @unittest.skipUnless(GIT, "needs git on PATH")
    def test_from_main_not_the_working_tree(self):
        repo = self.tmp / "repo"
        repo.mkdir()

        def git(*args):
            subprocess.run(
                [GIT, "-C", str(repo), *args],
                check=True,
                capture_output=True,
                env=signing.git_env(),
                creationflags=scan.NO_WINDOW,
            )

        git("init", "-q", "-b", "main")
        for k, v in (("user.name", "Test"), ("user.email", "test@example.invalid"), ("commit.gpgsign", "false")):
            git("config", k, v)
        (repo / lint.HOOKS).write_text(HOOKS.format(rev="v0.23.2"), encoding="utf-8", newline="\n")
        (repo / lint.RULES).write_bytes(RULES)
        git("add", ".")
        git("commit", "-q", "-m", "Add lint config")
        git("checkout", "-q", "-b", "work")
        (repo / lint.HOOKS).write_text(HOOKS.format(rev="v9.9.9"), encoding="utf-8", newline="\n")
        (repo / lint.RULES).write_bytes(b"default: false\n")
        git("commit", "-q", "-am", "Change it on a branch")
        (repo / lint.RULES).write_bytes(b"MD013: false\n")  # and once more, uncommitted
        self.assertEqual(lint.source("main", repo), ("0.23.2", RULES))
        self.assertEqual(lint.source("work", repo)[0], "9.9.9")
        for ref in ("no-such-ref", "--output=x"):
            with self.assertRaises(lint.LintError):
                lint.source(ref, repo)

    def test_from_the_installed_snapshot(self):
        tools = self.tmp / "tools"
        shutil.copytree(Path(lint.__file__).parent, tools / "python" / "coney_tools")
        snap = tools / lint.SNAPSHOT
        snap.mkdir()
        (snap / lint.HOOKS).write_text(HOOKS.format(rev="v0.23.1"), encoding="utf-8")
        (snap / lint.RULES).write_bytes(RULES.replace(b"\n", b"\r\n"))
        code = (
            "import sys; sys.path.insert(0, sys.argv[1]); from coney_tools import lint; "
            "print(lint.checkout_root()); print(lint.source('main', lint.checkout_root()))"
        )
        r = subprocess.run(
            [sys.executable, "-I", "-B", "-c", code, str(tools / "python")],
            capture_output=True,
            text=True,
            check=True,
            creationflags=scan.NO_WINDOW,
        )
        self.assertEqual(r.stdout.splitlines(), ["None", repr(("0.23.1", RULES))])
        (snap / lint.RULES).unlink()
        r = subprocess.run(
            [sys.executable, "-I", "-B", "-c", code, str(tools / "python")],
            capture_output=True,
            text=True,
            check=False,
            creationflags=scan.NO_WINDOW,
        )
        self.assertIn("LintError: cannot read .markdownlint.yaml", r.stderr)

    def test_snapshot_missing(self):
        with self.assertRaises(lint.LintError):
            lint.source("main", None, self.tmp / "nothing")


class Program(unittest.TestCase):
    @unittest.skipUnless(os.name == "nt", "Windows only: npx is npx.cmd there")
    def test_find_cmd(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "npx.cmd").write_text("@echo off\n", encoding="utf-8")
            with mock.patch.dict(os.environ, {"PATH": tmp}):
                self.assertIsNone(scan.find_program("npx"))  # .exe only, as before, unless asked
                self.assertEqual(lint.npx_program(), str(Path(tmp).resolve() / "npx.cmd"))
            with mock.patch.dict(os.environ, {"PATH": ""}), self.assertRaises(lint.LintError):
                lint.npx_program()
            with self.assertRaises(lint.LintError):
                lint.npx_program(str(Path(tmp) / "missing.cmd"))
            odd = Path(tmp, "a&b")
            odd.mkdir()
            (odd / "npx.cmd").write_text("@echo off\n", encoding="utf-8")
            with self.assertRaises(lint.LintError):
                lint.npx_program(str(odd / "npx.cmd"))


@unittest.skipUnless(os.environ.get("CONEY_TEST_NPX") == "1", "set CONEY_TEST_NPX=1 to run the real markdownlint")
class RealMarkdownlint(unittest.TestCase):
    """The pinned markdownlint-cli2 through npx, with the version and rules of this checkout's main."""

    def test_real(self):
        with tempfile.TemporaryDirectory() as tmp:
            linter = lint.Linter(tmp, "main")
            self.assertEqual(linter.check("clean-page", b"# A page\n\nA short line of made-up text.\n"), [])
            long_line = f"{WORD} " + " ".join(["made-up words"] * 12)
            found = linter.check("long-page", f"# A page\n\n{long_line}\n".encode())
            self.assertEqual([str(f) for f in found], ["line 3: MD013/line-length"])


if __name__ == "__main__":
    unittest.main()
