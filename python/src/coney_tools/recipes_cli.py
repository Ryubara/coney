# SPDX-License-Identifier: GPL-3.0-or-later
"""`coney-tools pcsx2 recipe list | show | run | render`: the shared recipes of research/pcsx2-recipes.toml.

`run` works only on a copy the agent holds a claim on and whose PCSX2 runs (the claims of pcsx2_claims.py), over that
copy's PINE port and, for the emulation steps, its debug server port: never someone else's game.

Research: docs/guides/pcsx2-recipes.md
"""

from __future__ import annotations

import json
from collections.abc import Sequence
from pathlib import Path
from typing import Any

from coney_tools import hooks as hook_tools
from coney_tools import pcsx2_claims as claims
from coney_tools import recipes, recipes_render, recipes_run
from coney_tools.config import ConfigError, find_repo_root
from coney_tools.debug_server import DebugClient, DebugServerError
from coney_tools.pcsx2_state import StateError, load_patches
from coney_tools.pine import PineClient, PineError
from coney_tools.scenario import GOAL_TYPES_FILE, load_goal_types


def add_commands(commands: Any) -> None:
    """Register `pcsx2 recipe ...` under the pcsx2 group's subcommands."""
    recipe = commands.add_parser("recipe", help="the shared PCSX2 recipes: find, move, edit, call (pcsx2-recipes.toml)")
    sub = recipe.add_subparsers(dest="recipe_command", required=True)
    sub.add_parser("list", help="list every recipe with its one-line summary")
    show = sub.add_parser("show", help="print one recipe: what it does, its parameters, preconditions and steps")
    show.add_argument("name")
    run = sub.add_parser("run", help="run a recipe on a PCSX2 copy you hold a claim on")
    run.add_argument("name")
    run.add_argument("--copy", required=True, help="the claimed copy (pcsx2, pcsx2-b, ...)")
    run.add_argument("--agent", required=True, help="your id (must hold the claim)")
    run.add_argument("--arg", action="append", default=[], metavar="NAME=VALUE", help="a parameter; repeatable")
    run.add_argument("--json", action="store_true", help="print what the show steps showed as one JSON line too")
    render = sub.add_parser("render", help="write docs/guides/pcsx2-recipes.md from the recipe file")
    render.add_argument("--check", action="store_true", help="write nothing; exit 1 when the page is out of date")


def run_command(args: Any) -> int:
    """Dispatch a `pcsx2 recipe` command."""
    try:
        if args.recipe_command == "render":
            return recipes_render.run_render(args.check)
        book = recipes.load(find_repo_root(Path.cwd()))
        if args.recipe_command == "list":
            return run_list(book)
        if args.recipe_command == "show":
            return run_show(book, args.name)
        return run_recipe(book, args.name, args.copy, args.agent, args.arg, args.json)
    except recipes.RecipeError as error:
        raise ConfigError(str(error)) from error


def _recipe(book: recipes.RecipeBook, name: str) -> recipes.Recipe:
    """The named recipe, or RecipeError listing the names."""
    if name not in book.recipes:
        raise recipes.RecipeError(f"no recipe {name!r}; there are {', '.join(book.recipes)}")
    return book.recipes[name]


def run_list(book: recipes.RecipeBook) -> int:
    """`recipe list`: every recipe by group, with its summary."""
    width = max(len(name) for name in book.recipes)
    for group in book.groups:
        members = [r for r in book.recipes.values() if r.group == group]
        if not members:
            continue
        print(f"{group}:")
        for recipe in members:
            print(f"  {recipe.name:<{width}}  {recipe.summary}")
    return 0


def run_show(book: recipes.RecipeBook, name: str) -> int:
    """`recipe show`: the recipe as the page shows it, in plain text."""
    recipe = _recipe(book, name)
    print("\n".join(recipes_render.recipe_text(recipe)))
    return 0


def parse_args(pairs: Sequence[str]) -> dict[str, str]:
    """`NAME=VALUE` pairs as a dict. Raises RecipeError for one without `=`."""
    out: dict[str, str] = {}
    for pair in pairs:
        name, sep, value = pair.partition("=")
        if not sep or not name:
            raise recipes.RecipeError(f"--arg {pair!r}: write NAME=VALUE")
        out[name.strip()] = value
    return out


def _uses_debug(recipe: recipes.Recipe) -> bool:
    """Whether a step needs the debug server."""
    return any("emulation" in step or "advance" in step for step in recipe.steps)


def run_recipe(book: recipes.RecipeBook, name: str, copy: str, agent: str, pairs: Sequence[str], as_json: bool) -> int:
    """`recipe run`: check the claim, the emulation state and the patches, then make the steps."""
    recipe = _recipe(book, name)
    values = recipes_run.bind_args(recipe, parse_args(pairs))
    if recipe.state:
        # Informational: the runner cannot tell which state the copy was started from.
        print(
            f"note: {name} was confirmed from the library state {recipe.state} "
            f"(`coney-tools pcsx2 states use {recipe.state} --copy {copy} --agent {agent} --launch`)"
        )
    registry = claims.default_registry()
    registry.require_claim(agent, copy)
    held = registry.copy(copy)
    row = next(r for r in registry.status() if r.copy.name == held.name)
    if not row.pids:
        raise ConfigError(f"{copy} runs no PCSX2; start it with `coney-tools pcsx2 launch --agent {agent} STATE`")
    if held.port is None:
        raise ConfigError(f"{copy}: PINE is off in its ini (`coney-tools pcsx2 setup-copies`)")
    root = find_repo_root(Path.cwd())
    try:
        patches = load_patches(root / recipes.PATCHES_DIR)
        hooks = hook_tools.load_hooks(root / recipes.PATCHES_DIR)
    except (StateError, hook_tools.HookError) as error:
        raise ConfigError(str(error)) from error
    try:
        with PineClient(held.port) as pine:
            status = pine.status()
            if recipe.emulation != "either" and status != recipe.emulation:
                raise recipes.RecipeError(f"{name} needs emulation {recipe.emulation}; {copy} is {status}")
            recipes_run.check_patches(pine, recipe.patches, patches, hooks)
            debug = None
            if _uses_debug(recipe):
                if held.debug is None:
                    raise recipes.RecipeError(f"{name} needs {copy}'s debug server, which is off in its ini")
                debug = DebugClient(held.debug)
            try:
                shown = recipes_run.Runner(pine, debug, goal_types=load_goal_types(root / GOAL_TYPES_FILE)).run(
                    recipe, values
                )
            finally:
                if debug is not None:
                    debug.close()
    except (PineError, DebugServerError) as error:
        raise ConfigError(f"{copy}: {error}") from error
    if as_json:
        print(json.dumps({key: value for key, value in shown.items()}, default=str))
    return 0
