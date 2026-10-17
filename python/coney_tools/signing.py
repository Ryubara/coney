"""Signatures: ssh-keygen's file signing (`ssh-keygen -Y`), in Coney's verdict namespace for verdicts and in git's
own namespace for commits.

A signature is checked against the `allowed_signers` file read from a git ref of the repository (`main` by default),
never from a working tree, so a change can never add its own key and sign with it. Every call names the namespace:
a key listed for another purpose (`namespaces="git"` for commit keys) never verifies a verdict, and a verdict key
never verifies a commit.

ssh-keygen and git are found on PATH only, the way the scan finds git: never in the current folder.
"""

import base64
import hashlib
import os
import re
import subprocess
from pathlib import Path

from coney_tools.cleanroom import scan

NAMESPACE = "coney-verdict"
COMMIT_NAMESPACE = "git"  # the namespace git signs commits in
IMPLEMENTER = "impl-"  # commit keys count only under these principals: impl-N, the implementer containers' names
PR_ANALYST = "pr-analyst-"  # code verdicts count only under these principals: pr-analyst-N
ALLOWED_SIGNERS = "allowed_signers"  # at the repository's root
PRINCIPAL = re.compile(r"^[a-z0-9][a-z0-9-]{0,63}$")
TIMEOUT = 120  # seconds one ssh-keygen or git call may take
ARMOR = (b"-----BEGIN SSH SIGNATURE-----", b"-----END SSH SIGNATURE-----")


class SigningError(Exception):
    """ssh-keygen or git is missing, failed or gave something unexpected (the command could not run: exit 2)."""


class BadSignature(Exception):
    """A signature that does not count: the message says why, naming no content."""


def lf(data):
    """Bytes with CRLF and CR line ends turned into LF."""
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def program(name, given=None):
    """The program's path: the one given (--ssh-keygen), else the first on PATH."""
    if given:
        if not Path(given).is_file():
            raise SigningError(f"{name} not found at {given}")
        return str(given)
    found = scan.find_program(name)
    if not found:
        raise SigningError(f"{name} not found on PATH")
    return found


def run(cmd, data=None, env=None):
    """subprocess.run without a console window; a missing program or a timeout raises SigningError."""
    try:
        return subprocess.run(
            cmd,
            input=data,
            capture_output=True,
            env=env,
            creationflags=scan.NO_WINDOW,
            timeout=TIMEOUT,
            check=False,
        )
    except subprocess.TimeoutExpired:
        raise SigningError(f"{Path(cmd[0]).name} took over {TIMEOUT} s and was stopped") from None
    except OSError as e:
        raise SigningError(f"cannot run {cmd[0]}: {e.__class__.__name__}") from None


# --- git ----------------------------------------------------------------------------------------------------------


def git_env(**extra):
    """The caller's environment without any GIT_ variable (which could point git at other objects or another index),
    with replace objects, lazy fetching and prompts off. The user's own git configuration still applies (its
    identity names the commits that import makes)."""
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith("GIT_")}
    env.update(GIT_NO_REPLACE_OBJECTS="1", GIT_NO_LAZY_FETCH="1", GIT_TERMINAL_PROMPT="0", **extra)
    return env


def git(exe, repo, *args, data=None, env=None, ok=(0,)):
    """Runs git in the repository; its standard output, or SigningError naming the git command."""
    r = run([exe, "-C", str(repo), *args], data, env or git_env())
    if r.returncode not in ok:
        detail = r.stderr.decode("utf-8", "replace").strip().splitlines()
        raise SigningError(f"git {args[0]} failed" + (f": {detail[-1]}" if detail else ""))
    return r


def allowed_signers(exe, repo, ref):
    """The allowed_signers file as it stands at the ref (LF line ends). Read with cat-file: plumbing, no textconv,
    no pager, never the working tree."""
    if not ref or ref.startswith("-"):
        raise SigningError(f"not a git ref: {ref!r}")
    r = git(exe, repo, "cat-file", "blob", f"{ref}:{ALLOWED_SIGNERS}", ok=(0, 128))
    if r.returncode:
        raise SigningError(f"no {ALLOWED_SIGNERS} at {ref} in {repo}")
    return lf(r.stdout)


# --- ssh-keygen ---------------------------------------------------------------------------------------------------


def sign(exe, key, data):
    """Signs the bytes (on standard input, so no .sig file appears anywhere) with the private key, in the verdict
    namespace. The armored signature, with LF line ends."""
    if not Path(key).is_file():
        raise SigningError(f"no private key at {key}")
    r = run([exe, "-Y", "sign", "-n", NAMESPACE, "-f", str(key), "-"], data)
    sig = lf(r.stdout)
    if r.returncode or not sig.startswith(ARMOR[0]):
        raise SigningError(f"ssh-keygen could not sign with {key} (exit {r.returncode})")
    return sig


def key_fingerprint(sig):
    """SHA-256 of the public key a signature carries (SSHSIG format), as ssh-keygen prints it. BadSignature when the
    signature cannot be read."""
    lines = lf(sig).strip().split(b"\n")
    if len(lines) < 3 or lines[0] != ARMOR[0] or lines[-1] != ARMOR[1]:
        raise BadSignature("not an SSH signature")
    try:
        blob = base64.b64decode(b"".join(lines[1:-1]), validate=True)
    except ValueError:
        raise BadSignature("not an SSH signature") from None
    if blob[:6] != b"SSHSIG" or len(blob) < 14:
        raise BadSignature("not an SSH signature")
    n = int.from_bytes(blob[10:14], "big")  # after the magic and the version: the public key, length-prefixed
    key = blob[14 : 14 + n]
    if not n or len(key) != n:
        raise BadSignature("not an SSH signature")
    return "SHA256:" + base64.b64encode(hashlib.sha256(key).digest()).decode().rstrip("=")


KEY_LINE = re.compile(
    r'^(\S+)\s+((?:[^\s"]|"[^"]*")*\s+)?((?:ssh|ecdsa|sk)-[A-Za-z0-9@.-]+)\s+([A-Za-z0-9+/=]+)(?:\s|$)'
)


def listed_keys(allowed):
    """[(principals, namespaces, fingerprint)] for every key line of an allowed_signers file's bytes: its principals
    as a list, the namespaces it is limited to (None when the line names none) and its key's SHA-256 fingerprint as
    key_fingerprint gives it. Lines that cannot be read are left out."""
    out = []
    for line in lf(allowed).decode("utf-8", "replace").split("\n"):
        m = KEY_LINE.match(line.strip())
        if not m or line.strip().startswith("#"):
            continue
        names = re.search(r'(?:^|,)namespaces="([^"]*)"', (m.group(2) or "").strip())
        try:
            blob = base64.b64decode(m.group(4), validate=True)
        except ValueError:
            continue
        fingerprint = "SHA256:" + base64.b64encode(hashlib.sha256(blob).digest()).decode().rstrip("=")
        out.append((m.group(1).split(","), set(names.group(1).split(",")) if names else None, fingerprint))
    return out


def verify(exe, allowed_path, data, sig, principal, tmp, namespace=NAMESPACE):
    """Checks one signature over the bytes: made by the principal's key as allowed_signers lists it, in the namespace
    (the verdict namespace unless another is named). The key's fingerprint, or BadSignature. tmp is a private folder
    for the signature's copy."""
    fingerprint = key_fingerprint(sig)
    sig_path = Path(tmp) / f"{principal}.sig"
    sig_path.write_bytes(lf(sig))
    # find-principals names only the first principal a key is listed under, so it shows the key is listed at all;
    # verify then decides whether it is listed for this principal and this namespace.
    r = run([exe, "-Y", "find-principals", "-s", str(sig_path), "-f", str(allowed_path)])
    if r.returncode or not r.stdout.strip():
        raise BadSignature(f"its key is not in {ALLOWED_SIGNERS}")
    r = run([exe, "-Y", "verify", "-f", str(allowed_path), "-I", principal, "-n", namespace, "-s", str(sig_path)], data)
    if r.returncode:
        purpose = "verdicts" if namespace == NAMESPACE else "commits"
        raise BadSignature(
            f"does not verify for {principal} (changed content, another principal's key, or a key not allowed for "
            f"{purpose})"
        )
    return fingerprint
