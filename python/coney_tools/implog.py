"""Implementer sessions made readable: the live log `impl run` writes while a session works, the Markdown transcripts
`impl collect` writes after it, and `impl log`, which any zone allowed to read the live folder uses to show them.

A session's events (Claude Code's stream-json output while it runs, its transcript JSONL after) become short lines:
a timestamp, a kind (`text`, the tool's name, `result`, `done`) and a one-line summary. Implementers see only public
material (the task file, gated pages, their own code), so their logs may be shown in every zone.

The live folder holds, per implementer, `<impl-N>.log` (the current or last session, one line per event, written as
they come) and `<impl-N>.json` (task, branch, started, ended, exit code).
"""

import json
import os
import time
from pathlib import Path

LIVE = "live"  # under the workspace's containers folder
TRANSCRIPTS = "transcripts"  # under each implementer's archive folder
WIDTH = 200  # characters of one summary
TAIL = 200  # lines `impl log` gives by default


def clock(stamp=None):
    """HH:MM:SS (UTC) of an ISO timestamp, or of now."""
    if isinstance(stamp, str) and len(stamp) >= 19 and stamp[10] == "T":
        return stamp[11:19]
    return time.strftime("%H:%M:%S", time.gmtime())


def one_line(text, width=WIDTH):
    text = " ".join(str(text).split())
    return text if len(text) <= width else text[: width - 3] + "..."


def tool_summary(name, args):
    """What a tool call does, in one line: its description, path, pattern or command."""
    if not isinstance(args, dict):
        return ""
    for key in ("description", "file_path", "path", "pattern", "command", "prompt", "url", "query"):
        if isinstance(args.get(key), str) and args[key].strip():
            extra = f" ({one_line(args['command'], 80)})" if key == "description" and "command" in args else ""
            return one_line(args[key]) + extra
    return ""


def result_summary(content):
    """A tool result in one line: how many lines it gave, and its first line."""
    if isinstance(content, list):
        content = "\n".join(c.get("text", "") for c in content if isinstance(c, dict))
    text = str(content or "").strip()
    if not text:
        return "(empty)"
    lines = text.splitlines()
    return f"{len(lines)} lines: {one_line(lines[0], 120)}" if len(lines) > 1 else one_line(lines[0])


def describe(event):
    """[(time, kind, summary)] for one event of a session (stream-json or transcript JSONL)."""
    if not isinstance(event, dict):
        return []
    at = clock(event.get("timestamp"))
    kind = event.get("type")
    if kind == "system" and event.get("subtype") == "init":
        return [(at, "start", f"model {event.get('model', '?')} in {event.get('cwd', '?')}")]
    if kind == "result":
        secs = (event.get("duration_ms") or 0) / 1000
        state = "error" if event.get("is_error") else "ok"
        return [(at, "done", f"{state} after {event.get('num_turns', '?')} turns, {secs:.0f} s")]
    message = event.get("message")
    if kind not in ("assistant", "user") or not isinstance(message, dict):
        return []
    content = message.get("content")
    if isinstance(content, str):
        return [(at, "text", one_line(content))] if kind == "assistant" and content.strip() else []
    out = []
    for part in content if isinstance(content, list) else []:
        if not isinstance(part, dict):
            continue
        if part.get("type") == "text" and kind == "assistant" and part.get("text", "").strip():
            out.append((at, "text", one_line(part["text"])))
        elif part.get("type") == "tool_use":
            out.append((at, str(part.get("name", "tool")), tool_summary(part.get("name"), part.get("input"))))
        elif part.get("type") == "tool_result":
            mark = "error" if part.get("is_error") else "result"
            out.append((at, mark, result_summary(part.get("content"))))
    return out


def line(entry):
    at, kind, summary = entry
    return f"{at} {kind:<8} {summary}".rstrip()


class LiveLog:
    """The live log of one implementer's session: written line by line, flushed at each, so a reader sees it as it
    grows. A new session starts the file again. It also counts the models the API answered with, by id, so a session's
    record says which model wrote its work as the API reported it, not as the session claims."""

    def __init__(self, public, name, state):
        folder = Path(public, LIVE)
        folder.mkdir(parents=True, exist_ok=True)
        self.state_path = folder / f"{name}.json"
        self.state = dict(state)
        self.write_state()
        self.f = open(folder / f"{name}.log", "w", encoding="utf-8", newline="\n")  # noqa: SIM115 -- closed in close()
        self.final = None
        self.models = {}

    def write_state(self):
        tmp = self.state_path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.state, sort_keys=True), encoding="utf-8")
        tmp.replace(self.state_path)

    def feed(self, raw):
        """One line of the session's stream-json output."""
        try:
            event = json.loads(raw)
        except (json.JSONDecodeError, UnicodeDecodeError):
            text = raw.decode("utf-8", "replace") if isinstance(raw, bytes) else str(raw)
            if text.strip():
                self.write([(clock(), "output", one_line(text))])
            return
        message = event.get("message") if isinstance(event, dict) else None
        if isinstance(message, dict) and event.get("type") == "assistant" and isinstance(message.get("model"), str):
            self.models[message["model"]] = self.models.get(message["model"], 0) + 1
        if isinstance(event, dict) and event.get("type") == "result" and isinstance(event.get("result"), str):
            self.final = event["result"]
        self.write(describe(event))

    def write(self, entries):
        for e in entries:
            self.f.write(line(e) + "\n")
        self.f.flush()

    def served(self):
        """{model id: responses} for every model the API answered this session with."""
        return dict(sorted(self.models.items()))

    def close(self, **ended):
        self.state.update(ended)
        self.write_state()
        self.f.close()


def transcript_markdown(name, lines_of_jsonl):
    """A Markdown transcript of one session's JSONL lines: a heading, then one list item per event."""
    out = [f"# {name}", ""]
    for raw in lines_of_jsonl:
        try:
            event = json.loads(raw)
        except (json.JSONDecodeError, UnicodeDecodeError):
            continue
        for at, kind, summary in describe(event):
            text = summary.replace("`", "'")
            out.append(f"- `{at}` **{kind}** {text}".rstrip())
    return "\n".join(out) + "\n"


def write_transcripts(archive):
    """Writes `<archive>/transcripts/<session>.md` for every session transcript (projects/**/*.jsonl) in an
    implementer's archive folder; the number written."""
    archive = Path(archive)
    out = archive / TRANSCRIPTS
    n = 0
    for src in sorted(archive.glob("projects/**/*.jsonl")):
        try:
            data = src.read_bytes().splitlines()
        except OSError:
            continue
        out.mkdir(parents=True, exist_ok=True)
        (out / f"{src.stem}.md").write_text(transcript_markdown(src.stem, data), encoding="utf-8", newline="\n")
        n += 1
    return n


def read_log(public, name=None, tail=TAIL):
    """{"implementers": [{name, state..., lines}], "shown": name, "readable": bool}: every implementer with a live log,
    its state, and the last `tail` lines of the named one (else the one most recently started). readable is False when
    this account may not list the live folder, so a reader can tell that from no implementer having a log."""
    folder = Path(public, LIVE)
    try:
        names = sorted(os.listdir(folder))
        readable = True
    except FileNotFoundError:
        names, readable = [], True
    except OSError:
        names, readable = [], False
    found = []
    for p in (folder / n for n in names if n.endswith(".json")):
        try:
            state = json.loads(p.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError, UnicodeDecodeError):
            state = {}
        found.append({"name": p.stem, **(state if isinstance(state, dict) else {})})
    shown = name or max(found, key=lambda s: str(s.get("started", "")), default={}).get("name")
    lines = []
    if shown:
        try:
            lines = (folder / f"{shown}.log").read_text(encoding="utf-8").splitlines()[-tail:]
        except (OSError, UnicodeDecodeError):
            lines = []
    return {"implementers": found, "shown": shown, "lines": lines, "readable": readable}
