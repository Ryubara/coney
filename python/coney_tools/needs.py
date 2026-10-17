"""Research requests: how an implementer's question becomes a request to the analysis side, and how its answer comes
back (docs/workflow.md).

An implementer writes what no page answers into `/work/artifacts/needs.json` (`coney-needs/1`), and `impl collect`
copies it to the public zone and reports whether it is well formed. The engine coordinator files the questions it
accepts with `request file`: each becomes `<to_analyst>/requests/<request id>.json` (`coney-research-request/1`). The
research coordinator answers with `request status`, one file per state change in `<from_analyst>/requests/`
(`coney-research-status/1`): accepted, then answered (naming the page that answers it) or declined. Status files hold
ids, states and times only, never free text, since they cross from the analysis side.

Both folders are add-only: nothing is ever overwritten. Every file is research.canonical's bytes. The tool checks the
shape of a question; whether it asks for behaviour only, never for the original's code, is the engine coordinator's
to judge before filing it.
"""

import hashlib
import json
import re
import time
from pathlib import Path

from coney_tools import research, signing

NEEDS = "coney-needs/1"
REQUEST = "coney-research-request/1"
STATUS = "coney-research-status/1"
NEEDS_FILE = "needs.json"  # in an implementer's artifacts folder
FOLDER = "requests"  # under the workspace's to_analyst (requests) and from_analyst (their states)
NEEDS_KEYS = {"schema", "questions"}
QUESTION_KEYS = {"id", "page", "topic", "question"}
REQUEST_KEYS = {"schema", "id", "task", "page", "topic", "question", "filed"}
STATUS_KEYS = {"schema", "request", "state", "page", "time"}
QUESTION_ID = re.compile(r"^[a-z0-9][a-z0-9-]{0,47}$")
REQUEST_ID = re.compile(r"^([a-z0-9][a-z0-9-]{0,47})-([0-9a-f]{8})$")
TASK = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")  # a task file's name
TIME = re.compile(r"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$")
MAX_TOPIC, MAX_QUESTION = 80, 600  # characters
MAX_FILE = 256 << 10  # bytes; a larger needs, request or status file is not read
STATES = ("accepted", "answered", "declined")  # what `request status` writes
RANK = {"new": 0, "accepted": 1, "answered": 2, "declined": 2}  # a state change only moves up


class RequestError(research.GateError):
    """The command could not run (exit 2)."""


def now():
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def _strict(pairs):
    keys = [k for k, _ in pairs]
    if len(keys) != len(set(keys)):
        raise ValueError("a key given twice")
    return dict(pairs)


def parse(data):
    """(doc, problems): the JSON in a file's bytes, or why it is not UTF-8 JSON (a key given twice is not)."""
    if len(data) > MAX_FILE:
        return None, [f"larger than {MAX_FILE} bytes"]
    try:
        return json.loads(data.decode("utf-8"), object_pairs_hook=_strict), []
    except (UnicodeDecodeError, ValueError):
        return None, ["not UTF-8 JSON with each key once"]


def _text(value, limit):
    """Why a topic or question is not one line of printable text up to the limit, or None."""
    if not isinstance(value, str) or not value.strip():
        return "is not text"
    if len(value) > limit:
        return f"is longer than {limit} characters"
    if not value.isprintable():
        return "is not one line of printable text"
    return None


def question_problems(q, where, pages=None):
    """Why one question is malformed, as lines naming where it is (never its text). pages: the page ids that exist,
    or None to check the form of a page id only."""
    if not isinstance(q, dict) or set(q) != QUESTION_KEYS:
        return [f"{where}: not an object with exactly {', '.join(sorted(QUESTION_KEYS))}"]
    out = []
    if not isinstance(q["id"], str) or not QUESTION_ID.match(q["id"]):
        out.append(f"{where}: id is not a slug (a-z, 0-9 and -, up to 48 characters)")
    page = q["page"]
    if page is not None and (not isinstance(page, str) or not research.PAGE_ID.match(page)):
        out.append(f"{where}: page is not a page id or null")
    elif page is not None and pages is not None and page not in pages:
        out.append(f"{where}: page names no research page on {research.MAIN}")
    for key, limit in (("topic", MAX_TOPIC), ("question", MAX_QUESTION)):
        bad = _text(q[key], limit)
        if bad:
            out.append(f"{where}: {key} {bad}")
    return out


def needs_problems(doc, pages=None):
    """Why a parsed needs file is malformed, as lines; empty when it is well formed."""
    if not isinstance(doc, dict) or set(doc) != NEEDS_KEYS:
        return [f"not an object with exactly {', '.join(sorted(NEEDS_KEYS))}"]
    out = [] if doc["schema"] == NEEDS else [f"schema is not {NEEDS}"]
    if not isinstance(doc["questions"], list):
        return [*out, "questions is not a list"]
    ids = []
    for n, q in enumerate(doc["questions"]):
        found = question_problems(q, f"questions[{n}]", pages)
        out += found
        if not found:
            ids.append(q["id"])
    if len(ids) != len(set(ids)):
        out.append("an id is given to two questions")
    return out


def read_needs(path, pages=None):
    """(doc, problems) for a needs file; one that cannot be read could not run (exit 2)."""
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise RequestError(f"cannot read {path}: {e.__class__.__name__}") from None
    doc, bad = parse(data)
    return doc, bad or needs_problems(doc, pages)


def pages_on_main(repo):
    """The research page ids main holds, from git, never the working tree."""
    main = f"refs/heads/{research.MAIN}"
    r = signing.git(signing.program("git"), repo, "ls-tree", "-d", "--name-only", main, "research/pages/")
    return {line.rsplit("/", 1)[-1] for line in r.stdout.decode().splitlines() if line.strip()}


def collect_line(name, folder, pages=None):
    """impl collect's report on one implementer's needs file, in its copied artifacts folder."""
    path = Path(folder) / NEEDS_FILE
    if not path.is_file():
        return f"{name}: no {NEEDS_FILE}"
    try:
        doc, bad = read_needs(path, pages)
    except RequestError as e:
        return f"{name}: {NEEDS_FILE} unreadable: {e}"
    if doc == []:  # the form task files asked for before the schema: a bare empty list, no questions
        return f"{name}: needs: 0 questions"
    if bad:
        return f"{name}: {NEEDS_FILE} malformed: {'; '.join(bad)}"
    return f"{name}: needs: {len(doc['questions'])} questions"


# --- one request and one status ----------------------------------------------------------------------------------


def request_id(q):
    """<question id>-<first 8 hex of the SHA-256 of the canonical question>: the same question, the same request."""
    data = research.canonical({k: q[k] for k in QUESTION_KEYS})
    return f"{q['id']}-{hashlib.sha256(data).hexdigest()[:8]}"


def request_problems(doc):
    """Why a parsed request is malformed, as lines; empty when it is well formed."""
    if not isinstance(doc, dict) or set(doc) != REQUEST_KEYS:
        return [f"not an object with exactly {', '.join(sorted(REQUEST_KEYS))}"]
    out = [] if doc["schema"] == REQUEST else [f"schema is not {REQUEST}"]
    if doc["task"] is not None and (not isinstance(doc["task"], str) or not TASK.match(doc["task"])):
        out.append("task is not a task file's name or null")
    if not isinstance(doc["filed"], str) or not TIME.match(doc["filed"]):
        out.append("filed is not a UTC time")
    m = REQUEST_ID.match(doc["id"]) if isinstance(doc["id"], str) else None
    if not m:
        return [*out, "id is not a request id"]
    q = {**{k: doc[k] for k in QUESTION_KEYS}, "id": m.group(1)}  # the question's own id, without the hash
    out += question_problems(q, "the question")
    if not out and doc["id"] != request_id(q):
        out.append("id is not the question's request id")
    return out


def status_problems(doc):
    """Why a parsed status is malformed, as lines; empty when it is well formed."""
    if not isinstance(doc, dict) or set(doc) != STATUS_KEYS:
        return [f"not an object with exactly {', '.join(sorted(STATUS_KEYS))}"]
    out = [] if doc["schema"] == STATUS else [f"schema is not {STATUS}"]
    if not isinstance(doc["request"], str) or not REQUEST_ID.match(doc["request"]):
        out.append("request is not a request id")
    if doc["state"] not in STATES:
        out.append(f"state is not one of {', '.join(STATES)}")
    page = doc["page"]
    if doc["state"] == "answered" and (not isinstance(page, str) or not research.PAGE_ID.match(page)):
        out.append("an answered request names no page id")
    if doc["state"] != "answered" and page is not None:
        out.append("only an answered request names a page")
    if not isinstance(doc["time"], str) or not TIME.match(doc["time"]):
        out.append("time is not a UTC time")
    return out


def _read(path, problems):
    """The document in a request or status file when it is well formed and canonical, else None."""
    try:
        data = path.read_bytes()
    except OSError:
        return None
    doc, bad = parse(data)
    if bad or problems(doc) or research.canonical(doc) != data:
        return None
    return doc


def folders(ws):
    """(<to_analyst>/requests, <from_analyst>/requests). A missing to_analyst or from_analyst cannot run."""
    out = []
    for key in ("to_analyst", "from_analyst"):
        base = ws.where(key)
        if not base.is_dir():
            raise RequestError(f"no {key} folder at {base}")
        out.append(base / FOLDER)
    return tuple(out)


def request_of(requests, rid):
    """The well-formed request filed as rid, or None."""
    if not REQUEST_ID.match(rid):
        return None
    doc = _read(requests / f"{rid}.json", request_problems)
    return doc if doc and doc["id"] == rid else None


def state_of(answers, rid):
    """(state, page) of a request, from its status files, looked up by name only (never by listing the folder): the
    highest state with a well-formed status file, answered before declined; new when there is none."""
    best = ("new", None)
    for state in STATES:
        doc = _read(answers / f"{rid}.{state}.json", status_problems)
        if doc and doc["request"] == rid and doc["state"] == state and RANK[state] > RANK[best[0]]:
            best = (state, doc["page"])
    return best


# --- the commands ------------------------------------------------------------------------------------------------


def _mkdir(folder):
    try:
        folder.mkdir(exist_ok=True)
    except OSError as e:
        raise RequestError(f"cannot make {folder}: {e.__class__.__name__}") from None


def file(ws, needs, ids=(), task=None, repo=None):
    """(code, lines): writes each chosen question of the needs file as a request into <to_analyst>/requests/. A
    malformed needs file, or an --id it does not hold, is refused (exit 1) and nothing is filed; a request filed
    before is left as it is."""
    if task is not None and not TASK.match(task):
        raise RequestError("--task must be a task file's name (letters, digits, . _ and -), not a path")
    requests, _ = folders(ws)
    pages = pages_on_main(Path(repo) if repo else ws.where("repo"))
    doc, bad = read_needs(needs, pages)
    if bad:
        return 1, [f"FAIL {b}" for b in bad] + ["nothing filed: the needs file is malformed"]
    questions = {q["id"]: q for q in doc["questions"]}
    unknown = [i for i in ids if i not in questions]
    if unknown:
        return 1, [f"FAIL --id {i}: no such question in the needs file" for i in unknown] + ["nothing filed"]
    chosen = [questions[i] for i in dict.fromkeys(ids)] if ids else list(questions.values())
    _mkdir(requests)
    lines, filed = [], 0
    for q in chosen:
        rid = request_id(q)
        target = requests / f"{rid}.json"
        if target.exists():
            lines.append(f"{rid}: filed before; left as it is")
            continue
        request = {**{k: q[k] for k in QUESTION_KEYS}, "schema": REQUEST, "id": rid, "task": task, "filed": now()}
        research.write_new(target, research.canonical(request))
        lines.append(f"{rid}: filed")
        filed += 1
    lines.append(f"filed {filed} of {len(chosen)} questions into {requests}")
    return 0, lines


def set_status(ws, rid, state, page=None):
    """(code, lines): writes <from_analyst>/requests/<rid>.<state>.json. An unknown request, or a state that does not
    move it on (new, accepted, then answered or declined), is refused (exit 1); the same state with the same page
    again is a no-op."""
    if state not in STATES:
        raise RequestError(f"--state must be one of {', '.join(STATES)}")
    if (state == "answered") != (page is not None):
        raise RequestError("--page is given with --state answered, and only then")
    if page is not None and not research.PAGE_ID.match(page):
        raise RequestError("--page must be a page id (a-z, 0-9 and -)")
    requests, answers = folders(ws)
    if request_of(requests, rid) is None:
        return 1, [f"FAIL {rid}: no such request in {requests}"]
    current, current_page = state_of(answers, rid)
    if current == state and current_page == page:
        return 0, [f"{rid}: {state} already; nothing written"]
    if RANK[state] <= RANK[current]:
        return 1, [f"FAIL {rid}: it is {current}; {state} does not move it on"]
    _mkdir(answers)
    doc = {"schema": STATUS, "request": rid, "state": state, "page": page, "time": now()}
    research.write_new(answers / f"{rid}.{state}.json", research.canonical(doc))
    return 0, [f"{rid}: {state}" + (f" by {page}" if page else "")]


# --- the inboxes (roles.PROVIDERS) -------------------------------------------------------------------------------

ANALYST_STATES = ("new", "accepted", "answered", "declined", "invalid")
PUBLIC_STATES = ("open", "answered", "declined", "closed", "invalid")


def filed(ws):
    """[(request id, state, page)] for each request file in <to_analyst>/requests/, its state read from the status
    files by name; invalid for a file there that is not a well-formed request named by its id."""
    requests, answers = folders(ws)
    try:
        names = sorted(p.name for p in requests.iterdir() if p.is_file())
    except FileNotFoundError:
        return []
    except OSError as e:
        raise RequestError(f"cannot read {requests}: {e.__class__.__name__}") from None
    out = []
    for name in names:
        rid = name.removesuffix(".json")
        if name == rid or request_of(requests, rid) is None:
            out.append((name, "invalid", None))
        else:
            out.append((rid, *state_of(answers, rid)))
    return out


def analyst_status(ws):
    """The research coordinator's station: each request, new until accepted, then answered or declined."""
    items = list(filed(ws))
    return research.station("research-coordinator", "requests", items, ANALYST_STATES, ("new", "accepted"))


def public_status(ws, repo):
    """The engine coordinator's station: each request open (new or accepted), answered, declined, or closed once
    main holds the answering page at research/pages/<page>/."""
    items, merged = [], None
    for rid, state, page in filed(ws):
        if state in ("new", "accepted"):
            state = "open"
        elif state == "answered":
            merged = pages_on_main(repo) if merged is None else merged
            state = "closed" if page in merged else state
        items.append((rid, state, page))
    return research.station("engine-coordinator", "requests", items, PUBLIC_STATES, ("answered", "declined"))
