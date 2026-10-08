# SPDX-License-Identifier: GPL-3.0-or-later
"""Adaptive pads for mission playthroughs: a course plays a stretch of a mission by what is on screen, the same way on
the original and on Coney (docs/guides/research-workflow.md#differential-playthroughs).

A fixed input script cannot finish a mission on two games: load times, the AI and the camera put the enemies and the
lessons at other updates. A course instead looks, once an update, at an Observation (where the player and the camera
are, the other humans, the world objects of interest) and at the events of that update (the script bindings called,
such as `HUDSetTutorialCallback`), and answers with the pad. Only the observation and the events are read, both games
give the same ones (coney_tools.mission), so both are driven by one policy with the same partial stick deflections.

`level99-tutorial` plays level99's checkpoint 1, the combat tutorial (docs/research/scripting.md#level99-lessons): it
walks into the objective markers of the first lesson, then to the nearest enemy and plays the armed lesson's moves,
the bats of lesson 9 included, until checkpoint 2. It is the policy of Coney's own CourseDriver
(tests/support/level99_course.h), written again over the observation; a lesson's moves are the play-through
script's.
"""

from __future__ import annotations

import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field

from coney_tools.events import Event, first_arguments
from coney_tools.input_script import BUTTONS, PadState, stick_byte

SQUARE, CROSS, CIRCLE, TRIANGLE = (BUTTONS[name] for name in ("square", "cross", "circle", "triangle"))
L1, R1, L2 = BUTTONS["l1"], BUTTONS["r1"], BUTTONS["l2"]


@dataclass(frozen=True)
class Seen:
    """Another human as the player sees it: where it stands, whether it is alive and on its feet, and whether it is of
    a gang hostile to the player's."""

    x: float
    y: float
    alive: bool
    standing: bool
    enemy: bool


@dataclass
class Observation:
    """What one update left on screen. `play` is false while no level is in play (a load, a menu); the rest is then
    empty. Headings and positions are the game's (metres, z up; degrees)."""

    frame: int
    play: bool = False
    player: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0)
    camera: tuple[float, float, float, float] = (0.0, 0.0, 0.0, 0.0)
    humans: list[Seen] = field(default_factory=list)
    objects: list[tuple[str, float, float, int]] = field(default_factory=list)


def stick_towards(observation: Observation, to: tuple[float, float], deflection: int) -> tuple[int, int]:
    """The left stick (x, y percent, y up) that walks the player towards `to` as the camera looks: up is away from the
    camera, right its right."""
    feet_x, feet_y = observation.player[0], observation.player[1]
    eye_x, eye_y, look_x, look_y = observation.camera
    fx, fy = look_x - eye_x, look_y - eye_y
    fn = max(math.hypot(fx, fy), 1e-4)
    fx, fy = fx / fn, fy / fn
    dx, dy = to[0] - feet_x, to[1] - feet_y
    dn = max(math.hypot(dx, dy), 1e-4)
    dx, dy = dx / dn, dy / dn
    right = dx * fy - dy * fx
    ahead = dx * fx + dy * fy
    return round(right * deflection), round(ahead * deflection)


def pad_of(buttons: int = 0, x: int = 0, y: int = 0) -> PadState:
    """A pad with `buttons` held and the left stick at (x, y) percent, y up."""
    sticks = [0x80, 0x80, stick_byte(max(-100, min(100, x))), stick_byte(max(-100, min(100, -y)))]
    return PadState(buttons, sticks)


@dataclass(frozen=True)
class Step:
    """One step of a move sequence: buttons held for `hold` updates with the stick at (x, y), then `wait` updates
    from the press before the next step."""

    buttons: int = 0
    wait: int = 0
    hold: int = 1
    x: int = 0
    y: int = 0


def _taps(buttons: int, count: int, gap: int, after: int) -> list[Step]:
    """`buttons` tapped `count` times, `gap` updates apart, the last followed by `after`."""
    return [Step(buttons, gap if i + 1 < count else after) for i in range(count)]


class Course:
    """A course's interface: see() each update's observation and events, then pad() for the next update."""

    name = ""
    #: The world object type prefixes the course needs observed.
    objects: tuple[str, ...] = ()

    def see(self, observation: Observation, events: Sequence[Event]) -> None:
        """Take in what the last update showed."""
        raise NotImplementedError

    def pad(self) -> PadState:
        """The pad for the next update."""
        raise NotImplementedError

    def status(self) -> str:
        """A line on how far the course is, for progress reports."""
        return ""


class Level99Tutorial(Course):
    """level99 checkpoint 1, the combat tutorial, to checkpoint 2 (the module docstring)."""

    name = "level99-tutorial"
    objects = ("dyn_w_mission", "dyn_bat_tuff")
    #: Plan metres: a strike's reach; how far a target may go before a sequence is dropped; inside a marker's box; at
    #: a bat; the snap's reach. Stick percentages: walking, aiming, walking to a bat. All as CourseDriver's.
    REACH, LEAVE_REACH, MARKER_REACH, PICK_UP_REACH, SNAP_REACH = 1.3, 2.5, 0.6, 0.5, 1.8
    WALK, AIM, PICK_UP = 60, 25, 40
    PICK_UP_RETRY = 40
    #: Updates of walking without headway before the player is taken to be held in place (_walk()).
    STUCK = 60
    SNAP_OFF_FACING = math.radians(60.0)

    def __init__(self) -> None:
        """A course at its start: no lesson armed yet."""
        self.observation = Observation(0)
        self.frame = 0
        self.lesson = ""  # the tutorial callback armed now (HUDSetTutorialCallback)
        self.checkpoint = ""
        self.markers_done = False  # a lesson has been armed: lesson 1's markers are over
        self.thrown = False  # lesson 8's callback has been seen: the bats come next
        self.armed = False  # lesson 9's callback has been seen: a bat was taken
        self.grab_next = False
        self.idle = False
        self.part = 0
        self.steps_lesson = ""
        self.steps: list[Step] = []
        self.step_index = 0
        self.held = Step()
        self.hold_until = 0
        self.wait_until = 0
        self.moves = 0
        self.walk_from: tuple[float, float, int] | None = None  # where and when the current walk last made headway
        self.free_next = False  # which move frees the player next (_walk())

    def see(self, observation: Observation, events: Sequence[Event]) -> None:
        """Track the armed lesson and the checkpoint from the script's calls."""
        self.observation = observation
        self.frame += 1
        for event in events:
            if event.kind != "call":
                continue
            arguments = first_arguments(event.detail)
            first = arguments[0].strip('"') if arguments and arguments[0] != "nil" else ""
            if event.name == "HUDSetTutorialCallback":
                self.lesson = first
                self.markers_done = self.markers_done or bool(first)
            elif event.name == "SetCheckPoint":
                self.checkpoint = first

    def status(self) -> str:
        """The armed lesson and the moves made."""
        return f"lesson {self.lesson or '-'}; moves {self.moves}"

    # --- what the player sees ---

    def _feet(self) -> tuple[float, float]:
        """The player's feet in plan."""
        return self.observation.player[0], self.observation.player[1]

    def _facing(self) -> tuple[float, float]:
        """The player's facing in plan (heading 0 faces +y, anticlockwise)."""
        heading = math.radians(self.observation.player[3])
        return -math.sin(heading), math.cos(heading)

    def nearest_enemy(self, off_facing: bool = False) -> tuple[float, float] | None:
        """The enemy to fight: one on his feet before one down, then the nearest; with `off_facing`, only a standing
        one within SNAP_REACH and more than SNAP_OFF_FACING off the facing."""
        feet = self._feet()
        ahead = self._facing()
        best: tuple[int, float, tuple[float, float]] | None = None
        for human in self.observation.humans:
            if not human.alive or not human.enemy:
                continue
            dx, dy = human.x - feet[0], human.y - feet[1]
            distance = math.hypot(dx, dy)
            if off_facing:
                cosine = (dx * ahead[0] + dy * ahead[1]) / max(distance, 1e-4)
                angle = math.acos(max(-1.0, min(1.0, cosine)))
                if not human.standing or distance > self.SNAP_REACH or angle < self.SNAP_OFF_FACING:
                    continue
            rank = 1 if human.standing else 0
            if best is None or rank > best[0] or (rank == best[0] and distance < best[1]):
                best = (rank, distance, (human.x, human.y))
        return best[2] if best else None

    def _nearest_object(self, prefix: str, shown_only: bool) -> tuple[float, float] | None:
        """The nearest observed world object whose type starts with `prefix` (with `shown_only`, only one shown: one a
        script showed, on Coney; on the original, any not hidden)."""
        feet = self._feet()
        found = [
            (math.hypot(x - feet[0], y - feet[1]), (x, y))
            for kind, x, y, shown in self.observation.objects
            if kind.startswith(prefix) and (not shown_only or shown == 1)
        ]
        return min(found)[1] if found else None

    def _distance(self, to: tuple[float, float]) -> float:
        """Plan metres from the feet to `to`."""
        feet = self._feet()
        return math.hypot(to[0] - feet[0], to[1] - feet[1])

    # --- the moves ---

    def sequence(self, lesson: str) -> list[Step]:
        """The armed lesson's moves (CourseDriver's), one part per walk for a lesson of several steps."""
        rage_special, grapple = SQUARE | CROSS, CROSS | CIRCLE
        part = self.part
        self.part += 1
        if lesson == "P1.BasicAttacks":
            if part % 3 == 0:
                return [Step(SQUARE, 30), Step(CROSS, 40)]
            if part % 3 == 1:
                return [Step(CIRCLE, 30), *_taps(SQUARE, 1, 6, 6), *_taps(CROSS, 4, 6, 40)]
            return [Step(CIRCLE, 90, 14), *_taps(SQUARE, 1, 6, 6), *_taps(CROSS, 11, 6, 40)]
        if lesson == "P1.LightCombos":
            return [*_taps(SQUARE, 2, 10, 56), *_taps(SQUARE, 3, 10, 50)]
        if lesson == "P1.HeavyCombos":
            heavy = [Step(SQUARE, 10), Step(CROSS, 56), *_taps(CROSS, 2, 10, 56)]
            return [*heavy, *_taps(SQUARE, 2, 10, 10), Step(CROSS, 50)]
        if lesson == "P1.Power":
            if part % 2 == 0:
                return [Step(rage_special, 60)]
            return [*_taps(grapple, 7, 6, 6), *_taps(rage_special, 2, 6, 28), *_taps(CROSS, 2, 28, 40)]
        if lesson == "P1.PowerMove":
            return [Step(CIRCLE, 45), Step(rage_special, 28), *_taps(CROSS, 2, 28, 50)]
        if lesson == "P1.Throws":
            return [Step(CIRCLE, 40), Step(0, 12, 12, 0, 100), Step(CIRCLE, 50, 3, 0, 100)]
        if lesson == "P1.Weapons":
            return [Step(SQUARE, 45), Step(CROSS, 45)]
        steps = [Step(L1 | R1, 12)]
        grab = self.grab_next
        self.grab_next = not self.grab_next
        if not grab:
            return [*steps, *_taps(rage_special, 3, 6, 60)]
        return [*steps, Step(CIRCLE, 30), Step(rage_special, 30), *_taps(CROSS, 2, 30, 60), *_taps(SQUARE, 3, 8, 40)]

    @staticmethod
    def idle_routine() -> list[Step]:
        """Between lessons: a cross tap (a dialog), six L2 taps and L1 held (lesson 3's targeting), then a pause."""
        return [Step(CROSS, 20), *_taps(L2, 6, 6, 6), Step(L1, 95, 75)]

    def _play(self, step: Step) -> PadState:
        """Start `step` now: its press, held for its updates, and the wait before the next."""
        self.held = step
        self.hold_until = self.frame + max(step.hold, 1)
        self.wait_until = self.frame + max(step.wait, step.hold)
        self.moves += 1 if step.buttons else 0
        return pad_of(step.buttons, step.x, step.y)

    def pad(self) -> PadState:
        """The pad for the next update, by CourseDriver's rules."""
        if self.frame < self.hold_until:
            return pad_of(self.held.buttons, self.held.x, self.held.y)
        if self.frame < self.wait_until or not self.observation.play:
            return pad_of()
        lesson = self.lesson
        if lesson == "P1.Throws":
            self.thrown = True
        if lesson in ("P1.Weapons", "P1.RageMoves"):
            self.armed = True
        # A sequence whose target went (or whose lesson ended) is dropped between two steps; its first always plays.
        if 1 < self.step_index < len(self.steps):
            enemy = self.nearest_enemy()
            ended = not self.idle and lesson != self.steps_lesson and not self.armed
            if ended or (not self.idle and (enemy is None or self._distance(enemy) > self.LEAVE_REACH)):
                self.steps, self.step_index = [], 0
        if self.step_index < len(self.steps):
            step = self.steps[self.step_index]
            self.step_index += 1
            return self._play(step)
        self.steps, self.step_index, self.idle = [], 0, False
        # Lesson 1: the objective markers, walked into.
        if not self.markers_done:
            marker = self._nearest_object("dyn_w_mission", True)
            if marker is not None:
                if self._distance(marker) > self.MARKER_REACH:
                    return self._walk(marker, self.WALK)
                return pad_of()
        # Lesson 9: after the throws, a bat taken (triangle at it) arms the lesson.
        if self.thrown and not self.armed and lesson != "P1.Throws":
            bat = self._nearest_object("dyn_bat_tuff", False)
            if bat is None:
                return pad_of()
            if self._distance(bat) <= self.PICK_UP_REACH:
                self.wait_until = self.frame + self.PICK_UP_RETRY
                self.moves += 1
                return pad_of(TRIANGLE)
            return self._walk(bat, self.PICK_UP)
        if lesson == "P1.Snaps":
            return self._snap()
        if not lesson and not self.armed:
            self.steps, self.idle = self.idle_routine(), True
            return pad_of()
        enemy = self.nearest_enemy()
        if enemy is None:
            return pad_of()
        if self._distance(enemy) > self.REACH:
            return self._walk(enemy, self.WALK)
        # In reach: one update of aim, then the lesson's sequence.
        x, y = stick_towards(self.observation, enemy, self.AIM)
        if lesson != self.steps_lesson:
            self.part = 0
        self.steps_lesson = lesson
        self.steps = self.sequence(lesson)
        self.wait_until = self.frame + 1
        self.walk_from = None
        return pad_of(0, x, y)

    def _walk(self, to: tuple[float, float], deflection: int) -> PadState:
        """A walk towards `to`; when the feet have not moved for STUCK updates (a hold the moves left open, a scene's
        pose), a move that frees the player instead: cross (a grab's finisher) and a throw (circle with the stick out)
        in turn, as CourseDriver does for a hold."""
        feet = self._feet()
        anchor = self.walk_from
        if anchor is None or math.hypot(feet[0] - anchor[0], feet[1] - anchor[1]) > 0.3:
            self.walk_from = (feet[0], feet[1], self.frame)
        elif self.frame - anchor[2] > self.STUCK:
            self.walk_from = None
            self.free_next = not self.free_next
            return self._play(Step(CIRCLE, 30, 3, 0, 100) if self.free_next else Step(CROSS, 30))
        return pad_of(0, *stick_towards(self.observation, to, deflection))

    def _snap(self) -> PadState:
        """Lesson 7: square with the stick full at a standing enemy close by and off the facing; else a short turn
        away from the nearest, or a walk to him."""
        off = self.nearest_enemy(off_facing=True)
        if off is not None:
            x, y = stick_towards(self.observation, off, 100)
            return self._play(Step(SQUARE, 45, 2, x, y))
        enemy = self.nearest_enemy()
        if enemy is None:
            return pad_of()
        if self._distance(enemy) > self.SNAP_REACH:
            return self._walk(enemy, self.WALK)
        feet = self._feet()
        side = (feet[0] - (enemy[1] - feet[1]), feet[1] + (enemy[0] - feet[0]))
        x, y = stick_towards(self.observation, side, self.WALK)
        return self._play(Step(0, 10, 8, x, y))


class Level99Street(Course):
    """level99 checkpoint 2 up to the stolen car radio (docs/research/crimes.md#stores, #stereo): it stands
    before each loot item in the store's cabinets in turn, breaks the pane with square and takes the item with
    triangle; once the script spawns the radio (`CarSpawnRadio`) it walks to the car stereo, breaks the window with
    square, starts the theft with triangle and turns the left stick anticlockwise, fully out, about 1.5 turns a
    second, until the script's `P2.CarRadioStolen` runs."""

    name = "level99-street"
    LOOT = ("dyn_pwatch", "dyn_ring")
    objects = (*LOOT, "dyn_carstereo")
    #: Plan metres: the stand-off before an item's pane (the panes are 0.3 m in front of the items) and around a
    #: stereo; close enough to a stand point. Stick percentages: walking, aiming.
    STAND_OFF, STEREO_OFF, AT_STAND = 0.9, 1.0, 0.25
    WALK, AIM = 60, 25
    #: Updates without headway before a walk counts as arrived (a car body in the way). The attempts before an item
    #: or a stereo is given up: a stereo is tried from eight sides in turn, since only a strike from beside it breaks
    #: its window (window 15, docs/research/cars.md) and the car's heading is not observed. Updates of stick turning
    #: per theft attempt, and the stick's turn per update in degrees (1.5 turns a second).
    STUCK, TRIES, STEREO_TRIES, TURNING, TURN = 40, 3, 8, 720, 9.0

    def __init__(self) -> None:
        """A course at its start: looting."""
        self.observation = Observation(0)
        self.frame = 0
        self.radio = False  # the script spawned the car radio: the loot is over
        self.busy_until = 0
        self.held = PadState(0, [0x80] * 4)
        self.hold_until = 0
        self.steps: list[Step] = []
        self.tries: dict[tuple[float, float], int] = {}
        self.target: tuple[float, float] | None = None
        self.turn_from = 0  # the update the stick began turning; -1 about to, 0 not turning
        self.angle = 0.0
        self.walk_from: tuple[float, float, int] | None = None

    def see(self, observation: Observation, events: Sequence[Event]) -> None:
        """Note the radio's spawn."""
        self.observation = observation
        self.frame += 1
        self.radio = self.radio or any(e.kind == "call" and e.name == "CarSpawnRadio" for e in events)

    def _feet(self) -> tuple[float, float]:
        """The player's feet in plan."""
        return self.observation.player[0], self.observation.player[1]

    def status(self) -> str:
        """The phase, the player's feet, the target and the objects given up on."""
        feet = self._feet()
        stereos = sum(kind.startswith("dyn_carstereo") for kind, *_ in self.observation.objects)
        phase = "radio" if self.radio else "loot"
        return f"{phase}; feet {feet[0]:.1f},{feet[1]:.1f}; target {self.target}; stereos {stereos}; tries {self.tries}"

    def _nearest(self, prefixes: Sequence[str]) -> tuple[float, float] | None:
        """The nearest observed object of one of `prefixes` not given up on."""
        feet = self._feet()
        found = [
            (math.hypot(x - feet[0], y - feet[1]), (round(x, 1), round(y, 1)))
            for kind, x, y, _shown in self.observation.objects
            if kind.startswith(tuple(prefixes))
            and self.tries.get((round(x, 1), round(y, 1)), 0) < (self.STEREO_TRIES if self.radio else self.TRIES)
        ]
        return min(found)[1] if found else None

    def _stand(self, item: tuple[float, float]) -> tuple[float, float]:
        """Where to stand before `item`: STAND_OFF out from its cabinet's pane, towards the room. A cabinet's items
        stand in a row, so the row's axis (the items sharing the item's x, or its y) gives the pane's facing, and the
        middle of all the loot the side the room is on."""
        loot = [(x, y) for kind, x, y, _shown in self.observation.objects if kind.startswith(self.LOOT)]
        middle = (sum(x for x, _ in loot) / len(loot), sum(y for _, y in loot) / len(loot)) if loot else item
        same_x = sum(abs(x - item[0]) < 0.15 for x, _ in loot)
        same_y = sum(abs(y - item[1]) < 0.15 for _, y in loot)
        if same_x > same_y:
            return item[0] + math.copysign(self.STAND_OFF, middle[0] - item[0]), item[1]
        return item[0], item[1] + math.copysign(self.STAND_OFF, middle[1] - item[1])

    def _arrived(self, to: tuple[float, float], reach: float) -> bool:
        """Whether the feet are within `reach` of `to`, or have made no headway for STUCK updates of walking."""
        feet = self._feet()
        if math.hypot(to[0] - feet[0], to[1] - feet[1]) <= reach:
            self.walk_from = None
            return True
        anchor = self.walk_from
        if anchor is None or math.hypot(feet[0] - anchor[0], feet[1] - anchor[1]) > 0.2:
            self.walk_from = (feet[0], feet[1], self.frame)
        elif self.frame - anchor[2] > self.STUCK:
            self.walk_from = None
            return True
        return False

    def _play(self, steps: list[Step]) -> PadState:
        """Start a move sequence now."""
        self.steps = steps
        return self.pad()

    def pad(self) -> PadState:
        """The pad for the next update."""
        if self.frame < self.hold_until:
            return self.held
        if self.frame < self.busy_until or not self.observation.play:
            return pad_of()
        if self.steps:
            step = self.steps.pop(0)
            self.held = pad_of(step.buttons, step.x, step.y)
            self.hold_until = self.frame + max(step.hold, 1)
            self.busy_until = self.frame + max(step.wait, step.hold)
            return self.held
        if self.turn_from:
            return self._turn()
        if self.target is not None:
            self.tries[self.target] = self.tries.get(self.target, 0) + 1
            self.target = None
        if self.radio:
            stereo = self._nearest(("dyn_carstereo",))
            if stereo is None:
                return pad_of()
            side = math.radians(45.0 * self.tries.get(stereo, 0))
            stand = (stereo[0] + self.STEREO_OFF * math.cos(side), stereo[1] + self.STEREO_OFF * math.sin(side))
            if not self._arrived(stand, self.AT_STAND):
                return pad_of(0, *stick_towards(self.observation, stand, self.WALK))
            self.target = stereo
            aim = stick_towards(self.observation, stereo, self.AIM)
            self.turn_from = -1
            return self._play([Step(0, 1, 1, *aim), Step(SQUARE, 40), Step(TRIANGLE, 20)])
        item = self._nearest(self.LOOT)
        if item is None:
            return pad_of()
        stand = self._stand(item)
        if not self._arrived(stand, self.AT_STAND):
            return pad_of(0, *stick_towards(self.observation, stand, self.WALK))
        self.target = item
        aim = stick_towards(self.observation, item, self.AIM)
        return self._play([Step(0, 1, 1, *aim), Step(SQUARE, 40), *_taps(TRIANGLE, 3, 20, 20)])

    def _turn(self) -> PadState:
        """The theft: after the window and triangle steps, the left stick turned anticlockwise for TURNING updates."""
        if self.turn_from < 0:
            self.turn_from = self.frame
        if self.frame - self.turn_from > self.TURNING:
            self.turn_from = 0
            return pad_of()
        self.angle += math.radians(self.TURN)
        return pad_of(0, round(100 * math.cos(self.angle)), round(100 * math.sin(self.angle)))


#: The courses by name.
COURSES: dict[str, Callable[[], Course]] = {Level99Tutorial.name: Level99Tutorial, Level99Street.name: Level99Street}
