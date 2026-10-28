# SPDX-License-Identifier: GPL-3.0-or-later
"""Claude Code PreToolUse hook: no `gh` command posts a comment or a review under the maintainer's account.

Nothing an agent runs posts a comment as the maintainer (docs/guides/issues.md#agents; the maintainer's decision of
2026-10-12). The user-level hook that first enforced it knew `gh issue comment`, `gh pr comment`, `gh pr review` and
`gh api`; `gh issue close --comment` put a closing comment on #772 on 2026-10-28 because a close was not a comment to
it. This hook, registered in `.claude/hooks/dispatch.py` for the Bash and PowerShell tools and run for every session
(the main one too, since the coordinator closes issues), refuses every other way a `gh` command carries text:

* `gh issue close`, `gh pr close`, `gh issue reopen` and `gh pr reopen` with `--comment` or `-c`;
* `gh pr merge` with `--body`, `-b`, `--body-file` or `-F`;
* `gh pr review` with `--body`, `-b`, `--body-file`, `-F`, or `--comment` / `-c` (the review type that is a comment);
* `gh api` calls that POST, PATCH or PUT to a comments or reviews endpoint (`.../issues/N/comments`,
  `.../pulls/N/comments`, `.../pulls/N/reviews`, `.../issues/comments/ID`, ...), and GraphQL mutations that add a
  comment or a review (`addComment`, `addPullRequestReview...`, `addDiscussionComment`, ...).

Every spelling the options have is read as gh reads it: `--comment x`, `--comment=x`, `-c x`, `-c=x`, `-cx`,
clusters (`-dc x`), quoted values and quoted option names. The same commands without text pass (`gh issue close 5`,
`gh pr merge 5 --squash`, `gh pr review 5 --approve`), and so do reads, `DELETE` calls, and a comment deleted by hand.
`gh issue comment` and `gh pr comment` stay with the user-level hook, which allows the one claim-command exception.

A command counts only where `gh` starts one (the helpers of `pr_guard_shell.py`: after `;`, `&`, `|`, `(`, `$(`, a
backtick, an assignment, `then`, `do`, `env` and the like, inside `bash -c "..."`), so text that merely mentions one
passes: an `echo`, a commit message, a `grep`, and the body of a here-document or here-string. `coney-tools`' own `gh`
calls are subprocesses, not tool calls, so they are never seen here. Not covered: a command started through another
program (`xargs` with a placeholder, a script file), a GraphQL query read from a file, a variable or a here-document,
and a session that is not run through these hooks. Exit code 2 blocks the call and shows stderr to the agent; every
other outcome (also input it cannot read) exits 0. Standard library only.
"""

import json
import re
import sys
from pathlib import Path

HOOKS = Path(__file__).resolve().parent
if str(HOOKS) not in sys.path:
    sys.path.insert(0, str(HOOKS))
from pr_guard_shell import _ARG, _GH, _START, _clean, _mask, _split  # noqa: E402

#: gh's own options before the command group (`-R owner/name`, `--hostname host`, other flags).
_GLOBALS = r"(?:[ \t]+(?:(?:-R|--repo|--hostname)(?:[ \t]+|=)" + _ARG + r"|--?[\w-]+(?:=" + _ARG + r")?))*"
#: A `gh` command that can carry a comment where a command starts: `gh issue|pr close|reopen|merge|review`, `gh api`.
_COMMAND = re.compile(
    _START
    + _GH
    + _GLOBALS
    + r"[ \t]+(?:(?P<group>issue|pr)[ \t]+(?P<verb>close|reopen|merge|review)|(?P<api>api))(?![\w-])",
    re.MULTILINE,
)
#: The options that carry text on every command above: long names, and the letters of the short ones (`-c`, `-b`, `-F`).
_TEXT_LONG = frozenset({"--comment", "--body", "--body-file"})
_TEXT_SHORT = "cbF"
#: The options whose value is the next word (so a value that looks like an option is not read as one).
_VALUE_LONG = frozenset(
    {"--reason", "--duplicate-of", "--subject", "--author-email", "--match-head-commit", "--repo", "--hostname"}
)
#: The short options of each command that take a value (`-r reason` of `issue close`; `-r` is a flag on a pull request).
_VALUE_SHORT = {
    ("issue", "close"): "rR",
    ("issue", "reopen"): "R",
    ("pr", "close"): "R",
    ("pr", "reopen"): "R",
    ("pr", "merge"): "tAR",
    ("pr", "review"): "R",
}
#: The text options as they look in unsplit text, for a call whose arguments cannot be split (an unbalanced quote).
_TEXT_RAW = re.compile(r"(?:^|[ \t])[\"']?(?:--(?:comment|body|body-file)\b|-[a-zA-Z]*[cbF])")

#: The options of `gh api` whose value is the next word, and the ones that make gh send a POST.
_API_VALUE_LONG = frozenset({"--header", "--jq", "--template", "--hostname", "--cache", "--preview"})
_API_VALUE_SHORT = "HqtpR"
_API_FIELD_LONG = frozenset({"--field", "--raw-field", "--input"})
#: The methods that write.
_WRITES = frozenset({"POST", "PATCH", "PUT"})
#: The path segments of a comments or reviews endpoint (`issues/N/comments`, `pulls/N/reviews/ID/events`, ...).
_COMMENT_SEGMENTS = frozenset({"comments", "reviews"})
#: The GraphQL mutations that add a comment or a review.
_MUTATION = re.compile(
    r"\badd(?:Comment|CommitComment|DiscussionComment|PullRequestReview\w*)\b|\bsubmitPullRequestReview\b"
)

MESSAGE = (
    "Blocked: {what}. Nothing is ever posted as a comment or review under the maintainer's GitHub account "
    "(decision of 2026-10-12): not by `gh issue close --comment`, `gh pr merge --body`, `gh pr review --body`, "
    "`gh api` to a comments endpoint, nor any other command. Run the same command without the comment "
    "(`gh issue close N`, `gh pr merge N --squash`), put the text you wanted to post in your scratch folder, and "
    "tell the coordinator or the maintainer, who posts it themselves. Status labels show that work is under way "
    "(docs/guides/enforcement.md#hooks, docs/guides/issues.md#agents).\n"
)


def _text_option(words: list[str], key: tuple[str, str]) -> str | None:
    """The first option of a close, reopen, merge or review command that carries text, as written; None when none."""
    short_values = _VALUE_SHORT[key]
    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        if word == "--":
            return None
        if word.startswith("--"):
            name = word.partition("=")[0]
            if name in _TEXT_LONG:
                return name
            if name in _VALUE_LONG and "=" not in word:
                index += 1
        elif word.startswith("-") and len(word) > 1:
            for position, letter in enumerate(word[1:]):
                if letter in _TEXT_SHORT:
                    return "-" + letter
                if letter in short_values:
                    if position == len(word) - 2:
                        index += 1  # the value is the next word
                    break  # or the rest of this one
    return None


def _api_posts_comment(words: list[str]) -> bool:
    """Whether the arguments of a `gh api` call (after `api`) write to a comments or reviews endpoint, or send a
    GraphQL mutation that adds a comment or a review."""
    method: str | None = None
    fields = False
    endpoints: list[str] = []
    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        if word == "--":
            endpoints.extend(words[index:])
            break
        if word.startswith("--"):
            name, equals, value = word.partition("=")
            if name == "--method":
                method, index = (value, index) if equals else (words[index] if index < len(words) else "", index + 1)
            elif name in _API_FIELD_LONG:
                fields = True
                index += 0 if equals else 1
            elif name in _API_VALUE_LONG and not equals:
                index += 1
        elif word.startswith("-") and len(word) > 1:
            letter, rest = word[1], word[2:]
            if letter == "X":
                if rest:
                    method = rest.lstrip("=")
                else:
                    method, index = (words[index] if index < len(words) else ""), index + 1
            elif letter in "fF":
                fields = True
                index += 0 if rest else 1
            elif letter in _API_VALUE_SHORT and not rest:
                index += 1
        else:
            endpoints.append(word)
    if any(_MUTATION.search(word) for word in words):
        return True
    verb = method.upper() if method else ("POST" if fields else "GET")
    if verb not in _WRITES:
        return False
    for endpoint in endpoints:
        path = re.split(r"[?#]", endpoint, maxsplit=1)[0]
        if _COMMENT_SEGMENTS & set(path.split("/")):
            return True
    return False


def refused(command: str, powershell: bool = False) -> str | None:
    """Why the command is refused, or None when no `gh` command in it posts a comment."""
    text = _clean(command)
    bare = _mask(text, powershell)
    for match in _COMMAND.finditer(bare):
        rest = text[match.end() :]
        words = _split(rest, powershell)
        if match.group("api"):
            if words is None:
                line = rest.split("\n", 1)[0]
                if re.search(r"\b/?comments\b|\breviews\b|\badd[A-Z]\w+", line) and re.search(
                    r"(?:^|[ \t])(?:-[fFX]|--(?:field|raw-field|input|method))", line
                ):
                    return MESSAGE.format(what="a `gh api` call that posts to a comments endpoint")
            elif _api_posts_comment(words):
                return MESSAGE.format(what="a `gh api` call that posts a comment or a review")
            continue
        key = (match.group("group"), match.group("verb"))
        name = f"gh {key[0]} {key[1]}"
        if words is None:
            if _TEXT_RAW.search(rest.split("\n", 1)[0]):
                return MESSAGE.format(what=f"`{name}` with a comment or a body")
            continue
        option = _text_option(words, key)
        if option is not None:
            return MESSAGE.format(what=f"`{name} {option}` posts a comment")
    return None


def main() -> int:
    """Read the hook's JSON from stdin; 2 (blocked, with the reason on stderr) or 0."""
    try:
        data = json.loads(sys.stdin.read() or "{}")
        command = data["tool_input"]["command"]
    except (ValueError, KeyError, TypeError, OSError):
        return 0
    if not isinstance(command, str):
        return 0
    reason = refused(command, data.get("tool_name") == "PowerShell")
    if reason is None:
        return 0
    sys.stderr.write(reason)
    return 2


if __name__ == "__main__":
    sys.exit(main())
