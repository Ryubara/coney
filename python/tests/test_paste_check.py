"""Tests for `cleanroom paste-check`. Made-up text only: invented addresses, names and instructions."""

import io
import sys
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from coney_tools.cleanroom import page_check
from coney_tools.cli import main

LOG = """C:\\Users\\someone>pwsh -NoProfile -File C:\\Tools\\install.ps1 -Apply
done:  copy python\\ at main (0123456789abcdef0123456789abcdef01234567) to C:\\Tools\\VERSION
allowed through: someone (S-1-5-21-1-2-3-1003)
  permit connect-v4: remote port 4100 and user in {S-1-5-21-1-2-3-1003}
ok    port wall          sublayer                want present got weight 0xfffe
PASS: 64 checks
{"mcpServers": {"tool-a": {"type": "stdio", "command": "C:/Tools/python.exe", "args": ["-m", "tool_a"]}}}
param(
    [string]$ProjectDir = "C:\\Tools\\project",
)
for f in *.md; do wc -l "$f"; done
"""


def run(text):
    out, err = io.StringIO(), io.StringIO()
    stdin = mock.Mock(buffer=io.BytesIO(text.encode("utf-8") if isinstance(text, str) else text))
    with mock.patch.object(sys, "stdin", stdin), redirect_stdout(out), redirect_stderr(err):
        code = main(["cleanroom", "paste-check"])
    return code, out.getvalue(), err.getvalue()


class PasteCheck(unittest.TestCase):
    def test_logs_configs_and_scripts_pass(self):
        self.assertEqual(run(LOG), (0, "", ""))
        self.assertEqual(run("A knocked-down character gets up after 2.5 s unless hit again.\n")[0], 0)

    def test_original_shapes_are_refused_by_rule_and_line(self):
        cases = [
            ("note\n  addiu $sp, $sp, -0x20\n  sw $ra, 0x1c($sp)\n", "mips", 2),
            ("void FUN_00aabbcc(int param_1)\n{\n}\n", "decompiler-names", 1),
            ("x\n00aabb30: 27bdffe0 afbf001c 0c00aaaa 00000000\n", "instruction-words", 2),
        ]
        for text, rule, line in cases:
            with self.subTest(rule=rule):
                code, out, _ = run(text)
                self.assertEqual(code, 1)
                self.assertIn(f"line {line}: {rule}:", out)

    def test_findings_never_show_the_text(self):
        code, out, _ = run("Quillwort said\n  addiu $sp, $sp, -0x20\nvoid FUN_00aabbcc(void)\n")
        self.assertEqual(code, 1)
        for word in ("Quillwort", "addiu", "FUN_00aabbcc", "0x20"):
            self.assertNotIn(word, out)

    def test_text_that_cannot_be_checked_does_not_pass(self):
        self.assertEqual(run(b"\xff\xfe\x00bad")[0], 1)
        big = b"a" * (page_check.MAX_PAGE + 1)
        code, out, _ = run(big)
        self.assertEqual(code, 1)
        self.assertIn("too-large", out)

    def test_only_original_rules_count(self):
        # general code and banned phrases are page rules, not paste rules
        self.assertEqual(run("```python\nx = compute(1)\n```\nas the original does\n")[0], 0)


if __name__ == "__main__":
    unittest.main()
