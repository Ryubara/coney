"""The second independent review of page-check (2026-10-14), kept as tests: look-alike letters, markup tricks,
guide mode, label operands, steps, false positives, a worst-case timing check and pinned section hashes.

Synthetic content only; code-like strings are assembled from pieces so this file matches no rule itself.
"""

import io
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools.cleanroom import page_check
from coney_tools.cli import main

ADDR = "0x" + "00123456"
OP = "add" + "iu"
CYR_A = "а"


def page(body, heading="Topic"):
    return f"# {heading}\n\n{body}\n"


def rules(text, **kw):
    return sorted({f.rule for s in page_check.check_text(text, **kw) for f in s.findings})


class Evasions(unittest.TestCase):
    CASES = (
        # look-alike letters and invisible characters
        ("mips", CYR_A + "ddiu $sp, $sp, -32"),
        ("look-alike", CYR_A + "ddiu $sp, $sp, -32"),
        ("decompiler-names", "param" + "_1".replace("a", CYR_A) + " and " + "p" + CYR_A + "ram_2"),
        ("decompiler-names", "FUΝ" + "_00123456"),  # Greek capital Nu for N
        ("decompiler-names", "FUN\U000e0020" + "_00123456"),  # a tag character
        ("decompiler-names", "FUN￹" + "_00123456"),
        # markup tricks
        ("mips", "_" + OP + " $sp, $sp, -32_"),
        ("decompiler-names", "_FUN" + "_00123456_"),
        ("mips", "add<!-- > -->iu $sp, $sp, -32"),
        ("mips", 'add<span title="a>b">iu</span> $sp, $sp, -32'),
        ("mips", "add[](x)iu $sp, $sp, -32"),
        ("mips", "[add](x)[iu](y) $sp, $sp, -32"),
        # label and symbol operands, bare instructions
        ("mips", "ja" + "l Human_GetUp"),
        ("mips", "bne" + "z $v0, skip"),
        ("mips", "l" + "a $a0, gangTable"),
        ("mips", "l" + "w $v0, gangCount"),
        ("mips", "Prose.\n\nsys" + "call"),
        # code without semicolons
        ("code-text", "Then hp -= 1 happens."),
        ("code-text", "count := count + 1"),
        ("code-text", "hp ← hp - 1"),
        ("code-text", "if hp < 0: state = 3"),
        ("inline-code", "Call `SetState(DOWN)` there."),
        # helpers and phrases
        ("helper-boundary", "It hands off to Recover."),
        ("helper-boundary", "It is invoked by the timer."),
        ("helper-boundary", "Human_Update uses Human_Recover."),
        ("banned-phrase", "the original game's code does that"),
        # steps (in a section that names a game address)
        ("step-list", "At `0x" + "123456" + "`:\n\n1. reads\n2. moves"),
        ("step-list", "At `0x" + "80123456" + "`:\n\n1. reads\n2. moves"),
        ("step-list", f"At `{ADDR}`:\n\na. reads\nb. moves"),
        ("step-list", f"At `{ADDR}` it (1) reads the pad (2) moves."),
        ("step-list", f"At `{ADDR}`: initially it reads, afterwards it moves."),
        ("step-list", f"At `{ADDR}`:\n\n- First it reads\n- Second it moves"),
        # code blocks
        ("code-block", "-     five spaces after the marker"),
        ("image", '<div style="background: url(a.png)">x</div>'),
    )

    def test_each_evasion_is_caught(self):
        for rule, body in self.CASES:
            with self.subTest(rule=rule, body=body.encode("unicode_escape").decode()):
                self.assertIn(rule, rules(page(body)))


class Guides(unittest.TestCase):
    def test_c_inside_a_shell_block_is_caught(self):
        self.assertIn("code-text", rules(page("```sh\nif (x) { y(); }\n```"), guide=True))
        self.assertIn("code-text", rules(page("```sh\nint count = 3;\n```"), guide=True))

    def test_fake_fences_do_not_hide_text(self):
        hidden = "hp = hp - 1;"
        for fence in ("Prose\n    ```sh", "&#96;&#96;&#96;sh", "\\`\\`\\`sh"):
            with self.subTest(fence=fence):
                found = rules(page(f"{fence}\n{hidden}\n```"), guide=True)
                self.assertIn("code-block", found)
                self.assertIn("code-text", found)

    def test_guide_mode_only_under_docs_guides(self):
        with tempfile.TemporaryDirectory() as d:
            Path(d, ".git").mkdir()  # the checkout's root
            research, guides = Path(d, "guides", "research"), Path(d, "docs", "guides")
            research.mkdir(parents=True)
            guides.mkdir(parents=True)
            body = page("```sh\nmake\n```")
            (research / "p.md").write_text(body, encoding="utf-8")
            (guides / "p.md").write_text(body, encoding="utf-8")
            with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
                self.assertEqual(main(["cleanroom", "page-check", "--guide", str(research / "p.md")]), 2)
                self.assertEqual(main(["cleanroom", "page-check", "--guide", str(guides / "p.md")]), 0)
                self.assertEqual(main(["cleanroom", "page-check", "--guide", "-"]), 2)


class LegitimatePages(unittest.TestCase):
    PAGES = (
        "| Field | Offset |\n|---|---|\n| unk_0010 | `0x10` |",
        "The variable envVar1 is set by the launcher.",
        "Defaults: stamina = 100; armour = 0.",
        "For each gang:\n\n- a\n- b",
        "Café naïve über straße",
        "Строка на русском",  # a Russian sentence
        "The first byte is 0x" + "4d, then a u32 count.",
        "| Model | Hash |\n|---|---|\n| van | `0xCAFEF00D` |\n| bat | `0x0BADF00D` |",
    )

    def test_legitimate_pages_pass(self):
        for body in self.PAGES:
            with self.subTest(body=body):
                self.assertEqual(rules(page(body)), [])


class Timing(unittest.TestCase):
    """Worst cases for the patterns: each page is about 300 KB and must take well under a few seconds."""

    def assertFast(self, text, limit=5.0):
        start = time.perf_counter()
        page_check.check_text(text)
        self.assertLess(time.perf_counter() - start, limit)

    def test_assignment_lines_without_semicolons(self):
        self.assertFast("# T\n\n" + "a = b c d e f g\n" * 20000)

    def test_table_rows(self):
        self.assertFast("# T\n\n| f | v |\n|---|---|\n" + "| field = value | x |\n" * 15000)

    def test_unmatched_backticks_and_brackets(self):
        self.assertFast("# T\n\n" + "` [ ( < { a = " * 25000)


class SectionHashes(unittest.TestCase):
    def test_pinned_hashes(self):
        """Any change to how sections are split or normalised changes these values; verdict files depend on them."""
        text = "Intro line.  \n\n# A\n\nBody\n\n\n\nMore\t\n\n```\n# not a heading\n```\n## B ##\nLast\n"
        secs = page_check.check_text(text)
        # section A holds a code block, so it fails and its id is withheld; the '#' line inside the fence is no heading
        self.assertEqual([(s.id, s.line) for s in secs], [("top", 1), ("withheld-2", 3), ("b", 14)])
        self.assertEqual(secs[1].normalised(), "# A\n\nBody\n\nMore\n\n```\n# not a heading\n```\n")
        self.assertEqual(
            [s.sha256() for s in secs],
            [
                "21dea72506851287aba3f0c0e95ae9a98617323b8bb1737c9831e73c939992a8",
                "f18c6661d9d5f539fee8f56d4a90a7f517f413da28cdee126318c8752c13460a",
                "b514417a5c22528969bb6facad6d5d1ffd1c87e87cfb7cea419fbebb6dc73c2b",
            ],
        )


if __name__ == "__main__":
    unittest.main()
