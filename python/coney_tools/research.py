"""The page gate (clean-room page, section 6): verdict files, their two signatures, and the way a page crosses from
the analysis zone to the public repository.

A page lives in a folder of its own, `page.md` with its images and their capture records beside it. The first page
reviewer writes `verdict.json` there, a verdict per section and image tied to their hashes, and signs it; a second
page reviewer, after checking that every hash still matches, signs the same file. `publish` copies a verified page
into the analysis side's outbox, `import` commits it on the public side to a branch of its own, and `cleanroom
verify` checks every page in a checkout. All of them verify the same way (verify_bundle), and every step lints the
page with the repository's markdownlint (lint.py): a page is linted before its first signature, never reformatted
after it.

A verdict file is published, so it holds no free text: only ids, hashes and enums. Results and messages name ids,
paths and hashes, never page text: a section is named by its number on the page.
"""

import hashlib
import json
import os
import re
import secrets
import shutil
import tempfile
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools import lint, signing
from coney_tools.cleanroom import page_check

SCHEMA = "coney-verdict/1"
PAGE_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
PAGE_FILE = "page.md"
VERDICT_FILE = "verdict.json"
SIG_NAME = re.compile(r"^verdict\.json\.(.+)\.sig$")  # verdict.json.<principal>.sig in a page or bundle folder
CAPTURE = ".capture.toml"
VERDICTS = ("keep", "reject")
KINDS = ("fact", "behaviour", "reference")
REVIEWER = "page-reviewer-"  # only these principals count toward a page
SIGNATURES = 2  # a page carries exactly two, from two reviewers with two keys
REPO_PAGES = ("research", "pages")  # research/pages/<id>/page.md and what it references
REPO_VERDICTS = ("research", "verdicts")  # research/verdicts/<id>.json and <id>.<principal>.sig
MAIN = "main"
HEX64 = re.compile(r"^[0-9a-f]{64}$")


class GateError(Exception):
    """The command could not run, or refused to (exit 2): bad input, a missing tool, or a file that already
    exists where the gate only adds."""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical(doc):
    """A verdict's bytes: sorted keys, two-space indent, UTF-8, one final LF. The same document gives the same
    bytes on every machine, so the signatures over it can be checked anywhere."""
    return (json.dumps(doc, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode("utf-8")


def own_checkout():
    """The git checkout coney-tools runs from."""
    root = lint.checkout_root()
    if root is None:
        raise GateError("coney-tools is not running from a git checkout: give --repo")
    return root


# --- a page and its hashes ----------------------------------------------------------------------------------------


@dataclass
class Page:
    id: str
    dir: Path
    data: bytes  # the page file's bytes, as read once: checked, hashed and copied from this
    sections: list
    images: list = field(default_factory=list)  # {"path", "sha256", "capture_sha256"}
    files: dict = field(default_factory=dict)  # bundle path -> bytes, for every image and capture record

    @property
    def ok(self):
        return all(s.ok for s in self.sections)

    @property
    def sha256(self):
        return sha256(signing.lf(self.data))

    def failing(self):
        return f"page-check fails in {sum(not s.ok for s in self.sections)} of {len(self.sections)} sections"


def read_page(page_id, page_dir, page_file):
    """Reads and checks a page; its images are hashed only when every section passes."""
    page_dir = Path(page_dir).resolve()
    try:
        if os.stat(page_file).st_size > page_check.MAX_PAGE:  # refused unread, as page-check does
            return Page(page_id, page_dir, b"", page_check.check_file(page_file))
        data = Path(page_file).read_bytes()
    except FileNotFoundError:
        raise GateError(f"no {PAGE_FILE} in {page_dir}") from None
    except OSError as e:
        raise GateError(f"cannot read {page_file}: {e.__class__.__name__}") from None
    page = Page(page_id, page_dir, data, page_check.check_bytes(data, page_dir))
    if not page.ok:
        return page
    for rel in page_check.page_images(page.sections, page_dir):
        try:
            image = (page_dir / rel).read_bytes()
            record = (page_dir / (rel + CAPTURE)).read_bytes()
        except OSError as e:
            raise GateError(f"cannot read image {rel}: {e.__class__.__name__}") from None
        page.images.append({"path": rel, "sha256": sha256(image), "capture_sha256": sha256(signing.lf(record))})
        page.files[rel] = image
        page.files[rel + CAPTURE] = record
    return page


def skeleton(page):
    """What a verdict on this page must record, decisions aside."""
    return {
        "schema": SCHEMA,
        "page": page.id,
        "page_sha256": page.sha256,
        "sections": [{"id": s.id, "sha256": s.sha256()} for s in page.sections],
        "images": [dict(i) for i in page.images],
    }


def schema_problems(doc):
    """What makes a parsed verdict file not a coney-verdict/1 document; nothing in it but ids, hashes and enums."""
    if not isinstance(doc, dict):
        return ["the verdict is not a JSON object"]
    want = {"schema", "page", "page_sha256", "checker_sha256", "sections", "images"}
    if set(doc) != want:
        return [f"the verdict's fields are not {', '.join(sorted(want))}"]
    out = []
    if doc["schema"] != SCHEMA:
        out.append(f"the verdict's schema is not {SCHEMA}")
    if not isinstance(doc["page"], str) or not PAGE_ID.match(doc["page"]):
        out.append("the verdict's page is not a page id")
    for k in ("page_sha256", "checker_sha256"):
        if not isinstance(doc[k], str) or not HEX64.match(doc[k]):
            out.append(f"the verdict's {k} is not a SHA-256")
    for key, fields, enums in (
        ("sections", {"id", "sha256", "verdict", "kind"}, {"verdict": VERDICTS, "kind": KINDS}),
        ("images", {"path", "sha256", "capture_sha256", "verdict"}, {"verdict": VERDICTS}),
    ):
        entries = doc[key]
        if not isinstance(entries, list):
            out.append(f"the verdict's {key} is not a list")
            continue
        for n, e in enumerate(entries, 1):
            if not isinstance(e, dict) or set(e) != fields:
                out.append(f"{key} entry {n} has fields other than {', '.join(sorted(fields))}")
            elif any(not isinstance(e[k], str) or not HEX64.match(e[k]) for k in fields if k.endswith("sha256")):
                out.append(f"{key} entry {n} has a hash that is not a SHA-256")
            elif any(e[k] not in allowed for k, allowed in enums.items()):
                out.append(f"{key} entry {n} has a verdict or kind outside {', '.join(VERDICTS + KINDS)}")
            elif key == "sections" and (not isinstance(e["id"], str) or not e["id"]):
                out.append(f"sections entry {n} has no id")
            elif key == "images" and not image_path_ok(e["path"]):
                out.append(f"images entry {n} has a path that is not relative to the page's folder")
    return out


def image_path_ok(path):
    p = PurePosixPath(path) if isinstance(path, str) else None
    return bool(
        p
        and path == p.as_posix()
        and not p.is_absolute()
        and ".." not in p.parts
        and "\\" not in path
        and ":" not in path
    )


def differences(page, doc):
    """Where a verdict and the page as it stands now disagree. Sections are named by their number on the page."""
    out = []
    want = skeleton(page)
    if doc["page"] != page.id:
        out.append(f"the verdict is for page {doc['page']}, not {page.id}")
    if doc["page_sha256"] != want["page_sha256"]:
        out.append("the page's hash differs from the verdict")
    have = [{"id": e["id"], "sha256": e["sha256"]} for e in doc["sections"]]
    if len(have) != len(want["sections"]):
        out.append(f"the verdict lists {len(have)} sections, the page has {len(want['sections'])}")
    else:
        out += [
            f"section {n} differs from the verdict" for n, (a, b) in enumerate(zip(have, want["sections"]), 1) if a != b
        ]
    have = {e["path"]: {k: e[k] for k in ("path", "sha256", "capture_sha256")} for e in doc["images"]}
    wanted = {e["path"]: e for e in want["images"]}
    if len(have) != len(doc["images"]):
        out.append("the verdict lists an image twice")
    out += [f"image {p} is not in the verdict" for p in sorted(wanted.keys() - have.keys())]
    out += [f"image {p} in the verdict is not on the page" for p in sorted(have.keys() - wanted.keys())]
    out += [
        f"image {p} or its capture record differs from the verdict"
        for p in sorted(wanted)
        if p in have and have[p] != wanted[p]
    ]
    if not out and [e["path"] for e in doc["images"]] != [e["path"] for e in want["images"]]:
        out.append("the verdict's images are not in path order")
    return out


def read_verdict(data):
    """A verdict file's bytes (any line ends) -> (canonical bytes, document, problems)."""
    data = signing.lf(data)  # canonical JSON holds no CR, so a checkout's CRLF is undone exactly
    try:
        doc = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return data, None, ["the verdict is not JSON"]
    problems = schema_problems(doc)
    if not problems and canonical(doc) != data:
        problems.append("the verdict is not in canonical form")
    return data, (doc if not problems else None), problems


# --- the bundle: a page, its verdict and two signatures -----------------------------------------------------------


@dataclass
class Layout:
    """Where one page's files are: in a gate or bundle folder, or in the repository's research tree."""

    id: str
    page_dir: Path
    verdict: Path
    sigs: dict  # file name -> principal (as the name gives it)
    sig_dir: Path


def folder_layout(folder, page_id):
    """A gate or bundle folder: page.md, verdict.json and verdict.json.<principal>.sig side by side."""
    folder = Path(folder)
    sigs = {}
    if folder.is_dir():
        sigs = {p.name: m.group(1) for p in folder.iterdir() if (m := SIG_NAME.match(p.name))}
    return Layout(page_id, folder, folder / VERDICT_FILE, sigs, folder)


def repo_layout(root, page_id):
    """research/pages/<id>/ and research/verdicts/<id>.json, <id>.<principal>.sig."""
    vdir = Path(root, *REPO_VERDICTS)
    pattern = re.compile(r"^" + re.escape(page_id) + r"\.(.+)\.sig$")
    sigs = {}
    if vdir.is_dir():
        sigs = {p.name: m.group(1) for p in vdir.iterdir() if (m := pattern.match(p.name))}
    return Layout(page_id, Path(root, *REPO_PAGES, page_id), vdir / f"{page_id}.json", sigs, vdir)


@dataclass
class Bundle:
    id: str
    problems: list = field(default_factory=list)
    notes: list = field(default_factory=list)  # informational: a different checker version
    page: Page = None
    doc: dict = None
    verdict: bytes = b""  # canonical bytes, as signed
    sigs: dict = field(default_factory=dict)  # principal -> signature bytes (LF), the valid ones

    @property
    def ok(self):
        return not self.problems

    @property
    def verdict_sha256(self):
        return sha256(self.verdict)


class Tools:
    """ssh-keygen and git, found once, with the allowed_signers file from a ref written to a private temp folder, and
    the command run's one Linter (its version and rules read once, from the same ref)."""

    def __init__(self, ssh_keygen=None, need_git=True, npx=None, ref=MAIN):
        self.ssh_keygen = signing.program("ssh-keygen", ssh_keygen)
        self.git = signing.program("git") if need_git else None
        self.ref = ref
        self._tmp = tempfile.TemporaryDirectory(prefix="coney-gate-")
        self.tmp = Path(self._tmp.name)
        self.allowed = None
        self.linter = lint.Linter(self.tmp, ref, npx)

    def load_allowed(self, repo):
        self.allowed = self.tmp / signing.ALLOWED_SIGNERS
        self.allowed.write_bytes(signing.allowed_signers(self.git, repo, self.ref))
        return self

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self._tmp.cleanup()


def verify_bundle(layout, tools, lint=True):
    """The one check behind publish, import, cleanroom verify and orig-check: page-check passes with the current
    rules, the page passes markdownlint (unless lint is False: orig-check reads pages already merged, which CI
    linted), the verdict is a valid coney-verdict/1 file whose every hash matches the page as it
    stands, every entry is `keep`,
    and exactly two signatures verify, from two different page reviewers with two different keys. The checker that
    passed the page is recorded; a different one is noted, not failed (the current rules must pass the page)."""
    b = Bundle(layout.id)
    if not PAGE_ID.match(layout.id):
        b.problems.append("not a page id")
        return b
    b.page = read_page(layout.id, layout.page_dir, layout.page_dir / PAGE_FILE)
    if not b.page.ok:
        b.problems.append(b.page.failing())
        return b
    if lint:
        b.problems += [f"markdownlint {f}" for f in tools.linter.check(layout.id, b.page.data)]
    try:
        raw = layout.verdict.read_bytes()
    except FileNotFoundError:
        b.problems.append("no verdict")
        return b
    except OSError as e:
        raise GateError(f"cannot read {layout.verdict}: {e.__class__.__name__}") from None
    b.verdict, b.doc, problems = read_verdict(raw)
    if problems:
        b.problems += problems
        return b
    b.problems += differences(b.page, b.doc)
    b.problems += [f"section {n} is rejected" for n, e in enumerate(b.doc["sections"], 1) if e["verdict"] != "keep"]
    b.problems += [f"image {e['path']} is rejected" for e in b.doc["images"] if e["verdict"] != "keep"]
    if b.doc["checker_sha256"] != page_check.checker_sha256():
        b.notes.append("passed by another version of page-check; the current rules pass it too")
    keys = {}
    for name, principal in sorted(layout.sigs.items()):
        if not signing.PRINCIPAL.match(principal) or not principal.startswith(REVIEWER):
            b.problems.append(f"signature {name} is not from a page reviewer")
            continue
        try:
            sig = (layout.sig_dir / name).read_bytes()
            fingerprint = signing.verify(tools.ssh_keygen, tools.allowed, b.verdict, sig, principal, tools.tmp)
        except signing.BadSignature as e:
            b.problems.append(f"signature {name}: {e}")
            continue
        except OSError as e:
            raise GateError(f"cannot read {name}: {e.__class__.__name__}") from None
        if fingerprint in keys:
            b.problems.append(f"signatures by {keys[fingerprint]} and {principal} are made with the same key")
            continue
        keys[fingerprint] = principal
        b.sigs[principal] = signing.lf(sig)
    if len(layout.sigs) != SIGNATURES:
        b.problems.append(f"{len(layout.sigs)} signatures; a page needs exactly {SIGNATURES}")
    return b


# --- the reviewers: verdict and countersign -----------------------------------------------------------------------


def reviewer_ok(principal):
    if not signing.PRINCIPAL.match(principal or "") or not principal.startswith(REVIEWER):
        raise GateError(f"--reviewer must be a page reviewer's principal, {REVIEWER}<n>")


def gate_folder(folder):
    folder = Path(folder)
    if not folder.is_dir():
        raise GateError(f"no page folder at {folder}")
    page_id = folder.resolve().name
    if not PAGE_ID.match(page_id):
        raise GateError(f"the page folder's name is not a page id ({PAGE_ID.pattern}): {page_id}")
    return folder, page_id


def load_decisions(path):
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8-sig"))
    except OSError as e:
        raise GateError(f"cannot read the decisions file {path}: {e.__class__.__name__}") from None
    except ValueError:
        raise GateError(f"the decisions file {path} is not JSON") from None
    if not isinstance(doc, dict) or not set(doc) <= {"sections", "images"}:
        raise GateError('the decisions file must be {"sections": {...}, "images": {...}}')
    sections, images = doc.get("sections", {}), doc.get("images", {})
    if not isinstance(sections, dict) or not isinstance(images, dict):
        raise GateError('the decisions file\'s "sections" and "images" must be objects')
    return sections, images


def decide(page, sections, images):
    """The verdict document: the page's hashes with a decision for every section and image, and nothing else."""
    ids = [s.id for s in page.sections]
    paths = [i["path"] for i in page.images]
    problems = [f"no decision for section {i}" for i in ids if i not in sections]
    problems += [f"decision for unknown section {i}" for i in sections if i not in ids]
    problems += [f"no decision for image {p}" for p in paths if p not in images]
    problems += [f"decision for unknown image {p}" for p in images if p not in paths]
    for i, d in sections.items():
        if i in ids and (
            not isinstance(d, dict)
            or set(d) != {"verdict", "kind"}
            or d["verdict"] not in VERDICTS
            or d["kind"] not in KINDS
        ):
            problems.append(f'section {i}: give {{"verdict": {"|".join(VERDICTS)}, "kind": {"|".join(KINDS)}}}')
    for p, d in images.items():
        if p in paths and d not in VERDICTS:
            problems.append(f"image {p}: give {' or '.join(VERDICTS)}")
    if problems:
        raise GateError("the decisions do not fit the page: " + "; ".join(problems))
    doc = skeleton(page)
    doc["checker_sha256"] = page_check.checker_sha256()
    for e in doc["sections"]:
        e.update(verdict=sections[e["id"]]["verdict"], kind=sections[e["id"]]["kind"])
    for e in doc["images"]:
        e["verdict"] = images[e["path"]]
    return doc


def write_new(path, data):
    """Writes a file that must not exist yet: the gate folder is add-only."""
    try:
        with open(path, "xb") as f:
            f.write(data)
    except FileExistsError:
        raise GateError(f"{path} exists already; the gate folder is add-only") from None
    except OSError as e:
        raise GateError(f"cannot write {path}: {e.__class__.__name__}") from None


def lint_failure(page_id, found, outcome):
    """A page markdownlint fails: one line saying so, then each finding's line and rule (never its text)."""
    return [f"FAIL {page_id}: markdownlint finds {len(found)} problems; {outcome}", *(f"  {f}" for f in found)]


def verdict(folder, reviewer, key, decisions, ssh_keygen=None, npx=None):
    """The first reviewer: writes verdict.json in the page folder and signs it, once the page passes page-check and
    markdownlint. (code, lines)."""
    reviewer_ok(reviewer)
    folder, page_id = gate_folder(folder)
    target = folder / VERDICT_FILE
    if target.exists():
        raise GateError(f"{target} exists already; the gate folder is add-only (to sign it, use --countersign)")
    page = read_page(page_id, folder, folder / PAGE_FILE)
    if not page.ok:
        return 1, [f"FAIL {page_id}: {page.failing()}; no verdict can be given"]
    with Tools(ssh_keygen, need_git=False, npx=npx) as tools:
        found = tools.linter.check(page_id, page.data)
        if found:
            return 1, lint_failure(page_id, found, "no verdict can be given")
        doc = decide(page, *load_decisions(decisions))
        data = canonical(doc)
        sig = signing.sign(tools.ssh_keygen, key, data)
    write_new(target, data)
    write_new(folder / f"{VERDICT_FILE}.{reviewer}.sig", sig)
    return 0, [f"verdict {sha256(data)} for {page_id} written and signed by {reviewer}"]


def countersign(folder, reviewer, key, ssh_keygen=None, npx=None):
    """The second reviewer: signs the existing verdict.json only if every hash in it matches the page and its
    images as they stand, and the page passes markdownlint as its rules stand now. Decisions cannot change here; a
    reviewer who disagrees does not sign. (code, lines)."""
    reviewer_ok(reviewer)
    folder, page_id = gate_folder(folder)
    layout = folder_layout(folder, page_id)
    name = f"{VERDICT_FILE}.{reviewer}.sig"
    if name in layout.sigs:
        raise GateError(f"{reviewer} has signed this verdict already")
    if len(layout.sigs) != 1:
        raise GateError(f"the verdict has {len(layout.sigs)} signatures; countersigning needs exactly one")
    try:
        raw = layout.verdict.read_bytes()
    except OSError as e:
        raise GateError(f"cannot read {layout.verdict}: {e.__class__.__name__}") from None
    page = read_page(page_id, folder, folder / PAGE_FILE)
    if not page.ok:
        return 1, [f"FAIL {page_id}: {page.failing()}; not signed"]
    data, doc, problems = read_verdict(raw)
    if not problems:
        problems = differences(page, doc)
    if problems:
        return 1, [f"FAIL {page_id}: not signed: {'; '.join(problems)}"]
    with Tools(ssh_keygen, need_git=False, npx=npx) as tools:
        found = tools.linter.check(page_id, page.data)
        if found:
            return 1, lint_failure(page_id, found, "not signed")
        sig = signing.sign(tools.ssh_keygen, key, data)
    write_new(folder / name, sig)
    return 0, [f"verdict {sha256(data)} for {page_id} countersigned by {reviewer}"]


# --- crossing: publish (analysis side) and import (public side) ---------------------------------------------------


def report(b):
    if b.ok:
        lines = [f"PASS {b.id}: verdict {b.verdict_sha256}, signed by {', '.join(sorted(b.sigs))}"]
    else:
        lines = [f"FAIL {b.id}: {'; '.join(b.problems)}"]
    return lines + [f"  note: {n}" for n in b.notes]


def bundle_files(b):
    """Bundle path -> bytes: exactly the verified page, images, capture records, verdict and two signatures."""
    files = {PAGE_FILE: b.page.data, VERDICT_FILE: b.verdict, **b.page.files}
    files.update({f"{VERDICT_FILE}.{p}.sig": sig for p, sig in b.sigs.items()})
    return files


def publish(page_id, ws, ssh_keygen=None, npx=None):
    """Verifies the page in the gate and copies the verified bytes into the analysis side's outbox, under
    pages/<id>-<first 12 hex of the verdict's hash>/. (code, lines)."""
    if not PAGE_ID.match(page_id or ""):
        raise GateError(f"not a page id ({PAGE_ID.pattern}): {page_id}")
    folder = ws.where("gate") / page_id
    if not folder.is_dir():
        raise GateError(f"no page folder at {folder}")
    with Tools(ssh_keygen, npx=npx).load_allowed(ws.where("repo")) as tools:
        b = verify_bundle(folder_layout(folder, page_id), tools)
    if not b.ok:
        return 1, report(b)
    out = ws.where("from_analyst") / "pages"
    target = out / f"{page_id}-{b.verdict_sha256[:12]}"
    if target.exists():
        raise GateError(f"{target} exists already")
    try:
        out.mkdir(parents=True, exist_ok=True)
        # A plain mkdir, not tempfile.mkdtemp: since Python 3.13, mkdtemp's mode 0o700 gives the folder its own
        # permissions on Windows, which the analysis account's add-only right on the outbox does not allow.
        tmp = out / f".{page_id}-{secrets.token_hex(8)}"
        tmp.mkdir()
    except OSError as e:
        raise GateError(f"cannot write into {out}: {e.__class__.__name__}") from None
    try:
        for rel, data in bundle_files(b).items():
            dest = tmp / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(data)
        os.rename(tmp, target)
    except OSError as e:
        shutil.rmtree(tmp, ignore_errors=True)
        raise GateError(f"cannot write {target}: {e.__class__.__name__}") from None
    return 0, [*report(b), f"published to {target}"]


def repo_files(b):
    """Repository path -> bytes for a verified bundle. Text (the page, capture records, signatures) is stored with
    LF line ends; the hashes are taken that way, so nothing a reviewer saw changes."""
    pages = "/".join((*REPO_PAGES, b.id))
    verdicts = "/".join(REPO_VERDICTS)
    files = {f"{pages}/{PAGE_FILE}": signing.lf(b.page.data), f"{verdicts}/{b.id}.json": b.verdict}
    for rel, data in b.page.files.items():
        files[f"{pages}/{rel}"] = signing.lf(data) if rel.endswith(CAPTURE) else data
    files.update({f"{verdicts}/{b.id}.{p}.sig": sig for p, sig in b.sigs.items()})
    return files


def is_withdrawn(withdrawn, verdict_sha256, page_sha256):
    """Whether the workspace's withdrawn list (Workspace.withdrawn, or None for none) names the verdict or its page."""
    w = withdrawn or {}
    return verdict_sha256 in w.get("verdicts", ()) or page_sha256 in w.get("pages", ())


def import_bundle(bundle, repo=None, ssh_keygen=None, npx=None, withdrawn=None):
    """Verifies a published bundle and commits it to a new branch page/<id> from main, with git plumbing only (a
    temporary index): the working tree, the index and the current branch are never touched. A bundle whose verdict
    or page the withdrawn list names fails. (code, lines)."""
    bundle = Path(bundle)
    repo = Path(repo) if repo else own_checkout()
    try:
        data, doc, problems = read_verdict((bundle / VERDICT_FILE).read_bytes())
    except OSError as e:
        raise GateError(f"cannot read {bundle / VERDICT_FILE}: {e.__class__.__name__}") from None
    if problems:
        return 1, [f"FAIL {bundle.name}: {'; '.join(problems)}"]
    page_id = doc["page"]
    if bundle.resolve().name != f"{page_id}-{sha256(data)[:12]}":
        return 1, [f"FAIL {bundle.name}: the folder's name is not {page_id}-<first 12 hex of the verdict's hash>"]
    if is_withdrawn(withdrawn, sha256(data), doc["page_sha256"]):
        return 1, [f"FAIL {bundle.name}: withdrawn (the workspace's withdrawn list names its verdict or page)"]
    with Tools(ssh_keygen, npx=npx).load_allowed(repo) as tools:
        b = verify_bundle(folder_layout(bundle, page_id), tools)
        if not b.ok:
            return 1, report(b)
        commit = commit_page(tools, repo, b)
    return 0, [*report(b), f"committed {commit} to page/{page_id} from {MAIN}"]


def commit_page(tools, repo, b):
    """A commit on main's tree with the page's old files (if any) replaced by the bundle's, on a new branch."""
    branch = f"refs/heads/page/{b.id}"
    exe = tools.git
    if signing.git(exe, repo, "rev-parse", "--verify", "--quiet", branch, ok=(0, 1)).returncode == 0:
        raise GateError(f"branch page/{b.id} exists already")
    r = signing.git(exe, repo, "rev-parse", "--verify", "--quiet", f"refs/heads/{MAIN}^{{commit}}", ok=(0, 1))
    if r.returncode:
        raise GateError(f"no branch {MAIN} in {repo}")
    base = r.stdout.decode().strip()
    env = signing.git_env(GIT_INDEX_FILE=str(tools.tmp / "index"))
    signing.git(exe, repo, "read-tree", base, env=env)
    pages = "/".join((*REPO_PAGES, b.id)) + "/"
    verdicts = "/".join(REPO_VERDICTS) + "/" + b.id + "."
    listed = signing.git(exe, repo, "ls-files", "-z", "--cached", env=env).stdout.decode("utf-8").split("\0")
    old = [p for p in listed if p.startswith(pages) or (p.startswith(verdicts) and "/" not in p[len(verdicts) :])]
    if old:
        signing.git(
            exe, repo, "update-index", "-z", "--force-remove", "--stdin", data="\0".join(old).encode() + b"\0", env=env
        )
    entries = []
    for path, data in sorted(repo_files(b).items()):
        blob = signing.git(exe, repo, "hash-object", "-w", "--no-filters", "--stdin", data=data, env=env)
        entries.append(f"100644 {blob.stdout.decode().strip()}\t{path}\0")
    signing.git(exe, repo, "update-index", "-z", "--index-info", data="".join(entries).encode("utf-8"), env=env)
    tree = signing.git(exe, repo, "write-tree", env=env).stdout.decode().strip()
    verb = "Update" if f"{pages}{PAGE_FILE}" in old else "Add"
    message = f"research: {verb} the {b.id} page\n\nVerdict: {b.verdict_sha256}\n"
    commit = signing.git(exe, repo, "commit-tree", tree, "-p", base, "-F", "-", data=message.encode(), env=env)
    commit = commit.stdout.decode().strip()
    r = signing.git(
        exe, repo, "update-ref", "-m", f"research import {b.id}", branch, commit, "", ok=(0, 1, 128), env=env
    )
    if r.returncode:
        raise GateError(f"branch page/{b.id} exists already")
    return commit


# --- cleanroom verify: every page in a checkout -------------------------------------------------------------------


def verify_repo(repo=None, ref=MAIN, ssh_keygen=None, npx=None):
    """Every page under research/pages/ with its verdict and two signatures, every verdict file with its page, no
    stray file. One line per page, ids and results only. (code, lines)."""
    repo = Path(repo) if repo else own_checkout()
    pages_dir, verdicts_dir = Path(repo, *REPO_PAGES), Path(repo, *REPO_VERDICTS)
    lines, failed = [], False
    ids = sorted(p.name for p in pages_dir.iterdir()) if pages_dir.is_dir() else []
    with Tools(ssh_keygen, npx=npx, ref=ref).load_allowed(repo) as tools:
        for page_id in ids:
            folder = pages_dir / page_id
            if not folder.is_dir() or not PAGE_ID.match(page_id) or not (folder / PAGE_FILE).is_file():
                lines.append(f"FAIL {page_id}: not a page folder (a page id holding {PAGE_FILE})")
                failed = True
                continue
            b = verify_bundle(repo_layout(repo, page_id), tools)
            if b.page is not None and b.page.ok:
                known = {PAGE_FILE, *b.page.files}
                stray = sorted(
                    p.relative_to(folder).as_posix()
                    for p in folder.rglob("*")
                    if p.is_file() and p.relative_to(folder).as_posix() not in known
                )
                b.problems += [f"stray file {s}" for s in stray]
            failed |= not b.ok
            lines += report(b)
    pages = {p for p in ids if PAGE_ID.match(p)}
    if verdicts_dir.is_dir():
        for p in sorted(verdicts_dir.iterdir()):
            m = re.match(r"^([a-z0-9][a-z0-9-]{0,63})\.(?:json|[a-z0-9][a-z0-9-]{0,63}\.sig)$", p.name)
            if not p.is_file() or not m or m.group(1) not in pages:
                lines.append(f"FAIL {'/'.join(REPO_VERDICTS)}/{p.name}: stray verdict file (no page of its own)")
                failed = True
    if not ids and not lines:
        lines.append("no research pages")
    return (1 if failed else 0), lines


# --- status: what waits in each role's inbox ----------------------------------------------------------------------
#
# Each role's session polls its own inbox on a timer, so this is cheap: file presence and small reads only, no
# page-check, no markdownlint, no signature check. It names ids, states, counts and hashes, never page text.

COORDINATOR, REVIEWER_1, REVIEWER_2, PUBLIC = "research-coordinator", "page-reviewer-1", "page-reviewer-2", "public"
ROLES = {  # role -> (its states, in the order they are counted; the states that need its action)
    COORDINATOR: (
        (
            "awaiting-reviewer-1",
            "awaiting-reviewer-2",
            "declined",
            "rejected",
            "invalid-verdict",
            "invalid-signatures",
            "invalid-name",
            "ready-to-publish",
            "published",
        ),
        ("ready-to-publish", "rejected", "declined", "invalid-verdict", "invalid-signatures"),
    ),
    REVIEWER_1: (("awaiting-reviewer-1",), ("awaiting-reviewer-1",)),
    REVIEWER_2: (("awaiting-reviewer-2",), ("awaiting-reviewer-2",)),
    PUBLIC: (
        ("new", "imported", "merged", "superseded", "withdrawn", "withdrawn-imported"),
        ("new", "imported", "withdrawn-imported"),
    ),
}
DRILL = "gate-drill-"  # page folders for drills: no inbox lists them
DECLINE = re.compile(r"^review\.(.+)\.txt$")  # review.<principal>.txt: a reviewer's note that it did not sign
BUNDLE = re.compile(r"^(.+)-([0-9a-f]{12})$")  # <id>-<first 12 hex of the verdict's hash>, in the outbox
OUTBOX = "pages"  # under the workspace's from_analyst
MAX_VERDICT = 1 << 20  # bytes; a larger verdict.json is not read, and counts as invalid


def outbox_of(ws):
    """The outbox, <from_analyst>/pages. A missing from_analyst cannot run; a missing pages folder is an empty
    outbox (publish makes it on its first run)."""
    base = ws.where("from_analyst")
    if not base.is_dir():
        raise GateError(f"no outbox folder at {base}")
    return base / OUTBOX


@dataclass
class Look:
    """What a gate folder holds, read cheaply: names, and the verdict's bytes when there is one."""

    id: str
    page: bool = False
    verdict: bool = False
    sha12: str = None  # of the verdict as read (CRLF as LF), as publish names the bundle
    invalid: bool = False  # the verdict is not a valid verdict for this page
    rejected: bool = False
    sigs: set = field(default_factory=set)  # principals, as the signature files' names give them
    declines: set = field(default_factory=set)


def look(folder):
    names = {p.name for p in folder.iterdir()}
    lk = Look(folder.name, PAGE_FILE in names, VERDICT_FILE in names)
    lk.sigs = {m.group(1) for n in names if (m := SIG_NAME.match(n))}
    lk.declines = {m.group(1) for n in names if (m := DECLINE.match(n))}
    if lk.verdict:
        path = folder / VERDICT_FILE
        if path.stat().st_size > MAX_VERDICT:
            lk.invalid = True
            return lk
        data, doc, problems = read_verdict(path.read_bytes())
        lk.sha12 = sha256(data)[:12]
        lk.invalid = bool(problems) or doc["page"] != lk.id
        if not lk.invalid:
            lk.rejected = any(e["verdict"] != "keep" for e in doc["sections"] + doc["images"])
    return lk


def coordinator_state(lk, outbox):
    if not lk.verdict:
        return "awaiting-reviewer-1"
    if lk.invalid:
        return "invalid-verdict"
    if lk.rejected:
        return "rejected"
    if lk.declines:
        return "declined"
    if len(lk.sigs) == 1:
        # Reviewer 1 signs first; a page signed only by reviewer 2 is in no reviewer's inbox, so it is the
        # coordinator's to sort out rather than waiting unseen.
        return "awaiting-reviewer-2" if REVIEWER_1 in lk.sigs else "invalid-signatures"
    if len(lk.sigs) != SIGNATURES:
        return "invalid-signatures"
    # Only this bundle's own name is looked up: the analysis side may add to the outbox without listing it.
    return "published" if (outbox / f"{lk.id}-{lk.sha12}").is_dir() else "ready-to-publish"


def reviewer_state(lk, role):
    if role == REVIEWER_1:
        return "awaiting-reviewer-1" if not lk.verdict else None
    ready = lk.verdict and not lk.invalid and not lk.rejected and len(lk.sigs) == 1
    return "awaiting-reviewer-2" if ready and role not in lk.sigs and role not in lk.declines else None


def gate_pages(role, gate, outbox):
    """[(id, state, verdict hash prefix)] for the gate folder as the role sees it."""
    if not gate.is_dir():
        raise GateError(f"no gate folder at {gate}")
    out = []
    for folder in sorted(gate.iterdir()):
        if not folder.is_dir() or folder.name.startswith((DRILL, ".")):
            continue  # a drill, or a tool's hidden folder (the reviewers' sessions start in the gate)
        if not PAGE_ID.match(folder.name):
            if role == COORDINATOR:
                out.append((folder.name, "invalid-name", None))
            continue
        lk = look(folder)
        if not lk.page:
            continue
        state = coordinator_state(lk, outbox) if role == COORDINATOR else reviewer_state(lk, role)
        if state:
            out.append((lk.id, state, lk.sha12))
    return out


def read_objects(exe, repo, names):
    """{name: (type, bytes) or None} for git object names (`<ref>:<path>` and the like), read in one `git cat-file
    --batch`: plumbing, no filters, never the working tree."""
    r = signing.git(exe, repo, "cat-file", "--batch", data="".join(f"{n}\n" for n in names).encode())
    out, pos, found = r.stdout, 0, {}
    for name in names:
        end = out.find(b"\n", pos)
        head = out[pos:end].split() if end >= 0 else []
        if len(head) == 3 and head[2].isdigit():  # <oid> <type> <size>, then the object and LF
            size = int(head[2])
            found[name] = (head[1].decode(), out[end + 1 : end + 1 + size])
            pos = end + 1 + size + 1
        elif len(head) == 2:  # <name> missing (or ambiguous)
            found[name] = None
            pos = end + 1
        else:
            raise signing.SigningError("git cat-file gave output that cannot be read")
    return found


def bundle_withdrawn(folder, withdrawn):
    """Whether the withdrawn list names the bundle's verdict or page. A verdict that cannot be read is left to the
    import, which fails it."""
    if not withdrawn or not any(withdrawn.values()):
        return False
    path = folder / VERDICT_FILE
    if path.stat().st_size > MAX_VERDICT:
        return False
    data, doc, _ = read_verdict(path.read_bytes())
    return doc is not None and is_withdrawn(withdrawn, sha256(data), doc["page_sha256"])


def public_pages(outbox, repo, withdrawn=None):
    """[(id, state, verdict hash prefix)] for every bundle in the outbox: new, imported (on page/<id>) or merged (on
    main), by the hash of research/verdicts/<id>.json on those branches; withdrawn when the withdrawn list names it,
    and withdrawn-imported when such a bundle is on page/<id> or main all the same."""
    try:
        names = sorted(p.name for p in outbox.iterdir() if p.is_dir())
    except FileNotFoundError:
        names = []
    bundles = []
    for name in names:
        m = BUNDLE.match(name)
        if name.startswith(DRILL) or not m or not PAGE_ID.match(m.group(1)):
            continue  # a drill, publish's temporary folder or a stray
        if (outbox / name / VERDICT_FILE).is_file():
            bundles.append((m.group(1), m.group(2)))
    head = f"refs/heads/{MAIN}"

    def path(ref, page_id):
        return f"{ref}:{'/'.join(REPO_VERDICTS)}/{page_id}.json"

    names = [head] + [path(r, i) for i, _ in bundles for r in (head, f"refs/heads/page/{i}")]
    found = read_objects(signing.program("git"), repo, names)
    if not found[head]:
        raise GateError(f"no branch {MAIN} in {repo}")

    def holds(ref, page_id, sha12):
        blob = found[path(ref, page_id)]
        return blob is not None and blob[0] == "blob" and sha256(signing.lf(blob[1])).startswith(sha12)

    def superseded(page_id, sha12):
        # main holds another verdict for this page: the bundle is outdated when it was published before main's
        # verdict was committed (an import always follows its publish), a new revision when published after.
        blob = found[path(head, page_id)]
        if blob is None or blob[0] != "blob":
            return False
        rel = "/".join(REPO_VERDICTS) + f"/{page_id}.json"
        r = signing.git(signing.program("git"), repo, "log", "-1", "--format=%ct", head, "--", rel)
        committed = int(r.stdout.decode().strip() or 0)
        return (outbox / f"{page_id}-{sha12}").stat().st_mtime <= committed

    out = []
    for page_id, sha12 in bundles:
        if bundle_withdrawn(outbox / f"{page_id}-{sha12}", withdrawn):
            on = holds(head, page_id, sha12) or holds(f"refs/heads/page/{page_id}", page_id, sha12)
            state = "withdrawn-imported" if on else "withdrawn"
        elif holds(head, page_id, sha12):
            state = "merged"
        elif holds(f"refs/heads/page/{page_id}", page_id, sha12):
            state = "imported"
        elif superseded(page_id, sha12):
            state = "superseded"
        else:
            state = "new"
        out.append((page_id, state, sha12))
    return out


def status(role, ws, repo=None):
    """A role's inbox: {"role", "pages": [{"id", "state", "verdict"}], "counts", "action"}. Reviewers read the gate
    only; the coordinator the gate and the outbox (by name, never listed); public the outbox and the repo."""
    if role not in ROLES:
        raise GateError(f"--role must be one of {', '.join(ROLES)}")
    if repo and role != PUBLIC:
        raise GateError(f"--repo applies to --role {PUBLIC} only")
    try:
        if role == PUBLIC:
            pages = public_pages(outbox_of(ws), Path(repo) if repo else own_checkout(), ws.withdrawn)
        else:
            pages = gate_pages(role, ws.where("gate"), outbox_of(ws) if role == COORDINATOR else None)
    except OSError as e:
        raise GateError(f"cannot read {e.filename or 'a folder'}: {e.__class__.__name__}") from None
    states, needs = ROLES[role]
    counts = {s: 0 for s in states}
    for _, state, _ in pages:
        counts[state] += 1
    action = list(dict.fromkeys(i for i, state, _ in pages if state in needs))
    return {
        "role": role,
        "pages": [{"id": i, "state": state, "verdict": sha12} for i, state, sha12 in pages],
        "counts": counts,
        "action": action,
    }


def status_lines(doc):
    """One line per page, `<state> <id> [<verdict hash prefix>]`, then the counts."""
    lines = [" ".join(x for x in (p["state"], p["id"], p["verdict"]) if x) for p in doc["pages"]]
    counts = ", ".join(f"{n} {s}" for s, n in doc["counts"].items() if n)
    lines.append(f"{len(doc['pages'])} pages{': ' + counts if counts else ''}; {len(doc['action'])} need {doc['role']}")
    return lines


def import_all(ws, repo=None, ssh_keygen=None, npx=None):
    """Imports every new bundle in the outbox, one by one, going on after a failure. (code, lines): 0 all imported
    or none new, 1 one failed a check, 2 one could not be imported (the worst result counts)."""
    repo = Path(repo) if repo else own_checkout()
    outbox = outbox_of(ws)
    try:
        new = [(i, sha12) for i, state, sha12 in public_pages(outbox, repo, ws.withdrawn) if state == "new"]
    except OSError as e:
        raise GateError(f"cannot read {e.filename or 'the outbox'}: {e.__class__.__name__}") from None
    code, lines, done = 0, [], 0
    for page_id, sha12 in new:
        name = f"{page_id}-{sha12}"
        try:
            c, out = import_bundle(outbox / name, repo, ssh_keygen, npx, ws.withdrawn)
        except (GateError, signing.SigningError, lint.LintError) as e:
            c, out = 2, [f"ERROR {name}: {e}"]
        code, done = max(code, c), done + (c == 0)
        lines += out
    lines.append(f"imported {done} of {len(new)} new bundles")
    return code, lines
