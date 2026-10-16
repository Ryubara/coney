"""The cases an independent review of page-check reported (2026-10-14), kept as tests: evasions that must be caught,
legitimate pages that must pass, output that must not leak page text, and inputs that must not crash.

Synthetic content only; code-like strings are assembled from pieces so this file matches no rule itself.
"""

import hashlib
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools.cleanroom import page_check
from coney_tools.cli import main

ADDR = "0x" + "00123456"
OP = "add" + "iu"
FN = "FUN" + "_00abcdef"
ZW = "\u200b"


def page(body, heading="Topic"):
    return f"# {heading}\n\n{body}\n"


def rules(text, **kw):
    return sorted({f.rule for s in page_check.check_text(text, **kw) for f in s.findings})


class Evasions(unittest.TestCase):
    CASES = (
        # MIPS
        ("mips", f"Use `{OP} $sp, $sp, -32` there."),
        ("mips", f"**{OP}** $sp, $sp, -32"),
        ("mips", f"[{OP} $sp, $sp, -32]"),
        ("mips", "l" + "w $2, 0x10($29)"),
        ("mips", "l" + "i $v0, 1"),
        ("mips", "mo" + "ve $a0, $s0"),
        ("mips", "beq" + "z $v0, 0x10"),
        ("mips", "lw" + "l $t0, 3($a0)"),
        ("mips", "ja" + "l 0x" + "00123456"),
        ("mips", "pad" + "dw $t0, $t1, $t2"),
        ("mips", "vad" + "d.xyzw $vf1, $vf2, $vf3"),
        ("mips", f"{OP} $sp,\n$sp, -32"),
        ("mips", f"{OP.upper()}\t$SP, $SP, -32"),
        # decompiler names
        ("decompiler-names", "pu" + "Var3 is set"),
        ("decompiler-names", "au" + "Stack_20 holds it"),
        ("decompiler-names", "sub" + "_0012ab34 runs"),
        ("decompiler-names", "switchD" + "_00123456 picks"),
        ("decompiler-names", "ZEXT" + "48 of it"),
        ("decompiler-names", "fun" + "_00123456 lower case"),
        ("decompiler-names", "FUN&#95;" + "00123456 as an entity"),
        ("decompiler-names", "FUN&amp;#95;" + "00123456 double encoded"),
        ("decompiler-names", "FUN" + ZW + "_00123456 with a zero-width space"),
        ("decompiler-names", "FUN\\_" + "00123456 with a Markdown escape"),
        ("mips", "add&#105;u $sp, $sp, -32"),
        # code as text
        ("code-text", "Then hp = hp - 1; happens."),
        ("code-text", "if (hp <= 0) { state = 3; }"),
        ("code-text", "| cell | hp = hp - 1; |"),
        ("code-text", "if x:\n    y = 1"),
        ("inline-code", "Set `count = count + 1` there."),
        ("inline-code", "Call `GetUp(human, 2)` there."),
        ("inline-code", "Set `count =\ncount + 1` there."),
        # code blocks
        ("code-block", "> ```\n> anything\n> ```"),
        ("code-block", "- item\n\n    ```\n    anything\n    ```"),
        ("code-block", "    indented right after the heading"),
        ("code-block", "- item\n\n        eight spaces inside a list item"),
        ("code-block", "Prose.\n\n \tspace then tab"),
        ("code-block", "<samp>anything</samp>"),
        ("code-block", "<xmp>anything</xmp>"),
        ("image", "<svg><text>anything</text></svg>"),
        # dumps
        ("instruction-words", "0xcafef00d 0x0badf00d 0x12345678"),
        ("instruction-words", ADDR + " " + "cafe" + "f00d"),
        ("instruction-words", "ca fe f0 0d 0b ad f0 0d"),
        # phrases
        ("banned-phrase", "Applied in the original\u2019s order."),
        ("banned-phrase", "Coney\u2019s implementation keeps it."),
        ("banned-phrase", "the original code does it"),
        ("banned-phrase", "Damage works as the game does."),
        ("banned-phrase", "Found with Ghidra."),
        # helpers and branches
        ("helper-boundary", "GetUp calls Recover after that."),
        ("helper-boundary", "It is called by the timer."),
        ("branch-shape", "When dazed it falls; failing that it fires."),
        # step lists (in a section that names a game address)
        ("step-list", f"At `{ADDR}`. First it reads the pad. Then it moves."),
        ("step-list", f"At `{ADDR}`: step 1 reads the pad, step 2 moves."),
        ("step-list", f"At `{ADDR}`:\n\n- reads the pad\n- then moves"),
        ("step-list", f"At `{ADDR}`:\n\n- a\n  1. reads the pad\n  2. moves"),
        # images
        ("image", "![x](a.png 'title')"),
        ("image", "![a [b] c](a.png)"),
        ("image", "![shot]\n\n[shot]: a.png"),
        ("image", '<img\n  src="a.png">'),
        ("image", '<picture><source srcset="a.png"></picture>'),
        ("image", '<embed src="a.png">'),
        ("image", "![x](<a b.png>)"),
    )

    def test_each_evasion_is_caught(self):
        for rule, body in self.CASES:
            with self.subTest(rule=rule, body=body.encode("unicode_escape").decode()):
                self.assertIn(rule, rules(page(body)))

    def test_image_outside_the_page_folder(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "pages").mkdir()
            (root / "a.png").write_bytes(b"x")
            (root / "a.png.capture.toml").write_text('source = "s"\ncaptured = "c"\nby = "b"\n', encoding="utf-8")
            self.assertIn("image", rules(page("![x](../a.png)"), page_dir=root / "pages"))


class LegitimatePages(unittest.TestCase):
    PAGES = (
        f"| Address | Size | Field | Default |\n|---|---|---|---|\n| `{ADDR}` | 4 | stamina | `0x3F800000` |",
        "Access it as `Human->gang`; flags are `flags << 3`; the vector is `{x, y, z, w}`; run with `--level=3`.",
        "Press F1 or F2\n\nMissions s1 and s2",
        f"`{ADDR}` holds id 99999999 next to 12345678 and 87654321 10000000",
        "Colour `0xFF00FF00` and scale `0x3F800000`.\n\n1. The Human lies still.\n2. The Human kneels.",
        "The idle animation plays in a loop.",
        "A civilian who sees a crime calls the police, which then lets the gang flee.",
        "The menu text was translated from Japanese; the game was ported from the PS2 version.",
        "The first four bytes are the magic `0x4d4f4f4e`, then a u32 count.",
        "A knocked-down Human gets up after 2.5 s unless hit again.",
        "| Weapon | Id | Damage |\n|---|---|---|\n| Bat | 3 | 10 |\n| Knife | 4 | 15 |",
        "The signature is `Human::GetUp(Human*, int)`.",
    )

    def test_legitimate_pages_pass(self):
        for body in self.PAGES:
            with self.subTest(body=body):
                self.assertEqual(rules(page(body)), [])


class Guides(unittest.TestCase):
    def test_shell_blocks_pass_in_guides_only(self):
        body = page("Build it:\n\n```sh\ncmake --build build\nexport X=1; make\n```")
        self.assertEqual(rules(body, guide=True), [])
        self.assertEqual(rules(body), ["code-block"])

    def test_guides_still_check_everything_else(self):
        self.assertIn("code-block", rules(page("```c\nint x = 1;\n```"), guide=True))
        self.assertIn("mips", rules(page(f"```sh\n{OP} $sp, $sp, -32\n```"), guide=True))
        self.assertIn("decompiler-names", rules(page(f"```sh\necho {FN}\n```"), guide=True))


class Leaks(unittest.TestCase):
    def output(self, path, *flags):
        out = io.StringIO()
        with redirect_stdout(out), redirect_stderr(io.StringIO()):
            main(["cleanroom", "page-check", *flags, str(path)])
        return out.getvalue()

    def test_image_names_and_failing_headings_are_not_printed(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "p.md")
            p.write_text(f"# Notes on {'FUN' + ZW + '_00abcdef'}\n\n![x]({FN}.png)\n", encoding="utf-8")
            for flags in ((), ("--json",)):
                text = self.output(p, *flags).lower()
                self.assertNotIn("00abcdef", text)
                self.assertNotIn("notes", text)


class Robustness(unittest.TestCase):
    def run_cli(self, *args, stdin=None):
        out, err = io.StringIO(), io.StringIO()
        old = sys.stdin
        try:
            if stdin is not None:
                sys.stdin = io.TextIOWrapper(io.BytesIO(stdin))
            with redirect_stdout(out), redirect_stderr(err):
                code = main(["cleanroom", "page-check", *args])
        finally:
            sys.stdin = old
        return code, out.getvalue(), err.getvalue()

    def test_empty_page_fails(self):
        self.assertEqual(rules("   \n\n"), ["empty"])
        self.assertEqual(rules(ZW + "\n"), ["empty"])

    def test_not_text_is_the_same_from_stdin_and_files(self):
        code, out, _ = self.run_cli("-", stdin=b"# T\n\xff\xfe\n")
        self.assertEqual(code, 1)
        self.assertIn("not-text", out)

    def test_too_large(self):
        secs = page_check.check_bytes(b"a" * (page_check.MAX_PAGE + 1))
        self.assertEqual(secs[0].findings[0].rule, "too-large")

    def test_bad_capture_record_types_do_not_crash(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            (root / "a.png").write_bytes(b"x")
            (root / "a.png.capture.toml").write_text(
                'source = "s"\ncaptured = "c"\nby = "b"\nsha256 = 5\n', encoding="utf-8"
            )
            (root / "p.md").write_text(page("![x](a.png)"), encoding="utf-8")
            code, out, _ = self.run_cli(str(root / "p.md"))
            self.assertEqual(code, 1)
            self.assertIn("image", out)

    def test_non_ascii_path(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "s\u00e9ance \u65e5\u672c.md")
            p.write_text(page("Fine."), encoding="utf-8")
            code, _out, _ = self.run_cli(str(p))
            self.assertEqual(code, 0)

    def test_checker_hash_ignores_line_endings(self):
        src = Path(page_check.__file__).read_bytes()
        self.assertEqual(page_check.checker_sha256(), hashlib.sha256(src.replace(b"\r\n", b"\n")).hexdigest())


if __name__ == "__main__":
    unittest.main()
