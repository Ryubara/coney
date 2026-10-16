"""The third independent review of page-check (2026-10-14), kept as tests. Synthetic content only; code-like strings
are assembled from pieces so this file matches no rule itself."""

import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools.cleanroom import page_check

ADDR = "0x" + "00123456"
OP = "add" + "iu"


def page(body, heading="Topic"):
    return f"# {heading}\n\n{body}\n"


def rules(text, **kw):
    return sorted({f.rule for s in page_check.check_text(text, **kw) for f in s.findings})


class Evasions(unittest.TestCase):
    CASES = (
        ("look-alike", "ꓝ" + "UN_00123456 in Lisu letters"),  # Lisu look-alike for F
        ("look-alike", "par" + "ⲁ" + "m_2 with a Coptic letter"),  # Coptic alfa
        ("code-text", "if (hp <= 0) state = DOWN;"),
        ("code-text", "Then Recover(human, 2.5); runs."),
        ("code-text", "timer = GetTimer(human)"),
        ("code-text", "if hp <= 0 then state = DOWN end"),
        ("step-list", "At `0x" + "1234567" + "`:\n\n1. reads\n2. moves"),
        ("step-list", f"At `{ADDR}`:\n\n- it reads the pad and then moves"),
        ("helper-boundary", "It hands control to Recover."),
        ("mips", "add[^1]iu $sp, $sp, -32"),
        ("mips", "| " + OP + " | $sp, $sp, -32 |"),
    )

    def test_each_evasion_is_caught(self):
        for rule, body in self.CASES:
            with self.subTest(rule=rule, body=body.encode("unicode_escape").decode()):
                self.assertIn(rule, rules(page(body)))


class Guides(unittest.TestCase):
    def test_fence_in_a_quote_or_list_is_not_a_shell_block(self):
        for opener in ("> ```sh", "- item\n\n  ```sh"):
            with self.subTest(opener=opener):
                found = rules(page(f"{opener}\nmake\n\nhp = hp - 1;\n"), guide=True)
                self.assertIn("code-block", found)

    def test_shell_blocks_reject_other_code(self):
        for line in ("if (hp <= 0) state = 3;", "hp = hp - 1;", "if hp <= 0:", "Recover(human, 2);"):
            with self.subTest(line=line):
                self.assertIn("code-text", rules(page(f"```sh\n{line}\n```"), guide=True))

    def test_shell_and_powershell_still_pass(self):
        body = page(
            '```powershell\n$files = Get-ChildItem .\n$files[0] = $null\n$env:CC = "clang"\n'
            "if ($LASTEXITCODE -ne 0) { exit 1 }\nfor ($i = 0; $i -lt 3; $i++) { Write-Host $i }\n```\n\n"
            "```sh\nX=1 make -j8\ncmake --build build -- -j8\n```"
        )
        self.assertEqual(rules(body, guide=True), [])

    def test_shell_blocks_reject_increments(self):
        for line in ("hp--", "timer++;"):
            with self.subTest(line=line):
                self.assertIn("code-text", rules(page(f"```sh\n{line}\n```"), guide=True))

    def test_a_name_spelled_wholly_in_lookalikes(self):
        lisu = "ꓝꓴꓠ"  # three Lisu letters
        self.assertIn("look-alike", rules(page(lisu + "_00123456 runs")))
        self.assertEqual(rules(page("日本語の文 and Строка")), [])


class LegitimatePages(unittest.TestCase):
    PAGES = (
        "The flash lasts 5 µs.",
        "Gravity = 9.8 m/s;",
        "Flags are `0x"
        + "00000001"
        + "` and `0x"
        + "00000002"
        + "`.\n\n1. The Human lies still.\n2. The Human kneels.",
    )

    def test_legitimate_pages_pass(self):
        for body in self.PAGES:
            with self.subTest(body=body):
                self.assertEqual(rules(page(body)), [])


class Timing(unittest.TestCase):
    def test_unclosed_comments(self):
        start = time.perf_counter()
        page_check.check_text("# T\n\n" + "<!-- x " * 45000)
        self.assertLess(time.perf_counter() - start, 5.0)


if __name__ == "__main__":
    unittest.main()
