"""MkDocs hooks: the site's research section, read straight from research/pages (one source, no copies).

Links are written for the repository (they work on GitHub and in an editor); the hooks point each one that leaves
docs/ at the right place on the site: a research page at its site copy, any other file at the repository.
"""

import logging
import os
import re
from pathlib import Path

from mkdocs.structure.files import File

log = logging.getLogger("mkdocs.plugins.coney")

ROOT = Path(__file__).resolve().parent
PAGES = ROOT / "research" / "pages"
LINK = re.compile(r"(\]\()([^)\s#]+)(#[^)\s]*)?(\))")

# Every diagram looks the same: a top-to-bottom flowchart drawn by docs/assets/diagrams.js, which alone sets the look.
# A diagram that names another kind or direction, or styles itself, is refused (a warning, so the strict build fails).
DIAGRAM = re.compile(r"^```mermaid\n(.*?)^```", re.MULTILINE | re.DOTALL)
DIAGRAM_HEAD = "flowchart TB"
DIAGRAM_STYLE = re.compile(r"^\s*(style|classDef|class|linkStyle)\s|%%\{|^---$", re.MULTILINE)
# Boxes, cylinders for stores, and zones as subgraphs; a decision is a box worded as a question, never a diamond.
DIAGRAM_DIAMOND = re.compile(r"\w\{")


def _title(page: Path) -> str:
    for line in page.read_text(encoding="utf-8").splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return page.parent.name


def on_files(files, config):
    pages = sorted(p for p in PAGES.glob("*/page.md") if p.is_file())
    for page in pages:
        files.append(File.generated(config, f"research/{page.parent.name}.md", abs_src_path=str(page)))
    rows = "\n".join(f"| [{_title(p)}]({p.parent.name}.md) | `{p.parent.name}` |" for p in pages)
    index = (
        "# Research\n\nGated research pages: facts and observed behaviour, each signed by two page reviewers.\n\n"
        f"| Page | Id |\n| --- | --- |\n{rows}\n"
    )
    files.append(File.generated(config, "research/index.md", content=index))
    return files


def _site_path(target: Path, docs: Path) -> str | None:
    """Where a repository file lives on the site, as a docs-relative path; None when it is not on the site."""
    if target.name == "page.md" and target.parent.parent == PAGES:
        return f"research/{target.parent.name}.md"
    if target.is_relative_to(docs):
        return target.relative_to(docs).as_posix()
    return None


def _check_diagrams(markdown: str, where: str) -> None:
    for n, m in enumerate(DIAGRAM.finditer(markdown), 1):
        body = m.group(1)
        head = body.strip().splitlines()[0].strip() if body.strip() else ""
        if head != DIAGRAM_HEAD:
            log.warning("%s, diagram %d: starts with %r; every diagram starts with %r", where, n, head, DIAGRAM_HEAD)
        if DIAGRAM_STYLE.search(body):
            log.warning("%s, diagram %d: sets its own style; the look comes only from docs/assets/diagrams.js", where, n)
        if DIAGRAM_DIAMOND.search(body):
            log.warning("%s, diagram %d: has a diamond; word a decision as a question in a box", where, n)


def on_page_markdown(markdown, page, config, files):
    _check_diagrams(markdown, page.file.src_uri)
    if not page.file.abs_src_path:
        return markdown
    docs = Path(config["docs_dir"]).resolve()
    here = Path(page.file.abs_src_path).resolve().parent
    site_dir = (docs / page.file.src_uri).parent
    repo = (config.get("repo_url") or "").rstrip("/")

    def fix(m):
        url = m.group(2)
        if "://" in url or url.startswith(("/", "mailto:")):
            return m.group(0)
        target = (here / url).resolve()
        if not target.is_relative_to(ROOT):
            return m.group(0)
        on_site = _site_path(target, docs)
        if on_site is not None:
            new = Path(os.path.relpath(docs / on_site, site_dir)).as_posix()
        elif repo:
            kind = "tree" if target.is_dir() else "blob"
            new = f"{repo}/{kind}/main/{target.relative_to(ROOT).as_posix()}"
        else:
            return m.group(0)
        return f"{m.group(1)}{new}{m.group(3) or ''}{m.group(4)}"

    return LINK.sub(fix, markdown)
