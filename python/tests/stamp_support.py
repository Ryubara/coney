# SPDX-License-Identifier: GPL-3.0-or-later
"""Shared by the stamp tests: throwaway ed25519 keys (never a real one) and the rules of this checkout."""

import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path

import pytest
from conventions_support import REPO

from coney_tools.conventions import Stamps, load_conventions

NO_SSH_KEYGEN = shutil.which("ssh-keygen") is None
needs_ssh_keygen = pytest.mark.skipif(NO_SSH_KEYGEN, reason="ssh-keygen is not installed")


def rules() -> Stamps:
    """The stamp rules of this checkout's `.github/conventions.yml`."""
    return load_conventions(REPO).stamps


@dataclass(frozen=True)
class Key:
    """A throwaway key pair in a temporary folder."""

    private: Path
    public: Path

    @property
    def public_text(self) -> str:
        """The public key line (`ssh-ed25519 AAAA... comment`)."""
        return self.public.read_text(encoding="utf-8").strip()


def make_key(folder: Path, name: str = "key", comment: str = "alice") -> Key:
    """Create an ed25519 key without a passphrase in `folder` (a test folder, never ~/.ssh)."""
    folder.mkdir(parents=True, exist_ok=True)
    private = folder / name
    subprocess.run(
        ["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-C", comment, "-f", str(private)],
        check=True,
        capture_output=True,
    )
    return Key(private, Path(str(private) + ".pub"))


def allowed_signers(path: Path, *keys: tuple[str, Key], options: str = 'namespaces="coney-stamp"') -> Path:
    """Write an allowed-signers file with one line per (principal, key)."""
    path.write_text("".join(f"{principal} {options} {key.public_text}\n" for principal, key in keys), encoding="utf-8")
    return path
