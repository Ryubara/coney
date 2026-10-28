# SPDX-License-Identifier: GPL-3.0-or-later
"""`claim unclaim` and `claim renew`: post a claim command for the signed-in account and wait for the Action's answer.

Nobody writes a command comment by hand: `task create` posts `/claim`, the stage tools post `/progress`, and these two
post `/unclaim` (give a claim back) and `/claim renew` (start its idle clock again) the same way, through
`Remote.post_command`, so the Action handles them like anyone's (docs/guides/issues.md#claiming). Each checks first what
the Action would refuse, with the Action's words, and posts nothing then; after posting it waits, at most
`claims.create_wait_seconds` per issue, until the card lists the command as handled or a reply answers it; after a
wait that runs out it stops, since the Action runs one job at a time and the next issue would wait as long, and names
the issues it did not try. Waiting goes through an injected clock and sleep, so the tests never wait.
"""

from __future__ import annotations

from collections.abc import Callable
from dataclasses import dataclass
from datetime import datetime, timedelta

from coney_tools.conventions import Conventions
from coney_tools.issue_claim_engine import Engine
from coney_tools.issue_claim_model import reply_target
from coney_tools.issue_claim_pass import latest_held, stored_cards
from coney_tools.issue_claim_remote import Remote

#: The command each verb posts.
COMMANDS = {"unclaim": "/unclaim", "renew": "/claim renew"}


@dataclass
class PostEnv:
    """The world the commands work in; every part can be replaced in a test."""

    remote: Remote
    conventions: Conventions
    now: Callable[[], datetime]
    sleep: Callable[[float], None]
    out: Callable[[str], None] = print


def _refusal(env: PostEnv, engine: Engine, verb: str, number: int, user: str) -> str | None:
    """What the Action would answer instead of acting, or None when the command will be acted on."""
    issue = env.remote.issue(number)
    if issue.is_pr:
        return f"#{number} is a pull request"
    held = latest_held(stored_cards(env.remote.comments(number), env.conventions.claims))
    if held is None:
        return f"#{number} has no claim" + (" to give up" if verb == "unclaim" else " to renew")
    claimant = held.card.claimant
    if verb == "renew" and claimant != user:
        return f"#{number} is claimed by {claimant}: only the claimant can renew a claim"
    if verb == "unclaim" and claimant != user and not engine.is_maintainer(user):
        return f"#{number} is claimed by {claimant}: only {claimant} or a maintainer can give this claim up"
    return None


def _answer(env: PostEnv, number: int, command_id: int) -> str | None:
    """The Action's answer to command `command_id` on issue `number`: `done`, a refusal's text, or None (not yet)."""
    rules = env.conventions.claims
    comments = env.remote.comments(number)
    for comment in comments:
        if comment.login == rules.bot_login and reply_target(comment.body, rules) == command_id:
            return comment.body.split("\n", 1)[1] if "\n" in comment.body else comment.body
    if any(command_id in stored.card.handled for stored in stored_cards(comments, rules)):
        return "done"
    return None


def _wait(env: PostEnv, number: int, command_id: int) -> str | None:
    """Wait for the answer to a command, looking every `claims.poll_interval_seconds`; None when time ran out."""
    rules = env.conventions.claims
    started = env.now()
    while True:
        answer = _answer(env, number, command_id)
        if answer is not None or env.now() - started >= timedelta(seconds=rules.create_wait_seconds):
            return answer
        env.sleep(rules.poll_interval_seconds)


def post_claim_command(verb: str, numbers: list[int], env: PostEnv, wait: bool = True) -> int:
    """Post `/unclaim` or `/claim renew` on each issue; 0 when every one was acted on (or posted, with no wait), 1
    otherwise. An issue the Action would refuse gets nothing posted."""
    text = COMMANDS[verb]
    user = env.remote.current_user()
    engine = Engine(env.remote, env.conventions, env.now)
    failed = 0
    for index, number in enumerate(numbers):
        problem = _refusal(env, engine, verb, number, user)
        if problem is not None:
            env.out(f"refused: {problem}; nothing posted")
            failed += 1
            continue
        command_id = env.remote.post_command(number, text)
        if not wait:
            env.out(f"#{number}: posted {text} (comment {command_id}); the claim Action answers it")
            continue
        answer = _wait(env, number, command_id)
        if answer == "done":
            env.out(f"#{number}: {text} done")
        elif answer is None:
            env.out(
                f"#{number}: posted {text}, and no answer came within {env.conventions.claims.create_wait_seconds} s; "
                "the next claim run or sweep acts on it (`claim list` shows the claims)"
            )
            rest = numbers[index + 1 :]
            if rest:
                env.out(f"not tried (the Action is slow): {', '.join(f'#{n}' for n in rest)}; run the command again")
                return 1
            failed += 1
        else:
            env.out(f"#{number}: refused by the claim Action: {answer}")
            failed += 1
    return 1 if failed else 0
