"""Tether in every zone and container (docs/workflow.md): one build, behaviour set only by per-zone settings.

The workspace's `tether` table is the one source: a `base` config every zone shares, an override per zone and one for
the implementer containers, the zones that may reach GitHub, and the Tether commit the installer copies. `write`
renders each zone's locked config file (the installer puts them where no zone account can change them, and each
zone's start script names its file in TETHER_CONFIG); `check` refuses a table that would let Tether reach the network,
put free text in front of a session, or register tools (served from a loopback port) outside the GitHub zones, and
with `--installed` compares the installed files with the table.
"""

import json
import os
import re
from pathlib import Path

from coney_tools.workspace import WorkspaceError

SECTIONS = {"fleet", "checkpoint", "watch", "housekeeping", "conversations", "stop", "cacheGuard"}
TOP_LEVEL = {"locked", "network", "plainOutput", "allowedCommands"}
CONTAINER = "container"  # the implementer containers' config, baked into the image


class TetherError(Exception):
    """The workspace's tether table cannot be read (the command could not run: exit 2)."""


def table(ws):
    t = ws.data.get("tether")
    if not isinstance(t, dict):
        raise TetherError("the workspace has no 'tether' table")
    return t


def names(ws):
    """Every config the table renders: each workspace zone, then the containers."""
    return [*ws.data.get("zones", {}), CONTAINER]


def render(ws, name):
    """The locked config for one zone (or the containers): the base, each section merged field by field with the
    override, top-level values replaced, and `locked` always true."""
    t = table(ws)
    base, over = t.get("base", {}), t.get("zones", {}).get(name)
    if name == CONTAINER:
        over = t.get(CONTAINER)
    if not isinstance(base, dict) or not isinstance(over, dict):
        raise TetherError(f"the tether table has no settings for '{name}'")
    out = json.loads(json.dumps(base))
    for key, value in over.items():
        if isinstance(value, dict) and isinstance(out.get(key), dict):
            out[key] = {**out[key], **value}
        else:
            out[key] = value
    out["locked"] = True
    return out


def problems_of(name, cfg, github):
    out = []
    for key in cfg:
        if key not in SECTIONS | TOP_LEVEL:
            out.append(f"{name}: unknown key '{key}'")
    if github:
        return out
    if cfg.get("network") is not False:
        out.append(f"{name}: network must be false outside the GitHub zones")
    if cfg.get("plainOutput") is not True:
        out.append(f"{name}: plainOutput must be true outside the GitHub zones")
    pollers = (cfg.get("watch") or {}).get("pollers") or []
    if any(isinstance(p, dict) and p.get("kind") == "prs" for p in pollers):
        out.append(f"{name}: a prs poller outside the GitHub zones")
    # Claude Code serves a mod's tools from a loopback port in the session's process: none outside the GitHub zones
    if (cfg.get("cacheGuard") or {}).get("registerTools") is not False:
        out.append(f"{name}: cacheGuard.registerTools must be false outside the GitHub zones")
    return out


def parts_problems(ws, name, cfg, exempt):
    """Every config keeps fleet, cacheGuard and watch on, and the checkpoint on with its role's continuity notes as the
    handoff, unless checkpoint_exempt names it with a reason (maintainer, 2026-10-16)."""
    out = []
    if (cfg.get("fleet") or {}).get("enabled") is False:
        out.append(f"{name}: fleet must be on")
    if (cfg.get("watch") or {}).get("enabled") is False:
        out.append(f"{name}: watch must be on")
    if (cfg.get("cacheGuard") or {}).get("auto") is False:
        out.append(f"{name}: the cache guard must act (cacheGuard.auto)")
    cp = cfg.get("checkpoint") or {}
    if name in exempt:
        return out + ([f"{name}: checkpoint_exempt but its checkpoint is on"] if cp.get("enabled") is True else [])
    if cp.get("enabled") is not True:
        return out + [f"{name}: checkpoint must be on (or the zone listed in checkpoint_exempt with a reason)"]
    notes = set()
    for role, r in (ws.data.get("roles") or {}).items():
        if isinstance(r, dict) and r.get("zone") == name and isinstance(r.get("notes"), str):
            try:
                notes.add(Path(os.path.normpath(ws.resolve(r["notes"], f"roles.{role}.notes"))))
            except (WorkspaceError, TypeError):
                continue
    handoff = cp.get("handoffFile")
    try:
        # As Tether reads it: relative to the session's folder (the zone's), '..' allowed
        where = (
            Path(os.path.normpath(ws.resolve(ws.data["zones"][name]["folder"]) / handoff))
            if isinstance(handoff, str)
            else None
        )
    except (WorkspaceError, KeyError, TypeError):
        where = None
    if where is None or where not in notes:
        out.append(f"{name}: checkpoint.handoffFile must be its role's continuity notes (roles.<role>.notes)")
    return out


def check(ws, installed=False):
    """Problems with the tether table (and, when installed, with the installed copy), as lines."""
    t = table(ws)
    out = []
    exempt = t.get("checkpoint_exempt", {})
    if not isinstance(exempt, dict) or not all(
        z in names(ws) and isinstance(why, str) and why.strip() for z, why in exempt.items()
    ):
        out.append("checkpoint_exempt must map configs of the table to a reason")
        exempt = {}
    github = t.get("github_zones")
    if not isinstance(github, list) or not all(z in ws.data.get("zones", {}) for z in github):
        out.append("github_zones must list zones of the workspace")
        github = []
    commit = t.get("commit")
    if not isinstance(commit, str) or not re.fullmatch(r"[0-9a-f]{40}", commit):
        out.append("commit must be a full 40-character commit id")
    allow = ws.data.get("mcp_allow", {})
    for name in names(ws):
        try:
            cfg = render(ws, name)
        except TetherError as e:
            out.append(str(e))
            continue
        out += problems_of(name, cfg, name in github)
        out += parts_problems(ws, name, cfg, exempt)
        if name != CONTAINER and "tether" not in (allow.get(name) or []):
            out.append(f"{name}: mcp_allow must list tether (its tools are mcp__tether__*)")
        if installed:
            out += installed_problems(ws, name, cfg)
    if installed:
        mod = ws.where("tether_mod")
        if not (mod / ".claude-plugin" / "plugin.json").is_file():
            out.append(f"Tether is not installed at {mod}")
        else:
            got = (mod / "VERSION").read_text(encoding="ascii").strip() if (mod / "VERSION").is_file() else None
            if got != commit:
                out.append(f"installed Tether is {got or 'of no recorded commit'}, the table names {commit}")
    return out


def installed_problems(ws, name, cfg):
    path = ws.where("tether_configs") / f"{name}.json"
    try:
        got = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return [f"{name}: no readable config at {path}"]
    return [] if got == cfg else [f"{name}: {path} differs from the workspace (run the installer)"]


def write(ws, out_dir):
    """Write every zone's (and the containers') locked config into out_dir; returns the paths written."""
    out_dir.mkdir(parents=True, exist_ok=True)
    written = []
    for name in names(ws):
        path = out_dir / f"{name}.json"
        path.write_text(json.dumps(render(ws, name), indent=2) + "\n", encoding="utf-8")
        written.append(path)
    return written
