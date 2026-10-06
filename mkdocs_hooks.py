"""MkDocs hooks: the repository's and the site's addresses, worked out at build time.

No file names the repository's or the site's address: they come from the build. Pages link files outside `docs/` as
`repo:<path>`, which this hook turns into a link to that file on the default branch of the repository the site is
built from. It also adds the Changelog page, written from the git history at build time.
"""

import os
import re
import subprocess
from datetime import UTC, datetime
from pathlib import Path

# A `repo:` link target in Markdown: `](repo:LEGAL.md#no-game-data)`.
_REPO_LINK = re.compile(r"\]\(repo:([^)\s]+)\)")


def _repo_url():
    """The repository's web address: from GitHub Actions in CI, otherwise from the `origin` remote."""
    server, repo = os.environ.get("GITHUB_SERVER_URL"), os.environ.get("GITHUB_REPOSITORY")
    if server and repo:
        return f"{server}/{repo}"
    try:
        remote = subprocess.run(
            ["git", "remote", "get-url", "origin"], capture_output=True, text=True, check=True
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None
    # git@github.com:owner/repo.git and https://github.com/owner/repo.git both become https://github.com/owner/repo.
    remote = re.sub(r"^git@([^:]+):", r"https://\1/", remote)
    return remote.removesuffix(".git") or None


def on_config(config):
    """Sets repo_url from the build's repository and site_url from CONEY_SITE_URL (set by CI from Pages)."""
    url = _repo_url()
    if url:
        config["repo_url"] = url
        # MkDocs works the name out from repo_url only while it validates mkdocs.yml, before this hook runs, so the
        # header would show "None". The repository's own name.
        config["repo_name"] = url.rstrip("/").rsplit("/", 1)[-1]
    site = os.environ.get("CONEY_SITE_URL")
    if site:
        config["site_url"] = site
    return config


def on_page_markdown(markdown, config, **_):
    """Rewrites `repo:<path>` links to the file on the default branch, before MkDocs validates links."""
    base = config.get("repo_url")
    if not base:
        # No repository to point at (a copy without git): an empty anchor keeps the strict build passing.
        return _REPO_LINK.sub("](#)", markdown)
    return _REPO_LINK.sub(lambda m: f"]({base.rstrip('/')}/blob/main/{m.group(1)})", markdown)


# Markdown punctuation that could change how a commit title renders; each is backslash-escaped.
_MARKDOWN_SPECIAL = re.compile(r"([\\`*_{}\[\]<>()#+!|~&])")

# One commit as `git log` reports it: abbreviated hash, committer time in Unix seconds, subject line.
Commit = tuple[str, int, str]

_INTRO = (
    "This page lists every commit on `main`, grouped by day, newest first. Titles follow the "
    '[commit conventions](guides/review/commits.md) and the "Commits and GitHub" rules in '
    "[AGENTS.md](repo:AGENTS.md). It is written from the git history each time the site is built."
)


def _escape(text):
    """Backslash-escapes Markdown punctuation so a commit title shows exactly as written."""
    return _MARKDOWN_SPECIAL.sub(lambda m: "\\" + m.group(1), text)


def render_changelog(commits, repo_url=None, note=None):
    """The Markdown of the Changelog page for `commits` (newest first) as (short hash, Unix time, subject) tuples.

    Commits are grouped by UTC day, one collapsible block per day with the newest expanded. A hash links to its commit
    page when `repo_url` is known. With no commits, `note` (or a default line) says why the list is empty.
    """
    lines = ["# Changelog", "", _INTRO, ""]
    if not commits:
        lines.append(note or "No commits are available to list.")
        return "\n".join(lines) + "\n"
    # Group in order: the input is newest first, so the first day met is the newest and later days are older.
    days = {}
    for short, stamp, subject in commits:
        # UTC, not local time, so the same history renders the same on any builder.
        day = datetime.fromtimestamp(stamp, UTC).strftime("%Y-%m-%d")
        days.setdefault(day, []).append((short, subject))
    for index, (day, entries) in enumerate(days.items()):
        # `???+` opens the block, `???` leaves it closed (pymdownx.details).
        lines += [f'{"???+" if index == 0 else "???"} note "{day}"', ""]
        for short, subject in entries:
            # Escaped brackets show the hash as `[abc1234]`, linked to its commit when the repository is known.
            link = f"[{short}]({repo_url.rstrip('/')}/commit/{short})" if repo_url else f"`{short}`"
            label = rf"\[{link}\]"
            lines.append(f"    - {label} {_escape(subject)}")
        lines.append("")
    return "\n".join(lines) + "\n"


def _git_commits(root):
    """The first-parent history of HEAD as `Commit` tuples, or (None, reason) when it cannot be read in full."""
    try:
        shallow = subprocess.run(
            ["git", "rev-parse", "--is-shallow-repository"], cwd=root, capture_output=True, text=True, check=True
        ).stdout.strip()
        if shallow != "false":
            return None, "The changelog is not shown because this build has only part of the git history."
        out = subprocess.run(
            ["git", "log", "--first-parent", "--format=%h%x09%ct%x09%s"],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError):
        return None, "The changelog is not shown because this build could not read the git history."
    commits = []
    for line in out.splitlines():
        short, stamp, subject = line.split("\t", 2)
        commits.append((short, int(stamp), subject))
    return commits, None


def on_files(files, config):
    """Adds the virtual `changelog.md` page, so no generated file lives in the repository."""
    # Imported here so the rendering function can be tested without MkDocs installed.
    from mkdocs.structure.files import File

    root = Path(config.config_file_path).parent
    commits, note = _git_commits(root)
    content = render_changelog(commits or [], config.get("repo_url"), note)
    files.append(File.generated(config, "changelog.md", content=content))
    return files
