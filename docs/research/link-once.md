# The link-once region

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims.

## Purpose

The game's code ends with a block of 651 functions at `0x004da118`-`0x004f6578` that belong to no single source file
(607 found by Ghidra's analysis, and 44 tiny tactic slots reached only through vtables, made into functions since):
the compiler emitted them once for the whole program because they are **template instances** (the standard library's
containers and algorithms, the game's own free lists and safe-delete helpers) or **inline virtual functions** of
classes declared in headers (deleting destructors, one-line getters and setters, slot forwarders). This page lists
every one by owner, so a reader who meets an address in this range in another page knows what it is.

For Coney almost none of it is reimplemented as such: the containers are the C++ standard library's, and an inline
virtual belongs with its class. What matters is what each instance **does for its owner**: the key a map is ordered
by, the sort key of a menu list, the size of a queue's element, a tactic's type number. Those facts are in the
tables below and in the owners' pages.

Every function here has a name and a plate comment in the shared Ghidra project; the plate ends with a pointer to this
page.

## Original structure

The region seems to follow the order in which the original link met each class (inferred): first the core helpers
(the `Vec4` and quaternion inlines, the handle arrays, `SafeDelete`), then the camera classes, the file manager and
file systems, the resource manager's maps, the front end's pages and widgets, the AI's tactics, the memory managers,
the script system, the task managers and the world's boxes. Neighbouring functions usually share an owner.

## The families {#families}

Most functions are instances of a few templates. Knowing the shape of each saves reading them one by one. Confirmed
(code) for every family.

- **Deleting destructors** (44-48 bytes): store the base class's vtable in the object (often the root vtable
  `0x00534298`, or a class base such as the tactic base `0x00543260`), then call `operator delete` (`0x0042c820`)
  when bit 0 of the flag is set. They sit at slot `+0x08` of their vtable (slot `+0x28` for tactics, whose vtable
  pointer is at `+0x1c`). The owner is the class whose constructor stores that vtable.
- **Widget destroy slots** (slot `+0x60`): destroy the widget's members in reverse order, then `Widget_Destroy`,
  `BaseWidget_Destroy` or `MessageHUD_Destroy`; arrays of child widgets are walked backwards through each child's
  own slot `+0x60`.
- **`SafeDelete<T>` / `SafeFree<T>` / `SafeDeleteDirect<T>`**: a null check, the object's destructor (vtable slot
  `+0x08` with flag 2, or a direct call), then `g_Memory` slot `+0x58` with the caller's file and line. One instance
  per pointee type; the names carry the type.
- **`std::map` instances** (`std::map<uint, T*>` with `0x18`-byte nodes for the resource maps, `u16` keys for the
  rumble character data): `LowerBound`, `InsertNode` (insert and rebalance), `InsertUnique`, `InsertHint` and
  `Find` or `EraseSubtree`, each map with its own copies. The node's key is at `+0x10`.
- **`std::list` instances**: `Clear` (frees every node), `Delete` (clear, free the head node, free the list object),
  and the odd `Remove` or `EraseRange`. The allocator's tag string names the list (`SFC_States`, `TextItemContainer`,
  `TutorialItemPQueue`, `STL_List(QueueElement*)`).
- **`std::vector` instances**: `insert_aux` (the grow-and-insert path of `push_back`, 21 instances), vector deletes
  (free the storage, then the vector object), uninitialised copies and assignment.
- **`std::deque` instances**: `CreateNodes`, `InitMap` (at least eight node pointers, centred), `ReallocMap`,
  `PushBackNode` and `PopFrontNode`. Three deques use them: the file manager's request queue (`0x54`-byte requests, six
  per `0x1f8`-byte node), the resource manager's deferred requests (12-byte requests, 42 per `0x1f8`-byte node) and
  the world manager's pack-name queue (4-byte strings, 128 per `0x200`-byte node).
- **`std::sort` chains** (introsort, insertion sort, heap adjust, median of three, partition): four instances, each
  sorting a menu list by one key: the mission list by the level record's float `+0x04`, the area list by a value read
  through `0x0041f428` (compared with `0x00433a68`), the game-statistics levels by the level record's byte `+0x0c`
  and the swap-soldier list by the soldier's byte `+0x18`.
- **SGI `hash_map` instances** (the string table cache and the object-type list): bucket fill and fill-insert, clear,
  iterator increment, resize to the next prime (list at `0x0057dfd8`) and insert-unique. The string hash is
  `h = h × 5 + c`.
- **Free lists** (`FreeListContainer<T>`, the task managers' pools and the turf boxes): slot `+0x10` takes a slot and
  constructs a `T` in it, `+0x18` takes a raw slot (null when the pool is empty), `+0x20` destroys a `T` and returns
  its slot (slot `+0x28`), `+0x08` frees the pool's two arrays. `FreeList<T>` (script objects, player and volume boxes)
  has only the delete.
- **Handle arrays** of 2, 4, 16 and 60 handles: clear, index of, add in a free slot, remove (set to the null handle
  `0x006ebd30`), prune the dead, nearest to a point, sort by distance. The AI's brains keep 16-handle arrays of enemies
  and attack slots; the trigger spheres and volume boxes keep 60-handle occupant arrays.

## Tactic types {#tactic-types}

Each AI tactic class ([Tactics](ai.md#tactics)) has three inline virtuals here: slot `+0x28` the deleting destructor,
slot `+0x30` **GetType** (returns the class's type number) and slot `+0x38` **IsCombatType** (type below `0x12`: the
fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up). The vtables lie every `0x60`
bytes from `0x005432c0` to `0x005441c0`. Confirmed (code); the meaning of the `0x12` split is inferred.

| Type | Tactic | Type | Tactic | Type | Tactic |
| --- | --- | --- | --- | --- | --- |
| `0x00` | Attack | `0x0e` | Luther (boss) | `0x1c` | Vandalize |
| `0x01` | WarriorAttack | `0x0f` | BossScenarioF | `0x1d` | Steal |
| `0x02` | Defend | `0x10` | Virgil (boss) | `0x1e` | vtable `0x00543e00` |
| `0x03` | PlayerGang | `0x11` | Chatterbox (boss) | `0x1f` | vtable `0x00543b00` |
| `0x04` | HoldTheLine | `0x12` | WarriorFollow | `0x20` | AvoidEnemies |
| `0x05` | vtable `0x00543ce0` | `0x13` | vtable `0x00543980` | `0x21` | UseFlag |
| `0x06` | ManWeaponPile | `0x14` | Pursue | `0x22` | StandGround |
| `0x07` | Domination | `0x15` | WalkinTall | `0x23` | Confront |
| `0x08` | vtable `0x00543c80` | `0x16` | Wander | `0x24` | Idle |
| `0x09` | vtable `0x005433e0` | `0x17` | TravelPath | `0x25` | vtable `0x00543d40` |
| `0x0a` | DiegoVargas (boss) | `0x18` | HanginOut | `0x26` | vtable `0x005441c0` |
| `0x0b` | vtable `0x005434a0` (boss) | `0x19` | MoveToFlag | `0x27` | Scout |
| `0x0c` | Moe (boss) | `0x1a` | vtable `0x005432c0` | `0x28` | vtable `0x00543f20` |
| `0x0d` | Roof (boss) | `0x1b` | Crowd | | |

Slot `+0x40` returns 1 for Crowd, Idle, `0x1f`, StandGround and `0x1a`, and 0 for the rest; what it asks is open.

## Functions {#functions}

The tables list every function of the region by owner, with what it does. Names in the deleting-destructor and
free-list rows follow the families above.

### Memory and template helpers {#mem}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004da118` | `LinkOnce_StaticInit` | a global constructor in the list `DoGlobalCtors` walks: `0x0042eb58(0x005341f0, 0x00714ae8)` (a static object's registration; library call not traced) | confirmed (code) |
| `0x004da140` | `SortedU64_LowerBound` | binary search over sorted 8-byte keys: the first not below the key (the hash tables' prime list, `ObjTypeList`) | confirmed (code) |
| `0x004dab08` | `Memory_FreeIfSet` | frees a block through `g_Memory` slot `+0x58` when not null | confirmed (code) |
| `0x004ddb58` | `RbTree_EraseSubtree` | `std::map` / `std::set` node teardown: erases a subtree recursively (the profile-manager modes' sets and others) | confirmed (code) |
| `0x004ddd30` | `Ptr_Assign` | `*p = v`; shared by 47 widget and control vtables (slot `+0x10` / `+0x18`) | confirmed (code) |
| `0x004de310` | `SafeDeleteArrayPod` | null-safe free of an array allocation (header 4 bytes before it), no destructors | confirmed (code) |
| `0x004de938` | `SafeDelete_CarInstance` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_ReleaseCarInstance` | confirmed (code) |
| `0x004de9c8` | `SafeDelete_ModelEntry` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_FreeCarModel`, `ResourceMgr_FreeCharModel` | confirmed (code) |
| `0x004dec50` | `SafeDelete_CharacterInstance` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_ReleaseCharacterInstance` | confirmed (code) |
| `0x004ded90` | `SafeFree_CharacterModelDestruct` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `CharacterModel_Destruct` | confirmed (code) |
| `0x004dedd8` | `SafeDeleteDirect_Embers` | null-safe `delete` calling `Embers_Destruct` directly, then the free; used by `Embers_Destroy` | confirmed (code) |
| `0x004dee48` | `LinkOnce_EmptyStub48` | an empty function with no callers and no vtable reference (an unused inline) | confirmed (code) |
| `0x004df228` | `SafeDelete_ObjectInstance` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_ReleaseObjectInstance` | confirmed (code) |
| `0x004df2a8` | `SafeDelete_ObjectModel` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_FreeObjectModel` | confirmed (code) |
| `0x004df390` | `SafeDelete_Sheet` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_ReleaseSheet` | confirmed (code) |
| `0x004e36a8` | `SafeDelete_ScreenFxEndLayer` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ScreenFx_EndLayer` | confirmed (code) |
| `0x004e3728` | `SafeFree_ScreenFxResetAll` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `ScreenFx_ResetAll` | confirmed (code) |
| `0x004e3780` | `SafeDelete_TextureSet` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ResourceMgr_FreeTextures`, `WaterEffect_Unload` | confirmed (code) |
| `0x004e39c8` | `SafeDelete_RadarBlip` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GameStats_Shutdown`, `HUD_ANLaunchEndScreen`, `HUD_ShutdownLevel`, `OverlayEffect_Destruct` ... | confirmed (code) |
| `0x004e3c90` | `SafeDelete_HeatDistortion` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `DistortionMgr_DestroyAll`, `DistortionMgr_StopHeat` | confirmed (code) |
| `0x004e3d10` | `SafeDelete_WaveDistortion` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `DistortionMgr_DestroyAll`, `DistortionMgr_StopWave` | confirmed (code) |
| `0x004e3f38` | `SafeFree_BaseWidgetEnableShadow` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `BaseWidget_EnableShadow`, `BaseWidget_Release` | confirmed (code) |
| `0x004e41a0` | `SafeDelete_ChecklistItem` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `Checklist_Clear`, `Checklist_Remove` | confirmed (code) |
| `0x004e44e0` | `SafeDelete_CreditsShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `Credits_Shutdown`, `PM_Continue_Shutdown`, `PM_Create_Shutdown`, `PM_Delete_Shutdown` ... | confirmed (code) |
| `0x004e4560` | `SafeFree_CreditsShutdown` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `Credits_Shutdown` | confirmed (code) |
| `0x004e4640` | `SafeDeleteDirect_StringTableCache` | null-safe `delete` calling `StringTableCache_Destroy` directly, then the free; used by `Credits_Shutdown`, `RM_Controller_Shutdown` | confirmed (code) |
| `0x004e4768` | `SafeDelete_ScrollingMenuItem` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GridContainer_Shutdown`, `ScrollingMenuItem_ReleaseDescription`, `ScrollingMenu_Shutdown` | confirmed (code) |
| `0x004e47e8` | `SafeDelete_GridItem` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GridContainer_Shutdown` | confirmed (code) |
| `0x004e4a78` | `SafeDelete_HUDShutdownLevel` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `HUD_ShutdownLevel` | confirmed (code) |
| `0x004e6698` | `SafeDelete_MissionSelectShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `MissionSelect_Shutdown` | confirmed (code) |
| `0x004e6718` | `SafeDeleteDirect_MissionSelectShutdown` | null-safe `delete` calling `MSMission_Delete` directly, then the free; used by `MissionSelect_Shutdown` | confirmed (code) |
| `0x004e6788` | `SafeDeleteDirect_MissionSelectShutdown2` | null-safe `delete` calling `MSArea_Delete` directly, then the free; used by `MissionSelect_Shutdown` | confirmed (code) |
| `0x004e7f20` | `SafeDelete_PMContinueShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Continue_Shutdown`, `PM_Delete_Shutdown`, `PM_Difficulty_Shutdown`, `PM_Extras_Shutdown` ... | confirmed (code) |
| `0x004e7fa0` | `SafeDelete_GameStatsShutdown2` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GameStats_Shutdown`, `PM_Continue_Shutdown`, `PM_Delete_Shutdown`, `PM_Difficulty_Shutdown` ... | confirmed (code) |
| `0x004e8270` | `SafeDelete_OptionGridShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `OptionGrid_Shutdown` | confirmed (code) |
| `0x004e82f0` | `SafeDelete_OptionGridShutdown2` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `OptionGrid_Shutdown` | confirmed (code) |
| `0x004e8528` | `SafeDelete_OptionPageShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `OptionPage_Shutdown` | confirmed (code) |
| `0x004e87d0` | `SafeDelete_ScrollingMenuShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ScrollingMenu_Shutdown` | confirmed (code) |
| `0x004e92b8` | `SafeDelete_GameStatsShutdown3` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GameStats_Shutdown`, `RM_ChooseArea_Shutdown`, `RM_GameMode_Shutdown`, `RM_SwapSoldier_Shutdown` ... | confirmed (code) |
| `0x004e9580` | `SafeDelete_PMCreateShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Create_Shutdown`, `PM_Delete_Shutdown`, `PM_NoSpace_Shutdown`, `PM_TooManyProfiles_Shutdown` ... | confirmed (code) |
| `0x004e9600` | `SafeDelete_PMLightShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Light_Shutdown`, `RM_SwapSoldier_Shutdown` | confirmed (code) |
| `0x004e9680` | `SafeDelete_PMLightShutdown2` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Light_Shutdown`, `RM_ChooseGangs_Shutdown`, `RM_EditGang_Shutdown`, `RM_EditGangs_Shutdown` ... | confirmed (code) |
| `0x004e9960` | `SafeDelete_RMChooseAreaShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_ChooseArea_Shutdown` | confirmed (code) |
| `0x004e99e0` | `SafeFree_RMChooseAreaShutdown` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `RM_ChooseArea_Shutdown` | confirmed (code) |
| `0x004e9c60` | `SafeFree_RMChooseGangsShutdown` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `RM_ChooseGangs_Shutdown` | confirmed (code) |
| `0x004e9ca8` | `SafeDelete_RMChooseGangsShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_ChooseGangs_Shutdown`, `RM_EditGang_Shutdown`, `RM_EditGangs_Shutdown` | confirmed (code) |
| `0x004e9d28` | `SafeDeleteDirect_RMChooseGangsShutdown` | null-safe `delete` calling `RM_GangData_Free` directly, then the free; used by `RM_ChooseGangs_Shutdown` | confirmed (code) |
| `0x004ea610` | `SafeDelete_RMMain` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea690` | `SafeDelete_RMNo2ndController` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea710` | `SafeDelete_RMNumPlayers` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea790` | `SafeDelete_RMGameMode` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea810` | `SafeDelete_RMChooseArea` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea890` | `SafeDelete_RMChooseGangs` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea910` | `SafeDelete_RMCreateGang` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004ea990` | `SafeDelete_RMEditGang` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004eaa10` | `SafeDelete_RMEditGangs` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004eaa90` | `SafeDelete_RMSwapSoldier` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004eab10` | `SafeFree_RMControllerShutdown` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `RM_Controller_Shutdown` | confirmed (code) |
| `0x004eac98` | `SafeDelete_PMCreateShutdown2` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Create_Shutdown`, `RM_CreateGang_Shutdown` | confirmed (code) |
| `0x004eaeb8` | `SafeDelete_GameStatsSubItemShutdown` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `GameStatsSubItem_Shutdown`, `GameStats_Shutdown`, `RM_GameMode_Shutdown` | confirmed (code) |
| `0x004eaf38` | `SafeDeleteDirect_RMGameModeShutdown` | null-safe `delete` calling `RM_GameModeData_Free` directly, then the free; used by `RM_GameMode_Shutdown` | confirmed (code) |
| `0x004eba20` | `SafeDelete_PMGreet` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebaa0` | `SafeDelete_PMNoSpace` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebb20` | `SafeDelete_PMTooManyProfiles` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebba0` | `SafeDelete_PMMode` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebc20` | `SafeDelete_PMExtras` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebca0` | `SafeDelete_PMNumPlayers` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebd20` | `SafeDelete_PMProfile` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebda0` | `SafeDelete_PMCreate` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebe20` | `SafeDelete_PMLoad` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebea0` | `SafeDelete_PMContinue` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebf20` | `SafeDelete_PMDelete` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ebfa0` | `SafeDelete_PMDifficulty` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ec020` | `SafeDelete_PMLight` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ec0a0` | `SafeDelete_PMSubtitles` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PM_Controller_Shutdown` | confirmed (code) |
| `0x004ed3f8` | `SafeFree_GameStatsShutdown` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `GameStats_Shutdown` | confirmed (code) |
| `0x004ed538` | `SafeFree_HumanDestroy` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `Human_Destroy`, `Tag_End` | confirmed (code) |
| `0x004f0520` | `MemHeap_Contains` | slot `+0xa8` of the heap vtables (`0x00544370`, `0x00544428`, `0x005444e0`): reads the heap's range (slot `+0x78`) and returns whether the address lies in it | confirmed (code) |
| `0x004f0598` | `MemHeap544370_DeletingDtor` | heap class `0x00544370` (`MemoryManager_Init`) deleting destructor | confirmed (code) |
| `0x004f0608` | `MemHeap544428_DeletingDtor` | heap class `0x00544428` (`MemoryManager_Init`) deleting destructor | confirmed (code) |
| `0x004f0650` | `WarriorsMemory_DeletingDtor` | heap class `0x005444e0` (`WarriorsMemory_StaticInit`) deleting destructor | confirmed (code) |
| `0x004f06a8` | `PoolTrackMap_LowerBound` | `std::map` of the memory-pool tracker's record map (`MemoryPoolTrack_*`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004f06e8` | `PoolTrackMap_InsertNode` | `std::map` of the memory-pool tracker's record map (`MemoryPoolTrack_*`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004f0ac0` | `PoolTrackMap_InsertUnique` | `std::map` of the memory-pool tracker's record map (`MemoryPoolTrack_*`): `insert_unique(value)` | confirmed (code) |
| `0x004f0c18` | `PoolTrackMap_InsertHint` | `std::map` of the memory-pool tracker's record map (`MemoryPoolTrack_*`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004f0dc8` | `PoolTrackMap_Find` | `std::map` of the memory-pool tracker's record map (`MemoryPoolTrack_*`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004f0e30` | `PoolTrackMap_EraseSubtree` | the memory-pool tracker's map: erases a subtree recursively (debug STL allocator) | confirmed (code) |
| `0x004f0ed0` | `MemoryPoolTrack_DeletingDtor` | `MemoryPoolTrack` (vtable `0x005445f8`) deleting destructor: erases its map and frees the header | confirmed (code) |
| `0x004f1148` | `SafeDelete_PhysicsBodyDestroy` | null-safe `delete`: destructor (vtable slot `+0x08` (the vtable at `+0x08`), flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `PhysicsBody_Destroy` | confirmed (code) |
| `0x004f1368` | `SafeDelete_MoviePlay` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `Movie_Play`, `SceneTask_FreeData` | confirmed (code) |
| `0x004f1450` | `SafeDelete_ScriptSystem` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `ScriptSystem_Destroy` | confirmed (code) |
| `0x004f3be0` | `SafeFree_ObjectManagerFreeArrays` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `ObjectManager_FreeArrays` | confirmed (code) |
| `0x004f3c28` | `SafeFree_ObjectManagerFreeArrays2` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `ObjectManager_FreeArrays` | confirmed (code) |
| `0x004f4820` | `SafeDelete_TaskManagerDestruct` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TaskManager_Destruct` | confirmed (code) |
| `0x004f48a0` | `SafeDelete_TaskManagerDestruct2` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TaskManager_Destruct` | confirmed (code) |
| `0x004f4920` | `SafeDelete_TaskManagerDestruct3` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TaskManager_Destruct` | confirmed (code) |
| `0x004f49a0` | `SafeDelete_TaskManagerDestruct4` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TaskManager_Destruct` | confirmed (code) |
| `0x004f4a20` | `SafeDelete_TaskManagerDestruct5` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TaskManager_Destruct` | confirmed (code) |
| `0x004f4af8` | `SafeDelete_WorldLevel` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `WorldLevel_Release` | confirmed (code) |
| `0x004f4b78` | `SafeDelete_LevelObjectDestroy` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `LevelObject_Destroy` | confirmed (code) |
| `0x004f4bf8` | `SafeFree_LevelObjectDestroy` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `LevelObject_Destroy` | confirmed (code) |
| `0x004f4e98` | `Chars_Copy` | copies n bytes forward | confirmed (code) |
| `0x004f4ed8` | `RcString_Clone` | a new string buffer (capacity a power of two from 16; header: length `-0x10`, capacity `-0x0c`, references `-0x08` = 1, unshareable `-0x04` = 0) holding a copy; returns its text | confirmed (code) |
| `0x004f4fc0` | `CiString_Length` | the length of a case-insensitive string (char traits that fold `A`-`Z`) | confirmed (code) |
| `0x004f5040` | `Chars_Move` | copies n bytes, backwards when the ranges overlap | confirmed (code) |
| `0x004f50e0` | `RcString_Replace` | replaces a range of a reference-counted string with n characters, copying the buffer when it is shared or too small (`WorldManager_QueuePack`) | confirmed (code) |
| `0x004f57d8` | `SafeDeleteDirect_WorldManagerUnload` | null-safe `delete` calling `World_Destroy` directly, then the free; used by `WorldManager_Unload` | confirmed (code) |
| `0x004f5848` | `SafeDeleteDirect_WorldManagerUnload2` | null-safe `delete` calling `WaterEffect_Destruct` directly, then the free; used by `WorldManager_Unload` | confirmed (code) |
| `0x004f58b8` | `SafeFree_WorldManagerUnload` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `WorldManager_Unload` | confirmed (code) |
| `0x004f5d80` | `SafeDelete_TurfBoxPool` | null-safe `delete`: destructor (vtable slot `+0x08`, flag 2), then `g_Memory` slot `+0x58` with the caller's file and line; a template instance used by `TurfBox_DestroyPool` | confirmed (code) |
| `0x004f6208` | `SafeFree_FlagPoolDestroy` | null-safe `delete` of a type without a destructor: `g_Memory` slot `+0x58` with file and line; used by `FlagPool_Destroy` | confirmed (code) |
| `0x004f6250` | `SafeDeleteDirect_FlagPoolDestroy` | null-safe `delete` calling `WorldPath_Destruct` directly, then the free; used by `FlagPool_Destroy` | confirmed (code) |

### Maths {#maths}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004dabe8` | `Vec4_Sub` | `a − b` (VU0) | confirmed (code) |
| `0x004dac60` | `Vec4_Add` | `a + b` (VU0) | confirmed (code) |
| `0x004dac90` | `Vec4_Scale` | `v × s` (VU0) | confirmed (code) |
| `0x004dacc0` | `Vec3_Cross` | cross product (VU0 outer product) | confirmed (code) |
| `0x004db4d0` | `Vec3_Length` | the length of a vector (VU0 square root) | confirmed (code) |
| `0x004db510` | `Quat_Multiply` | quaternion product (VU0 outer product plus the scalar terms) | confirmed (code) |
| `0x004e6918` | `Vec4_AddTo` | `a += b` (`Vec4_Add(a, a, b)`), returns `a`; `PhysicsHumanBody_PushAwayHumans` | confirmed (code) |
| `0x004f3c70` | `Vec_PackFixed12` | packs a vector into four s16 with 12 fractional bits (VU0 `vftoi12`); `ObjRecord_Add`, `ObjRecord_Store` | confirmed (code) |

### Handle arrays {#handles}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004dad20` | `HandleArray4_Clear` | fills four handles with -1 (`memset 0xff`, 16 bytes) | confirmed (code) |
| `0x004dad40` | `HandleArray4_Compact` | drops dead handles from a four-entry array, keeping the live ones in order; returns the count | confirmed (code) |
| `0x004dadf0` | `HandleArray_At` | `array + 4 × i` | confirmed (code) |
| `0x004dae00` | `HandleArray16_CountLive` | live handles among 16 (the brains' attacker slots) | confirmed (code) |
| `0x004dae60` | `HandleArray4_IndexOf` | index of a handle among four, or -1 | confirmed (code) |
| `0x004dae90` | `HandleArray4_CountLive` | live handles among four | confirmed (code) |
| `0x004daef0` | `HandleArray4_PutInFree` | stores a handle in the first dead slot of four | confirmed (code) |
| `0x004daf50` | `HandleArray4_Add` | `HandleArray4_PutInFree` | confirmed (code) |
| `0x004daf70` | `HandleArray4_Remove` | clears a handle's slot if present | confirmed (code) |
| `0x004dafb8` | `HandleArray4_AddUnique` | adds a handle unless present, into the first dead slot | confirmed (code) |
| `0x004db060` | `HandleArray_ClearAt` | slot `i` = -1 | confirmed (code) |
| `0x004db5b0` | `HandleArray16_Nearest` | the live object among 16 handles nearest a point (not counting one to skip): its handle and squared distance, via each object's position slot `+0xa8` | confirmed (code) |
| `0x004db828` | `HandleArray2_Clear` | fills two handles with -1 | confirmed (code) |
| `0x004db848` | `HandleArray2_Compact` | drops dead handles from a two-entry array, keeping order; returns the live count | confirmed (code) |
| `0x004db8f8` | `HandleArray2_IndexOf` | index of a handle among two, or -1 | confirmed (code) |
| `0x004db928` | `HandleArray2_PutInFree` | stores a handle in the first dead slot of two | confirmed (code) |
| `0x004db988` | `HandleArray2_Remove` | clears a handle's slot if present | confirmed (code) |
| `0x004db9d0` | `HandleArray2_CountLive` | live handles among two (the locked camera's kept humans) | confirmed (code) |
| `0x004dba30` | `HandleArray2_At` | `array + 4 × i` | confirmed (code) |
| `0x004de098` | `HandleArray16_At` | `array + 4 × i` (238 callers: the brains' handle lists) | confirmed (code) |
| `0x004de0a8` | `HandleArray16_Clear` | fills 16 handles with -1 | confirmed (code) |
| `0x004de0c8` | `HandleArray16_IsEmpty` | true when none of 16 handles is live | confirmed (code) |
| `0x004de130` | `HandleArray16_First` | the first live handle of 16, or the null handle `0x006ebd30` | confirmed (code) |
| `0x004de198` | `EnemyList16_At` | `array + 4 × i` (a second instance, for the brains' enemy lists) | confirmed (code) |
| `0x004de1a8` | `HandleArray16_PutInFree` | stores a handle in the first dead slot of 16 | confirmed (code) |
| `0x004de208` | `HandleArray16_IndexOf` | index of a handle among 16, or -1 | confirmed (code) |
| `0x004de238` | `HandleArray16_ClearAt` | slot `i` = the null handle | confirmed (code) |
| `0x004eda68` | `HandleArray16_Copy` | copies 16 handles (`0x40` bytes) | confirmed (code) |
| `0x004edaf0` | `HandleArray16_Remove` | sets the matching handle to the null handle (`0x006ebd30`); `Brain_UncountEnemy`, `Brain_ReleaseAttackSlot` | confirmed (code) |
| `0x004edb38` | `HandleArray16_PruneDead` | replaces every handle that no longer resolves with the null handle | confirmed (code) |
| `0x004edba8` | `HandleArray16_SortByDistance` | sorts the live handles by squared distance to a point with a given comparator (`qsort`, `0x00433fd8`), packs them first and fills the rest with the null handle | confirmed (code) |
| `0x004edd20` | `HandleArray16_SortNearest` | `HandleArray16_SortByDistance` with the comparator `0x0028a328` (13 callers: attack slots, engage enemy, nearest gang member) | confirmed (code); order inferred |
| `0x004ede08` | `HandleArray16_NearestHandle` | the handle whose object is nearest a point, or -1 | confirmed (code) |
| `0x004f0420` | `HandleArray16_SortForCharge` | `HandleArray16_SortByDistance` with the comparator `0x00321d80` (`ChargeSubTactic_Order`) | confirmed (code) |
| `0x004f5c78` | `HandleArray60_Clear` | fills 60 handles with -1 (`memset` 0xff over `0xf0` bytes) | confirmed (code) |
| `0x004f5c98` | `HandleArray60_IndexOf` | index of a handle among 60, or -1 | confirmed (code) |
| `0x004f5cc8` | `HandleArray60_AddInFreeSlot` | stores a handle in the first slot that no longer resolves | confirmed (code) |
| `0x004f5d28` | `HandleArray60_Remove` | sets the matching handle to the null handle | confirmed (code) |
| `0x004f5d70` | `HandleArray60_At` | address of slot n | confirmed (code) |
| `0x004f61b0` | `HandleArray60_ClearAt` | sets slot n to the null handle (`VolumeBox_Update`) | confirmed (code) |

### Sorts {#sorts}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004e5098` | `MissionSort_PushHeap` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__push_heap` | confirmed (code) |
| `0x004e51a8` | `MissionSort_AdjustHeap` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__adjust_heap` | confirmed (code) |
| `0x004e52f0` | `MissionSort_MakeHeap` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `make_heap` | confirmed (code) |
| `0x004e5380` | `MissionSort_SortHeap` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `sort_heap` | confirmed (code) |
| `0x004e5428` | `MissionSort_PartialSort` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `partial_sort` (the heap fallback of the introsort) | confirmed (code) |
| `0x004e5548` | `MissionSort_Partition` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__unguarded_partition` round a pivot | confirmed (code) |
| `0x004e5670` | `MissionSort_IntrosortLoop` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__introsort_loop`: median-of-three quicksort down to runs of 16, heap sort past the depth limit | confirmed (code) |
| `0x004e58f8` | `MissionSort_LinearInsert` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__unguarded_linear_insert` | confirmed (code) |
| `0x004e59b0` | `MissionSort_InsertionSort` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__insertion_sort` | confirmed (code) |
| `0x004e5ab8` | `MissionSort_UnguardedInsertion` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__unguarded_insertion_sort` | confirmed (code) |
| `0x004e5b08` | `MissionSort_FinalInsertion` | `std::sort` of pointers by the level record's float `+0x04` (records by id through `0x0041f540`), for the mission list: `__final_insertion_sort`: insertion sort of the first 16, unguarded after | confirmed (code) |
| `0x004e5b78` | `AreaSort_PushHeap` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__push_heap` | confirmed (code) |
| `0x004e5c90` | `AreaSort_AdjustHeap` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__adjust_heap` | confirmed (code) |
| `0x004e5dd8` | `AreaSort_MakeHeap` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `make_heap` | confirmed (code) |
| `0x004e5e68` | `AreaSort_SortHeap` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `sort_heap` | confirmed (code) |
| `0x004e5f10` | `AreaSort_PartialSort` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `partial_sort` (the heap fallback of the introsort) | confirmed (code) |
| `0x004e6038` | `AreaSort_Partition` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__unguarded_partition` round a pivot | confirmed (code) |
| `0x004e6168` | `AreaSort_IntrosortLoop` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__introsort_loop`: median-of-three quicksort down to runs of 16, heap sort past the depth limit | confirmed (code) |
| `0x004e6410` | `AreaSort_LinearInsert` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__unguarded_linear_insert` | confirmed (code) |
| `0x004e64d0` | `AreaSort_InsertionSort` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__insertion_sort` | confirmed (code) |
| `0x004e65d8` | `AreaSort_UnguardedInsertion` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__unguarded_insertion_sort` | confirmed (code) |
| `0x004e6628` | `AreaSort_FinalInsertion` | `std::sort` of pointers by a value read through `0x0041f428` and compared with `0x00433a68`, for the area list: `__final_insertion_sort`: insertion sort of the first 16, unguarded after | confirmed (code) |
| `0x004eb210` | `SoldierSort_PushHeap` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__push_heap` | confirmed (code) |
| `0x004eb290` | `SoldierSort_AdjustHeap` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__adjust_heap` | confirmed (code) |
| `0x004eb348` | `SoldierSort_MakeHeap` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `make_heap` | confirmed (code) |
| `0x004eb3d8` | `SoldierSort_SortHeap` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `sort_heap` | confirmed (code) |
| `0x004eb480` | `SoldierSort_PartialSort` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `partial_sort` (the heap fallback of the introsort) | confirmed (code) |
| `0x004eb540` | `SoldierSort_Partition` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__unguarded_partition` round a pivot | confirmed (code) |
| `0x004eb5e0` | `SoldierSort_IntrosortLoop` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__introsort_loop`: median-of-three quicksort down to runs of 16, heap sort past the depth limit | confirmed (code) |
| `0x004eb728` | `SoldierSort_LinearInsert` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__unguarded_linear_insert` | confirmed (code) |
| `0x004eb778` | `SoldierSort_InsertionSort` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__insertion_sort` | confirmed (code) |
| `0x004eb830` | `SoldierSort_UnguardedInsertion` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__unguarded_insertion_sort` | confirmed (code) |
| `0x004eb880` | `SoldierSort_FinalInsertion` | `std::sort` of pointers by the soldier record's byte `+0x18`, for `RM_SwapSoldier_BuildGroups`: `__final_insertion_sort`: insertion sort of the first 16, unguarded after | confirmed (code) |
| `0x004ecbc0` | `StatLevelSort_PushHeap` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__push_heap` | confirmed (code) |
| `0x004ecc68` | `StatLevelSort_AdjustHeap` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__adjust_heap` | confirmed (code) |
| `0x004ecd58` | `StatLevelSort_MakeHeap` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `make_heap` | confirmed (code) |
| `0x004ecde8` | `StatLevelSort_SortHeap` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `sort_heap` | confirmed (code) |
| `0x004ece90` | `StatLevelSort_PartialSort` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `partial_sort` (the heap fallback of the introsort) | confirmed (code) |
| `0x004ecf88` | `StatLevelSort_Partition` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__unguarded_partition` round a pivot | confirmed (code) |
| `0x004ed068` | `StatLevelSort_IntrosortLoop` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__introsort_loop`: median-of-three quicksort down to runs of 16, heap sort past the depth limit | confirmed (code) |
| `0x004ed1e8` | `StatLevelSort_LinearInsert` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__unguarded_linear_insert` | confirmed (code) |
| `0x004ed250` | `StatLevelSort_InsertionSort` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__insertion_sort` | confirmed (code) |
| `0x004ed338` | `StatLevelSort_UnguardedInsertion` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__unguarded_insertion_sort` | confirmed (code) |
| `0x004ed388` | `StatLevelSort_FinalInsertion` | `std::sort` of pointers by the level table record's byte `+0x0c` (records of `0x84` bytes at game state `+0x14d4`), for `GameStats_CollectLevels`: `__final_insertion_sort`: insertion sort of the first 16, unguarded after | confirmed (code) |

### Files {#files}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004dcb68` | `DS_PS2Device_DeletingDtor` | `DS_PS2Device` (vtable `0x00537c80`) deleting destructor | confirmed (code) |
| `0x004dcbb8` | `FileSys_DeleteObject` | null-safe virtual delete and free (with the allocator's file and line), used by `0x001486c0` | confirmed (code) |
| `0x004dcc38` | `FileSysBase_DeletingDtor` | the file-system base (vtable `0x00537d38`, name at `+0x04`, built at `0x001485c8`) deleting destructor | confirmed (code) |
| `0x004dcc88` | `PS2StreamFileSys_DeletingDtor` | `PS2StreamFileSys` (vtable `0x00537e18`): frees the record pool and free list, then deletes | confirmed (code) |
| `0x004dcf70` | `PS2DbgFile_DeletingDtor` | `PS2DbgFile` (vtable `0x00537ef8`) deleting destructor | confirmed (code) |
| `0x004dcfc0` | `StreamFSFile_DeletingDtor` | the file embedded in `FS_FSToStreamFSFileSys` (vtable `0x00538190`) deleting destructor | confirmed (code) |
| `0x004dd010` | `FSToStreamFS_DeletingDtor` | `FS_FSToStreamFSFileSys` (vtable `0x00538118`) deleting destructor, with its embedded file | confirmed (code) |
| `0x004dd108` | `ReqDeque_CreateNodes` | the file manager's request queue (a `std::deque` of `0x54`-byte requests, six per `0x1f8`-byte node): allocates the nodes for a map range | confirmed (code) |
| `0x004dd1c8` | `ReqDeque_InitMap` | the queue's map (at least 8 node pointers, centred) and first nodes | confirmed (code) |
| `0x004dd340` | `ReqDeque_DestroyNodes` | frees a map range's nodes | confirmed (code) |
| `0x004dd3e0` | `ReqDeque_FreeStorage` | frees all nodes and the map | confirmed (code) |
| `0x004dd468` | `ReqDeque_PopFrontNode` | `pop_front` across a node boundary: frees the empty node and moves to the next | confirmed (code) |
| `0x004dd4f8` | `ReqDeque_ReallocMap` | recentres or grows the map to make room for nodes | confirmed (code) |
| `0x004dd718` | `ReqDeque_PushBackNode` | `push_back` when the last node is full: a new node, then the copy (from `FileManager_Request`) | confirmed (code) |
| `0x004dd9b8` | `FileManager_Destroy` | the file manager's destructor: frees its stream buffer, destroys the request queue (`ReqDeque_Destroy`), `operator delete` when flag 1 ([File I/O](file-io.md)) | confirmed (code) |
| `0x004dda18` | `ReqDeque_Destroy` | destroys the queue's elements and storage (`FileManager_Destroy`) | confirmed (code) |

### Resource manager {#resources}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004de290` | `CharDataRes_DeleteSlot` | null-safe virtual delete of a character-data slot object, with file and line | confirmed (code) |
| `0x004de358` | `CharDataRes_DeleteTable` | null-safe virtual delete of the slot table | confirmed (code) |
| `0x004de3d8` | `Resource_DeleteObject` | null-safe virtual delete of a resource object (models, sheets, texture dictionaries, packs) | confirmed (code) |
| `0x004de458` | `CharDataMap_Find` | `std::map` of the resource manager's character-data map (`+0x40`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004de4c0` | `ResourceMgr_DeleteCharEntry` | null-safe delete of a character entry (its vtable at `+0x08`) | confirmed (code) |
| `0x004de540` | `AnimPackMap_Find` | `std::map` of the animation-pack map (`+0x50`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004de5a8` | `ResourceMgr_DeleteAnimEntry` | null-safe delete of an animation-pack entry | confirmed (code) |
| `0x004de628` | `ResourceBase_DeletingDtor` | deleting destructor of the resource base (vtable `0x005389f8` at `+0x08`, shared by `Model` and `Sheet`): back to the base vtable, `operator delete` when flag 1 | confirmed (code) |
| `0x004de678` | `CharacterDataChunk_DeletingDtor` | `LevelChunk_CharacterData`'s object (vtable `0x005389a0`) deleting destructor: back to the root vtable `0x00534298` | confirmed (code) |
| `0x004de868` | `CarModelMap_Find` | `std::map` of the car-model map (`+0x20`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004de8d0` | `ResMap_LowerBound` | `std::map<uint, T*>` lower bound (key at node `+0x10`), one instance shared by the resource manager's model maps (`ObjectModel_IsLoaded`, `ObjectModel_Request`, `ResourceMgr_CarFits`, `ResourceMgr_CharacterFits`, `ResourceMgr_FindForGroup`; 17 callers) | confirmed (code); which maps inferred |
| `0x004debe8` | `CharModelMap_Find` | `std::map` of the character-model map (`+0x00`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004decd0` | `CharacterInstance_DeletingDtor` | `CharacterInstance` (vtable `0x00538ad0`) deleting destructor: back to the root vtable `0x00534298` | confirmed (code) |
| `0x004df1c0` | `ObjModelMap_Find` | `std::map` of the object-model map (`+0x10`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004df328` | `SheetMap_Find` | `std::map` of the sheet map (`+0x60`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004df430` | `CharDataMap_LowerBound` | `std::map` of the resource manager's character-data map (`+0x40`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004df470` | `CharDataMap_InsertNode` | `std::map` of the resource manager's character-data map (`+0x40`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004df840` | `CharDataMap_InsertUnique` | `std::map` of the resource manager's character-data map (`+0x40`): `insert_unique(value)` | confirmed (code) |
| `0x004df998` | `CharDataMap_InsertHint` | `std::map` of the resource manager's character-data map (`+0x40`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004dfb50` | `ResDeferDeque_CreateNodes` | the resource manager's deferred-request queue (a `std::deque` of 12-byte requests, 42 per `0x1f8`-byte node): allocates the nodes for a map range | confirmed (code) |
| `0x004dfc10` | `ResDeferDeque_InitMap` | the deferred queue's map (at least 8 node pointers, centred) and first nodes, from `ResourceManager_Construct` | confirmed (code) |
| `0x004dfd90` | `RequestMap_Find` | `std::map` of the resource manager's map of requested files (`ResourceMgr_Request`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004dfdf8` | `CharModelMap_LowerBound` | `std::map` of the character-model map (`+0x00`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004dfe38` | `CharModelMap_InsertNode` | `std::map` of the character-model map (`+0x00`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e0208` | `CharModelMap_InsertUnique` | `std::map` of the character-model map (`+0x00`): `insert_unique(value)` | confirmed (code) |
| `0x004e0360` | `CharModelMap_InsertHint` | `std::map` of the character-model map (`+0x00`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e0510` | `TextureMap_LowerBound` | `std::map` of the texture map (`+0x30`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e0550` | `TextureMap_InsertNode` | `std::map` of the texture map (`+0x30`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e0920` | `TextureMap_InsertUnique` | `std::map` of the texture map (`+0x30`): `insert_unique(value)` | confirmed (code) |
| `0x004e0a78` | `TextureMap_InsertHint` | `std::map` of the texture map (`+0x30`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e0c28` | `ObjModelMap_LowerBound` | `std::map` of the object-model map (`+0x10`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e0c68` | `ObjModelMap_InsertNode` | `std::map` of the object-model map (`+0x10`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e1038` | `ObjModelMap_InsertUnique` | `std::map` of the object-model map (`+0x10`): `insert_unique(value)` | confirmed (code) |
| `0x004e1190` | `ObjModelMap_InsertHint` | `std::map` of the object-model map (`+0x10`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e1340` | `CarModelMap_LowerBound` | `std::map` of the car-model map (`+0x20`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e1380` | `CarModelMap_InsertNode` | `std::map` of the car-model map (`+0x20`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e1750` | `CarModelMap_InsertUnique` | `std::map` of the car-model map (`+0x20`): `insert_unique(value)` | confirmed (code) |
| `0x004e18a8` | `CarModelMap_InsertHint` | `std::map` of the car-model map (`+0x20`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e1a58` | `SheetMap_LowerBound` | `std::map` of the sheet map (`+0x60`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e1a98` | `SheetMap_InsertNode` | `std::map` of the sheet map (`+0x60`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e1e68` | `SheetMap_InsertUnique` | `std::map` of the sheet map (`+0x60`): `insert_unique(value)` | confirmed (code) |
| `0x004e1fc0` | `SheetMap_InsertHint` | `std::map` of the sheet map (`+0x60`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e2170` | `AnimPackMap_LowerBound` | `std::map` of the animation-pack map (`+0x50`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e21b0` | `AnimPackMap_InsertNode` | `std::map` of the animation-pack map (`+0x50`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e2580` | `AnimPackMap_InsertUnique` | `std::map` of the animation-pack map (`+0x50`): `insert_unique(value)` | confirmed (code) |
| `0x004e26d8` | `AnimPackMap_InsertHint` | `std::map` of the animation-pack map (`+0x50`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e2888` | `RequestMap_LowerBound` | `std::map` of the resource manager's map of requested files (`ResourceMgr_Request`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e28c8` | `RequestMap_InsertNode` | `std::map` of the resource manager's map of requested files (`ResourceMgr_Request`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e2c98` | `RequestMap_InsertUnique` | `std::map` of the resource manager's map of requested files (`ResourceMgr_Request`): `insert_unique(value)` | confirmed (code) |
| `0x004e2df0` | `RequestMap_InsertHint` | `std::map` of the resource manager's map of requested files (`ResourceMgr_Request`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e2fa0` | `ResDeferDeque_ReallocMap` | recentres or grows the deferred queue's map to make room for nodes | confirmed (code) |
| `0x004e31c0` | `ResDeferDeque_PushBackNode` | `push_back` when the last node is full: a new node, then the copy (from `ResourceMgr_QueueWhenRoom`) | confirmed (code) |
| `0x004e32d8` | `ResDeferDeque_PopFrontNode` | `pop_front` across a node boundary: frees the empty node and moves to the next (`ResourceMgr_ServiceDeferred`, `ResourceMgr_ClearDeferred`) | confirmed (code) |
| `0x004e3368` | `ResourceMgrQueuePaneFar_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `ResourceMgr_QueuePaneFar`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e3508` | `ResourceMgrQueuePaneNear_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `ResourceMgr_QueuePaneNear`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |

### Cameras {#cameras}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004db090` | `ICamera_DefaultFinish` | camera slot `+0x140`: `ICamera_FinishUpdate(1/60)` | confirmed (code) |
| `0x004db0e0` | `ICamera_SetName` | camera slot `+0x188`: copies a 15-character name to `+0x30` | confirmed (code) |
| `0x004db108` | `ICamera_SetFov` | camera slot `+0x190`: `+0x44`, then the view window | confirmed (code) |
| `0x004db128` | `ICamera_SetAspect` | camera slot `+0x198`: `+0x4c`, then the view window | confirmed (code) |
| `0x004db198` | `ICamera_GetFov` | camera slot `+0x1f0`: `+0x44` | confirmed (code) |
| `0x004db1c0` | `Camera_GetViewDistance` | camera slot `+0x210`: `+0x58` | confirmed (code) |
| `0x004db2d8` | `Camera_GetViewFlagGlobal` | returns the global `0x0050b1d8` (read by `ICamera_IsViewEnabled`) | confirmed (code) |
| `0x004db568` | `CamFixed_GetFirstTarget` | `CamFixed` vtable `0x00535a90` slot `+0x268`: resolves entry 0 of the shared camera target list `0x005d91a8` | confirmed (code) |
| `0x004db6a0` | `CamFollow_ResetSlot` | `Cam_Follow` vtable `0x00535d50` slot `+0x138`: `Cam_Follow_Reset(this, 1)` | confirmed (code) |
| `0x004db728` | `CamFollow_ApplyDistances` | moves the leash band by default − band (`+0x308` − `+0x32c`) and steps the zoom to the maximum distance `+0x304` | confirmed (code) |
| `0x004db7b0` | `CamFollow_GetBandNear` | the leash band's near edge `+0x32c` | confirmed (code) |
| `0x004db7d8` | `CamFollow_GetMinDistance` | minimum distance `+0x300` | confirmed (code) |
| `0x004db7e0` | `CamFollow_GetMaxDistance` | maximum distance `+0x304` | confirmed (code) |
| `0x004db7e8` | `CamFollow_GetDefaultDistance` | default distance `+0x308` | confirmed (code) |
| `0x004dbb68` | `CamRail_KeepOffWalls` | keeps the rail camera at least 2 m in front of the rail's side planes around the current segment (`+0x352`), or a given plane by a margin; true when it moved the camera | inferred |
| `0x004dc638` | `CamBlend_ForwardSlot198` | `CamBlend` vtable `0x00537610` slot `+0x198`: forwards to the destination camera (`+0x214`) | confirmed (code) |
| `0x004dc668` | `CamBlend_ForwardSlot1a0` | slot `+0x1a0`: forwards to the destination camera | confirmed (code) |
| `0x004dc698` | `CamBlend_ForwardSlot1a8` | slot `+0x1a8`: forwards to the destination camera | confirmed (code) |
| `0x004dc6c8` | `CamBlend_ForwardSlot1c8` | slot `+0x1c8`: forwards to the destination camera | confirmed (code) |
| `0x004dc6f8` | `CamBlend_SetDrawDistance` | slot `+0x1b0`: its own draw distance, then the destination's | confirmed (code) |
| `0x004dc748` | `CamBlend_ForwardActivate1b8` | slot `+0x1b8`: makes the destination camera active for this blend's player (slot `+0x278`), then forwards | confirmed (code) |
| `0x004dc7c0` | `CamBlend_ForwardActivate1c0` | slot `+0x1c0`: the same for slot `+0x1c0` | confirmed (code) |
| `0x004dc848` | `CamBlend_GetFov` | slot `+0x1f0`: the destination's fov blended with the start fov `+0x224` by the weight `+0x220` | confirmed (code) |
| `0x004dc8a0` | `CamBlend_GetParam228` | slot `+0x200`: the same blend against the start value `+0x228` (not the aspect, which is `0x00143090`, slot `+0x1fc`) | confirmed (code) |
| `0x004dc8f8` | `CamBlend_GetNearClip` | slot `+0x208`: the same blend against `+0x22c` | inferred |

### Animation tasks {#anim}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004da1c8` | `AnimData_DeletingDtor` | `AnimData` (vtable `0x00534240`) slot `+0x08`: back to the base vtable `0x00534298`, `operator delete` when flag 1 | confirmed (code) |
| `0x004da328` | `AnimTaskLoop_DeletingDtor` | `AnimTaskLoop` (vtable `0x005350d0`) deleting destructor | confirmed (code) |
| `0x004da4b8` | `AnimTask5_DeletingDtor` | `AnimTask5` (vtable `0x00534e80`) deleting destructor | confirmed (code) |
| `0x004da558` | `AnimTaskScene_DeletingDtor` | `AnimTaskScene` (vtable `0x00534d58`) deleting destructor | confirmed (code) |
| `0x004da670` | `AnimTaskFourMix_DeletingDtor` | `AnimTaskFourMix` (vtable `0x00534b08`) deleting destructor | confirmed (code) |
| `0x004da700` | `AnimTaskTwoMix_DeletingDtor` | `AnimTaskTwoMix` (vtable `0x005349e0`) deleting destructor | confirmed (code) |
| `0x004da848` | `GaitBlend_DeletingDtor` | `GaitBlend` (vtable `0x00534790`) deleting destructor | confirmed (code) |
| `0x004da8f0` | `AnimTaskPairedOverlay_DeletingDtor` | `AnimTaskPairedOverlay` (vtable `0x00534668`) deleting destructor | confirmed (code) |
| `0x004da990` | `PairedTask_DeletingDtor` | `PairedTask` (vtable `0x00534540`) deleting destructor | confirmed (code) |
| `0x004daa30` | `FaceBlendOut_DeletingDtor` | `FaceBlendOut` (vtable `0x00534418`) deleting destructor | confirmed (code) |
| `0x004daa60` | `FaceTask_DeletingDtor` | `FaceTask` (vtable `0x005342f0`) deleting destructor | confirmed (code) |
| `0x004daac8` | `Cursor_ForwardSlot10` | `Cursor_Take` vtable `0x005351f8` slot `+0x10`: forwards the call to the same slot of the object at `+0x04` | confirmed (code) |

### Game modes and the front end {#frontend}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004ddc60` | `GameMode0e_Dtor` | game mode `0xe` (vtable `0x00538438`) destructor: back to `GameMode_dtor` | confirmed (code) |
| `0x004ddc90` | `GameplayMode_Dtor` | game mode 1, gameplay (vtable `0x00538480`) destructor | confirmed (code) |
| `0x004ddcc0` | `GameMode13_Dtor` | game mode `0x13` (vtable `0x005384c8`): destroys its widget at `+0x20`, then `GameMode_dtor` | confirmed (code) |
| `0x004dddb0` | `LevelFlowMode_Dtor` | game mode 8, level flow (vtable `0x005385c8`) destructor | confirmed (code) |
| `0x004ddde0` | `GameMode0c_Dtor` | game mode `0xc` (vtable `0x00538610`) destructor | confirmed (code) |
| `0x004dde48` | `GameMode10_Dtor` | game mode `0x10` (vtable `0x005386a0`): destroys its mission select at `+0x20` | confirmed (code) |
| `0x004dded8` | `ProfileManagerMode_Dtor` | game mode `0x12`, profile manager (vtable `0x00538730`): destroys its page widget at `+0x30` | confirmed (code) |
| `0x004ddfa0` | `GameMode11_Dtor` | game mode `0x11` (vtable `0x00538828`): destroys its page widget at `+0x30` | confirmed (code) |
| `0x004e4340` | `CreditsAddLine_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `Credits_AddLine`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e45a8` | `CreditList_Delete` | frees the credits' `std::vector` storage (tag `CreditList`) and the vector object (`Credits_Shutdown`) | confirmed (code) |
| `0x004e46b0` | `Credits_DestroyWidget` | `Credits` (vtable `0x00539a10`) slot `+0x60`: `Widget_Destroy` | confirmed (code) |
| `0x004e4868` | `GridContainerAddItem_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `GridContainer_AddItem`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e4a08` | `GridContainer_DestroyWidget` | `GridContainer` (vtable `0x00539d70`) slot `+0x60`: `Widget_Destroy` | confirmed (code) |
| `0x004e4d58` | `MissionSelectAddMission_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `MissionSelect_AddMission`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e4ef8` | `MissionSelectAddArea_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `MissionSelect_AddArea`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e69b0` | `SfcStateList_Clear` | the screen-flow controller's `std::list` of states (tag `SFC_States`): frees every node | confirmed (code) |
| `0x004e6a48` | `SfcTransitionMap_EraseSubtree` | the screen-flow controller's transition map: erases a subtree recursively | confirmed (code) |
| `0x004e6ae0` | `TransitionMap_LowerBound` | `std::map` of the screen-flow controller's transition map: `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e6b20` | `TransitionMap_InsertNode` | `std::map` of the screen-flow controller's transition map: `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e6ef0` | `TransitionMap_InsertUnique` | `std::map` of the screen-flow controller's transition map: `insert_unique(value)` | confirmed (code) |
| `0x004e7048` | `TransitionMap_InsertHint` | `std::map` of the screen-flow controller's transition map: `insert_unique(hint, value)` | confirmed (code) |
| `0x004e71f8` | `SfcStateList_EraseRange` | unlinks and frees the states from one iterator to another (`ScreenFlowController_UnwindTo`) | confirmed (code) |
| `0x004e72c8` | `TransitionMap_Find` | `std::map` of the screen-flow controller's transition map: `find`: the node with the key, or the end | confirmed (code) |
| `0x004e7330` | `SfcStateList_Delete` | clears the state list and frees its head node and the list (`ScreenFlowController_Destroy`) | confirmed (code) |
| `0x004e73d0` | `SfcTransitionMap_Delete` | erases the transition map's tree, frees its header and the map (`ScreenFlowController_Destroy`) | confirmed (code) |
| `0x004e7498` | `SharedValueMap_LowerBound` | `std::map` of the screen-flow controller's shared-value map (`GetShared` / `SetShared`): `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e74d8` | `SharedValueMap_InsertNode` | `std::map` of the screen-flow controller's shared-value map (`GetShared` / `SetShared`): `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e78a8` | `SharedValueMap_InsertUnique` | `std::map` of the screen-flow controller's shared-value map (`GetShared` / `SetShared`): `insert_unique(value)` | confirmed (code) |
| `0x004e7a00` | `SharedValueMap_InsertHint` | `std::map` of the screen-flow controller's shared-value map (`GetShared` / `SetShared`): `insert_unique(hint, value)` | confirmed (code) |
| `0x004e7bb0` | `SharedValueMap_Find` | `std::map` of the screen-flow controller's shared-value map (`GetShared` / `SetShared`): `find`: the node with the key, or the end | confirmed (code) |
| `0x004e8370` | `OptionGridAddItem_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `OptionGrid_AddItem`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e85e0` | `PauseMenu_DestroyWidget` | the pause menu (vtable `0x0053bdc0`, second vtable at `+0x5c`) slot `+0x60`: its stats, control-menu, usage-info and option-grid members and item arrays, then the widget (`ANPauseMenu_Destroy`) | confirmed (code) |
| `0x004e8700` | `PauseMenu_SetQuitTarget` | stores its argument at `+0x56f0` (`PauseMenu_QuitToRumbleQuick`, `0x0041d788`) | confirmed (code); meaning inferred |
| `0x004e8718` | `PauseMenu_SetFlag1b80` | slot `+0xc0` of the pause-menu vtables: sets `+0x1b80` to 1 | confirmed (code) |
| `0x004e8738` | `ScrollingMenuItemMap_EraseSubtree` | the scrolling menu's item map (tag `ScrollingMenuItem`): erases a subtree recursively | confirmed (code) |
| `0x004e8850` | `MenuDescMap_LowerBound` | `std::map` of the scrolling menu's item-description map: `lower_bound` by 32-bit key | confirmed (code) |
| `0x004e8890` | `MenuDescMap_InsertNode` | `std::map` of the scrolling menu's item-description map: `_M_insert`: a new `0x18`-byte node `{colour, parent, left, right, key, value}` linked and rebalanced | confirmed (code) |
| `0x004e8c60` | `MenuDescMap_InsertUnique` | `std::map` of the scrolling menu's item-description map: `insert_unique(value)` | confirmed (code) |
| `0x004e8db8` | `MenuDescMap_InsertHint` | `std::map` of the scrolling menu's item-description map: `insert_unique(hint, value)` | confirmed (code) |
| `0x004e8f68` | `ScrollingMenuAddItem_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `ScrollingMenu_AddItem`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e9108` | `MenuDescMap_Find` | `std::map` of the scrolling menu's item-description map: `find`: the node with the key, or the end | confirmed (code) |
| `0x004e91c0` | `ScrollingMenuItem_SetFlag80` | `ScrollingMenuItem` slot `+0xb8`: stores the byte at `+0x80` and passes it to its widget at `+0x60` through the same slot | confirmed (code) |
| `0x004e93d0` | `RMSwapSoldierAddToGroup_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `RM_SwapSoldier_AddToGroup`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e9700` | `ProfileSetWidget_DeletingDtor` | deleting destructor of the profile pages' set holder (vtable `0x0053cc88` at `+0x18`; `PM_Profile`, `RM_No2ndController`): erases the `std::set` at `+0x08` and frees its header | confirmed (code) |
| `0x004e97c0` | `RMChooseAreaAddArena_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `RM_ChooseArea_AddArena`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e9a28` | `RmArenaVector_Delete` | frees the choose-area page's vector storage and the vector (`RM_ChooseArea_Shutdown`) | confirmed (code) |
| `0x004e9ac0` | `RMChooseGangsAddGang_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `RM_ChooseGangs_AddGang`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e9d98` | `RmGangVector_Delete` | frees the choose-gangs page's vector storage and the vector (`RM_ChooseGangs_Shutdown`) | confirmed (code) |
| `0x004e9e30` | `RmChooseGangsCb_Invoke` | slot `+0x08` of vtable `0x0053cec8` (`RM_ChooseGangs_Init`): calls slot `+0x0c` of the object at `+0x20` (its vtable at `+0x1c`) | confirmed (code) |
| `0x004e9e60` | `RmCharDataMap_LowerBound` | `std::map<u16, T>` (key at node `+0x10`) of the rumble character data: lower bound | confirmed (code) |
| `0x004e9ea0` | `RmCharDataMap_InsertNode` | that map's node insert and rebalance | confirmed (code) |
| `0x004ea270` | `RmCharDataMap_InsertUnique` | that map's unique insert (`RM_AddCharData`, `RM_EditGang_SelectSlot`, `RM_SwapSoldier_*`) | confirmed (code) |
| `0x004ea3c8` | `RmCharDataMap_InsertHint` | that map's insert with a hint (`operator[]`) | confirmed (code) |
| `0x004ea578` | `RmControllerMap_EraseSubtree` | the rumble controller's map: erases a subtree recursively | confirmed (code) |
| `0x004eab58` | `RmControllerMap_Delete` | erases that map's tree, frees its header and the map (`RM_Controller_Shutdown`) | confirmed (code) |
| `0x004ead18` | `RMGameModeAddMode_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `RM_GameMode_AddMode`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004eafa8` | `RmModeVector_Delete` | frees the game-mode page's vector storage and the vector (`RM_GameMode_Shutdown`) | confirmed (code) |
| `0x004eb070` | `RMSwapSoldierAddGroup_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `RM_SwapSoldier_AddGroup`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004eb8f0` | `RmSoldierGroupVector_Delete` | frees a swap-soldier vector (storage at `+0x08`) and the vector (`RM_SwapSoldier_Shutdown`) | confirmed (code) |
| `0x004eb988` | `RmSoldierVector_Delete` | frees a swap-soldier vector (storage at `+0x04`) and the vector (`RM_SwapSoldier_Shutdown`) | confirmed (code) |
| `0x004eca20` | `GameStatsAddLevel_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `GameStats_AddLevel`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004ed440` | `GsMissionList_Delete` | frees the game stats' mission-list vector (tag `GS_MissionList`) and the vector (`GameStats_Shutdown`) | confirmed (code) |
| `0x004ed4d8` | `GameStatsPage_DestroyWidget` | the game-stats page widget (vtable `0x0053eec0`, second vtable `0x0053ee98` at `+0x5c`) slot `+0x60`: `Widget_Destroy` | confirmed (code) |

### HUD and widgets {#hud}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004ddf60` | `Widget_DestroyThunk` | `OptionGrid` vtable `0x00538778` slot `+0x60`: `Widget_Destroy` | confirmed (code) |
| `0x004ddf80` | `Widget_SetFlag40` | sets `+0x40` to 1; slot `+0x90` of 50 widget vtables (inferred: marks the widget for a redraw) | inferred |
| `0x004e3a48` | `OverlayEffectAddWidget_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `OverlayEffect_AddWidget`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e3d90` | `Widget_SetOpacity` | slot `+0xa8` of 14 widget vtables: stores the value clamped to 0-1 at `+0x50` | confirmed (code); opacity inferred |
| `0x004e3dc8` | `ActionPrompt_DestroyWidget` | `ActionPrompt` (vtable `0x00539270`) slot `+0x60`: its base widget at `+0x480`, then `BoxedText_Destroy` | confirmed (code) |
| `0x004e3e10` | `HudCrewStatus_DestroyWidget` | the HUD's crew-status widget (vtable `0x00539408`) slot `+0x60`: `CrewStatusEntry_Shutdown`, two base widgets, `Widget_Destroy` | confirmed (code) |
| `0x004e3e70` | `HUD_DestroyWidget` | the HUD (vtable `0x00539370`) slot `+0x60`: its message HUD at `+0x13b0`, its array of `0x250`-byte items `+0x70`-`+0x12f0` (each item's slot `+0x60`), then `Widget_Destroy` | confirmed (code) |
| `0x004e4068` | `ChecklistList_Clear` | the checklist's `std::list` of text items: frees every node (allocator tag `TextItemContainer`) | confirmed (code) |
| `0x004e4100` | `ChecklistList_Delete` | clears the checklist's list, frees its head node and the list object (`Checklist_Shutdown`) | confirmed (code) |
| `0x004e4248` | `HudCrimePanelPart_DeletingDtor` | the crime panel's sub-object (vtable `0x005398c8` at `+0x100`, `HudCrimePanel_Setup`) deleting destructor | confirmed (code) |
| `0x004e42f0` | `Widget_ReturnMinus1` | slot `+0x28` of 100 widget vtables: returns -1 | confirmed (code) |
| `0x004e4a58` | `HudLabel_DestroyWidget` | `HudLabel` (vtable `0x00539e38`) slot `+0x60`: `MessageHUD_Destroy` | confirmed (code) |
| `0x004e4af8` | `HudItemRow_DestroyWidget` | the HUD's widget at `+0x16b30` (vtable `0x00539f38`) slot `+0x60`: its nine `0x100`-byte items, two base widgets and `Widget_Destroy` | confirmed (code); what it shows open |
| `0x004e4c20` | `MessageHUD_MeasureSlot` | slot `+0x70` of ten message-HUD vtables: `MessageHUD_Measure(widget, out, 0)` | confirmed (code) |
| `0x004e4c78` | `MessageHUD_SetColourSlot` | slot `+0x20` of ten message-HUD vtables: `MessageHUD_SetColour` with the first word of the argument | confirmed (code) |
| `0x004e7c50` | `ScrollInList_Clear` | the scroll-in's `std::list<QueueElement*>`: frees every node | confirmed (code) |
| `0x004e7ce8` | `ScrollInList_Delete` | clears the scroll-in list and frees its head node and the list (`ScrollIn_Shutdown`) | confirmed (code) |
| `0x004e7d88` | `ScrollInQueue_Delete` | frees a `Queue<QueueElement*>`'s buffer and the queue (`ScrollIn_Shutdown`) | confirmed (code) |
| `0x004e7e28` | `ScrollInList_Remove` | unlinks and frees every node whose value equals the argument (`ScrollIn_Flush`) | confirmed (code) |
| `0x004e7ef0` | `HudMenuGrid_DestroyWidget` | the HUD's menu grid (vtable `0x0053abe8`, second vtable `0x0053abc0` at `+0x6c`) slot `+0x60`: `Widget_Destroy` | confirmed (code) |
| `0x004e8040` | `StopWatchHud_DestroyWidget` | `StopWatchHud` (vtable `0x0053aef0`) slot `+0x60`: `MessageHUD_Destroy` | confirmed (code) |
| `0x004e8068` | `TutorialQueueList_Clear` | the hint box's `std::list` (tag `TutorialItemPQueue`): frees every node | confirmed (code) |
| `0x004e8100` | `TutorialQueueList_Delete` | clears that list and frees its head node and the list (`HintBox_Shutdown`) | confirmed (code) |
| `0x004e81a0` | `HintBoxQueue_Delete` | frees the hint box's `Queue<QueueElement*>` buffer and the queue (`HintBox_Shutdown`) | confirmed (code) |
| `0x004eb040` | `HudPanel53d440_DestroyWidget` | the HUD's widget at `+0xe530` (vtable `0x0053d440`, second vtable `0x0053d418` at `+0x5c`) slot `+0x60`: `Widget_Destroy` | confirmed (code) |
| `0x004ec4b0` | `ANHudPanel1_DestroyWidget` | one of `ANHud`'s four text panels (vtable `0x0053ec28`, base `0x00539fd0`) slot `+0x60`: base widget, message HUD, text widget, `Widget_Destroy` | confirmed (code) |
| `0x004ec518` | `ANHudPanel2_DestroyWidget` | the same for vtable `0x0053eb90` | confirmed (code) |
| `0x004ec580` | `ANHudPanel3_DestroyWidget` | the same for vtable `0x0053eaf8` | confirmed (code) |
| `0x004ec5e8` | `ANHudPanel4_DestroyWidget` | the same for vtable `0x0053ea60` | confirmed (code) |
| `0x004ec660` | `PlayerHUD_DeletingDtor` | `PlayerHUD` (vtable `0x0053edf8` at `+0x4120`) deleting destructor: its item arrays, widgets, `WarCommandDisplay` and the four text panels, then the `ANHud` base (`0x0053e9c0`); no direct callers | confirmed (code) |

### Statistics {#stats}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004de250` | `Stats_AddHarmony` | adds n to a harmony event of the human's live stats record: `Stats_GetPlayer` (`0x004228a0`) then `PlayerStats_AddHarmony` (`0x004211a0`); for arrests, knock-outs and the cuffed-or-down tally | confirmed (code) |
| `0x004ed888` | `Stats_AddBonus` | `PlayerStats_AddBonus` on the human's live record (`Stats_GetPlayer`); lock picks, tags, `StatAdd` | confirmed (code) |
| `0x004ed8c8` | `Stats_AddCombat` | `PlayerStats_AddCombat` on the human's live record (27 callers: grabs, hits, defeats) | confirmed (code) |
| `0x004ed908` | `Stats_AddMission` | `PlayerStats_AddMission` on the human's live record | confirmed (code) |
| `0x004ed948` | `Stats_AddCrime` | `PlayerStats_AddCrime` on the human's live record (car hits and explosions, reported crimes) | confirmed (code) |
| `0x004ed988` | `Stats_AddStyle` | `PlayerStats_AddStyle` on the human's live record | confirmed (code) |
| `0x004ed9c8` | `Stats_CombatPoints` | `n ×` the combat points table (`0x00715500`) entry of an event (`Hit_Score`) | confirmed (code) |
| `0x004eda18` | `Stats_StylePoints` | `n ×` the style points table (`0x00715510`) entry of an event (`Hit_Score`) | confirmed (code) |
| `0x004f39e8` | `Stats_AddCrimeAllPlayers` | `PlayerStats_AddCrime` on every live player's record (game state `+0x228`, count `+0x224`): vandalism and thrown-object crimes | confirmed (code) |

### AI: tactics, sub-tactics, brains and filters {#ai}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004edd50` | `Brain_ConstructMembers` | constructs a brain's steering (`+0xa0`) and route (`+0xe0`) and clears its handles (`+0x124`, `+0x128`, the 16 at `+0x164` and `+0x1a4`, `+0x260`, `+0x294`); `AI_ResetPools` | confirmed (code) |
| `0x004edec8` | `ObjFilterHasFlags_DeletingDtor` | object filter (vtable `0x0053f2d0`, test `ObjFilter_HasFlags`) deleting destructor: back to the filter base `0x0053f2f0` | confirmed (code) |
| `0x004edef8` | `ObjFilterFetch_DeletingDtor` | object filter `0x0053f2b0` (test `0x0029d378`, `WarriorVandalStealGoal_TryFetch`) deleting destructor | confirmed (code) |
| `0x004edf28` | `ObjFilterHasName_DeletingDtor` | object filter `0x0053f290` (`ObjFilter_HasName`) deleting destructor | confirmed (code) |
| `0x004edf58` | `ObjFilterNearestWithFlags_DeletingDtor` | object filter `0x0053f270` (`ObjFilter_NearestWithFlags`) deleting destructor | confirmed (code) |
| `0x004edf88` | `ObjFilterVandalisable_DeletingDtor` | object filter `0x0053f250` (`ObjFilter_IsVandalisable`) deleting destructor | confirmed (code) |
| `0x004edff0` | `ObjFilter_DeletingDtor` | the object filter base (vtable `0x0053f2f0`) deleting destructor | confirmed (code) |
| `0x004ef2a8` | `TacticType1a_DeletingDtor` | tactic vtable `0x005432c0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef2d8` | `TacticType1a_GetType` | slot `+0x30`: the tactic type, `0x1a` | confirmed (code) |
| `0x004ef2e0` | `TacticType1a_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef318` | `TacticAttack_DeletingDtor` | tactic vtable `0x00543320` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef348` | `TacticAttack_GetType` | slot `+0x30`: the tactic type, `0x00` | confirmed (code) |
| `0x004ef350` | `TacticAttack_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef380` | `TacticAvoidEnemies_DeletingDtor` | tactic vtable `0x00543380` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef3b0` | `TacticAvoidEnemies_GetType` | slot `+0x30`: the tactic type, `0x20` | confirmed (code) |
| `0x004ef3b8` | `TacticAvoidEnemies_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef3e8` | `TacticType09_DeletingDtor` | tactic vtable `0x005433e0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef418` | `TacticType09_GetType` | slot `+0x30`: the tactic type, `0x09` | confirmed (code) |
| `0x004ef420` | `TacticType09_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef450` | `TacticBossDiegoVargas_DeletingDtor` | tactic vtable `0x00543440` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef480` | `TacticBossDiegoVargas_GetType` | slot `+0x30`: the tactic type, `0x0a` | confirmed (code) |
| `0x004ef488` | `TacticBossDiegoVargas_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef4b8` | `TacticType0b_DeletingDtor` | tactic vtable `0x005434a0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef4e8` | `TacticType0b_GetType` | slot `+0x30`: the tactic type, `0x0b` | confirmed (code) |
| `0x004ef4f0` | `TacticType0b_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef520` | `TacticBossMoe_DeletingDtor` | tactic vtable `0x00543500` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef550` | `TacticBossMoe_GetType` | slot `+0x30`: the tactic type, `0x0c` | confirmed (code) |
| `0x004ef558` | `TacticBossMoe_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef588` | `TacticBossRoof_DeletingDtor` | tactic vtable `0x00543560` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef5b8` | `TacticBossRoof_GetType` | slot `+0x30`: the tactic type, `0x0d` | confirmed (code) |
| `0x004ef5c0` | `TacticBossRoof_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef5f0` | `TacticBossLuther_DeletingDtor` | tactic vtable `0x005435c0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef620` | `TacticBossLuther_GetType` | slot `+0x30`: the tactic type, `0x0e` | confirmed (code) |
| `0x004ef628` | `TacticBossLuther_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef658` | `TacticBossScenarioF_DeletingDtor` | tactic vtable `0x00543620` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef688` | `TacticBossScenarioF_GetType` | slot `+0x30`: the tactic type, `0x0f` | confirmed (code) |
| `0x004ef690` | `TacticBossScenarioF_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef6c0` | `TacticBossVirgil_DeletingDtor` | tactic vtable `0x00543680` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef6f0` | `TacticBossVirgil_GetType` | slot `+0x30`: the tactic type, `0x10` | confirmed (code) |
| `0x004ef6f8` | `TacticBossVirgil_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef728` | `TacticBossChatterbox_DeletingDtor` | tactic vtable `0x005436e0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef758` | `TacticBossChatterbox_GetType` | slot `+0x30`: the tactic type, `0x11` | confirmed (code) |
| `0x004ef760` | `TacticBossChatterbox_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef790` | `TacticConfront_DeletingDtor` | tactic vtable `0x00543740` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef7c0` | `TacticConfront_GetType` | slot `+0x30`: the tactic type, `0x23` | confirmed (code) |
| `0x004ef7c8` | `TacticConfront_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef7f8` | `TacticCrowd_DeletingDtor` | tactic vtable `0x005437a0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef828` | `TacticCrowd_GetType` | slot `+0x30`: the tactic type, `0x1b` | confirmed (code) |
| `0x004ef830` | `TacticCrowd_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef868` | `TacticDefend_DeletingDtor` | tactic vtable `0x00543800` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef8a0` | `TacticDefend_GetType` | slot `+0x30`: the tactic type, `0x02` | confirmed (code) |
| `0x004ef8a8` | `TacticDefend_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef8d8` | `TacticDomination_DeletingDtor` | tactic vtable `0x00543860` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef908` | `TacticDomination_GetType` | slot `+0x30`: the tactic type, `0x07` | confirmed (code) |
| `0x004ef910` | `TacticDomination_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef940` | `TacticWarriorFollow_DeletingDtor` | tactic vtable `0x005438c0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef970` | `TacticWarriorFollow_GetType` | slot `+0x30`: the tactic type, `0x12` | confirmed (code) |
| `0x004ef978` | `TacticWarriorFollow_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004ef9a8` | `TacticHanginOut_DeletingDtor` | tactic vtable `0x00543920` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004ef9e0` | `TacticHanginOut_GetType` | slot `+0x30`: the tactic type, `0x18` | confirmed (code) |
| `0x004ef9e8` | `TacticHanginOut_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efa18` | `TacticType13_DeletingDtor` | tactic vtable `0x00543980` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efa48` | `TacticType13_GetType` | slot `+0x30`: the tactic type, `0x13` | confirmed (code) |
| `0x004efa50` | `TacticType13_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efa80` | `TacticPlayerGang_DeletingDtor` | tactic vtable `0x005439e0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efab0` | `TacticPlayerGang_GetType` | slot `+0x30`: the tactic type, `0x03` | confirmed (code) |
| `0x004efab8` | `TacticPlayerGang_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efae8` | `TacticHoldTheLine_DeletingDtor` | tactic vtable `0x00543a40` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efb28` | `TacticHoldTheLine_GetType` | slot `+0x30`: the tactic type, `0x04` | confirmed (code) |
| `0x004efb30` | `TacticHoldTheLine_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efb60` | `TacticIdle_DeletingDtor` | tactic vtable `0x00543aa0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efb90` | `TacticIdle_GetType` | slot `+0x30`: the tactic type, `0x24` | confirmed (code) |
| `0x004efb98` | `TacticIdle_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efbd0` | `TacticType1f_DeletingDtor` | tactic vtable `0x00543b00` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efc00` | `TacticType1f_GetType` | slot `+0x30`: the tactic type, `0x1f` | confirmed (code) |
| `0x004efc08` | `TacticType1f_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efc40` | `TacticManWeaponPile_DeletingDtor` | tactic vtable `0x00543b60` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efc70` | `TacticManWeaponPile_GetType` | slot `+0x30`: the tactic type, `0x06` | confirmed (code) |
| `0x004efc78` | `TacticManWeaponPile_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efca8` | `TacticMoveToFlag_DeletingDtor` | tactic vtable `0x00543bc0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efcd8` | `TacticMoveToFlag_GetType` | slot `+0x30`: the tactic type, `0x19` | confirmed (code) |
| `0x004efce0` | `TacticMoveToFlag_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efd10` | `TacticPursue_DeletingDtor` | tactic vtable `0x00543c20` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efd48` | `TacticPursue_GetType` | slot `+0x30`: the tactic type, `0x14` | confirmed (code) |
| `0x004efd50` | `TacticPursue_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efd80` | `TacticType08_DeletingDtor` | tactic vtable `0x00543c80` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efdb0` | `TacticType08_GetType` | slot `+0x30`: the tactic type, `0x08` | confirmed (code) |
| `0x004efdb8` | `TacticType08_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efde8` | `TacticType05_DeletingDtor` | tactic vtable `0x00543ce0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efe18` | `TacticType05_GetType` | slot `+0x30`: the tactic type, `0x05` | confirmed (code) |
| `0x004efe20` | `TacticType05_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efe58` | `TacticType25_DeletingDtor` | tactic vtable `0x00543d40` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efe88` | `TacticType25_GetType` | slot `+0x30`: the tactic type, `0x25` | confirmed (code) |
| `0x004efe90` | `TacticType25_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004efec0` | `TacticScout_DeletingDtor` | tactic vtable `0x00543da0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004efef0` | `TacticScout_GetType` | slot `+0x30`: the tactic type, `0x27` | confirmed (code) |
| `0x004efef8` | `TacticScout_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004eff28` | `TacticType1e_DeletingDtor` | tactic vtable `0x00543e00` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004eff58` | `TacticType1e_GetType` | slot `+0x30`: the tactic type, `0x1e` | confirmed (code) |
| `0x004eff60` | `TacticType1e_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004eff90` | `TacticStandGround_DeletingDtor` | tactic vtable `0x00543e60` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004effc0` | `TacticStandGround_GetType` | slot `+0x30`: the tactic type, `0x22` | confirmed (code) |
| `0x004effc8` | `TacticStandGround_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0000` | `TacticSteal_DeletingDtor` | tactic vtable `0x00543ec0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0030` | `TacticSteal_GetType` | slot `+0x30`: the tactic type, `0x1d` | confirmed (code) |
| `0x004f0038` | `TacticSteal_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0068` | `TacticType28_DeletingDtor` | tactic vtable `0x00543f20` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0098` | `TacticType28_GetType` | slot `+0x30`: the tactic type, `0x28` | confirmed (code) |
| `0x004f00a0` | `TacticType28_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f00d0` | `TacticTravelPath_DeletingDtor` | tactic vtable `0x00543f80` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0100` | `TacticTravelPath_GetType` | slot `+0x30`: the tactic type, `0x17` | confirmed (code) |
| `0x004f0108` | `TacticTravelPath_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0138` | `TacticUseFlag_DeletingDtor` | tactic vtable `0x00543fe0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0168` | `TacticUseFlag_GetType` | slot `+0x30`: the tactic type, `0x21` | confirmed (code) |
| `0x004f0170` | `TacticUseFlag_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f01a0` | `TacticVandalize_DeletingDtor` | tactic vtable `0x00544040` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f01d0` | `TacticVandalize_GetType` | slot `+0x30`: the tactic type, `0x1c` | confirmed (code) |
| `0x004f01d8` | `TacticVandalize_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0208` | `TacticWalkinTall_DeletingDtor` | tactic vtable `0x005440a0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0238` | `TacticWalkinTall_GetType` | slot `+0x30`: the tactic type, `0x15` | confirmed (code) |
| `0x004f0240` | `TacticWalkinTall_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0270` | `TacticWander_DeletingDtor` | tactic vtable `0x00544100` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f02a0` | `TacticWander_GetType` | slot `+0x30`: the tactic type, `0x16` | confirmed (code) |
| `0x004f02a8` | `TacticWander_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f02d8` | `TacticWarriorAttack_DeletingDtor` | tactic vtable `0x00544160` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0308` | `TacticWarriorAttack_GetType` | slot `+0x30`: the tactic type, `0x01` | confirmed (code) |
| `0x004f0310` | `TacticWarriorAttack_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f0340` | `TacticType26_DeletingDtor` | tactic vtable `0x005441c0` slot `+0x28` (vtable pointer at `+0x1c`): back to the base tactic vtable `0x00543260`, `operator delete` when flag 1 | confirmed (code) |
| `0x004f0370` | `TacticType26_GetType` | slot `+0x30`: the tactic type, `0x26` | confirmed (code) |
| `0x004f0378` | `TacticType26_IsCombatType` | slot `+0x38`: type below `0x12` (the fighting and boss tactics, as against the crowd, travel and idle ones from `0x12` up) | confirmed (code); meaning inferred |
| `0x004f03a8` | `GrabSubTactic_DeletingDtor` | `GrabSubTactic` (vtable `0x00544220`, vptr at `+0x0c`) deleting destructor: back to the sub-tactic base `0x00544258` | confirmed (code) |
| `0x004f03f0` | `SubTactic_DeletingDtor` | the sub-tactic base (vtable `0x00544258`) deleting destructor | confirmed (code) |
| `0x004f0440` | `ChargeSubTactic_DeletingDtor` | `ChargeSubTactic` (vtable `0x00544290`) deleting destructor | confirmed (code) |
| `0x004f0478` | `GuardSubTactic_DeletingDtor` | `GuardSubTactic` (vtable `0x005442c8`) deleting destructor | confirmed (code) |
| `0x004f04b0` | `LeaderSubTactic_DeletingDtor` | `LeaderSubTactic` (vtable `0x00544300`) deleting destructor | confirmed (code) |
| `0x004f04e8` | `ThrowSubTactic_DeletingDtor` | `ThrowSubTactic` (vtable `0x00544338`) deleting destructor | confirmed (code) |

### Timers {#timers}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004dc958` | `GameTimer_DeletingDtor` | `GameTimer` (vtable `0x00537bc0`) deleting destructor; also resets the embedded `Timer`'s vtable | confirmed (code) |
| `0x004dc9b0` | `GameTimer_GetSeconds` | slot `+0x38`: game milliseconds × 0.001 | confirmed (code) |
| `0x004dca60` | `GameTimer_MsSince` | slot `+0x48`: ticks since a count ÷ 294,912 | confirmed (code) |
| `0x004dcaa0` | `GameTimer_SecondsSince` | slot `+0x50`: milliseconds since × 0.001 | confirmed (code) |
| `0x004dcdd8` | `Timer_GetMilliseconds` | `Timer` (vtable `0x00537e98`) slot `+0x30`: ticks ÷ 294,912 | confirmed (code) |
| `0x004dce18` | `Timer_GetSeconds` | slot `+0x38`: milliseconds ÷ 1000, whole seconds | confirmed (code) |
| `0x004dce88` | `Timer_TicksSince` | slot `+0x40`: ticks − a count | confirmed (code) |
| `0x004dcec0` | `Timer_MsSince` | slot `+0x48`: ticks since ÷ 294,912 | confirmed (code) |
| `0x004dcf00` | `Timer_SecondsSince` | slot `+0x50`: ticks since × 3.390842e-9 | confirmed (code) |

### Task pools and tasks {#task-pools}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004f2a70` | `CarTaskPool_New` | car task free list slot `+0x10`: takes a slot and constructs a car task in it (`CarTask_Init`) | confirmed (code) |
| `0x004f2ae0` | `CarTaskPool_Alloc` | slot `+0x18`: takes a slot (null when the pool is empty) | confirmed (code) |
| `0x004f2b40` | `CarTaskPool_Delete` | slot `+0x20`: destroys the task (its slot `+0x08`, flag 2) and returns the slot (slot `+0x28`) | confirmed (code) |
| `0x004f2bd8` | `GlassTask_DeletingDtor` | glass task (vtable `0x00544ed0`) deleting destructor | confirmed (code) |
| `0x004f2c80` | `GlassTaskPool_DeletingDtor` | the glass tasks' `FreeListContainer` (vtable `0x005450d8`) deleting destructor: frees its arrays | confirmed (code) |
| `0x004f2dd8` | `GlassTaskPool_New` | slot `+0x10`: takes a slot and constructs a glass task (vtable, `+0xe0` = -1) | confirmed (code) |
| `0x004f2e50` | `GlassTaskPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f2eb0` | `GlassTaskPool_Delete` | slot `+0x20`: destroys the task and returns its slot | confirmed (code) |
| `0x004f2f48` | `LightTask_DeletingDtor` | light task (vtable `0x00545138`) deleting destructor | confirmed (code) |
| `0x004f3000` | `LightTaskPool_DeletingDtor` | the light tasks' free list (vtable `0x00545340`) deleting destructor | confirmed (code) |
| `0x004f3158` | `LightTaskPool_New` | slot `+0x10`: takes a slot and constructs a light task | confirmed (code) |
| `0x004f31c8` | `LightTaskPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f3228` | `LightTaskPool_Delete` | slot `+0x20`: destroys the task and returns its slot | confirmed (code) |
| `0x004f3a98` | `ObjectTask_DeletingDtor` | object task (vtable `0x005453a0`) deleting destructor | confirmed (code) |
| `0x004f3b40` | `ObjectTask_GetPosition` | slot `+0x60`: the position of the object (its slot `+0xa8`) | confirmed (code) |
| `0x004f3d40` | `ObjectTaskPool_DeletingDtor` | the object tasks' free list (vtable `0x00545600`) deleting destructor | confirmed (code) |
| `0x004f3e98` | `ObjectTaskPool_New` | slot `+0x10`: takes a slot and constructs an object task (`+0xf8`, `+0xe4` = -1) | confirmed (code) |
| `0x004f3f18` | `ObjectTaskPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f3f78` | `ObjectTaskPool_Delete` | slot `+0x20`: destroys the task and returns its slot | confirmed (code) |
| `0x004f4010` | `ParticleTask_GetFlag10` | the particle task's byte `+0x10` (`ParticleTask_Draw`) | confirmed (code) |
| `0x004f4020` | `ParticleTask_GetFlag11` | the particle task's byte `+0x11` (`ParticleTask_Draw`) | confirmed (code) |
| `0x004f4030` | `ParticleTask_DeletingDtor` | particle task (vtable `0x00545660`) deleting destructor | confirmed (code) |
| `0x004f4098` | `ParticleTask_GetPosition` | slot `+0x60`: the position of the object (its slot `+0xa8`) | confirmed (code) |
| `0x004f4158` | `ParticleTaskPool_DeletingDtor` | the particle tasks' free list (vtable `0x00545868`) deleting destructor | confirmed (code) |
| `0x004f42b0` | `ParticleTaskPool_New` | slot `+0x10`: takes a slot and constructs a particle task (`+0xd4` = -1) | confirmed (code) |
| `0x004f4328` | `ParticleTaskPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f4388` | `ParticleTaskPool_Delete` | slot `+0x20`: destroys the task and returns its slot | confirmed (code) |
| `0x004f4420` | `SceneTask_DeletingDtor` | scene task (vtable `0x005458c8`) deleting destructor | confirmed (code) |
| `0x004f44b8` | `SceneTaskPool_DeletingDtor` | the scene tasks' free list (vtable `0x00545ad0`) deleting destructor | confirmed (code) |
| `0x004f4610` | `SceneTaskPool_New` | slot `+0x10`: takes a slot and constructs a scene task | confirmed (code) |
| `0x004f4680` | `SceneTaskPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f46e0` | `SceneTaskPool_Delete` | slot `+0x20`: destroys the task and returns its slot | confirmed (code) |

### Scripting and messages {#scripting}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004e3770` | `MsgArgs_Init` | a message's argument stack (64 slots of 16 bytes, on the caller's stack): the base at `+0x404`, the top at `+0x400` one slot before the first; 112 callers build one before a message and read it back with `Msg_PopArg` | confirmed (code) |
| `0x004ed738` | `ScriptHost_SetSlotExThunk` | vtable `0x0053f018` slot `+0x18` (this adjusted by `-0x70`): `ScriptHandler_SetSlotEx` on the handler at `+0xe8` | confirmed (code); owning class open |
| `0x004ed758` | `ScriptHost_GetSlotThunk` | the same vtable's slot `+0x20`: `ScriptHandler_GetSlot` on `+0xe8` | confirmed (code) |
| `0x004ed778` | `ScriptHost_SetOwnerThunk` | the same vtable's slot `+0x38`: `ScriptHandler_SetOwner` on `+0xe8` | confirmed (code) |
| `0x004f14d0` | `SchedHeap_PushUp` | the script scheduler's heap of 24-byte calls (key: the u32 due time at `+0x00`): moves an entry up while it is due before its parent | confirmed (code) |
| `0x004f1610` | `SchedHeap_AdjustDown` | the same heap: sifts an entry down after the front is removed (`ScriptSystem_Update`, `ScriptSystem_FlushScheduled`) | confirmed (code) |
| `0x004f1758` | `SchedCalls_UninitCopy` | copies 24-byte scheduled calls into raw storage | confirmed (code) |
| `0x004f17b0` | `SchedCalls_InsertAux` | the scheduled calls' `std::vector` insert / grow (`ScriptSystem_ScheduleCall*`) | confirmed (code) |
| `0x004f1a70` | `SchedCalls_UninitCopy2` | a second instance of the 24-byte raw copy | confirmed (code) |
| `0x004f1ac8` | `SchedCalls_Assign` | the scheduled calls' vector assignment (`ScriptSystem_FlushScheduled`) | confirmed (code) |
| `0x004f1d80` | `ScriptSystemPart_DeletingDtor` | the script system's member (vtable `0x00544aa8`, `ScriptSystem_Destruct`) deleting destructor | confirmed (code) |
| `0x004f1db8` | `ScriptObjectFreeList_Delete` | frees a `FreeList<ScriptObject>`'s two arrays and the list (`ScriptHandlerPool_Destroy`) | confirmed (code) |

### World, objects and boxes {#world}

| Address | Name | What it does | Evidence |
| --- | --- | --- | --- |
| `0x004da210` | `BaseObject_DeletingDtor` | the root vtable `0x00534298`'s slot `+0x08`: the same for the base class every link-once destructor ends in | confirmed (code) |
| `0x004dab50` | `GameState_GetLevelIndex` | game state `+0x56dc`, the current level's index | confirmed (code) |
| `0x004dab58` | `GameState_GetLevelTable` | game state `+0x14d4`, the level table (records of `0x84` bytes, id at `+0x04`) | confirmed (code) |
| `0x004dab68` | `Object_DeleteVirtual` | calls an object's destructor (slot `+0x08`, flag 2) and frees it | confirmed (code) |
| `0x004dac20` | `TaskObject_OnContactNone` | the base contact handler (owner vtable slot `+0xf8`) kept by the glass, light, particle and scene tasks: returns 0 ([Physics](physics.md#contacts)) | confirmed (code) |
| `0x004de6c8` | `CarPartVisibilityCallback_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `CarPart_VisibilityCallback`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004dea48` | `HumanAtomicVisibilityCallback_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `HumanAtomic_VisibilityCallback`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004dee58` | `LightManagerCullForViewport_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `LightManager_CullForViewport`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004df008` | `Renderable_SetParam24` | sets `+0x24` and, when there is a child at `+0x0c`, the child's `+0x20` to the same value; from `Human_RenderWithAttachments`, `Human_ResetRenderState` and `Humans_Update` | confirmed (code); meaning open |
| `0x004df020` | `ObjectAtomicVisibilityCallback_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `ObjectAtomic_VisibilityCallback`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004e3820` | `RwDevice_DeletingDtorCopy` | an unreferenced copy of the render device's deleting destructor (vtable `0x00538d78`, `RwDevice_Construct`): its three cameras and two arrays of `0x60`-byte children; the vtable's own slot `+0x08` is `0x00194558` | confirmed (code) |
| `0x004f0fa8` | `IPhysicsAddBody_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `IPhysics_AddBody`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004f12a8` | `CollisionMesh_DeletingDtor` | `CollisionMesh` (vtable `0x005448b0`) deleting destructor: `CollisionMesh_FreeChunks` | confirmed (code) |
| `0x004f13e8` | `WarMoveInstance_ForwardSlot10` | `WarMoveInstance` (vtable `0x00544960`) slot `+0x10`: calls slot `+0x10` of the object at `+0x04` | confirmed (code) |
| `0x004f1420` | `WarMoveInstance_DeletingDtor` | `WarMoveInstance` deleting destructor | confirmed (code) |
| `0x004f1f00` | `Flag_TestSlot18` | `Flag` vtable slot `+0x50`: whether its slot `+0x18` with 0 returns non-zero | confirmed (code); meaning open |
| `0x004f1f30` | `StrHashBuckets_Fill` | fills a range of bucket pointers with a value | confirmed (code) |
| `0x004f1f60` | `StrHashBuckets_FillN` | fills n bucket pointers with a value | confirmed (code) |
| `0x004f1f88` | `StrHashBuckets_FillInsert` | the string hash table's bucket `std::vector` fill-insert (`HashTable_Construct`) | confirmed (code) |
| `0x004f21e8` | `StrHashTable_Clear` | frees every node of the string table cache's hash table | confirmed (code) |
| `0x004f22e8` | `StrHashTable_IterNext` | advances an iterator: the next node, or the first node of the next non-empty bucket (hash `h × 5 + c`) | confirmed (code) |
| `0x004f23e8` | `StrHashTable_Resize` | grows the bucket count to the next prime (list at `0x0057dfd8`) and rehashes | confirmed (code) |
| `0x004f2650` | `StrHashTable_InsertUnique` | inserts a string key unless present (no resize; hash `h × 5 + c`), `StringTableCache_Intern` | confirmed (code) |
| `0x004f32c0` | `ObjTypeBuckets_Fill` | the object-type list's hash buckets: fills a range | confirmed (code) |
| `0x004f32f0` | `ObjTypeBuckets_FillN` | fills n buckets | confirmed (code) |
| `0x004f3318` | `ObjTypeBuckets_FillInsert` | the bucket vector's fill-insert (`ObjTypeList_Reset`) | confirmed (code) |
| `0x004f3580` | `ObjTypeHash_Resize` | grows the object-type hash table to the next prime and rehashes (`ObjTypeList_Grow`) | confirmed (code) |
| `0x004f37e8` | `ObjTypeHash_InsertUnique` | inserts a name key unless present (`ObjTypeList_Grow`) | confirmed (code) |
| `0x004f4ae8` | `ObjType_GetField78` | returns `+0x78` (`ObjType_GetProperty`) | confirmed (code) |
| `0x004f4c60` | `PackDeque_CreateNodes` | the world manager's pack queue (a `std::deque` of 4-byte strings, 128 per `0x200`-byte node): allocates the nodes for a map range | confirmed (code) |
| `0x004f4d20` | `PackDeque_InitMap` | the pack queue's map and first nodes (`WorldManager_Construct`) | confirmed (code) |
| `0x004f5338` | `PackDeque_ReallocMap` | recentres or grows the pack queue's map | confirmed (code) |
| `0x004f5558` | `PackDeque_PushBack` | appends a string (shared by reference count unless marked unshareable, else cloned), with a new node when the last is full (`WorldManager_QueuePack`) | confirmed (code) |
| `0x004f5700` | `PackDeque_PopFront` | releases the front string and frees an emptied node (`WorldManager_Preload`) | confirmed (code) |
| `0x004f5900` | `WorldCollectSector_VecInsertAux` | `std::vector<T *>::_M_insert_aux` for `World_CollectSector`: inserts one pointer, doubling the storage when full (first 1) | confirmed (code) |
| `0x004f5ab8` | `WorldHeader_DeletingDtor` | world header (vtable `0x00545bf0`) deleting destructor: `WorldHeader_Release` | confirmed (code) |
| `0x004f5b10` | `Box_SetFlag68` | slot `+0x58` of the box vtables (`Box`, `PlayerBox`, `TurfBox`): stores a byte at `+0x68` | confirmed (code); meaning open |
| `0x004f5b60` | `PlayerBoxFreeList_Delete` | frees a `FreeList<PlayerBox>`'s arrays and the list (`PlayerBox_DestroyPool`) | confirmed (code) |
| `0x004f5c58` | `PlayerBox_DeliverScript` | `PlayerBox` slot `+0x40`: `ScriptHandler_Deliver` on its handler at `+0x70` | confirmed (code) |
| `0x004f5e18` | `TurfBoxPool_DeletingDtor` | `FreeListContainer<TurfBox>` (vtable `0x00545d28`) deleting destructor | confirmed (code) |
| `0x004f5ef0` | `TurfBoxPool_New` | slot `+0x10`: takes a slot and constructs a turf box (`TurfBox_Construct`) | confirmed (code) |
| `0x004f5f60` | `TurfBoxPool_Alloc` | slot `+0x18`: takes a slot | confirmed (code) |
| `0x004f6038` | `TurfBoxPool_Delete` | slot `+0x20`: destroys the box and returns its slot | confirmed (code) |
| `0x004f60d8` | `VolumeBoxFreeList_Delete` | frees a `FreeList<VolumeBox>`'s arrays and the list (`VolumeBox_DestroyPool`) | confirmed (code) |
| `0x004f61e8` | `VolumeBox_DeliverScript` | `VolumeBox` slot `+0x40`: `ScriptHandler_Deliver` on its handler at `+0x160` | confirmed (code) |
| `0x004f6428` | `SaveDate_Clear` | zeroes a six-byte date (`PS2SaveSystem_GetDate`, `SaveSystem_Construct`) | confirmed (code) |
| `0x004f64c8` | `SaveDate_Construct` | `PS2SaveSystem` vtable slot `+0x1e8`: clears a date and returns it | confirmed (code) |
| `0x004f6500` | `BinkMovie_Delete` | `delete player` from `Movie_Play`: the movie's destructor, then the pool free ([Movies](movies.md)) | confirmed (code) |

## Open questions

- What tactic slot `+0x40` asks (1 for Crowd, Idle, `0x1f`, StandGround and `0x1a`).
- Which map `ResMap_LowerBound` (`0x004de8d0`) belongs to: it is shared by the object, car and character model
  look-ups, so the resource maps may share one instance.
- The meaning of `Renderable_SetParam24` (`0x004df008`), `Flag_TestSlot18` (`0x004f1f00`) and `Box_SetFlag68`
  (`0x004f5b10`).
- The owner of the script-handler vtable `0x0053f018` (`0x004ed738`-`0x004ed778`).
