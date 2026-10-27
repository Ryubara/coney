# SPDX-License-Identifier: GPL-3.0-or-later
"""The `coney-tools issue ...` commands: draft an issue, check it, and file, edit or link it only after the check.

An issue filed with a bare `gh issue create --body` skips the issue form, so nothing guarded its sections, its
labels or its links. These commands put the guard before the write: `draft` prints what to fill in, `check` says what
is wrong, and `create`, `edit` and `link` are the only way agents and the coordinator write an issue to GitHub
(docs/guides/issues.md#filing). The rules come from `.github/conventions.yml` and the form files through
`coney_tools.issue_form`. `gh` is run as a subprocess through an injected runner, so the tests never touch the network;
`--dry-run` prints the commands without running any that writes.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import tempfile
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path

from coney_tools import (
    audit_files,
    audit_github,
    audit_text,
    issue_form,
    issue_impact,
    issue_impact_open,
    issue_impact_report,
    issue_titles,
)
from coney_tools.audit_github import Runner
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.conventions import Conventions, load_conventions
from coney_tools.unit import scratch_folder

#: The first line of the comment the issue-check workflow keeps on an issue, which finds it again to update or delete.
COMMENT_MARKER = "<!-- coney-issue-check -->"
#: The number at the end of the URL `gh issue create` prints.
_CREATED = re.compile(r"/issues/(?P<number>\d+)\s*$")


def add_issue_parser(groups: argparse._SubParsersAction[argparse.ArgumentParser]) -> None:
    """Add the `issue` group: `draft`, `check`, `create`, `edit`, `link`, `titles` and `impact`."""
    group = groups.add_parser(
        "issue", help="draft, check and file issues in their form's shape (docs/guides/issues.md)"
    )
    commands = group.add_subparsers(dest="command", required=True)

    draft = commands.add_parser(
        "template",
        help="print a blank issue body in the form's exact headings, with the rules of each section as comments",
    )
    draft.add_argument("form", nargs="?", help="the form (task or bug); default: the form the command line uses")

    def body_options(command: argparse.ArgumentParser, title_required: bool) -> None:
        """The options `check`, `create` and `edit` share."""
        command.add_argument("--title", required=title_required, help="the issue title")
        command.add_argument("--body", required=True, help="a file holding the issue body; - reads stdin")
        command.add_argument("--form", help="the form the body follows (default: from the title and headings)")
        command.add_argument("--leftover", action="store_true", help="the issue is a leftover: it needs a From: line")
        command.add_argument(
            "--attach",
            action="append",
            default=[],
            metavar="FILE",
            help="a capture to upload with the issue (gh 2.99+)",
        )

    check = commands.add_parser(
        "check", help="check a title and body against the form and the conventions; exit 1 on a problem"
    )
    body_options(check, True)
    check.add_argument("--online", action="store_true", help="also check the open/merged/closed words against gh")
    check.add_argument(
        "--markdown", action="store_true", help="print the problems as the comment the issue-check workflow posts"
    )

    create = commands.add_parser("create", help="check, then file the issue with the form's labels (the only way)")
    body_options(create, True)
    create.add_argument("--label", action="append", default=[], help="an extra label (validated against the labels)")
    create.add_argument("--parent", type=int, metavar="N", help="make the new issue a sub-issue of issue N")
    create.add_argument("--offline", action="store_true", help="do not read the cited issues' states with gh")
    create.add_argument("--dry-run", action="store_true", help="check and print what would run; write nothing")

    edit = commands.add_parser("edit", help="check, then replace the body (and title) of issue N")
    edit.add_argument("number", type=int, help="the issue number")
    body_options(edit, False)
    edit.add_argument("--offline", action="store_true", help="do not read the cited issues' states with gh")
    edit.add_argument("--dry-run", action="store_true", help="check and print what would run; write nothing")

    titles = commands.add_parser(
        "titles",
        help="list the open issues whose title breaks 'area: Verb the rest', with a proposed title and its gh command",
    )
    titles.add_argument("--json", action="store_true", help="print the proposals as JSON")
    titles.add_argument("--apply", action="store_true", help="rename the issues (the coordinator's, once)")
    titles.add_argument(
        "--include-review", action="store_true", help="with --apply, also rename the proposals marked 'review'"
    )
    titles.add_argument("--limit", type=int, default=500, help="how many open issues to read (default: %(default)s)")

    impact = commands.add_parser(
        "impact",
        help="list the open issues whose bodies cite what the branch touches (for the Issues affected section)",
    )
    impact.add_argument(
        "--base", help="the branch the change is made against (default: origin/ and the default branch)"
    )
    impact.add_argument("--head", default="HEAD", help="the tip of the change (default: %(default)s)")
    impact.add_argument(
        "--replaces",
        action="append",
        default=[],
        metavar="PR_OR_BRANCH",
        help="a pull request number or a branch the change replaces; the commits' 'Replaces #N' lines count too",
    )
    impact.add_argument(
        "--description", metavar="FILE", help="the pull request text: its 'Fixes #N' issues are left out of the list"
    )
    impact.add_argument(
        "--json", action="store_true", help="print the report as JSON (version 1, a stable output for tools to read)"
    )
    impact.add_argument(
        "--refresh", action="store_true", help="read the open issues from GitHub even if the cache is fresh"
    )
    impact.add_argument(
        "--issues", metavar="FILE", help="read the open issues from this `gh issue list --json` file, not GitHub"
    )

    link = commands.add_parser("set-parent", help="make issue N a sub-issue of issue --parent")
    link.add_argument("number", type=int, help="the child issue")
    link.add_argument("--parent", type=int, required=True, metavar="P", help="the parent issue")
    link.add_argument("--dry-run", action="store_true", help="print what would run; write nothing")


# --- reading the inputs ---------------------------------------------------------------------------------------------


def _read_body(source: str) -> str:
    """The body from the file `source` (`-` for stdin); ConfigError when it cannot be read."""
    try:
        text = sys.stdin.read() if source == "-" else Path(source).read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise ConfigError(f"--body {source}: cannot be read ({error})") from error
    return text.replace("\r\n", "\n")


def _gh_states(run: Runner, unread: list[int]) -> Callable[[int], str | None]:
    """A lookup of the state (`open`, `closed`, `merged`) of an issue or pull request through gh; numbers gh could
    not read are appended to `unread`."""
    cache: dict[int, str | None] = {}

    def lookup(number: int) -> str | None:
        if number not in cache:
            state = None
            for noun in ("pr", "issue"):
                try:
                    state = str(json.loads(run([noun, "view", str(number), "--json", "state"]))["state"]).lower()
                    break
                except (OSError, ValueError, KeyError, TypeError):
                    continue
            cache[number] = state
            if state is None:
                unread.append(number)
        return cache[number]

    return lookup


def _check(
    args: argparse.Namespace, title: str, body: str, run: Runner, online: bool
) -> tuple[Conventions, issue_form.FormSpec, issue_form.CheckResult]:
    """Run the check of `body` as the form it follows; also returns the conventions and the form."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    name = args.form or issue_form.detect_form(title, body, conventions)
    spec = issue_form.load_form(root, conventions, name)
    unread: list[int] = []
    options = issue_form.CheckOptions(
        leftover=args.leftover,
        tree=audit_github.git_tree(root, f"origin/{conventions.repo.default_branch}"),
        states=_gh_states(run, unread) if online else None,
        address_terms=audit_files.repository_address_terms(root),
        uploaded_only=not args.attach,
    )
    result = issue_form.check_issue(title, body, spec, conventions, options)
    if unread:
        result.notices.append(
            f"gh could not read {', '.join(f'#{n}' for n in sorted(set(unread)))}: their states were not checked"
        )
    return conventions, spec, result


def _print_result(result: issue_form.CheckResult) -> None:
    """Print the notices and the problems of a check, one per line."""
    for notice in result.notices:
        print(f"note: {notice}")
    for problem in result.problems:
        print(f"  [{problem.kind}] {problem.message}")
    count = len(result.problems)
    print(f"issue check ({result.form} form): {'ok' if not count else f'{count} problem(s)'}")


# --- the commands ---------------------------------------------------------------------------------------------------


def run_draft(form: str | None) -> int:
    """`issue template`: print the blank body of a form."""
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    spec = issue_form.load_form(root, conventions, form or conventions.issues.cli_uses_form)
    print(issue_form.draft_body(spec, conventions), end="")
    return 0


def run_check(args: argparse.Namespace, run: Runner = audit_github.run_gh) -> int:
    """`issue check`: print what is wrong with the title and body; 0 when nothing, 1 otherwise."""
    result = _check(args, args.title, _read_body(args.body), run, args.online)[2]
    if args.markdown:
        if result.problems:
            print(markdown_comment(result))
    else:
        _print_result(result)
    return 1 if result.problems else 0


def markdown_comment(result: issue_form.CheckResult) -> str:
    """The comment that tells the author of an issue what to fix, headed by `COMMENT_MARKER`."""
    lines = [
        COMMENT_MARKER,
        f"This issue does not match the {result.form} form yet. `coney-tools issue check` found "
        f"{len(result.problems)} problem(s):",
        "",
    ]
    lines += [f"- **{problem.kind}**: {problem.message}" for problem in result.problems]
    lines += [
        "",
        f"`coney-tools issue template {result.form}` prints the form's exact shape, and "
        "[Filing an issue](../blob/main/docs/guides/issues.md#filing) explains the rules. Edit the issue; this "
        "comment is updated when you do, and removed once the issue passes.",
    ]
    return "\n".join(lines)


def _write_body(body: str, folder: Path) -> Path:
    """The body without the draft's comments, in a file in `folder`, for `gh --body-file`."""
    path = folder / "body.md"
    path.write_text(audit_text.clean(body).strip() + "\n", encoding="utf-8", newline="\n")
    return path


def _show(command: list[str]) -> None:
    """Print a gh command as it would be typed."""
    print("gh " + " ".join(f'"{word}"' if " " in word else word for word in command))


def link_sub_issue(number: int, parent: int, run: Runner, dry_run: bool) -> None:
    """Make issue `number` a sub-issue of `parent`: the endpoint takes the child's database id, not its number.

    OSError with gh's message when a call fails.
    """
    if number == parent:
        raise ConfigError("an issue cannot be its own parent")
    lookup = ["api", f"repos/{{owner}}/{{repo}}/issues/{number}", "--jq", ".id"]
    if dry_run:
        _show(lookup)
        _show(["api", f"repos/{{owner}}/{{repo}}/issues/{parent}/sub_issues", "-X", "POST", "-F", "sub_issue_id=<id>"])
        return
    identifier = run(lookup).strip()
    if not identifier.isdigit():
        raise OSError(f"gh api did not return the id of #{number}: {identifier!r}")
    run(
        [
            "api",
            f"repos/{{owner}}/{{repo}}/issues/{parent}/sub_issues",
            "-X",
            "POST",
            "-F",
            f"sub_issue_id={identifier}",
        ]
    )
    print(f"linked #{number} as a sub-issue of #{parent}")


def run_create(args: argparse.Namespace, run: Runner = audit_github.run_gh) -> int:
    """`issue create`: check, then file the issue with its labels and link it to its parent; 1 if the check fails."""
    body = _read_body(args.body)
    conventions, spec, result = _check(args, args.title, body, run, not args.offline)
    _print_result(result)
    if result.problems:
        return 1
    labels, label_problems = issue_form.labels_for(spec, result.area, args.label, conventions)
    for message in label_problems:
        print(f"  [labels] {message}")
    if label_problems:
        return 1
    if args.leftover and args.parent is None:
        print(
            "note: a leftover is a sub-issue of the issue its source pull request fixes: add --parent N if it has one"
        )
    with tempfile.TemporaryDirectory() as folder:
        path = _write_body(body, Path(folder))
        command = ["issue", "create", "--title", args.title, "--body-file", str(path)]
        for label in labels:
            command += ["--label", label]
        for capture in args.attach:
            command += ["--attach", capture]
        if args.dry_run:
            _show(command)
            if args.parent is not None:
                link_sub_issue(0, args.parent, run, True)
            print("dry run: nothing was written")
            return 0
        try:
            created = run(command).strip()
        except OSError as error:
            print(f"coney-tools: the issue was not created: {error}", file=sys.stderr)
            return 1
    print(created)
    found = _CREATED.search(created)
    if args.parent is not None:
        if found is None:
            print(
                f"coney-tools: could not read the new issue's number from {created!r}; link it with `issue set-parent`",
                file=sys.stderr,
            )
            return 1
        try:
            link_sub_issue(int(found.group("number")), args.parent, run, False)
        except OSError as error:
            print(
                f"coney-tools: the issue exists ({created}) but linking it to #{args.parent} failed: {error}\n"
                f"  link it with: coney-tools issue set-parent {found.group('number')} --parent {args.parent}",
                file=sys.stderr,
            )
            return 1
    return 0


def run_edit(args: argparse.Namespace, run: Runner = audit_github.run_gh) -> int:
    """`issue edit`: check the new body (with the new or the current title), then replace it; 1 if the check fails."""
    body = _read_body(args.body)
    title = args.title
    if title is None:
        try:
            title = str(json.loads(run(["issue", "view", str(args.number), "--json", "title"]))["title"])
        except (OSError, ValueError, KeyError, TypeError) as error:
            raise ConfigError(f"cannot read the title of #{args.number} with gh ({error}); give --title") from error
    result = _check(args, title, body, run, not args.offline)[2]
    _print_result(result)
    if result.problems:
        return 1
    with tempfile.TemporaryDirectory() as folder:
        path = _write_body(body, Path(folder))
        command = ["issue", "edit", str(args.number), "--body-file", str(path)]
        if args.title is not None:
            command += ["--title", args.title]
        for capture in args.attach:
            command += ["--attach", capture]
        if args.dry_run:
            _show(command)
            print("dry run: nothing was written")
            return 0
        try:
            run(command)
        except OSError as error:
            print(f"coney-tools: #{args.number} was not edited: {error}", file=sys.stderr)
            return 1
    print(f"edited #{args.number}")
    return 0


def run_link(args: argparse.Namespace, run: Runner = audit_github.run_gh) -> int:
    """`issue set-parent`: make an existing issue a sub-issue of another."""
    try:
        link_sub_issue(args.number, args.parent, run, args.dry_run)
    except OSError as error:
        print(f"coney-tools: could not link #{args.number} to #{args.parent}: {error}", file=sys.stderr)
        return 1
    if args.dry_run:
        print("dry run: nothing was written")
    return 0


def run_titles(args: argparse.Namespace, run: Runner = audit_github.run_gh) -> int:
    """`issue titles`: list the open issues with a title that breaks the rule and what to rename each to.

    Prints the proposals and, below them, the `gh issue edit` commands; with `--apply` it runs the `auto` ones (and the
    `review` ones with `--include-review`). Exit 1 when some issue still needs a new title, 0 when none does.
    """
    conventions = load_conventions(find_repo_root(Path.cwd()))
    try:
        issues = json.loads(
            run(["issue", "list", "--state", "open", "--limit", str(args.limit), "--json", "number,title,labels"])
        )
    except (OSError, ValueError) as error:
        raise ConfigError(f"cannot read the open issues with gh ({error})") from error
    proposals = issue_titles.list_retitles(issues, conventions)
    if args.json:
        print(json.dumps([retitle.__dict__ for retitle in proposals], indent=2))
    else:
        for retitle in proposals:
            print(f"#{retitle.number} [{retitle.confidence}] {retitle.current!r}")
            print(f"    -> {retitle.proposed!r}  ({retitle.why})")
        print(f"{len(proposals)} of {len(issues)} open issue(s) break the title rule")
        for retitle in proposals:
            print(retitle.command())
    if not args.apply:
        return 1 if proposals else 0
    failed = 0
    for retitle in proposals:
        if retitle.confidence != "auto" and not args.include_review:
            continue
        try:
            run(["issue", "edit", str(retitle.number), "--title", retitle.proposed])
        except OSError as error:
            print(f"coney-tools: #{retitle.number} was not renamed: {error}", file=sys.stderr)
            failed += 1
        else:
            print(f"renamed #{retitle.number}")
    return 1 if failed else 0


def _commit_messages(root: Path, base: str, head: str) -> list[str]:
    """The messages of the commits `base..head`, one string each; empty when git cannot list them."""
    try:
        text = issue_impact.git_output(root, "log", "--format=%B%x00", f"{base}..{head}")
    except ConfigError:
        return []
    return [message for message in text.split("\0") if message.strip()]


def run_impact(
    args: argparse.Namespace,
    run: Runner = audit_github.run_gh,
    now: Callable[[], datetime] = lambda: datetime.now(UTC),
) -> int:
    """`issue impact`: list the open issues the branch may have changed, with why each matched.

    The list is a suggestion for the Issues affected section of the pull request, never the section itself: the author
    reads each issue. Exit 0 whether or not anything matched; 2 when the refs or the issues cannot be read.
    """
    root = find_repo_root(Path.cwd())
    conventions = load_conventions(root)
    rules = conventions.issues.impact
    base = args.base or f"origin/{conventions.repo.default_branch}"
    description = ""
    if args.description:
        try:
            description = Path(args.description).read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as error:
            raise ConfigError(f"--description {args.description}: cannot be read ({error})") from error
    messages = _commit_messages(root, base, args.head)
    replaces = [f"#{ref.lstrip('#')}" if ref.lstrip("#").isdigit() else ref for ref in args.replaces]
    touched = issue_impact.gather(root, base, args.head, messages, description, replaces)
    touched.branch = _branch_name(root, args.head)
    notices: list[str] = []
    if args.issues:
        try:
            issues = issue_impact_open.parse_issues(json.loads(Path(args.issues).read_text(encoding="utf-8")))
        except (OSError, ValueError) as error:
            raise ConfigError(f"--issues {args.issues}: cannot be read ({error})") from error
    else:
        cache = scratch_folder(root) / "issue-impact" / "open-issues.json"
        issues, notices = issue_impact_open.read_open_issues(cache, rules, run, now(), args.refresh)
    candidates, excluded = issue_impact.match_issues(issues, touched, rules)
    if args.json:
        print(json.dumps(issue_impact_report.report_json(touched, candidates, excluded, notices), indent=2))
    else:
        for line in issue_impact_report.report_lines(
            candidates, excluded, [*touched.notices, *notices], rules.max_reasons
        ):
            print(line)
    return 0


def _branch_name(root: Path, head: str) -> str:
    """The branch `head` names (`HEAD` is the checked-out one); empty for a detached HEAD."""
    try:
        name = issue_impact.git_output(root, "rev-parse", "--abbrev-ref", head).strip()
    except ConfigError:
        return ""
    return "" if name == "HEAD" else name


def run_issue(args: argparse.Namespace) -> int:
    """Dispatch an `issue` command."""
    if args.command == "impact":
        return run_impact(args)
    if args.command == "titles":
        return run_titles(args)
    if args.command == "template":
        return run_draft(args.form)
    if args.command == "check":
        return run_check(args)
    if args.command == "create":
        return run_create(args)
    if args.command == "edit":
        return run_edit(args)
    return run_link(args)
