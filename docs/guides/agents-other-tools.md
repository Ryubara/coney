# Agent tools other than Claude Code

How Coney reaches an agent tool that is not Claude Code: what it reads, the hooks the repository ships for it, the
commands that stand in for what a hook cannot see, and, for Codex, its repository-level setup from OpenAI's own pages.
The contributor's short version is in [CONTRIBUTING.md](repo:CONTRIBUTING.md#agents-other-than-claude-code); how the
project's own agents are set up is in [Running agents](agents.md#setup).

Coney never relies on instructions alone, in any tool: the repository ships a hook file for every agent tool that reads
hooks from a project (Codex `.codex/hooks.json`, Gemini CLI `.gemini/settings.json`, Antigravity `.agents/hooks.json`,
Cursor `.cursor/hooks.json`, GitHub Copilot `.github/hooks/coney.json`, Grok Build `.grok/hooks/coney.json`), and each
runs the dispatcher of [The hooks](agents.md#hooks) through an adapter, so the same guards refuse a raw `cmake --build`,
a hand-typed `gh pr create` or a push before the command runs. Such a session works as `contributor` unless
`CONEY_AGENT_ROLE` names another role, and the person trusts the hooks once in the tool. The table of tools, their
events, deny formats and trust rules, and what stays Claude Code's only (the `Agent` tool's rules, the identity token,
the trailer guard): [Agent tools and their hooks](enforcement.md#harnesses). Supported tools (maintainer decisions of
2026-10-27 and 2026-10-29): Codex, Gemini CLI, Antigravity, Cursor, GitHub Copilot and Grok Build, the Codex,
Antigravity and Grok command-line tools among them. Another tool reads the rules from `AGENTS.md`, can take the three MCP
servers (`ghidra-mcp`, `pcsx2`, `xemu`) as its own entries (the same `uv run --isolated --frozen --project python coney-tools mcp serve <name>` command,
run from the checkout), and runs the checks that come after the hooks through three commands that need no Claude Code,
because every program that decides a rule is a `coney-tools` command or a git hook:

| When | Command | What it does |
| --- | --- | --- |
| Once per clone or environment | `uv run --project python coney-tools doctor` | Checks the machine and says how to fix each problem ([Check your setup](building.md#doctor)) |
| Once per clone or environment | `uvx pre-commit install` | Installs the `pre-commit` and `commit-msg` git hooks: the file checks run on every commit and a message that breaks the trailer rules is refused, whatever tool makes the commit |
| Before opening or updating a pull request | `uv run --project python coney-tools ci pr --title "<title>" --body <file>` | CI's `title`, `body` and `docs` jobs on your text, then [`ci run`](coney-tools-ci.md#ci-run), then the engine build and tests when the change touches them ([`ci pr`](coney-tools-ci.md#ci-pr)) |

The contributor's version of this, with what to write in the description, is in
[CONTRIBUTING.md](repo:CONTRIBUTING.md#agents-other-than-claude-code). The hooks are the first gate; `ci pr`, the
pre-commit hooks and CI are the backstop, for a person who has not trusted the hooks or turned them off.

**Codex.** What follows is from OpenAI's own pages (read 2026-10-26; they moved from developers.openai.com to
learn.chatgpt.com), and says where a page is silent.

- **The rules.** Codex reads `AGENTS.md`: first the one in its home folder (`~/.codex`, or `CODEX_HOME`), then, from the
  repository root (the Git root) down to the folder it runs in, one file per folder, `AGENTS.override.md` before
  `AGENTS.md` before any name in `project_doc_fallback_filenames`; files nearer the folder come later and win. It builds
  the chain once per run, stops adding files at `project_doc_max_bytes` (32 KiB by default, all files together) and
  loads only the file names above, so a file that `AGENTS.md` merely links to is not part of the chain, and the
  `@AGENTS.md` import of `CLAUDE.md` is Claude Code's. So the rules a Codex run must follow are in `AGENTS.md` itself
  or named in the task, and `AGENTS.md` stays small (9 KB now; a test holds it under half of Codex's limit, so the
  rest of the chain has room). Source: [Custom instructions with AGENTS.md](https://learn.chatgpt.com/docs/agent-configuration/agents-md).
- **Settings in the repository.** A project's `.codex/config.toml` and `.codex/hooks.json` are read only when the
  project is trusted, and a hook is skipped until a person has reviewed it with `/hooks` (Codex records trust against
  the hook's hash). A `PreToolUse` hook can deny a shell command or an edit, which is what `.claude/hooks/` does for
  Claude Code, and the repository ships `.codex/hooks.json`, which runs the same dispatcher
  ([Agent tools](enforcement.md#harnesses)); OpenAI calls hooks "a useful guardrail, not a complete enforcement
  boundary", so the git hooks, `ci pr` and CI stay behind them. Source:
  [Hooks](https://learn.chatgpt.com/docs/hooks).
- **MCP.** `codex mcp add <name> -- <command>` (or a `[mcp_servers.<name>]` table in `~/.codex/config.toml`, or in a
  trusted project's `.codex/config.toml`) registers a stdio server. The page names no project-level `.mcp.json` (it
  mentions that file only for servers a plugin provides), so assume Codex does not read ours and give it the three
  commands. For the cloud it says only "Local MCP servers and transports may not be available in the cloud; check
  connectivity and authentication separately", and not that a configured server runs there. Coney's servers need
  a disc, Ghidra and PCSX2 (or xemu) on the same machine ([Ghidra](ghidra.md),
  [Driving PCSX2](research-tools.md#driving-pcsx2)), so a Codex task without them says so in the description's Testing
  section and does no reverse engineering. Source: [MCP](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
- **Local runs.** The sandbox is `read-only`, `workspace-write` (the default: edit the workspace and run routine
  commands) or `danger-full-access`, set with `sandbox_mode` and `approval_policy` in `config.toml`; more writable
  folders are `sandbox_workspace_write.writable_roots`. OpenAI's sandbox page gives neither the network access of a
  local run nor whether `.git` is writable in `workspace-write` (a third-party write-up says it stays read-only). If
  the agent cannot run `git commit` or `uv`, the person approves the command or commits themselves: the hooks
  `pre-commit install` put in place run either way. Source: [Sandboxing](https://learn.chatgpt.com/docs/sandboxing).
- **Codex cloud.** A task starts in a container with the repository checked out at the chosen branch. The
  environment's setup script runs first, with internet access, in its own shell (its exports do not reach the agent,
  and secrets exist only there); an optional maintenance script runs when a cached container resumes (a cache lives up
  to 12 hours). During the agent phase the internet is off by default; an environment can allow it, with a domain
  allowlist and allowed HTTP methods. `AGENTS.md` is read "to find project-specific lint and test commands"; the page
  does not say nested files and overrides are read there too. So the setup script has to fetch everything the checks
  need: uv, the Python project, the pre-commit hook environments and, for the engine, the pinned dependencies through
  the first configure (`coney-tools build --configure-only`; `build` also wants `--agent <id>` whenever it is not typed
  in a terminal), and the branch the change is measured from: a cloud checkout may hold only the task's branch, and
  `ci pr` measures from `origin/main` (`--base`). A minimal setup script:

    ```sh
    curl -LsSf https://astral.sh/uv/install.sh | sh
    uv sync --project python --frozen
    uvx pre-commit install --install-hooks
    git fetch origin +refs/heads/main:refs/remotes/origin/main
    uv run --project python coney-tools doctor
    uv run --project python coney-tools build --preset dev --configure-only --agent codex
    ```

  The default image may lack the C++ toolchain that `doctor` asks for; a task that cannot build says so rather than
  claiming the tests passed. Sources: [Cloud environments](https://learn.chatgpt.com/docs/environments/cloud-environment)
  (titled "Codex Cloud (Legacy)"), [Internet access](https://learn.chatgpt.com/docs/cloud/internet-access).

**The others, briefly.** Which instruction files they read is from third-party guides (check the vendor before relying
on a line): Gemini CLI reads `GEMINI.md`, which here imports `AGENTS.md` with `@AGENTS.md`, and can be pointed at
`AGENTS.md` with `context.fileName`; Cursor reads `AGENTS.md` at the project root (and `.cursor/rules/`); Copilot's
coding agent reads `AGENTS.md`, `.github/copilot-instructions.md`, `CLAUDE.md` and `GEMINI.md`, as context. Their hooks
are from the vendors' own pages and are what enforces the rules there: [Agent tools](enforcement.md#harnesses).
