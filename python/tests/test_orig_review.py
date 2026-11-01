# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the orig review: which `@orig` functions a branch touches, the release gate on the reviewer's
verdict, and the ledger of reviewed functions. Invented code, names and addresses only."""

import hashlib
import json
from pathlib import Path
from typing import Any

import pytest
from conventions_support import NEWER_KEY, REMOVE, changed, write_checkout, write_older_checkout
from git_support import git

from coney_tools import orig_queue, orig_review
from coney_tools.cli import main
from coney_tools.conventions import ConventionsError

BRAIN_H = """#pragma once
namespace coney::ai {
class Brain {
  public:
    /// Thinks.
    /// @orig 0x00100010 Brain_Think (Brain.cpp)
    int think(int hunger);
    /// @coney-only a test aid
    void dump();
    /// Waits inline.
    /// @orig 0x00100020 Brain_Wait (Brain.cpp)
    int wait() const {
        return kPatience;
    }
};
} // namespace coney::ai
"""

BRAIN_CPP = """#include "ai/brain.h"
namespace coney::ai {
int Brain::think(int hunger) {
    return hunger > 3 ? 1 : 0;
}
void Brain::dump() {}
} // namespace coney::ai
"""


def commit_all(repo: Path, text: str) -> str:
    """Commit every file; returns the commit."""
    git(repo, "add", "-A")
    git(repo, "commit", "-q", "-m", text)
    return git(repo, "rev-parse", "HEAD").strip()


@pytest.fixture
def repo(tmp_path: Path) -> Path:
    """A checkout on `main` with a header-tagged and an inline-tagged function and a Coney-only one."""
    root = tmp_path / "repos" / "coney"
    root.mkdir(parents=True)
    write_checkout(root)
    for name, text in {"src/ai/brain.h": BRAIN_H, "src/ai/brain.cpp": BRAIN_CPP}.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8")
    git(root, "init", "-q", "-b", "main")
    commit_all(root, "base")
    return root


def change_think(repo: Path, branch: str = "feature") -> str:
    """A branch that changes Brain::think's body; returns its tip."""
    git(repo, "checkout", "-q", "-b", branch)
    path = repo / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("> 3", "> 4"), encoding="utf-8")
    return commit_all(repo, "change think")


def test_units_take_their_address_from_the_declaration(repo: Path) -> None:
    units = orig_review.units_at(repo, "HEAD")
    assert [(u.coney, u.where, [hex(a) for a in u.addresses]) for u in units] == [
        ("Brain::think", "src/ai/brain.cpp:3", ["0x100010"]),
        ("Brain::wait", "src/ai/brain.h:12", ["0x100020"]),
    ]


def test_an_xbox_tag_makes_a_unit_whose_xbox_address_is_kept_apart() -> None:
    text = (
        "namespace coney::render {\n"
        "/// Draws the shadows the Xbox port adds.\n"
        "/// @orig xbox 0x000bd3c0 Shadow_Draw (Shadow.cpp)\n"
        "void draw() {}\n"
        "/// Both builds.\n"
        "/// @orig 0x00100010 Light_Set (Light.cpp)\n"
        "/// @orig xbox 0x000bd4e0 Light_Set (Light.cpp)\n"
        "void light() {}\n"
        "} // namespace coney::render\n"
    )
    units = orig_review.units_from({"src/render/shadow.cpp": text})
    assert [(u.coney, u.addresses, u.xbox) for u in units] == [
        ("draw", (), (0x000BD3C0,)),
        ("light", (0x00100010,), (0x000BD4E0,)),
    ]
    assert units[0].to_json()["xbox"] == ["0x000bd3c0"]
    assert "xbox" not in orig_review.units_from({"src/ai/brain.cpp": BRAIN_CPP, "src/ai/brain.h": BRAIN_H})[0].to_json()


def test_the_ledger_and_the_address_status_keep_an_xbox_only_functions_xbox_address(tmp_path: Path) -> None:
    text = (
        "namespace coney::render {\n"
        "/// Draws the shadows the Xbox port adds.\n"
        "/// @orig xbox 0x000bd3c0 Shadow_Draw (Shadow.cpp)\n"
        "void draw() {}\n"
        "} // namespace coney::render\n"
    )
    [unit] = orig_review.units_from({"src/render/shadow.cpp": text})
    recorded = orig_review.entry(unit, "HEAD", "diff-bug", "sweep", "x", "")
    assert recorded["addresses"] == [] and recorded["xbox"] == ["0x000bd3c0"]
    assert "xbox" not in orig_review.entry(
        orig_review.units_from({"src/ai/brain.cpp": BRAIN_CPP, "src/ai/brain.h": BRAIN_H})[0],
        "HEAD",
        "clean",
        "sweep",
        "x",
        "",
    )
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    ledger.add([recorded])
    assert orig_review.address_status(ledger, [unit]) == {}
    assert orig_review.address_status(ledger, [unit], build="xbox") == {0x000BD3C0: "diff-bug"}


def test_a_branch_lists_only_the_tagged_functions_it_changes(repo: Path) -> None:
    tip = change_think(repo)
    stamp = orig_review.pr_stamp(repo, tip, "main")
    assert stamp["tip"] == tip
    assert [f["coney"] for f in stamp["functions"]] == ["Brain::think"]
    assert stamp["functions"][0]["addresses"] == ["0x00100010"]
    # An untagged or Coney-only change touches nothing.
    git(repo, "checkout", "-q", "main")
    git(repo, "checkout", "-q", "-b", "other")
    path = repo / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("dump() {}", "dump() { think(1); }"), encoding="utf-8")
    other = commit_all(repo, "dump")
    assert orig_review.pr_stamp(repo, other, "main")["functions"] == []


def verdict(stamp: dict[str, Any], cls: str = "clean", **extra: Any) -> dict[str, Any]:
    """A verdict covering every stamped function with `cls`."""
    functions = [
        {
            "key": f["key"],
            "body_sha256": f["body_sha256"],
            "addresses": f["addresses"],
            "class": cls,
            "evidence": "confirmed (code)",
            "note": "ok",
        }
        for f in stamp["functions"]
    ]
    data = {
        "verdict": "pass" if cls in orig_review.PASSING else "fail",
        "tip": stamp["tip"],
        "base": stamp["base"],
        "reviewed": sorted({a for f in stamp["functions"] for a in f["addresses"]}),
        "findings": [],
        "rubric_version": 1,
        "functions": functions,
        "sampled": None,
        "not_reviewed": [],
    }
    data.update(extra)
    return data


def xbox_stamp(repo: Path) -> dict[str, Any]:
    """The stamp of a branch that changes one function that follows the Xbox port only."""
    git(repo, "checkout", "-q", "-b", "xbox")
    path = repo / "src/render/shadow.cpp"
    path.parent.mkdir(parents=True)
    path.write_text(
        "namespace coney::render {\n/// Draws the shadows the Xbox port adds.\n"
        "/// @orig xbox 0x000bd3c0 Shadow_Draw (Shadow.cpp)\nvoid draw() {}\n} // namespace coney::render\n",
        encoding="utf-8",
    )
    return orig_review.pr_stamp(repo, commit_all(repo, "shadow"), "main")


def test_the_release_gate_takes_an_xbox_only_function_by_its_xbox_address(repo: Path) -> None:
    stamp = xbox_stamp(repo)
    assert [f["xbox"] for f in stamp["functions"]] == [["0x000bd3c0"]]
    data = verdict(stamp)
    for item in data["functions"]:
        item["addresses"] = ["xbox 0x000bd3c0"]
    data["reviewed"] = ["xbox 0x000bd3c0"]
    assert orig_review.verdict_problems(data, stamp, 1) == []
    # The bare number is the PS2's, so it does not stand for the Xbox function.
    data["functions"][0]["addresses"] = ["0x000bd3c0"]
    data["reviewed"] = ["0x000bd3c0"]
    assert any("`addresses` must be its own" in p for p in orig_review.verdict_problems(data, stamp, 1))


def test_a_passing_verdict_for_the_tip_passes(repo: Path) -> None:
    tip = change_think(repo)
    stamp = orig_review.pr_stamp(repo, tip, "main")
    assert orig_review.verdict_problems(verdict(stamp), stamp, 1) == []


@pytest.mark.parametrize(
    ("change", "words"),
    [
        ({"tip": "abc"}, "tip"),
        ({"verdict": "fail"}, "verdict"),
        ({"rubric_version": 0}, "rubric"),
        ({"reviewed": []}, "reviewed"),
        ({"functions": [{"key": "Brain::think(int)", "class": "clean"}]}, "addresses"),
        ({"functions": []}, "Brain::think"),
        ({"findings": [{"severity": "blocker", "where": "x", "text": "y"}]}, "blocker"),
    ],
)
def test_a_stale_failing_or_partial_verdict_is_refused(repo: Path, change: dict[str, Any], words: str) -> None:
    tip = change_think(repo)
    stamp = orig_review.pr_stamp(repo, tip, "main")
    problems = orig_review.verdict_problems(verdict(stamp, **change), stamp, 1)
    assert problems and any(words in p for p in problems)


@pytest.mark.parametrize("cls", ["transcription", "diff-bug", "diff-uncommented", "wrong-address"])
def test_a_failing_class_cannot_pass(repo: Path, cls: str) -> None:
    tip = change_think(repo)
    stamp = orig_review.pr_stamp(repo, tip, "main")
    data = verdict(stamp, cls)
    data["verdict"] = "pass"
    assert any(cls in p for p in orig_review.verdict_problems(data, stamp, 1))


def test_a_rebase_that_keeps_the_bodies_keeps_the_verdict(repo: Path) -> None:
    tip = change_think(repo)
    data = verdict(orig_review.pr_stamp(repo, tip, "main"))
    # A new commit elsewhere moves the tip but leaves the reviewed body as it was.
    (repo / "notes.txt").write_text("x", encoding="utf-8")
    moved = orig_review.pr_stamp(repo, commit_all(repo, "notes"), "main")
    assert orig_review.verdict_problems(data, moved, 1) == []
    assert any("tip" in p for p in orig_review.verdict_problems(data, moved, 1, strict_tip=True))
    # A change to the reviewed function makes it stale.
    path = repo / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace("> 4", "> 5"), encoding="utf-8")
    changed = orig_review.pr_stamp(repo, commit_all(repo, "again"), "main")
    assert any("another body" in p for p in orig_review.verdict_problems(data, changed, 1))


def test_check_verdict_file_is_what_a_release_step_calls(repo: Path, tmp_path: Path) -> None:
    tip = change_think(repo)
    path = tmp_path / "ORIG-REVIEW.json"
    stamp, problems = orig_review.check_verdict_file(repo, path, tip, "main")
    assert "missing" in problems[0]
    path.write_text(json.dumps(verdict(stamp)), encoding="utf-8")
    assert orig_review.check_verdict_file(repo, path, tip, "main")[1] == []


def test_a_sample_must_say_so(repo: Path) -> None:
    tip = change_think(repo)
    stamp = orig_review.pr_stamp(repo, tip, "main")
    data = verdict(stamp, functions=[], not_reviewed=[stamp["functions"][0]["key"]])
    assert any("sampled" in p for p in orig_review.verdict_problems(data, stamp, 1))
    data["sampled"] = "large diff: the riskiest functions first"
    assert orig_review.verdict_problems(data, stamp, 1) == []


def test_the_gate_finds_the_branch_from_the_scratch_folder(repo: Path) -> None:
    tip = change_think(repo, "tools-x")
    git(repo, "checkout", "-q", "main")
    folder = repo.parent.parent / "scratch" / "tools-x"
    folder.mkdir(parents=True)
    body = folder / "PR.md"
    body.write_text("tools: Add x\n\nbody\n", encoding="utf-8")
    ledger = orig_review.Ledger(folder.parent / "orig-review" / "ledger.jsonl")
    problems = orig_review.gate(repo, body, None, "main", None, ledger)
    assert problems and "ORIG-REVIEW.json" in problems[0]
    stamp = orig_review.pr_stamp(repo, tip, "main")
    (folder / "ORIG-REVIEW.json").write_text(json.dumps(verdict(stamp)), encoding="utf-8")
    assert orig_review.gate(repo, body, None, "main", None, ledger) == []
    # A pass is recorded in the ledger: the function is reviewed at this body.
    assert orig_review.status_of(ledger, orig_review.units_at(repo, tip))["Brain::think(int)"] == "clean"
    # --head names the branch too, and a branch with no tagged change needs no verdict.
    assert orig_review.gate(repo, body, "main", "main", None, ledger) == []


def test_the_gate_cannot_tell_without_a_branch(repo: Path, tmp_path: Path) -> None:
    body = tmp_path / "elsewhere" / "PR.md"
    body.parent.mkdir()
    body.write_text("x\n", encoding="utf-8")
    with pytest.raises(orig_review.CannotTell):
        orig_review.gate(repo, body, None, "main", None, orig_review.Ledger(tmp_path / "l.jsonl"))


def test_the_ledger_marks_changed_functions_stale(repo: Path, tmp_path: Path) -> None:
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    units = orig_review.units_at(repo, "HEAD")
    assert set(orig_review.status_of(ledger, units).values()) == {"unreviewed"}
    ledger.add([orig_review.entry(units[0], "HEAD", "diff-bug", "sweep", "x.tsv", "")])
    assert orig_review.status_of(ledger, units)["Brain::think(int)"] == "diff-bug"
    tip = change_think(repo)
    assert orig_review.status_of(ledger, orig_review.units_at(repo, tip))["Brain::think(int)"] == "stale"


def test_findings_tsv_imports_into_the_ledger(repo: Path, tmp_path: Path) -> None:
    tsv = tmp_path / "FINDINGS.tsv"
    tsv.write_text(
        "addr\torig_name\tconey_function\tfile:line\tclass\tevidence\tnote\n"
        "0x00100010\tBrain_Think\tBrain::think\tsrc/ai/brain.h:6\tclean\tconfirmed (code)\tsame\n"
        "0x00100020\tBrain_Wait\tconey::ai::Brain::wait\tsrc/ai/brain.h:12\ttranscription\tconfirmed (code)\tshape\n"
        "0x00100099\tGone\tBrain::gone\tsrc/ai/brain.cpp:9\tclean\tinferred\tno such\n",
        encoding="utf-8",
    )
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    added, unmatched = orig_review.import_findings(repo, ledger, [tsv], "HEAD")
    assert added == 2 and len(unmatched) == 1 and "0x00100099" in unmatched[0]
    status = orig_review.status_of(ledger, orig_review.units_at(repo, "HEAD"))
    assert status == {"Brain::think(int)": "clean", "Brain::wait()": "transcription"}
    # Importing again adds nothing.
    assert orig_review.import_findings(repo, ledger, [tsv], "HEAD")[0] == 0


def test_the_queue_drops_clean_reviews_and_keeps_findings(repo: Path, tmp_path: Path) -> None:
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    units = orig_review.units_at(repo, "HEAD")
    ledger.add([orig_review.entry(units[0], "HEAD", "clean", "sweep", "x", "")])
    ledger.add([orig_review.entry(units[1], "HEAD", "transcription", "sweep", "x", "")])
    reviews = orig_review.address_status(ledger, units)
    assert reviews == {0x00100010: "clean", 0x00100020: "transcription"}
    entry = orig_queue.Entry("0x00100020", "Brain_Wait", "src/ai")
    orig_queue.add_review(entry, reviews.get(0x00100020, "unreviewed"))
    assert entry.reasons == ["review: transcription"]
    assert orig_queue.review_done("clean") and not orig_queue.review_done("unclear")


def test_review_stamp_command_prints_json(repo: Path, monkeypatch: pytest.MonkeyPatch, capsys: Any) -> None:
    change_think(repo)
    monkeypatch.chdir(repo)
    assert main(["orig", "review-stamp", "--branch", "feature", "--base", "main"]) == 0
    printed = json.loads(capsys.readouterr().out)
    assert printed["mode"] == "pr" and len(printed["functions"]) == 1
    assert main(["orig", "review-stamp", "--folder", "src/ai/", "--ledger", str(repo / "l.jsonl")]) == 0
    sweep = json.loads(capsys.readouterr().out)
    assert sweep["mode"] == "sweep" and [f["coney"] for f in sweep["functions"]] == ["Brain::think", "Brain::wait"]


# --- the body hash ignores comments and whitespace (issue 859) ------------------------------------------------------

THINK_BODY = "    return hunger > 3 ? 1 : 0;\n"


def think_hash(body: str) -> str:
    """The body hash of Brain::think when its body is `body` (the lines between the braces)."""
    text = BRAIN_CPP.replace(THINK_BODY, body)
    units = orig_review.units_from({"src/ai/brain.h": BRAIN_H, "src/ai/brain.cpp": text})
    return next(u.body_sha256 for u in units if u.coney == "Brain::think")


def old_hash(text: str, first_line: int, count: int) -> str:
    """The body hash as it was before comments and whitespace were left out: the definition's lines, each with its
    trailing white space dropped, joined by newlines. Written out here so the migration tests do not rely on the code
    they test."""
    lines = text.splitlines()[first_line - 1 : first_line - 1 + count]
    return hashlib.sha256("\n".join(line.rstrip() for line in lines).encode("utf-8")).hexdigest()


@pytest.mark.parametrize(
    "body",
    [
        "    return hunger > 3 ? 1 : 0; // the original's threshold\n",
        "    // Why: see docs/research/ai/brain.md#think\n" + THINK_BODY,
        "    /* one line */\n" + THINK_BODY,
        "    /*\n     * a block comment\n     * that spans lines, with a ' and a \"\n     */\n" + THINK_BODY,
        "    return /* inline */ hunger > 3 ? 1 : 0;\n",
        "    // a comment that continues \\\n    // onto the next line\n" + THINK_BODY,
        "        return   hunger>3 ? 1 : 0;   \n",
        "\n\n    return hunger > 3\n        ? 1\n        : 0;\n\n",
        "\treturn hunger > 3 ? 1 : 0;\r\n",
    ],
    ids=[
        "trailing-comment",
        "link-comment",
        "block-comment",
        "multi-line-block",
        "inline-block",
        "continued-line-comment",
        "spacing",
        "line-breaks",
        "tab-and-crlf",
    ],
)
def test_a_comment_or_whitespace_edit_keeps_the_body_hash(body: str) -> None:
    assert think_hash(body) == think_hash(THINK_BODY)


@pytest.mark.parametrize(
    "body",
    [
        "    return hunger > 4 ? 1 : 0;\n",
        "    return hunger >= 3 ? 1 : 0;\n",
        "    return hunger > 3 ? 0 : 1;\n",
        "    return hungry > 3 ? 1 : 0;\n",
        "    return hunger > 3 ? 1 : 0;\n    go();\n",
        "    // return hunger > 3 ? 1 : 0;\n    return 0;\n",
        "    return hunger > 3 ? 1 : 0 /* ; */;\n    ;\n",
    ],
    ids=["number", "operator", "swapped", "identifier", "added-statement", "commented-out", "extra-token"],
)
def test_a_one_token_code_edit_changes_the_body_hash(body: str) -> None:
    assert think_hash(body) != think_hash(THINK_BODY)


def test_a_space_that_separates_two_tokens_is_code() -> None:
    assert think_hash("    return hunger > 3 ? 1 : 0;\n") != think_hash("    returnhunger > 3 ? 1 : 0;\n")
    assert think_hash("    return a + +b;\n") != think_hash("    return a ++b;\n")


@pytest.mark.parametrize(
    ("one", "other"),
    [
        ('    log("see http://a/b");\n', '    log("see http://a/c");\n'),
        ('    log("// not a comment");\n', '    log("// a different one");\n'),
        ('    log("/*"); go(1);\n', '    log("/*"); go(2);\n'),
        ('    log("a b");\n', '    log("a  b");\n'),
        ("    char c = '\"'; go(1);\n", "    char c = '\"'; go(2);\n"),
        ("    char c = '/'; go(1);\n", "    char c = '/'; go(2);\n"),
        ('    log(R"x(// not)x"); go(1);\n', '    log(R"x(// not)x"); go(2);\n'),
        ('    log("a\\"//b"); go(1);\n', '    log("a\\"//b"); go(2);\n'),
        ("    wait(48'000); go(1);\n", "    wait(48'000); go(2);\n"),
    ],
    ids=["url", "slashes", "block-opener", "spaces", "quote-char", "slash-char", "raw", "escaped-quote", "separator"],
)
def test_a_string_or_character_literal_that_looks_like_a_comment_is_code(one: str, other: str) -> None:
    assert think_hash(one) != think_hash(other)


def test_a_comment_after_a_literal_that_looks_like_a_comment_is_still_a_comment() -> None:
    plain = think_hash('    log("/*"); go(1);\n')
    assert think_hash('    log("/*"); go(1); // a note\n') == plain
    assert think_hash('    log("/*"); /* a note */ go(1);\n') == plain
    assert think_hash("    char c = '\"'; go(1); // \"\n") == think_hash("    char c = '\"'; go(1);\n")
    assert think_hash("    wait(48'000); go(1); // it's fine\n") == think_hash("    wait(48'000); go(1);\n")


def edit_brain(repo: Path, old: str, new: str, message: str) -> str:
    """Replace `old` with `new` in brain.cpp and commit; returns the commit."""
    path = repo / "src/ai/brain.cpp"
    path.write_text(path.read_text(encoding="utf-8").replace(old, new), encoding="utf-8")
    return commit_all(repo, message)


def test_a_comment_only_branch_lists_no_function(repo: Path) -> None:
    git(repo, "checkout", "-q", "-b", "links")
    tip = edit_brain(repo, THINK_BODY, "    // See docs/research/ai/brain.md#think\n" + THINK_BODY, "repoint a link")
    assert orig_review.pr_stamp(repo, tip, "main")["functions"] == []


def test_a_verdict_survives_a_comment_edit_after_the_review(repo: Path) -> None:
    tip = change_think(repo)
    reviewed = verdict(orig_review.pr_stamp(repo, tip, "main"))
    body = THINK_BODY.replace("3", "4")
    later = edit_brain(repo, body, "    // why\n" + body, "comment")
    stamp = orig_review.pr_stamp(repo, later, "main")
    assert orig_review.verdict_problems(reviewed, stamp, 1) == []


# --- what happens to hashes recorded before: the ledger and the `compared` stamps -----------------------------------


def think_unit(repo: Path, ref: str = "HEAD") -> orig_review.Unit:
    """Brain::think at `ref`."""
    (unit,) = [u for u in orig_review.units_at(repo, ref) if u.coney == "Brain::think"]
    return unit


def test_a_ledger_entry_recorded_under_the_old_hash_still_counts(repo: Path, tmp_path: Path) -> None:
    unit = think_unit(repo)
    old = old_hash(BRAIN_CPP, unit.line, 3)
    assert old != unit.body_sha256
    recorded = orig_review.entry(unit, "HEAD", "clean", "sweep", "x", "")
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    ledger.add([{**recorded, "body_sha256": old}])
    assert orig_review.status_of(ledger, [unit]) == {unit.key: "clean"}
    # A review under neither hash is stale, as ever.
    other = orig_review.Ledger(tmp_path / "other.jsonl")
    other.add([{**recorded, "body_sha256": "0" * 64}])
    assert orig_review.status_of(other, [unit]) == {unit.key: "stale"}


def test_a_ledger_entry_under_the_old_hash_is_stale_once_the_code_changed(repo: Path, tmp_path: Path) -> None:
    unit = think_unit(repo)
    recorded = {**orig_review.entry(unit, "HEAD", "clean", "sweep", "x", ""), "body_sha256": old_hash(BRAIN_CPP, 3, 3)}
    ledger = orig_review.Ledger(tmp_path / "ledger.jsonl")
    ledger.add([recorded])
    tip = change_think(repo)
    assert orig_review.status_of(ledger, [think_unit(repo, tip)]) == {unit.key: "stale"}


def test_a_body_recorded_under_the_old_hash_is_current_while_the_code_is_unchanged(repo: Path) -> None:
    unit = think_unit(repo)
    commit = git(repo, "rev-parse", "HEAD").strip()
    old = old_hash(BRAIN_CPP, unit.line, 3)
    assert orig_review.recorded_body_is_current(repo, old, commit, unit.key, unit.body_sha256)
    assert orig_review.recorded_body_is_current(repo, unit.body_sha256, commit, unit.key, unit.body_sha256)


def test_a_body_recorded_under_the_old_hash_survives_a_comment_edit_but_not_a_code_edit(repo: Path) -> None:
    unit = think_unit(repo)
    commit = git(repo, "rev-parse", "HEAD").strip()
    old = old_hash(BRAIN_CPP, unit.line, 3)
    edit_brain(repo, THINK_BODY, "    // a link\n" + THINK_BODY, "comment")
    assert orig_review.recorded_body_is_current(repo, old, commit, unit.key, think_unit(repo).body_sha256)
    edit_brain(repo, "> 3", "> 4", "code")
    assert not orig_review.recorded_body_is_current(repo, old, commit, unit.key, think_unit(repo).body_sha256)


def test_a_recorded_body_that_matches_nothing_at_its_commit_is_not_current(repo: Path) -> None:
    unit = think_unit(repo)
    commit = git(repo, "rev-parse", "HEAD").strip()
    assert not orig_review.recorded_body_is_current(repo, "0" * 64, commit, unit.key, unit.body_sha256)
    assert not orig_review.recorded_body_is_current(repo, "1" * 64, "2" * 40, unit.key, unit.body_sha256)
    assert not orig_review.recorded_body_is_current(repo, None, commit, unit.key, unit.body_sha256)


# --- a branch cut before main added a conventions key (#1355) -------------------------------------------------------


def test_the_orig_review_fills_a_key_main_added_from_the_defaults(repo: Path, tmp_path: Path) -> None:
    """The verdict check, the release gate and the ledger's place read the branch's file; one lacking a key main
    added since takes main's value, as the release commands' lenient read does."""
    tip = change_think(repo, "tools-x")
    write_older_checkout(repo)
    path = tmp_path / "ORIG-REVIEW.json"
    stamp = orig_review.pr_stamp(repo, tip, "main")
    path.write_text(json.dumps(verdict(stamp)), encoding="utf-8")
    assert orig_review.check_verdict_file(repo, path, tip, "main")[1] == []
    folder = repo.parent.parent / "scratch" / "tools-x"
    folder.mkdir(parents=True)
    body = folder / "PR.md"
    body.write_text("tools: Add x\n\nbody\n", encoding="utf-8")
    (folder / "ORIG-REVIEW.json").write_text(json.dumps(verdict(stamp)), encoding="utf-8")
    ledger = orig_review.Ledger(folder.parent / "orig-review" / "ledger.jsonl")
    assert orig_review.gate(repo, body, None, "main", None, ledger) == []
    orig_review.default_ledger(repo)


def test_the_orig_review_still_refuses_a_wrong_value_or_a_key_with_no_default(
    repo: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    tip = change_think(repo, "tools-x")
    path = tmp_path / "ORIG-REVIEW.json"
    stamp = orig_review.pr_stamp(repo, tip, "main")
    path.write_text(json.dumps(verdict(stamp)), encoding="utf-8")
    write_older_checkout(repo, (NEWER_KEY, "lots"))
    with pytest.raises(ConventionsError, match=r"check_stall_minutes: expected a whole number"):
        orig_review.check_verdict_file(repo, path, tip, "main")
    write_older_checkout(repo)
    monkeypatch.setattr("coney_tools.conventions._own_defaults", lambda: changed((NEWER_KEY, REMOVE)))
    with pytest.raises(ConventionsError, match=r"missing the key 'check_stall_minutes'"):
        orig_review.check_verdict_file(repo, path, tip, "main")
    with pytest.raises(ConventionsError, match=r"missing the key 'check_stall_minutes'"):
        orig_review.default_ledger(repo)
