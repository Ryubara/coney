---
template: home.html
hide:
  - navigation
  - toc
lead: >-
  An independent, open-source reimplementation of the engine of The Warriors (PS2, 2005), in C++23 on SDL3 for
  Windows, Linux and macOS.
note: >-
  In early development and not playable yet. Coney contains none of the original game's files or code: you play it
  with your own copy of the game.
actions:
  - label: The clean room
    link: clean-room/
  - label: Research
    link: research/
---

# Coney

## What Coney aims for

<div class="grid cards" markdown>

- **Faithful gameplay**

    ---

    Every gameplay rule matches the original until the game is complete.

- **Two renderer modes**

    ---

    Faithful keeps the PS2 look; enhanced adds shadows, reflections and highlights over the original lighting.

- **Fixed 60 Hz simulation**

    ---

    Deterministic, with networked play built in: lockstep with rollback.

- **Your own install**

    ---

    A launcher reads your PS2 or Xbox disc image once and builds a local install. The original mission scripts run
    from it on Coney's Lua 4.0 VM.

- **Modding**

    ---

    A Lua SDK for new missions, and tools for custom content.

- **Clean room**

    ---

    Code is written only from research pages that passed a two-reviewer gate, never from the original.
    [How it works](clean-room.md)

</div>

## Where it stands

The clean-room tooling comes first: the page checks, the gates between research and the engine, and the isolated
containers implementers work in. Engine work starts when that system is in place, one subsystem at a time.

- [The clean room](clean-room.md): what crosses from research to the engine, and what never does.
- [How the roles run](workflow.md): the chain of roles, each in its own zone.
- [Research](research/index.md): the pages that passed the gate.

Coney is not affiliated with, endorsed by or sponsored by Rockstar Games or Take-Two Interactive.
