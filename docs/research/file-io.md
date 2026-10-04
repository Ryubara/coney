# File I/O

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). No runtime claims.

## Purpose

How the game turns a file name such as `level1.lev` into bytes in memory. Every asset lives in `WARRIORS.WAD` and is
found by name through `WARRIORS.DIR` ([WARRIORS.DIR / .WAD](formats/wad-dir.md)); the bytes are fetched by the game's
own IOP module, either synchronously through a 384 KB buffer or asynchronously through a request queue serviced once
per frame. The [chunk system](chunk-system.md) reads from these streams; the [boot path](boot.md) creates them.

For Coney most of this layer is replaced by plain reads from the player's disc image, but the *interfaces* (a file
system with `Exists`, `GetSize`, `Open`, `Close`; a file with `Read`, `Seek`, `Tell`, `GetSize`) and the two read
paths are what the rest of the game is written against.

## Original structure

| File | Class (role) | Address range | Evidence |
| --- | --- | --- | --- |
| `Device/ps2/DS_PS2Device.cpp` | `DS_PS2Device`: the platform object; creates and owns the file systems | `0x001483e8`-`0x00148580` (+ methods `0x00145790`-`0x001458b0`, `0x00148230`, `0x00148820`-`0x001488f8`) | confirmed (code) |
| `Device/ps2/DS_PS2FileSys.cpp` | `PS2FileSys` (`host0:` debug files) and `PS2DbgFile`; also `PS2StreamFileSys` and its stream file | `0x00148608`-`0x00148e00` | confirmed (code) for the anchors; the stream classes' file is inferred from position |
| `Device/ps2/fileio/DVDWadIndexPS2.cpp` | `DVDWadIndex`: the name index | `0x00149040`-`0x00149248` | confirmed (code) |
| `DVDWadIndexPS2.cpp` (by position) | the stream request for one WAD entry | `0x00149410`-`0x00149500` | inferred |
| (unnamed, after `sound/msaudiodevice.cpp`'s stub `0x0014d508`) | the IOP command layer: commands to `IOP.IRX`, shared with audio | `0x0014da38`-`0x00151c70` | inferred |
| `FileIO/` (unnamed, before `FS_MemoryFile.cpp`) | `FS_FSToStreamFSFileSys` and its file | `0x00153f60`-`0x00154168` | confirmed (code) for the allocation tag; file inferred |
| `FileIO/FS_MemoryFile.cpp` | `FS_MemoryFile`: a file in memory | `0x001541e0`-`0x00154440` | confirmed (code) |
| `FileIO/StreamManager.cpp` | the buffered reader, and `FileManager` (asynchronous requests, the `File Stream Buffer`) | `0x001544c0`-`0x00155b10` | confirmed (code) |
| `World/` (just before `WorldLevel.cpp`'s anchor `0x0040c7f0`) | the WAD object: opens `WARRIORS.DIR` and `WARRIORS.WAD`, wraps `Find` | `0x0040c5e0`-`0x0040c688` | confirmed (code) for the code; file inferred |

Key functions (names ours):

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x001483e8` | `DS_PS2Device::CreateStreamFileSys` | `PS2StreamFileSys` → device `+0x94` | confirmed (code) |
| `0x001484d8` | `DS_PS2Device::CreateDefaultFileSys` | `FS_FSToStreamFSFileSys` over the stream file system, mounted as current (`+0x90`) | confirmed (code) |
| `0x00148460` | `DS_PS2Device::CreateHostFileSys` | `PS2FileSys("debug/")` → device `+0x8c` | confirmed (code) |
| `0x00148aa0` | `PS2StreamFileSys::Open` | start an IOP read of a WAD entry | confirmed (code) |
| `0x00148c90` | `PS2StreamFile::Wait` | block until the read is done, servicing pads | confirmed (code) |
| `0x00149160` | `DVDWadIndex::DVDWadIndex(path)` | read the index | confirmed (code) |
| `0x001490b8` | `DVDWadIndex::Find(name)` | name → entry | confirmed (code) |
| `0x00149410` | `IopStream::Start` | send the read commands to the IOP | confirmed (code); name inferred |
| `0x00153fa8` | `FSToStreamFile::Read` | one blocking read | confirmed (code) |
| `0x001544c0` | `BufferedStream::BufferedStream(file, buf, size)` | | confirmed (code) |
| `0x00154630` | `BufferedStream_Read` | | confirmed (code) |
| `0x001547b0` | `FileManager` creation | | confirmed (code) |
| `0x00154d50` | `FileManager_Request` | queue an asynchronous whole-file read | confirmed (code) |
| `0x00154ae0` | `FileManager_Service(fm, block)` | start/finish queued reads | confirmed (code) |

## Data

All classes use the GCC 2 vtable layout (8-byte `{delta, fn}` slots, see [Compiler](compiler.md)); slot offsets
below are from the vtable start. Slots `+0x08` to `+0x48` are the common base class's (destructor and allocation
helpers) and are left out.

### File system interface {#file-system-interface}

| Slot | Method | Evidence |
| --- | --- | --- |
| `+0x50` | `bool Exists(name)` | confirmed (code), `0x00148bf0`, `0x00148748` |
| `+0x58` | `u32 GetSize(name)` | confirmed (code), `0x00148ba8`, `0x001487b0` |
| `+0x60` | `File* Open(name, ...)`: the stream file system takes `(name, dest, length, offset)`; the others `(name, mode)` with mode 0 = read, 1 = write | confirmed (code) |
| `+0x68` | `Close(File*)` | confirmed (code) |
| `+0x70` | `Update()`: stream file system only, drives the IOP client | confirmed (code), `0x00148c60` |

### File interface {#file-interface}

| Slot | Method | Evidence |
| --- | --- | --- |
| `+0x50` | `Read(dst, n)` | confirmed (code) |
| `+0x58` | `Write(src, n)` | confirmed (code) |
| `+0x60` | `Seek(position)` (absolute) | confirmed (code) |
| `+0x68` | `u32 Tell()` | confirmed (code) |
| `+0x70` | `u32 GetSize()` | confirmed (code) |
| `+0x78` | release: frees or detaches the buffer | confirmed (code) |
| `+0x88` | `void* GetData()` (`FS_MemoryFile` only) | confirmed (code) |

None of the implementations checks bounds or reports a short read.

### DS_PS2Device

The platform object, a static at `0x005de160` (vtable `0x00537c80`) reached through `0x0050b788`. File-system fields,
confirmed (code):

| Offset | Meaning |
| --- | --- |
| `+0x04` | index of the last mounted file system, -1 at start |
| `+0x08` | array of mounted file systems (`0x00145888` appends and makes the new one current) |
| `+0x88` | copy of `+0x90` (`0x001458b0`) |
| `+0x8c` | `PS2FileSys` for `host0:` debug files |
| `+0x90` | **current file system**: the `FS_FSToStreamFSFileSys`; every game load goes through this one |
| `+0x94` | `PS2StreamFileSys` |

### PS2StreamFileSys

0x1c bytes, vtable `0x00537e18`, confirmed (code) at `0x00148920`:

| Offset | Meaning |
| --- | --- |
| `+0x04` | pool of 10 stream-file records, 20 bytes each |
| `+0x08` | free list: array of 10 record pointers |
| `+0x0c` | pool capacity (10) |
| `+0x10` | records in use |
| `+0x14` | **the** open stream file, or null: only one read can be in flight |

A stream file record (vtable `0x00537db0`): `+0x04` IOP request handle (-1 when idle), `+0x0c` 0, `+0x10` owning file
system. Slot `+0x50` is `IsDone()`, slot `+0x58` is `Wait()`.

### FS_FSToStreamFSFileSys

0x58 bytes, two vtables (`0x00538118` for the file system, `0x00538190` for the file it embeds), confirmed (code) at
`0x001484d8`:

| Offset | Meaning |
| --- | --- |
| `+0x04` | the `PS2StreamFileSys` |
| `+0x08` | the one embedded file: `+0x04` position, `+0x08` name (64 bytes), `+0x48` the stream file system |
| `+0x54` | in use (1 between `Open` and `Close`) |

So the default file system can have **one file open at a time**.

### DVDWadIndex {#the-wad-index}

8 bytes, confirmed (code) at `0x00149160`: `+0x00` entry count, `+0x04` pointer to the entries (12 bytes each,
`{wadOffset, size, nameHash}`, allocation tag `DVDWadEntry`). Layout of the file: [WARRIORS.DIR](formats/wad-dir.md).
The WAD object at `0x006f39e8` (reached through `0x005147a4`) holds the index pointer at `+0`.

### BufferedStream

Built on the caller's stack (0x20 bytes, vtable `0x005382b8`), confirmed (code) at `0x00154550`:

| Offset | Meaning |
| --- | --- |
| `+0x04` | buffer |
| `+0x08` | buffer size |
| `+0x0c` | bytes left in the buffer |
| `+0x10` | read cursor in the buffer |
| `+0x14` | position in the file (`Tell`) |
| `+0x18` | file size (from the file's `GetSize` at construction) |
| `+0x1c` | the underlying file |

### FileManager {#filemanager}

0x48 bytes, tag `FileManager`, global `0x005e5340`, confirmed (code) at `0x001547b0` and `0x001548c0`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | the stream file system (device `+0x94`) |
| `+0x04` | stream file of the request in flight |
| `+0x08` | busy (a request is in flight) |
| `+0x0c` | last request id (ids count up from 1) |
| `+0x10`-`+0x38` | queue of requests (an STL deque: `+0x1c` front, `+0x2c` back) |
| `+0x3c` | **`File Stream Buffer`**: `0x60000` bytes (393,216), aligned to 128 |
| `+0x40` | its size |
| `+0x44` | vtable `0x00538348` |

A request is 0x54 bytes, confirmed (code) at `0x00154d50`:

| Offset | Meaning |
| --- | --- |
| `+0x00` | id |
| `+0x04` | name (64 bytes, without the `./ee_files/` prefix) |
| `+0x44` | destination; the `File Stream Buffer` when the caller passes 0 |
| `+0x48` | bytes to read; the buffer's size when the caller passes 0 |
| `+0x4c` | completion callback `(request*, user)` |
| `+0x50` | user value for the callback |

### FS_MemoryFile

0x18 bytes, vtable `0x00538220`, confirmed (code) at `0x001541e0`:

| Offset | Meaning |
| --- | --- |
| `+0x04` | owns the data (1 when it allocated it) |
| `+0x08` | data |
| `+0x0c` | size: the highest position written |
| `+0x10` | capacity |
| `+0x14` | position |

`0x001541e0(capacity)` allocates the data (tag `unsigned char`, align 16); `0x00154268(buffer, capacity, size)` wraps
an existing buffer without owning it. `Read` and `Write` copy and advance with no bounds check; `Write` raises the
size; `Seek`, `Tell`, `GetSize` and `GetData` are field accesses. Confirmed (code).

## Behaviour

### Opening the WAD at boot

`0x0040c5e0`, called from [initialisation](boot.md#initialisation-order), confirmed (code):

1. `new DVDWadIndex("cdrom0:\WARRIORS.DIR;1")`: open with the SCE file API (`sceOpen`, read-only), read the 4-byte
   count, seek to `0x10`, allocate `count × 12` bytes, read the entries, close. This is the only EE-side, blocking
   disc read the game makes through the standard file API.
2. Send the IOP command `0x1f` with the path `cdrom0:\WARRIORS.WAD;1` (`0x001500a8`): the game's IOP module opens the
   WAD and keeps it open for every later read. Command meaning inferred.

### Resolving a name {#resolving-a-name}

Confirmed (code) at `0x00148bf0`, `0x00148ba8`, `0x00148aa0`, `0x001490b8`:

1. The caller passes a bare file name, for example `level1.lev` or `warriors.glr`.
2. The stream file system prefixes it: `"./ee_files/" + name`, in a 256-byte stack buffer. All game files live in
   that one flat folder.
3. `DVDWadIndex::Find` lowercases the buffer in place, computes the CRC-32 of the whole path
   ([Name hashing](name-hash.md)) and scans the entries **linearly** for that hash. No match returns null.
4. `Exists` is "Find returned an entry"; `GetSize` is the entry's `size` (a missing name crashes, there is no check).

The `host0:` file system (`PS2FileSys`, root `debug/`) never sees game files; it is for debug output on a
development kit (`PS2DbgFile`, `sceOpen("host0:<name>")`, write mode `0x602` = create, truncate, write). Inferred
that a retail game never uses it.

### The IOP stream {#the-iop-stream}

Every WAD read is done by the game's own IOP module `IOP.IRX` (loaded at [boot](boot.md#device-initialisation)), not
by the EE. The EE side builds a command buffer and sends it (`0x0014da38` starts a command, `0x0014dcf8` adds a word,
`0x0014dc88` a half-word, `0x0014dd78` a string, `0x0014daa8` sends it). `IopStream::Start` (`0x00149410`) sends, for
a read of `length` bytes at byte `offset` of a WAD entry into EE memory at `dest`:

| Command | Arguments | Meaning (inferred) |
| --- | --- | --- |
| `0x29` | 0, `entry.wadOffset + offset`, `length` | set the source range in the open WAD |
| `0x31` | `dest` | set the EE destination |
| `0x01` | channel/mode words, `0x7e` | start the transfer |

`0x00150838` reads the stream's status; the stream file's `IsDone` is "not busy". The same module and command set
play the streamed audio (the `0x01` command with other modes); that belongs on an audio page. Confirmed (code) for the
command numbers and arguments; their meanings are inferred.

### Synchronous reads {#synchronous-reads}

The common path, used by level loading (`0x0040c688`, `0x0040d900`) and resource packs (`0x00187c38`). Confirmed
(code):

```text
fs   = device.currentFileSystem                    # FS_FSToStreamFSFileSys
file = fs.Open(name)                               # marks the one embedded file in use, position 0
s    = BufferedStream(file, fileManager.buffer, 0x60000)
ChunkSystem_LoadContainer(s)                       # or any reader of s
s.detach(); fs.Close(file)
```

`BufferedStream.Read(dst, n)` copies from its buffer and, whenever the buffer is empty, refills it with
`file.Read(buffer, min(bufferSize, fileSize - position))`. Reads are forward-only (`Seek` does nothing).

`FSToStreamFile::Read(dst, n)` (`0x00153fa8`) turns every refill into one complete IOP transfer:

```text
streamFile = streamFs.Open(name, dst, n, position)   # IOP read of n bytes at position
position  += n
streamFile.Wait()
streamFs.Close(streamFile)
if discErrorState == 3: push the error mode           # 0x005e5580, see below
```

`Wait` (`0x00148c90`) loops until `IsDone`: each pass it calls the stream file system's `Update`, yields
(`0x004aa9f0(0)`), and every 67 ms (`0x0050b720`) reads the pads (`0x001454a8`) and, if a callback is installed at
`0x0050b728`, calls it (for a "controller removed" screen during loading; inferred).

So a synchronous load of a file of size S costs `ceil(S / 384 KB)` blocking IOP transfers, each into the shared
`File Stream Buffer`, plus a copy into the chunk's own allocation.

### Asynchronous reads {#asynchronous-reads}

`FileManager_Request(fm, name, length, callback, user, dest)` (`0x00154d50`) fills a request (defaults as in the
table above), appends it to the queue, calls `FileManager_Service(fm, 0)` and returns the request id. When `dest` is
the shared buffer and `length` is larger than it, an assertion was compiled into a store through a null pointer
(the game would crash; inferred).

`FileManager_Service(fm, block)` (`0x00154ae0`), confirmed (code):

```text
if already servicing: return                        # re-entrancy guard 0x0050c64c
streamFs.Update()
do:
    if busy and block: inFlight.Wait()
    if not busy:
        if queue empty: break
        r = queue.front
        inFlight = streamFs.Open(r.name, r.dest, r.length, 0); busy = inFlight != null
    else if inFlight.IsDone():
        if discErrorState == 3: push the error mode (only when blocking); return
        r = pop queue.front
        r.callback(&r, r.user)
        busy = false; streamFs.Close(inFlight)
while block
```

So without `block` each call starts at most one read or finishes at most one; with `block` it drains the whole queue.
Every game-mode frame calls it once without blocking (step 11 of [one frame](boot.md#one-frame)); loaders that need
the data at once call it with `block = 1` right after their request (for example `0x0016e8f0`, `0x003535e8`).

Callbacks usually parse the buffer they were given by wrapping it in an `FS_MemoryFile` and running the
[chunk system](chunk-system.md) on it (the resource manager's `0x00186710` does this). Because the default
destination is the shared `File Stream Buffer`, the data must be consumed inside the callback.

The two paths share the buffer and the stream file system's single slot. A synchronous read started while a queued
read is in flight would get no stream file (`Open` returns null) and crash; the code avoids this by draining the
queue first (inferred from the callers; no lock exists).

### Disc errors

`0x005e5580` holds a device error state; the value 3 makes the readers push the error mode (`0x00157930(errorMode,
1, 3)`). Where the value is set (the IOP client's status handling) is open. Confirmed (code) for the checks.

### Buffer sizes and limits

| What | Size | Evidence |
| --- | --- | --- |
| `File Stream Buffer` | `0x60000` bytes (384 KB), 128-byte aligned | confirmed (code) at `0x001548c0` |
| name buffer for a WAD path | 256 bytes, `./ee_files/` prefix included | confirmed (code) |
| request name | 64 bytes | confirmed (code) |
| stream file records | 10, but only one open at a time | confirmed (code) |
| files open on the default file system | 1 | confirmed (code) |
| pad service interval while waiting | 67 ms | confirmed (code) at `0x0050b720` |

## Coney's implementation

Written from this page and [WARRIORS.DIR / .WAD](formats/wad-dir.md), in `src/fileio/` (namespace `coney::io`):

- `stream.h`: the file interface as `Stream` (`read`, `seek`, `tell`, `size`, `skip`), where a read delivers every
  byte or fails with `coney::Error`; `MemoryStream` over a buffer (the read side of `FS_MemoryFile`); `SubStream`, a
  window of another stream that the chunk system hands to stream readers.
- `file_stream.h`: `FileStream`, a byte range of a host file read on demand with standard C++ file I/O.
- `disc.h`: `Disc`, the player's disc as a folder or an ISO 9660 image (primary volume descriptor and root directory
  only, 2048-byte sectors), with files found by name in any letter case, as `coney-tools` does.
- `wad_index.h`: `WadIndex`, the parsed `WARRIORS.DIR` with lookup by name (`./ee_files/` prefix, lowercased CRC-32)
  through a hash table, and by hash. It refuses a file whose size is not `16 + count x 12` and an entry that ends
  past the end of `WARRIORS.WAD`.
- `wad.h`: `Wad`, the disc plus its index; `lookup` takes a name or a `0x` hash and `openEntry` returns a stream over
  one entry. Reads are synchronous; there is no IOP, no 384 KB staging buffer and no one-file-open limit.

Not done yet: the file-system interface (`Exists`, `GetSize`, `Open` by name for the game's own callers), the
`BufferedStream` and the asynchronous `FileManager` queue. None is needed until a subsystem loads files by itself.

TODO for the analysts, found while implementing: the WAD object's constructor at `0x0040c5e0` and the
`FS_MemoryFile` functions have no names on this page, so Coney's equivalents carry no `@orig` tag; `Stream_SkipBytes`
is cited with file `(unknown)`.

## Open questions

- Where the disc error state `0x005e5580` is set, and what values other than 3 mean.
- The exact `IOP.IRX` command protocol (command words, the channel argument of `0x01`, status layout at
  `0x00150838`); only needed if Coney ever emulates the IOP side, which it does not plan to.
- Whether any game file is read from outside `./ee_files/` through the stream file system (movies use
  `cdrom0:\%s;1` through Bink, and the sound banks are opened by the IOP from `cdrom0:\IOP\`).
- `RockWadIndexPS2.cpp` has a path string but no code: a second WAD index format, compiled out.
