# Engine: how Coney works

This section describes Coney's own code: how its parts fit together, why they are shaped as they are, and the port
notes that say where a part stands against the original. It never says what the original does. That is the
[research pages](../research/overview.md), which these pages link to for each behaviour they port, and the
[guides](../guides/how-we-work.md) say how the work is done.

## What belongs here

- How a part of Coney works and why: the game loop, how the modes own their state, how input, saves and the world are
  kept apart from the platform.
- Port notes: for a behaviour ported from the original, which function of Coney does it, which stand-ins remain and what
  each waits for.
- Nothing about the original's own behaviour. A plan belongs in issues, except the design of a part's next steps
  where the maintainers ask for it on the part's page ([the rule](../guides/writing-docs.md#engine-pages)).

Add each page to this one; the rules for writing a page, and how they are checked, are in
[Writing these docs](../guides/writing-docs.md#engine-pages).

| Page | Subsystem |
| --- | --- |
| [Combat](combat.md) | The player's attacks, grabs, blocks, meters and reactions (`src/combat/`, the fighter in `src/human/`) |
| [Renderer](renderer.md) | How Coney draws, and the design for its frame rates, backend, lighting, anti-aliasing, presets and performance (`src/platform/`, `src/graphics/`) |
| [GPU renderer](gpu-renderer.md) | The renderer on SDL3's GPU API: `--renderer gpu`, its device, window and present modes, and librw opened for data only (`src/platform/gpu_presenter.*`, `src/core/renderer_choice.*`) |
