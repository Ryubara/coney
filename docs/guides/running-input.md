# Controls and input scripts

How Coney reads input: the [controls](#controls) of the keyboard and the gamepads, and [input scripts](#input-scripts)
that replace them for tests and headless runs. The other pages are listed in [the overview](running.md#pages).

## Controls {#controls}

In a window, Coney reads the keyboard and any gamepad SDL3 recognises, and turns them into the PS2 pad the game expects
([Front end](../research/frontend.md#coneys-implementation)). The legal screen ignores input, as in the original; the
menus, [the world viewer](running-viewers.md#the-world-viewer), [the character
viewer](running-viewers.md#the-character-viewer) and [a played level](running-play-level.md#playing-a-level) and [the
sandbox](sandbox.md) read port 1.

| PS2 pad | Gamepad (SDL3 names) | Keyboard (port 1) |
| --- | --- | --- |
| d-pad | d-pad | arrow keys |
| left stick | left stick | W A S D (full deflection) |
| right stick | right stick | none |
| cross | south (A on Xbox, cross on PlayStation) | K or Space |
| circle | east | L |
| square | west | J |
| triangle | north | I |
| START | start | Enter |
| SELECT | back | Backspace |
| L1, R1 | left and right shoulder | Q, E |
| L2, R2 | left and right trigger (held from a quarter of the travel) | 1, 3 |
| L3, R3 | stick presses | F, H |
| L3 and R3 together (the debug menu) | both stick presses | F4, or F and H together |
| none (the developer overlay) | none | F1 |

Gamepads can be connected and pulled out at any time. A gamepad that connects takes the first free port, port 1
first, and the log says so (`gamepad: NAME on port 1`); one pulled out frees its port and the other port keeps its
gamepad; a third waits for a free port. The keyboard always plays on port 1, alongside port 1's gamepad, and keeps
playing when it goes. Coney does not pause or show the original's controller-removed screen. On Windows, SDL's
DirectInput support is off: its device scan, which SDL repeats whenever any USB or Bluetooth device comes or goes,
can freeze the window for seconds. Xbox (XInput) gamepads and those SDL drives directly (PlayStation, Switch and
more) do not need it; for an older DirectInput-only gamepad, set the environment variable
`SDL_JOYSTICK_DIRECTINPUT=1` before starting Coney.

Escape quits, unless the developer overlay has the keyboard (a text box in it is being typed in); while it has the
keyboard, the keyboard does not play on port 1. A headless run reads no devices. The last two rows open the debug
menu and the overlay ([Opening it and the controls](debug-menu.md#controls)).

A gamepad's sticks are squared off like a DualShock 2's: a modern stick reports a circle, about 0.71 on each axis at a
full diagonal, which the game's per-axis dead zone turns into a walk; the DualShock 2 reaches both extremes there, so
Coney stretches each stick's circle onto the square and a full diagonal runs as it does on a PS2
([Pad record](../research/frontend.md#pad-record)). Straight pushes and W A S D are unchanged.

## Input scripts {#input-scripts}

`--input-script FILE` replaces the keyboard and gamepads with a script, so a test or a headless run gets the same
input every time without a human. Each line is `FRAME [p1|p2] ACTION ARGS`, where `FRAME` counts from 0 (the frame
the line takes effect on; a script puts Coney in test mode, where a frame is one fixed step); `#` starts
 a comment and blank lines are skipped. Frames must not go down from one line to
the next.

| Action | Meaning |
| --- | --- |
| `press BUTTON...` | hold the buttons from this frame on |
| `release BUTTON...` | let them go from this frame on |
| `tap BUTTON...` | hold them for this frame only: pressed on it, released on the next |
| `stick left X Y`, `stick right X Y` | move a stick; X and Y are whole numbers from -100 to 100, right and up positive |
| `steer [PERCENT] X,Y...` | push the left stick, at PERCENT (1 to 100, default 100) of its reach, toward each world point (metres) in turn as seen from player 1's feet and the camera's heading, recomputed every frame; a point counts as reached within 1 m, and the stick centres at the last one or at the next `stick left` line |
| `connect`, `disconnect` | plug the pad in or pull it out |

Buttons are `cross`, `circle`, `triangle`, `square`, `l1`, `r1`, `l2`, `r2`, `l3`, `r3`, `start`, `select`, `up`,
`down`, `left` and `right`. The port defaults to `p1`; port 1 starts connected and port 2 does not. A held button
reports full pressure. For example:

```text
# Press START on frame 150, then move down the menu and accept.
150 tap start
160 tap down
170 tap cross
200 p2 connect
```

```sh
build/dev/src/platform/coney --headless --frames 300 --input-script ../../scratch/menu.txt
```

A script that cannot be read or has a bad line stops Coney at start-up with exit code 1 and the line's number.
