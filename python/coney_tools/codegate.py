"""The public side of the code gate (clean-room page, section 7): every engine commit signed by an implementer, and
every reimplemented function tied to an address on a gated page.

`orig-check` reads the engine's sources in a checkout and every `@orig` tag in their comments:

    // @orig 0x00aa11bb open the store
    // @page research/pages/store/page.md

The address must be in the Entry points table of the linked page, that page must be merged and verified (read from
`main` with its verdict and two signatures), and the job must be the page's job for that address, so every word of
the tag is gated text. A function defined in a game-behaviour folder without a tag is flagged. `commit-sigs` checks
that every commit in a range that touches the engine is signed by a key that `allowed_signers` lists for git under an
implementer's principal.

Both read their settings (`[tool.coney-tools.code-gate]` in python/pyproject.toml) and allowed_signers from a git
ref, `main` unless told otherwise, never from the change being checked. Output names paths, lines, rules, addresses,
page ids and commit ids only: never a job, a comment or a line of code.
"""

import io
import json
import os
import re
import tarfile
import tomllib
from bisect import bisect_right
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools import research, signing
from coney_tools.cleanroom import page_check, scan

SETTINGS = "python/pyproject.toml"  # relative to the repository's root, read from the ref
SECTION = ("tool", "coney-tools", "code-gate")
TAG = re.compile(r"^@orig[ \t]+(0x[0-9a-f]{8})[ \t]+(\S.*?)\s*$")
TAG_LINE = re.compile(r"^@orig\b")
LINK = re.compile(r"^@page[ \t]+(\S+)\s*$")
MARK = re.compile(r"@(?:orig|page)\b")
MAX_SOURCE = 4 << 20  # bytes; a larger source file is not read, and fails


class CodeGateError(research.GateError):
    """The settings, the ref or a tool is missing or malformed (the command could not run: exit 2)."""


# --- settings -----------------------------------------------------------------------------------------------------


REQUIRED = ("engine_root", "source_extensions", "game_behaviour")
# The scripted pre-filter's keys (cleanroom prefilter), each with the value it takes when the table leaves it out.
PREFILTER_DEFAULTS = {
    "prefilter_trivial_constants": (0, 1, -1, 2),
    "prefilter_min_calls": 2,
    "prefilter_min_branches": 4,
}


@dataclass
class Settings:
    engine_root: str
    extensions: tuple
    behaviour: tuple  # folders, relative to the repository's root
    trivial: tuple = PREFILTER_DEFAULTS["prefilter_trivial_constants"]  # constants the pre-filter ignores
    min_calls: int = PREFILTER_DEFAULTS["prefilter_min_calls"]
    min_branches: int = PREFILTER_DEFAULTS["prefilter_min_branches"]


def rel_path(value, where):
    if not research.image_path_ok(value) or value in ("", "."):  # the same test as a page's image paths
        raise CodeGateError(f"{where} in {SETTINGS} is not a relative path inside the repository")
    return value


def settings(exe, repo, ref):
    """The code gate's settings as they stand at the ref."""
    if not ref or ref.startswith("-"):
        raise CodeGateError(f"not a git ref: {ref!r}")
    r = signing.git(exe, repo, "cat-file", "blob", f"{ref}:{SETTINGS}", ok=(0, 128))
    if r.returncode:
        raise CodeGateError(f"no {SETTINGS} at {ref} in {repo}")
    try:
        doc = tomllib.loads(signing.lf(r.stdout).decode("utf-8"))
    except (UnicodeDecodeError, tomllib.TOMLDecodeError):
        raise CodeGateError(f"{SETTINGS} at {ref} is not valid TOML") from None
    for key in SECTION:
        doc = doc.get(key) if isinstance(doc, dict) else None
    name = f"[{'.'.join(SECTION)}]"
    if not isinstance(doc, dict) or not set(REQUIRED) <= set(doc) or set(doc) - set(REQUIRED) - set(PREFILTER_DEFAULTS):
        raise CodeGateError(
            f"{SETTINGS} at {ref} has no {name} with {', '.join(REQUIRED)} (and optionally "
            f"{', '.join(PREFILTER_DEFAULTS)}) and nothing else"
        )
    root = rel_path(doc["engine_root"], "engine_root")
    exts = doc["source_extensions"]
    if not isinstance(exts, list) or not exts or not all(isinstance(e, str) and re.match(r"^\.\w+$", e) for e in exts):
        raise CodeGateError(f"source_extensions in {SETTINGS} is not a list of extensions such as .cpp")
    folders = doc["game_behaviour"]
    if not isinstance(folders, list):
        raise CodeGateError(f"game_behaviour in {SETTINGS} is not a list of folders")
    behaviour = []
    for f in folders:
        f = rel_path(f, "a game_behaviour folder")
        if not under(f, root):
            raise CodeGateError(f"a game_behaviour folder in {SETTINGS} is not under engine_root")
        behaviour.append(f)
    trivial = doc.get("prefilter_trivial_constants", PREFILTER_DEFAULTS["prefilter_trivial_constants"])
    if not isinstance(trivial, list | tuple) or not all(is_number(v) for v in trivial):
        raise CodeGateError(f"prefilter_trivial_constants in {SETTINGS} is not a list of numbers")
    counts = []
    for key in ("prefilter_min_calls", "prefilter_min_branches"):
        v = doc.get(key, PREFILTER_DEFAULTS[key])
        if not isinstance(v, int) or isinstance(v, bool) or v < 1:
            raise CodeGateError(f"{key} in {SETTINGS} is not a whole number of 1 or more")
        counts.append(v)
    return Settings(root, tuple(e.lower() for e in exts), tuple(behaviour), tuple(trivial), *counts)


def is_number(v):
    return isinstance(v, int | float) and not isinstance(v, bool)


def under(path, folder):
    return path == folder or path.startswith(folder + "/")


# --- the gated pages, as merged ------------------------------------------------------------------------------------


@dataclass
class Pages:
    entries: dict = field(default_factory=dict)  # page id -> {address: job}, for every verified page
    unverified: list = field(default_factory=list)  # ids of pages at the ref that do not verify

    def holding(self, address):
        return sorted(i for i, e in self.entries.items() if address in e)


def research_tree(exe, repo, ref, into):
    """Writes research/ as it stands at the ref into the folder (git archive: plumbing, never the working tree).
    False when the ref has no research folder."""
    top = "/".join(research.REPO_PAGES[:1])
    r = signing.git(exe, repo, "ls-tree", "-d", "--name-only", ref, "--", top)
    if not r.stdout.strip():
        return False
    r = signing.git(exe, repo, "archive", "--format=tar", ref, "--", top)
    with tarfile.open(fileobj=io.BytesIO(r.stdout)) as tar:
        for m in tar.getmembers():
            p = PurePosixPath(m.name)
            if not m.isfile():
                continue
            if p.is_absolute() or ".." in p.parts or not p.parts or p.parts[0] != top:
                raise CodeGateError(f"git archive gave a path outside {top}/")
            out = Path(into, *p.parts)
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(tar.extractfile(m).read())
    return True


def gated_pages(tools, repo, ref):
    """Every page under research/pages/ at the ref that verifies as cleanroom verify checks it (page-check, every
    hash, every entry keep, two page reviewers' signatures; markdownlint aside), with its Entry points."""
    pages = Pages()
    tree = tools.tmp / "tree"
    if not research_tree(tools.git, repo, ref, tree):
        return pages
    folder = Path(tree, *research.REPO_PAGES)
    ids = sorted(p.name for p in folder.iterdir() if p.is_dir()) if folder.is_dir() else []
    for page_id in ids:
        if not research.PAGE_ID.match(page_id) or not (folder / page_id / research.PAGE_FILE).is_file():
            continue
        b = research.verify_bundle(research.repo_layout(tree, page_id), tools, lint=False)
        if b.ok:
            pages.entries[page_id] = page_check.entry_points(b.page.sections)
        else:
            pages.unverified.append(page_id)
    return pages


def page_link(page_id):
    return "/".join((*research.REPO_PAGES, page_id, research.PAGE_FILE))


LINK_PATH = re.compile(  # research/pages/<page id>/page.md
    "^"
    + re.escape("/".join(research.REPO_PAGES))
    + "/("
    + research.PAGE_ID.pattern.removeprefix("^").removesuffix("$")
    + ")/"
    + re.escape(research.PAGE_FILE)
    + "$"
)


# --- reading C and C++ ---------------------------------------------------------------------------------------------


def lex(text):
    """(code, comments). code is the text with every comment, the inside of every string and character literal, and
    every preprocessor line turned into spaces (line breaks kept, so offsets and line numbers stay); comments is
    [(offset, text)] for every comment outside a preprocessor line."""
    out = list(text)
    comments = []
    n, i, line_start = len(text), 0, True

    def blank(a, b):
        for k in range(a, b):
            if out[k] != "\n":
                out[k] = " "

    while i < n:
        c = text[i]
        if c == "\n":
            line_start = True
            i += 1
            continue
        if c in " \t\r\f\v":
            i += 1
            continue
        if c == "#" and line_start:  # a preprocessor line, with its continuations
            j = i
            while j < n and not (text[j] == "\n" and text[j - 1] != "\\"):
                if text.startswith("/*", j):
                    end = text.find("*/", j + 2)
                    j = n if end < 0 else end + 2
                    continue
                j += 1
            blank(i, j)
            i = j
            continue
        line_start = False
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((i, text[i:j]))
            blank(i, j)
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((i, text[i:j]))
            blank(i, j)
            i = j
        elif c == '"':
            word = re.search(r"[A-Za-z0-9_]*$", text[max(0, i - 3) : i]).group(0)
            if word in ("R", "u8R", "uR", "UR", "LR"):  # a raw string: R"delim( ... )delim"
                m = re.match(r'"([^()\\\s]{0,16})\(', text[i:])
                end = text.find(")" + m.group(1) + '"', i) if m else -1
                j = n if end < 0 else end + len(m.group(1)) + 2
            else:
                j = i + 1
                while j < n and text[j] != '"' and text[j] != "\n":
                    j += 2 if text[j] == "\\" else 1
                j = min(j + 1, n)
            blank(i + 1, j - 1)
            i = j
        elif c == "'":
            word = re.search(r"[A-Za-z0-9_.']*$", text[:i]).group(0)
            if word[:1].isdigit():  # a digit separator: 1'000
                i += 1
                continue
            j = i + 1
            while j < n and text[j] != "'" and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            blank(i + 1, j - 1)
            i = j
        else:
            i += 1
    return "".join(out), comments


CONTAINER = re.compile(r"^(?:typedef\s+)?(?:inline\s+)?(?:export\s+)?(?:namespace|class|struct|union|enum)\b")
EXTERN = re.compile(r'^extern\s*"\s*"$')
NOT_A_NAME = {
    "if", "for", "while", "switch", "catch", "return", "sizeof", "alignof", "decltype", "alignas", "noexcept",
    "requires", "throw", "static_assert", "__attribute__", "__declspec", "typeid", "co_return", "co_await",
}  # fmt: skip
NAME = re.compile(r"(operator\s*(?:\(\s*\)|[^\s\w(]{1,3}|\w+)|~?[A-Za-z_]\w*(?:\s*::\s*~?[A-Za-z_]\w*)*)\s*$")
CTOR_INIT = re.compile(r"^\s*(?:(?:const|volatile|noexcept|override|final|mutable)\b|&&?|\s)*:(?!:)")
NOT_ASSIGNMENT = re.compile(r"operator\s*\S+|[=!<>]=")


def group_end(code, i, open_, close):
    """The offset just after the bracket that closes the one at i (len(code) when it never closes)."""
    depth = 0
    for j in range(i, len(code)):
        if code[j] == open_:
            depth += 1
        elif code[j] == close:
            depth -= 1
            if depth == 0:
                return j + 1
    return len(code)


def blank_groups(head, word, open_="(", close=")"):
    """The head with every `word(...)` (balanced) turned into spaces, so offsets stay."""
    while (m := re.search(r"\b" + re.escape(word) + r"\s*" + re.escape(open_), head)) is not None:
        end = group_end(head, m.end() - 1, open_, close)
        head = head[: m.start()] + " " * (end - m.start()) + head[end:]
    return head


def spaces(m):
    return " " * len(m.group(0))


def plain_head(head):
    """A block's head with what never names a function turned into spaces (offsets stay): access labels, attributes
    and template parameter lists."""
    head = re.sub(r"\b(?:public|private|protected)\s*:(?!:)", spaces, head)
    head = re.sub(r"\[\[.*?\]\]", spaces, head, flags=re.DOTALL)
    for word in ("alignas", "__attribute__", "__declspec"):
        head = blank_groups(head, word)
    return blank_groups(head, "template", "<", ">")


def function_name(head):
    """The offset of the parameter list when the head of a block is a function's, else None."""
    depth = 0
    for k, ch in enumerate(head):
        if ch == "(" and depth == 0:
            m = NAME.search(head[:k])
            if m and m.group(1).split("::")[-1].strip() not in NOT_A_NAME:
                if "=" in NOT_ASSIGNMENT.sub("", head[:k]):
                    return None  # an initializer: x = [](...) {
                return k
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
    return None


@dataclass
class Function:
    name_line: int
    head_start: int  # the offset after the declaration or block before it
    body: int  # the offset of its opening brace


def functions(code):
    """Every function definition at namespace, class or file scope (bodies are skipped whole, so lambdas and local
    classes inside them are not listed)."""
    found = []
    n, i, head_start, depth, paren = len(code), 0, 0, 0, 0  # depth: open namespaces, classes, extern "C" blocks
    while i < n:
        c = code[i]
        if c in "([":
            paren += 1
        elif c in ")]":
            paren = max(0, paren - 1)
        elif c == ";" and not paren:
            head_start = i + 1
        elif c == "}" and not paren:
            depth = max(0, depth - 1)
            head_start = i + 1
        elif c == "{" and not paren:
            head = plain_head(code[head_start:i])
            k = function_name(head)
            if k is None and (CONTAINER.match(" ".join(head.split())) or EXTERN.match(" ".join(head.split()))):
                depth += 1
                head_start = i + 1
                i += 1
                continue
            if k is not None:
                prev = code[:i].rstrip()[-1:]
                if CTOR_INIT.match(head[group_end(head, k, "(", ")") :]) and (prev.isalnum() or prev in "_>"):
                    i = group_end(code, i, "{", "}")  # a member initialized with braces, x{0}: the head goes on
                    continue
                found.append(Function(code.count("\n", 0, head_start + k) + 1, head_start, i))
            i = group_end(code, i, "{", "}")  # a function's body, or a data initializer: skipped whole
            if k is not None:
                head_start = i  # after a data initializer, the declaration goes on to its semicolon
            continue
        i += 1
    return found


# --- orig-check ---------------------------------------------------------------------------------------------------


@dataclass
class Finding:
    path: str
    line: int
    rule: str
    message: str


RULES = [
    ("orig-malformed", "a tag that is not '@orig 0x<8 lower-case hex digits> <job>', or an '@page' line without one"),
    ("orig-no-page", "an '@orig' tag without '@page research/pages/<id>/page.md' on the next line"),
    ("orig-page", "a link to a page that is not merged and verified"),
    ("orig-address", "an address that is not in the linked page's Entry points table"),
    ("orig-job", "a job that is not the page's job for that address"),
    ("orig-placement", "a tag that is not just above a function's definition"),
    ("orig-untagged", "a function in a game-behaviour folder without an '@orig' tag"),
    ("orig-not-text", "a source file that is not UTF-8 text, or larger than 4 MB"),
]


@dataclass
class Tag:
    line: int
    offset: int
    address: str  # None for a malformed tag: it is placed, but checked no further
    job: str = None
    link: str = None


def comment_lines(text, comments):
    """[(line, offset, content)] for every line of every comment, with the comment markers taken off."""
    starts = [0] + [m.end() for m in re.finditer("\n", text)]
    out = []
    for offset, body in comments:
        pos = offset
        for part in body.split("\n"):
            content = re.sub(r"^\s*(?://+!?|/\*+!?|\*+(?!/))?", "", part)
            content = re.sub(r"\*+/\s*$", "", content).strip()
            out.append((bisect_right(starts, pos), pos, content))
            pos += len(part) + 1
    return out


def read_tags(path, text, comments, findings):
    """Every tag of a file (a comment line starting with @orig) with the @page line after it; every malformed tag,
    stray @page line and @orig or @page elsewhere in a comment is a finding."""
    lines = comment_lines(text, comments)
    tags, used = [], set()
    for k, (line, offset, content) in enumerate(lines):
        if k in used or not MARK.search(content):
            continue
        if not TAG_LINE.match(content):
            message = "an '@page' line that follows no '@orig' tag" if LINK.match(content) else "a mark inside text"
            findings.append(Finding(path, line, "orig-malformed", message))
            continue
        nxt = lines[k + 1] if k + 1 < len(lines) else None
        link = LINK.match(nxt[2]) if nxt and nxt[0] == line + 1 else None
        if link:
            used.add(k + 1)
        tag = TAG.match(content)
        if not tag:
            findings.append(Finding(path, line, "orig-malformed", "not '@orig 0x<8 lower-case hex digits> <job>'"))
            tags.append(Tag(line, offset, None))
            continue
        t = Tag(line, offset, tag.group(1), page_check.normal_job(tag.group(2)), link.group(1) if link else None)
        if not link:
            findings.append(Finding(path, line, "orig-no-page", f"{t.address} has no '@page' line after it"))
        tags.append(t)
    return tags


def check_tag(path, t, pages, findings):
    if t.address is None or t.link is None:
        return
    m = LINK_PATH.match(t.link)
    if not m:
        findings.append(Finding(path, t.line + 1, "orig-page", f"the link is not {page_link('<id>')}"))
        return
    page_id = m.group(1)
    if page_id not in pages.entries:
        state = "does not verify" if page_id in pages.unverified else "is not merged"
        findings.append(Finding(path, t.line + 1, "orig-page", f"page {page_id} {state}"))
        return
    entries = pages.entries[page_id]
    if t.address not in entries:
        elsewhere = pages.holding(t.address)
        where = f"it is on {', '.join(elsewhere)}" if elsewhere else "it is on no gated page"
        findings.append(
            Finding(path, t.line, "orig-address", f"{t.address} is not in page {page_id}'s Entry points ({where})")
        )
    elif entries[t.address] != t.job:
        findings.append(Finding(path, t.line, "orig-job", f"the job differs from page {page_id}'s job for {t.address}"))


def owner_of(fns, tag):
    """The function a tag is attached to (the tag sits in the head before its body), or None."""
    return next((f for f in fns if f.head_start <= tag.offset < f.body), None)


def name_of(code, f):
    """A listed function's name as its definition writes it, spaces taken out (Store::open, ~Store, operator==)."""
    head = plain_head(code[f.head_start : f.body])
    k = function_name(head)
    m = NAME.search(head[:k]) if k is not None else None
    return re.sub(r"\s+", "", m.group(1)) if m else ""


def check_source(path, data, behaviour, pages):
    """(findings, tags) for one source file's bytes."""
    findings = []
    if len(data) > MAX_SOURCE:
        return [Finding(path, 1, "orig-not-text", "larger than 4 MB")], 0
    try:
        text = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return [Finding(path, 1, "orig-not-text", "not UTF-8 text")], 0
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    code, comments = lex(text)
    tags = read_tags(path, text, comments, findings)
    fns = functions(code)
    tagged = set()
    for t in tags:
        owner = owner_of(fns, t)
        if owner is None:
            where = f"{t.address or 'the tag'} is not just above a function"
            findings.append(Finding(path, t.line, "orig-placement", where))
        else:
            tagged.add(id(owner))
        check_tag(path, t, pages, findings)
    if behaviour:
        for f in fns:
            if id(f) not in tagged:
                findings.append(Finding(path, f.name_line, "orig-untagged", "a function without an '@orig' tag"))
    return sorted(findings, key=lambda f: (f.line, f.rule)), len(tags)


def source_files(repo, s):
    """[(repo-relative POSIX path, absolute path)] of every source file under the engine root, sorted."""
    root = Path(repo, *s.engine_root.split("/"))
    out = []
    if not root.is_dir():
        return out
    for folder, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d != ".git"]
        for name in files:
            if Path(name).suffix.lower() in s.extensions:
                p = Path(folder, name)
                out.append((p.relative_to(repo).as_posix(), p))
    return sorted(out)


def orig_check(repo=None, ref=research.MAIN, ssh_keygen=None, as_json=False):
    """(code, lines): every @orig tag in the engine's sources against the gated pages at the ref."""
    repo = Path(repo) if repo else research.own_checkout()
    findings, ntags, nfiles = [], 0, 0
    with research.Tools(ssh_keygen, ref=ref) as tools:
        s = settings(tools.git, repo, ref)
        tools.load_allowed(repo)
        pages = gated_pages(tools, repo, ref)
    for rel, path in source_files(repo, s):
        try:
            data = path.read_bytes()
        except OSError as e:
            raise CodeGateError(f"cannot read {rel}: {e.__class__.__name__}") from None
        found, n = check_source(rel, data, any(under(rel, b) for b in s.behaviour), pages)
        findings += found
        ntags += n
        nfiles += 1
    failed = bool(findings)
    if as_json:
        doc = {
            "ok": not failed,
            "ref": ref,
            "engine_root": s.engine_root,
            "files": nfiles,
            "tags": ntags,
            "pages": sorted(pages.entries),
            "unverified_pages": pages.unverified,
            "findings": [f.__dict__ for f in findings],
        }
        return (1 if failed else 0), [json.dumps(doc, indent=2)]
    lines = [f"{f.path}:{f.line}: {f.rule}: {f.message}" for f in findings]
    lines += [f"note: page {i} at {ref} does not verify; its addresses do not count" for i in pages.unverified]
    lines.append(
        f"{'FAIL' if failed else 'PASS'}: {len(findings)} findings, {ntags} tags in {nfiles} source files under "
        f"{s.engine_root}/; {len(pages.entries)} gated pages at {ref}"
    )
    return (1 if failed else 0), lines


# --- commit-sigs --------------------------------------------------------------------------------------------------


def split_signed(raw):
    """(payload, signature or None) of a raw commit object: the signature is its gpgsig header, and the payload the
    object without it, as git verifies it."""
    head, sep, body = raw.partition(b"\n\n")
    payload, sig, inside = [], None, False
    for line in head.split(b"\n"):
        if line.startswith(b"gpgsig "):
            sig, inside = [line[len(b"gpgsig ") :]], True
        elif inside and line.startswith(b" "):
            sig.append(line[1:])
        else:
            inside = False
            payload.append(line)
    data = b"\n".join(payload) + sep + body
    return data, (b"\n".join(sig) + b"\n" if sig is not None else None)


# The oldest git with `merge-tree --write-tree`, which tells a merge's own engine changes from its parents'.
MERGE_TREE_GIT = (2, 38)


def entry(exe, repo, tree, root):
    """The object id at the root in the tree or commit, or None when it has none."""
    r = signing.git(exe, repo, "rev-parse", "--verify", "-q", f"{tree}:{root}", ok=(0, 1))
    return r.stdout.strip() if r.returncode == 0 else None


def touches(exe, repo, commit, root):
    """True when the commit changes the engine root. A merge of two parents counts only when its engine root differs
    from git's own clean merge of them, so a merge of main that brings in nothing of its own is not checked; a
    conflicted merge or one of more parents always counts."""
    parents = signing.git(exe, repo, "rev-parse", f"{commit}^@").stdout.split()
    if len(parents) == 2:
        merged = signing.git(exe, repo, "merge-tree", "--write-tree", *(p.decode() for p in parents), ok=(0, 1))
        if merged.returncode == 0:
            tree = merged.stdout.split()[0].decode()
            return entry(exe, repo, tree, root) != entry(exe, repo, commit, root)
        return True
    if len(parents) > 2:
        return True
    r = signing.git(exe, repo, "diff-tree", "-r", "--root", "--name-only", "--no-commit-id", commit, "--", root)
    return bool(r.stdout.strip())


# The trailer naming the model that wrote an engine commit (AGENTS.md: one Co-Authored-By trailer, the model's name
# and the vendor's noreply address).
MODEL_TRAILER = re.compile(
    r"^Co-Authored-By: (Claude [A-Z][a-z]+ [0-9]+(?:\.[0-9]+)*) <noreply@anthropic\.com>$", re.MULTILINE
)
ANY_COAUTHOR = re.compile(r"^Co-Authored-By:", re.MULTILINE | re.IGNORECASE)


def commit_model(data):
    """(model, None) when the commit's message names the model that wrote it in exactly one Co-Authored-By trailer,
    else (None, why)."""
    message = data.split(b"\n\n", 1)[1].decode("utf-8", "replace") if b"\n\n" in data else ""
    found = MODEL_TRAILER.findall(message)
    if len(ANY_COAUTHOR.findall(message)) != 1 or len(found) != 1:
        return None, "its message does not name its model in one 'Co-Authored-By: Claude <model> <noreply@...>' trailer"
    return found[0], None


def commit_problem(tools, repo, commit, keys):
    """(principal, model, None) when the commit is signed by an implementer's commit key and names the model that
    wrote it, else (None, None, why)."""
    raw = signing.git(tools.git, repo, "cat-file", "commit", commit).stdout
    data, sig = split_signed(raw)
    if sig is None:
        return None, None, "not signed"
    try:
        fingerprint = signing.key_fingerprint(sig)
    except signing.BadSignature as e:
        return None, None, str(e)
    principals = sorted(
        p
        for names, spaces, fp in keys
        if fp == fingerprint and spaces is not None and signing.COMMIT_NAMESPACE in spaces
        for p in names
        if p.startswith(signing.IMPLEMENTER) and signing.PRINCIPAL.match(p)
    )
    if not principals:
        return None, None, f"its key is not an implementer's commit key in {signing.ALLOWED_SIGNERS}"
    try:
        signing.verify(tools.ssh_keygen, tools.allowed, data, sig, principals[0], tools.tmp, signing.COMMIT_NAMESPACE)
    except signing.BadSignature as e:
        return None, None, str(e)
    model, why = commit_model(data)
    return (principals[0], model, None) if why is None else (None, None, why)


def commit_sigs(rng, repo=None, ref=research.MAIN, ssh_keygen=None, as_json=False):
    """(code, lines): every commit in the range that touches the engine root, signed by an implementer and naming the
    model that wrote it."""
    if not rng or rng.startswith("-"):
        raise CodeGateError(f"not a revision range: {rng!r}")
    repo = Path(repo) if repo else research.own_checkout()
    results, skipped = [], 0
    with research.Tools(ssh_keygen, ref=ref) as tools:
        s = settings(tools.git, repo, ref)
        tools.load_allowed(repo)
        keys = signing.listed_keys(tools.allowed.read_bytes())
        if scan.git_version(tools.git) < MERGE_TREE_GIT:
            raise CodeGateError(f"git older than {MERGE_TREE_GIT[0]}.{MERGE_TREE_GIT[1]} (no merge-tree --write-tree)")
        commits = signing.git(tools.git, repo, "rev-list", "--reverse", rng, "--").stdout.decode().split()
        for c in commits:
            if not touches(tools.git, repo, c, s.engine_root):
                skipped += 1
                continue
            principal, model, why = commit_problem(tools, repo, c, keys)
            results.append({"commit": c, "ok": why is None, "principal": principal, "model": model, "problem": why})
    failed = any(not r["ok"] for r in results)
    if as_json:
        doc = {"ok": not failed, "range": rng, "ref": ref, "skipped": skipped, "commits": results}
        return (1 if failed else 0), [json.dumps(doc, indent=2)]
    lines = [
        f"PASS {r['commit'][:12]} {r['principal']}, {r['model']}"
        if r["ok"]
        else f"FAIL {r['commit'][:12]}: {r['problem']}"
        for r in results
    ]
    lines.append(
        f"{'FAIL' if failed else 'PASS'}: {len(results)} commits touch {s.engine_root}/, "
        f"{sum(not r['ok'] for r in results)} not signed by an implementer or naming no model; "
        f"{skipped} others not checked"
    )
    return (1 if failed else 0), lines
