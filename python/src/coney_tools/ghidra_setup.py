# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools ghidra setup`: a contributor's own Ghidra project of `SLUS_212.15`, named, in one command.

The steps a newcomer otherwise does by hand from docs/guides/ghidra.md: fetch the pinned Ghidra with the Emotion
Engine extension, compile the extension's language once, import and analyse the executable from the player's own
disc into the project the ghidra-mcp server opens, and apply our published function names (ghidra_names). Each step
already done is skipped, so running it again is safe, and `--dry-run` only prints them. The executable is copied
from the disc into a temporary folder for the import and deleted after it; only its SHA-1 and size are printed
(LEGAL.md). See docs/guides/ghidra.md#own-project.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import tempfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from functools import partial
from pathlib import Path

from coney_tools import ghidra_names
from coney_tools.config import ConfigError
from coney_tools.disc import Disc
from coney_tools.ghidra_headless import (
    LANGUAGE,
    LANGUAGES,
    PROGRAM,
    GhidraProject,
    Runner,
    analyze_headless,
    failed,
    has_program,
    java_env,
    sleigh,
)
from coney_tools.progress_research import ListedFunction


@dataclass(frozen=True)
class Setup:
    """The folders and programs setup works with; the tests pass stand-ins for the programs."""

    install: Path
    #: The JDK 21 home; None when none was found.
    jdk: Path | None
    project: GhidraProject
    #: The scratch folder: logs, and the temporary copy of the executable.
    work: Path
    #: Where downloaded archives are kept (scratch/downloads).
    downloads: Path
    platform: str
    runner: Runner
    #: Installs the pinned Ghidra into its first argument, keeping downloads in its second.
    fetch: Callable[[Path, Path], None]
    #: True when a ghidra-mcp server answers on this project's port.
    server_up: Callable[[], bool]


@dataclass(frozen=True)
class Options:
    """What the user asked for."""

    #: The SHA-1 of the NTSC-U executable, which every address in the project refers to.
    expected_sha1: str
    #: Apply the published names after the import.
    names: bool = True
    #: Import another release's executable anyway (its addresses will not match the listing).
    any_release: bool = False
    dry_run: bool = False
    overwrite: bool = False


@dataclass(frozen=True)
class Step:
    """One thing setup does, or found already done (`action` None)."""

    verb: str
    target: str
    action: Callable[[], None] | None
    #: The action only reads, so a dry run does it too (the names, counted on a program opened read-only).
    reads_only: bool = False


def _sha1_and_size(disc: Disc, name: str) -> tuple[str, int]:
    """The SHA-1 and byte size of the disc file `name`, read in pieces."""
    digest = hashlib.sha1()
    size = 0
    with disc.open(name) as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
            size += len(block)
    return digest.hexdigest(), size


def _run(setup: Setup, what: str, command: Sequence[str], log_name: str, cwd: Path) -> None:
    """Run one Ghidra program with the JDK; ConfigError naming its log when it fails."""
    assert setup.jdk is not None  # checked before any step runs
    setup.work.mkdir(parents=True, exist_ok=True)
    log = setup.work / log_name
    code = setup.runner(command, java_env(setup.jdk, os.environ), log, cwd)
    if code != 0:
        raise failed(what, code, log)


def _compile_language(setup: Setup) -> None:
    """Compile the extension's r5900.sla, which its release does not ship (docs/guides/ghidra.md)."""
    languages = setup.install / LANGUAGES
    command = [str(sleigh(setup.install, setup.platform)), "r5900.slaspec", "r5900.sla"]
    _run(setup, "compiling r5900.sla", command, "ghidra-sleigh.log", languages)


def _import(setup: Setup, disc: Disc) -> None:
    """Copy the executable from the disc to a temporary folder, import and analyse it, and delete the copy."""
    setup.project.folder.mkdir(parents=True, exist_ok=True)
    setup.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=setup.work, prefix=".ghidra-import-") as temp:
        copy = Path(temp) / PROGRAM
        with disc.open(PROGRAM) as source, copy.open("wb") as target:
            shutil.copyfileobj(source, target, length=1 << 20)
        command = [
            str(analyze_headless(setup.install, setup.platform)),
            str(setup.project.folder),
            setup.project.name,
            "-import",
            str(copy),
            "-loader",
            "ElfLoader",
            "-processor",
            LANGUAGE,
            "-cspec",
            "default",
        ]
        _run(setup, f"importing {PROGRAM}", command, "ghidra-import.log", setup.work)


def _apply_names(setup: Setup, functions: Sequence[ListedFunction], options: Options) -> None:
    """Apply the published names and print the counts."""
    assert setup.jdk is not None
    job = ghidra_names.ImportJob(
        setup.project, PROGRAM, setup.install, setup.jdk, setup.work, options.dry_run, options.overwrite
    )
    summary = ghidra_names.import_names(job, functions, setup.runner)
    for line in ghidra_names.format_summary(summary, options.dry_run):
        print(f"  {line}")


def _check_disc(disc: Disc | None, options: Options) -> None:
    """Refuse a missing disc or another release's executable; print the executable's hash and size."""
    if disc is None:
        raise ConfigError(
            f"no disc: pass --disc (a mounted disc, a folder or an .iso) or set game_dir to import {PROGRAM}"
        )
    sha1, size = _sha1_and_size(disc, PROGRAM)
    print(f"  disc     {PROGRAM}: {size:,} bytes, SHA-1 {sha1}")
    if sha1 != options.expected_sha1 and not options.any_release:
        raise ConfigError(
            f"{PROGRAM} on {disc.path} is not the NTSC-U release (SHA-1 {options.expected_sha1}): every address in "
            "the research and the names listing refers to that one; pass --any-release to import it anyway"
        )


def plan(setup: Setup, disc: Disc | None, functions: Sequence[ListedFunction], options: Options) -> list[Step]:
    """The steps of setup, in order; each step already done has no action."""
    steps: list[Step] = []
    if (setup.install / "support").is_dir():
        steps.append(Step("exists", f"Ghidra in {setup.install}", None))
    else:
        steps.append(
            Step("fetch", f"Ghidra 12.1.4 -> {setup.install}", partial(setup.fetch, setup.install, setup.downloads))
        )
    languages = setup.install / LANGUAGES
    if (languages / "r5900.sla").is_file():
        steps.append(Step("exists", "r5900.sla", None))
    else:
        steps.append(Step("compile", f"r5900.sla in {languages}", partial(_compile_language, setup)))
    imported = has_program(setup.project, PROGRAM)
    if imported:
        steps.append(Step("exists", f"{PROGRAM} in {setup.project.gpr}", None))
    else:
        _check_disc(disc, options)
        assert disc is not None
        steps.append(
            Step("import", f"{PROGRAM} -> {setup.project.gpr} (and analyse it)", partial(_import, setup, disc))
        )
    if options.names:
        action = partial(_apply_names, setup, functions, options)
        steps.append(Step("apply", "the published function names", action, reads_only=options.dry_run and imported))
    return steps


def run_setup(setup: Setup, disc: Disc | None, functions: Sequence[ListedFunction], options: Options) -> int:
    """Set up the project; prints each step. Returns 0; problems raise ConfigError before or at the failing step."""
    if setup.jdk is None:
        raise ConfigError(
            "no JDK 21 found: install one (for example Eclipse Temurin 21) and set jdk_home in coney.local.toml"
        )
    if setup.project.lock.exists() and setup.server_up():
        raise ConfigError(
            f"{setup.project.gpr} is open in a running ghidra-mcp server; stop the server first (two programs on "
            "one project fight over its lock, docs/guides/ghidra.md#gotchas)"
        )
    print(f"Ghidra project: {setup.project.gpr}" + (" (dry run: nothing is changed)" if options.dry_run else ""))
    steps = plan(setup, disc, functions, options)
    for step in steps:
        if step.action is None:
            print(f"  {step.verb:<8} {step.target}")
        elif options.dry_run and not step.reads_only:
            print(f"  would {step.verb} {step.target}")
        else:
            print(f"  {step.verb:<8} {step.target}", flush=True)
            step.action()
    return 0
