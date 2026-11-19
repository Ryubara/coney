# The UI widget model for mods

The UI widget model is the C++ under the screens and widgets mods will build: a retained tree of screens and widgets by
id, drawn with the game's own widget classes, driven by the game's menu commands and timings, with the trusted layer
and the frame label that keep a mod's screen from passing for Coney's. This page is what is built of it; the whole
design, with the natives, screen files, extension points and Studio's Screen view still to come, is [Coney Studio: UI
and menus for mods](studio/ui.md).

## What is built {#built}

Build issue 2 of [the design](studio/ui.md#build-issues): the model, its renderer, the input mask and the two hosts,
in C++ with no Lua. Nothing calls it from a script yet: the natives over it are build issue 4, and the Mods screen
that is its first trusted screen is build issue 3. With no mod installed nothing here runs, so the faithful game is
unchanged.

| Part | Where | What it does |
| --- | --- | --- |
| Ids, definitions, events, limits | `src/gui/mod_ui/ui_types.h` | `UiScreenId`, `UiWidgetId`, a screen's and a widget's definition, the events, the refusals and `ui_limit` |
| A screen | `src/gui/mod_ui/ui_screen.h` | Its widgets in the order they were added, the focus, a choice dialog; one command into moves, focus moves and events |
| The model | `src/gui/mod_ui/ui_model.h` | Projects and their titles, screens and widgets by id, each slot's flow, the HUD screens shown, the trusted layer, the limits, back always closes |
| The renderer | `src/gui/mod_ui/ui_view.h` | Draws the shown screens with the game's widget classes and the frame labels in the trusted layer |
| The input mask | `src/gui/mod_ui/ui_input_mask.h` | Hides a slot's pad from gameplay while a mod screen has it |
| Host on a menu's flow | `src/gui/mod_ui/ui_flow_screen.h` | A screen-flow state (the profile manager's flow) that runs a slot's mod screens and pops when they close |
| Host over play | `src/gamemodes/ui_screen_mode.h` | Game mode `0x109`, over gameplay while a screen that pauses play is open (`UiModel::pausesPlay`) |

Tests: in `tests/gui/mod_ui/`, one file per part (`ui_model_test.cpp`, `ui_view_test.cpp`, `ui_input_mask_test.cpp`,
`ui_flow_screen_test.cpp`), and `tests/gamemodes/ui_screen_mode_test.cpp`.

## Screens and widgets {#widgets}

Coney's own code registers each project with the title its frame label shows (`UiModel::registerProject`). A project
then makes screens (`createScreen`: layer `Menu` or `Hud`, a title, whether it pauses play) and widgets on them
(`addWidget`), and changes them by id: `addItem`, `removeWidget`, `setText`, `setVisible`, `setPosition`, `setFill`,
`focus`, `askChoice`. A call naming another project's screen or widget is refused as unknown, so a project reaches only
its own. Each call that cannot be done returns a `UiRefusal` the natives will turn into an error a script can test.

The widget kinds are the game's classes, each holding what it already holds in the menus:

| Kind | Class | Takes focus |
| --- | --- | --- |
| Text | `TextWidget`, with the game's markup | no |
| Sprite | `BaseWidget`, a rectangle of a `game:/` sheet | no |
| Grid | `OptionGrid`: its items, rows, cues and moves | yes |
| List | `ScrollingMenu`: its cursor, window and moves; the renderer draws the rows | yes |
| Bar | `Bar` | no |
| Header | `CircledTextHeader` | no |
| Usage line | `UsageInfo` | no |
| Message | `MessageBox`'s timed message | no |
| Keyboard | `NameKeyboard`, PM_Create's keyboard, taking at most 32 characters | yes |

A hidden widget (`setVisible`) is not drawn and cannot hold the focus: hiding the focused one passes the focus to the
first widget left that can take it, and with none the screen has no focus and its commands go nowhere.

A choice dialog (`askChoice`) is the message box's choice: one or two choices in a one-row `OptionGrid` at the box's
place, the default selected.

**Hosting.** A `Menu` screen opens for one player slot, on that slot's flow: a project's next screen goes over it, and
closing it returns to the one under it. On the front end the flow runs inside the profile manager's screen flow through
`UiFlowScreen`. Over play, while a screen made with `pause` is on a slot's flow (`UiModel::pausesPlay`), the slot's
screens run in `UiScreenMode`, a game mode over gameplay, which is suspended under it as under the pause menu; the mode
leaves once no screen of the flow pauses play. A screen that does not pause play is stepped by its host while play goes
on, its player's pad taken by the input mask. A `Hud` screen joins the shown HUD elements and never takes input.

**One project per slot.** While a slot's flow holds a project's screens, another project's screen is refused for that
slot; the other slot is free.

## Focus and navigation {#focus}

`UiModel::step` reads one slot's pad through Coney's `MenuInput`, the port of `MenuInput_Dispatch` (`0x001e95c0`), so a
mod's menu has exactly the timings of the game's own menus in Coney: the command gap, the held stick, the d-pad's
auto-repeat and accept and back on release, as [Front end: input](../research/frontend.md#input) gives them. Where the
port still differs from the original ([#659](issue:659)), the fix there reaches the mod screens with no change here.
Each focus change takes the focus as `OptionGrid_TakeFocus`
(`0x001d4d28`) does: the last command is forgotten and the d-pad blocked for 20 ms.

The focused widget takes the command as its class does, with its cues and refusals. Between widgets:

- **A direction past the focused widget's edge** moves the focus when there is somewhere to go: a grid's first or last
  row or column, a list's first or last entry and its left and right, a keyboard's move that cannot leave its cell.
- **Where it goes**: the neighbour the widget names for that direction (`UiNeighbours`, by widget name), else the next
  focusable widget in the order they were added for down and right, the previous for up and left, without wrapping.
- **With nowhere to go** the widget takes the command as it would alone, so a screen of one grid moves exactly as the
  game's grid does: it wraps where the grid wraps and refuses with cue `0xe` where it refuses.

The events are queued in order and taken by the host (`takeEvents`): accept and move with the item, focus, back,
closed, a dialog's choice, the keyboard's text.

**Back always closes** ([Safety](studio/ui.md#no-spoofing)): back plays cue `0xf`, ends an open dialog with no choice,
and otherwise queues Back and Closed and pops the screen from its flow, whatever the project does with the event; the
screen under it takes the focus again. A project cannot keep a screen open.

## The input mask {#input-mask}

`UiInputMask` is an input source between the device and the game, as the debug menu's input gate is
([Controls](debug-menu.md#controls)). While a slot has a `Menu` screen open, gameplay sees that port connected with
nothing pressed and its sticks centred; the other port passes through, so player 2 plays on. The buttons held when the
screen closes stay hidden from gameplay until they are let go, so the press that closed it never acts in play. The
screens read the ports as the device gave them (`rawPad`).

## The trusted layer and the frame label {#trusted}

- **The trusted layer.** While Coney's code has a trusted screen open (`openTrusted`, nesting counted), no mod screen
  opens, menu or HUD, and no slot's pad reaches a mod screen; the mod screens already open stay where they were, and
  take their focus afresh when the last trusted screen closes. Trusted screens are not in the model at all, so no call
  of a project can name, read or press anything on one.
- **The frame label.** Every mod screen shown, menu or HUD, carries its project's registered title
  ([Safety](studio/ui.md#no-spoofing)): the renderer draws each such project's title once, small and right-aligned at
  the top right of the safe area, one line under another (slot 1's top screen, slot 2's, then the HUD screens). The
  labels go in batches of their own at depth 30,000, above every 2D depth Coney draws at (the highest, 20,001, is the
  HUD's lock-pick dial face). The mods' widgets go into batches at the game's menu depths (8,500 for sprites, 9,000 for
  text), so no widget, wherever it is placed and whatever it says, is drawn over a label. Its text comes only from
  `registerProject`.

## Limits {#limits}

Per project, refused with a `UiRefusal`: 16 screens alive, 512 widgets alive over all of them, 4 HUD screens shown at
once, 2,048 bytes in one text, 100 items in one list (a grid holds `OptionGrid`'s 50), 32 characters typed on the
keyboard. They are the design's first guesses ([Limits](studio/ui.md#limits)).

## Coney's choices {#choices}

Where neither the original nor the design decides, each called Coney's choice in the code's comments:

- Focus moving between widgets plays the move cue 4; an accept on a grid or list plays 8 (the message box's).
- The edge rule above, and no wrap from the last widget to the first.
- While a trusted screen is open no mod screen opens for any slot, not only that screen's player.
- A list's rows are drawn at the grid's row pitch (0.0505 at size 1.15), the selected one grey; a keyboard's cells at
  PM_Create's measured pitches; the frame label's place, size 0.8 and grey.

Not built yet, and where it comes: the refused markup tags and a handler's errors closing its screen (the natives, build
issue 4); screen files (issue 5); wiring the model into the game session (the input mask, and pushing `UiScreenMode`
when a screen that pauses play opens), which waits for the first screen a project can open.
