"""MkDocs hooks: the repository's and the site's addresses, worked out at build time.

No file names the repository's or the site's address: they come from the build. Pages link files outside `docs/` as
`repo:<path>`, which this hook turns into a link to that file on the default branch of the repository the site is
built from. It also adds the Changelog page, written from the git history at build time, fills the progress page's
tables and pictures, writes the README's badge data (shields.io endpoint JSON) and the progress numbers into the
site, writes each research system's function list page from research/functions/ and fills the source map's file
tables from research/source-files.toml, so none of those is committed.
"""

import os
import re
import subprocess
import sys
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


# The progress tracker's state for this build, read in on_files and used by on_page_markdown, and the pictures and
# numbers made from it (coney_tools.progress_site).
_progress_state = None
_progress_site = None

# The committed progress pages, whose markers the build fills.
_PROGRESS_PAGE = "progress/index.md"
_HISTORY_PAGE = "progress/history.md"
# The committed source map, whose marker the build fills with the file tables of research/source-files.toml.
_SOURCE_MAP_PAGE = "research/source-map.md"


def _use_checkout_tools(root):
    """Makes `coney_tools` importable from the checkout at `root`: the docs build installs only MkDocs."""
    src = str(Path(root) / "python" / "src")
    if src not in sys.path:
        sys.path.insert(0, src)


def load_progress(root):
    """The progress tracker's state for the checkout at `root`, from `coney_tools` in `python/src`.

    The docs build installs only MkDocs, so the tracker is imported from the checkout instead of from an installed
    package. Raises RuntimeError, naming the problems, when the tracker's inputs disagree with each other.
    """
    _use_checkout_tools(root)
    from coney_tools import progress
    from coney_tools.config import ConfigError

    try:
        state = progress.load(Path(root))
    except ConfigError as error:
        raise RuntimeError(str(error)) from error
    if state.problems:
        raise RuntimeError("the progress inputs disagree:\n  " + "\n  ".join(state.problems))
    return state


def fill_progress_page(markdown, state, site=None):
    """The progress page's Markdown with its marker replaced by the generated tables of `state` (and `site`'s pictures)."""
    from coney_tools import progress_render

    grid_html = site.grid_html if site else ""
    missions_html = site.missions_html if site else ""
    return progress_render.fill_page(
        markdown, progress_render.render_page(state, grid_html, missions_html), _PROGRESS_PAGE
    )


def fill_history_page(markdown, site):
    """The history page's Markdown with its marker replaced by the chart and the table of days."""
    from coney_tools import progress_render

    return progress_render.fill_history_page(markdown, site.history_html, _HISTORY_PAGE)


def build_progress_site(root, state, repo_url=None):
    """The progress pictures and numbers for this build (coney_tools.progress_site.build_site).

    The history is rebuilt from git only when `CONEY_PROGRESS_HISTORY=full` (the deploy sets it; each missing day costs
    one extraction of the tracker's inputs, a few seconds); otherwise it is the cache plus today, which keeps pull
    request and local builds quick. `CONEY_PROGRESS_HISTORY_CACHE` names a copy of the previously published
    history.json, whose days a deploy reuses, so it extracts one commit rather than all of them.
    """
    from coney_tools import progress_history, progress_site

    cache = None
    cache_path = os.environ.get("CONEY_PROGRESS_HISTORY_CACHE")
    if cache_path:
        try:
            cache = Path(cache_path).read_text(encoding="utf-8")
        except OSError:
            cache = None  # nothing published yet: the history is rebuilt in full
    head = None
    try:
        out = subprocess.run(
            ["git", "-C", str(root), "log", "-1", "--format=%h%x09%ct"], capture_output=True, text=True, check=True
        ).stdout.strip()
        short, stamp = out.split("\t")
        head = (progress_history.utc_day(int(stamp)), short)
    except (OSError, subprocess.CalledProcessError, ValueError):
        head = None
    return progress_site.build_site(
        Path(root),
        state,
        repo_url=repo_url,
        head=head,
        history_cache=cache,
        reconstruct_history=os.environ.get("CONEY_PROGRESS_HISTORY") == "full",
    )


def research_generated_pages(root):
    """The pages made from the research data, as {path in the site: Markdown}.

    The function list of each research system is made from research/functions/*.toml (coney_tools.research_data) and
    never committed. Raises RuntimeError, naming every problem, when the research data does not pass
    `coney-tools research render --check`.
    """
    _use_checkout_tools(root)
    from coney_tools import research_data

    findings = research_data.check_root(Path(root))
    if findings.errors:
        raise RuntimeError("the research data has problems:\n  " + "\n  ".join(findings.errors))
    return research_data.generated_pages(Path(root))


def fill_source_map_page(markdown, root):
    """The source map page's Markdown with its marker replaced by the file tables of research/source-files.toml.

    Raises RuntimeError, naming the problem, when the data or the marker is wrong.
    """
    _use_checkout_tools(root)
    from coney_tools import research_data
    from coney_tools.config import ConfigError

    try:
        return research_data.fill_source_map(Path(root), markdown)
    except ConfigError as error:
        raise RuntimeError(str(error)) from error


def progress_badge_files(state):
    """The README's badges as {path in the site: JSON text}, one shields.io endpoint file each."""
    from coney_tools import progress_render

    return {f"{progress_render.BADGES_DIR}/{name}": text for name, text in progress_render.badge_files(state).items()}


def on_page_markdown(markdown, config, page=None, **_):
    """Rewrites `repo:<path>` links to the file on the default branch, before MkDocs validates links.

    On the progress page it first replaces the marker with the generated tables and pictures, on the history page with
    the chart and the table of days, on the source map with the file tables.
    """
    if page is not None and page.file.src_uri == _PROGRESS_PAGE and _progress_state is not None:
        markdown = fill_progress_page(markdown, _progress_state, _progress_site)
    if page is not None and page.file.src_uri == _HISTORY_PAGE and _progress_site is not None:
        markdown = fill_history_page(markdown, _progress_site)
    if page is not None and page.file.src_uri == _SOURCE_MAP_PAGE:
        markdown = fill_source_map_page(markdown, Path(config.config_file_path).parent)
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


# Files that live outside `docs/` and are served at the same path in the site: the home page's artwork, kept in
# `assets/art/` so the launcher can use the one copy too.
_SERVED_ASSETS = ("assets/art/hero.webp", "assets/art/hero.jpg")


def on_files(files, config):
    """Adds the virtual `changelog.md` page, the progress badge files and the shared artwork.

    None of them is a committed file under `docs/`: the first two are generated, the artwork is served from `assets/`.
    """
    # Imported here so the rendering function can be tested without MkDocs installed.
    from mkdocs.exceptions import PluginError
    from mkdocs.structure.files import File

    global _progress_state, _progress_site
    root = Path(config.config_file_path).parent
    try:
        _progress_state = load_progress(root)
        _progress_site = build_progress_site(root, _progress_state, config.get("repo_url"))
    except (RuntimeError, ImportError, OSError) as error:
        raise PluginError(f"progress: {error}") from error
    for uri, text in progress_badge_files(_progress_state).items():
        files.append(File.generated(config, uri, content=text))
    for uri, text in _progress_site.files.items():
        files.append(File.generated(config, uri, content=text))
    try:
        research_pages = research_generated_pages(root)
    except (RuntimeError, ImportError, OSError) as error:
        raise PluginError(f"research data: {error}") from error
    for uri, text in research_pages.items():
        files.append(File.generated(config, uri, content=text))
    for uri in _SERVED_ASSETS:
        if not (root / uri).is_file():
            raise PluginError(f"the home page's artwork is missing: {uri} (docs/guides/writing-docs.md, The home page)")
        files.append(File(uri, str(root), config["site_dir"], config["use_directory_urls"]))
    commits, note = _git_commits(root)
    content = render_changelog(commits or [], config.get("repo_url"), note)
    files.append(File.generated(config, "changelog.md", content=content))
    return files
