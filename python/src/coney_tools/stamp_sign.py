# SPDX-License-Identifier: GPL-3.0-or-later
"""Signing and verifying stamps with `ssh-keygen -Y` (docs/guides/stamps.md#signing).

Every `ssh-keygen` call of Coney lives in this file, in two functions with one argv each, so a hook that keeps agents
from signing has a single command line to recognise. The key is a dedicated ed25519 key per maintainer: its private
half stays in the SSH agent (this module never reads it), its public half is a line of `.github/stamp-signers`
(allowed-signers format). The namespace is `stamps.namespace` of `.github/conventions.yml`, so a signature made for
anything else (a git commit, a release) cannot pass for a stamp.
"""

from __future__ import annotations

import subprocess
import tempfile
from collections.abc import Callable
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

#: How the functions run a program: `subprocess.run`, replaceable in tests.
Runner = Callable[..., "subprocess.CompletedProcess[bytes]"]


class SignError(Exception):
    """A signature could not be made or did not verify."""


def _run(argv: list[str], data: bytes, run: Runner) -> subprocess.CompletedProcess[bytes]:
    """Run `argv` with `data` on stdin; SignError when ssh-keygen is missing."""
    try:
        return run(argv, input=data, capture_output=True, check=False)
    except OSError as error:
        raise SignError(f"ssh-keygen cannot be run ({error}); install OpenSSH") from error


def _message(done: subprocess.CompletedProcess[bytes]) -> str:
    """The first line ssh-keygen said, for an error."""
    text = (done.stderr or done.stdout or b"").decode("utf-8", "replace").strip()
    return text.splitlines()[-1] if text else f"exit {done.returncode}"


def key_label(public_key: Path) -> str:
    """The comment of a public key file (`ssh-ed25519 AAAA... comment`), or "" for none: a signer name to show."""
    fields = public_key.read_text(encoding="utf-8").split()
    return fields[2] if len(fields) > 2 else ""


def sign(body: bytes, key: Path, namespace: str, use_agent: bool = True, run: Runner = subprocess.run) -> str:
    """The armored SSH signature of `body` under `namespace`.

    With `use_agent` (the default) `key` is the *public* key file and the private half is taken from the SSH agent
    (`-U`); without it `key` is a private key file, which only tests use (with throwaway keys).
    """
    argv = ["ssh-keygen", "-Y", "sign", "-f", str(key), "-n", namespace, *(["-U"] if use_agent else [])]
    done = _run(argv, body, run)
    if done.returncode != 0 or not done.stdout.strip():
        raise SignError(f"ssh-keygen could not sign: {_message(done)}")
    return done.stdout.decode("utf-8")


def _when(moment: datetime) -> str:
    """`-Overify-time=` value: the stamp's moment in UTC, so a retired key still verifies what it signed."""
    return "-Overify-time=" + moment.astimezone(UTC).strftime("%Y%m%d%H%M%SZ")


def verify(
    body: bytes, armor: str, allowed_signers: Path, namespace: str, when: datetime, run: Runner = subprocess.run
) -> str:
    """The principal of `allowed_signers` whose key signed `body` under `namespace` valid at `when`; SignError if none.

    The principal comes from ssh-keygen's own lookup, never from a field of the stamp.
    """
    if not allowed_signers.is_file():
        raise SignError(f"{allowed_signers}: the signers file is missing")
    sig_file = _signature_file(armor)
    try:
        found = _run(
            ["ssh-keygen", "-Y", "find-principals", "-s", str(sig_file), "-f", str(allowed_signers), _when(when)],
            b"",
            run,
        )
        principals = found.stdout.decode("utf-8", "replace").split()
        if found.returncode != 0 or not principals:
            raise SignError("no key of the signers file made this signature (or its key was retired before it)")
        who = principals[0]
        done = _run(
            [
                "ssh-keygen",
                "-Y",
                "verify",
                "-f",
                str(allowed_signers),
                "-I",
                who,
                "-n",
                namespace,
                "-s",
                str(sig_file),
                _when(when),
            ],
            body,
            run,
        )
        if done.returncode != 0:
            raise SignError(f"the signature does not verify: {_message(done)}")
        return who
    finally:
        sig_file.unlink(missing_ok=True)


def _signature_file(armor: str) -> Path:
    """The armored signature as a temporary file (ssh-keygen reads it by name); the caller deletes it."""
    with tempfile.NamedTemporaryFile("w", suffix=".sig", delete=False, encoding="utf-8", newline="\n") as handle:
        handle.write(armor)
    return Path(handle.name)


Signer = Callable[[bytes], dict[str, Any]]


def signer_for(key: Path | None, namespace: str, run: Runner = subprocess.run) -> Signer | None:
    """A function that signs a stamp's body with the maintainer's key, or None when no key is configured (the stamp is
    then written unsigned)."""
    if key is None:
        return None
    label = key_label(key) if key.is_file() else ""

    def make(body: bytes) -> dict[str, Any]:
        return {"signer": label, "armor": sign(body, key, namespace, run=run)}

    return make
