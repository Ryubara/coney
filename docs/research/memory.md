# Memory

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and, for the runtime sizes
in [Sizes at runtime](#sizes-at-runtime), the game running under PCSX2 2.9.94 (memory read over PINE).

## Purpose

How the game divides the PS2's 32 MB: one big arena taken at boot, carved into named pools that nest inside each
other, a stack of "current" pools that every allocation goes to, and two allocators (a general heap and a bump
allocator called a clump). The level loader, the world streamer, the resource manager, Lua, the STL containers and
RenderWare all allocate through it, and the streaming decisions read its free space. This page is what
[Boot](boot.md#device-initialisation), [Level loading](level-loading.md#memory) and
[The streamed world](world.md#loading-a-world) refer to for "heap", "pool" and "clump".

For Coney most of this is replaced by ordinary allocation. What must survive is listed under
[What a reimplementation must keep](#what-a-reimplementation-must-keep): chiefly the `Sector Pool` budget, because
the game evicts props and world parts when it runs short, and the player sees that.

## Original structure

`Memory/` sits at `0x00338420`-`0x0033b1a0` ([Source map](source-map.md#memory)); the block allocator under the heaps
follows it without a path string. Names below are ours unless they are path or tag strings.

| File | Class (role) | Evidence |
| --- | --- | --- |
| `Memory/MemoryClump.cpp` | `MemoryPoolClump`: a bump allocator over one block taken from a parent pool | confirmed (code), anchor `0x00338420` (tag `MemoryPoolClumpInternal`) |
| `Memory/MemoryFilter.cpp` | `MemoryFilter`: a pool that wraps another and forwards every call to it, the base of `MemoryPoolTrack`; not used on retail | confirmed (code), anchor `0x003388c0` |
| `Memory/MemoryHeap.cpp` | `MemoryPoolHeap`: a general-purpose heap over one block taken from a parent pool | confirmed (code), anchors `0x003389a0` (tag `u8`), `0x00338b38` |
| `Memory/MemoryPriv.cpp` | `PrivMemoryManager`: the memory manager, its heap stack and its pool registry | confirmed (code), anchor `0x00338e10` (tags `MemoryPoolHeap`, `All System`) |
| `Memory/MemoryTrack.cpp` | `MemoryPoolTrack`: a debug wrapper that tracks a pool's allocations | confirmed (code), anchors `0x00339e28`, `0x0033a050` |
| `Memory/WarriorsMemory.cpp` | the game's own pools (`Level Dynamic & LUA Memory`) and its free lists | confirmed (code), anchor `0x0033afe0` |
| (no path string, `0x0033b1a0`-`~0x0033c288`) | the block allocator under every `MemoryPoolHeap` | confirmed (code) for the code; its origin is discussed under [The heap allocator](#the-heap-allocator) |

Functions, by class. A "slot" is the vtable offset ([The pool interface](#pool-interface)); names are ours and match
the local Ghidra project. All confirmed (code) at the cited addresses unless the row says otherwise.

| Address | Name | Role |
| --- | --- | --- |
| `0x0033afc8` | `MemoryManager_SetGlobal` | points the global `0x005127e4` at the manager object `0x006eb958` |
| `0x0033b128` / `0x0033b168` | `WarriorsMemory_StaticInit` / its stub | give the manager its vtable `0x005444e0` and an empty heap stack; the stub calls it with `(1, 0xffff)` |
| `0x00338680` | `MemoryPoolTable_Clear` | boot step 1: zeroes the 50 words at `0x00715628` and the word `0x005127e0`, which no other code references |
| `0x00338e10` | `MemoryManager_Init` (slot `+0xb0`) | builds the boot [pool tree](#the-pool-tree) |
| `0x003395b8` / `0x003395d8` | `MemoryManager_Push` / `_Pop` (slots `+0xb8` / `+0xc0`) | the [heap stack](#the-heap-stack) |
| `0x00339a20`, `0x00339a58`, `0x00339a90`, `0x00339ac8`, `0x00339880`, `0x003398b8`, `0x00339d10` | `MemoryManager_Alloc`, `_Free`, `_Realloc`, `_SetOption`, `_LargestFreeBlock`, `_GetFreeBytes`, `_DescribeBlock` (slots `+0x50`, `+0x58`, `+0x60`, `+0x68`, `+0x80`, `+0x88`, `+0xa0`) | forward to the pool on top of the heap stack |
| `0x00339b00` | `MemoryManager_CheckEmpty` (slot `+0x70`) | true when every pool on the stack is empty |
| `0x00339878` / `0x003395e8` | `MemoryManager_GetRange` / `_Destroy` (slots `+0x78` / `+0x90`) | returns 0 / only calls its own `CheckEmpty` |
| `0x00339610` / `0x003397a0` | `MemoryManager_RegisterPool` / `_UnregisterPool` (slots `+0xc8` / `+0xd0`) | the [registry](#registry-records) |
| `0x003398f0`, `0x00339988` | `MemoryManager_TrackPool`, `_TrackPool2` (slots `+0xd8`, `+0xe0`) | wrap a pool in a [`MemoryPoolTrack`](#filter) when debug memory exists; on retail return the pool unchanged; the two differ only in their source line |
| `0x00339b80`, `0x00339c10`, `0x00339bf0`, `0x00339c80`, `0x00339cb8` | `MemoryManager_FindPoolByRange`, `_FindNameByRange`, `_FindPoolByName`, `_FindPoolByAddress`, `_FindNameByAddress` (slots `+0xe8`, `+0xf0`, `+0xf8`, `+0x100`, `+0x108`) | registry lookups |
| `0x00339cf0` / `0x00339d48` | `MemoryManager_ReportLeaks` (slot `+0x98`) / `MemoryRegistry_PrintTree` | walks the registry; the printing is compiled out |
| `0x003393c0`, `0x00339448`, `0x00339518` | `MemoryRegistry_InsertSibling`, `_FindContaining`, `_FindByName` | the registry's tree operations |
| `0x003389a0` / `0x00338a78` | `MemoryPoolHeap_InitInParent(heap, parent, size, name)` / `_InitDescriptor` | takes `size` bytes from `parent` and lays the block allocator's descriptor in them |
| `0x00338bf0`, `0x00338cc0`, `0x00338ce0` | `MemoryPoolHeap_Alloc`, `_Free`, `_Realloc` | call the [block allocator](#the-heap-allocator) |
| `0x00338d08`, `0x00338d30`, `0x00338df8` | `MemoryPoolHeap_GetRange`, `_LargestFreeBlock`, `_GetFreeBytes` | |
| `0x00338b38` | `MemoryPoolHeap_Destroy(heap, parent)` | gives the block back to `parent` |
| `0x00338420` / `0x00338498` | `MemoryPoolClump_InitInParent(clump, parent, size, name)` / `_InitInPlace(clump, block, size)` | takes `size` bytes (aligned to 128) from `parent` / uses a block given by the caller (the boot clumps) |
| `0x00338528` / `0x00338578` / `0x003385c8` | `MemoryPoolClump_Alloc` / `_Free` / `_Realloc` | `Realloc` returns 0 |
| `0x00338650`, `0x003385d0`, `0x00338618`, `0x00338640`, `0x00338610` | `MemoryPoolClump_SetOption`, `_CheckEmpty`, `_GetRange`, `_GetFreeBytes`, `_ReportLeaks` | `ReportLeaks` is empty |
| `0x003384b0` | `MemoryPoolClump_Destroy(clump, parent)` | |
| `0x003386d8`-`0x003388c0` | `MemoryFilter_Construct` and its eleven forwarding methods | [the filter base](#filter) |
| `0x00339e28`-`0x0033af78` | `MemoryPoolTrack_*` | [the tracking pool](#filter) |
| `0x0033ab98` / `0x0033ab28` | `MemoryManager_DumpToHost` / `MemoryDump_Printf` | [Debug](debug.md): every pool block to `host0:memdump.txt` |
| `0x0033b9d0`, `0x0033b650`, `0x0033bfa8`, `0x0033bf70` | `HeapAlloc_Malloc`, `_Free`, `_Realloc`, `_AlignedAlloc` | the [block allocator](#the-heap-allocator)'s entry points |
| `0x0033b1a0`, `0x0033b778`, `0x0033b810`, `0x0033b6f8`, `0x0033c268` | `HeapAlloc_FreeInternal`, `_Initialize`, `_MoreCore`, `_MoreCoreAligned`, `_GetDefault` | its internals |
| `0x0033afe0` | `WarriorsMemory_Init` | step 3 of [initialisation](boot.md#initialisation-order) |
| `0x0040d688` | `WorldManager_CreatePools` | `Sector Pool`, `Sector Pool 2` |
| `0x00192908` | `RwMemory_Alloc` | RenderWare's allocation hook: the `Filter Pool` or the current heap |
| `0x0032c758` | `Lua_Realloc` | Lua's allocator: always the `Level Dynamic & LUA Pool` |

## Data

All classes use the GCC 2 vtable layout (8-byte `{delta, fn}` slots, see [Compiler](compiler.md)); a slot is named
by its offset from the vtable start, as on the other pages.

### The pool interface {#pool-interface}

The memory manager, `MemoryPoolHeap` and `MemoryPoolClump` share one interface (manager vtable `0x005444e0`, heap
`0x00544428`, clump `0x00544370`). The manager's versions of `+0x50` to `+0xa8` forward to the pool on top of its
heap stack (`0x00339a20` and its neighbours), so "allocate from the manager" means "allocate from the current pool".

| Slot | Method | `MemoryPoolHeap` | `MemoryPoolClump` | Evidence |
| --- | --- | --- | --- | --- |
| `+0x50` | `Alloc(tag, size, align, file, line, name, flag)` | block allocator | bump | confirmed (code) |
| `+0x58` | `Free(ptr, file, line)` | block allocator | counted or ignored, [below](#clump-behaviour) | confirmed (code) |
| `+0x60` | `Realloc(tag, ptr, size, align, file, line, name)` | block allocator | returns 0 | confirmed (code) |
| `+0x68` | `SetOption(which, value)` | nothing | `which` 0: count frees (`+0x14`) | confirmed (code) |
| `+0x70` | `CheckEmpty()` | returns 0 | true when nothing is allocated; reports leaks otherwise (an empty function on retail) | confirmed (code) |
| `+0x78` | `GetRange(&first, &last)` | the heap's block | the clump's block | confirmed (code) |
| `+0x80` | `LargestFreeBlock()` | [search](#largest-free-block) | returns 0 | confirmed (code) |
| `+0x88` | `FreeBytes()` | total free | size minus used | confirmed (code) |
| `+0x90` | `Destroy(parent)` | returns the block to `parent` | same | confirmed (code) |
| `+0x98` | report leaks | empty | empty | confirmed (code) |
| `+0xa0` | a per-pointer query (`DescribeBlock`, our name) | empty (shared default) | empty (shared default) | confirmed (code) that only the filter and the tracker forward it; meaning inferred |
| `+0xa8` | `Contains(address)` (shared, `0x004f0520`) | through `GetRange` | same | confirmed (code) |

Note the clump's `LargestFreeBlock` returning 0: only a heap can answer "is there room for N bytes", which is why
every budget check in the game is made against a heap (the `Sector Pool`).

### Allocation arguments

Every allocation names itself: `Alloc(tag, size, align, file, line, name, flag)`. `tag` is a class name string
(`"GameTimer"`, `"u8"`, `"MemoryPoolClump"`), `file` and `line` the source position, `name` an optional debug name
(the heap compares it and the tag with `"Track"` and `"Debug"` and then allocates the same way whatever the result),
and `flag` a last word that is 1 for a few pool objects (the `Filter Memory`, `Level Dynamic & LUA Memory` and both
`Sector Pool` heap objects) and 0 everywhere else seen. Neither allocator's result depends on `tag`, `name` or
`flag`. Confirmed (code) at `0x00338bf0`, `0x00338528`. These
strings are what the [source map](source-map.md#method) uses as anchors; they have no effect on behaviour.

### The memory manager {#memory-manager}

One static object at `0x006eb958` (vtable `0x005444e0`), reached through the pointer `0x005127e4`. Confirmed (code)
at `0x0033afc8`, `0x0033b128`, `0x00338e10`.

| Offset | Meaning |
| --- | --- |
| `+0x00` | vtable |
| `+0x08` | the **global heap** (`Global Memory`), the default allocation target |
| `+0x0c` | the debug heap (`Debug Heap`) |
| `+0x10` | root of the pool registry |
| `+0x14` | free list of registry records (512 of them) |
| `+0x18` | index of the top of the heap stack; -1 when empty |
| `+0x1c` | 1 when a separate debug arena exists (never on retail) |
| `+0x20` | the heap stack: an array of pool pointers |

The stack has no bound check on push and pop ignores its argument (it only decrements). Its capacity is not stated;
the next global after the object is `0x006eb9b8`, so 16 entries fit (inferred).

### MemoryPoolHeap {#heaps}

0x10 bytes (vtable `0x00544428`): `+0x04` the allocator's descriptor, `+0x08` the block's first byte, `+0x0c` the
block's size. `MemoryPoolHeap_InitInParent` pushes the parent, allocates the block from it (tag `u8`, align 16),
pops, and lays the allocator's descriptor (0xf8 bytes, zeroed) at the block's start. Usable memory starts at the next
1 KB boundary after the descriptor; the descriptor's `+0x40` holds the usable size and `+0x48` the bytes in use, so
`FreeBytes` is their difference. Confirmed (code) at `0x003389a0`, `0x00338a78`, `0x00338df8`.

### MemoryPoolClump {#clumps}

0x18 bytes (vtable `0x00544370`), confirmed (code) at `0x00338420`-`0x00338650`:

| Offset | Meaning |
| --- | --- |
| `+0x04` | the block (taken from the parent with tag `MemoryPoolClumpInternal`, align 128) |
| `+0x08` | size |
| `+0x0c` | bytes used (the bump offset) |
| `+0x10` | live allocation count |
| `+0x14` | "count frees" option, 0 by default |

### Registry records

0x30 bytes each, 512 of them in one 0x6000-byte array (the free list's object has the tag
`FreeList<RegisteredPool>`, its arrays `R_ALIGN`), confirmed (code) at
`0x00338e10`, `0x00339610`: `+0x00` the pool, `+0x04` its parent, `+0x0c` first child, `+0x10` and `+0x14` the
neighbours above and below in address order, `+0x18`/`+0x1c` the first and last byte of the pool, `+0x20` the name
(at most 15 characters; spaces and underscores become `-`). A new record goes under the registered pool whose range
contains it, so the registry is a tree of pools by address containment (`0x00339448`, `0x003393c0`). Lookups by
range (slots `+0xe8`, `+0xf0`) and by address (`+0x100`, `+0x108`) exist; their callers were not traced. The only
walk found (`0x00339d48`, slot `+0x98`) prints nothing on retail.

### MemoryFilter and MemoryPoolTrack {#filter}

Debug-only classes; on retail nothing creates them, because the manager's `TrackPool` slots return the pool itself
when there is no debug arena (`+0x1c` is 0). Confirmed (code) at the cited addresses.

**`MemoryFilter`** (`Memory/MemoryFilter.cpp`): `+0x04` the wrapped pool, `+0x08` a name of up to 63 characters
(`0x003386d8`). Each method (`0x00338708`-`0x00338890`) forwards the same slot to the wrapped pool; `Destroy`
(`0x003388c0`) checks itself empty, destroys the wrapped pool into the parent, and frees the wrapped pool's object.

**`MemoryPoolTrack`** (0x60 bytes, vtable `0x005445f8`, constructor `0x0033a6f0`), a `MemoryFilter` that records
every live allocation in an STL map keyed by address (`+0x48`, its nodes tagged `STL` / `STL Debug Allocation`;
`+0x50` the record count). `Alloc` (`0x0033a7d0`) forwards and then records; `Free` (`0x0033a8a8`) removes the record
(`0x0033a050`) and forwards, asking the manager to report when the pointer is unknown; `Realloc` (`0x0033a948`)
removes, forwards and records the result. A record (`TrackInfo`, 100 bytes from the debug heap, `0x00339e28`) holds
the pointer, size, alignment, tag (31 characters), the source file's last path component (RenderWare's
`/home/ack/System/rwsdk370/` prefix stripped by `0x0033a690`), line, flag and the debug name or `none`.
`SetOption` (`0x0033ae38`) takes `which` 1 and 3 as report modes (`+0x58`, `+0x5c`); `ReportLeaks` (`0x0033aed0`)
then dumps to the host (`0x0033ab98`), or walks the records (`0x0033aa30`, which only spins a delay loop per record
on retail: its printing is compiled out). `CheckEmpty` (`0x0033af38`) is true when no record is left; `Destroy`
(`0x0033af78`) checks and then destroys as the filter does.

### Globals

| Address | Pool | Evidence |
| --- | --- | --- |
| `0x005127e4` | the memory manager (pointer) | confirmed (code) |
| `0x006eb9d0` | `Filter Pool` | confirmed (code) at `0x00338e10` |
| `0x006eb9b8`, `0x006eb9c0`, `0x005127f0` | `Level Dynamic & LUA Pool`: the game's, the STL allocator's and Lua's copy of the same pointer | confirmed (code) at `0x0033afe0` |
| `0x006eb9c8` | `Sector Pool` | confirmed (code) at `0x0040d688` |
| `0x006eb9cc` | `Sector Pool 2` (written, never read) | confirmed (code) |
| `0x005147d0` | the `Sector Pool`'s free bytes after a level loads (written, never read) | confirmed (code) at `0x0040dbb8` |

## Behaviour

### The pool tree

Indentation is containment: each pool's memory is one block of its parent. Confirmed (code) at the cited
functions for names, parents and sizes.

- **`All System`**: a clump over the whole arena, which is one `malloc` of `0x018d7c94` bytes (26,049,684) made
  by the device (device slot `+0x70`, `0x00148828`). `MemoryManager_Init`, `0x00338e10`.
    - **`System Memory Pool`** / **`Global Memory`**: a heap over everything the clump has left (the arena minus
      the 16-byte heap object). Registered as `Global Memory`; it is the manager's `+0x08` and stays on the
      bottom of the heap stack, so it is the default target of every allocation.
        - **`Filter Memory`** / **`Filter Pool`**: a heap of `0x29000` bytes (167,936), RenderWare's scratch pool
          ([below](#who-allocates-where)).
        - **`Debug Pool`** / **`Debug Heap`**: a heap of 1 KB (on retail, where the device reports no debug
          arena).
        - the registry's records and pointers (`0x6000` + `0x800` bytes).
        - **`Level Dynamic & LUA Memory`** / **`Level Dynamic & LUA Pool`**: a heap of `0x1ef000` bytes
          (2,027,520). `WarriorsMemory_Init`, `0x0033afe0`, step 3 of
          [initialisation](boot.md#initialisation-order).
        - everything `Game_InitializeSubsystems` allocates before step 21 (`Timer`, `GameTimer`, `IPhysics`,
          `ObjectAttribs` and the rest, [Boot](boot.md#initialisation-order)).
        - **`Sector Pool`**: a heap of (the global heap's largest free block − 128 KB). `WorldManager_CreatePools`,
          `0x0040d688`, step 21.
            - **`Global Data Pool`** (clump, debug name `GlobalPool`): 101 % of `warriors.glr`, for the whole game.
              `0x0040d900`.
            - **`World Level Pool`** (clump): 103 % of `<level>.lev`, at least 256 KB; per level. `0x0040dbb8`.
            - one clump per world, named after it: the manifest's world heap size; per level. `0x00410648`
              ([The streamed world](world.md#loading-a-world)).
            - **`Sectors<i>`** (clump), one per loaded world part: the manifest's part heap size; while playing.
              `0x004110c0`.
            - one clump per resource group, named after the resource: the size its pack's group header gives;
              while the resource is held. `0x00186e88` ([Chunk system](chunk-system.md#loading-a-grouped-container)).
        - **`Sector Pool 2`**: a heap of (the largest free block that is left − 128 KB), at least 4 KB.
          `0x0040d688`.
        - everything allocated afterwards with no other pool pushed: `ResourceManager` (`0x904c` bytes),
          `W_GameState`, `WorldManager` and the rest.

So all level data lives in clumps inside the `Sector Pool`, which is a heap: the clumps come and go (a level's, a
world part's, a resource's), and the heap's free space is the game's memory budget for streaming.

**Sizes.** The arena and every fixed size above are exact. The `Sector Pool` is not a constant: it is what the
global heap has left at step 21, minus 128 KB. Subtracting only the fixed sizes listed above gives an upper bound of
26,049,684 − 167,936 − 2,027,520 − 324,704 (`IPhysics`) − 216,588 (`ObjectAttribs`) − 131,072 = 23,181,864 bytes;
the smaller allocations of steps 4 to 20 take about 6 MB more, and on a retail boot the pool is **17,217,536 bytes**
([Sizes at runtime](#sizes-at-runtime)). Because `Sector Pool` is sized from the largest free block and leaves 128 KB
of it, `Sector Pool 2` is sized from what is then the largest free block, normally that same 128 KB remainder (minus
the heap's own object), so it ends up at its 4 KB minimum, which is what a retail boot shows. Nothing reads
`0x006eb9cc`, and no code pushes the pool, so **`Sector Pool 2` is unused** (confirmed (code) for the global: its only
references are the two writes in `0x0040d688`; inferred that nothing reaches it through the registry lookups).

### Sizes at runtime {#sizes-at-runtime}

Read from the pool objects of [Globals](#globals) and the heaps' descriptors ([MemoryPoolHeap](#heaps): `+0x40`
usable, `+0x48` in use) and from the registry records (`+0x18`/`+0x1c`, first and last byte), in the NTSC-U game.
**Evidence:** confirmed (runtime), PCSX2 2.9.94, memory read over PINE at three points: during the start-up movies
(after step 21), on the main menu (front-end level loaded) and in a Quick Rumble fight in the Fight Pen (`level102`,
its worlds `level102s-sec.w` and `level102d-sec.w` in the registry).

| Pool | Object | Block | Size (bytes) | Usable (heap descriptor `+0x40`) |
| --- | --- | --- | --- | --- |
| `All System` / `Global Memory` heap | `0x00715b70` | `0x00715b80` | 26,049,668 (`0x018d7c84`, the arena minus 16) | 26,048,516 |
| `Filter Pool` | `0x00760c00` | `0x00761000` | 167,936 (`0x29000`) | |
| `Level Dynamic & LUA Pool` | `0x00760fa0` | `0x00bbc000` | 2,027,520 (`0x1ef000`) | 2,026,496 |
| **`Sector Pool`** | `0x00760f80` | `0x00f62000` | **17,217,536 (`0x0106b800`)** | 17,216,512 |
| `Sector Pool 2` | `0x00760f70` | `0x01fcd800` | 4,096 (its minimum) | |

The objects themselves sit in the global heap; the `Debug Heap` is `0x00760ff0` and the registry's root record
`0x0078a400`. The `ResourceManager` is the next thing after `Sector Pool 2`, at `0x01fce800`, and the global heap
has 329,764 bytes free once the front end is up (usable minus in use, the same in the fight).

Use over time:

| When | `Sector Pool` in use | free | `0x005147d0` (free after the last level load) | `Level Dynamic & LUA` in use |
| --- | --- | --- | --- | --- |
| start-up movies | 4,437,632 | 12,778,880 | 15,259,008 | |
| main menu (front-end level) | 5,387,744 | 11,828,768 | 12,207,264 | 902,120 |
| Quick Rumble fight, `level102` | 9,064,800 | 8,151,712 | 12,087,072 | 1,071,160 |

So about half the `Sector Pool` is free in a small arena; the streamed story levels, which add `Sectors<i>` clumps,
were not measured. In the fight the registry shows, inside the `Sector Pool`: `Global Data Pool` 641,842 bytes,
`World Level Pool` 262,144 (its 256 KB minimum: `level102.lev` is small), the world clumps `level102s-sec.w`
485,812 and `level102d-sec.w` 265,236, a `generic-header` clump of 2,126,704, and dozens of resource clumps
named by the decimal CRC-32 of their resource ([Chunk system](chunk-system.md#loading-a-grouped-container)), from
a few kilobytes to a few hundred kilobytes. The heap stack was 3 deep at the start-up movies (`Global Memory`,
`Level Dynamic & LUA Pool`, `Sector Pool`) and 2 deep in the fight; its capacity stays as inferred below.

### The heap stack

There is no allocator argument in the game's code: an allocation goes to whatever pool is on top of the manager's
stack. Code that wants a particular pool pushes it, allocates, and pops, as in this pattern from the level loader
(`0x0040dbb8`):

```text
push(SectorPool)
clump = new MemoryPoolClump          # the 0x18-byte object, from the Sector Pool
clump.InitInParent(SectorPool, max(lev size * 103 / 100, 256 KB), "World Level Pool")
register("World Level Pool", clump, parent = SectorPool)
pop()
push(clump); load <name>.lev through the chunk system; pop()
```

Frees go the same way: `Free` is forwarded to the pool on top of the stack, not to the pool the pointer came from,
and the heap allocator does not check. Code that frees into another pool pushes it first (`0x00338b38`,
`0x004115d0`) or calls that pool directly (the STL allocator, `0x004e3a48`). `FS_MemoryFile`'s destructor frees its
data into whatever is current (`0x00154290`). Confirmed (code) at the cited addresses; that every free in the game
matches its pool is inferred from the pattern, not checked.

### The heap allocator {#the-heap-allocator}

`MemoryPoolHeap` delegates to a block allocator at `0x0033b1a0`-`0x0033c288` with one descriptor per heap
(`0x0033bf70` allocate, `0x0033b650` free, `0x0033bfa8` reallocate). Its shape, confirmed (code) at those addresses:
memory is managed in 1 KB blocks; requests up to 512 bytes are served from blocks split into equal power-of-two
fragments; larger requests take whole runs of blocks; a 12-byte record per block says which; the descriptor has
hooks for free, reallocate and "more memory" (the last pointed at a function that hands out the heap's own block,
`0x00338948`). This is the layout of the classic GNU `malloc` (block and fragment tables, power-of-two fragments)
with a descriptor per heap as in `mmalloc` (inferred from the structure; no strings name it).

The pieces, confirmed (code): `0x0033bf70` rounds the size up to the alignment and calls `HeapAlloc_Malloc`
(`0x0033b9d0`), which defers to a malloc hook (`+0x24`) when one is set, initialises the descriptor on first use
(`0x0033b778`: the 12-byte block table, flag 2), serves sizes up to 512 (at least 8) from the power-of-two fragment
lists, and larger sizes from the free block runs, else from new core (`0x0033b810`, which doubles the block table
when it no longer covers the heap; `0x0033b6f8` pads the core to 1 KB). `HeapAlloc_Free` (`0x0033b650`) honours a
list of aligned pointers (`+0xa4`) and a free hook (`+0x20`), then `0x0033b1a0`: a run of blocks merges with its
free neighbours in the address-ordered free list (and a run of at least 8 blocks at the top of the heap goes back
through the "more memory" hook), a fragment goes back to its size's list, and a block whose fragments are all free
is freed whole. `HeapAlloc_Realloc` (`0x0033bfa8`) frees on size 0, allocates on a null pointer, keeps a fragment
whose size class still fits, and shrinks a run in place. A null descriptor means the default one, the first heap
set up (`0x005127f4`, `0x0033c268`). Coney replaces all of this with ordinary allocation
([What a reimplementation must keep](#what-a-reimplementation-must-keep)).

**Alignment.** The heap aligns by rounding the size up to a multiple of `align` before allocating
(`0x0033bf70`): a fragment of 2^k bytes is aligned to 2^k and blocks to 1 KB, so any alignment up to 1 KB holds
(inferred from the layout). The clump aligns its bump offset to `align` relative to its block, which is aligned to
128. Most callers ask for 16; the STL allocator 16, Lua 8, clump blocks 128.

#### Largest free block {#largest-free-block}

`MemoryPoolHeap_LargestFreeBlock` (`0x00338d30`, slot `+0x80`) does
not walk the free lists; it searches by trial: try to allocate half the free bytes (align 16), and on failure halve
the step towards the last size that worked, on success free it and grow the guess, until the guess stops changing.
It returns the largest size that succeeded. Confirmed (code). The answer is exact only to within the search's last
step, and it is what every "is there room" check uses.

### Clumps {#clump-behaviour}

A clump is a bump allocator. `Alloc` rounds the offset up to `align`, hands out `block + offset`, adds `size` and
counts one more live allocation, **without checking the clump's size** (confirmed (code) at `0x00338528`): a clump
sized too small overruns into whatever follows it in the parent. The 101 %, 103 % and manifest sizes are the
original's headroom against that.

`Free` normally does nothing: with the "count frees" option off (the default) it calls an empty function. With it on
(`SetOption(0, 1)`) each free decrements the count, and the clump's offset returns to 0 when the count reaches 0.
Confirmed (code) at `0x00338578`, `0x00338650`. The world-part unload (`0x004115d0`) turns the option on while it
destroys a part's atomics and texture dictionary, checks the clump is empty, then destroys the clump into the
`Sector Pool` and deletes its object from the `Level Dynamic & LUA Pool`. Memory in a clump is otherwise reclaimed
only by destroying the whole clump.

### Who allocates where {#who-allocates-where}

| Allocator | Pool | Evidence |
| --- | --- | --- |
| `new` in game code | the current pool (the global heap unless a loader pushed another) | confirmed (code), e.g. `0x00160df8` |
| STL containers (`Memory/MemoryStl.h`, link-once code) | always the `Level Dynamic & LUA Pool`, through `0x006eb9c0`, called directly | confirmed (code) at `0x004e3a48`, `0x004eb8f0` |
| Lua 4.0 (`lmem.cpp`) | always the `Level Dynamic & LUA Pool`: pushed around a `Realloc` with align 8 | confirmed (code) at `0x0032c758` |
| RenderWare | `RwMemory_Alloc` (`0x00192908`): the `Filter Pool` for hints of duration `0x10000` and for hints `0x3000e`, `0x3000f` and (`0x3050d` with size 8); the current pool otherwise | confirmed (code); the hint meanings are RenderWare's |
| chunk data | the current pool (the loader pushes the destination clump) | confirmed (code), [Chunk system](chunk-system.md#loading-a-flat-container) |
| `FS_MemoryFile` data | the current pool | confirmed (code) at `0x001541e0` |
| animation, sound and task free lists | `WarriorsMemory_Init` creates them first (`0x0010b9f0`, `0x001048a0`, `0x00111ac0`) in the global heap | confirmed (code) |

The `Level Dynamic & LUA Pool` also holds the 0x18-byte clump objects of world parts and resource groups and the
stream wrappers the loaders create ([Level loading](level-loading.md#memory)); `UnloadLevel` frees the level's
game objects from it and the Lua state is recreated there.

### Failure

- **Heap**: when the block allocator returns null, `MemoryPoolHeap_Alloc` stores the byte 1 at address 0 and
  returns null (confirmed (code) at `0x00338c98`). The store is the usual deliberate "stop here" marker for a
  debugger; what it does on a retail console is not checked, and no caller seen handles the null, so an
  out-of-memory heap allocation is fatal one way or the other (inferred). The largest-free search calls the block
  allocator directly, so its failed trials do not trip it.
- **Lua**: a failed reallocation of a non-zero size stores to address 0 as well, then raises Lua's memory error
  (`0x003291f0` with code 4, Lua 4.0's `LUA_ERRMEM`). Confirmed (code) at `0x0032c758`; the code's meaning is Lua's.
- **Clump**: no failure at all; an overrun corrupts silently (above).
- **Registry**: more than 512 registered pools would dereference a null record (inferred from `0x00339610`).
- **Heap stack**: no bound check (above).

The game therefore never handles running out of memory; it avoids it by checking the `Sector Pool`'s largest free
block before every streaming allocation and evicting first ([Level loading](level-loading.md#memory)).

### Debug names and tracking

Each pool has two names: the one passed to its `InitInParent` (unused by the clump) and the one it is registered
under (`Global Memory`, `Global Data Pool`, `World Level Pool`, `Sectors<i>`, a world's or resource's name). The
manager's `Track` slots would wrap a pool in a 0x60-byte `MemoryPoolTrack` when a debug arena exists (`+0x1c`); on
retail they return the pool itself, so tracking costs nothing. `0x005147d0` (the `Sector Pool`'s free bytes after a
load) and the debug FPS counter's free-memory figure (`0x001569c0`) are for display only.

## What a reimplementation must keep {#what-a-reimplementation-must-keep}

**Behaviourally visible** (keep as a budget, deterministic in test mode):

- **The `Sector Pool` budget.** Before a world or a world part gets its clump, `ResourceManager_MakeRoom`
  (`0x00187d28`) evicts the least recently used resources that nothing holds until the `Sector Pool`'s
  `LargestFreeBlock` is at least the clump's size; the resource manager's parent pool is the `Sector Pool`
  (confirmed (code) at `0x0040f850`). Which props, characters and world parts are resident, and so what pops in
  where, follows from this budget. A PC build may raise it, but should model it as one budget with the original's
  size as an option.
- **The intro-movie reservation**: `InitLevel` holds 3,200,000 bytes of the resource manager's pool while the level
  loads when an intro movie will play ([Level loading](level-loading.md#initlevel)), shrinking the budget during the
  preload.
- **The part record's heap size** becomes the bytes the part really used after its first load
  ([The streamed world](world.md#streaming)), so later requests for that part ask for less.

Fragmentation also shapes the original's answer (the largest *block*, not the free total), and reproducing it
exactly would need the block allocator above. Treating the budget as "size minus bytes in use" is the practical
approximation; how much the two differ in play is open.

**Not visible** (replace with ordinary allocation): the arena and its `malloc`, the heap stack as a mechanism (pass
the destination explicitly), tags, file and line arguments, the registry, `MemoryPoolTrack`, the `Filter Pool`,
the `Debug Pool`, `Sector Pool 2`, the clump's bump allocation (any arena or per-level allocator will do, but keep
"free everything a level made in one go"), and the `Level Dynamic & LUA Pool`'s fixed size (an out-of-memory there
crashes the original, so nothing depends on its limit).

## Coney's implementation

Coney allocates with the C++ standard library. The `Sector Pool` budget exists as a number (2026-10-04):
`coney::world::SectorBudget` (`src/world/sector_budget.h`) counts "capacity minus bytes in use", the approximation
recommended above, and the world streamer asks it for room before every world and part, as
`ResourceManager_MakeRoom` asks the heap ([The streamed world](world.md#coneys-implementation)). There are no
resources yet, so a request either fits or the streamer frees a world part.

Its size is the retail boot's, 17,217,536 bytes (`kSectorPoolSize`, [Sizes at runtime](#sizes-at-runtime)). The
world viewer charges it, in the original's order, with the `Global Data Pool` (101 % of `warriors.glr`), the
`World Level Pool` (103 % of `<level>.lev`, at least 256 KB), each world's heap and each loaded part's heap, all from
the manifest. Per-level ownership is the world set's: destroying it frees every part and world and gives their bytes
back. At this size the largest levels' worlds and parts no longer fit at once (17.4 MB at most for worlds and parts
alone), so the original's eviction shows in ordinary play: Coney's disc test, streaming every level's worlds on their
own, frees 43 parts and is short of room in 435 of 15,945 frames.

## Open questions

- **The `Sector Pool`'s size** (answered): 17,217,536 bytes on a retail NTSC-U boot; use at the menu and in a level
  is in [Sizes at runtime](#sizes-at-runtime). Still open: the use in a streamed story level, and the largest free
  block (as opposed to the free total) at a level start.
- **How much fragmentation matters**: whether `LargestFreeBlock` and `FreeBytes` differ enough in play to change
  an eviction. A runtime log of both at each `ResourceManager_MakeRoom` call would tell.
- **The registry lookups** (slots `+0xe8` to `+0x108`): who calls them, if anyone, on retail.
- **The heap stack's capacity**: 16 is inferred from the next global.
