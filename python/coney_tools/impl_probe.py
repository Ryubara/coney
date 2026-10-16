"""The live isolation checks of `coney-tools impl check`, run inside an implementer container.

coney-tools sends this file's text to the container's python3 on standard input (`docker exec -i ... python3 -
ARGS`), so what runs is the checked-in code, never a copy in the image the session could have shaped. Standard
library only, Python 3.10 or later. It prints one JSON list of {"what", "ok", "got"} (and "warn" on a check that
is reported but does not fail) and exits 0 whatever the checks found: coney-tools judges them.

ARGS is one JSON object: {"denied": host, "allowed_file": path, "gateway": address or null, "ports": [port, ...]}.
"""

import errno
import json
import os
import socket
import subprocess
import sys

TIMEOUT = 5  # seconds per connection attempt
DOCKER_SOCKETS = ("/var/run/docker.sock", "/run/docker.sock")
CAPS = ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb")


def connects(host, port):
    """'connected', or why not."""
    try:
        with socket.create_connection((host, port), timeout=TIMEOUT):
            return "connected"
    except TimeoutError:
        return "timed out"
    except OSError as e:
        return errno.errorcode.get(e.errno, e.__class__.__name__) if e.errno else e.__class__.__name__


def status_fields():
    out = {}
    with open("/proc/self/status", encoding="ascii", errors="replace") as f:
        for line in f:
            k, _, v = line.partition(":")
            out[k] = v.strip()
    return out


def root_read_only():
    opts = []
    try:
        with open("/proc/self/mounts", encoding="utf-8", errors="replace") as f:
            for line in f:
                parts = line.split()
                if len(parts) > 3 and parts[1] == "/":
                    opts = parts[3].split(",")  # the last mount on / is the one in effect
    except OSError:
        pass
    root = "ro" in opts
    probe = f"/.coney-probe-{os.getpid()}"
    try:
        with open(probe, "w"):
            pass
        os.unlink(probe)
        wrote = "write succeeded"
    except OSError as e:
        wrote = errno.errorcode.get(e.errno, "error")
    return root and wrote == "EROFS", f"mounted {'ro' if root else 'rw'}, write: {wrote}"


def main(args):
    checks = []

    def check(what, ok, got):
        checks.append({"what": what, "ok": bool(ok), "got": got})

    got = connects(args["denied"], 443)
    check(f"egress to {args['denied']}:443 is refused", got != "connected", got)
    with open(args["allowed_file"], encoding="utf-8") as f:
        hosts = [x.strip() for x in f if x.strip() and not x.strip().startswith("#")]
    check("the allowed hosts are listed", hosts, ", ".join(hosts) or "none")
    for h in hosts:
        got = connects(h, 443)
        check(f"egress to {h}:443 connects", got == "connected", got)
    gw = args.get("gateway")
    check("the Windows host's NAT address is known", gw, gw or "unknown")
    if gw:
        for p in args["ports"]:
            got = connects(gw, p)
            check(f"the Windows host {gw}:{p} does not connect", got != "connected", got)
    for s in DOCKER_SOCKETS:
        present = os.path.lexists(s)
        check(f"no {s}", not present, "present" if present else "absent")
    st = status_fields()
    check("not root", os.getuid() != 0, f"uid {os.getuid()}")
    for c in CAPS:
        v = st.get(c, "?")
        check(f"{c} is empty", v.strip("0") == "" and v != "", v)
    check("no_new_privs is set", st.get("NoNewPrivs") == "1", st.get("NoNewPrivs", "?"))
    check("a seccomp filter is on", st.get("Seccomp") == "2", st.get("Seccomp", "?"))
    ok, got = root_read_only()
    check("the root filesystem is read-only", ok, got)
    # Decided 2026-10-16: Docker's default seccomp profile stays and Claude Code's own sandbox is off (it needs new
    # namespaces, which that profile refuses). The container is the sandbox; check that the refusal holds.
    try:
        r = subprocess.run(["unshare", "--user", "true"], capture_output=True, timeout=20, check=False)
        got = "refused" if r.returncode != 0 else "allowed"
    except (OSError, subprocess.TimeoutExpired) as e:
        got = e.__class__.__name__
    check("new user namespaces are refused", got == "refused", got)
    return checks


if __name__ == "__main__":
    try:
        result = main(json.loads(sys.argv[1]))
    except Exception as e:  # noqa: BLE001 -- report, never a traceback
        result = [{"what": "the probe ran", "ok": False, "got": e.__class__.__name__}]
    print(json.dumps(result))
