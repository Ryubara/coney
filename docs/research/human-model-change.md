---
kind: survey
---
# A human's model never changes

Verified against: `SLUS_212.15` (NTSC-U), read in the shared Ghidra project.

## Summary

The question, asked for Coney's debug model swap (the Entity inspector's Model item,
[Debug menus](../guides/debug-menu.md#model-swap)): does the original ever give a living human another model, through
a script native, a cut scene's costume or the switch of player 1 or a crew member, so that Coney could take the same
steps? **It does not.** A human's model follows from the character type it was created with and is never replaced. The
only time the original puts a model on a human that already exists is when the human was created before its files had
loaded: the model is filled in later, and that fill-in leaves everything else about the human as it was. Every other
thing that looks like a swap makes another human.

So Coney's swap has no original to port: it is Coney's own debug tooling, and it does to a human that already has a
model what the late fill-in does to one that has none.

## Method

Every function that puts a model on a character instance was found by its callers in Ghidra, starting from the one that
sets an instance's model, and each caller was read for the condition it calls under. The places that look like a swap
from the player's side (cut scenes, the Rumble crew editor, the hand-over of player 1) were read on their own pages and
checked for a call into the same functions.

## Findings

### The model is fixed at creation {#fixed}

`Human_Init` (`0x00218008`) writes the model index `+0x37c` from the type it is given ([Characters: From a type to a
model](characters.md#type-to-model)), and the instance the human is drawn with comes from that index; the resource
manager's update (`ResourceManager_LoadNearestModel`, `0x001897a8`) reads `+0x37c` back to find the character's record
when it loads the files. No other writer of `+0x37c` turned up among the functions read here. Confirmed (code) at
`0x00218008` and `0x001897a8`; that nothing else writes `+0x37c` is inferred from the callers below being the only paths
to a new model.

### A model is filled in, never replaced {#filled-in}

`CharacterInstance_SetModel` (`0x00174af8`) is the one function that puts a model on a character instance. Its two
callers are `CharacterInstance_Construct` (`0x00174d00`, a new instance) and `ResourceMgr_SetInstanceModel`
(`0x00177d50`). The latter's one caller is `ResourceManager_LoadNearestModel` (`0x001897a8`), which calls it only while
the human's instance has no model yet (instance `+0xd8`, its `+0x20` is 0): the late load of [Characters:
Creation](characters.md#creation). `Human_AttachInstance` (`0x00217a98`), which gives a human its instance, has two
callers too, `Human_Init` and that same update, and the update calls it only for a human with no instance (`+0xd8` is
0). Confirmed (code) at `0x00174af8`, `0x00177d50`, `0x001897a8` and `0x00217a98`.

### What the late fill-in keeps {#keeps}

`CharacterInstance_SetModel` (`0x00174af8`; its role in the character instance is on
[Graphics: Characters](graphics.md#code-characters)) sets the render instance up with the new model and its textures.
What it does next depends on whether the instance belongs to a human (`+0x2b8`, the human's pointer); confirmed (code):

- **No human:** the instance is taken out of the world, and nothing else is done.
- **A human:** it sets the s16 at `+0x2b0` to -1 and copies the model's 34 bind offsets (the table the model's
  `+0x2c` points to) into the instance at `+0x80`: 544 bytes, 34 entries of 16 bytes, a float4 each as
  [Characters: Files](characters.md#files) lists them, the skeleton the model is skinned with. It leaves the
  instance's task stack (`+0x2ac`, the animation playing) as it is, runs the queued tasks and calls two of the human's
  virtual functions (slots `+0x13c` and `+0xc4`, not traced).

The human itself, its handle, gang, brain, meters and place, is not touched. Confirmed (code) at `0x00174af8`: the
copy loop moves 32 bytes a pass for 17 passes (`0x220` bytes); what `+0x2b0` holds and the two virtual calls are
[open](#q-fill-in-calls).

### What looks like a swap makes another human {#others}

- **Cut scenes.** A role is drawn as the type of the human bound to it; a script that wants a character in other
  clothes creates a human of another type ([Scenes: Cast and props](scenes.md#cast)). Confirmed (code) on that page.
- **The Rumble crew editor.** The Swap Soldier screen replaces its preview human with a new one of the chosen type
  (`RM_SwapSoldier_ShowCandidate`, `0x00200c48`) and writes the type into the gang
  (`RM_SwapSoldier_OnInput`, `0x00201308`). Confirmed (code) at those addresses ([Front end](frontend.md)).
- **Player 1's hand-over.** `HuChangePlayerGang` makes another human player 1 rather than changing the player's model
  ([Characters: Players](characters.md#players)). Confirmed (code) on that page.

## Open questions

### The fill-in's two virtual calls {#q-fill-in-calls}

`CharacterInstance_SetModel` (`0x00174af8`) ends with calls through the human's vtable slots `+0x13c` and `+0xc4`,
after setting the instance's s16 at `+0x2b0` to -1. What the calls do and what `+0x2b0` holds are not traced;
`Human_AttachInstance` (`0x00217a98`) makes the same two calls after attaching an instance. Coney's swap does none of
the three. Reading the two slots of the human's vtable and the readers of `+0x2b0` in Ghidra answers it.
