# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the issue tests: a made-up main and a good task body in the structure of issue #340."""

from audit_support import FakeTree

#: Pages that exist on the made-up main, with their anchors.
TREE = FakeTree(
    {
        "docs/research/ai-goals.md": frozenset({"goal-object-pile", "goals"}),
        "docs/guides/issues.md": frozenset({"report"}),
        "research/missions.yaml": frozenset(),
    }
)
LINK = "[docs/research/ai-goals.md](../blob/main/docs/research/ai-goals.md)"

# A body in the structure of issue #340: every form heading at level three, the research page as a link, the status
# of the pull request stated, "Screenshots: none (reason)" and a From: line after it.
GOOD = f"""### Summary

Gang members in Coney never pick up objects to throw at the enemy gang. The Throw order is specified in pull request
#317 (open, not merged yet) and the goal in {LINK}.

### Area

A-ai

### Research page

{LINK} (ObjectPile, `#goal-object-pile`)

### What "done" means

Unit tests, written first: objects within 30 m nearest first; a claimed object is skipped for 5 s.

### Fix list or steps

- [ ] Throw: `ThrowSubTactic_Init` (`0x00322b78`)
- [ ] Files: `src/ai/sub_tactics.cpp`

### Blocked by

#317 (a pull request, open, not merged yet) and the ObjectPile spec (#335).

### Screenshots

Screenshots: none (AI logic)

From: research-reimpl-taskengine UNITS.md, unit 6, 2026-10-12
"""


def body(**replaced: str) -> str:
    """GOOD with the text of some sections replaced, keyed by the form's section id (`summary`, `area`, ...)."""
    result = GOOD
    originals = {
        "summary": GOOD.split("### Summary\n\n")[1].split("\n\n### Area")[0],
        "area": "A-ai",
        "done": GOOD.split('### What "done" means\n\n')[1].split("\n\n### Fix")[0],
        "screenshots": "Screenshots: none (AI logic)\n\nFrom: research-reimpl-taskengine UNITS.md, unit 6, 2026-10-12",
    }
    for key, text in replaced.items():
        result = result.replace(originals[key], text, 1)
    return result
