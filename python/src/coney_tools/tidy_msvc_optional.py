# SPDX-License-Identifier: GPL-3.0-or-later
"""Lets the local clang-tidy on Windows see an unchecked `*optional` (docs/guides/clang-tidy.md#differences).

`bugprone-unchecked-optional-access` finds the accesses to model by the class that declares the member: `operator*`
must be a member of `std::optional` itself. Microsoft's `<optional>` declares the four `operator*` overloads in the base
class `_Optional_construct_base` and brings them into `optional` with `using _Mybase::operator*;`, so the check never
matches a `*value` there: `value.value()` and `value->x` are reported, `*value` is not, while CI's libstdc++ declares
`operator*` in `std::optional` and CI reports all three.

The fix keeps Microsoft's library and changes one line of a copy of it: `patched_header` writes `<optional>` with that
`using` replaced by the four overloads declared in `optional`, each calling the base's. The copy's folder is put on the
include path of every local clang-tidy command as a system folder (`-external:I`, as the build's own flags name its
dependencies), ahead of the toolset's, so every `#include <optional>`, the library's own included, reads it and no
diagnostic inside it is reported (a plain `-I` makes it a user header: its `-Wsign-compare` is then an error in the
tests that compare an optional with an int). Nothing but clang-tidy reads the copy (the build uses Microsoft's header),
and the check does not look inside the call, so it models the dereference as it does libstdc++'s. The copy is made from
the very header clang-tidy would read, found with the include list clang-tidy prints for the build's compiler
(`include_dirs`), and written to the build folder.

When Microsoft's header changes so that the line is not there, the check still runs as before and says so in a note;
`tests/test_tidy_msvc_optional.py` fails on a machine with such a toolset, so the copy is brought up to date.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field
from pathlib import Path

from coney_tools import ci_tidy

#: The header that holds the `operator*` overloads Microsoft's library keeps in a base class.
HEADER = "optional"
#: The one line of that header the copy changes: the base class's `operator*` brought into `std::optional`.
USING_LINE = "    using _Mybase::operator*;\n"
#: What replaces it: the same four overloads declared in `std::optional`, calling the base's (the check matches the
#: class that declares the operator, and does not look into its body).
OVERLOADS = (
    "    _NODISCARD constexpr _Ty& operator*() & noexcept { return _Mybase::operator*(); }\n"
    "    _NODISCARD constexpr const _Ty& operator*() const& noexcept { return _Mybase::operator*(); }\n"
    "    _NODISCARD constexpr _Ty&& operator*() && noexcept { return static_cast<_Ty&&>(_Mybase::operator*()); }\n"
    "    _NODISCARD constexpr const _Ty&& operator*() const&& noexcept {\n"
    "        return static_cast<const _Ty&&>(_Mybase::operator*());\n"
    "    }\n"
)
#: The folder of the build folder that holds the copy (`<build>/tidy-optional/optional`) and the probe.
FOLDER = "tidy-optional"
#: The lines of clang's `-v` output that bound the `#include <...>` search list.
_LIST_START = "#include <...> search starts here:"
_LIST_END = "End of search list."


@dataclass(frozen=True)
class OptionalModel:
    """What a local run does about Microsoft's `std::optional`: the folder to put on the include path, or None, and the
    notes for the reader (why there is none, when there is not)."""

    include: Path | None = None
    notes: list[str] = field(default_factory=list)

    def arguments(self) -> tuple[str, ...]:
        """The clang-tidy arguments that put the copy first on the include path; none without a copy."""
        return (f"--extra-arg=-external:I{self.include}",) if self.include is not None else ()


def patched_header(text: str) -> str | None:
    """Microsoft's `<optional>` with `operator*` declared in `std::optional`; None when `text` has no line to change
    (another library, or a version that declares it there already)."""
    if text.count(USING_LINE) != 1:
        return None
    return text.replace(USING_LINE, OVERLOADS)


def include_dirs(output: str) -> list[Path]:
    """The `#include <...>` search list of clang's `-v` output, in order; empty when the output has none."""
    lines = output.splitlines()
    try:
        first = lines.index(_LIST_START) + 1
        last = lines.index(_LIST_END, first)
    except ValueError:
        return []
    return [Path(line.strip()) for line in lines[first:last] if line.strip()]


def compiler_of(build_dir: Path) -> str | None:
    """The compiler (`cl.exe`) the first command of the build's compile database runs; None when it names none."""
    try:
        entries = json.loads((build_dir / "compile_commands.json").read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    for entry in entries if isinstance(entries, list) else []:
        if not isinstance(entry, dict):
            continue
        arguments = entry.get("arguments")
        if isinstance(arguments, list) and arguments:
            return str(arguments[0])
        command = str(entry.get("command", "")).strip()
        quoted = re.match(r'"([^"]+)"', command)
        if quoted:
            return quoted.group(1)
        if command:
            return command.split()[0]
    return None


def prepare(program: str, build_dir: Path, run: ci_tidy.Runner) -> OptionalModel:
    """Make the copy of Microsoft's `<optional>` for the build at `build_dir` (see the module docstring) and say where
    it is; no copy, with a note, when the include list or the header cannot be had or has changed shape. `run` runs
    clang-tidy `program`."""
    compiler = compiler_of(build_dir)
    if compiler is None:
        return _without("the compile database names no compiler")
    folder = build_dir / FOLDER
    probe = folder / "probe"
    probe.mkdir(parents=True, exist_ok=True)
    source = probe / "empty.cpp"
    source.write_text("", encoding="utf-8")
    entry = {
        "directory": str(probe),
        "command": f'"{compiler}" /nologo /TP -std:c++latest -c empty.cpp',
        "file": str(source),
    }
    (probe / "compile_commands.json").write_text(json.dumps([entry]), encoding="utf-8")
    # clang-tidy prints the include list of the compiler the build uses, which is where it reads <optional> from
    code, output = run(
        [
            program,
            "-p",
            str(probe),
            "--quiet",
            "--checks=-*,bugprone-unchecked-optional-access",
            "--extra-arg=-v",
            str(source),
        ],
        probe,
    )
    directories = include_dirs(output)
    original = next((path / HEADER for path in directories if (path / HEADER).is_file()), None)
    if original is None:
        return _without(f"clang-tidy (exit {code}) lists no include folder with <{HEADER}>")
    try:
        text = original.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        return _without(f"{original} cannot be read ({error})")
    patched = patched_header(text)
    if patched is None:
        return _without(
            f"{original} has no `{USING_LINE.strip()}` to change: either it declares operator* in std::optional "
            "now (then there is nothing to do) or python/src/coney_tools/tidy_msvc_optional.py follows it"
        )
    target = folder / HEADER
    try:
        if not target.is_file() or target.read_text(encoding="utf-8") != patched:
            target.write_text(patched, encoding="utf-8", newline="")
    except (OSError, UnicodeError) as error:
        return _without(f"{target} cannot be written ({error})")
    return OptionalModel(include=folder)


def _without(reason: str) -> OptionalModel:
    """No copy: the run goes on as before, and the note tells the reader what it then cannot see."""
    return OptionalModel(
        notes=[
            f"bugprone-unchecked-optional-access does not see an unchecked `*` of a std::optional here ({reason}); "
            "CI's does (docs/guides/clang-tidy.md#differences)"
        ]
    )
