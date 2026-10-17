"""The code gate's scripted pre-filter (clean-room page, section 7): run by the PR analyst on the analysis side before
judging the `@orig`-tagged engine functions a change adds or modifies. Its outcome is a verdict's `prefilter` field,
and `code-verdict-sign` signs a verdict only beside the result this command writes.

Like `code-verdict`, it reads everything from git, never the working tree: the engine source files the range A..B
changes (from the merge base of its ends), every engine source file at B and each linked page at B. Its settings are
`[tool.coney-tools.code-gate]` in python/pyproject.toml at B, not at main: the analyst runs it in the collecting
outbox, whose main may be behind.

The facts file (`coney-prefilter-facts/1`) is the analyst's, from their own tools, and never leaves the analysis zone:
for each judged address, the original function's callee addresses in call order and its count of conditional
branches. Each tagged function is checked against three rules (RULES). The output names rules, paths and the judged
addresses only: never a constant's value, a callee address or a branch count.
"""

import json
import re
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath

from coney_tools import codegate, codeverdict, research, signing

FACTS_SCHEMA = "coney-prefilter-facts/1"
RESULT_SCHEMA = codeverdict.PREFILTER_SCHEMA
ADDRESS = codeverdict.ADDRESS
MAX_FACTS = 16 << 20  # bytes; a larger facts file is not read
RULES = [
    ("constant-not-on-page", "a numeric literal in the body whose value is not on the function's linked page"),
    ("same-call-order", "the calls to tagged functions, in order, are the original's tagged callees"),
    ("same-branch-count", "the body's branch points number the original's conditional branches"),
]

# A C++ number as the preprocessor reads it (digits, letters, dots, digit separators and signed exponents).
LITERAL = re.compile(r"(?<![\w.])(?:\d|\.\d)(?:[eEpP][+-]|[\w.']|)*")
INT_SUFFIX = r"[ulz]*"
FLOAT_SUFFIX = r"(?:f16|f32|f64|f128|bf16|f|l)?"
# A number as a page writes it: hex, decimal with or without thousands commas, with an optional sign.
PAGE_NUMBER = re.compile(r"(?<![\w.])([-−]?)(0[xX][0-9a-fA-F]+|\d{1,3}(?:,\d{3})+(?:\.\d+)?|\d+(?:\.\d+)?)")
CALL = re.compile(r"(?<!\w)((?:[A-Za-z_]\w*\s*::\s*)*~?[A-Za-z_]\w*)\s*(?:<[^<>;{}()]*>\s*)?\(")
CASTS = {"static_cast", "const_cast", "reinterpret_cast", "dynamic_cast"}
NOT_A_CALL = codegate.NOT_A_NAME | CASTS | {"new", "delete", "else", "case", "co_yield"}
BRANCH = re.compile(r"\b(?:if|case|for|while|do|catch)\b|\bdefault\s*:(?!:)|\?|&&|\|\|")
UNARY_BEFORE = re.compile(r"(?:^|[=(,\[{:;?<>+\-*/%&|^!~]|\breturn|\bcase|\bco_return)$")


class PrefilterError(codeverdict.VerdictError):
    """The command could not run (exit 2)."""


# --- the facts --------------------------------------------------------------------------------------------------


def read_facts(path):
    """{address: (callees, branches)} from the analyst's facts file, strictly checked. Problems name the file's
    shape only, never a value from it."""
    try:
        with open(path, "rb") as f:
            data = f.read(MAX_FACTS + 1)
    except OSError as e:
        raise PrefilterError(f"cannot read the facts file: {e.__class__.__name__}") from None
    if len(data) > MAX_FACTS:
        raise PrefilterError("the facts file is larger than 16 MB")
    try:
        doc = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise PrefilterError("the facts file is not UTF-8 JSON") from None

    def bad(why):
        return PrefilterError(f"the facts file is malformed: {why}")

    if not isinstance(doc, dict) or set(doc) != {"schema", "functions"}:
        raise bad("not an object with exactly functions, schema")
    if doc["schema"] != FACTS_SCHEMA:
        raise bad(f"schema is not {FACTS_SCHEMA}")
    if not isinstance(doc["functions"], dict):
        raise bad("functions is not an object")
    out = {}
    for address, entry in doc["functions"].items():
        if not ADDRESS.match(address):
            raise bad("a functions key is not 0x and 8 lower-case hex digits")
        if not isinstance(entry, dict) or set(entry) != {"callees", "branches"}:
            raise bad("a functions entry is not an object with exactly branches, callees")
        callees, branches = entry["callees"], entry["branches"]
        if not isinstance(callees, list) or not all(isinstance(c, str) and ADDRESS.match(c) for c in callees):
            raise bad("a callees entry is not a list of addresses (0x and 8 lower-case hex digits)")
        if not isinstance(branches, int) or isinstance(branches, bool) or branches < 0:
            raise bad("a branches entry is not a whole number of 0 or more")
        out[address] = (tuple(callees), branches)
    return out


# --- reading numbers --------------------------------------------------------------------------------------------


def literal_value(token):
    """The value of a C++ numeric literal (separators, suffixes and user-defined suffixes taken off), or None."""
    t = re.sub(r"_[a-z_]\w*$", "", token.replace("'", "").lower())
    try:
        if t.startswith("0x"):
            m = re.match(rf"^(0x[0-9a-f]*\.?[0-9a-f]*p[+-]?\d+){FLOAT_SUFFIX}$", t)
            if m:
                return float.fromhex(m.group(1))
            m = re.match(rf"^0x([0-9a-f]+){INT_SUFFIX}$", t)
            return int(m.group(1), 16) if m else None
        m = re.match(rf"^0b([01]+){INT_SUFFIX}$", t)
        if m:
            return int(m.group(1), 2)
        m = re.match(rf"^(\d+){INT_SUFFIX}$", t)
        if m:
            digits = m.group(1)
            return int(digits, 8) if len(digits) > 1 and digits[0] == "0" else int(digits)
        m = re.match(rf"^((?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?){FLOAT_SUFFIX}$", t)
        return float(m.group(1)) if m else None
    except ValueError:
        return None


def literals(code, start, end):
    """[value or None] for every numeric literal in code[start:end], negative when a unary minus stands before it."""
    out = []
    for m in LITERAL.finditer(code, start, end):
        value = literal_value(m.group(0).rstrip("'"))
        before = code[start : m.start()].rstrip()
        if value is not None and before.endswith("-") and not before.endswith("--"):
            prev = before[:-1].rstrip()
            if UNARY_BEFORE.search(prev) and not prev.endswith(("--", "++")):  # not after x-- or x++
                value = -value
        out.append(value)
    return out


def page_values(text):
    """Every number a page writes, as values: hex and decimal alike, with and without its sign; a number with
    thousands commas also counts as each of its comma-separated parts."""
    out = set()
    for m in PAGE_NUMBER.finditer(text):
        sign, digits = m.group(1), m.group(2)
        if digits[:2].lower() == "0x":
            values = [int(digits, 16)]
        else:
            values = [float(digits.replace(",", "")) if "." in digits else int(digits.replace(",", ""))]
            if "," in digits:
                values += [int(p) for p in digits.split(".")[0].split(",")]
        for v in values:
            out.update((v, -v) if sign else (v,))
            out.add(abs(v))
    return out


# --- the engine at B --------------------------------------------------------------------------------------------


@dataclass
class Source:
    path: str
    text: str
    code: str
    tags: list  # [(Tag, Function or None)]
    fns: list = field(default_factory=list)


def read_source(path, data):
    """The source's text, lexed code, functions and tags, or None when it is not UTF-8 text or is too large."""
    if data is None or len(data) > codegate.MAX_SOURCE:
        return None
    try:
        text = data.decode("utf-8-sig").replace("\r\n", "\n").replace("\r", "\n")
    except UnicodeDecodeError:
        return None
    code, comments = codegate.lex(text)
    fns = codegate.functions(code)
    tags = codegate.read_tags(path, text, comments, [])
    return Source(path, text, code, [(t, codegate.owner_of(fns, t)) for t in tags if t.address], fns)


def engine_sources(exe, repo, commit, s):
    """{path: Source or None} for every source file under the engine root at the commit."""
    r = signing.git(exe, repo, "ls-tree", "-r", "-z", "--full-tree", commit, "--", s.engine_root)
    blobs = {}
    for entry in r.stdout.decode("utf-8", "surrogateescape").split("\0"):
        head, _, path = entry.partition("\t")
        fields = head.split()
        if len(fields) == 3 and fields[1] == "blob" and PurePosixPath(path).suffix.lower() in s.extensions:
            blobs[path] = fields[2]
    objects = research.read_objects(exe, repo, sorted(set(blobs.values())))
    return {p: read_source(p, (objects.get(b) or (None, None))[1]) for p, b in sorted(blobs.items())}


class Index:
    """Every function defined in the engine at B, by its last name part: (its name as defined, its @orig address or
    None), so a call can be tied to the one tagged function it names."""

    def __init__(self, sources):
        self.by_name, self.tagged = {}, set()
        for src in sources.values():
            if src is None:
                continue
            owners = {id(f): t.address for t, f in src.tags if f is not None}
            for f in src.fns:
                name = codegate.name_of(src.code, f)
                if name:
                    self.by_name.setdefault(name.split("::")[-1], set()).add((name, owners.get(id(f))))
            self.tagged.update(t.address for t, _ in src.tags)

    def resolve(self, call):
        """The tagged address a call names, or None when it names an untagged function, or more than one."""
        candidates = {
            address
            for name, address in self.by_name.get(call.split("::")[-1], ())
            if "::" not in call
            or "::" not in name
            or name.endswith("::" + call)
            or call.endswith("::" + name)
            or name == call
        }
        return next(iter(candidates)) if len(candidates) == 1 else None


def calls(code, start, end, index):
    """The tagged addresses the code between start and end calls, ordered by where each call's argument list closes
    (so an argument's call comes before the call it is passed to)."""
    found = []
    for m in CALL.finditer(code, start, end):
        name = re.sub(r"\s+", "", m.group(1))
        if name.split("::")[-1] in NOT_A_CALL:
            continue
        address = index.resolve(name)
        if address is not None:
            found.append((codegate.group_end(code, m.end() - 1, "(", ")"), address))
    return [a for _, a in sorted(found)]


def branches(code, start, end):
    return sum(1 for _ in BRANCH.finditer(code, start, end))


# --- the command ------------------------------------------------------------------------------------------------


def judge(src, tag, owner, facts, index, page, s):
    """The rules one tagged function breaks."""
    start, end = owner.body, codegate.group_end(src.code, owner.body, "{", "}")
    rules = []
    trivial = set(s.trivial)
    values = literals(src.code, start, end)
    if any(v is None or (v not in trivial and v not in page and abs(v) not in page) for v in values):
        rules.append("constant-not-on-page")
    callees, count = facts[tag.address]
    ours = calls(src.code, start, end, index)
    theirs = [c for c in callees if c in index.tagged]
    if len(ours) >= s.min_calls and ours == theirs:
        rules.append("same-call-order")
    n = branches(src.code, start, end)
    if n >= s.min_branches and n == count:
        rules.append("same-branch-count")
    return rules


def prefilter(rng, facts_file, out=None, repo=None, as_json=False):
    """(code, lines): every @orig-tagged function in the engine source files the range changes, against the facts."""
    repo = Path(repo) if repo else research.own_checkout()
    if out is not None and Path(out).exists():
        raise PrefilterError(f"{out} exists already; the result is never overwritten")
    facts = read_facts(facts_file)
    exe = signing.program("git")
    start, end = codeverdict.ends(exe, repo, rng)
    s = codegate.settings(exe, repo, end)
    changed = codeverdict.changed(exe, repo, start, end, s.engine_root)
    changed = {p for p in changed if PurePosixPath(p).suffix.lower() in s.extensions}
    sources = engine_sources(exe, repo, end, s)
    index = Index(sources)
    judged, links = [], set()
    for path in sorted(changed):
        src = sources.get(path)
        if src is None:
            raise PrefilterError(f"{path} is not UTF-8 text, or is larger than 4 MB")
        for tag, owner in src.tags:
            if owner is None:
                raise PrefilterError(f"{path}: {tag.address} is not just above a function (see orig-check)")
            if not tag.link or not codegate.LINK_PATH.match(tag.link):
                raise PrefilterError(f"{path}: {tag.address} has no link to {codegate.page_link('<id>')}")
            if tag.address not in facts:
                raise PrefilterError(f"the facts file has no entry for {tag.address}")
            judged.append((src, tag, owner))
            links.add(tag.link)
    pages = research.read_objects(exe, repo, [f"{end}:{link}" for link in sorted(links)])
    page_text = {}
    for link in links:
        obj = pages.get(f"{end}:{link}")
        if obj is None or obj[0] != "blob":
            raise PrefilterError(f"{link} is not in {rng.partition('..')[2]}")
        page_text[link] = page_values(obj[1].decode("utf-8", "replace"))
    results = {}
    for src, tag, owner in judged:
        rules = judge(src, tag, owner, facts, index, page_text[tag.link], s)
        key = (src.path, tag.address)
        results[key] = sorted(set(results.get(key, [])) | set(rules), key=[r for r, _ in RULES].index)
    flagged = sum(1 for r in results.values() if r)
    outcome = "flag" if flagged else "pass"
    doc = {
        "schema": RESULT_SCHEMA,
        "commit": end,
        "outcome": outcome,
        "functions": [
            {"path": p, "address": a, "outcome": "flag" if r else "pass"} for (p, a), r in sorted(results.items())
        ],
    }
    if out is not None:
        research.write_new(Path(out), research.canonical(doc))
    code = 1 if flagged else 0
    if as_json:
        report = dict(doc, range=rng, rules={f"{p} {a}": r for (p, a), r in sorted(results.items())})
        return code, [json.dumps(report, indent=2)]
    lines = [f"PASS {p} {a}" if not r else f"FLAG {p} {a}: {', '.join(r)}" for (p, a), r in sorted(results.items())]
    lines.append(
        f"{outcome.upper()}: {len(results)} tagged functions in {len(changed)} engine source files changed in {rng}, "
        f"{flagged} flagged; commit {end}"
    )
    return code, lines
