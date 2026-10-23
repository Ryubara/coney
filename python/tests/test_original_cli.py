# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for the `original` and `stamp` commands: the whole flow through `main`, with a fake ghidra-mcp."""

from pathlib import Path

import pytest
from conventions_support import write_checkout
from git_support import git
from stamp_support import allowed_signers, make_key, needs_ssh_keygen
from test_orig_compare import SOURCE
from test_orig_study import ADDRESS, FakeGhidra

from coney_tools import orig_pairs, original_cli, stamps
from coney_tools.cli import main
from coney_tools.conventions import load_conventions
from coney_tools.progress import ProgressError
from coney_tools.unit import Unit


@pytest.fixture
def checkout(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> Path:
    """A made-up workspace with a checkout on `main` holding a tagged Coney function; the cwd is set to it."""
    root = tmp_path / "repos" / "coney"
    (root / "src").mkdir(parents=True)
    write_checkout(root)
    (root / "src" / "widget.cpp").write_text(SOURCE, encoding="utf-8")
    git(root, "init", "-q", "-b", "main")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "base")
    monkeypatch.chdir(root)
    monkeypatch.delenv("CONEY_TASK", raising=False)
    monkeypatch.setattr(original_cli, "http_fetch", lambda url: FakeGhidra())
    return root


def log(root: Path) -> Path:
    """The stamp log of the task `main`."""
    return Unit.of(root, "main").stamps


def test_study_prints_the_summary_and_a_second_run_is_unchanged(
    checkout: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["original", "study", "0x501000"]) == 0
    out = capsys.readouterr().out
    assert "0x00501000 Widget_Update: 9 lines, 5 pieces" in out and "stamped studied" in out
    assert (checkout.parent.parent / "scratch" / "main" / "study" / ADDRESS / "numbered.txt").is_file()
    assert main(["original", "study", ADDRESS, "--print"]) == 0
    again = capsys.readouterr().out
    assert "unchanged" in again and "iVar1" in again
    assert len(stamps.verify(log(checkout), load_conventions(checkout).stamps).stamps) == 1


def test_study_with_ghidra_down_exits_3_and_writes_nothing(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def down(path: str) -> object:
        raise ProgressError("ghidra-mcp at http://x: refused (is the server running?)")

    monkeypatch.setattr(original_cli, "http_fetch", lambda url: down)
    assert main(["original", "study", ADDRESS]) == 3
    assert "start it (docs/guides/ghidra.md" in capsys.readouterr().out
    assert not Unit.of(checkout, "main").dir.exists()


def test_a_bad_address_is_a_usage_error(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "study", "nope"]) == 2
    assert "not an address" in capsys.readouterr().err


def test_compare_flow_from_blank_pairs_to_a_stamp(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    assert main(["original", "compare", ADDRESS, "--base", "main"]) == 1
    assert "wrote 5 blank pairs" in capsys.readouterr().out
    path = Unit.of(checkout, "main").mapping(ADDRESS)
    assert main(["original", "compare", ADDRESS, "--base", "main"]) == 1
    refused = capsys.readouterr().out
    assert "relation not set" in refused and "refused, 5 problem(s); nothing stamped" in refused
    file = orig_pairs.load(path)
    where = {
        "1-3": ("src/widget.cpp:5", "same"),
        "4-6": ("src/widget.cpp:8", "same"),
        "7": ("src/widget.cpp:12", "stand-in"),
    }
    where.update({"8": ("src/widget.cpp:13", "same"), "9": ("src/widget.cpp:14", "same")})
    file.pairs = [
        orig_pairs.Pair(
            p.lines, p.kind, where[p.lines][0], where[p.lines][1], "the sound plays later" if p.lines == "7" else ""
        )
        for p in file.pairs
    ]
    orig_pairs.write(path, file)
    assert main(["original", "compare", ADDRESS, "--base", "main"]) == 0
    assert "5 pairs over 5 pieces (4 same, 1 stand-in, 0 not-ported); stamp #2" in capsys.readouterr().out
    assert main(["stamp", "verify"]) == 0
    assert "stamp verify: ok: 2 stamp(s)" in capsys.readouterr().out
    assert main(["stamp", "show"]) == 0
    shown = capsys.readouterr().out
    assert "studied" in shown and "compared" in shown and "2 stamp(s)" in shown


def test_stamp_verify_fails_on_an_edited_log(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    assert main(["original", "fails-first", "--none", "docs only"]) == 0
    capsys.readouterr()
    segment = log(checkout) / "0001.jsonl"
    segment.write_bytes(segment.read_bytes().replace(b'"result":"pass"', b'"result":"fail"', 1))
    assert main(["stamp", "verify"]) == 1
    assert "prev does not match" in capsys.readouterr().out


def test_a_release_check_wants_signed_steps_signed(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    capsys.readouterr()
    assert main(["stamp", "verify", "--release"]) == 0  # studied is not a signed step
    assert main(["stamp", "verify", "--dir", str(checkout / "nowhere")]) == 0


def test_stamp_task_and_dir_are_independent_options(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    capsys.readouterr()
    folder = str(log(checkout))
    assert main(["stamp", "verify", "--dir", folder]) == 0  # --dir alone: the log's task is not checked
    assert main(["stamp", "verify", "--task", "main", "--dir", folder]) == 0  # both: the log must be that task's
    assert main(["stamp", "show", "--task", "main", "--dir", folder]) == 0
    capsys.readouterr()
    assert main(["stamp", "verify", "--task", "other", "--dir", folder]) == 1  # another task's log fails
    assert "other" in capsys.readouterr().out


def test_an_unchanged_study_under_an_old_stamp_says_the_stamp_was_updated(
    checkout: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    capsys.readouterr()
    assert main(["original", "study", ADDRESS]) == 0
    assert "unchanged since the last study" in capsys.readouterr().out
    folder = log(checkout)
    [first] = stamps.verify(folder, load_conventions(checkout).stamps).stamps
    for segment in folder.iterdir():
        segment.unlink()
    stamps.append(
        folder,
        rules=load_conventions(checkout).stamps,
        task="main",
        step="studied",
        subject=ADDRESS,
        result="pass",
        commit=first.commit,
        covers={k: v for k, v in first.covers.items() if k != "identity_sha256"},
        detail=dict(first.detail),
        who={"kind": "tool", "tool": "test"},
    )
    assert main(["original", "study", ADDRESS]) == 0
    assert "unchanged; stamp updated to cover the identity" in capsys.readouterr().out


def test_fails_first_needs_arguments_or_none(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "fails-first"]) == 2
    assert main(["original", "fails-first", "--none", "docs only"]) == 0
    assert "none (docs only); stamp #1" in capsys.readouterr().out
    assert main(["original", "fails-first", "--test", "x", "--test-commit", "HEAD"]) == 2


def test_the_task_option_picks_the_unit(checkout: Path) -> None:
    assert main(["original", "fails-first", "--none", "tooling", "--task", "other"]) == 0
    assert (Unit.of(checkout, "other").stamps / "0001.jsonl").is_file()


def test_only_signed_steps_get_a_signer(checkout: Path) -> None:
    rules = load_conventions(checkout)
    assert original_cli.signer_for(checkout, rules, "studied") is None
    assert original_cli.signer_for(checkout, rules, "checked") is None  # no coney.local.toml: unsigned


def signed_log(root: Path, tmp_path: Path, key: object) -> Path:
    """A one-stamp log of the signed step `checked`, signed with `key`'s private file."""
    from stamp_support import Key

    assert isinstance(key, Key)
    rules = load_conventions(root).stamps

    def sign(body: bytes) -> dict[str, object]:
        from coney_tools import stamp_sign

        return {"signer": "x", "armor": stamp_sign.sign(body, key.private, rules.namespace, use_agent=False)}

    folder = tmp_path / "log"
    stamps.append(
        folder,
        rules=rules,
        task="main",
        step="checked",
        subject="",
        result="pass",
        commit="ab" * 20,
        covers={},
        detail={},
        who={"kind": "tool", "tool": "test"},
        sign=sign,
    )
    return folder


@needs_ssh_keygen
def test_a_key_added_in_the_working_tree_does_not_vouch_for_itself(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    alice = make_key(tmp_path / "a", comment="alice")
    mallory = make_key(tmp_path / "m", comment="mallory")
    signers = checkout / ".github" / "stamp-signers"
    allowed_signers(signers, ("alice", alice))
    git(checkout, "add", "-A")
    git(checkout, "commit", "-q", "-m", "signers")
    folder = signed_log(checkout, tmp_path, mallory)
    allowed_signers(signers, ("alice", alice), ("mallory", mallory))  # the change under review adds the signer
    base = ["stamp", "verify", "--release", "--dir", str(folder)]
    assert main(base) == 0  # the working tree vouches for itself
    capsys.readouterr()
    assert main([*base, "--signers-ref", "main"]) == 1
    assert "signature" in capsys.readouterr().out
    git(checkout, "commit", "-q", "-am", "add mallory")
    assert main([*base, "--signers-ref", "main"]) == 0  # merged first: now trusted


@needs_ssh_keygen
def test_the_namespace_and_the_signed_steps_come_from_the_trusted_ref_too(
    checkout: Path, tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    alice = make_key(tmp_path / "a", comment="alice")
    allowed_signers(checkout / ".github" / "stamp-signers", ("alice", alice))
    git(checkout, "add", "-A")
    git(checkout, "commit", "-q", "-m", "signers")
    folder = signed_log(checkout, tmp_path, alice)
    conventions = checkout / ".github" / "conventions.yml"
    conventions.write_text(
        conventions.read_text(encoding="utf-8").replace("namespace: coney-stamp", "namespace: other"), encoding="utf-8"
    )
    base = ["stamp", "verify", "--release", "--dir", str(folder)]
    assert main(base) == 1  # the working tree's namespace no longer matches the signature
    capsys.readouterr()
    assert main([*base, "--signers-ref", "main"]) == 0


def test_a_missing_ref_is_a_usage_error(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["stamp", "verify", "--signers-ref", "nowhere"]) == 2
    assert "nowhere" in capsys.readouterr().err


def test_a_release_check_refuses_an_empty_log_and_a_log_of_another_task(
    checkout: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["stamp", "verify", "--release"]) == 1
    assert "no stamps" in capsys.readouterr().out
    assert main(["original", "fails-first", "--none", "docs only", "--task", "elsewhere"]) == 0
    other = Unit.of(checkout, "elsewhere").stamps
    assert main(["stamp", "verify", "--dir", str(other)]) == 0  # a folder named without a task: any
    assert main(["stamp", "verify", "--dir", str(other), "--task", "main"]) == 1
    assert "task" in capsys.readouterr().out


def test_show_marks_signatures_it_could_not_check_and_fails_on_a_broken_log(
    checkout: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    assert main(["original", "study", ADDRESS]) == 0
    assert main(["original", "fails-first", "--none", "docs only"]) == 0
    capsys.readouterr()
    assert main(["stamp", "show"]) == 0
    assert "unsigned" in capsys.readouterr().out
    segment = log(checkout) / "0001.jsonl"
    segment.write_bytes(segment.read_bytes().replace(b'"result":"pass"', b'"result":"fail"', 1))
    assert main(["stamp", "show"]) == 1
    assert "prev does not match" in capsys.readouterr().out


def test_none_is_not_combined_with_tests(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    assert main(["original", "fails-first", "--none", "docs", "--test", "x"]) == 2
    assert main(["original", "fails-first", "--none", "two\nlines"]) == 2


def test_signers_and_signers_ref_cannot_be_combined(checkout: Path, capsys: pytest.CaptureFixture[str]) -> None:
    # --signers-ref would silently replace the file given with --signers; argparse refuses the pair instead.
    with pytest.raises(SystemExit) as stop:
        main(["stamp", "verify", "--signers", "x", "--signers-ref", "main"])
    assert stop.value.code == 2
    assert "not allowed with" in capsys.readouterr().err


def test_a_rename_also_says_to_carry_the_pairs(
    checkout: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # The pairs header binds the decompile's hash, so a rename-only restudy needs `--carry` as much as a change does.
    assert main(["original", "study", ADDRESS]) == 0
    assert main(["original", "compare", ADDRESS, "--base", "main"]) == 1
    capsys.readouterr()
    renamed = FakeGhidra()
    renamed.text = renamed.text.replace("iVar1", "health")
    monkeypatch.setattr(original_cli, "http_fetch", lambda url: renamed)
    assert main(["original", "study", ADDRESS]) == 0
    out = capsys.readouterr().out
    assert "renamed" in out and "--carry" in out
