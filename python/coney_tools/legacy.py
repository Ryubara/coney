"""The legacy-copy scan of `zones check --mode full`: exact copies of older Coney files in the analysis zone.

Research from before the current process reaches the analysis zone only through the research sweep's quarantine
folder, so that the sections the sweep turns down can be researched again by analysts who have not seen them. Any
other copy of an old file there is a failure of its own: not a breach of a wall, but named in the check's result as a
legacy copy, by folder.

What is scanned, and where the old-file hash list is, come from the workspace's `legacy_scan` table. The scan is the
clean-room scan (cleanroom/scan.py) with the hash list only: the analysis zone holds the original game's material
by design, so markers and near copies mean nothing there. It opens what files hold (archives, git packs, encoded
data) as that scan does. Folders listed under `skip` (the quarantine, and large folders that cannot hold older Coney
files) are not entered, and the result says which.

The old folders also held copies of the game's own files and of standard tool files, so two kinds of hash are taken
out of the list before the scan: every file in the folders listed under `original` (the zone's own copies of the
game's material: a file with the same bytes is the game's, not older Coney work), and every file on the optional
`allow` list (the starting proof's files reviewed and kept on purpose, such as git's sample hooks). Files larger than
ORIGINAL_MAX_BYTES in those folders (disc images) are not hashed.

Each file's verdict is cached by its path, size and modification time, so a run scans only new or changed files; a
run stops starting new files after `budget_seconds` and leaves the rest for the next run. The cache keys are
hashes, so the cache (in the machine's state folder, which every account reads) names no file. Paths and counts are
reported, never file contents.
"""

import hashlib
import json
import os
import time
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools.cleanroom import scan

CACHE_VERSION = 2
CHUNK_FILES = 1000  # files handed to the scan at once; the cache is saved after each such chunk
PARALLEL_FROM = 200  # uncached files from which the scan uses one worker per CPU (a pool costs about a second)
ORIGINAL_MAX_BYTES = 256 * 1024 * 1024  # larger files in the `original` folders (disc images) are not hashed


class LegacyError(Exception):
    """The hash list or the cache could not be used (the check fails; it is not a legacy copy)."""


@dataclass
class Result:
    root: str
    files: int = 0  # files under the root, outside the skipped folders
    scanned: int = 0  # files scanned in this run
    cached: int = 0  # files whose verdict came from the cache
    left: int = 0  # files left for the next run (budget spent)
    hits: dict = field(default_factory=dict)  # folder: exact copies under it (directly, or inside a file there)
    gaps: dict = field(default_factory=dict)  # folder: files whose insides could not all be opened
    unreadable: list = field(default_factory=list)  # folders the walk could not list
    links: int = 0  # links and junctions not followed
    skipped: list = field(default_factory=list)  # (folder, why)
    seconds: float = 0.0


def _norm(p):
    return os.path.normcase(os.path.abspath(scan.shown_path(p)))


def file_key(path, st):
    """A file's cache key: its path, size and modification time, hashed so that the cache names no file."""
    text = f"{_norm(path)}\0{st.st_size}\0{st.st_mtime_ns}"
    return hashlib.sha256(text.encode("utf-8", "surrogatepass")).hexdigest()[:32]


def walk(root, skip):
    """(files [(path, key)], unreadable folders, links not followed) under root, outside the `skip` folders (normed
    paths). Links and junctions are not followed: what they point to is scanned where it is, or not at all."""
    files, unreadable, links = [], [], 0
    todo = [str(root)]
    while todo:
        top = todo.pop()
        try:
            with os.scandir(scan.long_path(top)) as it:
                entries = sorted(it, key=lambda e: e.name)
        except OSError:
            unreadable.append(top)
            continue
        dirs = []
        for e in entries:
            path = os.path.join(top, e.name)
            try:
                if e.is_symlink() or (e.is_dir(follow_symlinks=False) and scan.is_link(e.path)):
                    links += 1
                elif e.is_dir(follow_symlinks=False):
                    if _norm(path) not in skip:
                        dirs.append(path)
                elif e.is_file(follow_symlinks=False):
                    files.append((path, file_key(path, e.stat(follow_symlinks=False))))
            except OSError:
                unreadable.append(path)
        todo.extend(reversed(dirs))
    return files, unreadable, links


def load_hashes(path):
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise LegacyError(f"the old-file hash list {path} is unreadable: {e.__class__.__name__}") from None
    try:
        return scan.load_hash_list(data, "hashes"), hashlib.sha256(data).hexdigest()
    except scan.DataError as e:
        raise LegacyError(f"the old-file hash list {path}: {e}") from None


def load_allowed(path):
    """The hashes on the allow list (the clean-room scan's allow.json)."""
    try:
        data = Path(path).read_bytes()
    except OSError as e:
        raise LegacyError(f"the allow list {path} is unreadable: {e.__class__.__name__}") from None
    try:
        return set(scan.load_allow(data))
    except scan.DataError as e:
        raise LegacyError(f"the allow list {path}: {e}") from None


def read_doc(path):
    """The cache document, or {} when it is missing, unreadable or of another cache version."""
    try:
        doc = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return {}
    return doc if isinstance(doc, dict) and doc.get("version") == CACHE_VERSION else {}


def read_cache(doc, stamp):
    """{key: verdict} from the cache document, or {} when it is from another hash list or scanner."""
    files = doc.get("files") if doc.get("stamp") == stamp else None
    return files if isinstance(files, dict) else {}


def write_cache(path, stamp, files, originals):
    from coney_tools.zones import write_atomic

    doc = {"version": CACHE_VERSION, "stamp": stamp, "files": files, "originals": originals}
    write_atomic(path, json.dumps(doc) + "\n")


def original_hashes(folders, old):
    """({sha256 of every file in the `original` folders}, {key: sha256} to cache). `old` is the cached {key: sha256};
    a file is hashed again only when its path, size or modification time changed."""
    hashes, cache = set(), {}
    for folder in folders:
        files, _unreadable, _links = walk(folder, set())
        for path, key in files:
            sha = old.get(key)
            if not isinstance(sha, str):
                try:
                    if os.path.getsize(scan.long_path(path)) > ORIGINAL_MAX_BYTES:
                        continue
                    h = hashlib.sha256()
                    with open(scan.long_path(path), "rb") as f:
                        for block in iter(lambda: f.read(1 << 20), b""):
                            h.update(block)
                    sha = h.hexdigest()
                except OSError:
                    continue
            cache[key] = sha
            hashes.add(sha)
    return hashes, cache


def owner(shown, files):
    """The scanned file a finding's path belongs to: the path itself, or the file a member or stream is inside
    (`file!member`, `file:stream`)."""
    n = os.path.normcase(shown)
    if n in files:
        return files[n]
    for i, c in enumerate(n):
        if c in "!:" and i > 2 and n[:i] in files:
            return files[n[:i]]
    return None


def scan_chunk(paths, hashes, jobs):
    """{normed path: [exact copies, gaps]} for the files, through the clean-room scan with the hash list only."""
    s = scan.Scan([], hashes, set(), {}, "", {"jobs": jobs, "file_timeout": scan.FILE_TIMEOUT if jobs > 1 else 0}).run(
        paths
    )
    by = {_norm(p): _norm(p) for p in paths}
    out = {k: [0, 0] for k in by}
    for shown, _kind, _count in s.hits:
        k = owner(shown, by)
        if k:
            out[k][0] += 1
    for shown, why in s.gaps:
        k = owner(shown, by)
        if k and why != "missing root":  # a file that went away since the walk
            out[k][1] += 1
    return out


def run(cfg, resolve, jobs=None, clock=time.monotonic):
    """Scans the analysis zone as `cfg` (the workspace's legacy_scan table) says; `resolve` turns a workspace path
    into a folder. Raises LegacyError when the hash list cannot be used."""
    start = clock()
    root = resolve(cfg["root"])
    res = Result(str(root))
    skip = {}
    for s in cfg["skip"]:
        p = resolve(s["path"])
        skip[_norm(p)] = s["why"]
        res.skipped.append((os.path.relpath(p, root) if _norm(p).startswith(_norm(root)) else str(p), s["why"]))
    hashes, _hsha = load_hashes(resolve(cfg["hashes"]))
    cache_path = resolve(cfg["cache"])
    doc = read_doc(cache_path)
    if not os.path.isdir(root):
        res.seconds = clock() - start
        return res
    # The game's own files and the allowed files are not older Coney work, whatever the old folders held
    old_orig = doc.get("originals") if isinstance(doc.get("originals"), dict) else {}
    game, originals = original_hashes([resolve(o["path"]) for o in cfg.get("original", [])], old_orig)
    allowed = load_allowed(resolve(cfg["allow"])) if cfg.get("allow") else set()
    hashes = hashes - game - allowed
    stamp = hashlib.sha256(("\n".join(sorted(hashes)) + "\0" + scan.scanner_sha256()).encode()).hexdigest()
    files, res.unreadable, res.links = walk(root, set(skip))
    res.files = len(files)
    old = read_cache(doc, stamp)
    new = {}
    todo = []
    for path, key in files:
        if key in old:
            new[key] = old[key]
            res.cached += 1
        else:
            todo.append((path, key))
    jobs = jobs or (os.cpu_count() or 1)
    deadline = start + cfg["budget_seconds"]
    for i in range(0, len(todo), CHUNK_FILES):
        if clock() >= deadline:
            res.left = len(todo) - i
            break
        chunk = todo[i : i + CHUNK_FILES]
        got = scan_chunk([p for p, _ in chunk], hashes, jobs if len(chunk) >= PARALLEL_FROM else 1)
        for path, key in chunk:
            hits, gaps = got.get(_norm(path), (0, 0))
            rel = os.path.relpath(os.path.dirname(path), root)
            new[key] = [hits, gaps, rel] if hits or gaps else 0
        res.scanned += len(chunk)
        write_cache(cache_path, stamp, new, originals)
    if not todo and new.keys() != old.keys():
        write_cache(cache_path, stamp, new, originals)  # files removed since the last run
    for v in new.values():
        if isinstance(v, list) and len(v) == 3:
            folder = os.path.normpath(os.path.join(root, v[2]))
            if v[0]:
                res.hits[folder] = res.hits.get(folder, 0) + v[0]
            if v[1]:
                res.gaps[folder] = res.gaps.get(folder, 0) + 1
    res.seconds = clock() - start
    return res
