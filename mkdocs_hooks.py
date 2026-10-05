"""MkDocs hooks: the repository's and the site's addresses, worked out at build time.

No file names the repository's or the site's address: they come from the build. Pages link files outside `docs/` as
`repo:<path>`, which this hook turns into a link to that file on the default branch of the repository the site is
built from.
"""

import os
import re
import subprocess

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
