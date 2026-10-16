# Contributing

Thank you for wanting to help. Coney reimplements *The Warriors* under a clean-room process: the people who study
the original and the people who write Coney's code are kept apart, and only checked research pages cross between
them. So the first question is not what you want to work on, but which role you can take.

> [!IMPORTANT]
> **A person who has read the original game's code never implements.** That means its disassembly, its decompiler
> output, its script bytecode, or old research that described its code step by step. If you have, you can still
> help as an analyst, tester, or on documentation and tools.

The system that keeps the roles apart (containers, signing keys and the gates) is being built now. Until it is in
place, only the maintainers contribute engine work and research; documentation and tool fixes are welcome.

## Roles

| Role | What you do | What you need |
| --- | --- | --- |
| Implementer | write engine code and tests from the gated research pages | Docker, and a signing key registered as below |
| Analyst | study the original and publish research pages through the page gate | your own copy of the game, and the analyst zone set up on your machine |
| Tester | play Coney builds and the original side by side and report behaviour | your own copy of the game, and the tester zone set up on your machine |
| Documentation and tools | improve guides, `coney-tools`, CI | Python 3.11 or later |

Implementers never see the original or anything shaped by it. They work in a container that holds only Coney's
repository and their task, and they learn how the game behaves from research pages. Analysts and testers work with
the original, so they never write engine code. Tether and the other agent tools the maintainers use are optional;
Docker and `coney-tools` are not.

## Becoming an implementer

1. Start the implementer container. On first start it generates your signing key inside the container.
2. Open a pull request that adds the public key to the list of allowed signers, with this declaration in its body:

   > I have never read The Warriors' code, disassembly or decompiler output, nor any Coney code or research that is
   > not in this repository.

3. The maintainers review it. Once it is merged, CI accepts engine commits signed with that key.

Every engine pull request, whoever wrote it, is compared with the original by an analyst before it is merged. For
now the only registered key is the maintainers' own container. The container commands and the setup guides for
Windows, Linux and macOS are being written.

## Research pages

A research page gives facts and observed behaviour, never procedures. Check a page before you submit it:

```sh
python -I python/coney-tools.py cleanroom page-check page.md
python -I python/coney-tools.py cleanroom page-check --rules
```

A page that fails is never edited until it passes. The failing section is dropped and its topic is researched again
from scratch. Two independent reviewers sign every page before it can enter the repository.

## Pull requests

- Keep each pull request to one change, with a summary of what changed and why.
- Run the local checks first: `python -I python/coney-tools.py ci` runs every check CI runs, the same way
  ([Checks and CI](docs/ci.md)), and the page check covers any research page.
- Pull requests are squash-merged: the title is a short imperative summary, and the description follows the
  template. Issues are filed through the issue forms.
- Never attach or commit anything from the game: no game files, saves, dumps or disc images. Screenshots and short
  clips that show a bug may go in an issue ([LEGAL.md](LEGAL.md)).
- Follow the [Code of Conduct](CODE_OF_CONDUCT.md).

## Licence

By contributing you agree that your work is licensed under the project's licences (GPL-3.0-or-later for code) and
that it follows [LEGAL.md](LEGAL.md).
