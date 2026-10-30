# Coney Studio: prefabs and their overrides

**Status: design.** Nothing on this page is built. How a prefab instance's overrides behave in Coney Studio: what
changing, reverting and applying a value does, reverting an added or a dropped entity, applying to a nested or the
outermost prefab, unpacking, variants, why there is no *Editable Children* toggle, undo for each, and what the level
file holds afterwards. What a prefab is and its file's statements are [Prefabs](project-formats.md#prefabs) in the
project formats; the Inspector that draws overrides is [Entities and the Inspector](entities.md#inspector); the
Hierarchy's instance row is [The Hierarchy's tree](editor-experience.md#hierarchy-tree).

## Overrides {#overrides}

Overriding one instance never changes another: an edit made on an instance's entity is written as that instance's
`set` or `drop` in the level file ([Prefabs](project-formats.md#prefabs)) and nowhere else, and only *Apply*, chosen by
the user, writes into a prefab file. Studio takes Unity's model, which keeps every instance's entities editable in
place, and Godot's rule of writing only values that differ.

**What Unity and Godot do**, from their manuals and Godot's editor source ([Sources](#sources)); neither editor was run
for this page:

| Case | Unity | Godot |
| --- | --- | --- |
| An instance's changed value | An override, its name in bold with a blue bar in the margin; the asset's later changes to that property do not reach it | An override, with a revert arrow beside it; kept when the scene changes |
| All of an instance's changes | The *Overrides* menu on the root instance lists them, with *Revert All*, *Apply All* and per-item *Revert* and *Apply* | No list: each property's revert arrow, one at a time |
| Apply to the prefab | *Apply* writes to the prefab asset; *Apply All* always to the outermost prefab; the context menu of a nested override offers the nested prefab or the outer one | None from an instance: the scene itself is opened and edited, or a branch saved as a new scene |
| Added and removed children | Added children carry a plus badge, removed components a minus badge; reverting an added object deletes it, reverting a removal puts it back, applying a removal deletes it from the asset | Children may be added to an instance; an instanced child cannot be deleted from it |
| Breaking the link | *Unpack* turns the instance into plain objects keeping every overridden value, nested instances staying instances; *Unpack Completely* unpacks those too | *Make Local*: the instance's nodes become the scene's own, as one undoable action |
| A prefab based on another | A *Prefab Variant*: the base's values unless overridden, with its own *Overrides* menu | An *inherited scene*: the base's nodes, overridden properties with a revert arrow |
| A value set back to the prefab's | The manual does not say; overrides whose target is gone stay in the scene until *Remove Unused Overrides* | The scene file stores only the values that differ from the instanced scene |
| A toggle to edit children | None: an instance's children are always editable | *Editable Children*, off by default; turning it off asks first, then reverts every property of the children to the scene's |

**Coney Studio's rules.** Each action is one edit command and one undo step; the right column is what the level file
holds afterwards.

| Action | What it does | The level file after |
| --- | --- | --- |
| Change a value on an instance's entity | An override, drawn bold with the margin bar; later edits to the prefab's value do not reach it | `set p2/h3 key=value`, with the instance's other keys |
| Change it back to the prefab's value | The override is tidied away (Godot's rule, the level file's *only what differs*) | The key leaves the `set` line; an empty `set` line goes |
| *Revert* on one override | The prefab's value again | As above |
| Delete one of an instance's entities | A drop; the Hierarchy shows it struck through, so it can be found and reverted | `drop p2/c2`; the entity's `set` keys go with it |
| *Revert* on a drop | The entity is back, with the prefab's values | The `drop` line goes |
| Add an entity under an instance's entity | An added entity, with a plus badge (Unity's) | An ordinary entity statement with `parent=p2/h3` |
| *Revert* on an added entity | It is deleted with its children | Its statements go |
| *Revert All* (the instance's *Overrides* menu) | Every override, drop and added entity of this instance reverted | Only the `instance` line is left |
| *Apply* on one override | Written into the prefab that holds the entity; every instance without its own override follows | The `set` key leaves the level; the prefab file holds the value |
| *Apply* on a nested entity's override | A choice, as Unity's: into the innermost prefab that holds the entity, or as an override in an outer prefab | Inner: the inner prefab file holds the value. Outer: the outer prefab file gains `set inner/h2 key=value` |
| *Apply All* | Every override, drop and added entity applied to the outermost prefab, as Unity's | The instance's `set`, `drop` and added lines go; the prefab file holds them, a nested entity's as its own `set` lines |
| *Apply* of a drop or an added entity | The entity leaves, or joins, the prefab, for every instance | The prefab file loses, or gains, the entity |
| *Unpack* | The instance becomes plain entities with its current values, as Unity's; nested instances stay instances; *Unpack Completely* unpacks them too | The `instance`, `set` and `drop` lines are replaced by one statement per entity, with new ids ([Unpacking](project-formats.md#unpack)) |
| *Make variant* (from an instance) | A new prefab based on the instance's, holding this instance's overrides; the instance now places the variant (Unity's) | A new prefab file with `variant` and the overrides ([Variants](project-formats.md#variants)); the `instance` line names it |

- **Asked before it reaches other instances.** *Apply* and *Apply All* say how many other instances and variants the
  change reaches and list those whose own override hides it; nothing else writes a prefab file.
- **Undo restores both files.** An *Apply* changes the level and a prefab file as one command; its undo puts both
  back, and a prefab file saved since is marked changed again. *Unpack* undoes to the same `instance`, `set` and
  `drop` lines, byte for byte ([One statement format](project-formats.md#statement-format)).
- **Override, edit, undo.** Override a value (a `set` key appears), edit it again (the key's value changes), undo (the
  first override's value), undo again (the key is gone and the entity follows the prefab once more). Editing the
  prefab between those steps changes no instance that holds the key.
- **No *Editable Children* toggle.** An instance's entities are always editable, as in Unity: an override is one key
  of one instance and costs nothing until it is made, so a toggle would only hide edits, and turning Godot's off
  reverts every edit under it.

Variants, unpacking and the statements behind each row are [Prefabs](project-formats.md#prefabs) in the project
formats; the open choices are listed under [Open questions](#open-questions).

## Tests {#tests}

For every row of the rules [above](#overrides), a scripted edit on a fixture level with two instances of one prefab
(one of them nested) gives the level and prefab files the table names and leaves the other instance unchanged, and its
undo gives both files back byte for byte. The override, edit and undo sequence ends with no `set` key, and a value set
back to the prefab's writes none. They run headless, under the Dear ImGui Test Engine, with no disc
([Tests](entities.md#tests) of the Inspector).

## Open questions {#open-questions}

- **For the user: an override equal to the prefab's value.** The design tidies it away, so the instance follows the
  prefab again; Unity's manual does not say it does the same. The other choice is to keep it as a pinned value that a
  later change of the prefab does not reach, shown as an override with the prefab's value. Which does the user want?
- **For the user: the ids after *Unpack*.** The design gives the unpacked entities new plain ids and keeps their names
  and tags, which scripts look entities up by, and lists every script string that held an old `p2/h3` id. Keeping the
  old ids instead would mean an id with a `/` that names no instance. Is that acceptable?
- **For the user: *Apply All* with nested prefabs.** The design follows Unity: *Apply All* writes everything to the
  outermost prefab, and only a single override's *Apply* offers the inner one. Should *Apply All* ask instead?

## Sources {#sources}

Ideas only; no code is taken from any of these.

| Idea | Source |
| --- | --- |
| A prefab instance's header with *Select* and *Open*; the *Overrides* menu with *Revert All*, *Apply All* and per-item *Apply* and *Revert*, on the root instance only | Unity, [Prefab instance Inspector reference](https://docs.unity.com/en-us/engine/6000.6/manual/working-with-gameobjects/prefabs/prefab-instance-inspector-reference) |
| Overrides in bold with a blue margin bar; plus and minus badges; an override taking precedence over the asset's later changes; per-item *Apply* and *Revert* in context menus | Unity, [Override prefab instances](https://docs.unity.com/en-us/engine/current/manual/working-with-gameobjects/prefabs/override/prefab-instance-overrides) |
| Overrides of a missing target kept until *Remove Unused Overrides* | Unity, [Removing unused override data](https://docs.unity.com/en-us/engine/6000.6/manual/working-with-gameobjects/prefabs/override/unused-overrides) |
| *Apply All* going to the outermost prefab; reverting and applying a removed component | Unity 2019.4, [Editing a Prefab via its instances](https://docs.unity3d.com/2019.4/Documentation/Manual/EditingPrefabViaInstance.html) |
| A nested prefab's changes applied to the outer prefab as overrides | Unity, [Nest prefab instances in other prefabs](https://docs.unity.com/en-us/engine/6000.6/manual/working-with-gameobjects/prefabs/nested) |
| *Unpack* keeping overridden values, nested instances kept; *Unpack Completely* | Unity, [Revert a prefab instance to a GameObject](https://docs.unity.com/en-us/engine/current/manual/working-with-gameobjects/prefabs/unpacking-prefab-instances) |
| Prefab variants: a base's values unless overridden, their own *Overrides* menu, made from an instance with its overrides | Unity, [Create variations of prefabs](https://docs.unity.com/en-us/engine/current/manual/working-with-gameobjects/prefabs/prefab-variants) |
| An instance's changed property overriding the scene's and kept when the scene changes; the revert icon | Godot, [Creating instances](https://docs.godotengine.org/en/stable/getting_started/step_by_step/instancing.html) |
| *Editable Children* turned off reverting every property of the children, after a confirmation; *Make Local* clearing the instance's scene, undoable | Godot's editor source, [scene_tree_dock.cpp](https://github.com/godotengine/godot/blob/4.5-stable/editor/docks/scene_tree_dock.cpp) at `4.5-stable` |
| Inherited scenes; an instanced child not deletable from the instance; an instance's values stored only where they differ from the instanced scene | Godot's editor *(general knowledge, not re-checked)* |
