"""coney-tools command line. Exit status: 0 passed, 1 a check failed, 2 the command could not run."""

import argparse
import json
import subprocess
import sys

from coney_tools import (
    __version__,
    ci,
    codegate,
    codeverdict,
    impl,
    implog,
    lint,
    needs,
    prefilter,
    research,
    roles,
    signing,
    tether,
    wfp,
    workspace,
    zones,
)
from coney_tools.cleanroom import disclosure, page_check, scan


def disclosure_check_cmd(a):
    if a.rules:
        for rid, what, _ in disclosure.RULES:
            print(f"{rid:14} {what}")
        return 0
    if bool(a.files) == bool(a.commits):
        print(
            "disclosure-check: give text files (- for standard input) or --commits RANGE, one of the two",
            file=sys.stderr,
        )
        return 2
    if a.commits and a.commits.startswith("-"):
        print("disclosure-check: a revision range cannot start with -", file=sys.stderr)
        return 2
    sources = []  # (name, findings)
    try:
        if a.commits:
            for sha, body in disclosure.commit_messages(a.commits):
                sources.append((sha, disclosure.check_bytes(body)))
        else:
            for p in a.files:
                if p == "-":
                    data = sys.stdin.buffer.read(disclosure.MAX_TEXT + 1)
                else:
                    with open(p, "rb") as f:
                        data = f.read(disclosure.MAX_TEXT + 1)
                sources.append((p, disclosure.check_bytes(data, a.commit_msg)))
    except (OSError, subprocess.SubprocessError) as e:
        print(f"disclosure-check: cannot read the text: {e.__class__.__name__}", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry the text), never exit 1 for a crash
        print(f"disclosure-check: internal error: {e.__class__.__name__}", file=sys.stderr)
        return 2
    failed = any(fs for _, fs in sources)
    if a.json:
        print(
            json.dumps(
                {
                    "ok": not failed,
                    "sources": [
                        {
                            "source": name,
                            "ok": not fs,
                            "findings": [{"rule": f.rule, "line": f.line, "message": f.message} for f in fs],
                        }
                        for name, fs in sources
                    ],
                },
                indent=2,
                ensure_ascii=False,
            )
        )
        return 1 if failed else 0
    for name, fs in sources:
        for f in fs:
            print(f"{name}:{f.line}: {f.rule}: {f.message}")
    n = sum(len(fs) for _, fs in sources)
    print(f"{'FAIL' if failed else 'PASS'}: {n} findings in {len(sources)} texts")
    return 1 if failed else 0


def paste_check_cmd(a):
    try:
        findings = page_check.paste_findings(sys.stdin.buffer.read())
    except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry the text), never exit 1 for a crash
        print(f"paste-check: internal error: {e.__class__.__name__}", file=sys.stderr)
        return 2
    for f in findings:
        print(f"line {f.line}: {f.rule}: {f.message}")
    return 1 if findings else 0


def page_check_cmd(a):
    if a.rules:
        for rid, what in page_check.RULES:
            print(f"{rid:18} {what}")
        return 0
    if not a.pages:
        print("page-check: give one or more pages, or - for standard input", file=sys.stderr)
        return 2
    results, failed, broken = [], False, False
    for p in a.pages:
        if a.guide and (p == "-" or not page_check.is_guide_path(p)):
            print(f"page-check: --guide applies only to pages under docs/guides/ of a checkout: {p}", file=sys.stderr)
            broken = True
            continue
        try:
            if p == "-":
                sections = page_check.check_bytes(sys.stdin.buffer.read(), None, a.guide)
            else:
                sections = page_check.check_file(p, a.guide)
        except OSError as e:
            print(f"page-check: cannot read {p}: {e.__class__.__name__}", file=sys.stderr)
            broken = True
            continue
        except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry page text), never exit 1 for a crash
            print(f"page-check: internal error on {p}: {e.__class__.__name__}", file=sys.stderr)
            broken = True
            continue
        failed |= any(not s.ok for s in sections)
        results.append((p, sections))
    if a.json:
        print(
            json.dumps(
                {
                    "checker": page_check.checker_sha256(),
                    "guide": a.guide,
                    "pages": [
                        {
                            "path": p,
                            "ok": all(s.ok for s in secs),
                            "sections": [
                                {
                                    # a failing section's hash is withheld too: for a short section it would
                                    # confirm a guessed text
                                    "id": s.id,
                                    "heading": s.heading,
                                    "line": s.line,
                                    "sha256": s.sha256() if s.ok else None,
                                    "ok": s.ok,
                                    "findings": [
                                        {"rule": f.rule, "line": f.line, "message": f.message} for f in s.findings
                                    ],
                                }
                                for s in secs
                            ],
                        }
                        for p, secs in results
                    ],
                },
                indent=2,
                ensure_ascii=False,
            )
        )
    else:
        for p, secs in results:
            bad = [s for s in secs if not s.ok]
            if not bad:
                print(f"PASS {p} ({len(secs)} sections)")
                continue
            n = sum(len(s.findings) for s in bad)
            print(f"FAIL {p}: {n} findings in {len(bad)} of {len(secs)} sections")
            for s in bad:
                for f in s.findings:
                    print(f"  {p}:{f.line}  [{s.id}]  {f.rule}: {f.message}")
    return 2 if broken else (1 if failed else 0)


def jobs_arg(text):
    try:
        n = int(text)
    except ValueError:
        n = 0
    if n < 1:
        raise argparse.ArgumentTypeError("must be a whole number, 1 or more")
    return n


def seconds_arg(text):
    try:
        n = int(text)
    except ValueError:
        n = -1
    if n < 0:
        raise argparse.ArgumentTypeError("must be a whole number of seconds, 0 or more")
    return n


def scan_cmd(a):
    try:
        code, doc = scan.run(a)
    except scan.DataError as e:
        print(f"scan: {e}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("scan: stopped (Ctrl+C); no report", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry file text), never exit 1 for a crash
        print(f"scan: internal error: {e.__class__.__name__}", file=sys.stderr)
        return 2
    text = json.dumps(doc, indent=2, ensure_ascii=False) + "\n" if a.json else scan.text_report(doc)
    if a.out:
        try:
            scan.write_atomic(a.out, text)
        except (OSError, ValueError) as e:
            print(f"scan: cannot write {a.out}: {e.__class__.__name__}", file=sys.stderr)
            return 2
        print("\n".join([*scan.summary_lines(doc), f"full report: {scan.esc(a.out)}"]))
    else:
        sys.stdout.write(text)
    return code


def allow_renew_cmd(a):
    try:
        paths = scan.data_paths(a)
        if "allow" not in paths or "markers" not in paths:
            raise scan.DataError("give --allow and --markers, or --data DIR holding allow.json and markers.json")
        n, msha = scan.renew_allow(paths["allow"], paths["markers"], a.sha256)
    except scan.DataError as e:
        print(f"allow-renew: {e}", file=sys.stderr)
        return 2
    except (OSError, ValueError) as e:
        print(f"allow-renew: cannot write the allow list: {e.__class__.__name__}", file=sys.stderr)
        return 2
    print(f"renewed {n} allow entries under markers {msha}")
    return 0


def zones_check_cmd(a):
    try:
        ws = workspace.load(a.workspace)
        mode = a.mode or ("full" if zones.is_elevated() else "public")
        code, doc, checks = zones.run(ws, mode)
    except workspace.WorkspaceError as e:
        print(f"zones check: {e}", file=sys.stderr)
        return 2
    except PermissionError as e:
        print(f"zones check: {e}", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001 -- never a traceback, never exit 1 for a crash
        print(f"zones check: internal error: {e.__class__.__name__}", file=sys.stderr)
        return 2
    text = json.dumps(doc, indent=2) + "\n"
    try:
        if a.cache:
            zones.write_atomic(a.cache, text)
        if a.write_state:
            zones.write_state(ws, doc, checks, may_clear=mode == "full" and zones.is_elevated())
    except workspace.WorkspaceError as e:
        print(f"zones check: {e}", file=sys.stderr)
        return 2
    except OSError as e:
        print(f"zones check: cannot write the result: {e.__class__.__name__}", file=sys.stderr)
        return 2
    sys.stdout.write(text if a.json else zones.text_report(doc))
    return code


def zones_ports_cmd(a):
    """The port wall's filters: --apply and --remove need elevation; --list shows what stands."""

    def say(m):
        print(f"zones ports: {m}", file=sys.stderr)

    if (a.apply or a.remove) and not zones.is_elevated():
        say("changing the port wall's filters needs elevation: run it elevated (as an administrator) on Windows")
        return 2
    try:
        if not a.remove:
            ws = workspace.load(a.workspace)
        if a.apply:
            allowed = {s: zones.sid_of(s) for s in sorted(ws.analyst_side_accounts())}
            specs = wfp.expected([p["port"] for p in ws.ports], allowed.values())
            programs = zones.program_specs(ws, zones.sid_of, wfp.app_id)
            specs += [s for *_, ps in programs for s in ps]
        with wfp.Engine() as eng:
            if a.apply:
                removed, added = wfp.apply(eng, specs)
                print(f"removed {len(removed)} old filters; added {len(added)} filters (persistent)")
                print(f"allowed through: {', '.join(f'{n} ({s})' for n, s in allowed.items()) or 'no account'}")
                walled = ", ".join(f"{a} ({path})" for a, _, path, _ in programs)
                print(f"programs taking no loopback connection: {walled or 'none found'}")
                for s in added:
                    print(f"  {s.text()}")
            elif a.remove:
                removed = wfp.remove(eng)
                print(f"removed {len(removed)} filters, Coney's sublayer and provider")
            else:
                weight, have = wfp.read_state(eng)
                print("sublayer: " + ("missing" if weight is None else f"present, weight 0x{weight:x}"))
                above, level = wfp.outranking(weight, wfp.read_others(eng)) if weight is not None else ([], [])
                for label, group in (("above it", above), ("level with it", level)):
                    for name, w in group:
                        print(f"  other sublayer {label}: {name or '(unnamed)'} 0x{w:x}")
                if weight is not None and (above or level):
                    risky = wfp.overriding(eng, weight, [p["port"] for p in ws.ports])
                    for f, s, lay, port, conds, scope in risky:
                        if scope == "off-loopback":
                            print(f"  limited to a non-loopback interface: filter {f} in {s} ({lay}, port {port})")
                        else:
                            print(f"  COULD OVERRULE the wall: filter {f} in {s} ({lay}, port {port})")
                        for c in conds:
                            print(f"      condition: {c}")
                    if not risky:
                        print(
                            "  none of their filters could overrule the wall (no hard permit or callout on its ports)"
                        )
                print(f"{len(have)} filters")
                for s in have:
                    print(f"  {s.text()}")
    except workspace.WorkspaceError as e:
        say(str(e))
        return 2
    except OSError as e:
        say(str(e) if isinstance(e, wfp.WfpError) else f"{e.strerror or e}")
        return 2
    except Exception as e:  # noqa: BLE001 -- never a traceback, never exit 1 for a crash
        say(f"internal error: {e.__class__.__name__}")
        return 2
    return 0


def gate(name, action):
    """A page-gate command: its lines on standard output; refusals and errors on standard error with exit 2."""

    def run(a):
        try:
            code, lines = action(a)
        except (
            research.GateError,
            signing.SigningError,
            lint.LintError,
            roles.RoleError,
            tether.TetherError,
            workspace.WorkspaceError,
        ) as e:
            print(f"{name}: {e}", file=sys.stderr)
            return 2
        except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry page text), never exit 1 for a crash
            print(f"{name}: internal error: {e.__class__.__name__}", file=sys.stderr)
            return 2
        for line in lines:
            print(line)
        return code

    return run


def verdict_action(a):
    if a.countersign:
        if a.decisions:
            raise research.GateError("--countersign takes no --decisions: the second reviewer cannot change them")
        return research.countersign(a.folder, a.reviewer, a.key, a.ssh_keygen, a.npx)
    if not a.decisions:
        raise research.GateError("give --decisions (the first reviewer), or --countersign (the second)")
    return research.verdict(a.folder, a.reviewer, a.key, a.decisions, a.ssh_keygen, a.npx)


def import_action(a):
    if a.all == bool(a.bundle):
        raise research.GateError("give a bundle folder, or --all for every new bundle in the outbox")
    ws = workspace.load(a.workspace)  # its withdrawn list holds for a single bundle too
    if a.all:
        return research.import_all(ws, a.repo, a.ssh_keygen, a.npx)
    return research.import_bundle(a.bundle, a.repo, a.ssh_keygen, a.npx, ws.withdrawn)


def status_action(a):
    doc = research.status(a.role, workspace.load(a.workspace), a.repo)
    lines = [json.dumps(doc, indent=2)] if a.json else research.status_lines(doc)
    return (1 if doc["action"] else 0), lines


def inbox_action(a):
    doc = roles.inbox(workspace.load(a.workspace), a.role)
    lines = [json.dumps(doc, indent=2)] if a.json else roles.inbox_lines(doc)
    return (1 if doc["action"] else 0), lines


def zone_status_action(a):
    doc = roles.zone_status(workspace.load(a.workspace), zones.current_account())
    action = (doc["inbox"] or {}).get("action", [])
    if a.json:
        return (1 if action else 0), [json.dumps(doc, indent=2)]
    w = doc["walls"]
    lines = [
        f"zone {doc['zone'] or '(none)'} as {doc['account']}; role {doc['role'] or '(none)'}",
        f"walls: {w['passed']}/{w['checks']} checks at {w['checked']}"
        + ("; BREACH FLAG SET" if w["breach"] else "")
        + (f"; LEGACY COPY in {', '.join(w['legacy_folders'])}" if w["legacy_folders"] else ""),
    ]
    if doc["pages_in_main"] is not None:
        lines.append(f"pages in main: {doc['pages_in_main']}")
    if doc["error"]:
        lines.append(f"error: {doc['error']}")
    if doc["inbox"]:
        lines += roles.inbox_lines(doc["inbox"])
    return (1 if action else 0), lines


def roles_check_action(a):
    problems = roles.check(workspace.load(a.workspace))
    return (1 if problems else 0), problems or ["every role is wired"]


def tether_check_action(a):
    problems = tether.check(workspace.load(a.workspace), installed=a.installed)
    return (1 if problems else 0), problems or ["every zone's Tether config is in order"]


def orig_check_cmd(a):
    if a.rules:
        for rid, what in codegate.RULES:
            print(f"{rid:15} {what}")
        return 0
    return gate("cleanroom orig-check", lambda a: codegate.orig_check(a.repo, a.ref, a.ssh_keygen, a.json))(a)


def tether_write_action(a):
    from pathlib import Path

    written = tether.write(workspace.load(a.workspace), Path(a.out))
    return 0, [f"wrote {p}" for p in written]


def tether_show_action(a):
    return 0, [json.dumps(tether.render(workspace.load(a.workspace), a.name), indent=2)]


def impl_cmd(name, action):
    """An `impl` command: its lines on standard output; refusals and errors on standard error with exit 2. The
    commands that stream (build, up, run) print their tools' output as it comes."""

    def run(a):
        try:
            code, lines = action(a, workspace.load(a.workspace))
        except (impl.ImplError, signing.SigningError, workspace.WorkspaceError) as e:
            print(f"impl {name}: {e}", file=sys.stderr)
            return 2
        except KeyboardInterrupt:
            print(f"impl {name}: stopped (Ctrl+C)", file=sys.stderr)
            return 2
        except Exception as e:  # noqa: BLE001 -- never a traceback (it could carry a secret), never exit 1 for a crash
            print(f"impl {name}: internal error: {e.__class__.__name__}", file=sys.stderr)
            return 2
        for line in lines:
            print(line)
        return code

    return run


def ci_cmd(a):
    from pathlib import Path

    try:
        tools = lint.checkout_root()
        repo = Path(a.repo).resolve() if a.repo else tools
        if repo is None or tools is None:
            raise ci.CiError("coney-tools is not running from a git checkout")
        if a.list:
            for job in ci.JOBS.values():
                print(f"{job.name:13} {job.what}")
            return 0
        if a.versions:
            for key, value in ci.versions(repo).items():
                print(f"{key}={value}")
            return 0
        if a.install_node:
            print(ci.install_node(repo, a.install_node))
            return 0
        unknown = [n for n in a.job if n not in ci.JOBS]
        if unknown:
            raise ci.CiError(f"no job {', '.join(unknown)}; the jobs: {', '.join(ci.JOBS)}")
        site_dir = Path(a.site_dir).resolve() if a.site_dir else None
        ctx = ci.Context(tools=tools, repo=repo, ref=a.ref, range=a.range, texts=a.text, site_dir=site_dir)
        note = ci.python_note(ci.python_version(repo))
    except ci.CiError as e:
        print(f"ci: {e}", file=sys.stderr)
        return 2
    except Exception as e:  # noqa: BLE001 -- never a traceback, never exit 1 for a crash
        print(f"ci: internal error: {e.__class__.__name__}", file=sys.stderr)
        return 2
    if note:
        print(note)
    code, lines = ci.run_jobs(ctx, a.job or list(ci.JOBS))
    print("== summary")
    for line in lines:
        print(line)
    return code


def npx_option(parser):
    parser.add_argument(
        "--npx",
        help="npx to run markdownlint with (default: the first on PATH; on Windows npx.cmd). The first run in an "
        "account downloads the pinned markdownlint-cli2 from npm",
    )


def main(argv=None):
    # Page paths may hold any character; output must survive a pipe whatever the console's code page.
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    ap = argparse.ArgumentParser(prog="coney-tools")
    ap.add_argument("--version", action="version", version=f"coney-tools {__version__}")
    top = ap.add_subparsers(dest="group", required=True)

    clean = top.add_parser("cleanroom", help="clean-room checks (playbook section 8)")
    cl = clean.add_subparsers(dest="command", required=True)
    pc = cl.add_parser(
        "page-check",
        help="check research pages against the page rules (8.1)",
        description="Checks each section of each page against the page rules. Prints the rule and "
        "line of every finding, never the matched text; a failing section's heading is "
        "withheld.",
    )
    pc.add_argument("pages", nargs="*", help="Markdown pages; - reads one page from standard input")
    pc.add_argument("--json", action="store_true", help="per-section results with each section's SHA-256")
    pc.add_argument(
        "--guide",
        action="store_true",
        help="a how-to guide: fenced shell blocks (sh, bash, powershell, cmd...) are allowed; their "
        "content and everything else is checked as usual",
    )
    pc.add_argument("--rules", action="store_true", help="list the rules and exit")
    pc.set_defaults(run=page_check_cmd)

    pa = cl.add_parser(
        "paste-check",
        help="check text pasted into a session that must never see the original (8.5)",
        description="Reads text from standard input and checks it against the rules only the original's own material "
        f"sets off ({', '.join(page_check.PASTE_RULES)}); scripts, configs and logs pass. Prints the rule and line "
        "of every finding, never the text. Exit 0 passed, 1 the text must not be pasted, 2 could not run.",
    )
    pa.set_defaults(run=paste_check_cmd)

    dc = cl.add_parser(
        "disclosure-check",
        help="check that public text describes the process in neutral words (17)",
        description="Checks pull request bodies, commit messages and other public text for disclosure wording: "
        "reports of incidents, dates of problems, what was removed, the old repository. Prints the rule and line of "
        "every finding, never the text. Exit 0 passed, 1 the text must be reworded, 2 could not run.",
    )
    dc.add_argument("files", nargs="*", help="text files; - reads standard input (a pull request body, say)")
    dc.add_argument("--commits", metavar="RANGE", help="check every commit message in a git revision range instead")
    dc.add_argument(
        "--commit-msg",
        action="store_true",
        help="the files are git commit message files (a commit-msg hook): comment lines and everything from the "
        "scissors line on are left out, as git leaves them out",
    )
    dc.add_argument("--json", action="store_true", help="per-text results with each finding's rule and line")
    dc.add_argument("--rules", action="store_true", help="list the rules and exit")
    dc.set_defaults(run=disclosure_check_cmd)

    sc = cl.add_parser(
        "scan",
        help="look through folders for original-game material and old files (starting proof, 5.5)",
        description="Scans every file under the roots, inside archives, compressed streams, PDF streams, base64 in "
        "JSON and git objects, against private data files: named markers, old-file hashes and shingles. Reports "
        "paths, marker names and counts, never the matched text. Exit 0 no hits and no coverage gaps, 1 hits or "
        "gaps, 2 could not run.",
    )
    sc.add_argument("roots", nargs="+", help="folders or single files; a missing root is a coverage gap")
    sc.add_argument("--data", help="folder holding markers.json, old-file-hashes.txt, old-shingles.txt, allow.json")
    sc.add_argument("--markers", help="markers file (JSON); overrides --data")
    sc.add_argument("--hashes", help="old-file hashes, one SHA-256 per line; overrides --data")
    sc.add_argument("--shingles", help="old-text shingle hashes, one per line; overrides --data")
    sc.add_argument("--allow", help="allow list (JSON); overrides --data; optional")
    sc.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="FOLDER",
        help="skip a folder: a path (absolute, . or .., or starting with ./ .\\ ../ or ..\\) skips exactly that "
        "folder; any other value (.venv, game, tester\\game) skips every folder whose path ends with those whole "
        "components. Each skipped folder is listed",
    )
    sc.add_argument(
        "--except",
        dest="excepts",
        action="append",
        default=[],
        metavar="PATH",
        help="report hits on this file, or under this folder, as exceptions (classified by hand) instead of hits",
    )
    sc.add_argument(
        "--accept-gap",
        action="append",
        default=[],
        metavar="PATH",
        help="list coverage gaps on this path, or under this folder, as accepted gaps instead of failing on them "
        "(path-wide: every gap there, whatever its reason)",
    )
    sc.add_argument(
        "--jobs",
        type=jobs_arg,
        metavar="N",
        help="worker processes that scan files (default: one per CPU); 1 with --file-timeout 0 scans in this "
        "process. The report is the same whatever N is",
    )
    sc.add_argument(
        "--file-timeout",
        type=seconds_arg,
        metavar="SECONDS",
        help=f"stop a file (or git object) that takes longer than this in its worker; it is a coverage gap "
        f"(default {scan.FILE_TIMEOUT}; 0 no limit, and with --jobs 1 the files are then scanned in this process)",
    )
    sc.add_argument("--list-images", action="store_true", help="list every image's path (images are not text)")
    sc.add_argument("--out", help="write the full report here; the console then gets the summary only")
    sc.add_argument("--json", action="store_true", help="the report as JSON")
    sc.set_defaults(run=scan_cmd)

    ar = cl.add_parser(
        "allow-renew",
        help="after a human re-review, stamp the current markers hash on the named allow-list entries",
        description="Marks the named allow-list entries as reviewed under the current markers file. Nothing else "
        "renews an entry: an entry reviewed under older markers is checked again by every scan until renewed here.",
    )
    ar.add_argument("sha256", nargs="+", help="the entries' file hashes")
    ar.add_argument("--data", help="folder holding allow.json and markers.json")
    ar.add_argument("--allow", help="allow list (JSON); overrides --data")
    ar.add_argument("--markers", help="markers file (JSON); overrides --data")
    ar.set_defaults(run=allow_renew_cmd)

    vf = cl.add_parser(
        "verify",
        help="check every research page in a checkout: its verdict, two signatures and every hash (CI, pre-push)",
        description="For the working tree: every research/pages/<id>/page.md has its verdict and two valid "
        "page-reviewer signatures (allowed_signers read from --ref), every hash matches, every section and image is "
        "keep, page-check and markdownlint (version and rules read from --ref too) pass; every verdict file has "
        "its page; no stray files. One line per page, ids and results only. Exit 0 all passed, 1 a check failed, 2 "
        "could not run.",
    )
    vf.add_argument("--repo", help="the checkout (default: the one coney-tools runs from)")
    vf.add_argument("--ref", default=research.MAIN, help="the git ref allowed_signers is read from (default: main)")
    vf.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    npx_option(vf)
    vf.set_defaults(run=gate("cleanroom verify", lambda a: research.verify_repo(a.repo, a.ref, a.ssh_keygen, a.npx)))

    oc = cl.add_parser(
        "orig-check",
        help="check every @orig tag in the engine against the gated pages' Entry points (code gate, 8.4)",
        description="Reads every source file under the engine root of the working tree and every '@orig 0x<address> "
        "<job>' tag in its comments, each followed by '@page research/pages/<id>/page.md'. The address must be in "
        "that page's Entry points table with that job, and the page must verify at --ref (its verdict and two "
        "page-reviewer signatures). Every function defined in a game-behaviour folder needs a tag. The engine root, "
        "source extensions and game-behaviour folders are [tool.coney-tools.code-gate] in python/pyproject.toml, "
        "read from --ref. Prints paths, lines, rules, addresses and page ids, never a job or a line of code. Exit 0 "
        "passed, 1 a check failed, 2 could not run.",
    )
    oc.add_argument("--repo", help="the checkout (default: the one coney-tools runs from)")
    oc.add_argument(
        "--ref", default=research.MAIN, help="the git ref pages, allowed_signers and settings are read from (main)"
    )
    oc.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    oc.add_argument("--json", action="store_true", help="the findings, counts and gated pages as JSON")
    oc.add_argument("--rules", action="store_true", help="list the rules and exit")
    oc.set_defaults(run=orig_check_cmd)

    cs = cl.add_parser(
        "commit-sigs",
        help="check that every commit touching the engine is signed by an implementer (code gate, 8.4)",
        description="For every commit in the revision range that touches the engine root: it carries an SSH "
        "signature made in git's namespace by a key that allowed_signers (read from --ref) lists with "
        f'namespaces="git" under an implementer\'s principal ({signing.IMPLEMENTER}<n>). Prints commit ids and '
        "principals. Exit 0 every such commit is signed, 1 one is not, 2 could not run.",
    )
    cs.add_argument("range", help="the commits to check, as git rev-list takes them (main..HEAD)")
    cs.add_argument("--repo", help="the repository (default: the checkout coney-tools runs from)")
    cs.add_argument(
        "--ref", default=research.MAIN, help="the git ref allowed_signers and settings are read from (main)"
    )
    cs.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    cs.add_argument("--json", action="store_true", help="one entry per checked commit")
    cs.set_defaults(
        run=gate(
            "cleanroom commit-sigs",
            lambda a: codegate.commit_sigs(a.range, a.repo, a.ref, a.ssh_keygen, a.json),
        )
    )

    cv = cl.add_parser(
        "code-verdict",
        help="check that a PR analyst's signed verdict covers every changed engine source file (code gate, 8.4)",
        description="Every engine source file the range adds or changes (from the merge base of its ends) must be "
        f"listed with its exact blob by a verdict the range adds under {codeverdict.FOLDER}/, and every @orig "
        "address in it must be judged independent there. Each added verdict must be well formed, canonical, pass, "
        f"and be signed in the verdict namespace by a key allowed_signers (read from --ref) lists under "
        f"{signing.PR_ANALYST}<n>. Reads git objects only. Prints paths, addresses, verdict names and principals. "
        "Exit 0 passed, 1 a check failed, 2 could not run.",
    )
    cv.add_argument("range", help="the change to check, as A..B (main..HEAD)")
    cv.add_argument("--repo", help="the repository (default: the checkout coney-tools runs from)")
    cv.add_argument(
        "--ref", default=research.MAIN, help="the git ref allowed_signers and settings are read from (main)"
    )
    cv.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    cv.add_argument("--json", action="store_true", help="the changed files, the verdicts and every problem")
    cv.set_defaults(
        run=gate(
            "cleanroom code-verdict",
            lambda a: codeverdict.code_verdict(a.range, a.repo, a.ref, a.ssh_keygen, a.json),
        )
    )

    pf = cl.add_parser(
        "prefilter",
        help="the code gate's scripted pre-filter on a change's tagged functions (the analysis side)",
        description="For every @orig-tagged function in the engine source files the range A..B changes (git objects "
        "only), three rules: constant-not-on-page (a numeric literal whose value is not on the linked page at B, "
        "trivial constants aside), same-call-order (its calls to tagged functions, in order, are the original's "
        "tagged callees) and same-branch-count (its branch points number the original's conditional branches). "
        f"The facts file ({prefilter.FACTS_SCHEMA}) holds each judged address's callees and branch count; it is the "
        "analyst's and stays in the analysis zone. Settings are [tool.coney-tools.code-gate] in python/pyproject.toml "
        "at B. Prints rule names, paths and judged addresses, never a constant, a callee or a count. Exit 0 pass, "
        "1 flag, 2 could not run.",
    )
    pf.add_argument("range", help="the change to judge, as A..B (main..HEAD)")
    pf.add_argument("--facts", required=True, help=f"the facts file ({prefilter.FACTS_SCHEMA})")
    pf.add_argument("--out", help=f"write the result ({prefilter.RESULT_SCHEMA}) to this new file")
    pf.add_argument("--repo", help="the repository (default: the checkout coney-tools runs from)")
    pf.add_argument("--json", action="store_true", help="the result with each function's rules")
    pf.set_defaults(
        run=gate(
            "cleanroom prefilter",
            lambda a: prefilter.prefilter(a.range, a.facts, a.out, a.repo, a.json),
        )
    )

    cvs = cl.add_parser(
        "code-verdict-sign",
        help="a PR analyst signs a passing code verdict (the analysis side)",
        description=f"Checks the verdict file ({codeverdict.SCHEMA}) and writes it in canonical form with the "
        "principal's signature into --out (add-only). A malformed verdict, or one with any function not "
        "independent or a flagged pre-filter, is refused: only a passing verdict is signed. So is one the "
        f"pre-filter's result ({codeverdict.PREFILTER_SCHEMA}, from cleanroom prefilter --out) does not match: "
        "the same commit, outcome and functions. Exit 0 signed, 1 refused, 2 could not run.",
    )
    cvs.add_argument("verdict", help="the verdict, <name>.json")
    cvs.add_argument("--key", required=True, help="the PR analyst's private verdict key")
    cvs.add_argument("--principal", required=True, help=f"the key's principal ({signing.PR_ANALYST}<n>)")
    cvs.add_argument("--out", required=True, help="the folder the verdict and its signature go into")
    cvs.add_argument("--prefilter", required=True, help="the pre-filter's result for the verdict's commit")
    cvs.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    cvs.set_defaults(
        run=gate(
            "cleanroom code-verdict-sign",
            lambda a: codeverdict.sign(a.verdict, a.key, a.principal, a.out, a.prefilter, a.ssh_keygen),
        )
    )

    rg = top.add_parser("research", help="the page gate: verdicts, publish, import (clean-room page, section 6)")
    rc = rg.add_subparsers(dest="command", required=True)
    rv = rc.add_parser(
        "verdict",
        help="a page reviewer's verdict on a page: write and sign it, or countersign it",
        description="The first reviewer writes verdict.json in the page folder from a decisions file (a verdict for "
        "every section and image) and signs it, once the page passes page-check and markdownlint. The second "
        "reviewer, with --countersign, signs the same file only if every hash in it still matches the page and its "
        "images and the page still passes markdownlint. Exit 0 signed, 1 the page or the verdict failed a check, 2 "
        "could not run or refused.",
    )
    rv.add_argument("folder", help="the page folder in the gate (its name is the page id; it holds page.md)")
    rv.add_argument("--reviewer", required=True, help="your principal in allowed_signers, page-reviewer-<n>")
    rv.add_argument("--key", required=True, help="your private verdict key")
    rv.add_argument("--decisions", help='JSON: {"sections": {id: {"verdict", "kind"}}, "images": {path: verdict}}')
    rv.add_argument("--countersign", action="store_true", help="sign the existing verdict as the second reviewer")
    rv.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    npx_option(rv)
    rv.set_defaults(run=gate("research verdict", verdict_action))

    rp = rc.add_parser(
        "publish",
        help="analysis side: verify a page in the gate and copy it to the outbox",
        description="Verifies the page in the workspace's gate folder (allowed_signers from main of the workspace's "
        "repo) and copies exactly the page, its images and capture records, the verdict and its two signatures to "
        "<from_analyst>/pages/<id>-<first 12 hex of the verdict's hash>/. Exit 0 published, 1 the page failed a "
        "check, 2 could not run or refused.",
    )
    rp.add_argument("page", help="the page id")
    rp.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    rp.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    npx_option(rp)
    rp.set_defaults(
        run=gate(
            "research publish", lambda a: research.publish(a.page, workspace.load(a.workspace), a.ssh_keygen, a.npx)
        )
    )

    ri = rc.add_parser(
        "import",
        help="public side: verify a published page and commit it to a new branch page/<id>",
        description="Verifies the bundle (allowed_signers from main of the repo) and commits it to a new branch "
        "page/<id> made from main with git plumbing only: the working tree and the current branch are never "
        "touched. Exit 0 committed, 1 the bundle failed a check, 2 could not run or refused.",
    )
    ri.add_argument("bundle", nargs="?", help="the published folder, <id>-<hash>")
    ri.add_argument(
        "--all", action="store_true", help="import every new bundle in the workspace's outbox, going on after a failure"
    )
    ri.add_argument("--repo", help="the repository (default: the checkout coney-tools runs from)")
    ri.add_argument(
        "--workspace", help=f"with --all: workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)"
    )
    ri.add_argument("--ssh-keygen", help="ssh-keygen to use (default: the first on PATH)")
    npx_option(ri)
    ri.set_defaults(run=gate("research import", import_action))

    rs = rc.add_parser(
        "status",
        help="a role's inbox: the pages waiting for it (cheap: for a session polling on a timer)",
        description="Lists the pages in the gate or the outbox and the state each is in, as one role sees them: "
        "file presence and small reads only, no page-check, markdownlint or signature check. Prints ids, states, "
        "counts and verdict hash prefixes, never page text. Reviewers read the gate only; the research coordinator "
        "the gate and the outbox; public the outbox and the repo. Exit 0 nothing needs this role, 1 something does, "
        "2 could not run.",
    )
    rs.add_argument("--role", required=True, choices=list(research.ROLES), help="whose inbox")
    rs.add_argument("--json", action="store_true", help='{"role", "pages": [{"id", "state", "verdict"}], ...}')
    rs.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    rs.add_argument("--repo", help="--role public: the repository (default: the checkout coney-tools runs from)")
    rs.set_defaults(run=gate("research status", status_action))

    ib = top.add_parser(
        "inbox",
        help="what waits for a role, from every station it takes part in (for a session polling on a timer)",
        description="The role's items from every station that serves it (the page gate, the code gate, research "
        "requests), as the role may see "
        "them: ids, states and counts only, never page text. A role whose tools are not built yet is reported as "
        "inactive. Exit 0 nothing needs this role, 1 something does, 2 could not run.",
    )
    ib.add_argument("--role", required=True, help="a role of the workspace's roles table")
    ib.add_argument("--json", action="store_true", help='{"role", "active", "stations": {...}, "action": [...]}')
    ib.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    ib.set_defaults(run=gate("inbox", inbox_action))

    rq = top.add_parser("request", help="research requests: an implementer's question and its answer's state")
    rqc = rq.add_subparsers(dest="command", required=True)
    rf = rqc.add_parser(
        "file",
        help="file an implementer's questions (needs.json) as research requests (public side)",
        description=f"Checks the needs file ({needs.NEEDS}) and writes each chosen question as a canonical "
        f"{needs.REQUEST} file into <to_analyst>\\{needs.FOLDER}\\<id>-<8 hex>.json, the hex from the SHA-256 of "
        "the canonical question. Add-only: a question filed before is left as it is. Whether a question asks for "
        "behaviour only is for the one filing it to judge. Exit 0 filed, 1 refused (a malformed needs file or an "
        "--id it does not hold; nothing is filed), 2 could not run.",
    )
    rf.add_argument("needs", metavar="needs.json", help="an implementer's needs file, as impl collect copied it")
    rf.add_argument(
        "--id",
        action="append",
        default=[],
        dest="ids",
        metavar="ID",
        help="a question to file (repeatable; default: every question)",
    )
    rf.add_argument("--task", help="the name of the task file the questions came from")
    rf.add_argument("--repo", help="the repository whose main holds the pages the questions name (default: repo)")
    rf.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    rf.set_defaults(
        run=gate("request file", lambda a: needs.file(workspace.load(a.workspace), a.needs, a.ids, a.task, a.repo))
    )
    rt = rqc.add_parser(
        "status",
        help="record a research request's new state (analysis side)",
        description=f"Writes a canonical {needs.STATUS} file, <from_analyst>\\{needs.FOLDER}\\<request>.<state>"
        ".json: ids, a state and a time, no free text. A request moves from new to accepted, then to answered "
        "(naming the page that answers it) or declined; add-only, one file per change. Exit 0 written (or the same "
        "state again: nothing written), 1 refused (no such request, or a state that does not move it on), 2 could "
        "not run.",
    )
    rt.add_argument("request", help="the request id, <id>-<8 hex>")
    rt.add_argument("--state", required=True, choices=list(needs.STATES))
    rt.add_argument("--page", help="--state answered: the id of the page that answers it")
    rt.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    rt.set_defaults(
        run=gate(
            "request status",
            lambda a: needs.set_status(workspace.load(a.workspace), a.request, a.state, a.page),
        )
    )

    zn = top.add_parser("zone", help="this account's zone, as its Claude Code mod shows it")
    znc = zn.add_subparsers(dest="command", required=True)
    zs = znc.add_parser(
        "status",
        help="this account's zone, role, inbox, walls and (public) pages in main, in one read",
        description="What the zone's mod draws in its pane and wakes the session on: the zone and role of the "
        "account running it, the role's inbox (ids, states and counts, never page text), the last zones check and "
        "the breach flag, and on the public side the number of research pages in main. Exit 0 nothing needs this "
        "role, 1 something does, 2 could not run.",
    )
    zs.add_argument("--json", action="store_true", help="the whole status as one JSON object")
    zs.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    zs.set_defaults(run=gate("zone status", zone_status_action))

    rl = top.add_parser("roles", help="the pipeline's roles (docs/workflow.md)")
    rlc = rl.add_subparsers(dest="command", required=True)
    rk = rlc.add_parser(
        "check",
        help="check that every role in the workspace's table is fully wired",
        description="Every role has its brief, a valid zone and loop interval, and, when active, an inbox. The "
        "installer runs it. Exit 0 every role wired, 1 something missing, 2 could not run.",
    )
    rk.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    rk.set_defaults(run=gate("roles check", roles_check_action))

    tt = top.add_parser("tether", help="Tether's per-zone settings (docs/workflow.md)")
    ttc = tt.add_subparsers(dest="command", required=True)
    ws_help = rf"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\Coney\)"
    tk = ttc.add_parser(
        "check",
        help="check the workspace's tether table (and the installed copy)",
        description="Every zone and the containers have a config; outside the GitHub zones, network is off, "
        "plainOutput on and no prs poller; every zone's mcp_allow lists tether. With --installed, the installed "
        "Tether is the table's commit and every installed config matches the table. The installer runs it. Exit 0 "
        "in order, 1 a problem, 2 could not run.",
    )
    tk.add_argument("--installed", action="store_true", help="also compare the installed copy with the table")
    tk.add_argument("--workspace", help=ws_help)
    tk.set_defaults(run=gate("tether check", tether_check_action))
    tw = ttc.add_parser(
        "write",
        help="write every zone's locked config into a folder",
        description="One <zone>.json per zone and container.json, rendered from the table (the installer writes "
        "them into the machine folder). Exit 0 written, 2 could not run.",
    )
    tw.add_argument("--out", required=True, help="the folder to write into")
    tw.add_argument("--workspace", help=ws_help)
    tw.set_defaults(run=gate("tether write", tether_write_action))
    tsh = ttc.add_parser("show", help="print one zone's rendered config", description="Exit 0, 2 could not run.")
    tsh.add_argument("name", help="a zone of the workspace, or container")
    tsh.add_argument("--workspace", help=ws_help)
    tsh.set_defaults(run=gate("tether show", tether_show_action))

    zg = top.add_parser("zones", help="the zones' walls (spec 5.1)")
    zc = zg.add_subparsers(dest="command", required=True)
    zk = zc.add_parser(
        "check",
        help="check that every zone's walls stand",
        description="public: what this account can list and reach (zone folders, home folders, the analyst's local "
        "ports, tested with a listener of its own on a free port), plus installed-copy drift as warnings. full "
        "(elevated or SYSTEM): every zone folder's owner, inheritance and explicit entries against the zones file, "
        "and the port wall's filters, and the analysis zone scanned for exact copies of older Coney files outside the "
        "research sweep's quarantine (a legacy copy, not a breach). Prints paths and verdicts, never file contents. "
        "Exit 0 all checks passed, 1 a check failed, 2 could not run.",
    )
    zk.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    zk.add_argument("--mode", choices=("public", "full"), help="default: full when elevated, else public")
    zk.add_argument("--json", action="store_true", help="the result as JSON")
    zk.add_argument(
        "--write-state",
        action="store_true",
        help="write the result to the workspace's check_result and, on a breach, the breach flag (only an "
        "elevated full run clears the flag)",
    )
    zk.add_argument("--cache", metavar="PATH", help="also write the JSON result here")
    zk.set_defaults(run=zones_check_cmd)

    zp = zc.add_parser(
        "ports",
        help="the port wall: filters that keep every account but the analyst side off the analyst's ports",
        description="Windows Filtering Platform filters, persistent, under Coney's own provider and sublayer: for "
        "each analyst port, connections to it are blocked for every account except those whose zone folder lies "
        "in the analyst root, and connections that do not come from this machine are blocked for everyone. "
        "--apply replaces Coney's filters in one transaction (running it again changes nothing); --remove takes "
        "them all away. Both need elevation. Exit 0 done, 2 could not run.",
    )
    zp.add_argument("--workspace", help=f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)")
    act = zp.add_mutually_exclusive_group(required=True)
    act.add_argument("--apply", action="store_true", help="write the filters for the workspace's analyst ports")
    act.add_argument("--remove", action="store_true", help="remove every filter, the sublayer and the provider")
    act.add_argument("--list", action="store_true", help="show Coney's filters (listing may need elevation)")
    zp.set_defaults(run=zones_ports_cmd)

    ig = top.add_parser("impl", help="the implementer containers (playbook 7; clean-room page, section 4)")
    ic = ig.add_subparsers(dest="command", required=True)
    ws_help = f"workspace file (default: ${workspace.ENV}, else %%ProgramData%%\\Coney\\)"

    def impl_parser(name, help, description, action, names=None):
        p = ic.add_parser(name, help=help, description=description + " Exit 0 done, 1 a check failed, 2 could not run.")
        p.add_argument("--workspace", help=ws_help)
        if names:
            p.add_argument("names", nargs="*", metavar="impl-N", help=names)
        p.set_defaults(run=impl_cmd(name, action))
        return p

    impl_parser(
        "build",
        "build the implementer image in the WSL2 distro",
        "Copies the checkout's containers/ folder into <wsl_root>/build in the distro and builds the image compose.yml "
        "names there.",
        lambda a, ws: impl.build(ws),
    )
    impl_parser(
        "up",
        "check the hardening of compose.yml and start containers",
        "Renders compose.yml, checks every service against the hardening rules and refuses to start anything if one "
        "fails; then starts the containers (each behind its firewall, which checks itself before it reports "
        "healthy). On a container's first start it makes its Ed25519 signing key and prints its allowed_signers line.",
        lambda a, ws: impl.up(ws, a.names),
        names="containers to start (default: the whole pool)",
    )
    impl_parser(
        "down",
        "stop and remove containers (their volumes and keys stay)",
        "Stops and removes the containers and their firewalls; named volumes, keys and outboxes stay.",
        lambda a, ws: impl.down(ws, a.names),
        names="containers to stop (default: the whole pool)",
    )
    impl_parser(
        "status",
        "each container's state, firewall, key fingerprint, caps and current task",
        "One line per container of the pool.",
        lambda a, ws: impl.status(ws),
    )
    rs = impl_parser(
        "reset",
        "remove a container with all its volumes, its key and its folders in the distro",
        "Removes the container, its firewall, its named volumes (the clone and the signing key) and its outbox, "
        "artifacts, transcripts and task folders in the distro. Refused while its outbox holds work not yet "
        "collected, unless --force. Its key's line must then leave allowed_signers.",
        lambda a, ws: impl.reset(ws, a.name, a.force),
    )
    rs.add_argument("name", metavar="impl-N")
    rs.add_argument("--force", action="store_true", help="reset even when its outbox holds work not yet collected")
    ru = impl_parser(
        "run",
        "run one task in a container: a fresh worktree on a new branch, Claude Code until it ends",
        "Copies the task file in (read-only), refreshes the container's clone from main of the collecting repo "
        "(itself fetched from the workspace's repo), makes a fresh worktree on a new branch and runs Claude Code on "
        "the task without a terminal; returns when it ends. Needs the auth file (containers.auth_file). Exit 0 the "
        "session ended well and the branch is in the outbox, 1 it did not, 2 could not run.",
        lambda a, ws: impl.run(ws, a.name, a.task, a.branch, base=a.base),
    )
    ru.add_argument("name", metavar="impl-N")
    ru.add_argument("--task", required=True, help="the task file (Markdown)")
    ru.add_argument("--branch", required=True, help="a new branch name for the work")
    ru.add_argument(
        "--base",
        help="a branch of the collecting repo (impl-N/<branch>) to stand in for main: build on work not merged yet",
    )
    impl_parser(
        "collect",
        "copy outboxes, artifacts and transcripts out to the public zone",
        "Fetches each outbox's branches into <containers>\\outbox\\coney.git as <impl-N>/<branch> (from a bundle "
        "streamed out of the distro), and copies artifacts and transcripts into <containers>\\artifacts\\<impl-N> "
        "and <containers>\\archive\\<impl-N> (plain files and folders only).",
        lambda a, ws: impl.collect(ws, a.names),
        names="containers to collect from (default: the whole pool)",
    )
    lg = impl_parser(
        "log",
        "show the implementers' live logs (any zone that may read the live folder)",
        "Lists every implementer with a live log in <containers>\\live (its task, branch, start, end and exit) and "
        "prints the last lines of one: the named implementer's, else the one started last. Each line is a UTC time, "
        "a kind (text, a tool's name, result, done) and a one-line summary.",
        lambda a, ws: impl.log(ws, a.name, a.tail, a.json),
    )
    lg.add_argument("name", nargs="?", metavar="impl-N", help="whose log (default: the one started last)")
    lg.add_argument("--tail", type=int, default=implog.TAIL, help=f"lines to show (default {implog.TAIL})")
    lg.add_argument("--json", action="store_true", help="the implementers, the one shown and its lines, as JSON")
    impl_parser(
        "check",
        "check the hardening and isolation of running containers, live",
        "For each running container (or those named): its firewall is healthy; docker inspect agrees with the "
        "rules; git signs with its key; and from inside, as impl: a host off the allowlist is refused, each "
        "allowed host connects, the Windows host's NAT address does not on the analyst ports, there is no Docker "
        "socket, no capability, no_new_privs and seccomp are on, and the root filesystem is read-only.",
        lambda a, ws: impl.check(ws, a.names),
        names="containers to check (default: every running one)",
    )

    cg = top.add_parser(
        "ci",
        help="run CI's jobs, the same way GitHub Actions runs them (docs/ci.md)",
        description="Runs the named jobs (every job when none is named) on the checkout, each as a workflow runs it: "
        "the workflows call `ci --job <name>`, so a local run and CI cannot drift. Exit 0 every job passed, 1 a job "
        "failed, 2 a job could not run.",
    )
    cg.add_argument("--job", action="append", default=[], metavar="NAME", help="a job to run (repeatable)")
    cg.add_argument("--list", action="store_true", help="list the jobs and exit")
    cg.add_argument(
        "--versions", action="store_true", help="print each pinned tool version as name=value (for $GITHUB_OUTPUT)"
    )
    cg.add_argument(
        "--install-node",
        metavar="DIR",
        help="download the Node containers/Dockerfile pins, check its SHA-256, unpack it into DIR and print its bin "
        "folder (Linux x64; for a workflow's $GITHUB_PATH)",
    )
    cg.add_argument("--repo", help="the checkout the jobs check (default: the one coney-tools runs from)")
    cg.add_argument(
        "--ref",
        default=research.MAIN,
        help="verify, orig-check, commit-sigs: the git ref allowed_signers, pages and settings are read from (in CI "
        "the base branch, never the change); also the base of the default range (default: main)",
    )
    cg.add_argument("--range", help="disclosure, commit-sigs: the commits to check (default: <ref>..HEAD)")
    cg.add_argument(
        "--site-dir",
        metavar="DIR",
        help="site: build the site into DIR (default: build/ci/site; pages.yml deploys what it builds)",
    )
    cg.add_argument(
        "--text",
        action="append",
        default=[],
        metavar="FILE",
        help="disclosure: also check this text file (a pull request's title or body; repeatable)",
    )
    cg.set_defaults(run=ci_cmd)

    a = ap.parse_args(argv)
    return a.run(a)
