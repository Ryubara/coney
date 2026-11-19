# SPDX-License-Identifier: GPL-3.0-or-later
"""The checks `task start` makes before it creates anything (docs/guides/issues.md#starting): that the claim on each
issue is still the engineer's, that the engineer holds no more open claims than the limit allows, and that the unit
has a place in its track (`task_slots`). Each returns why the start must be refused, or None; `task_start` calls them
in that order. They are read-only: GitHub through `Remote` and `Engine`, the scratch folder through `task_slots`."""

from __future__ import annotations

from typing import TYPE_CHECKING

from coney_tools import task_slots
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import Card
from coney_tools.issue_claim_remote import Remote
from coney_tools.issue_claim_rules import limit_for, open_task_keys
from coney_tools.task_file import TaskFile
from coney_tools.unit_record import UnitRecord

if TYPE_CHECKING:
    from coney_tools.task_start import StartEnv


def slot_check(task: TaskFile, env: StartEnv, engine: Engine) -> tuple[str, str | None]:
    """(the tracks line for the report, the refusal when the limit is enforced and exceeded). Whether a unit is a
    workflow, Studio, networking or renderer unit comes from the held claim cards `engine` read (their `workflow`,
    `studio`, `networking` and `renderer`), not from any file here; the room is `task_slots.room`, the rule the Agent
    guard applies."""
    conventions = env.conventions
    slots = conventions.tasks.start.slots
    held_cards = engine.held_cards()
    cards = task_slots.Cards(held_cards.get)
    told = task_slots.unit_labels(task.issues, cards, conventions)
    if told is not None and told.workflow:
        return "slots: a workflow task (its issue has the workflow label) holds no place until the cutover", None

    def labels(record: UnitRecord) -> task_slots.Labels | None:
        """Another unit's labels, from the same cards."""
        return task_slots.unit_labels(record.issues, cards, conventions)

    track = task_slots.track_of(
        task.kind,
        told.studio if told is not None else False,
        told.networking if told is not None else False,
        told.renderer if told is not None else False,
    )
    current = task_slots.scan(env.scratch, conventions, env.now(), labels=labels)
    # A kept place whose track had nothing to start when the fill check last looked is open (tasks.start.slots.lapse).
    lapsed = task_slots.read_lapses(env.scratch, conventions, env.now())
    fit = task_slots.room(current, track, task.task, conventions, env.build_slots, task.kind, lapsed)
    line = f"slots: {fit.note(task.task)}"
    if fit.fits:
        return line, None
    if not slots.enforce:
        return line + " (over the limit, which is not enforced: tasks.start.slots.enforce)", None
    busy = [holder for holder in current.holders if holder.task != task.task]
    if fit.track_full:
        why = f"the {track} track is full ({fit.count} of {fit.maximum})"
    else:
        kept = "".join(f", {places} kept for {name}" for name, places in fit.kept.items())
        why = f"the machine has no place for a {track} unit ({fit.total} of {fit.machine} in use{kept})"
    return line, (
        f"{why} (tasks.start.slots in .github/conventions.yml, build_slots {env.build_slots}): "
        f"{', '.join(f'{h.line()} [{h.track}]' for h in busy)}. Finish one (it passes Review) before starting another"
    )


def claim_problem(task: TaskFile, user: str, expected_task: str, engine: Engine, remote: Remote) -> str | None:
    """Why the claim on any issue of the task is not `user`'s for `expected_task`, or None while it holds."""
    held = engine.held_cards()
    for number in task.issues:
        issue = remote.issue(number)
        if issue.is_pr or issue.state != "open":
            return f"#{number} is closed" if issue.state != "open" else f"#{number} is a pull request"
        card: Card | None = held.get(number)
        if card is None:
            return f"#{number}: no claim card is held (it was never made, or it was released): ask the coordinator"
        if card.claimant != user:
            return f"#{number} is claimed by {card.claimant}, not {user}"
        if not card.task:
            return (
                f"#{number}: the claim has no task id (it was made by hand, not by `task create`): "
                "ask the coordinator to claim it again with task create"
            )
        if card.task != expected_task:
            return f"#{number}: the claim was made for the task '{card.task}', not '{expected_task}'"
    return None


def limit_problem(task: TaskFile, user: str, engine: Engine) -> str | None:
    """Why `user` holds more open claims than the limit allows, or None (a fix task reuses its claim).

    A card's `exempt` flag follows the issue's labels (`claims.exempt_label`, `claims.studio_label`,
    `claims.networking_label`, `claims.renderer_label`) only when a claim run passes over it, and a burst of label
    events leaves some cards stale until the sweep (GitHub cancels pending jobs); so the labels are read from each of
    the user's issues here, as `task create`'s checks read them, and a stale card cannot refuse the start.
    """
    rules = engine.rules
    held = [
        card
        if card.exempt or card.claimant != user
        else card.changed(exempt=rules.outside_limit(engine.remote.issue(card.issue).labels))
        for card in engine.held_cards().values()
    ]
    open_tasks = open_task_keys(held, user, 0)
    maintainer = engine.is_maintainer(user)
    limit = limit_for(maintainer, engine.rules)
    if len(open_tasks) > limit:
        return (
            f"{user} holds {len(open_tasks)} open claim(s) ({limit} at most for "
            f"{'a maintainer' if maintainer else 'a contributor'}): give one back with /unclaim first"
        )
    return None
