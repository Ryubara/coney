# SPDX-License-Identifier: GPL-3.0-or-later
"""A light reader of Coney's C++ for the `orig` commands: functions, the comments above them and their `@orig` tags.

It is not a C++ parser. It walks the text once, skipping comments, string and character literals and preprocessor
lines, keeps a stack of the braces it is inside (namespace, class, function, other) and treats the code between two
`;`, `{` or `}` as one statement. A statement that ends in `{` at namespace or class level and has a name followed by
`(` is a function definition; one that ends in `;` there is a declaration. The comments met before a statement are
its comment block, where a doc comment and its `@orig` tags live (docs/guides/conventions.md#comments-and-orig).
That is enough for clang-formatted code, which is all `src/` holds; a construct it misreads costs one wrong row in a
report, never a wrong answer from a check that passes.
"""

from __future__ import annotations

import re
from dataclasses import dataclass, field

#: `@orig 0x<address> <OriginalName> (<File>.cpp)`; the file part may be missing on a malformed tag.
ORIG_TAG = re.compile(
    r"@orig\s+0x(?P<address>[0-9a-fA-F]{8})\b[ \t]*(?P<name>[^\n(]*?)[ \t]*(?:\((?P<file>[^()\n]*)\))?[ \t]*$"
)
#: The marker a Coney-only function or file carries (docs/guides/conventions.md#coney-only): `@coney-only <reason>`.
CONEY_ONLY_TAG = re.compile(r"@coney-only\b")
#: Wording the conventions asked for before the marker existed: "Coney's own ...", "(no @orig)".
LEGACY_CONEY_ONLY = re.compile(r"Coney's own|Coney-only|\(no @orig\)|no @orig\b|the original has (?:nothing|no such)")
RESEARCH_LINK = re.compile(r"docs/research/[A-Za-z0-9_./-]+?\.md(?:#[A-Za-z0-9_-]+)?")

_NAME_BEFORE_PAREN = re.compile(
    r"((?:[A-Za-z_]\w*\s*(?:<[^<>;{}()]*>)?\s*::\s*)*(?:operator\s*(?:\(\)|\[\]|[^\s\w(]+|\w+)|~?[A-Za-z_]\w*))\s*\("
)
_NAMESPACE = re.compile(r"^(?:inline\s+)?namespace\b\s*([\w:]*)")
_EXTERN_BLOCK = re.compile(r'^extern\s*""$')
_CLASS = re.compile(
    r"^(?:template\s*<.*>\s*)?(?:class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?(?:\[\[[^\]]*\]\]\s*)?(\w+)"
)
_ENUM = re.compile(r"^(?:enum)\b")
_ACCESS = re.compile(r"\b(?:public|private|protected)\s*:(?!:)")
_INIT_LIST = re.compile(r"\)\s*(?:noexcept\s*)?:(?!:)")
_TRAILING_WORDS = frozenset({"const", "override", "final", "noexcept", "mutable", "volatile"})
_NOT_FUNCTIONS = frozenset(
    {"if", "for", "while", "switch", "catch", "return", "sizeof", "decltype", "alignof", "static_assert", "alignas"}
)


@dataclass(frozen=True)
class Tag:
    """One `@orig` tag: the original function a piece of Coney code reimplements."""

    address: int
    name: str
    file: str  # the original's source file, "" when the tag has none
    line: int


@dataclass
class Function:
    """A function Coney's source declares or defines."""

    path: str  # repository-relative, with forward slashes
    line: int  # the line its statement starts on
    scope: str  # enclosing classes and explicit qualifier, "Brain" or "Outer::Inner"; "" for a free function
    name: str
    body: bool  # a definition with a body, not only a declaration
    helper: bool  # in an anonymous namespace, or `static` at namespace level: a file's private helper
    in_class: bool  # defined inside its class's body (a header's inline member)
    lines: int = 1  # lines from the statement's start to the closing brace (1 for a declaration)
    comment: str = ""  # the comment block above it
    tags: list[Tag] = field(default_factory=list)  # tags in that comment block
    body_tags: list[Tag] = field(default_factory=list)  # tags in comments inside its body

    @property
    def qualified(self) -> str:
        """`Scope::name`, or the bare name of a free function."""
        return f"{self.scope}::{self.name}" if self.scope else self.name

    @property
    def key(self) -> tuple[str, str]:
        """What a declaration and its out-of-class definition share."""
        return (self.scope, self.name)


@dataclass
class SourceFile:
    """What the reader found in one file."""

    path: str
    header_comment: str  # the comment block before the file's first code
    functions: list[Function]
    loose_tags: list[Tag]  # tags in no function's comment or body: a file's or a class's header comment
    tag_owner: dict[int, str]  # tag line -> the qualified name of what it documents, for loose tags too
    class_comments: dict[str, str] = field(default_factory=dict)  # "Outer::Inner" -> the comment above the class


def parse_tags(comment: str, first_line: int) -> list[Tag]:
    """Every `@orig` tag in a comment that starts on `first_line`."""
    tags = []
    for offset, text in enumerate(comment.splitlines()):
        match = ORIG_TAG.search(text)
        if match:
            name = match["name"].strip()
            tags.append(Tag(int(match["address"], 16), name, (match["file"] or "").strip(), first_line + offset))
    return tags


def _clean_statement(text: str) -> str:
    """A statement's code on one line, access specifiers and attributes dropped."""
    text = _ACCESS.sub(" ", text)
    text = re.sub(r"\[\[[^\]]*\]\]", " ", text)
    return " ".join(text.split())


def _function_name(statement: str) -> tuple[str, str] | None:
    """The (qualifier, name) of the function a statement declares or defines, or None when it is not one."""
    match = _NAME_BEFORE_PAREN.search(statement)
    if not match:
        return None
    before = statement[: match.start()]
    if "=" in before or "(" in before or before.rstrip().endswith((".", "->", "return")):
        return None  # an initializer, a lambda, a call, or a macro's argument list
    parts = [re.sub(r"<[^<>]*>", "", p).strip() for p in re.split(r"\s*::\s*", match.group(1))]
    name = parts[-1]
    if name in _NOT_FUNCTIONS or not name:
        return None
    return "::".join(parts[:-1]), name


@dataclass
class _Frame:
    """One open brace: what it opened and, for a function, the Function."""

    kind: str  # namespace, anon, class, function, init, block
    name: str = ""
    function: Function | None = None


class _Reader:
    """The single pass over one file (see the module's docstring)."""

    def __init__(self, path: str, text: str) -> None:
        self.path = path
        self.text = text
        self.stack: list[_Frame] = []
        self.functions: list[Function] = []
        self.loose: list[Tag] = []
        self.owner: dict[int, str] = {}
        self.class_comments: dict[str, str] = {}
        self.statement: list[str] = []
        self.statement_line = 0
        self.lead: list[tuple[int, str]] = []  # comments before the statement: (first line, text)
        self.header_comment: str | None = None
        self.line = 1

    # --- context --------------------------------------------------------------------------------------------------

    def _in_function(self) -> Function | None:
        """The innermost function whose body the reader is in."""
        for frame in reversed(self.stack):
            if frame.kind == "function":
                return frame.function
        return None

    def _at_declaration_level(self) -> bool:
        """True at namespace or class level, where statements declare things."""
        return all(f.kind in ("namespace", "anon", "class") for f in self.stack)

    def _class_scope(self) -> list[str]:
        """The classes the reader is inside, outermost first."""
        return [f.name for f in self.stack if f.kind == "class"]

    # --- comments -------------------------------------------------------------------------------------------------

    def _comment(self, first_line: int, text: str) -> None:
        """Route a comment: into the statement's lead block, a function's body, or the file's header."""
        if self.header_comment is None and not self.functions and not self.statement and not self.stack:
            self.lead.append((first_line, text))
            return
        inside = self._in_function()
        if inside is not None and not self._at_declaration_level():
            tags = parse_tags(text, first_line)
            inside.body_tags.extend(tags)
            for tag in tags:
                self.owner[tag.line] = inside.qualified
            return
        if not _clean_statement("".join(self.statement)):
            self.lead.append((first_line, text))

    def _take_lead(self) -> tuple[str, list[Tag]]:
        """The lead comment block and its tags, emptied."""
        text = "\n".join(t for _, t in self.lead)
        tags = [tag for first, t in self.lead for tag in parse_tags(t, first)]
        self.lead = []
        return text, tags

    def _drop_lead(self, owner: str) -> None:
        """Lead comments that document no function (a class, a variable, the file): keep their tags as loose."""
        if self.header_comment is None and not self.stack and not self.functions:
            self.header_comment = "\n".join(t for _, t in self.lead)
        _, tags = self._take_lead()
        self.loose.extend(tags)
        for tag in tags:
            self.owner[tag.line] = owner

    # --- statements -----------------------------------------------------------------------------------------------

    def _new_function(self, statement: str, body: bool) -> Function | None:
        """A Function for the statement, or None when it is not one."""
        named = _function_name(statement)
        if named is None:
            return None
        qualifier, name = named
        scope = "::".join([*self._class_scope(), qualifier] if qualifier else self._class_scope())
        helper = any(f.kind == "anon" for f in self.stack) or (
            not self._class_scope()
            and re.match(r"^(?:template\s*<.*?>\s*)?(?:inline\s+)?static\b", statement) is not None
        )
        comment, tags = self._take_lead()
        function = Function(
            self.path, self.statement_line, scope, name, body, helper, bool(self._class_scope()), 1, comment, tags
        )
        for tag in tags:
            self.owner[tag.line] = function.qualified
        self.functions.append(function)
        return function

    def _reset_statement(self) -> None:
        """Start a new statement."""
        self.statement = []
        self.statement_line = 0

    def _open(self) -> None:
        """A `{`: decide what it opens."""
        raw = "".join(self.statement)
        statement = _clean_statement(raw)
        if not self._at_declaration_level():
            self.stack.append(_Frame("block"))
            self._reset_statement()
            return
        namespace = _NAMESPACE.match(statement)
        if namespace:
            self._drop_lead("(namespace)")
            self.stack.append(_Frame("namespace" if namespace.group(1) else "anon", namespace.group(1)))
        elif _EXTERN_BLOCK.match(statement):
            self._drop_lead("(extern)")
            self.stack.append(_Frame("namespace"))
        elif _CLASS.match(statement) and "(" not in statement.split(":")[0]:
            match = _CLASS.match(statement)
            assert match is not None
            name = match.group(1)
            qualified = "::".join([*self._class_scope(), name])
            self.class_comments[qualified] = "\n".join(t for _, t in self.lead)
            self._drop_lead(qualified)
            self.stack.append(_Frame("class", name))
        elif _ENUM.match(statement):
            self._drop_lead("(enum)")
            self.stack.append(_Frame("block"))
        elif re.search(r"\w\s*$", statement) and (
            "(" not in statement or (_INIT_LIST.search(statement) and statement.split()[-1] not in _TRAILING_WORDS)
        ):
            # A brace initializer (`int m_x{0}`, `: m_a{1}`): the statement goes on after its `}`.
            self.stack.append(_Frame("init"))
            return
        else:
            function = self._new_function(statement, body=True)
            if function is None:
                self._drop_lead("(initializer)")
                self.stack.append(_Frame("block"))
            else:
                self.stack.append(_Frame("function", function.name, function))
        self._reset_statement()

    def _close(self) -> None:
        """A `}`: end what it closes."""
        frame = self.stack.pop() if self.stack else _Frame("block")
        if frame.kind == "init":
            self.statement.append("{}")
            return
        if frame.function is not None:
            frame.function.lines = self.line - frame.function.line + 1
        if frame.kind in ("namespace", "anon", "class"):
            self._drop_lead("(end)")
        self._reset_statement()

    def _end(self) -> None:
        """A `;`: a declaration at namespace or class level."""
        if self._at_declaration_level():
            statement = _clean_statement("".join(self.statement))
            if statement and self._new_function(statement, body=False) is None:
                self._drop_lead(statement[:40])
            elif not statement:
                self._drop_lead("(empty)")
        self._reset_statement()

    # --- the pass -------------------------------------------------------------------------------------------------

    def _code(self, char: str) -> None:
        """One character of code."""
        if not self.statement and not char.isspace():
            self.statement_line = self.line
        if self.statement or not char.isspace():
            self.statement.append(char)

    def run(self) -> SourceFile:
        """Read the whole file."""
        text, i, n = self.text, 0, len(self.text)
        at_line_start = True
        while i < n:
            c = text[i]
            if c == "\n":
                self.line += 1
                at_line_start = True
                self._code(" ")
                i += 1
                continue
            if at_line_start and c == "#":
                # A preprocessor line, with its continuations.
                while i < n and text[i] != "\n":
                    if text[i] == "\\" and i + 1 < n and text[i + 1] == "\n":
                        self.line += 1
                        i += 1
                    i += 1
                continue
            if not c.isspace():
                at_line_start = False
            if text.startswith("//", i):
                end = text.find("\n", i)
                end = n if end == -1 else end
                # Consecutive `//` lines are one comment block.
                self._comment(self.line, text[i:end])
                i = end
                continue
            if text.startswith("/*", i):
                end = text.find("*/", i + 2)
                end = n if end == -1 else end + 2
                block = text[i:end]
                self._comment(self.line, block)
                self.line += block.count("\n")
                i = end
                continue
            if c == '"' or (c == "R" and text.startswith('R"', i)):
                i = self._skip_string(i)
                continue
            if c == "'" and not (i > 0 and text[i - 1].isalnum() and text[i - 1] not in "uUL8"):
                i = self._skip_char(i)
                continue
            if c == "{":
                self._open()
            elif c == "}":
                self._close()
            elif c == ";":
                self._end()
            else:
                self._code(c)
            i += 1
        if self.header_comment is None:
            self.header_comment = "\n".join(t for _, t in self.lead)
        return SourceFile(self.path, self.header_comment, self.functions, self.loose, self.owner, self.class_comments)

    def _skip_string(self, i: int) -> int:
        """Skip a string literal starting at `i` (raw strings too); it stands in the statement as `""`."""
        text = self.text
        self._code('"')
        self._code('"')
        if text.startswith('R"', i):
            open_paren = text.find("(", i)
            delimiter = text[i + 2 : open_paren]
            end = text.find(")" + delimiter + '"', open_paren)
            end = len(text) if end == -1 else end + len(delimiter) + 2
            self.line += text.count("\n", i, end)
            return end
        j = i + 1
        while j < len(text) and text[j] != '"':
            j += 2 if text[j] == "\\" else 1
        return j + 1

    def _skip_char(self, i: int) -> int:
        """Skip a character literal starting at `i`."""
        j = i + 1
        while j < len(self.text) and self.text[j] != "'" and self.text[j] != "\n":
            j += 2 if self.text[j] == "\\" else 1
        self._code("0")
        return j + 1


def read_source(path: str, text: str) -> SourceFile:
    """Read one C++ file's text; `path` is how rows name it (repository-relative)."""
    return _Reader(path, text).run()
