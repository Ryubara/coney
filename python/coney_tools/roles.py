"""The pipeline's roles (docs/workflow.md): each one's inbox, and the check that every role is fully wired.

Sessions in different zones never message each other: each role that runs continuously polls its own inbox on a
timer and acts on what it lists. `inbox` gathers that role's items from every station it takes part in (today the
page gate; the queue, the containers, the code gate and the tester add theirs as they are built). Output is ids,
states and counts only, never page text.

The roles, their zones, loop intervals and whether their tools exist yet live in the workspace's `roles` table; each
role's brief is `<roles path>/<role>.md`. `roles check` (run by the installer) refuses a table where a role lacks its
brief, an active role lacks an inbox, a looping role lacks its interval, or a role's continuity notes (`notes`, which
the zone shortcut's first prompt names) lie outside its own zone or, for a fenced zone, outside its write fence.

A role whose own work goes on between inbox items (the research coordinator works down its research list) names
`idle_wake_minutes`: its zone's mod wakes it again after that long idle with no subagent running, so the session
never stops for good while nothing lands in its inbox. Roles that only answer their inbox leave it out.

A role another role's session dispatches as a subagent (the research coordinator dispatches the PR analyst) names it
in `dispatched_by`: it shares that role's zone, never loops and keeps no notes, and its stations, while it is active,
are part of the dispatcher's inbox, so one session per zone account wakes for all of them.
"""

import json

from coney_tools import codeverdict, research
from coney_tools.workspace import WorkspaceError

CONTAINER = "container"  # roles started per task by the engine coordinator: no zone account, no loop

# role -> the stations that fill its inbox: (station name, function(ws) -> research.status-like document)
PROVIDERS = {
    # the public role reads the repository the workspace names: the installed tools are not a checkout
    "engine-coordinator": [
        ("pages", lambda ws: research.status(research.PUBLIC, ws, ws.where("repo"))),
        ("code-verdicts", lambda ws: codeverdict.public_status(ws, ws.where("repo"))),
    ],
    "research-coordinator": [("pages", lambda ws: research.status(research.COORDINATOR, ws))],
    "page-reviewer-1": [("pages", lambda ws: research.status(research.REVIEWER_1, ws))],
    "page-reviewer-2": [("pages", lambda ws: research.status(research.REVIEWER_2, ws))],
    "pr-analyst": [("engine-branches", codeverdict.analyst_status)],
}


class RoleError(Exception):
    """The role table is incomplete or the role is unknown (the command could not run: exit 2)."""


def table(ws):
    """The workspace's roles, without its comment."""
    roles = ws.data.get("roles")
    if not isinstance(roles, dict):
        raise RoleError("the workspace has no 'roles' table")
    return {k: v for k, v in roles.items() if k != "comment"}


def brief(ws, role):
    return ws.where("roles") / f"{role}.md"


def _check_notes(ws, role, zone, notes):
    """A role's continuity notes: a workspace path inside its own zone's root, so no role is ever pointed at what
    another zone keeps."""
    if zone == CONTAINER:
        return [f"{role}: a container role keeps no notes (notes must be absent or null)"]
    if not isinstance(notes, str):
        return [f"{role}: notes must be a workspace path ('<root>:<path>')"]
    try:
        ws.resolve(notes, f"roles.{role}.notes")
        own = ws.split(ws.data["zones"][zone]["folder"])[0]
    except (WorkspaceError, KeyError) as e:
        return [f"{role}: notes {notes!r}: {e}"]
    if ws.split(notes)[0] != own:
        return [f"{role}: notes {notes!r} lie outside its zone's root '{own}'"]
    # A fenced zone writes only where its fence lets it: the notes must lie there too
    fence = (ws.data.get("write_fence") or {}).get(zone)
    if isinstance(fence, list):
        where = ws.resolve(notes, f"roles.{role}.notes")
        inside = False
        for f in fence:
            try:
                inside |= where.is_relative_to(ws.resolve(f, f"write_fence.{zone}"))
            except WorkspaceError:
                continue
        if not inside:
            return [f"{role}: notes {notes!r} lie outside its zone's write fence"]
    return []


def check(ws, providers=PROVIDERS):
    """Problems with the role table, as lines; empty when every role is wired."""
    out = []
    for role, r in table(ws).items():
        if not isinstance(r, dict):
            out.append(f"{role}: not an object")
            continue
        zone, active, minutes = r.get("zone"), r.get("active"), r.get("loop_minutes")
        if zone != CONTAINER and zone not in ws.data.get("zones", {}):
            out.append(f"{role}: zone '{zone}' is not a zone of the workspace (or '{CONTAINER}')")
        if not isinstance(active, bool):
            out.append(f"{role}: 'active' must be true or false")
        by = r.get("dispatched_by")
        if by is not None:
            lead = table(ws).get(by)
            if not isinstance(lead, dict) or lead.get("dispatched_by") is not None or lead.get("zone") != zone:
                out.append(f"{role}: dispatched_by must name a role of its own zone that is not dispatched itself")
            elif active is True and not lead.get("active"):
                out.append(f"{role}: active, but its dispatcher '{by}' is not")
            if zone == CONTAINER or minutes is not None or r.get("notes") is not None or r.get("idle_wake_minutes"):
                out.append(f"{role}: a dispatched role has a zone, never loops or wakes and keeps no notes")
        elif zone == CONTAINER:
            if minutes is not None:
                out.append(f"{role}: a container role never loops (loop_minutes must be null)")
        elif not isinstance(minutes, int) or minutes < 1:
            out.append(f"{role}: loop_minutes must be a whole number of minutes, 1 or more")
        wake = r.get("idle_wake_minutes")
        if wake is not None and (zone == CONTAINER or not isinstance(wake, int) or isinstance(wake, bool) or wake < 1):
            out.append(
                f"{role}: idle_wake_minutes must be absent, null or a whole number of minutes, 1 or more"
                " (and never on a container role)"
            )
        notes = r.get("notes")
        if notes is not None:
            out += _check_notes(ws, role, zone, notes)
        if not brief(ws, role).is_file():
            out.append(f"{role}: no brief at {brief(ws, role)}")
        if active is True and zone != CONTAINER and role not in providers:
            out.append(f"{role}: active, but no inbox serves it")
    return out


def inbox(ws, role, providers=PROVIDERS):
    """The role's inbox: {"role", "active", "stations": {name: document}, "action": [ids]}, with the stations of the
    active roles it dispatches."""
    roles = table(ws)
    if role not in roles:
        raise RoleError(f"--role must be one of {', '.join(roles)}")
    if not roles[role].get("active"):
        return {"role": role, "active": False, "stations": {}, "action": []}
    stations, action = {}, []
    served = [role] + [r for r, v in roles.items() if v.get("dispatched_by") == role and v.get("active")]
    for name, fn in [p for r in served for p in providers.get(r, [])]:
        doc = fn(ws)
        stations[name] = doc
        action += [f"{name}:{i}" for i in doc["action"]]
    return {"role": role, "active": True, "stations": stations, "action": action}


def inbox_lines(doc):
    if not doc["active"]:
        return [f"{doc['role']}: inactive (its tools are not built yet); nothing to do"]
    lines = []
    for name, d in doc["stations"].items():
        lines += [f"{name}: {line}" for line in research.status_lines(d)]
    lines.append(f"{len(doc['action'])} need {doc['role']}")
    return lines


def zone_of(ws, account):
    """The workspace zone whose account this is, or None."""
    for zone, z in ws.data.get("zones", {}).items():
        if str(z.get("account", "")).lower() == account.lower():
            return zone
    return None


def looping_role(ws, zone):
    """The zone's one active role that runs on its inbox, or None (none, or more than one: started by hand)."""
    found = [r for r, v in table(ws).items() if v.get("zone") == zone and v.get("active") and v.get("loop_minutes")]
    return found[0] if len(found) == 1 else None


def walls(ws):
    """The last zones check and the breach flag, from the machine's state folder (every account may read it), with
    the folders where the last check found a legacy copy. Checks and passed count the walls only: a legacy copy is
    not a wall, and is named by its folder instead."""
    out = {"checked": None, "passed": None, "checks": None, "breach": False, "breach_folders": [], "legacy_folders": []}
    try:
        doc = json.loads(ws.where("check_result").read_text(encoding="utf-8-sig"))
        wall = [c for c in doc.get("checks", []) if not c.get("legacy")]
        out.update(
            checked=doc.get("time"),
            checks=len(wall),
            passed=sum(1 for c in wall if c.get("ok")),
            legacy_folders=[str(f) for f in doc.get("legacy") or []],
        )
    except (OSError, ValueError):
        pass
    flag = ws.where("breach_flag")
    if flag.exists():
        out["breach"] = True
        try:
            out["breach_folders"] = list(json.loads(flag.read_text(encoding="utf-8-sig")).get("folders", []))
        except (OSError, ValueError):
            out["breach_folders"] = ["(flag unreadable)"]
    return out


def pages_in_main(ws):
    """How many research pages main holds (the public side's view), from git, never the working tree."""
    from coney_tools import signing

    r = signing.git(signing.program("git"), ws.where("repo"), "ls-tree", "-d", "--name-only", "main", "research/pages/")
    return len([line for line in r.stdout.decode().splitlines() if line.strip()])


def zone_status(ws, account, providers=PROVIDERS, now=None):
    """Everything a zone's mod shows and wakes on, for this account: its zone and role, the role's inbox, the walls
    and, on the public side, the pages in main. Ids, states and counts only."""
    import time

    zone = zone_of(ws, account)
    role = looping_role(ws, zone) if zone else None
    doc = {
        "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(now)),
        "account": account,
        "zone": zone,
        "role": role,
        "brief": str(brief(ws, role)) if role else None,
        "idle_wake_minutes": table(ws)[role].get("idle_wake_minutes") if role else None,
        "inbox": None,
        "error": None,
        "walls": walls(ws),
        "pages_in_main": None,
    }
    if role:
        try:
            doc["inbox"] = inbox(ws, role, providers)
        except Exception as e:  # noqa: BLE001 -- shown in the pane: a broken inbox must never look empty
            doc["error"] = f"{e.__class__.__name__}: {e}"
    if zone == "public":
        try:
            doc["pages_in_main"] = pages_in_main(ws)
        except Exception as e:  # noqa: BLE001
            doc["error"] = doc["error"] or f"pages in main: {e.__class__.__name__}"
    return doc
