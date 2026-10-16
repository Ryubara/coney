# Security

## What counts

Report anything that lets untrusted input harm a person running Coney, for example:

- a crash, memory corruption or code execution caused by a crafted game file, archive or save game;
- an unsafe default, such as a network service listening without being asked to;
- a flaw in the build, CI or release process that could ship tampered binaries.

Ordinary bugs, including crashes with unmodified game files, go in a normal issue. Do not attach game files to
any issue (see [LEGAL.md](LEGAL.md)); describe how to reproduce the problem instead.

## Report a vulnerability

Use GitHub's **Report a vulnerability** button under the repository's Security tab. Please do not open a public
issue or pull request for a vulnerability.

Include what you found, the affected commit, and the steps to reproduce it. If a crafted file is needed, describe how
to build one rather than sending it.

## What happens next

A maintainer will acknowledge the report, confirm whether it is a vulnerability, and work on a fix in private. The
fix is published together with a note crediting you, unless you prefer to stay anonymous.

## Supported versions

The latest commit on `main` until releases exist.
