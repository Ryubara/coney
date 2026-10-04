# Compiler & code conventions

Observations about how `SLUS_212.15` was compiled. These affect how decompiled code should be read.

* **GCC** (Sony/SN `ee-gcc` toolchain): `.gcc_except_table`, `.reginfo`, `.DVP.overlay` sections.
* **C++ exceptions enabled** (`.gcc_except_table`); `bad_alloc` strings present.
* **No RTTI type-name strings** were found in the game's own code, which suggests `-fno-rtti`. The only ones in the
  executable belong to the linked C++ runtime (`__si_type_info`, `__class_type_info`, `filebuf`, ...), whose
  `__*_type_info` class family is GCC 2.x's (see [Source map](source-map.md#middleware)).
* **Vtable layout is GCC 2.x style** (thunk-free): each vtable slot is an 8-byte pair `{int16 delta; int16 pad; void* fn}`.
  A virtual call looks like:

  ```c
  vt = *obj;
  ((fn)(*(void**)(vt + 0x54)))(obj + *(int16_t*)(vt + 0x50), ...);
  ```

  Each slot is an 8-byte `{delta, fn}` pair. This is the GCC 2.x layout, which points to **GCC 2.95.x**. The exact
  version and the vtable header size are not confirmed yet.
* The memory allocator is a global object (`DAT_005127e4`) whose `Alloc` is called with
  `(name, size, align, file, line, ...)`, for example `("DVDWadEntry", n*12, 0x10, "…/DVDWadIndexPS2.cpp", 0x98)`.
  Each allocation names its source file and line, which helps place functions into their original source files.
* SCE SDK 3.0.0 (`PsIIlibkernl3000`, `PsIIlibgraph3000`).
