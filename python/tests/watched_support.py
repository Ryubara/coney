# SPDX-License-Identifier: GPL-3.0-or-later
"""Made-up machines for the tests of the watched drives (coney_tools.watched_drives): a checkout whose folders are on
drives that the tests name, and a probe that reads fake free space instead of the real disk.

A machine is a folder holding one subfolder per drive (`c`, `e`, `f`): a path's drive is the first folder under it, so
the tests run the same on every operating system and the real disk is never asked."""

from dataclasses import dataclass
from pathlib import Path

from coney_tools.watched_drives import Probe

GIB = 1024**3


@dataclass
class Machine:
    """Drives `c`, `e` and `f` under one temporary folder, with free space in GB that a test sets and reads (a negative
    figure is a drive that cannot be read)."""

    root: Path
    free_gb: dict[str, float]

    @property
    def workspace(self) -> Path:
        """The workspace folder, on drive `c`."""
        return self.root / "c" / "work"

    @property
    def checkout(self) -> Path:
        """The main checkout, `<workspace>/repos/coney`."""
        return self.workspace / "repos" / "coney"

    def drive_of(self, path: Path) -> str:
        """The drive a path is on: the first folder under the machine's root."""
        return path.resolve().relative_to(self.root.resolve()).parts[0]

    def probe(self) -> Probe:
        """A probe that reads this machine's fake free space, drives and labels."""

        def free(path: Path) -> int:
            gb = self.free_gb[self.drive_of(path)]
            if gb < 0:
                raise OSError(f"{path}: the drive is not ready")
            return int(gb * GIB)

        return Probe(
            free=free,
            same=lambda one, two: self.drive_of(one) == self.drive_of(two),
            label=lambda path: self.drive_of(path).upper() + ":",
        )

    def local(self, text: str) -> None:
        """Write the machine's coney.local.toml."""
        (self.checkout / "coney.local.toml").write_text(text, encoding="utf-8")


def make_machine(root: Path, **free_gb: float) -> Machine:
    """A machine with the given free GB per drive (all of `c`, `e`, `f` are made; an unset one has 500 GB)."""
    made = Machine(root, {"c": 500.0, "e": 500.0, "f": 500.0, **free_gb})
    made.checkout.mkdir(parents=True)
    (made.checkout / "coney.local.example.toml").write_text("", encoding="utf-8")
    for drive in ("e", "f"):
        (root / drive).mkdir()
    return made
