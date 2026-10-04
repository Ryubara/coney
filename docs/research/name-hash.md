# Name hashing (CRC32)

The engine identifies names (WAD files and many other names; there are 120 call sites) with standard **CRC-32**:
reflected polynomial `0xEDB88320`, initial value `0xFFFFFFFF`, final XOR `0xFFFFFFFF`. This gives the same result as
`zlib.crc32`.

| Address | Role |
| --- | --- |
| `0x143ea0` | Build the 256-entry table (into `.bss` at `0x5d91e0`) |
| `0x143f68` | `crc32(table, const char*)` |
| `0x143fd8` | Lowercase a string in place (ctype table at `0x58c531`, `_U` flag) |

Callers lowercase the name before hashing, so all hashes are of **lowercase** strings.

```python
import zlib
def warriors_hash(name: str) -> int:
    return zlib.crc32(name.lower().encode("ascii"))
```

**Confidence:** confirmed from code (decompiled table generator and hash loop).

## Coney's implementation

`src/core/name_hash.h`: `crc32` (of bytes, or of a string as given), `lowercaseAscii` (A to Z only, so the hash
never depends on the host's locale) and `nameHash`, the CRC-32 of the lowercased string. The CRC table is built at
compile time. `coney::io::wadPath` adds the `./ee_files/` prefix for WAD lookups
([WARRIORS.DIR / .WAD](formats/wad-dir.md#name-lookup)). The Python equivalent is `name_hash` in
`python/src/coney_tools/wad.py`.
