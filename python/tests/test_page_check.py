"""Tests for cleanroom page-check, on synthetic pages only (playbook 20: no real research in tests).

Code-like strings are assembled from pieces (e.g. "FUN" + "_...") so that this file itself never matches the
starting-proof scanner or the page rules.
"""

import hashlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from coney_tools.cleanroom import page_check
from coney_tools.cli import main

ADDR = "0x" + "00123456"
MIPS = "add" + "iu $sp, $sp, -32"
DECOMP = "FUN" + "_00123456"
CLEAN = f"""# Knockdown

A knocked-down Human gets up after 2.5 s unless hit again.

## Fields

| Address | Size | Meaning | Default |
|---|---|---|---|
| `{ADDR}` | 4 | stamina | `0x3F800000` |

## Recovery

What the player sees, in order:

1. The Human lies still.
2. The Human kneels.

- Gang ids: 1 to 9
- Weapon ids: see `WeaponTable`
"""


def rules(sections):
    return sorted({f.rule for s in sections for f in s.findings})


def page(body, heading="Topic"):
    return f"# {heading}\n\n{body}\n"


class RuleTests(unittest.TestCase):
    def assertFlags(self, text, rule):
        self.assertIn(rule, rules(page_check.check_text(text)), text)

    def assertPasses(self, text):
        self.assertEqual(rules(page_check.check_text(text)), [], text)

    def test_clean_fact_page_passes(self):
        self.assertPasses(CLEAN)

    def test_code_blocks(self):
        self.assertFlags(page("```\nanything\n```"), "code-block")
        self.assertFlags(page("~~~text\nanything\n~~~"), "code-block")
        self.assertFlags(page("Prose.\n\n    indented block"), "code-block")
        self.assertFlags(page("<pre>anything</pre>"), "code-block")
        self.assertPasses(page("- item\n\n    continued item text"))

    def test_inline_code(self):
        self.assertFlags(page("Set `x = y + 1;` on hit."), "inline-code")
        self.assertFlags(page("See `if (ready)` for it."), "inline-code")
        self.assertPasses(page("The field `stamina` at `" + ADDR + "` holds `Human::GetUp`."))

    def test_mips(self):
        self.assertFlags(page(MIPS), "mips")
        self.assertFlags(page("| " + MIPS + " |"), "mips")
        self.assertPasses(page("The player or at least one gang member wins."))

    def test_instruction_words(self):
        # Plainly fake words: the rule is about the shape of a dump, not any real encoding.
        self.assertFlags(page("00123456: " + "cafe" + "f00d"), "instruction-words")
        self.assertFlags(page(ADDR + " holds " + "cafe" + "f00d"), "instruction-words")
        self.assertFlags(page("cafef00d 0badf00d 12345678"), "instruction-words")
        self.assertFlags(page("pat" + "ch=1,EE,00123456,word,00000000"), "instruction-words")
        self.assertPasses(page(f"| `{ADDR}` | `0x3F800000` |"))

    def test_decompiler_names(self):
        for name in (DECOMP, "param" + "_2", "u" + "Var3", "local" + "_1c", "sub" + "_0012AB34", "undefined" + "4"):
            self.assertFlags(page(f"Named {name} here."), "decompiler-names")

    def test_shapes_and_phrases(self):
        self.assertFlags(page("If dazed it falls, else if armed it fires."), "branch-shape")
        self.assertFlags(page("It calls the timer, which then resets."), "helper-boundary")
        self.assertFlags(page("It loops over every gang member."), "loop-shape")
        self.assertFlags(page("The local variable holds the count."), "loop-shape")
        self.assertFlags(page("Damage is applied as the original does."), "banned-phrase")
        self.assertFlags(page("Coney's implementation keeps a cache."), "banned-phrase")
        self.assertFlags(page("Read from the " + "decom" + "piler output."), "banned-phrase")

    def test_step_lists_need_an_address(self):
        self.assertFlags(page(f"At `{ADDR}`:\n\n1. Load it.\n2. Store it."), "step-list")
        self.assertFlags(page(f"At `{ADDR}` it first reads the pad, then moves."), "step-list")
        self.assertPasses(page("1. The Human lies still.\n2. The Human kneels."))
        self.assertPasses(page("The Human first staggers, then falls."))

    def test_not_utf8(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "p.md")
            p.write_bytes(b"# T\n\xff\xfe\n")
            self.assertEqual(rules(page_check.check_file(p)), ["not-text"])


class ImageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.img = self.dir / "shot.png"
        self.img.write_bytes(b"\x89PNG synthetic")
        self.page = self.dir / "page.md"
        self.page.write_text(page("![A knockdown](shot.png)"), encoding="utf-8")

    def tearDown(self):
        self.tmp.cleanup()

    def record(self, text):
        (self.dir / "shot.png.capture.toml").write_text(text, encoding="utf-8")

    def test_image_needs_a_capture_record(self):
        self.assertEqual(rules(page_check.check_file(self.page)), ["image"])

    def test_valid_record_passes(self):
        digest = hashlib.sha256(self.img.read_bytes()).hexdigest()
        self.record(f'source = "tester capture"\ncaptured = "2026-10-14"\nby = "coney-tester"\nsha256 = "{digest}"\n')
        self.assertEqual(rules(page_check.check_file(self.page)), [])

    def test_bad_records_fail(self):
        for text in (
            'source = "tester capture"\n',
            "not toml [",
            'source = "a"\ncaptured = "b"\nby = "c"\nsha256 = "00"\n',
        ):
            self.record(text)
            self.assertEqual(rules(page_check.check_file(self.page)), ["image"], text)

    def test_remote_and_stdin_images_fail(self):
        self.assertIn("image", rules(page_check.check_text(page("![x](https://example.com/a.png)"), self.dir)))
        self.assertIn("image", rules(page_check.check_text(page("![x](shot.png)"))))
        self.assertIn("image", rules(page_check.check_text(page('<img src="shot.png">'), self.dir)))

    def test_images_in_headings_and_on_a_first_line_are_checked(self):
        self.assertIn("image", rules(page_check.check_text("# Knockdown ![x](shot.png)\n\nText.\n", self.dir)))
        self.assertIn("image", rules(page_check.check_text("![x](shot.png)\n\n# Later\n\nText.\n", self.dir)))
        digest = hashlib.sha256(self.img.read_bytes()).hexdigest()
        self.record(f'source = "tester capture"\ncaptured = "2026-10-14"\nby = "coney-tester"\nsha256 = "{digest}"\n')
        sections = page_check.check_text("# Knockdown ![x](shot.png)\n\nText.\n", self.dir)
        self.assertEqual(rules(sections), [])
        self.assertEqual(page_check.page_images(sections, self.dir), ["shot.png"])


class EntryPointTests(unittest.TestCase):
    ROW = "| `0x00aa11bb` | open the store | Opening |"
    GOOD = (
        "# Store\n\nA made-up store.\n\n## Entry points\n\n| Address | Job | Outcomes |\n| --- | --- | --- |\n"
        "| `0x00aa11bb` | open the store | Opening |\n\n## Opening\n\nAn empty store opens with no entries.\n"
    )

    def check(self, text):
        return [(f.rule, f.message) for s in page_check.check_text(text) for f in s.findings]

    def test_the_agreed_form_passes(self):
        self.assertEqual(self.check(self.GOOD), [])

    def test_each_deviation_is_named(self):
        cases = [
            (self.GOOD.replace("`0x00aa11bb`", "`0x00AA11BB`"), "8 lower-case hex digits"),
            (self.GOOD.replace("`0x00aa11bb`", "0x00aa11bb"), "8 lower-case hex digits"),
            (self.GOOD.replace("| Opening |", "| Elsewhere |"), "heading of another section"),
            (self.GOOD.replace("| Opening |", "| Entry points |"), "heading of another section"),
            (self.GOOD.replace("| Address | Job | Outcomes |", "| Address | Responsible for | Outcomes |"), "columns"),
            (self.GOOD.replace("| open the store |", "|  |"), "the job is empty"),
            (self.GOOD.replace("| open the store | Opening |", "| open | Opening | extra |"), "three cells"),
            (self.GOOD.replace("## Entry points", "### Entry points"), "level-2"),
            (self.GOOD + "\n## Entry points\n\nNo table.\n", "one '## Entry points'"),
            ("# Store\n\n## Entry points\n\nNone yet.\n", "no 'Address | Job | Outcomes' table"),
            (self.GOOD.replace(self.ROW, self.ROW + "\n" + self.ROW), "an address is listed once"),
        ]
        for text, message in cases:
            with self.subTest(message=message):
                found = self.check(text)
                self.assertTrue(any(r == "entry-points" and message in m for r, m in found), found)

    def test_the_job_is_checked_as_page_text(self):
        code = "`" + "x = y" + "(1);`"  # code in a job cell is code on the page
        found = self.check(self.GOOD.replace("| open the store |", f"| open the store {code} |"))
        self.assertIn("inline-code", [r for r, _ in found])

    def test_entry_points_are_read_from_a_passing_page(self):
        text = self.GOOD.replace(self.ROW, self.ROW + "\n| `0x00cc22dd` |  Close   the **Store** | Opening |")
        self.assertEqual(
            page_check.entry_points(page_check.check_text(text)),
            {"0x00aa11bb": "open the store", "0x00cc22dd": "close the **store**"},
        )
        self.assertEqual(page_check.entry_points(page_check.check_text("# Store\n\nNo table.\n")), {})
        with self.assertRaises(ValueError):
            page_check.entry_points(page_check.check_text(text.replace("| Opening |", "| Elsewhere |")))

    def test_other_address_tables_are_ordinary_text(self):
        old = "# Store\n\n## Addresses\n\n| Address | Responsible for |\n| --- | --- |\n| `0x00aa11bb` | opening |\n"
        self.assertEqual(self.check(old), [])


class SectionTests(unittest.TestCase):
    def test_sections_and_ids(self):
        secs = page_check.check_text("Intro.\n\n# A\n\nx\n\n## A\n\ny\n\n```\n# not a heading\n```\n")
        self.assertEqual([s.id for s in secs], ["top", "a", "withheld-3"])  # failing sections are withheld
        self.assertEqual([s.ok for s in secs], [True, True, False])

    def test_hash_ignores_line_endings_and_trailing_space(self):
        a = page_check.check_text("# T\n\nSome text.\n\n\n\nMore.\n")[0].sha256()
        b = page_check.check_text("# T  \r\n\r\nSome text.   \r\n\r\nMore.\r\n\r\n")[0].sha256()
        self.assertEqual(a, b)

    def test_heading_with_a_finding_is_withheld(self):
        secs = page_check.check_text("# Fine\n\nx\n\n## About " + DECOMP + "\n\ny\n")
        self.assertEqual([s.id for s in secs], ["fine", "withheld-2"])
        self.assertEqual(secs[1].heading, "")
        out = io.StringIO()
        with tempfile.TemporaryDirectory() as d:
            p = Path(d, "p.md")
            p.write_text("# About " + DECOMP + "\n\ny\n", encoding="utf-8")
            with redirect_stdout(out):
                main(["cleanroom", "page-check", "--json", str(p)])
                main(["cleanroom", "page-check", str(p)])
        self.assertNotIn(DECOMP.lower(), out.getvalue().lower())

    def test_finding_lines(self):
        secs = page_check.check_text(page("Fine.\n\nNamed " + DECOMP + " here."))
        self.assertEqual([(f.rule, f.line) for f in secs[0].findings], [("decompiler-names", 5)])


class CliTests(unittest.TestCase):
    def run_cli(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            code = main(list(args))
        return code, out.getvalue()

    def test_exit_codes_and_no_matched_text(self):
        with tempfile.TemporaryDirectory() as d:
            good, bad = Path(d, "good.md"), Path(d, "bad.md")
            good.write_text(CLEAN, encoding="utf-8")
            secret = "Named " + DECOMP + " here."
            bad.write_text(page(secret), encoding="utf-8")
            self.assertEqual(self.run_cli("cleanroom", "page-check", str(good))[0], 0)
            code, text = self.run_cli("cleanroom", "page-check", str(good), str(bad))
            self.assertEqual(code, 1)
            self.assertIn("decompiler-names", text)
            self.assertNotIn(DECOMP, text)
            code, text = self.run_cli("cleanroom", "page-check", "--json", str(bad))
            self.assertNotIn(DECOMP, text)
            data = json.loads(text)
            self.assertEqual(len(data["checker"]), 64)
            self.assertFalse(data["pages"][0]["ok"])
            self.assertIsNone(data["pages"][0]["sections"][0]["sha256"])  # withheld for a failing section
            self.assertEqual(self.run_cli("cleanroom", "page-check", str(Path(d, "missing.md")))[0], 2)

    def test_rules_listing(self):
        code, text = self.run_cli("cleanroom", "page-check", "--rules")
        self.assertEqual(code, 0)
        for rid, _ in page_check.RULES:
            self.assertIn(rid, text)

    def test_launcher_runs_isolated(self):
        r = subprocess.run(
            [sys.executable, "-I", str(ROOT / "coney-tools.py"), "cleanroom", "page-check", "-"],
            input=CLEAN.encode(),
            capture_output=True,
            check=False,
        )
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(b"PASS -", r.stdout)


if __name__ == "__main__":
    unittest.main()
