# SPDX-License-Identifier: GPL-3.0-or-later
"""The environment variables the tools read, which `conftest.py` clears before every test.

A test must give the same answer on every machine and in every shell. The shell that runs pytest is not clean: inside a
task it has `CONEY_TASK` and `VIRTUAL_ENV`, inside an agent tool `CLAUDECODE` and `CONEY_AGENT_TOKEN`, in CI
`GITHUB_REPOSITORY`, and on a player's machine `CONEY_DISC`. Each of these changes what a tool answers (which task a
command is about, whose build it is, where the disc is), so a test that does not set it itself would pass in one shell
and fail in another. A test that needs one sets it with `monkeypatch.setenv`.
"""

#: Every variable a tool reads from the caller's environment and that a test must not inherit, and what it changes.
#: `test_test_environment.py` fails when the code reads a variable that is in none of the tables of this module.
CLEARED: dict[str, str] = {
    # Which task and which agent a command is about.
    "CONEY_TASK": "the task a command is about (`unit.task_name`, `task start`), ahead of the branch",
    "CONEY_AGENT": "the id `check changed` names its builds with",
    "CONEY_AGENT_TOKEN": "the hook's token, which names the verified identity (`identity.load`)",
    # Never read by a tool (the id, type and model come from the token only); cleared so the tests that prove it hold.
    "CONEY_AGENT_ID": "an agent's word about itself, which no tool trusts",
    "CONEY_AGENT_TYPE": "an agent's word about itself, which no tool trusts",
    "CONEY_AGENT_MODEL": "an agent's word about itself, which no tool trusts",
    "CONEY_AGENT_INJECTED": "the marker of an injected token, which no tool trusts",
    # The Python and Claude Code environments around the run.
    "VIRTUAL_ENV": "the environment of the shell, which `uv run` and the slot guard look at",
    "CLAUDECODE": "set inside an agent tool: `build` then requires `--agent`",
    "CLAUDE_PROJECT_DIR": "the checkout the hooks and the MCP launcher start from",
    # The machine's own setup.
    "CONEY_DISC": "the game's disc: `build --test` passes it on, `task check` refuses one that is missing",
    "CONEY_FFMPEG": "where `ffmpeg` is (`clips`)",
    "CONEY_WINDOW_DISPLAY": "the display a PCSX2 window is placed on",
    "CONEY_DEPS_SOURCES": "the shared dependency sources (`shared_deps`)",
    "GHIDRA_MCP_URL": "the Ghidra server `mcp_serve` points the MCP client at",
    "PCSX2_DEBUG_PORT": "the PCSX2 copy's debug port (`mcp_serve`)",
    "PCSX2_PINE_PORT": "the PCSX2 copy's PINE port (`mcp_serve`)",
    # The compiler and the Java setup the build, doctor and Ghidra tools look at.
    "CMAKE_C_COMPILER_LAUNCHER": "ccache for the C compiler: the compiler cache checks it first (`compiler_cache`)",
    "CMAKE_CXX_COMPILER_LAUNCHER": "ccache for the C++ compiler: the build's compiler cache checks it first",
    "CXX": "the C++ compiler `doctor` says CMake will use",
    "VSCMD_VER": "set in a Visual Studio prompt: `build` then does not load the Visual Studio environment",
    "JAVA_HOME": "the JDK the Ghidra tools and `workspace` checks look for",
    "UV": "the `uv` that started coney-tools, which `mcp serve` reuses",
    # The docs build and CI.
    "CONEY_PROGRESS_HISTORY": "the progress history file (`mkdocs_hooks.py`)",
    "CONEY_PROGRESS_HISTORY_CACHE": "the previous history, whose days are kept (`mkdocs_hooks.py`)",
    "CONEY_SITE_URL": "the docs site's address (`mkdocs_hooks.py`)",
    "GITHUB_REPOSITORY": "the repository CI runs in (`audit_files`, `mkdocs_hooks.py`)",
    "GITHUB_SERVER_URL": "the GitHub server CI runs on (`mkdocs_hooks.py`)",
    "GITHUB_OUTPUT": "the file a CI step writes its outputs to (`ci_cli`)",
}

#: Variables a tool reads that are the machine's own and that every program needs, so they stay: clearing `PATH` would
#: stop every subprocess a test starts.
KEPT: dict[str, str] = {
    "PATH": "where programs are found (`build_run`, `ghidra_headless`)",
    "ProgramFiles(x86)": "where Windows keeps the Visual Studio installer (`build_run`, `workspace_checks`)",
}

#: Names the code spells that are not variables the caller sets: a tool sets them for a child process, so what the
#: caller had is overwritten and cannot change a result.
NOT_READ: dict[str, str] = {
    "CONEY_COMMAND_LINE": "set for the PCSX2 placer's child (`pcsx2_proc`)",
    "CONEY_DIRECTORY": "set for the PCSX2 placer's child (`pcsx2_proc`)",
    "GHIDRA_MCP_LAZY": "set for the Ghidra MCP server's child (`mcp_serve`)",
    "PCSX2_REQUIRE_AGENT": "set for the PCSX2 MCP server's child (`mcp_serve`)",
}

#: Variables a test reads on purpose, and which therefore reach it: a developer's switch that changes how the suite
#: runs, not something a tool answers from. Not cleared, and named here so that no one adds them to `CLEARED`.
OPT_IN: dict[str, str] = {
    "CI": "GitHub sets it; the Conclave boundary tests fail instead of skip under it",
    "CONEY_ENGINE_STUB": "names another engine stub for the Conclave boundary tests",
    "CONEY_TYPESCRIPT": "names the TypeScript compiler for the Conclave boundary tests",
    "CONEY_UPDATE_FIXTURES": "writes the Conclave fixture again instead of comparing with it",
}
