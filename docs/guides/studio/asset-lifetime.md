# Coney Studio: loading and releasing assets from scripts

**Status: design.** Nothing on this page is built. How a project's script owns the memory of the assets it uses, as a
FiveM script does: it **loads** what a spawn needs, **waits** until it has arrived, **spawns**, and **releases** it when
it needs it no more. It writes down what a spawn of an asset that is not loaded does, how a prefab's assets are
loaded, what is released when a level ends or a project stops, how the project monitor shows what a project holds, how
this fits the game's own `game:/` models, and why every machine sees the loads in the same order. The natives are the
[Lua SDK](lua-sdk.md#assets)'s, the paths are [Paths](content-and-formats.md#paths), and the worked example (a thread
spawning waves of a prefab) is [below](#wave-example). The parent issue is
[#1054](issue:1054); the request is [#1176](issue:1176).

## Load, wait, spawn, release {#pattern}

FiveM's pattern is `RequestModel`, a wait on `HasModelLoaded`, the spawn, then `SetModelAsNoLongerNeeded`. Its natives,
from its public reference ([Sources](#sources)), and what Coney does with each:

| FiveM | What its reference says | Coney |
| --- | --- | --- |
| `REQUEST_MODEL` | "Request a model (archetype) to be loaded for use by the current script"; use `SET_MODEL_AS_NO_LONGER_NEEDED` when done | `Coney.LoadAsset(path)` |
| `HAS_MODEL_LOADED` | Whether the model is loaded; true even when something other than the current script loaded it, so request it anyway | `Coney.IsAssetLoaded(id)`, true only for what the caller holds, so a script cannot lean on another project's load |
| `SET_MODEL_AS_NO_LONGER_NEEDED` | "Releases the script ownership assigned by `REQUEST_MODEL`" | `Coney.ReleaseAsset(id)` |
| `CREATE_PED`, `CREATE_VEHICLE` | "the model should be loaded already"; return `0` when the entity "failed to be created" (the reference says no more about an unloaded model) | a spawn of an asset that is not held and loaded raises an error naming it |
| `REQUEST_ANIM_DICT`, `HAS_ANIM_DICT_LOADED`, `REMOVE_ANIM_DICT` | The same three steps for animation dictionaries; the reference describes none of them | one trio for every kind of asset |

Coney's version of FiveM's loop is shorter to write and cannot hang on a bad file:

```lua
-- Inside a thread (Coney.CreateThread), because it waits.
local id = Coney.LoadAsset("project:/models/crate.glb")      -- RequestModel
while Coney.GetAssetState(id) == "loading" do Coney.Wait(0) end   -- HasModelLoaded
if Coney.IsAssetLoaded(id) then
  local crate = Coney.SpawnModel("project:/models/crate.glb", {x = 4, y = 2, z = 0}, 90)
  -- ... use the crate ...
  Coney.ReleaseAsset(id)                                      -- SetModelAsNoLongerNeeded
end
```

## The rules {#rules}

A maintainer's direction ([Lua SDK: Maintainer decisions](lua-sdk.md#decisions)). The natives are `shared`
([Where a native runs](lua-sdk.md#determinism)).

- **The id.** `Coney.LoadAsset(path)` returns the asset id at once, before the load completes. One path has one id for
  the session, given in the order of first request, so the id is the same in a replay; ids are numbers, never
  pointers ([Determinism](lua-sdk.md#determinism)). Ids are for the asset natives only; the spawns name the asset by
  its path.
- **Holds and uses.** An asset has two kinds of reference. A **hold** is a project's, one for each `LoadAsset` call. A
  **use** is a living thing's: a human, an object or a prefab member made from the asset, a sound or clip playing it.
  `Coney.ReleaseAsset(id)` drops one hold of the calling project and returns true, or returns false (and logs once)
  when the project holds none. **The memory is freed at the step boundary after the last hold and the last use are
  gone**, so releasing never breaks a living human: he keeps his model until he is deleted. `Coney.SpawnModel` and the
  other natives take their use when they succeed, and `Coney.SetHumanModel` drops the old model's.
- **The states.** `loading` from the call, then `loaded`, or `failed` when the file cannot be read or decoded, with
  the reason as `Coney.GetAssetState`'s second value; `none` is an asset the project does not hold (never loaded, or
  released). A path that cannot be resolved (it does not exist, leaves its root, names a kind Coney cannot load) is
  the call's error instead. `Coney.IsAssetLoaded(id)` is true only in `loaded`. FiveM's loop, `while not
  HasModelLoaded(m) do Wait(0) end`, never ends for a model that cannot load; a Coney loop tests the state and ends on
  `failed`.
- **A spawn of an asset that is not loaded fails, and loads nothing.** `Coney.SpawnModel`, `Coney.SpawnPrefab`,
  `Coney.SetHumanModel`, `Coney.PlayClip`, `Coney.PlaySound` and a `HuCreate` of a project character type raise a Lua
  error that names the asset and its state: `Coney.SpawnModel: project:/models/crate.glb is not loaded (loading; call
  Coney.LoadAsset and wait for Coney.IsAssetLoaded)`. Nothing is loaded behind the script's back, so no stall from a
  load hides in a spawn. It is an error and not FiveM's `0`, because a `nil` handle is easily ignored and the cause is
  the script's; a spawn that fails for a game reason (a full pool, [All or nothing](entities.md#spawn-prefab)) still
  returns `nil` and the reason. "Loaded" means held by the calling project: an asset only another project loaded does
  not count.
- **Prefabs.** `Coney.LoadAsset` on a prefab's path loads every asset its members need, as one asset with one id. The
  prefab file itself holds no asset memory: it is text, read when the prefab is spawned. `Coney.GetPrefabAssets(path)`
  lists those assets for a script that wants some of them or a progress bar ([Spawning a
  prefab](entities.md#spawn-prefab)).
- **Scopes and the safety net.** A hold taken in a project's main chunks or its `start` handlers lasts the session; any
  other hold lasts the level it was taken in. **What a project loaded and never released is released after its
  `levelEnd` handlers (the level's holds) and after its `stop` handlers (the session's)**, each named in the run log and
  in Studio's Console as a warning, so a leak is found and nothing outlives Play; a [project
  restart](lua-sdk.md#project-restart) does the same as `stop`. A level's own entities, made by the loader from the
  level file, load their assets with the level and free them at its end; only what a script spawns is the script's to
  load.
- **When a load completes.** Loads requested before the level loads (main chunks, `start`) are complete before the
  `levelStart` handlers run; any other load completes at a step boundary after the request, in request order, across
  projects in load order. A request that has arrived waits for the earlier ones, so every machine sees the same
  order. In a live run the arrival may take more than one boundary; the replay records the step each load completed at
  and applies it at that step, so a thread's waits land alike. **In test mode a load completes at the first boundary
  after the request and never later**, not inside the calling step: a script that spawns without waiting fails in a
  test as it fails in play ([For the user](#open-questions)).
- **Co-op.** A server script loads on the host and a client script on its own machine, each with its own holds. The
  simulation never waits for a client's load; a client loads the assets of the entities the host tells it of, as for any
  entity ([Network architecture](../network-architecture.md#resources)).
- **No cap.** Like the rest of the SDK, no per-project limit on loaded assets for now ([No
  budgets](lua-sdk.md#sandbox)); a file that cannot be decoded, or memory that runs out, makes the load `failed` with
  the reason. The monitor shows it.

### The monitor lists what a project holds {#monitor}

The SDK's [monitor](lua-sdk.md#monitor) gains an **Assets** column beside memory: how many assets the project holds
and their bytes (the asset store's count of what each occupies, a plain number, repeatable). Opening the row lists them:
the path, the kind, the bytes, the state, how many holds the project has and how many things use it, and the scope
(*session* or *level*). An asset another project holds too is marked *shared*; each holder shows its full size and the
panel's total counts it once. A leak is a list that grows wave after wave, or a *level* hold that is still there at the
next level, and shows without reading a script. The monitor only reads, as it does for time and memory; in test mode
the bytes stay (the time column is the only one off), so a test can pin a project's assets. Studio's Monitor panel and
the F8 bar's Resources panel ([Resources](console-bar.md#resources)) read the same numbers.

## A thread spawning waves {#wave-example}

A wave spawner, written with the pattern of FiveM's `RequestModel`, the wait on `HasModelLoaded`, the spawn and
`SetModelAsNoLongerNeeded` ([Load, wait, spawn, release](#pattern)). The `ambush` prefab ([Spawning a
prefab](entities.md#spawn-prefab)) holds a gang and five humans, one of them named `Boss`; the project spawns it three
times, one wave after the last wave's boss is down. It logs to the run log's `script` category at `error` and `warn`
([Levels and categories](../run-log.md#categories)).

```lua
local AMBUSH = "project:/prefabs/ambush.prefab"
local PLACES = { {x = 30, y = 5, z = 0}, {x = 40, y = -8, z = 0} }

-- Waits for a load the way FiveM's `while not HasModelLoaded(m) do Wait(0) end` does, and
-- ends on a load that failed, which FiveM's loop cannot.
local function waitLoaded(id)
  while true do
    local state, why = Coney.GetAssetState(id)
    if state == "loaded" then return true end
    if state ~= "loading" then
      Coney.Log("script", "error", "ambush assets: " .. (why or state))
      return false
    end
    Coney.Wait(0)
  end
end

Coney.On("levelStart", function()
  Coney.CreateThread(function()
    local assets = Coney.LoadAsset(AMBUSH)         -- the prefab's assets, one id (RequestModel)
    if not waitLoaded(assets) then return end

    for wave = 1, 3 do
      local at = PLACES[(wave - 1) % #PLACES + 1]
      local instance, members = Coney.SpawnPrefab(AMBUSH, at, 90 * wave)
      if instance then
        while HuIsAlive(members.Boss) do Coney.Wait(1000) end
        Coney.DespawnPrefab(instance)
      else
        Coney.Log("script", "warn", "wave " .. wave .. ": " .. members)  -- the reason
      end
      Coney.Wait(5000)
    end

    Coney.ReleaseAsset(assets)                      -- SetModelAsNoLongerNeeded
  end)
end)
```

- **Without the wait**, the first `Coney.SpawnPrefab` raises an error naming the first asset and saying it is still
  `loading`, in a test as in play ([When a load completes](#rules)).
- **Without the release**, the hold lasts until the level ends, when it is released and named as a warning, and the
  Monitor's row for the project lists the assets until then ([The monitor lists what a project
  holds](#monitor)). Returning early on a failed load leaves no hold to release: a failed load holds
  no memory.
- **Loading once for the level** is the same code with the load before the loop and the release after it, as here; a
  project that spawns the ambush from many places loads it once and releases it when it is done with all of them.

## The game's own models (`game:/`) {#game-models}

Two cases.

- **Game natives keep the original's behaviour.** `HuCreate` with a game type number, `ObjCreate` and the rest need no
  `Coney.LoadAsset` and never raise the error above, because the original loads for them itself. Its level's
  dependency list loads resources at `InitLevel` until `UnloadLevel`, the section's pack at the preload, and others
  stream in and out as the resource manager needs them, so a human whose files are not resident is made at once and
  drawn when they arrive ([Resident and streamed](../../research/level-loading.md#resident-and-streamed),
  [Characters: Creation](../../research/characters.md#creation)). That hidden loading is faithful for those natives,
  and faithful first holds ([Studio's decisions](index.md#decisions)).
- **Coney natives that take a path follow the rule above for a `game:/` path as for any other:** the script loads it
  first (`Coney.LoadAsset("game:/models/...")`). A project then does not depend on what the original keeps resident in
  one level.

**Stand-in (Coney's choice):** Coney treats no game asset as resident for a level, so a script loads every `game:/`
path it names, and the load of one already in memory completes at the next boundary without a read. Which models and
resources the original has resident for each level is not known: which Dependency List groups other than `always` the
game wants, and when, is an [open question](../../research/formats/global-lists.md#open-questions) of its page, and what
the section packs hold is not listed anywhere. It is a question for a later research task; the answer changes only
whether a load costs a read, never the rule. Coney has no resource manager yet, so today code reads a game model when it
needs it ([Memory: Coney's implementation](../../research/memory.md#coneys-implementation)).

## Tests {#tests}

By the [testing rules](../testing.md): headless, a fixture asset store with synthetic models and sounds, no disc.

- **The pattern.** A test project loads a fixture model, waits, spawns an object, releases and destroys it; the
  project's held bytes rise at the load and fall to zero at the step boundary after the object is destroyed, not
  before: released while the object lives, the model stays.
- **The error.** A spawn with the asset `none`, `loading`, `failed` and released each raises an error that names the
  asset and the state, and the asset store's read count does not change (nothing was loaded behind the call). A spawn
  that fails because a pool is full returns `nil` and the reason instead of an error.
- **Holds.** Two `LoadAsset` calls need two releases; a release with no hold returns false; one project's hold does not
  make `IsAssetLoaded` true in another, and each project's release frees nothing the other holds; a project character
  type's `HuCreate` raises the error, a game type number's does not.
- **A failed load.** A corrupt fixture ends in `failed` with the reason, and the example's wait loop ends.
- **Order.** Three loads requested in one step arrive in request order although the third finishes first (the fixture
  store delays them); a recorded run replays with every load completing at the recorded step; in test mode a load
  completes at the first boundary and a spawn in the calling step fails.
- **The safety net.** A project leaves a level hold and a session hold: after its `levelEnd` handlers the first is
  gone and a warning names it, after `stop` the second, and its held bytes are zero; a project restart does the same.
- **The monitor.** The row lists the asset with its bytes, hold count and uses; a loop that loads without releasing
  grows the count; two projects holding one asset show it *shared* and the total counts it once; test mode keeps the
  bytes; opening the monitor changes no run.
- **Prefabs.** The cases of [Spawning a prefab](entities.md#tests).

## Open questions {#open-questions}

- **For the user: counted holds or one ownership.** The design counts: each `LoadAsset` takes a hold and each
  `ReleaseAsset` drops one, so two modules of one project that load the same crate do not release it from each other.
  FiveM's reference says only that `SET_MODEL_AS_NO_LONGER_NEEDED` "releases the script ownership", which reads as one
  ownership per script, however many requests. The other choice is one hold per project: simpler to explain, but one
  module's release would break another's next spawn. Which does the user want?
- **For the user: "loaded" means held by the caller.** The design makes `IsAssetLoaded` and the spawns check the
  calling project's own hold, so a missing `LoadAsset` always fails, even when another project loaded the asset.
  FiveM's `HAS_MODEL_LOADED` is true when anything loaded it, and its reference advises requesting anyway. The other
  choice is "loaded by anyone", which is fewer errors and more hidden dependence on load order between mods. Which does
  the user want?
- **For the user: when a test-mode load completes.** There are two choices. *At once*: the load is complete when the
  call returns, so a test needs no waiting at all, but a script that spawns without waiting passes every test and
  fails in play, where a load takes steps. *At the first step boundary after the request*, never inside the calling
  step: a script that spawns without waiting fails in a test as it fails in play, and a test needs no real waiting,
  only one step. The design takes the second. Which does the user want?
- **Research, not for the user: which game assets the original has resident per level.** The Dependency List's groups
  other than `always`, when they are wanted, and the contents of each section pack. Until a research task answers it,
  the stand-in above stands ([The game's own models](#game-models)).

## Sources {#sources}

Ideas only; no code is taken from any of these. Each link is the entry's page in FiveM's native reference; the
sentences quoted above are from the entries' text in the natives repository the reference is built from, read on
2026-10-30.

| Idea | Source |
| --- | --- |
| `REQUEST_MODEL`: a model "to be loaded for use by the current script", released with `SET_MODEL_AS_NO_LONGER_NEEDED`; the Lua loop with `Wait(0)` | FiveM, [REQUEST_MODEL](https://docs.fivem.net/natives/?_0x963D27A58DF860AC) |
| `HAS_MODEL_LOADED`: true also when another script loaded the model | FiveM, [HAS_MODEL_LOADED](https://docs.fivem.net/natives/?_0x98A4EB5D89A0C952) |
| `SET_MODEL_AS_NO_LONGER_NEEDED`: "Releases the script ownership assigned by `REQUEST_MODEL`" | FiveM, [SET_MODEL_AS_NO_LONGER_NEEDED](https://docs.fivem.net/natives/?_0xE532F5D78798DAAB) |
| A spawn expects the model "loaded already"; `0` when the entity failed to be created | FiveM, [CREATE_PED](https://docs.fivem.net/natives/?_0xD49F9B0955C367DE) and [CREATE_VEHICLE](https://docs.fivem.net/natives/?_0xAF35D0D2583051B0) |
| The same three steps for animation dictionaries | FiveM, [REQUEST_ANIM_DICT](https://docs.fivem.net/natives/?_0xD3BD40951412FEF6), [HAS_ANIM_DICT_LOADED](https://docs.fivem.net/natives/?_0xD031A9162D01088C), [REMOVE_ANIM_DICT](https://docs.fivem.net/natives/?_0xF66A602F829E2A06) |
