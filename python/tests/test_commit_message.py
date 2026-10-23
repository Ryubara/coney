# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for coney_tools.commit_message and `coney-tools pr check-commit`, the check behind the git commit-msg hook.

The rules are the `commits.trailers` of the real conventions file (AGENTS.md, Commits and GitHub). The model of the
agent comes from `--model` or from the identity file the Claude Code hooks wrote for the command
(docs/guides/agents.md#identity); typed variables are not trusted.
"""

import json
from pathlib import Path

import pytest
from conventions_support import REPO
from hooks_support import load_hook_module

from coney_tools import cli, commit_message, identity
from coney_tools.conventions import load_conventions

CONVENTIONS = load_conventions(REPO)
RULES = CONVENTIONS.commits.trailers
#: The e-mail pattern of the project (`hygiene.attachments.patterns.email`), the one the attachment scan uses too.
EMAILS = CONVENTIONS.hygiene.attachments.patterns.email
SONNET = "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
OPUS = "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
BODY = "tools: Add a thing\n\nIt does the thing, because the thing needed doing.\n"
SCISSORS = "# ------------------------ >8 ------------------------"


def problems(message: str, model: str | None = None) -> list[str]:
    """What the check refuses in a message."""
    return commit_message.check_commit_message(message, RULES, model, emails=EMAILS)


def test_a_plain_message_and_a_message_with_the_ai_trailer_pass() -> None:
    assert problems(BODY) == []
    assert problems(f"{BODY}\n{SONNET}\n") == []
    assert problems(f"{BODY}\n{SONNET}\n{OPUS}\n") == []  # two models on one branch: both are named


def test_git_comment_lines_are_not_the_message() -> None:
    message = f"{BODY}\n{SONNET}\n# Please enter the commit message\n# jane@example.com\n"
    assert problems(message) == []


def test_another_trailer_is_refused() -> None:
    for trailer in ("Signed-off-by: Jane <noreply@anthropic.com>", "Fixes: #12", "Reviewed-by: x"):
        found = problems(f"{BODY}\n{SONNET}\n{trailer}\n")
        assert found and "trailer" in found[0], trailer


def test_prose_with_a_colon_is_not_a_trailer() -> None:
    assert problems("tools: Add a thing\n\nNote: this changes the hook.\n\nIt also fixes the other hook.\n") == []


def test_another_email_address_is_refused_anywhere_in_the_message() -> None:
    assert problems(f"{BODY}\nCo-Authored-By: Jane <jane@example.com>\n")
    assert problems("tools: Add a thing\n\nAsk jane@example.com.\n")
    assert problems(f"{BODY}\nCo-Authored-By: Gemini <noreply@google.com>\n") == []


@pytest.mark.parametrize(
    "text",
    [
        "Moves markdownlint-cli2@0.18.1 and icon@2x.png.",
        "Bumps pkg@1.2.3, logo@3x.png and actions/checkout@v4.",
        "Adds icon@1.5x.png and logo@2X.PNG.",
        "Pins uvx pre-commit@4.0.1 and ruff@0.14.0 in the hooks.",
    ],
)
def test_a_pinned_package_or_an_asset_name_is_not_an_address(text: str) -> None:
    assert problems(f"build: Update the pins\n\n{text}\n") == []


@pytest.mark.parametrize(
    "text",
    [
        "Ask jane@example.com.",
        "Mail Jane.Doe@mail.example.co.uk about it.",
        "Or a@b.io",
        "pkg@1.2.3 and jane@example.com",
        # A domain that starts like an asset's scale suffix is still an address.
        "Mail jane@2x.com.",
        "Mail ops@10x.dev.",
        "Mail x@0x.ai.",
        "Mail bob@4x.co.uk.",
        "Mail JANE@2X.COM.",
        "Mail jane@3x.mail.example.com.",
        # An image name as the first labels of a domain is a domain.
        "Mail jane@2x.png.example.com.",
        "Mail jane@2x.svg.io.",
        "Mail jane@2x.png-cdn.com.",
    ],
)
def test_a_real_address_is_still_refused(text: str) -> None:
    found = problems(f"build: Update the pins\n\n{text}\n")
    assert found and "e-mail address" in found[0]


def test_the_address_pattern_is_the_conventions_not_a_copy() -> None:
    # A pattern of the conventions that matches nothing lets every address through: the check holds none of its own.
    message = "tools: Add a thing\n\nAsk jane@example.com.\n"
    assert commit_message.check_commit_message(message, RULES, emails=("(?!)",)) == []
    assert commit_message.check_commit_message(message, RULES, emails=EMAILS)


def test_the_model_the_agent_runs_on_must_be_the_one_named() -> None:
    assert problems(f"{BODY}\n{SONNET}\n", "claude-sonnet-5-5") == []
    wrong = problems(f"{BODY}\n{OPUS}\n", "claude-sonnet-5-5")
    assert wrong and "Claude Sonnet 5.5" in wrong[0]
    # A branch with two models: the agent must still name its own.
    assert problems(f"{BODY}\n{SONNET}\n{OPUS}\n", "claude-sonnet-5-5")
    assert problems(f"{BODY}\n{SONNET}\n{OPUS}\n", "claude-opus-5-5")


def test_an_agent_commit_without_the_trailer_is_refused() -> None:
    missing = problems(BODY, "claude-sonnet-5-5")
    assert missing and "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>" in missing[0]


def test_a_model_id_that_is_not_a_claude_id_is_not_judged() -> None:
    assert problems(BODY, "gpt-5") == []
    assert problems(BODY, "") == []


def test_merge_messages_are_not_judged() -> None:
    assert problems("Merge branch 'main' into x\n", "claude-sonnet-5-5") == []


def test_what_git_writes_itself_is_not_judged_for_a_missing_trailer() -> None:
    # `git revert --no-edit`, `git commit --fixup` and `--squash` write the message; trailer_guard passed them too.
    for message in (
        'Revert "tools: Add a thing"\n\nThis reverts commit 0123abc.\n',
        "fixup! tools: Add a thing\n",
        "squash! tools: Add a thing\n",
        "amend! tools: Add a thing\n\nA better body, without a colon\n",
    ):
        assert problems(message, "claude-sonnet-5-5") == [], message
    # ... but a wrong or foreign trailer in them is still caught.
    assert problems(f'Revert "x"\n\n{OPUS}\n', "claude-sonnet-5-5")
    assert problems('Revert "x"\n\nSigned-off-by: a <noreply@anthropic.com>\n')


def test_a_squash_merge_message_lists_other_peoples_commits_and_is_not_judged() -> None:
    message = (
        "Squashed commit of the following:\n\ncommit 0123abc\nAuthor: Jane <jane@example.com>\n\n"
        "    tools: Add a thing\n\n    Co-Authored-By: Jane <jane@example.com>\n"
    )
    assert problems(message, "claude-sonnet-5-5") == []


def test_a_diff_below_gits_scissors_line_is_not_the_message() -> None:
    # `git commit -v` (or commit.verbose) puts the staged diff under this line; its e-mail addresses are not ours.
    diff = "diff --git a/AUTHORS b/AUTHORS\n+Jane <jane@example.com>\n+Signed-off-by: x"
    message = f"{BODY}\n{SONNET}\n{SCISSORS}\n# Do not modify or remove the line above.\n{diff}\n"
    assert problems(message, "claude-sonnet-5-5") == []
    # ... and the agent's trailer is still looked for above it, where it is.
    assert problems(f"{BODY}\n{OPUS}\n{SCISSORS}\n{diff}\n", "claude-sonnet-5-5")
    assert problems(f"{BODY}\n{SCISSORS}\n{SONNET}\n", "claude-sonnet-5-5")  # a trailer below it does not count


TEMPLATE = (
    "# Please enter the commit message for your changes. Lines starting\n"
    "# with '#' will be ignored, and an empty message aborts the commit.\n"
    "#\n"
    "# On branch tools-x\n"
)


def test_a_comment_line_of_a_message_given_with_dash_m_is_checked_because_git_keeps_it() -> None:
    # `git commit -m` and `-F` keep every line, `#` ones too (cleanup=whitespace), so an address there lands in history.
    message = f"{BODY}\n# Ask jane@example.com about it\n\n{SONNET}\n"
    found = problems(message)
    assert found and "jane@example.com" in found[0]
    assert problems(f"{BODY}\n# a note without an address\n\n{SONNET}\n") == []


def test_a_comment_line_in_the_editor_template_is_dropped_because_git_strips_it() -> None:
    message = f"{BODY}\n{SONNET}\n{TEMPLATE}# Changes to be committed:\n#\tmodified: jane@example.com.txt\n"
    assert problems(message) == []
    # ... but a line the author typed without the comment character is the message.
    assert problems(f"{BODY}\nAsk jane@example.com\n{TEMPLATE}")


@pytest.mark.parametrize("char", [";", "@", "!", "%"])
def test_the_comment_character_is_the_one_git_uses(char: str) -> None:
    template = TEMPLATE.replace("#", char)
    scissors = f"{char} ------------------------ >8 ------------------------"
    diff = "+Jane <jane@example.com>"
    # Found from the scissors line, whatever the setting says...
    assert problems(f"{BODY}\n{SONNET}\n{template}{scissors}\n{diff}\n") == []
    # ... and from the setting (core.commentChar) when there is no scissors line.
    assert commit_message.check_commit_message(f"{BODY}\n{SONNET}\n{template}", RULES, None, char, emails=EMAILS) == []
    # With the default `#`, a line starting with another character is part of the message.
    assert problems(f"{BODY}\n{SONNET}\n{char} jane@example.com\n")


@pytest.mark.parametrize("char", list(";@!$%^&|:"))
def test_the_template_of_core_comment_char_auto_is_found_in_any_character_git_picks(char: str) -> None:
    # `auto` has no setting to read and, without `git commit -v`, no scissors line; git writes its template in a
    # character no line of the message starts with, and strips those lines.
    template = TEMPLATE.replace("#", char) + f"{char} Author:    Jane <jane@example.com>\n"
    assert commit_message.check_commit_message(f"{BODY}\n{SONNET}\n{template}", RULES, emails=EMAILS) == []
    # Without the template the same line is part of the message.
    assert problems(f"{BODY}\n{char} Author:    Jane <jane@example.com>\n")


def test_core_comment_char_is_read_from_git(tmp_path: Path) -> None:
    import subprocess

    subprocess.run(["git", "init", "-q", str(tmp_path)], check=True)
    assert commit_message.configured_comment_char(tmp_path) is None
    subprocess.run(["git", "-C", str(tmp_path), "config", "core.commentChar", ";"], check=True)
    assert commit_message.configured_comment_char(tmp_path) == ";"
    subprocess.run(["git", "-C", str(tmp_path), "config", "core.commentChar", "auto"], check=True)
    assert commit_message.configured_comment_char(tmp_path) is None  # `auto`: found from the scissors line
    subprocess.run(["git", "-C", str(tmp_path), "config", "core.commentChar", "##"], check=True)
    assert commit_message.configured_comment_char(tmp_path) is None  # not one character: git refuses it too
    assert commit_message.configured_comment_char(tmp_path / "missing") is None


# --- parity with the Claude Code hook that is kept meanwhile ----------------------------------------------------------


@pytest.mark.parametrize(
    "model",
    [
        "claude-sonnet-5-5",
        "claude-opus-5-5[1m]",
        "claude-3-5-sonnet-20241022",
        "claude-opus-4-1-20250805",
        "gpt-5",
        "x",
    ],
)
def test_the_display_name_is_the_one_trailer_guard_uses(model: str) -> None:
    guard = load_hook_module("hooklib").load_guard("trailer_guard")
    assert commit_message.display_name(model) == guard.display_name(model)


# --- the command ------------------------------------------------------------------------------------------------------


def run(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    tmp_path: Path,
    message: str,
    *extra: str,
    env: dict[str, str] | None = None,
) -> tuple[int, str]:
    """Run `coney-tools pr check-commit` on a message file, from the repository; (exit code, stdout + stderr)."""
    for name in ("CONEY_AGENT_MODEL", "CONEY_AGENT_INJECTED", "CONEY_AGENT_TOKEN"):
        monkeypatch.delenv(name, raising=False)
    identity.reset()
    for name, value in (env or {}).items():
        monkeypatch.setenv(name, value)
    monkeypatch.chdir(REPO)
    file = tmp_path / "COMMIT_EDITMSG"
    file.write_text(message, encoding="utf-8")
    code = cli.main(["pr", "check-commit", str(file), *extra])
    out = capsys.readouterr()
    return code, out.out + out.err


def test_the_command_passes_and_refuses(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    assert run(monkeypatch, capsys, tmp_path, f"{BODY}\n{SONNET}\n") == (0, "commit message: ok\n")
    code, out = run(monkeypatch, capsys, tmp_path, f"{BODY}\nSigned-off-by: x <noreply@anthropic.com>\n")
    assert code == 1 and "trailer" in out and "AGENTS.md" in out


def hook_identity(folder: Path, model: str) -> dict[str, str]:
    """An identity file as the hook writes it (docs/guides/agents.md#identity); the environment that names it."""
    token = "0123456789abcdef0123456789abcdef"
    folder.mkdir(parents=True, exist_ok=True)
    data = {"version": 1, "id": "a1", "type": "engineer", "model": model, "issued": 0, "expires": 1e18}
    (folder / identity.file_name(token)).write_text(json.dumps(data), encoding="utf-8")
    return {"CONEY_AGENT_TOKEN": token}


def test_the_model_comes_from_the_option_or_from_the_identity_the_hook_wrote(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    message = f"{BODY}\n{OPUS}\n"
    assert run(monkeypatch, capsys, tmp_path, message, "--model", "claude-sonnet-5-5")[0] == 1
    monkeypatch.setattr(identity, "default_folder", lambda: tmp_path / "identity")
    sonnet = hook_identity(tmp_path / "identity", "claude-sonnet-5-5")
    assert run(monkeypatch, capsys, tmp_path, message, env=sonnet)[0] == 1
    opus = hook_identity(tmp_path / "identity", "claude-opus-5-5")
    assert run(monkeypatch, capsys, tmp_path, message, env=opus)[0] == 0


def test_typed_variables_and_a_token_without_a_file_judge_nothing(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    monkeypatch.setattr(identity, "default_folder", lambda: tmp_path / "identity")
    typed = {"CONEY_AGENT_MODEL": "claude-sonnet-5-5", "CONEY_AGENT_INJECTED": "1", "CONEY_AGENT_TOKEN": "f" * 32}
    # Unverified: the shape of the trailers is checked, a model is not demanded.
    assert run(monkeypatch, capsys, tmp_path, f"{BODY}\n{OPUS}\n", env=typed)[0] == 0
    assert run(monkeypatch, capsys, tmp_path, BODY, env=typed)[0] == 0


def test_a_message_that_is_not_utf8_is_still_checked(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    # An editor or a locale can write Latin-1; the check must not stop with a usage error on it.
    monkeypatch.chdir(REPO)
    file = tmp_path / "COMMIT_EDITMSG"
    file.write_bytes(BODY.encode() + b"\nCaf\xe9 au lait\n\n" + SONNET.encode() + b"\n")
    assert cli.main(["pr", "check-commit", str(file)]) == 0
    assert "commit message: ok" in capsys.readouterr().out
    file.write_bytes(BODY.encode() + b"\nCaf\xe9 jane@example.com\n")
    assert cli.main(["pr", "check-commit", str(file)]) == 1


def test_the_command_uses_the_comment_character_git_is_set_to(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    monkeypatch.setattr(commit_message, "configured_comment_char", lambda folder=None: ";")
    message = f"{BODY}\n{SONNET}\n; Please enter the commit message for your changes.\n; jane@example.com\n"
    assert run(monkeypatch, capsys, tmp_path, message) == (0, "commit message: ok\n")


def test_an_unreadable_file_is_a_usage_error(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str], tmp_path: Path
) -> None:
    monkeypatch.chdir(REPO)
    assert cli.main(["pr", "check-commit", str(tmp_path / "missing")]) == 2
    assert "cannot be read" in capsys.readouterr().err


def test_pre_commit_runs_it_at_the_commit_msg_stage() -> None:
    import yaml

    config = yaml.safe_load((REPO / ".pre-commit-config.yaml").read_text(encoding="utf-8"))
    assert "commit-msg" in config["default_install_hook_types"] and "pre-commit" in config["default_install_hook_types"]
    hooks = [hook for repo in config["repos"] for hook in repo["hooks"] if hook["id"] == "commit-message"]
    assert len(hooks) == 1 and hooks[0]["stages"] == ["commit-msg"]
    assert "coney-tools pr check-commit" in hooks[0]["entry"]
