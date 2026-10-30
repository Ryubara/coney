# SPDX-License-Identifier: GPL-3.0-or-later
"""The software-pictures job's pins (docs/guides/testing.md#software-pictures): the container image the drawing runs in.

A picture depends on the OpenGL driver, so the image must name its base by digest, read packages from a dated snapshot
and install Mesa at an exact version it then checks. These tests read the Dockerfile and the workflow as text.
"""

from __future__ import annotations

import re
from pathlib import Path

from coney_tools.config import find_repo_root

ROOT = find_repo_root(Path(__file__))
DOCKERFILE = (ROOT / ".github" / "software-pictures" / "Dockerfile").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github" / "workflows" / "build.yml").read_text(encoding="utf-8")


def test_the_base_image_is_named_by_its_digest() -> None:
    # Expected: engine/renderer.md#comparing, the job runs in a container image pinned by its digest
    assert re.search(r"^FROM \S+@sha256:[0-9a-f]{64}$", DOCKERFILE, re.MULTILINE)


def test_the_packages_come_from_a_dated_snapshot() -> None:
    assert re.search(r"^ARG SNAPSHOT=\d{8}T\d{6}Z$", DOCKERFILE, re.MULTILINE)
    assert "snapshot.ubuntu.com/ubuntu/%s" in DOCKERFILE


def test_mesa_is_installed_at_a_fixed_version_that_the_build_checks() -> None:
    # Expected: engine/renderer.md#comparing, Mesa at a fixed package version
    mesa = re.search(r"^ARG MESA=(\S+)$", DOCKERFILE, re.MULTILINE)
    assert mesa is not None and re.fullmatch(r"\d+\.\d+\.\d+-\S+", mesa.group(1))
    assert '"libgl1-mesa-dri=$MESA"' in DOCKERFILE
    assert "dpkg-query -W -f='${Version}' libgl1-mesa-dri" in DOCKERFILE
    assert "GALLIUM_DRIVER=llvmpipe" in DOCKERFILE


def test_the_job_caches_the_image_by_the_dockerfiles_hash_and_keeps_the_drawn_pictures() -> None:
    job = WORKFLOW[WORKFLOW.index("  software-pictures:") :].split("\n  cache-prune:")[0]
    assert "hashFiles('.github/software-pictures/Dockerfile')" in job
    assert "actions/upload-artifact@" in job and "!cancelled()" in job
    assert "coney-tools picture scenes" in job


def test_every_action_the_job_uses_is_pinned_to_a_commit() -> None:
    job = WORKFLOW[WORKFLOW.index("  software-pictures:") :].split("\n  cache-prune:")[0]
    for line in job.splitlines():
        match = re.search(r"uses: (?!\./)(\S+)", line)
        if match:
            assert re.search(r"@[0-9a-f]{40}$", match.group(1)), f"not pinned to a commit: {match.group(1)}"
