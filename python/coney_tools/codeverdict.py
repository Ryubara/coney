"""The code gate's third check (clean-room page, section 7): a PR analyst's signed similarity verdict covers every
engine source file a change adds or modifies.

A verdict is `research/code-verdicts/<name>.json` with its signatures beside it as `<name>.<principal>.sig`. It holds
no free text: the commit the analyst judged, every source file judged with its git blob, one entry per tagged function
(its file, its `@orig` address and the verdict on it) and the scripted pre-filter's outcome. Its bytes are
research.canonical's, with `files` sorted by path and `functions` by path then address, so the same verdict always
gives the same signed bytes.

`code-verdict` reads everything from git, never the working tree: the files the range changes under the engine root
(from the merge base of its two ends, so work merged into the base meanwhile is not counted), and the verdict files the
range adds. Each added verdict must be well formed, canonical, pass (every function `independent`, the pre-filter
`pass`) and carry at least one signature that verifies in the verdict namespace by a key allowed_signers (read from
the ref) lists under a PR analyst's principal; any other file in the folder, or a signature that does not verify,
fails. Each changed source file must be listed with its exact blob by an added verdict, and each `@orig` address in it
must have an `independent` entry there. Verdicts merged earlier are not checked again.

`code-verdict-sign` is the analyst side's: it signs a verdict that passes, and refuses one that does not or that the
scripted pre-filter's result (`cleanroom prefilter`, in prefilter.py) does not match.
"""

import json
import re
from pathlib import Path, PurePosixPath

from coney_tools import codegate, research, signing

FOLDER = "research/code-verdicts"
SCHEMA = "coney-code-verdict/1"
VERDICTS = ("independent", "parallel", "transcribed")
PASSING = "independent"
PREFILTER = ("pass", "flag")
SHA1 = re.compile(r"^[0-9a-f]{40}$")
ADDRESS = re.compile(r"^0x[0-9a-f]{8}$")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$")
SIG = re.compile(r"^([A-Za-z0-9][A-Za-z0-9_-]{0,63})\.([a-z0-9][a-z0-9-]{0,63})\.sig$")
KEYS = {"schema", "commit", "files", "functions", "prefilter"}
PREFILTER_SCHEMA = "coney-prefilter/1"  # the scripted pre-filter's result (cleanroom prefilter --out)
PREFILTER_KEYS = {"schema", "commit", "outcome", "functions"}


class VerdictError(codegate.CodeGateError):
    """The command could not run (exit 2)."""


# --- one verdict ------------------------------------------------------------------------------------------------


def problems(doc):
    """Why a parsed verdict is malformed, as lines; empty when it is well formed (whether it passes is `failing`)."""
    if not isinstance(doc, dict) or set(doc) != KEYS:
        return [f"not an object with exactly {', '.join(sorted(KEYS))}"]
    out = []
    if doc["schema"] != SCHEMA:
        out.append(f"schema is not {SCHEMA}")
    if not isinstance(doc["commit"], str) or not SHA1.match(doc["commit"]):
        out.append("commit is not a 40-digit lower-case commit id")
    if doc["prefilter"] not in PREFILTER:
        out.append(f"prefilter is not one of {', '.join(PREFILTER)}")
    files, functions = doc["files"], doc["functions"]
    paths = []
    if not isinstance(files, list) or not files:
        out.append("files is not a list of one or more files")
        files = []
    for f in files:
        if not isinstance(f, dict) or set(f) != {"path", "blob"}:
            out.append("a files entry is not an object with exactly path, blob")
        elif not research.image_path_ok(f["path"]) or not isinstance(f["blob"], str) or not SHA1.match(f["blob"]):
            out.append("a files entry has a bad path or blob")
        else:
            paths.append(f["path"])
    if paths != sorted(set(paths)):
        out.append("files is not sorted by path, each listed once")
    keys = []
    if not isinstance(functions, list):
        out.append("functions is not a list")
        functions = []
    for f in functions:
        if not isinstance(f, dict) or set(f) != {"path", "address", "verdict"}:
            out.append("a functions entry is not an object with exactly path, address, verdict")
        elif f["path"] not in paths or not isinstance(f["address"], str) or not ADDRESS.match(f["address"]):
            out.append("a functions entry names a file not in files, or a bad address")
        elif f["verdict"] not in VERDICTS:
            out.append(f"a functions entry's verdict is not one of {', '.join(VERDICTS)}")
        else:
            keys.append((f["path"], f["address"]))
    if keys != sorted(set(keys)):
        out.append("functions is not sorted by path then address, each listed once")
    return out


def failing(doc):
    """Why a well-formed verdict does not pass, as lines (addresses and verdict names only)."""
    out = [] if doc["prefilter"] == "pass" else ["the pre-filter flagged it"]
    out += [f"{f['path']} {f['address']}: {f['verdict']}" for f in doc["functions"] if f["verdict"] != PASSING]
    return out


def parse(data):
    """(doc, problems) for a verdict's bytes: well formed and in canonical form, or why not."""
    try:
        doc = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, ["not UTF-8 JSON"]
    found = problems(doc)
    if not found and research.canonical(doc) != data:
        found = ["not in canonical form (sorted keys, two-space indent, one final LF)"]
    return doc, found


# --- the check --------------------------------------------------------------------------------------------------


def ends(exe, repo, rng):
    """(start, end) commit ids of an A..B range: the start is the merge base of A and B."""
    a, sep, b = rng.partition("..")
    if not sep or not a or not b or b.startswith(".") or rng.startswith("-"):
        raise VerdictError(f"not an A..B revision range: {rng!r}")
    ids = []
    for rev in (a, b):
        r = signing.git(exe, repo, "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}", ok=(0, 1))
        if r.returncode:
            raise VerdictError(f"no commit {rev!r} in {repo}")
        ids.append(r.stdout.decode().strip())
    r = signing.git(exe, repo, "merge-base", ids[0], ids[1], ok=(0, 1))
    if r.returncode:
        raise VerdictError(f"{rng} has no merge base")
    return r.stdout.decode().strip(), ids[1]


def changed(exe, repo, start, end, folder):
    """{path: blob} of every file under the folder that exists at end and differs from start."""
    r = signing.git(exe, repo, "diff-tree", "-r", "-z", "--no-renames", start, end, "--", folder)
    out, parts = {}, r.stdout.decode("utf-8", "surrogateescape").split("\0")
    for head, path in zip(parts[0::2], parts[1::2], strict=False):
        fields = head.lstrip(":").split()
        if len(fields) == 5 and fields[4] in ("A", "M", "T") and not fields[1].startswith("16"):
            out[path] = fields[3]
    return out


def check_signatures(tools, name, data, sigs):
    """(principals that signed, problems) for one verdict's signature files {principal: bytes}."""
    signed, out = [], []
    for principal, sig in sorted(sigs.items()):
        if not principal.startswith(signing.PR_ANALYST):
            out.append(f"{name}: a signature by {principal}, which is not a PR analyst")
            continue
        try:
            signing.verify(tools.ssh_keygen, tools.allowed, data, sig, principal, tools.tmp)
        except signing.BadSignature as e:
            out.append(f"{name}: {principal}'s signature: {e}")
            continue
        signed.append(principal)
    if not sigs:
        out.append(f"{name}: not signed")
    return signed, out


def code_verdict(rng, repo=None, ref=research.MAIN, ssh_keygen=None, as_json=False):
    """(code, lines): every engine source file the range changes is covered by a passing verdict the range adds."""
    repo = Path(repo) if repo else research.own_checkout()
    found, verdicts = [], {}
    with research.Tools(ssh_keygen, ref=ref) as tools:
        s = codegate.settings(tools.git, repo, ref)
        tools.load_allowed(repo)
        start, end = ends(tools.git, repo, rng)
        sources = {
            p: b
            for p, b in changed(tools.git, repo, start, end, s.engine_root).items()
            if PurePosixPath(p).suffix.lower() in s.extensions
        }
        added = changed(tools.git, repo, start, end, FOLDER)
        blobs = research.read_objects(tools.git, repo, [*added.values(), *sources.values()])
        jsons, sigs = {}, {}
        for path, blob in sorted(added.items()):
            base = PurePosixPath(path).name
            stem, m = base.removesuffix(".json"), SIG.match(base)
            data = (blobs.get(blob) or (None, b""))[1]
            if base.endswith(".json") and NAME.match(stem) and PurePosixPath(path).parent.as_posix() == FOLDER:
                jsons[stem] = data
            elif m and PurePosixPath(path).parent.as_posix() == FOLDER:
                sigs.setdefault(m.group(1), {})[m.group(2)] = data
            else:
                found.append(f"{path}: not a verdict or a signature (<name>.json, <name>.<principal>.sig)")
        for stem in sorted(set(sigs) - set(jsons)):
            found.append(f"{FOLDER}/{stem}: signatures without their verdict in this range")
        for stem, data in sorted(jsons.items()):
            doc, bad = parse(data)
            if bad:
                found += [f"{stem}: {b}" for b in bad]
                continue
            signed, bad = check_signatures(tools, stem, data, sigs.get(stem, {}))
            found += bad
            fails = failing(doc)
            found += [f"{stem}: {f}" for f in fails]
            if signed and not bad and not fails:
                verdicts[stem] = (doc, signed)
    covered = []
    for path, blob in sorted(sources.items()):
        holders = [(n, d) for n, (d, _) in verdicts.items() if {"path": path, "blob": blob} in d["files"]]
        if not holders:
            found.append(f"{path}: no passing verdict in this range lists it at blob {blob[:12]}")
            continue
        text = blobs.get(blob)
        tags = tags_of(path, text[1] if text else b"")
        judged = {f["address"] for _, d in holders for f in d["functions"] if f["path"] == path}
        for address in sorted(set(tags) - judged):
            found.append(f"{path}: {address} has no verdict")
        covered.append(path)
    failed = bool(found)
    summary = (
        f"{'FAIL' if failed else 'PASS'}: {len(sources)} engine source files changed in {rng}, {len(covered)} "
        f"covered; {len(verdicts)} passing verdicts added"
    )
    if as_json:
        doc = {
            "ok": not failed,
            "range": rng,
            "ref": ref,
            "changed": sorted(sources),
            "covered": covered,
            "verdicts": {n: signed for n, (_, signed) in sorted(verdicts.items())},
            "problems": found,
        }
        return (1 if failed else 0), [json.dumps(doc, indent=2)]
    lines = [f"PASS {n}: signed by {', '.join(signed)}" for n, (_, signed) in sorted(verdicts.items())]
    return (1 if failed else 0), [*lines, *(f"FAIL {f}" for f in found), summary]


def tags_of(path, data):
    """The @orig addresses tagged in one source file's bytes (malformed tags are orig-check's to report)."""
    try:
        text = data.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    except UnicodeDecodeError:
        return []
    _, comments = codegate.lex(text)
    return [t.address for t in codegate.read_tags(path, text, comments, []) if t.address]


# --- signing (the analyst side) ---------------------------------------------------------------------------------


def prefilter_problems(doc):
    """Why a parsed pre-filter result (cleanroom prefilter --out) is malformed, as lines; empty when well formed."""
    if not isinstance(doc, dict) or set(doc) != PREFILTER_KEYS:
        return [f"not an object with exactly {', '.join(sorted(PREFILTER_KEYS))}"]
    out = []
    if doc["schema"] != PREFILTER_SCHEMA:
        out.append(f"schema is not {PREFILTER_SCHEMA}")
    if not isinstance(doc["commit"], str) or not SHA1.match(doc["commit"]):
        out.append("commit is not a 40-digit lower-case commit id")
    if doc["outcome"] not in PREFILTER:
        out.append(f"outcome is not one of {', '.join(PREFILTER)}")
    functions = doc["functions"] if isinstance(doc["functions"], list) else None
    if functions is None:
        return [*out, "functions is not a list"]
    keys = []
    for f in functions:
        if not isinstance(f, dict) or set(f) != {"path", "address", "outcome"}:
            out.append("a functions entry is not an object with exactly path, address, outcome")
        elif (
            not research.image_path_ok(f["path"])
            or not isinstance(f["address"], str)
            or not ADDRESS.match(f["address"])
        ):
            out.append("a functions entry has a bad path or address")
        elif f["outcome"] not in PREFILTER:
            out.append(f"a functions entry's outcome is not one of {', '.join(PREFILTER)}")
        else:
            keys.append((f["path"], f["address"]))
    if keys != sorted(set(keys)):
        out.append("functions is not sorted by path then address, each listed once")
    if not out and (doc["outcome"] == "flag") != any(f["outcome"] == "flag" for f in functions):
        out.append("outcome is not flag exactly when a function is flagged")
    return out


def prefilter_mismatch(doc, result):
    """Why a well-formed pre-filter result does not belong to a well-formed verdict, as lines."""
    out = []
    if result["commit"] != doc["commit"]:
        out.append("the pre-filter result is for another commit")
    if result["outcome"] != doc["prefilter"]:
        out.append("the pre-filter result's outcome is not the verdict's prefilter")
    if {(f["path"], f["address"]) for f in result["functions"]} != {
        (f["path"], f["address"]) for f in doc["functions"]
    }:
        out.append("the pre-filter result judged other functions than the verdict lists")
    return out


def read_prefilter(path):
    """(result, problems) for a pre-filter result file; a file that cannot be read could not run (exit 2)."""
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise VerdictError(f"cannot read {path}: {e.__class__.__name__}") from None
    try:
        result = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        return None, ["the pre-filter result is not UTF-8 JSON"]
    return result, [f"the pre-filter result: {b}" for b in prefilter_problems(result)]


def sign(verdict, key, principal, out, prefilter, ssh_keygen=None):
    """(code, lines): writes the verdict in canonical form and the principal's signature over it into out. A verdict
    that is malformed or does not pass is refused (exit 1), so only a passing verdict is ever signed; so is one the
    pre-filter result (cleanroom prefilter --out) does not match: its commit, its outcome and the functions it
    judged must be the verdict's."""
    if not signing.PRINCIPAL.match(principal) or not principal.startswith(signing.PR_ANALYST):
        raise VerdictError(f"--principal must be {signing.PR_ANALYST}<n>")
    verdict, out = Path(verdict), Path(out)
    stem = verdict.name.removesuffix(".json")
    if not verdict.name.endswith(".json") or not NAME.match(stem):
        raise VerdictError("the verdict file must be named <name>.json (letters, digits, _ and -)")
    try:
        doc = json.loads(verdict.read_bytes().decode("utf-8"))
    except OSError as e:
        raise VerdictError(f"cannot read {verdict}: {e.__class__.__name__}") from None
    except (UnicodeDecodeError, json.JSONDecodeError):
        return 1, [f"FAIL {stem}: not UTF-8 JSON"]
    bad = problems(doc) or failing(doc)
    if not bad:
        result, bad = read_prefilter(prefilter)
        bad = bad or prefilter_mismatch(doc, result)
    if bad:
        return 1, [f"FAIL {stem}: {b}" for b in bad] + [f"{stem}: not signed: only a passing verdict is"]
    data = research.canonical(doc)
    exe = signing.program("ssh-keygen", ssh_keygen)
    sig = signing.sign(exe, key, data)
    if not out.is_dir():
        raise VerdictError(f"no folder {out}")
    target = out / f"{stem}.json"
    if target.exists():
        if target.read_bytes() != data:
            raise VerdictError(f"{target} exists with other contents")
    else:
        research.write_new(target, data)
    research.write_new(out / f"{stem}.{principal}.sig", sig)
    return 0, [f"signed {stem} as {principal}: {len(doc['files'])} files, {len(doc['functions'])} functions"]


# --- the inboxes (roles.PROVIDERS) ------------------------------------------------------------------------------

HANDOVER = "code-verdicts"  # under the workspace's from_analyst: the PR analyst's signed verdicts, for the public side
ENGINE_BRANCH = re.compile(r"^refs/heads/(impl-[0-9]+/engine/[A-Za-z0-9._/-]+)$")  # in the collecting outbox


def handover(ws):
    """The hand-over folder: <from_analyst>/code-verdicts, where the PR analyst's signed verdicts go."""
    return ws.where("from_analyst") / HANDOVER


def outbox(ws):
    """The collecting outbox, the bare repo under the workspace's containers folder the implementers' branches reach."""
    from coney_tools import impl

    return ws.where("containers").joinpath(*impl.COLLECTING)


def handed_over(ws):
    """{name: (bytes, doc)} of each well-formed verdict in the handover folder with a signature beside it. Whether the
    signature verifies is code-verdict's to say; a missing folder is an empty handover."""
    folder = handover(ws)
    try:
        names = sorted(p.name for p in folder.iterdir() if p.is_file())
    except FileNotFoundError:
        return {}
    signed = {m.group(1) for m in map(SIG.match, names) if m}
    out = {}
    for base in names:
        stem = base.removesuffix(".json")
        if base != stem and NAME.match(stem) and stem in signed:
            data = (folder / base).read_bytes()
            doc, bad = parse(data)
            if not bad:
                out[stem] = (data, doc)
    return out


def station(role, noun, items, states, needs):
    """An inbox station's document, shaped like research.status's: items are (id, state, short hash)."""
    counts = {s: 0 for s in states}
    for _, state, _ in items:
        counts[state] += 1
    return {
        "role": role,
        "noun": noun,
        "pages": [{"id": i, "state": s, "verdict": v} for i, s, v in items],
        "counts": counts,
        "action": [i for i, s, _ in items if s in needs],
    }


def analyst_status(ws):
    """The PR analyst's station: each engine branch in the collecting outbox (<impl-N>/engine/<name>), to-judge until
    a handed-over verdict names its head commit, then judged."""
    repo, items = outbox(ws), []
    if repo.is_dir():
        r = signing.git(signing.program("git"), repo, "for-each-ref", "--format=%(objectname) %(refname)")
        judged = {doc["commit"] for _, doc in handed_over(ws).values()}
        for line in r.stdout.decode().splitlines():
            commit, _, ref = line.partition(" ")
            m = ENGINE_BRANCH.match(ref)
            if m:
                items.append((m.group(1), "judged" if commit in judged else "to-judge", commit[:12]))
    return station("pr-analyst", "branches", sorted(items), ("to-judge", "judged"), ("to-judge",))


def public_status(ws, repo):
    """The engine coordinator's station: each handed-over verdict, new until a local branch holds its exact bytes at
    research/code-verdicts/<name>.json (copied), then merged once main does."""
    exe, verdicts = signing.program("git"), handed_over(ws)
    r = signing.git(exe, repo, "for-each-ref", "--format=%(refname)", "refs/heads/")
    refs = r.stdout.decode().split()
    found = research.read_objects(exe, repo, [f"{ref}:{FOLDER}/{n}.json" for n in verdicts for ref in refs])

    def holds(ref, name):
        blob = found.get(f"{ref}:{FOLDER}/{name}.json")
        return blob is not None and blob[1] == verdicts[name][0]

    items = []
    for name, (_, doc) in sorted(verdicts.items()):
        if holds(f"refs/heads/{research.MAIN}", name):
            state = "merged"
        elif any(holds(ref, name) for ref in refs):
            state = "copied"
        else:
            state = "new"
        items.append((name, state, doc["commit"][:12]))
    return station("engine-coordinator", "verdicts", items, ("new", "copied", "merged"), ("new",))
