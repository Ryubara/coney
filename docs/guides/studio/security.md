# Coney Studio: script security and the sandbox

The design of how Coney protects players from the scripts they run: what other platforms that run strangers' Lua do and
why, the threat model, what a project's scripts keep and lose and why, the few permissions a mod declares, trust, the
attack surface, and the tests and fuzzing that prove the sandbox holds. Its companion page, [Coney Studio: mods,
packages and loading](mods.md), covers how mods are packaged and loaded. It ends with the build issues of both pages, in
order. The parent issue is [#1054](issue:1054); this page is [#1062](issue:1062).

**Status: design.** Nothing on this page is built ([Page kinds](../writing-docs.md#page-kinds)). It builds on [Coney
Studio: the Lua SDK](lua-sdk.md), whose [sandbox](lua-sdk.md#sandbox) section it owns (that section summarises it), and
on the [security](../network-architecture.md#security) and [sandboxing](../network-architecture.md#sandbox) of Network
architecture; [What changes on other pages](#other-pages) lists what it changes elsewhere.

## Contents {#contents}

1. [Principles](#principles): the rules everything below follows.
2. [What other platforms do, and why](#prior-art): FiveM, Roblox, Garry's Mod, World of Warcraft, Factorio,
   Minecraft, and Lua itself; their escapes and the fixes.
3. [The threat model](#threats): who could attack, what must be protected, what is out of scope.
4. [Coney's line](#line): the Lua library item by item, what natives may do, the limits, one thread, and what
   modders lose and get.
5. [Files and paths](#paths): the one way a script's name for a file becomes a file.
6. [A shared Studio project](#studio-project): why opening one runs nothing.
7. [The console and mods](#console): what a mod's commands may do, and what the release console never reaches.
8. [Permissions and trust](#permissions): what needs no permission, what a mod declares, signatures, what the player
   sees, and server-sent scripts compared with local mods.
9. [The attack surface](#surface): every way in, and its defence.
10. [Proof](#proof): the escape suite, fuzzing, review and keeping the runtime current.
11. [What changes on other pages](#other-pages), [Maintainer decisions](#decisions), [Open questions](#open-questions),
    [Proposed build issues](#build-issues), [Sources](#sources). Mods and how they load: [Mods, packages and
    loading](mods.md).

## Principles {#principles}

1. **Safe by construction, not by permission.** A project's scripts can reach the game's world through natives and
   nothing else: no file outside the project, no network, no program, no memory address. Because the base SDK cannot
   harm the computer, almost every mod needs no approval at all, and a player is not trained to click "allow" (the
   lesson of Android's and Chrome's permission prompts, [below](#permissions)).
2. **No native code, ever,** from a mod, a project or a server. Native code cannot be sandboxed; Minecraft's Java mods
   show what follows ([Minecraft](#minecraft)). A player who wants the engine changed builds Coney from its source.
3. **Defence in depth.** No single layer is trusted alone: the Lua sandbox, natives that validate everything, files
   reached only through one checked layer, data parsers that are fuzzed, budgets on time and memory, a pinned and
   current Lua, and tests that try the known escapes on every build.
4. **The protection lives where a script cannot reach.** The instruction hook, the allocator, the path layer, the
   natives' checks and the budgets are C++ and C. Everything a script can see (its globals, the `Coney` table, the
   string metatable) is a convenience: changing it can only break the script's own project.
5. **The project is the unit of isolation.** Each project runs in a Lua state of its own. Inside it, a script may do as
   it likes with its own tables; across projects, and between a project and the game state, nothing is shared but the
   world, reached through natives ([The two states](lua-sdk.md#two-states)).
6. **No more limited than protection needs.** Every Lua feature that goes has a reason on this page and, where a
   modder would miss it, a replacement ([What modders lose, and what they get](#dx)).
7. **One model offline and online.** Offline, the one machine is the server and the client; a mod loads the same way
   a server's package does, so a project that works offline works on a server ([Coney's model](mods.md#mp-model)).
8. **Levels are data; code lives in the project's scripts.** No script is attached to an entity (a maintainer
   decision): a project's scripts find the entities they need by name or tag when a level starts, as the original's
   level scripts do, and handle one that is missing. So a level file never holds code, and a level-only mod runs no
   script at all, as a Minecraft data pack runs no program.
9. **The package is the unit of trust.** A finished project is one `.cmod` file, mounted read-only where it lies:
   its manifest lists every file with its hash, a signature may cover the manifest, and what the player approves,
   what a save or replay records and what a co-op host checks is that one package ([The `.cmod` package](mods.md#cmod)).
10. **Fail closed, and say why.** A refused call is a Lua error that names the rule ("`io` is not part of the Coney
   SDK: read project data with `Coney.ReadData`"), shown in Studio's Console with the file and line, never a crash and
   never a silent no-op.

## What other platforms do, and why {#prior-art}

Every platform below runs Lua (or code) written by strangers on players' machines, and each one limits it. The reasons
repeat: files and processes are how malware steals and spreads, bytecode is how a script breaks out of the virtual
machine, and the debug library is how a script reaches what it was not given. Only public documentation, forum posts
by the platforms' staff and published security write-ups were read ([Sources](#sources)); for FiveM, only its public
documentation and forum, because its code is not under an open-source licence
([Network architecture: Prior art](../network-architecture.md#sources)).

### FiveM (CfxLua) {#fivem}

FiveM runs CfxLua, a modified Lua 5.4 (every script since June 2025), in resources on servers and on clients.

- **What it limits on the server.** Its sandbox page states the goal: stability and preventing malicious code.
  `os.execute` is blocked outright, `io.popen` only emulates directory listing, `io.tmpfile` is blocked; files are
  reached by `@resource/path`; a resource may write only inside itself, never into another resource, the server's main
  folder or anything outside the resource folders; symlinks are neither followed nor created, and `..` is refused.
  Child processes and workers need a line in the operator's `server.cfg` (`add_unsafe_child_process_permission`), and
  the page warns that the filesystem overrides are for fully trusted resources only. `load` is kept.
- **What it limits on the client.** In late 2024 Cfx.re shipped a sandboxed `os`, `io` and `debug` library; on the
  client the debug library kept five functions (`getinfo`, `traceback`, `getmetatable`, `setmetatable`, `getupvalue`)
  and lost the other eleven, among them `setupvalue`, `setlocal`, `sethook` and `getregistry`. Staff's reason, on the
  forum: the debug library is unsafe, setters most of all, and it is meant for debugging, not production code. `load`
  stays.
- **Files on the client.** `LoadResourceFile(resource, path)` returns a file's text, and on the client only a file the
  resource's manifest lists under `files`. The manifest is a Lua file run in a runtime of its own, apart from the
  resource's scripts.
- **What went wrong.** Leaked and resold resources carry a few obfuscated lines that decode to `PerformHttpRequest`
  and `load`: they download code from the attacker's server and run it, and the infected server's other resources gain
  the same lines again after they are deleted (Cfx forum, 2023). A 2026 warning describes a loader that fetches a second
  stage which steals browser logins and Discord tokens and runs operating-system commands through `os.execute`. Every
  step needs one of three things: a network call, string-to-code, or writing a script file.
- **Lesson for Coney.** A script with no network, no way to write a file that later runs, and no process cannot carry
  this class of malware at all, so Coney removes those three rather than detecting them.

### Roblox (Luau) {#roblox}

Luau, Roblox's Lua, is built to run actively malicious code. Its sandbox page lists what goes and why: `io` (files and
processes), `package` (files and native modules), `os` down to `clock`, `date`, `difftime` and `time`, `debug` down
to `info` and `traceback` (the rest is not memory safe or breaks isolation), `dofile` and `loadfile` (files),
`string.dump` and bytecode in `loadstring` (bytecode is hard to validate and untrusted bytecode may lead to exploits),
`collectgarbage` down to `"count"` (changing the collector's state disturbs other code), and `__gc` (performance,
memory safety, isolation). The built-in libraries, the string metatable and the global table are read-only; each
script gets its own global table whose `__index` reads the built-ins. A host interrupt stops runaway scripts (Roblox's
watchdog stops a Studio script after 10 seconds). Luau imposes no memory limit by default; the host sets one.

Luau's security policy counts any memory-safety violation from source code as a vulnerability (reported for a bounty)
and puts two things outside its guarantee: flaws in C functions the host exposes, and bytecode not made by Luau's own
compiler, which must be signed or encrypted when it crosses a network or a disk. It promises nothing about termination
or memory use: the host bounds those.

**Script capabilities** (a beta) mark a model or folder as a sandboxed container: its scripts reach only instances
inside it and only the engine APIs its capability set names (Audio, Physics, Network, DataStore, LoadString,
CreateInstances, ...). A nested container gets the intersection of its own and its parent's. The stated purpose is
toolbox models and shared libraries that nobody has to audit line by line. **What went wrong:** free models carried
backdoors that loaded remote modules (`require(assetId)`), `loadstring` or `InsertService`; the Creator Store's
moderation answers by flagging exactly those calls.

### Garry's Mod {#gmod}

Garry's Mod runs addons' Lua on servers and sends client files to players (`AddCSLuaFile`: at most 8,192 files of
64 KB compressed each). Its file library writes only inside `data/`, and only files whose names end in a fixed list of
data and media extensions (`.txt`, `.json`, `.png`, `.wav`, ...): never `.lua`, so a script cannot write code that
later runs. Binary modules load only from `garrysmod/lua/bin/`, which a player fills by hand: they cannot come inside
an addon, because a module can be malicious and harm the system. Whether a client may run Lua of its own is the
server's choice (`sv_allowcslua`). **Lesson:** scripts write data, never code, and native code is never part of
anything a game downloads.

### World of Warcraft {#wow}

WoW's add-ons run a modified Lua 5.1 with no operating-system or file libraries. An add-on keeps data only in its
**SavedVariables**: Lua tables the client itself writes at logout into per-account and per-character files, one per
add-on. Its **taint** system marks everything an add-on executes or writes; protected functions (casting, targeting,
moving) fail from tainted code in combat, so add-ons can suggest but never decide for the player. That protects
gameplay from automation rather than the computer, which the missing libraries already do. **Lesson:** persistent data
through a store the host writes, and a clear line between what a script may show and what it may decide.

### Factorio {#factorio}

Factorio's mods and the scenario scripts inside saved maps run Lua 5.2 in lockstep multiplayer. Its documentation lists
the changes: `loadfile`, `dofile`, `coroutine`, `io` and `os` are gone, "mostly to ensure determinism"; `require`
resolves from the mod's root with `..` refused, so it cannot load a file outside the mod; `math.random` uses the map's
seeded generator; `pairs` follows insertion order; the trigonometric functions are Factorio's own so every platform
agrees; `debug` keeps `getinfo` and `traceback`.

**What went wrong, twice.** In 2020 a player built arbitrary memory read and write from bytecode passed to `load` and
asked for it to be removed; staff kept `load`, patched the opcodes used (type checks in `OP_SETLIST`, `OP_FORLOOP` and
three more) and called further patching a possible fool's errand. In 2024 CVE-2024-39840 was published: a malicious
server could run arbitrary code on every client that joined, through a custom map whose scripts used base-library
functions that run bytecode and forge objects. Version 1.1.101 stopped `load` from loading binary chunks "due to
security concerns". **Lesson:** a bytecode verifier is a race Lua's authors gave up in 5.2; refuse bytecode instead,
and treat a server-sent script as hostile.

### Minecraft {#minecraft}

- **Java mods** are Java code with the whole machine's rights. In 2023 the fractureiser malware spread through mods
  uploaded to CurseForge and BukkitDev, some in popular modpacks. Its last stage read Microsoft and launcher tokens,
  Discord tokens, browsers' cookies and saved logins, crypto wallets and the clipboard (swapping wallet addresses), and
  injected itself into every other mod or plugin jar it found. This is what a mod with native rights does.
- **Data packs** hold no program: advancements, loot tables, recipes, tags, world generation, and functions that are
  lists of game commands, which run with permission level 2 at most whoever starts them.
- **Bedrock's Script API** runs JavaScript in behaviour packs, sandboxed. On Bedrock Dedicated Server the operator's
  `permissions.json` lists the script modules a pack may import, per pack; `@minecraft/server-net` (HTTP) is not in the
  default list and exists only on dedicated servers; secrets (tokens) are readable only by the objects that use them,
  such as an HTTP header, never as plain values. **Lesson:** powerful capabilities belong to a server, granted by its
  operator per package, never to a client.

### Lua itself {#lua-itself}

- **Bytecode.** Lua 5.4's manual: loading a malformed binary chunk is safe, but Lua "does not check the consistency of
  the code inside binary chunks; running maliciously crafted bytecode can crash the interpreter". `load`'s `mode`
  argument (`"t"`, `"b"`, `"bt"`, default `"bt"`) is the switch.
- **Source can escape too.** CVE-2021-44964: a use-after-free in the collector and finalizers of Lua 5.4.0 to 5.4.3
  allowed a sandbox escape from a crafted script, fixed in 5.4.4. Lua's bugs page lists many more crashes reachable from
  plain source in 5.4 (the parser, coroutines, finalizers, the emergency collector), each fixed in a later release.
- **One stray function is enough.** CVE-2026-41196: in Luanti (formerly Minetest), `getfenv` reached the unsandboxed
  environment under LuaJIT, so any mod could escape with full file access; the stopgap was `getfenv = nil`.
- **5.4 has ended.** Lua 5.4.9 (August 2026) is the last 5.4 release: "There will be no further releases of Lua 5.4",
  and later bugs are fixed in 5.5 ([Maintainer decisions](#decisions)).

### What Coney takes from them {#lessons}

| Lesson | Seen in | Coney's rule |
| --- | --- | --- |
| Remote code arrives by network plus string-to-code, and spreads by writing script files | FiveM backdoors, Roblox free models | No network for project scripts; scripts write no files, only values in a store ([Limits](#limits)) |
| Bytecode breaks the VM | Factorio 2020 and CVE-2024-39840, Luau's policy, Lua's manual | No bytecode in, none out: `load` is text only, `string.dump` is gone, a game-script patch is text that Coney compiles ([Attack surface](#surface)) |
| The debug library breaks isolation | FiveM 2024, Luau | No `debug` library; the debugger is Coney's own, outside the state ([The script debugger](lua-sdk.md#debugger)) |
| A function that reaches another environment is an escape | Luanti `getfenv` | The state holds no function that reaches the host, the registry, another state or the game state; a test lists every global ([Proof](#proof)) |
| Native code cannot be sandboxed | fractureiser, Garry's Mod binary modules | Never from a mod, project or server ([Principles](#principles)) |
| Scripts write data, never code | Garry's Mod `file.Write`, FiveM's write rules | A per-project store of values, never a file path ([Limits](#limits)) |
| Capabilities that reach outside belong to a server and its operator | Bedrock's `permissions.json` and secrets, FiveM's `add_unsafe_*` | Web requests and secrets only on ConeyMP servers, granted per project by the operator ([Permissions](#declared)) |
| Few, specific permissions; ask again when they grow | Android, Chrome, Roblox capabilities | One player-approved permission today; an update that adds one is held until approved ([Permissions](#permissions)) |
| Opening a folder must not run its code | VS Code Workspace Trust | Studio runs nothing on open; Play runs the sandbox ([A Studio project](#studio-project)) |
| The runtime has bugs; fixes come with releases | Lua's bugs page, CVE-2021-44964 | Pin the last release, fuzz, watch the bugs page ([Keeping the runtime current](#runtime-updates)) |

## The threat model {#threats}

### Where hostile code or data comes from {#attackers}

| Source | Who controls it | What it runs on the player's machine |
| --- | --- | --- |
| **A downloaded mod** (a `.cmod`) | Its author, or whoever altered the copy on the way (a re-upload, a modpack) | The project's server, shared and client scripts offline, its levels and assets through Coney's parsers, its package through the package reader |
| **A project shared in Studio** | Whoever sent the folder or the repository | Nothing on open; on Play, the same as a mod; its files through Studio's importers and viewers, and its editor settings through the language server |
| **A co-op host** ([P1 to P3](../network-architecture.md#phases)) | The friend hosting | On the guest: the client part of the host's mods and their assets. The host runs every server script, so it can change the shared game as on its own console |
| **A ConeyMP server** (P4) | Its operator, and every resource the operator installed | On each client: the client scripts and files of the server's projects, cached |
| **Another client** | Another player | Nothing: clients never talk to each other ([The trust model](../network-architecture.md#trust)); a server reads its messages as requests only |
| **A hostile data file** | Any of the above | A level, model, texture, sound or manifest crafted to break a parser |

### What must be protected {#protect}

| Harm | Example | The defence | Where |
| --- | --- | --- | --- |
| The computer | Run a program; load a native library; break out of the VM | No process, no native code, no bytecode; budgets; fuzzed runtime and parsers | [Library](#library), [Surface](#surface) |
| The player's files | Read documents or keys, encrypt or delete them, plant a file that runs later | No file access but the project's own files, read-only; links refused; writes only to a store of values | [Paths](#paths), [Limits](#limits) |
| The account and credentials | Steal browser logins, Discord or launcher tokens (fractureiser); leak the Windows login hash through a network path such as `\\host\share`; phish a password with a fake prompt | No file, network or network-path access; Coney has no account to steal; no native shows a text field that is not marked as the script's | [Paths](#paths), [Natives](#natives) |
| Privacy | Read the user name, machine name, environment, clipboard or hardware ids; learn which servers the player visited | None of those is reachable; server caches are kept per server | [Library](#library), [Coney's model](mods.md#mp-model) |
| Other players | Send something to another player's machine; make a guest run a script it did not agree to | Star topology; a client runs only presentation natives; joining a modded game is a choice shown before download | [Coney's model](mods.md#mp-model), [Network architecture](../network-architecture.md#resources) |
| Save data | Overwrite or corrupt the story profile | A modded session saves apart from the faithful profile; a project's own data lives in its own store with a quota | [Limits](#limits) |
| Denial of service | Endless loop, memory bomb, spawning thousands of things, flooding the log or the disk, a parser that never ends | Instruction and memory budgets, caps per project, quotas, a watchdog, fuzzed parsers | [Limits](#limits) |
| The game state | Patch the faithful game's scripts or globals in ways nobody can debug | Separate states; game-script patches only by permission, as text Coney compiles | [Library](#library), [Declared permissions](#declared) |

### Out of scope {#out-of-scope}

Malware already on the computer, or a person with its account (it can change Coney itself); a player who modifies Coney,
which is open source (cheating against others is the network page's subject, [What a client can
try](../network-architecture.md#cheats)); a host who cheats in their own co-op game, accepted on the network page as on
the host's own console; content (a mod with offensive text or pictures is a moderation question for whoever distributes
it); and builds of Coney not made from this repository.

## Coney's line {#line}

### The Lua library, item by item {#library}

A project state runs stock Lua 5.4 ([How Lua 5.4 is built](lua-sdk.md#lua-build)). What its scripts see:

| Item | Kept? | Why | What replaces it |
| --- | --- | --- | --- |
| `string` (but `dump`), `table`, `utf8`, `coroutine` | yes | Pure computation; their cost is bounded by the memory cap and the work charge ([Limits](#limits)) | |
| `string.dump` | no | It makes bytecode, which nothing may load; with no bytecode path it is useless | |
| `math` | yes, `random` and `randomseed` replaced | The project's own `GameRandom` stream, for replays ([Determinism](lua-sdk.md#determinism)) | |
| `assert`, `error`, `pcall`, `xpcall`, `select`, `type`, `tostring`, `tonumber`, `ipairs`, `pairs`, `next`, `rawequal`, `rawlen`, `rawget`, `rawset`, `getmetatable`, `warn` | yes | Ordinary Lua; `rawset` can only change the project's own tables ([Principles](#principles), 4 and 5) | |
| `setmetatable` | yes, but it refuses a metatable with `__gc` | A finalizer runs at a time the script does not choose (inside another call's allocation, or while the state closes), which makes budgets and errors hard to attribute; Luau removes `__gc` for the same reasons | `Coney.On("stop", fn)` for cleanup; weak tables for caches |
| `__close` (to-be-closed variables) | yes | Runs inside the call that owns it, under its budget | |
| `print` | yes | To the run log and Studio's Console, tagged with the project, at most a number of lines and bytes per step ([Limits](#limits)) | |
| `load` | text only, in the project's environment by default | String-to-code adds nothing a script cannot already do (it runs in the same sandbox), and REPLs, templates and data-driven tools need it; FiveM and Factorio keep it. The mode is forced to `"t"` whatever the script passes, so bytecode is refused | |
| `require` | inside the project only, text only | Modules are how a project grows; `package` itself stays hidden | |
| `collectgarbage` | `"count"` only | The rest changes the collector for the whole state and costs unbounded time; `"count"` helps a modder find a leak, and is repeatable because the allocator is | |
| `dofile`, `loadfile` | no | They open files by path | `require`, `Coney.ReadData` |
| `io` | no | Files and processes | `Coney.ReadData` for project data, `Coney.SaveData` and `Coney.LoadData` for the project's store |
| `os` | no | Processes (`execute`), files (`remove`, `rename`, `tmpname`), the environment (`getenv`), exit, and a real clock that would break replays | `Coney.GetGameTime` |
| `package` | no | Native module loading (`loadlib`, C searchers) and file paths | `require` |
| `debug` | no | Breaks isolation and memory safety (`setupvalue`, `setlocal`, `sethook`, `getregistry`, `setmetatable` on any type) | Errors carry file, line and a traceback; the [script debugger](lua-sdk.md#debugger) works from outside the state |

The rows that say "no" are absent, not stubbed: the name is nil in the project's environment, the language server's
stubs disable it ([Typed stubs](lua-sdk.md#stubs)), and the error a script gets from using one (Lua's own "attempt to
index a nil value") gains a line naming the replacement.

### What natives may do {#natives}

Natives are the only door to the world, so every native, game or Coney, follows these rules. A Coney native's entry in
`research/bindings/coney.yaml` is reviewed against them before it becomes available
([How a native reaches the SDK](lua-sdk.md#availability)).

1. **No file system path.** A native that loads something takes a `project:/` or `game:/` path and goes through the
   asset store's sources ([Assets by path](lua-sdk.md#assets)), never the operating system's file functions. `game:/`
   is read-only; `project:/` is read-only to scripts too. The only writes are the project's store ([Limits](#limits)).
2. **No network, no process, no other program.** No native opens a socket, sends a request, starts a program, opens a
   web page or reads the clipboard. Web requests exist only for ConeyMP server scripts that the operator allows
   ([Declared permissions](#declared)).
3. **No pointer and no engine object.** Natives take and return handles, asset ids and plain values
   ([The value bridge](lua-sdk.md#bridge)). A handle is checked on every call: one that names nothing, names a thing of
   another kind, or outlived its thing (a generation counter) is an error, never a crash.
4. **Every argument checked.** Numbers for range and for NaN and infinity, strings for length, tables for size;
   `std::expected` from the native's body becomes a Lua error at the bridge ([C API](#surface)).
5. **Bounded work.** A native's cost does not grow without limit with its arguments: a native that walks a list takes
   a count it clamps, and a native that loads something completes at a step boundary ([Determinism](lua-sdk.md#determinism)).
6. **Where it runs.** `server` natives exist only in server and shared scripts, `client` natives only in client
   scripts ([`apiset`](lua-sdk.md#determinism)); a client native never writes game state
   ([Coney resources](../network-architecture.md#resources)).
7. **No text field that pretends to be Coney's.** A native that asks the player to type something (a later chat or
   name box) shows the project's name in its frame. Coney has no login and never asks for a password, so any password
   prompt in a script's frame is a fake, and the frame says whose it is.
8. **Game natives that make no sense from a project** carry `mods: false` ([Allowed](lua-sdk.md#availability)).

### Limits {#limits}

A script that cannot escape can still take the machine's time, memory or disk. The limits, all set by Coney in C++:

| Limit | How | Past it |
| --- | --- | --- |
| **Instructions per call** into the state | The count hook ([Sandbox](lua-sdk.md#sandbox)), installed on the main thread and inherited by every coroutine, all counting into one budget per project | The call stops with an error. The budget is **sticky**: once spent, every later check in the same call raises again, so `pcall` or `coroutine.resume` cannot catch the error and go on looping |
| **Instructions per step**, all calls of a project together | The same counter, reset each step | The project's remaining calls in that step are skipped and the run log says so, so a project with ten thousand handlers cannot add their budgets up |
| **Memory per project** | Coney's allocator ([How Lua 5.4 is built](lua-sdk.md#lua-build)) | The allocation fails as Lua's memory error; nothing more can be allocated until the project frees memory |
| **Work inside one library call** | The count hook does not run inside a C function. Library functions whose work grows with their input (`string.rep`, `table.concat`, `table.sort`, `string.format`, the pattern functions) are wrapped so that their input sizes are charged to the call's budget before they run, and the pattern functions refuse a subject longer than a limit | An error naming the function |
| **A call that still does not return** | A watchdog outside the simulation notices a script call that has run past a wall-clock limit (seconds) | Studio and the game show "A script is not responding", with Stop; the watchdog never changes the simulation, is off in test mode, and stopping marks the run ([Determinism](lua-sdk.md#determinism)) |
| **Nesting** | Lua's own C-stack limit (`LUAI_MAXCCALLS`) for deep recursion of the parser and of C calls; the Lua stack is bounded by the memory cap | "stack overflow" or "too many C levels", as stock Lua |
| **Things a project makes** | Counters per project of the handles it holds: humans and objects share the game's pools, so a project may hold at most a share of each, and Coney's own kinds (spawned models, sounds playing, effects, HUD messages per step) have caps of their own | The native returns its "none" value or an error, as when the game's pool is full |
| **Log output** | Lines and bytes per project per step | The rest of the step's lines are dropped with one line saying how many |
| **The project's store** | `Coney.SaveData(key, value)` and `Coney.LoadData(key)` keep tables of plain values (no functions, no userdata, no cycles) as JSON in a folder of Coney's per project, with a quota | `SaveData` returns an error; nothing is written |

The sizes (budgets, cap, quota, shares) are measured on real projects before the first release, as the SDK page's
[open question](lua-sdk.md#open-questions) says; a project may ask for more memory up to a ceiling in its manifest
(`[limits]`), shown in its details, with no prompt: a bigger budget risks only the game's own speed.

**Saves.** A modded session saves apart from the faithful profile, so no mod can corrupt the story a player made
without mods ([Modded saves and replays](mods.md#saves-replays)).

### One thread, cooperative {#one-thread}

Scripts never run in parallel. `Coney.CreateThread` and `Coney.Wait` ([Threads and waits](lua-sdk.md#threads)) are
coroutines resumed one at a time on the simulation thread, inside the step, as FiveM's `Citizen.CreateThread` threads
are: cooperative, never preemptive, never on another core. The reasons:

- **Determinism.** The order in which scripts act must be the same in every run, so a replay or a test gives the same
  result; threads racing for the world would make it depend on the machine's timing.
- **Co-op and servers.** The server's simulation is the one run every client follows
  ([The authority model](../network-architecture.md#authority)); its script effects must happen in a fixed order.
- **A Lua state is not thread-safe.** One state is used by one thread at a time; stock Lua's locking hooks are empty,
  and filling them would serialise the state anyway.
- **The sandbox.** The budgets, the hook and the allocator are per state and per call; natives touch a world that
  is single-threaded. Running scripts or natives on several threads would add data races to the attack surface for
  no gain the step does not already give.

**Later: engine-side asynchronous jobs.** Heavy work that does not touch the world (a path query over a large map, the
parsing of a big project data file, a large computation Coney offers as a native) can run on a worker thread: a native
starts the job with copied inputs and returns a job id; the result arrives at a later step boundary, at a step fixed by
the request (the job's declared delay), and the simulation waits for the worker there if it is late, so the run is the
same on any machine; in test mode the result arrives at once. No script runs on a worker: only Coney's own code does,
on copies. It is a proposed build issue ([17](#build-issues)), filed when a native needs it.

### What modders lose, and what they get {#dx}

| A modder used to | In Coney |
| --- | --- |
| `io.open` a config or data file in the mod | `Coney.ReadData("project:/data/waves.json")` returns the parsed table (JSON or TOML) |
| Writing a save or settings file | `Coney.SaveData("best", {time = 93})`, `Coney.LoadData("best")`: one store per project, kept across sessions |
| `os.time`, `os.clock` for timers | `Coney.GetGameTime()` and `Coney.Wait(ms)`, in game time, the same in a replay |
| `os.date` for a timestamp | none: a real date would differ in every replay; a project shows elapsed game time |
| `require` of a module anywhere | `require("fighters.waves")` from the project's `scripts/`; libraries are copied into the project |
| A C module (`luasocket`, `lfs`) | none: native code never loads. JSON comes built in (`Coney.JsonEncode`, `Coney.JsonDecode`, as FiveM bundles `json`) |
| HTTP for a leaderboard or an update check | none for local mods and clients; a ConeyMP server script may get web requests from its operator ([Declared permissions](#declared)) |
| The `debug` library for introspection | Tracebacks in every error; breakpoints, stepping and watches in the [script debugger](lua-sdk.md#debugger), from outside the state |
| `string.dump` and precompiled chunks to hide source | none: projects ship source. Client scripts are public anyway ([Coney resources](../network-architecture.md#resources)) |
| Shipping a changed copy of a game script | A patch, as [#1074](issue:1074) designs: only the differences, applied to the player's own disc script, with the `game-scripts` permission ([Declared permissions](#declared)) |

Spawning, moving, fighting, sounds, the HUD, the project's own assets and its data need no approval from the player.

## Files and paths {#paths}

One layer turns a name a script or manifest gives into a file: Coney's **project file layer**, used by `require`, the
loader, the asset store's project source, `Coney.ReadData` and Studio's File system panel. Nothing else opens a project
file. It reads from two kinds of root: a **project folder** (Studio's open project, `coney --project <folder>`) and a
**mounted package** (a `.cmod`, read in place, [The `.cmod` package](mods.md#cmod)). The name rules hold for both; the
link and handle rules are for folders, since a package holds no links. Its rules:

- **Relative names only**, written with `/`. Refused: an absolute path, a drive letter, a network path (`\\host\share`,
  which on Windows would send the player's login hash to that host), a device path (`\\.\`, `\\?\`), `..`, `.` as a
  component, a backslash, a colon (which on Windows names a file's alternate data stream), a name ending in a dot or a
  space, Windows' reserved device names (`CON`, `NUL`, `AUX`, `COM1`, ...) in any folder, control characters, and a
  name longer than a limit.
- **No links.** Symbolic links, junctions and every other reparse point inside a project are refused, never followed
  (FiveM's rule); a file with more than one hard link is refused, because a hard link can point at any file on the same
  disk and no path shows it. A link is refused even if it points inside the project: checking where it leads and then
  opening it would race a change.
- **Opened relative to the project folder's handle**, walking one component at a time, so a folder renamed or replaced
  during the walk cannot redirect it; the file's type and link count are checked on the open handle.
- **Size limits** per file read and per project, so a manifest or a data file cannot be gigabytes.
- **Case.** Names compare as written; a project that would load differently on a case-insensitive disk (two files that
  differ only by case) is refused at load, so a project behaves the same on Windows, Linux and macOS.
- **Read-only for scripts.** The layer has no write call a script reaches; Studio writes project files through its own
  editor, never on a script's behalf, and a mounted package is never written at all.

`game:/` paths go to the installed game's files through the asset store, by the extract tool's names, read-only
([Assets by path](lua-sdk.md#assets)); the project's store is written by Coney into its own folder, by key, never by a
path a script gives.

## A shared Studio project {#studio-project}

Studio's Play runs the open project exactly as the loader runs a mod: sandboxed, no approval asked for it (it is the
user's own work; a `game-scripts` permission in a project Studio opened from elsewhere is shown before the first Play).
Opening a project someone else made is safe because nothing in it runs on open:

- **No script runs until Play**, and Play runs the sandbox, so Studio needs no trust prompt for scripts, unlike VS
  Code's Restricted Mode, whose tasks and debuggers run programs.
- **The language server is configured by Studio.** lua-language-server runs a Lua plugin named by its
  `runtime.plugin` setting with the server's own rights. Studio passes the server its settings itself (the SDK's stubs,
  `Lua 5.4`, the sandbox's disabled libraries) and never takes `runtime.plugin`, or any setting that names a program or
  a file outside the project, from a project's `.luarc.json`; a project that carries such a setting is told so.
- **Importers and viewers read untrusted files**: Studio's File system panel draws thumbnails of models and textures
  as it lists them, so every importer is held to the [fuzzing](#fuzzing) rule like the game's parsers.
- **The debugger** listens on the loopback address only, asks for a random token Studio passes it, and is off unless
  Studio or `--debug-scripts` starts it ([The script debugger](lua-sdk.md#debugger)).
- **A signing key never lives in a project.** Studio keeps the user's key in the per-user location and refuses to save
  it inside a project folder, so publishing a repository cannot leak it.

## The console and mods {#console}

The Coney console is the console design planned on [#1064](issue:1064) (`console.md` in this folder once it merges,
which owns the console and mod-registered commands); what its commands do to mods is [Console
commands](mods.md#console). The security line both hold to:

- **A mod's command runs in the mod's own sandbox.** `Coney.RegisterCommand(name, handler, spec)` (FiveM's
  `RegisterCommand`) makes the handler an entry point like an event handler: it runs in the mod's state, queued to the
  next step boundary, under the mod's instruction budget, memory cap and print limits, with only the natives that state
  has. Its arguments arrive as strings checked against the declared types, never evaluated as Lua. It can do nothing
  the mod's scripts cannot do already, whoever types it: a console access level never widens what a mod's code may
  reach. A mod cannot register a built-in's name, and its commands go when it stops or reloads.
- **No script runs console text.** No native executes a console command (FiveM's `ExecuteCommand`): a mod cannot drive
  the built-in commands, another mod's commands or the console's access levels.
- **Online**, a command marked `restricted` is the host's or the server operator's only; a server mod's command typed
  on a client is a request the server checks against the sender's slot, as it checks a client's event
  ([Coney resources](../network-architecture.md#resources)); a client mod's command runs in that client's state with
  client natives only.
- **What the release console may never reach**, at any access level: evaluating Lua or any expression, calling a
  native, reading or writing an entity, a value or memory, opening a file by path, the network, starting a program,
  approving a permission, or any of the debug tools (the Lua console, the Natives page, the debug menu), which stay out
  of the release build. Its changes to mods are for the running session only.
- **Proved by** the console's tests and the [escape suite](#escape-suite): the release console refuses `lua` and a
  native's name as unknown commands; a mod's command that loops stops on the mod's budget; a mod cannot register a
  built-in's name; no available native executes console text.

## Permissions and trust {#permissions}

### What needs no permission {#no-permission}

Everything in [Coney's line](#line): every available native, the SDK functions, `require` and `Coney.ReadData` inside
the project, the project's store within its quota, its own assets, and replacing presentation assets. None of it can
reach the computer, the player's files, the network or another project's data, so asking would only teach players to
click "allow" (Android grants such permissions at install without a prompt; Chrome warns only for powerful ones).

### The permissions a mod declares {#declared}

A project declares in its manifest what reaches beyond its own sandbox:

```toml
[permissions]
game-scripts = ["level3"]      # patches these game scripts (the differences only)

[limits]
memory = 128                   # MiB, up to Coney's ceiling; shown, never asked
```

| Permission | What it allows | Who approves | Why it is a permission |
| --- | --- | --- | --- |
| `game-scripts` | Patches to the named disc scripts, as [#1074](issue:1074) designs (the mission mods page planned for this folder): only the differences, pinned to the disc script's hash; each player's Coney applies them to the script from their own disc, compiles the result with its Lua 4.0 [compiler](lua-sdk.md#compiler) and runs it in the game state in the script's place ([Three parts](../asset-and-script-architecture.md#script-parts)). No game script ships, not even as Lua 4.0 source | The player, before the mod first starts | They run in the game state beside the faithful scripts, can change the story's flow and are not held by a project state's separation; the player should know which levels a mod changes |
| `http` (P4) | Web requests from a ConeyMP **server** script to the hosts listed | The server's operator, per project, in the server's configuration (Bedrock's `permissions.json`) | Network access is how remote code and stolen data travel ([FiveM](#fivem)); never on a client and never for a local mod |
| `secrets` (P4) | Named values from the server's configuration, usable only as an `http` header's value, never readable as text | The operator, per project | A token a script can print can be leaked |

`[limits]` and the project's asset replacements ([Override packs](project-formats.md#override-packs)) are disclosures,
shown in the details and never asked. The rules for approval, as Android and Chrome do it:

- **Asked once, at the moment of use**: before the mod first starts, in its details; the mod waits, held, until then,
  while the other mods start. A console command never approves.
- **An update that adds a permission is held** until the player approves it (Chrome disables an extension whose update
  asks for more); an update that keeps or drops permissions loads without a question.
- **Approvals are recorded** per project in `enabled.toml`, with the signer, and the mods menu can withdraw them.
- **A project cannot ask at run time.** There is no native that requests a permission while the game runs, so a mod
  cannot nag in the middle of play.

### Trust: where mods come from, hashes and signatures {#trust}

Coney runs no services ([Transport](../network-architecture.md#transport)): no mod store, no account, no central list
of good or bad mods. Players will get mods from community sites, repositories and chat. So trust rests on what a
player can check without a service:

- **Integrity: hashes.** A package's manifest lists every file's SHA-256 and its package hash names the whole
  ([The `.cmod` package](mods.md#cmod)), so a damaged or altered copy fails its check, and a save, a replay and a co-op
  join name each mod by its package hash.
- **Identity: signatures, trust on first use.** An author signs with an Ed25519 key (libsodium, which the network page
  already plans for encryption). Coney shows the key's short fingerprint; the first install records it, as SSH records
  a host key, and an update signed by another key, or unsigned after being signed, is held with a clear warning (an
  Android app's update must carry the same signature). An author can publish their fingerprint where players find the
  mod.
- **Unsigned is allowed** and shown as such: signing proves who made a mod, not that it is safe, and the sandbox is
  what makes it safe.
- **No revocation.** With no service there is nothing to revoke against; a known bad version is answered by its
  author's next version and by the community, and the sandbox limits what any version can do.

### What the player sees {#player-view}

The details of each mod, which the mods UI shows ([#1063](issue:1063)) and the console's `mod <name>` prints, before it
first starts and at any time after:

- name, title, version, author and **signed by** (fingerprint) or **unsigned**, and the package hash's short form;
- whether it starts with every session or when its level is played (`autostart`), the SDK level and the other mods it
  needs, and whether they are here;
- the clashes with other enabled mods ([Load order](mods.md#load-order)) and which one wins;
- **what it changes**: its levels, the assets it replaces, and, under `game-scripts`, the game scripts
  it patches;
- **its limits** if it asks for more than the default;
- its size and file count;
- one fixed line: "This mod's scripts run in Coney's sandbox: they cannot read your files, use the network or run
  programs."

The same panel opens from the join prompt of a modded server and from Studio's Play of a project opened from elsewhere.

### Server-sent scripts compared with local mods {#server-vs-local}

| | A local mod | A server's client package |
| --- | --- | --- |
| Where it comes from | a `.cmod` in `mods/`, installed by the player | the guest's own `.cmod` of the same package hash, or `cache/servers/<key>/`, sent while connected |
| Scripts that run on this machine | server, shared and client (offline the machine is both) | client and shared only, as a client |
| Natives | server and client natives, by script kind | client natives only |
| Permissions | `game-scripts` if approved | none |
| Its store | `mod-data/<project>/` with the full quota | a small one per server and project |
| Asked | before it first starts if it declares a permission, and when its permissions grow | at join: the list of projects and their size |
| Lifetime | until the player removes it | while connected; the cache is evicted later |

## The attack surface {#surface}

| Entry point | The risk | The defence | Proved by |
| --- | --- | --- | --- |
| **`load` and string-to-code** | A way to bytecode, or to an environment that is not the sandbox | `load` forces mode `"t"` and defaults to the project environment; a script can pass only tables it already holds as `env` | Escape suite; the source fuzzer |
| **Bytecode, Lua 5.4** | Crafted bytecode corrupts memory ([Lua itself](#lua-itself)) | No path in: `load` text only, `require` text only (a file starting with Lua's binary signature is refused), no `string.dump`, no `loadfile`; packages and server caches hold no bytecode | Escape suite: every path given a dumped chunk |
| **Bytecode, Lua 4.0** | A patched disc script runs in Coney's own 4.0 VM, which reads bytecode | A game script changes only by a project's `game-scripts` patch ([#1074](issue:1074)): text, never bytecode, applied only to the player's own disc script whose hash it pins, and the result compiled by Coney's [compiler](lua-sdk.md#compiler); no game script ships in a `.cmod`, not even as Lua 4.0 source; `overrides/` never holds a script, and its `files/` entries of script kinds are refused; the player's own disc chunks, checked against their SHA-1 at install, are the only bytecode the game state runs; the 4.0 chunk loader and VM are fuzzed all the same | A patch carrying bytecode, a patch against another hash and a script chunk in `overrides/files/` refused; the 4.0 chunk fuzzer |
| **Lua's own bugs** | A crash or escape from plain source (CVE-2021-44964) | Lua 5.4.9 pinned, its bugs page watched, every script-reachable fix applied ([Keeping the runtime current](#runtime-updates)); fuzzing under sanitizers | The source fuzzer |
| **The C API and errors** | Lua's errors unwind with `longjmp`, which skips C++ destructors (undefined behaviour, and a leak at best); a native holding C++ objects when an error is raised breaks the engine | Natives never call Lua's API. The bridge's C function reads the arguments into `script::Value`s, calls the native, and raises the native's error only after every C++ object of the call is gone; the API calls that may raise (allocations) happen before or after the C++ part, never in it | Bridge tests under ASan: a native that fails, an allocation failure during argument conversion and during results |
| **The value bridge** | A metatable on a table argument runs script code inside a native (`__index`, `__len`); a cycle or a huge table; a string with zero bytes; NaN in an integer; a stale callback id | Raw access only (`lua_rawgeti`, `lua_rawlen`) when reading arguments, so no metamethod runs inside a native; sizes and depth limited; integers range-checked; a callback id carries its state and generation and is dropped on reload ([The value bridge](lua-sdk.md#bridge)) | The native fuzzer |
| **Re-entry** | A native that fires a callback at once runs a project's code in the middle of a game action | Callbacks fire at the points the [loader](lua-sdk.md#entry-points) names, inside the budget of the call they run in; a native never calls into a state synchronously | Bridge tests |
| **Natives with paths or handles** | A name that reaches the disk (`../`, `C:\`, `\\host\share`); a handle of another kind or another session | The [file layer](#paths) and the asset store; handles checked with kind and generation ([What natives may do](#natives)) | The file monitor ([Proof](#proof)); the native fuzzer |
| **Manifests and packages** | A crafted `coney-project.toml` or `.cmod`: overlapping or duplicate zip entries, a central directory that disagrees with the local headers, a compression bomb | Parsed into types with limits, `std::expected` on every error; the strict zip reader of [The `.cmod` package](mods.md#cmod), sizes enforced while inflating, every file checked against the manifest's hash | Parser fuzzers |
| **Levels, models, textures, sounds** | A crafted file breaks a parser (a classic route into a game) | Every parser reads through bounds-checked readers, checks counts before allocating, and is fuzzed; the same parsers serve the game's own files | Parser fuzzers |
| **Mod commands** | Text the player types reaching a mod's handler; a release console that grows into a debug tool | The handler runs in the mod's sandbox at a step boundary; no script runs console text; the release set never reaches Lua, natives or paths ([The console and mods](#console); the console design's section Mod commands, `console.md`) | The console's tests and the escape suite's console items |
| **Studio's tools** | The language server's plugin, the debugger's port | [A shared Studio project](#studio-project) | A test project carrying `runtime.plugin` runs nothing; the debugger refuses a client without the token |
| **The network** | Packets and packages from a server or client | The network page's strict decoding and fuzzing ([Testing](../network-architecture.md#testing)); packages as above | The network fuzzers |

## Proof {#proof}

The sandbox is done when these tests pass, in the terms of [Testing](../testing.md): unit tests on synthetic fixtures
written first, no disc, run by every build and the local gate, and fuzzing beside them.

### The escape suite {#escape-suite}

Catch2 tests tagged `[sandbox]`, each a small project written for the test (a fixture folder of `.lua` files, never
game data), each named after the escape it closes:

- **The environment is exactly the list.** The project state's globals, and every field of every library table, are
  compared with the allowed list of [The Lua library](#library); a name added by mistake fails the test (the Luanti
  `getfenv` class).
- **No bytecode anywhere.** `load` of a chunk made by `string.dump` in a test-only state, with every mode argument;
  `require` of a module file holding bytecode; a server package or `.cmod` holding a chunk; a game-script patch
  carrying bytecode. Each is refused with its message.
- **No file outside the project.** `require` and `Coney.ReadData` with `..`, absolute paths, drive letters, network
  and device paths, alternate streams, reserved names, a symbolic link, a junction and a hard link (each made by the
  test in a temporary folder, skipped with a message where the platform cannot make one).
- **No way out of the environment.** Setting a native's name fails; `setmetatable(_ENV, nil)` fails;
  `getmetatable(_ENV)` reveals no table; nothing reachable from a project state names another project's state, the
  registry or the game state.
- **Runaway code stops.** An endless loop, the same inside `pcall`, inside `coroutine.wrap`, inside a `__close`, in a
  thread that never waits, and ten thousand handlers in one step: each stops with the budget error and the next step
  runs.
- **Memory bombs stop.** `string.rep` of a huge size, a table that grows forever, a deep recursion, a long `..` chain:
  each fails within the cap and the game goes on.
- **Library work is charged.** A pathological pattern on a long subject, `table.concat` and `table.sort` of large
  tables: each errors or completes within its charge.
- **`__gc` is refused** by `setmetatable`.
- **The file monitor.** In test builds, the platform's file layer can record every file the process opens; a test runs
  a project that calls every available native with path-like strings (`../../x`, `C:\Windows\win.ini`, `\\host\x`,
  `project:/../x`) and fails on any open outside the project, the game's files and the store.
- **Isolation between projects.** Two projects in one session: neither reads the other's globals or store.
- **`.cmod` packages.** Built by the test from synthetic files: a file whose bytes differ from the manifest, a file the
  manifest does not list, a `../` or absolute entry, an entry marked as a link, two names differing only by case, an
  entry that inflates past its declared size, a bad signature: each is refused, and mounting a good one writes nothing
  to disk.
- **Server packages.** A package over the limits, a file whose hash differs, a project above the SDK level: each is
  refused before anything runs.

### Fuzzing {#fuzzing}

| Target | Input | Checks |
| --- | --- | --- |
| Lua source into a project state | Generated and mutated Lua text (a grammar-based generator, seeded with the project tests' scripts) | No crash, no sanitizer report, no hang past the budget |
| The native bridge | Random values (numbers at the edges, NaN, long strings, nested and cyclic tables, tables with metatables, functions) to every available native | Only clean errors or results; no open outside the allowed roots |
| The project file layer | Random names and folder trees, links included | Only refusals or files inside the project |
| Manifests and `.cmod` packages | Mutated TOML and zip files | Only `std::expected` errors; no write to disk; inflated sizes within the declared ones |
| Each parser of project content | Mutated levels, models, textures, sounds, written synthetically | Only errors; allocations within limits |
| The Lua 4.0 chunk loader and VM | Mutated chunks | Only errors (the patched-script path) |

- **Where it runs.** Fuzz targets are built with Clang's libFuzzer and AddressSanitizer and UndefinedBehaviorSanitizer
  on Linux in a nightly CI job, as the network page's fuzzing does. A crash found becomes a fixture and a unit test.
- **The corpus replays in every build.** The saved corpus (synthetic inputs only) is replayed as ordinary Catch2 tests
  in every preset, so the local gate and the `asan` preset run it on Windows too.

### Review {#reviews}

The [code review](../review/code.md) gains a check for any change that adds or changes a native or a project-facing
parser: the rules of [What natives may do](#natives), raw access in the bridge, no Lua API call from a native, and a
fuzz target for a new parser.

### Keeping the runtime current {#runtime-updates}

Lua 5.4.9 is pinned in `cmake/deps.cmake`: the last 5.4 release, with every fix listed for 5.4. The bugs page is read at
every Coney release; since 5.4 gets no more releases, a fix reachable from scripts that appears for 5.5 is applied to
5.4.9 as a patch the build fetches beside it, with a test, or the runtime moves to 5.5 ([Maintainer
decisions](#decisions)). A dependency update is a security change: its pull request runs the escape suite and the fuzz
corpus and names the bugs it brings in.

## What changes on other pages {#other-pages}

Made in this change:

- [Coney Studio: the Lua SDK](lua-sdk.md#sandbox): its Sandbox section becomes a summary of [Coney's line](#line) that
  links here; its base library gains `collectgarbage("count")` and `setmetatable`'s refusal of `__gc`, and loses
  `string.dump`; its manifest gains `[load]`, `[permissions]`, `[limits]`, versioned `[dependencies]` and the packaged
  `[files]`; its mods folder holds `.cmod` packages and records approvals; its runtime and loader build issues point
  here.
- [Coney Studio: mods, packages and loading](mods.md): the new companion page, split from this one.
- [Network architecture: Sandboxing](../network-architecture.md#sandbox): points here for the rules.
- [Asset and script architecture: The install folder](../asset-and-script-architecture.md#install-folder): `mods/`
  (`.cmod` packages), `cache/` and the per-user `modded/` and `mod-data/` folders; `overrides/` holds data, never
  scripts.

When the work is built:

- [Asset and script architecture: Sources](../asset-and-script-architecture.md#sources): the `OverrideSource` refuses
  every script chunk.
- [Network architecture: Coney resources](../network-architecture.md#resources): the client package, its cache and
  limits ([Coney's model](mods.md#mp-model)), and the `http` and `secrets` permissions.
- [Profiles and saving](../../research/save.md): where a modded session saves.
- [Code review](../review/code.md): the natives and parsers check ([Review](#reviews)).

## Maintainer decisions {#decisions}

Decided and built into these pages: scripts are on by default; console commands act on the running session only; the
release build includes a player console, limited and kept apart from the debug tools ([#1064](issue:1064));
dependencies and exports as FiveM has them; scripts never run in parallel ([One thread, cooperative](#one-thread)).
Open:

1. **Lua 5.4 has no more releases.** The decided runtime for projects is Lua 5.4 ([The runtime](lua-sdk.md#runtime-choice));
   5.4.9 is its last release and later bugs are fixed in 5.5 only. lua-language-server lists 5.5 among its runtimes.
   The choice: keep 5.4.9 and apply script-reachable fixes as patches (this page's assumption), or move projects to
   5.5 before the first release. Nothing else on this page changes either way.
2. **A new dependency for signatures**: libsodium (ISC licence) for Ed25519, which the network page plans for
   encryption ([#561](issue:561)); a licence question is a maintainer's ([AGENTS.md](repo:AGENTS.md)).
3. **No native code from mods.** The roadmap's [Script mods](../../roadmap.md#script-mods) says Coney "may add" a native
   plugin interface later. This page recommends never loading native code from a mod, a project or a server; a native
   plugin, if ever wanted, would be a separate thing the player installs by hand outside `mods/`, as Garry's Mod keeps
   binary modules out of addons.
4. **The mods step of the `.cpak` layout** ([#798](issue:798)): `.cmod` packages replace its `mods/<mod>/` folders
   ([The `.cmod` package](mods.md#cmod)); confirm, and whether a `.cmod` uses the `.cpak` container or zip.

## Open questions {#open-questions}

- **The limits' sizes**: budgets per call and per step, the memory cap and ceiling, the store's quota, the caps on
  things a project makes, the package limits and the watchdog's time, measured on real projects
  ([The SDK's open questions](lua-sdk.md#open-questions)).
- **The pattern functions**: whether refusing long subjects is enough, or the matcher needs a step count of its own (a
  patch to `lstrlib.c`), decided by the fuzzer's findings.
- **Hard links**: whether refusing files with more than one link troubles real projects (some tools make them).
- **A client's own projects on servers**: whether a server may allow them, and how a player sees that it does.
- **The package's name**: `.cmod` is a working name.

## Proposed build issues {#build-issues}

The build issues of this page and of [Mods, packages and loading](mods.md), in order, interleaved with the SDK's [build
issues](lua-sdk.md#build-issues): 1 and 2 come with its runtime (SDK issue 2), 3 to 11 with its loader (SDK issue 3; 7
and 8 also with the console of [#1064](issue:1064)), 12 with its compiler (SDK issue 7) and #1074's patches, 13 and 14
when Studio's Export and code editor are built, 15 and 16 with ConeyMP's resources ([#566](issue:566)), and 17 when a
native needs it.

1. **core: Add the project file layer.** The one way a name becomes a project file, over a folder root: relative names
   with `/`, every refusal of [Files and paths](#paths) (absolute, drive, network and device paths, `..`, alternate
   streams, reserved names, trailing dots), no links of any kind, hard links refused, opened component by component from
   the project folder's handle, size limits, case collisions refused; and the test-only file monitor. `require`, the
   loader and the asset store's project source use it. Tests: every refusal, with links made in a temporary folder; a
   project with two names differing by case is refused; the monitor sees no open outside the roots while a project
   loads.
2. **tests: Add the sandbox escape suite and the script fuzzers.** The `[sandbox]` tests of [The escape
   suite](#escape-suite) against the SDK's project runtime, with the library exactly as [The Lua library](#library)
   lists it (`load` forced to text, `require` text only, `collectgarbage("count")`, `setmetatable` refusing `__gc`, no
   `string.dump`), the sticky and per-step budgets, the charged library calls; the libFuzzer targets for Lua source, the
   native bridge and the file layer, a nightly Linux job, and the corpus replayed as Catch2 tests in every preset.
   Tests: the suite itself, and a planted extra global or a bytecode path makes it fail.
3. **core: Add the limits on what a project makes.** Per-project counters of the handles it holds and caps on its share
   of the game's pools and on Coney's own kinds (models, sounds, effects, HUD messages per step); log lines and bytes
   per step; the watchdog with its "not responding" notice and Stop, off in test mode. Tests: a project that spawns past
   its cap gets the none value and the game's own spawns still succeed; a flooding `print` is cut with one notice line;
   the watchdog never fires in test mode.
4. **core: Add the strict reading of `.cmod` packages.** The one reader of `.cmod` packages, built here; [Content and
   formats](content-and-formats.md#build-issues) build issue 13 adds the export that writes them: the strict zip reader,
   the package hash, the check of every file against the manifest's list with its result kept in `cache/mods/`, the file
   layer over a package root; `mods/` holding packages only (a folder there reported, not loaded) and `coney --project
   <folder>`. Tests: a synthetic package mounts and its project runs; every refusal of the escape suite's `.cmod` item;
   mounting writes nothing to disk; a package edited after its check is checked again.
5. **core: Add starting, stopping, dependencies and the load order.** `mods/enabled.toml` as the start list (a new
   package added on at start, written by nothing else but the mods menu and the player); `[load] autostart`; starting
   dependencies first and stopping dependants first; `[dependencies]` with semantic-version ranges, missing, disabled
   and cyclic ones refused with their messages; the load order from the graph and the file; clash detection from
   manifests (same name, same replaced asset, same game script, `conflicts`); the `mod:/` asset root. Tests: two mods
   run their handlers in the file's order with a dependency first; a cycle and a version outside the range are refused
   while the other mods start; a content mod runs only when its level is played; two mods patching the same game script
   cannot both run.
6. **core: Add exports between mods.** `Coney.Export`, `Coney.CallExport` and `Coney.GetExport`: the call in the
   library's state and budget and charged to the caller's step, values copied deep within limits without metatables,
   functions as callbacks to the state they came from, declared dependencies only, a depth limit, same side only. Tests:
   a library's function returns a copied table the caller cannot use to reach the library's state; a call to an
   undeclared mod and a mutual recursion past the depth are errors; a runaway export stops on the library's budget.
7. **docs: Update the release build's rule for the player console.** Shared with the console design (`console.md`,
   [#1064](issue:1064)), whose release front end depends on it: the rule in [AGENTS.md](repo:AGENTS.md), the conventions
   and [The debug tools and the game](../building.md#debug-tools) that the release build holds no debug tools gains the
   player console as the one console it holds, kept apart by target and model ([The player
   console](mods.md#player-console)); the release marker list loses `imgui.ini` and gains markers only the debug tools
   carry. A maintainer decided the console; this issue changes the text that states the rule.
8. **core: Add the mods' console commands.** The `ModHost` side of the console: the commands of [Console
   commands](mods.md#console) (`mods`, `mod`, `levels`, `start`, `stop`, `restart`, `ensure`, `refresh`, `enable`,
   `disable`, `play`), for the running session only, at a step boundary, the host's or operator's online. Built on the
   console design's console-core issue (`console.md`, [#1064](issue:1064)). Tests against a fake console: start runs
   dependencies first, stop stops dependants first, `disable` keeps a mod off for the session, nothing writes
   `enabled.toml`, a guest gets the listing commands only.
9. **core: Add mod permissions, approvals and the mod details.** `[permissions]` and `[limits]` in the manifest;
   `enabled.toml` recording each approval with the package hash and the signer; a mod whose permissions grew or whose
   signer changed held; the details of [What the player sees](#player-view) for the mods UI. Tests: a mod with
   `game-scripts` starts only once approved; an update adding a permission is held; one dropping a permission starts
   without a question.
10. **core: Add the project's store.** `Coney.SaveData`, `Coney.LoadData`, `Coney.ReadData`, `Coney.JsonEncode` and
    `Coney.JsonDecode`: plain values only, JSON in `mod-data/<project>/` keyed by name and signer, the quota, writes at
    step boundaries. Tests: a value round-trips; functions, cycles and userdata are refused; past the quota nothing is
    written; two projects never see each other's keys.
11. **core: Add modded saves apart from the story and exact modded replays.** A save written while any mod runs goes to
    the per-user `modded/` folder with every running mod's name, version and package hash; a save made with no mod is
    unmarked, in `profiles/`; loading a modded save with a mod missing, stopped or changed warns; a replay records the
    list and every start and stop in the run, and refuses to play back with any difference. Tests: a modded session's
    save leaves `profiles/` byte-for-byte unchanged; a save without mods carries no mark; the warning names the missing
    mod; a replay with one package changed is refused.
12. **core: Add the safety rules for game-script patches.** Around [#1074](issue:1074)'s patches (its issues build the
    format and applying it): a patch read as text only, bytecode refused, applied only to the disc script whose hash
    it pins, compiled by Coney's Lua 4.0 compiler; `overrides/files/` script kinds refused; a memory cap on a modded
    game state; the 4.0 chunk loader fuzz target. Tests: a patched synthetic disc chunk runs in its place; a patch with
    bytecode, one against another hash and a script chunk in `overrides/files/` are refused; the fuzz corpus replays.
13. **tools: Add signed `.cmod` packages.** `coney-tools mod sign` and `mod verify`, Studio's Export signing with the
    user's key: the Ed25519 `coney-project.sig` over the packaged manifest; Coney verifying it at the check, recording
    the fingerprint on first enable and holding an update by another key or unsigned after signed. Needs the libsodium
    decision ([Maintainer decisions](#decisions)). Tests: a signed package verifies; a changed manifest and a wrong key
    are refused; an update by another key is held with its warning.
14. **tools: Add safe opening of shared projects to Studio.** Studio's language-server settings passed by Studio,
    `runtime.plugin` and settings naming programs never taken from a project, a notice when a project carries them; the
    debugger's loopback-only port with a token; the signing key kept out of projects; importers' fuzz targets. Comes
    with Studio's code editor issue. Tests: a fixture project with a plugin setting runs no plugin; the debugger refuses
    a client without the token.
15. **core: Add hosts' and servers' client packages and their cache.** The load-order announcement (names, versions,
    package hashes, packaged manifests), the guest's own `.cmod` used when its package hash matches, the fetch of the
    rest on the rate-capped stream with hash and size checks, client limits the server cannot raise, the cache per
    server with eviction, the join prompt, the client's own mods off while connected, client states with client natives
    only. A sub-issue of [#566](issue:566), used by co-op hosts with mods too. Tests over the loopback: a guest with the
    same package downloads nothing; a guest without it receives and runs the client script; a wrong hash, an over-limit
    package and a higher SDK level are refused; a second server cannot learn what the first sent.
16. **core: Add web requests and secrets for ConeyMP server scripts.** The operator's per-project `http` host list and
    `secrets`, a server-only `Coney.HttpRequest` whose results arrive at a step boundary, secrets usable only as header
    values; absent from client and local states. A sub-issue of [#566](issue:566). Tests: a request to an unlisted host
    is refused; a secret cannot be read as text; the native does not exist in a client state or offline.
17. **core: Add asynchronous jobs for heavy natives.** A worker pool for Coney's own work that does not touch the world
    (path queries, parsing large project data): a native starts a job on copied inputs and returns a job id, the result
    arrives at the step fixed by the request and the simulation waits there for a late worker, at once in test mode; no
    script runs on a worker ([One thread, cooperative](#one-thread)). Filed when a native needs it. Tests: the same run
    gives the same results with one worker and with many, and with a worker slowed on purpose.

## Sources {#sources}

Ideas only; no code is taken from any of these. FiveM's code was not read, only its documentation, of which one
native's page lives in its repository ([Prior art](../network-architecture.md#sources)).

| Idea | Source |
| --- | --- |
| FiveM's resource sandbox: blocked `os` and `io` calls, write rules, no symlinks or `..`, the `add_unsafe_*` lines, `load` kept | FiveM, [Sandbox](https://docs.fivem.net/docs/developers/sandbox/) |
| The client's sandboxed `os`, `io` and `debug` (2024), staff's reasons, `load` kept | Cfx.re forum, [Why disable the debug lib without warning?](https://forum.cfx.re/t/why-disable-the-debug-lib-without-warning/5291361) |
| `LoadResourceFile` and the manifest's `files` on the client | FiveM's native documentation, [LoadResourceFile](https://github.com/citizenfx/fivem/blob/master/ext/native-decls/LoadResourceFile.md) |
| The manifest: client, shared and server scripts, the packfile, `file`, `server_only`, its own runtime, Lua 5.4 for all scripts | FiveM, [Resource manifest](https://docs.fivem.net/docs/scripting-reference/resource-manifest/) |
| CfxLua, a modified Lua 5.4 | FiveM, [Lua runtime](https://docs.fivem.net/docs/scripting-manual/runtimes/lua/) |
| FxDK projects as a runnable server, restart on change | FiveM, [FxDK](https://docs.fivem.net/docs/fxdk/) |
| Backdoor lines (`PerformHttpRequest` with `load`) spreading through resources | Cfx.re forum, [A weird line gets added to some resources](https://forum.cfx.re/t/a-weird-line-gets-added-to-some-resources/5142544) |
| A remote code loader in resources stealing logins and tokens, running `os.execute` | Cfx.re forum, [Blum Panel warning](https://forum.cfx.re/t/warning-blum-panel-blum-panel-me-warden-panel-me-is-a-malicious-backdoor-remote-code-loader-hidden-in-resources/5415839) |
| Luau's sandbox: removed and limited libraries and why, read-only built-ins, per-script globals, interrupts | Luau, [Sandboxing](https://luau.org/sandbox) |
| Luau's security guarantees and what they exclude | Luau, [SECURITY.md](https://github.com/luau-lang/luau/blob/master/SECURITY.md) |
| Roblox script capabilities and sandboxed containers | Roblox, [Script capabilities](https://create.roblox.com/docs/scripting/capabilities) |
| Creator Store moderation of `require(assetId)`, `loadstring`, `InsertService` | Roblox DevForum, [The word "require" made any model not distributed](https://devforum.roblox.com/t/solved-the-word-require-made-any-model-not-distributed-on-creator-store/4051504) |
| Garry's Mod: binary modules kept out of addons; `file.Write` inside `data/` with fixed extensions; `AddCSLuaFile` limits; `sv_allowcslua` | Garry's Mod wiki, [require](https://wiki.facepunch.com/gmod/Global.require), [file.Write](https://wiki.facepunch.com/gmod/file.Write), [AddCSLuaFile](https://wiki.facepunch.com/gmod/Global.AddCSLuaFile) |
| WoW's taint and protected functions | Warcraft Wiki, [Secure Execution and Tainting](https://warcraft.wiki.gg/wiki/Secure_Execution_and_Tainting) |
| WoW's missing OS and file libraries; SavedVariables | Warcraft Wiki, [Lua functions](https://warcraft.wiki.gg/wiki/Lua_functions), [SavedVariables](https://warcraft.wiki.gg/wiki/SavedVariables) |
| Factorio's changed libraries, `require` inside the mod, `load` without binary chunks | Factorio, [Libraries and functions](https://lua-api.factorio.com/latest/auxiliary/libraries.html) |
| Factorio 2020: bytecode exploits, opcode patches, `load` kept | Factorio forums, [Remove the ability to load bytecode through load()](https://forums.factorio.com/viewtopic.php?p=490612) |
| CVE-2024-39840, and 1.1.101 refusing binary chunks | [NVD: CVE-2024-39840](https://nvd.nist.gov/vuln/detail/CVE-2024-39840); Factorio forums, [load() returns nil after 1.1.101](https://forums.factorio.com/viewtopic.php?p=599656) |
| fractureiser and what its last stage stole | fractureiser investigation, [overview](https://github.com/fractureiser-investigation/fractureiser), [technical breakdown](https://github.com/fractureiser-investigation/fractureiser/blob/main/docs/tech.md) |
| Data packs: data and command functions | Minecraft Wiki, [Data pack](https://minecraft.wiki/w/Data_pack) |
| Bedrock Dedicated Server: per-pack module permissions, `server-net`, secrets | Microsoft, [Scripting Bedrock Dedicated Server](https://learn.microsoft.com/en-us/minecraft/creator/documents/scriptingservers) |
| `load`'s mode, unchecked bytecode, `string.rep`'s memory note, `collectgarbage` | [Lua 5.4 reference manual](https://www.lua.org/manual/5.4/manual.html) |
| Bugs reachable from Lua code in 5.4, by release | Lua, [Bugs](https://www.lua.org/bugs.html) |
| 5.4.9 the last 5.4 release; 5.5 current | Lua, [Versions](https://www.lua.org/versions.html) |
| CVE-2021-44964, a sandbox escape from a crafted script in Lua 5.4.0 to 5.4.3 | [NVD: CVE-2021-44964](https://nvd.nist.gov/vuln/detail/cve-2021-44964) |
| CVE-2026-41196, Luanti's `getfenv` escape | [NVD: CVE-2026-41196](https://nvd.nist.gov/vuln/detail/CVE-2026-41196) |
| Install-time and runtime permissions; ask only what an action needs, late | Android, [Permissions on Android](https://developer.android.com/guide/topics/permissions/overview) |
| An update that adds warned permissions disables the extension until accepted | Chrome, [Permission warnings](https://developer.chrome.com/docs/extensions/develop/concepts/permission-warnings) |
| Opening a folder can run code; Restricted Mode | VS Code, [Workspace Trust](https://code.visualstudio.com/docs/editing/workspaces/workspace-trust) |
| lua-language-server's `runtime.plugin` | lua-language-server, [Plugins](https://luals.github.io/wiki/plugins/) |
| lua-language-server's Lua 5.5 runtime | lua-language-server, [VS Code extension](https://marketplace.visualstudio.com/items?itemName=sumneko.lua) |
