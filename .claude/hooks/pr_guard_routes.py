# SPDX-License-Identifier: GPL-3.0-or-later
"""The routes only the switch refuses, for `pr_guard.py`: which `git push` and which `gh api` calls a shell command
starts, read with the helpers of `pr_guard_shell.py`.

Standard library only, as the hooks run without packages. What the reading does not see is listed in
docs/guides/enforcement.md#pr-guard.
"""

import re
import sys
from dataclasses import dataclass
from pathlib import Path

HOOKS = Path(__file__).resolve().parent
if str(HOOKS) not in sys.path:
    sys.path.insert(0, str(HOOKS))
from pr_guard_shell import _ARG, _GH, _START, EXPANSION, _clean, _directory, _mask, _move, _split  # noqa: E402


@dataclass
class GitPush:
    """What one `git push` command asks for."""

    #: The folder it runs in; None when it cannot be told (a `cd` to an expansion, `--git-dir`, `GIT_DIR`).
    folder: str | None
    #: The remote it names (a name, an address or a path), else None for the default; an expansion is `EXPANSION`.
    remote: str | None
    #: `--dry-run` / `-n`, `--help` / `-h`: it writes nothing.
    harmless: bool


#: `git push` where a command starts, with git's own options in between: `--no-pager`, `--git-dir=x`, and those
#: whose value is the next word (`-C dir`, `-c key=value`, `--git-dir x`, `--work-tree x`, `--namespace x`).
_GIT_PUSH = re.compile(
    _START + r"(?:[^\s;&|()'\"`]*[/\\])?git(?:\.exe)?(?P<opts>(?:[ \t]+(?:"
    r"(?:-[Cc]|--git-dir|--work-tree|--namespace|--super-prefix|--config-env)[ \t]+"
    + _ARG
    + r"|--?[\w-]+(?:="
    + _ARG
    + r")?))*)[ \t]+push(?![\w-])",
    re.MULTILINE,
)
#: The folder of a `git -C <dir>` option.
_GIT_DIR = re.compile(r"(?:^|[ \t])-C[ \t]+(?P<dir>" + _ARG + ")")
#: A git option or setting that moves the repository or rewrites where a push goes, so the folder no longer tells
#: which repository it writes to: `--git-dir`, `GIT_DIR`, `--config-env` and git's config from the environment
#: (`GIT_CONFIG_COUNT`, `GIT_CONFIG_PARAMETERS`, `GIT_CONFIG_GLOBAL`), searched outside quotes, ...
_HIDDEN_REPOSITORY = re.compile(r"--git-dir\b|\bGIT_DIR\b|--config-env\b|\bGIT_CONFIG_\w+", re.IGNORECASE)
#: ... and `-c remote.*`, `url.*`, `branch.*`, `include.*` or `includeIf.*` among the push's own git options.
_HIDDEN_CONFIG = re.compile(r"(?:^|[ \t])-c[ \t]+[\"']?(?:remote|url|branch|include|includeIf)\.", re.IGNORECASE)
#: The long options of `git push` (from `git push -h`), which git also takes by a unique prefix and with `no-`.
_PUSH_LONG = (
    "all", "atomic", "branches", "delete", "dry-run", "exec", "follow-tags", "force", "force-if-includes",
    "force-with-lease", "ipv4", "ipv6", "mirror", "no-verify", "porcelain", "progress", "prune", "push-option",
    "quiet", "receive-pack", "recurse-submodules", "repo", "set-upstream", "signed", "tags", "thin", "verbose",
    "verify", "help",
)  # fmt: skip
#: Of those, the ones whose value is the next word when it is not given with `=`.
_PUSH_LONG_VALUE = frozenset({"exec", "push-option", "receive-pack", "repo"})
#: The short options of `git push` whose value is the rest of the word or the next word (`-o value`, `-ovalue`).
_PUSH_SHORT_VALUE = "o"


def _long_option(name: str) -> str | None:
    """The `git push` long option a spelling means (exact, its `no-` form, or a unique prefix); None when it is
    unknown or ambiguous."""
    names = [*_PUSH_LONG, *("no-" + option for option in _PUSH_LONG)]
    if name in names:
        return name
    found = [option for option in names if option.startswith(name)]
    return found[0] if len(found) == 1 else None


def _push_arguments(words: list[str]) -> tuple[str | None, bool]:
    """The remote a `git push` writes to and whether it only prints, from its words after `push`.

    As git reads them: a remote on the command line beats `--repo` (git-push(1)), the last of `--dry-run` and
    `--no-dry-run` wins, `-o` takes the rest of its word or the next one, `-h` or `--help` prints the usage. An option
    the hook does not know makes the remote `EXPANSION`: it cannot tell what the option does.
    """
    remote: str | None = None
    dry_run = False
    help_ = False
    unknown = False
    repo_option: str | None = None
    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        if word == "--":
            remote = remote if remote is not None else (words[index] if index < len(words) else None)
            break
        if word.startswith("--"):
            spelled, equals, value = word[2:].partition("=")
            option = _long_option(spelled)
            if option is None:
                unknown = True
            elif option == "help":
                help_ = True
            elif option in ("dry-run", "no-dry-run"):
                dry_run = option == "dry-run" and not equals  # git refuses `--dry-run=x`, which pushes nothing either
            elif option in _PUSH_LONG_VALUE and not equals:
                value = words[index] if index < len(words) else EXPANSION
                index += 1
            if option == "repo":
                repo_option = value
        elif word.startswith("-") and len(word) > 1:
            for position, letter in enumerate(word[1:], start=2):
                if letter == "h":
                    help_ = True
                elif letter == "n":
                    dry_run = True
                elif letter in _PUSH_SHORT_VALUE:
                    if position == len(word):
                        index += 1  # `-o value`; `-ovalue` keeps its value in the word
                    break
        elif remote is None:
            remote = word
    if unknown:
        return EXPANSION, help_
    return (remote if remote is not None else repo_option), dry_run or help_


def git_pushes(command: str, powershell: bool = False, cwd: str = "") -> list[GitPush]:
    """Every `git push` the command starts, with the folder it runs in and the remote it names."""
    text = _clean(command)
    bare = _mask(text, powershell)
    pushes: list[GitPush] = []
    for match in _GIT_PUSH.finditer(bare):
        folder = _directory(text, bare, match.start(), cwd, powershell)
        for option in _GIT_DIR.finditer(text, match.start("opts"), match.end("opts")):
            folder = None if folder is None else _move(folder, option.group("dir"))
        if _HIDDEN_REPOSITORY.search(bare, 0, match.end("opts")) or _HIDDEN_CONFIG.search(
            text, match.start("opts"), match.end("opts")
        ):
            folder = None
        words = _split(text[match.end() :], powershell)
        remote, harmless = _push_arguments(words if words is not None else [EXPANSION])
        pushes.append(GitPush(folder, remote, harmless))
    return pushes


#: `gh api` where a command starts, with gh's own options before it.
_GH_API = re.compile(
    _START + _GH + r"(?:[ \t]+(?:(?:-R|--repo|--hostname)(?:[ \t]+|=)" + _ARG + r"|--?[\w-]+(?:=" + _ARG + r")?))*"
    r"[ \t]+api(?=[ \t]|$)",
    re.MULTILINE,
)
#: The method of a `gh api` call: `-X POST`, `-XPOST`, `--method POST` or `--method=POST`.
_METHOD = re.compile(r"(?:^|[ \t])(?:-X[ \t]*=?|--method(?:[ \t]+|=))[\"']?(?P<method>\w+)", re.IGNORECASE)
#: A field or input of a `gh api` call, which makes gh send a POST unless a method is given.
_FIELDS = re.compile(r"(?:^|[ \t])(?:-[fF]|--field|--raw-field|--input)")
#: A REST endpoint, with its quotes dropped and perhaps a host, always `repos/<owner>/<name>/...` (the owner and name
#: are captured; `{owner}/{repo}` are placeholders gh fills in from the checkout): `.../pulls`, nothing after it, which
#: opens a pull request; or `.../pulls/<number>`, which a PATCH rewrites (title, body, base, state).
_REPO = r"(?:^|[ \t/])repos/(?P<owner>[^/\s]+)/(?P<name>[^/\s]+)"
_PULLS = re.compile(_REPO + r"/pulls/?(?=[ \t?#]|$)")
_PULL = re.compile(_REPO + r"/pulls/(?:\d+|\{[^}\s]+\})/?(?=[ \t?#]|$)")
#: `.../pulls/<number>/update-branch`, which a PUT moves: GitHub merges the base branch into it.
_UPDATE_BRANCH = re.compile(_REPO + r"/pulls/(?:\d+|\{[^}\s]+\})/update-branch/?(?=[ \t?#]|$)")
#: The git data and repository endpoints that move a branch without `git push`: `.../git/refs` and `.../git/refs/<ref>`
#: (POST makes a ref, PATCH moves it, DELETE removes it), `.../merges` (POST merges one branch into another) and
#: `.../contents/<path>` (PUT or DELETE commits a file to a branch).
_REFS = re.compile(_REPO + r"/git/refs(?:/[^\s?#]*)?/?(?=[ \t?#]|$)")
_MERGES = re.compile(_REPO + r"/merges/?(?=[ \t?#]|$)")
_CONTENTS = re.compile(_REPO + r"/contents/[^\s?#]+")
#: A GraphQL query the hook cannot read: from a file (`query=@mut.graphql`), a variable (`query="$Q"`) or `--input`.
_HIDDEN_QUERY = re.compile(r"(?:^|[ \t])(?:-[fF]|--field|--raw-field)(?:[ \t]+|=)?[\"']?query=[\"']?[@$]|--input")
#: The endpoint of a GraphQL call; only a call to it can run a mutation.
_GRAPHQL = re.compile(r"(?:^|[ \t])[\"']?graphql[\"']?(?=[ \t]|$)")
#: The GraphQL mutations that open a pull request, rewrite its text or turn on auto-merge ...
_PR_MUTATIONS = ("createPullRequest", "updatePullRequest", "enablePullRequestAutoMerge")
#: ... and those that move a branch: `updatePullRequestBranch`, a commit made on a branch, a ref made, moved or deleted,
#: a branch merged into another.
_BRANCH_MUTATIONS = (
    "updatePullRequestBranch",
    "createCommitOnBranch",
    "createRef",
    "updateRef",
    "deleteRef",
    "mergeBranch",
)


#: Where a GraphQL mutation starts: the word `mutation`, an optional name and variables, then the selection's `{`.
_MUTATION_START = re.compile(r"\bmutation\b\s*(?:\w+\s*)?(?:\([^)]*\)\s*)?\{")
#: A GraphQL string, as the shell passes it: bare inside a single-quoted argument, but with its quotes escaped (`\"` in
#: bash, a backtick in PowerShell) inside a double-quoted one.
_STRING_BARE = re.compile(r'"(?:[^"\\]|\\.)*"', re.DOTALL)
_STRING_ESCAPED = re.compile(r'(?:\\|`)"(?:(?!(?:\\|`)").)*(?:\\|`)"', re.DOTALL)
#: A GraphQL comment, to the end of its line.
_COMMENT = re.compile(r"#[^\n]*")


def _mutation(args: str, names: tuple[str, ...]) -> bool:
    """Whether a GraphQL document in `args` is a mutation that calls one of `names`: the word `mutation`, then a field
    of that name with an argument list. The name in a comment, a title, a string argument (an `addComment` body) or a
    query is no mutation."""
    call = re.compile(r"\b(?:" + "|".join(names) + r")\b\s*\(")
    inside: list[tuple[int, int]] = []
    for start in _MUTATION_START.finditer(args):
        if any(lo <= start.start() < hi for lo, hi in inside):  # the word sits in a string of an earlier one: text
            continue
        before = args[: start.start()]
        quote = before[max(before.rfind("'"), before.rfind('"')) :][:1]
        strings = _STRING_ESCAPED if quote == '"' else _STRING_BARE
        # strings and comments are blanked from the mutation's start on, so a name in them is no call
        spans = [found.span() for found in strings.finditer(args, start.start())]
        document = list(args)
        for lo, hi in spans:
            document[lo:hi] = " " * (hi - lo)
        text = _COMMENT.sub(lambda found: " " * len(found.group(0)), "".join(document[start.start() :]))
        if call.search(text):
            return True
        inside += spans
    return False


@dataclass
class ApiRoute:
    """One `gh api` call the switch refuses."""

    #: Why it is refused.
    reason: str
    #: The `owner/name`, lower-cased, the endpoint names; None for a GraphQL call, which names none, and for the
    #: placeholders `{owner}/{repo}`, which gh fills in from the folder the command runs in.
    repository: str | None = None
    #: Whether the endpoint names `{owner}/{repo}`: the repository is then the folder's.
    placeholder: bool = False


def _route(reason: str, found: re.Match[str]) -> ApiRoute:
    """The refusal of a REST endpoint, with the repository it names."""
    owner, name = found.group("owner"), found.group("name")
    if owner.startswith("{") or name.startswith("{"):
        return ApiRoute(reason, None, True)
    return ApiRoute(reason, f"{owner}/{name}".lower())


def api_routes(command: str, powershell: bool = False) -> list[ApiRoute]:
    """Each `gh api` call of the command that the switch refuses, and the repository its endpoint names: a POST to
    `pulls`, a PATCH of `pulls/<n>`, a PUT of `pulls/<n>/update-branch`, a POST, PATCH or DELETE of `git/refs`, a POST
    of `merges`, a PUT or DELETE of `contents/<path>`, a GraphQL mutation of `_PR_MUTATIONS` or `_BRANCH_MUTATIONS`
    (read as a mutation, not as any text), or a GraphQL query the hook cannot read."""
    text = _clean(command)
    bare = _mask(text, powershell)
    routes: list[ApiRoute] = []
    for match in _GH_API.finditer(bare):
        end = re.search(r"[;&|)\n]", bare[match.end() :])
        stop = match.end() + end.start() if end else len(text)
        args = text[match.end() : stop]
        endpoint = args.replace("'", " ").replace('"', " ")
        method = _METHOD.search(args)
        verb = method.group("method").upper() if method else ("POST" if _FIELDS.search(args) else "GET")
        found: re.Match[str] | None
        if verb == "POST" and (found := _PULLS.search(endpoint)):
            routes.append(_route("`gh api` opens a pull request by hand.", found))
        elif verb == "PATCH" and (found := _PULL.search(endpoint)):
            routes.append(_route("`gh api` rewrites a pull request by hand.", found))
        elif verb == "PUT" and (found := _UPDATE_BRANCH.search(endpoint)):
            routes.append(_route("`gh api` moves a pull request's branch by hand.", found))
        elif verb in ("POST", "PATCH", "DELETE") and (found := _REFS.search(endpoint)):
            routes.append(
                _route(
                    "`gh api` makes, moves or deletes a git ref by hand, which moves a pull request's branch.", found
                )
            )
        elif verb == "POST" and (found := _MERGES.search(endpoint)):
            routes.append(
                _route("`gh api` merges one branch into another by hand, which moves a pull request's branch.", found)
            )
        elif verb in ("PUT", "DELETE") and (found := _CONTENTS.search(endpoint)):
            routes.append(
                _route("`gh api` commits a file to a branch by hand, which moves a pull request's branch.", found)
            )
        elif _GRAPHQL.search(args) and _mutation(args, _BRANCH_MUTATIONS):
            routes.append(
                ApiRoute("`gh api graphql` moves a pull request's branch by hand (a commit, a ref or a merge).")
            )
        elif _GRAPHQL.search(args) and _mutation(args, _PR_MUTATIONS):
            routes.append(ApiRoute("`gh api graphql` opens, rewrites or sets auto-merge on a pull request by hand."))
        elif _GRAPHQL.search(args) and _HIDDEN_QUERY.search(args):
            routes.append(
                ApiRoute("`gh api graphql` reads its query from a file or variable, which the hook cannot read.")
            )
    return routes
