# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests for `ssh-keygen -Y` signing and verifying of stamps, with throwaway keys only."""

import subprocess
from datetime import UTC, datetime
from pathlib import Path
from typing import Any

import pytest
from stamp_support import allowed_signers, make_key, needs_ssh_keygen

from coney_tools import stamp_sign
from coney_tools.stamp_sign import SignError

NOW = datetime(2026, 10, 22, 12, 0, 0, tzinfo=UTC)


class Recorder:
    """A stand-in for subprocess.run that records argv and answers with a canned result."""

    def __init__(self, returncode: int = 0, out: bytes = b"", err: bytes = b"") -> None:
        self.calls: list[tuple[list[str], bytes | None]] = []
        self.result = subprocess.CompletedProcess([], returncode, out, err)

    def __call__(self, argv: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
        self.calls.append((argv, kwargs.get("input")))
        return self.result


def test_sign_asks_the_agent_for_the_key_of_a_public_file() -> None:
    run = Recorder(out=b"-----BEGIN SSH SIGNATURE-----\nabc\n-----END SSH SIGNATURE-----\n")
    armor = stamp_sign.sign(b"body", Path("k.pub"), "coney-stamp", run=run)
    assert armor.startswith("-----BEGIN SSH SIGNATURE-----")
    argv, data = run.calls[0]
    assert argv[:2] == ["ssh-keygen", "-Y"] and argv[2] == "sign"
    assert "-U" in argv and argv[argv.index("-n") + 1] == "coney-stamp" and argv[argv.index("-f") + 1] == "k.pub"
    assert data == b"body"


def test_sign_failure_names_the_reason() -> None:
    run = Recorder(returncode=255, err=b"Couldn't find key in agent\n")
    with pytest.raises(SignError, match="agent"):
        stamp_sign.sign(b"body", Path("k.pub"), "coney-stamp", run=run)


def test_missing_ssh_keygen_is_a_sign_error() -> None:
    def gone(argv: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
        raise FileNotFoundError("ssh-keygen")

    with pytest.raises(SignError, match="ssh-keygen"):
        stamp_sign.sign(b"x", Path("k.pub"), "coney-stamp", run=gone)


def test_verify_asks_for_the_principal_and_the_time_of_the_stamp(tmp_path: Path) -> None:
    class Two(Recorder):
        def __call__(self, argv: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
            self.calls.append((argv, kwargs.get("input")))
            return subprocess.CompletedProcess([], 0, b"alice\n" if "find-principals" in argv else b"Good\n", b"")

    run = Two()
    signers = tmp_path / "allowed"
    signers.write_text('alice namespaces="coney-stamp" ssh-ed25519 AAAA\n', encoding="utf-8")
    who = stamp_sign.verify(b"body", "ARMOR", signers, "coney-stamp", NOW, run=run)
    assert who == "alice"
    find, check = (c[0] for c in run.calls)
    assert "find-principals" in find and "-Overify-time=20261022120000Z" in find
    assert check[check.index("-I") + 1] == "alice" and "-Overify-time=20261022120000Z" in check
    assert check[check.index("-n") + 1] == "coney-stamp"


@needs_ssh_keygen
def test_a_real_signature_verifies_and_a_changed_body_does_not(tmp_path: Path) -> None:
    key = make_key(tmp_path)
    signers = allowed_signers(tmp_path / "signers", ("alice", key))
    armor = stamp_sign.sign(b"body", key.private, "coney-stamp", use_agent=False)
    assert stamp_sign.verify(b"body", armor, signers, "coney-stamp", NOW) == "alice"
    with pytest.raises(SignError):
        stamp_sign.verify(b"other", armor, signers, "coney-stamp", NOW)


@needs_ssh_keygen
def test_another_key_or_namespace_or_an_empty_file_is_refused(tmp_path: Path) -> None:
    key = make_key(tmp_path / "a")
    stranger = make_key(tmp_path / "b", comment="mallory")
    signers = allowed_signers(tmp_path / "signers", ("alice", key))
    armor = stamp_sign.sign(b"body", stranger.private, "coney-stamp", use_agent=False)
    with pytest.raises(SignError):
        stamp_sign.verify(b"body", armor, signers, "coney-stamp", NOW)
    mine = stamp_sign.sign(b"body", key.private, "other-namespace", use_agent=False)
    with pytest.raises(SignError):
        stamp_sign.verify(b"body", mine, signers, "coney-stamp", NOW)
    with pytest.raises(SignError, match="signers"):
        stamp_sign.verify(b"body", mine, tmp_path / "missing", "coney-stamp", NOW)


@needs_ssh_keygen
def test_a_retired_key_still_verifies_what_it_signed_before(tmp_path: Path) -> None:
    key = make_key(tmp_path)
    signers = allowed_signers(
        tmp_path / "signers", ("alice", key), options='namespaces="coney-stamp",valid-before="20260101000000Z"'
    )
    armor = stamp_sign.sign(b"body", key.private, "coney-stamp", use_agent=False)
    before = datetime(2025, 6, 1, tzinfo=UTC)
    assert stamp_sign.verify(b"body", armor, signers, "coney-stamp", before) == "alice"
    with pytest.raises(SignError):
        stamp_sign.verify(b"body", armor, signers, "coney-stamp", NOW)


def test_signer_label_is_the_key_comment(tmp_path: Path) -> None:
    pub = tmp_path / "k.pub"
    pub.write_text("ssh-ed25519 AAAAC3 alice@laptop\n", encoding="utf-8")
    assert stamp_sign.key_label(pub) == "alice@laptop"
    pub.write_text("ssh-ed25519 AAAAC3\n", encoding="utf-8")
    assert stamp_sign.key_label(pub) == ""


def test_signer_for_signs_with_the_configured_public_key_and_without_one_gives_none(tmp_path: Path) -> None:
    assert stamp_sign.signer_for(None, "coney-stamp") is None
    pub = tmp_path / "k.pub"
    pub.write_text("ssh-ed25519 AAAAC3 alice@laptop\n", encoding="utf-8")
    run = Recorder(out=b"-----BEGIN SSH SIGNATURE-----\nabc\n-----END SSH SIGNATURE-----\n")
    sign = stamp_sign.signer_for(pub, "coney-stamp", run=run)
    assert sign is not None
    made = sign(b"body")
    assert made["signer"] == "alice@laptop" and made["armor"].startswith("-----BEGIN SSH SIGNATURE-----")
    assert str(pub) in run.calls[0][0] and "-U" in run.calls[0][0]
