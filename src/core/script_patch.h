// SPDX-License-Identifier: GPL-3.0-or-later
// The `.cpatch` script patch format of Coney Studio's mission mods: Coney's own, with no counterpart in the original.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "core/error.h"
#include "core/sha256.h"

/// Script patches: the differences between a game script's base text and a mod's edited copy, holding only the
/// author's own text and references to the base, so no game script text ever ships in a mod
/// (docs/guides/script-patches.md, the design: docs/guides/studio/mission-mods.md#patches).
namespace coney::patch {

/// The first word of a `.cpatch` file, before its format version.
inline constexpr std::string_view kMagic = "coney-script-patch";
/// The format version this Coney reads and writes.
inline constexpr int kFormatVersion = 1;

/// The largest `.cpatch` file read, in bytes.
inline constexpr std::size_t kMaxPatchBytes = 4U * 1024U * 1024U;
/// The largest text (a base text, an edited copy, a result) taken or made, in bytes: a game script's decompile is a
/// few hundred kilobytes at most, and the limit keeps a patch of copies from growing a text without bound.
inline constexpr std::size_t kMaxTextBytes = 4U * 1024U * 1024U;
/// The most lines a text may have.
inline constexpr std::size_t kMaxTextLines = 200'000;
/// The most sections (one per base) a `.cpatch` holds.
inline constexpr std::size_t kMaxSections = 16;
/// The most operations one section holds.
inline constexpr std::size_t kMaxOperations = 200'000;
/// The most pieces one rebuilt line is made of.
inline constexpr std::size_t kMaxPieces = 1024;

/// Base lines `first` to `last`, numbered from 1, both included. `last` unset means to the end of the base (only a
/// keep's range may be open).
struct LineRange {
    std::uint32_t first = 1;
    std::optional<std::uint32_t> last;
    bool operator==(const LineRange&) const = default;
};

/// Bytes `first` to `last` of a base line, numbered from 0, both included; `last` unset means to the end of the line.
struct ColumnRange {
    std::uint32_t first = 0;
    std::optional<std::uint32_t> last;
    bool operator==(const ColumnRange&) const = default;
};

/// One piece of a rebuilt line: columns of its base line, or text the author wrote.
using Piece = std::variant<ColumnRange, std::string>;

/// What one operation does. Keep, Drop and Rebuild consume base lines, in order; Add and Copy only write.
enum class OpKind : std::uint8_t {
    Keep,    ///< `= a-b`: base lines a to b, unchanged.
    Drop,    ///< `- a-b sha256:...`: base lines a to b left out, checked by their hash.
    Add,     ///< `+ text`: a line the author wrote.
    Copy,    ///< `< a-b`: base lines a to b written here (a block moved or duplicated); consumes nothing.
    Rebuild, ///< `~ n [a-b] "text" [c-]`: base line n replaced by a line made of its columns and the author's text.
    /// `^ n [a-b] "text" [c-]`: a line made of base line n's columns and the author's text, written here; consumes
    /// nothing (a line moved and also changed).
    Borrow,
};

/// One line of a section's operations.
struct Operation {
    OpKind kind = OpKind::Keep;
    LineRange lines;           ///< Keep, Drop, Copy: the base lines; Rebuild, Borrow: the one line (`first == last`).
    Sha256Digest hash{};       ///< Drop: linesHash() of the dropped lines.
    std::string text;          ///< Add: the line, without its newline.
    std::vector<Piece> pieces; ///< Rebuild, Borrow: the new line's pieces, in order.
    bool operator==(const Operation&) const = default;
};

/// The patch of one base text: what it applies to and its operations, in base-line order.
struct PatchSection {
    std::optional<std::uint32_t> entry; ///< The WAD entry the script lives in (its name's hash), when known.
    std::optional<std::string> name;    ///< The script's name (`level99.lua`), when known.
    Sha256Digest chunk{};               ///< The SHA-256 of the compiled chunk the base text was made from.
    std::string form;                   ///< The base text's form: `lua4 1` (decompile form 1).
    Sha256Digest base{};                ///< textHash() of the base text.
    Sha256Digest result{};              ///< textHash() of the text after the patch, checked after applying.
    std::vector<Operation> operations;
    bool operator==(const PatchSection&) const = default;
};

/// A `.cpatch` file: one section per base it fits (most hold one; a script that differs between regions takes one
/// each).
struct ScriptPatch {
    std::vector<PatchSection> sections;
    bool operator==(const ScriptPatch&) const = default;

    /// The section whose base text hash is `base`; null when there is none.
    [[nodiscard]] const PatchSection* sectionForBase(const Sha256Digest& base) const;
    /// The section made from the chunk whose hash is `chunk`; null when there is none.
    [[nodiscard]] const PatchSection* sectionForChunk(const Sha256Digest& chunk) const;
};

/// Why a patch could not be applied, made or merged. Callers branch on it: a wrong base stops a mod as "your script is
/// different", the others as a damaged patch (docs/guides/studio/mission-mods.md#refusals).
enum class PatchFault : std::uint8_t {
    WrongBase,     ///< The text is not the patch's base (its hash differs), or two patches have different bases.
    DamagedRange,  ///< A dropped range's lines do not have the hash the patch gives.
    DamagedResult, ///< The text the patch made does not have its result hash.
    DoesNotFit,    ///< An operation names a line or column the base does not have, or leaves base lines unaccounted.
    TooLarge,      ///< A text or the result passes kMaxTextBytes or kMaxTextLines.
};

/// A refused patch: the fault to branch on and one line for people.
struct PatchFailure {
    PatchFault fault = PatchFault::DoesNotFit;
    std::string message;
};

/// The lines of `text`, split at each `\n` (which no line includes): `"a\nb\n"` is `a`, `b` and an empty last line,
/// so joining them with `\n` gives the text back exactly. An empty text is one empty line.
[[nodiscard]] std::vector<std::string_view> splitLines(std::string_view text);

/// splitLines() of a text held to the limits every text is: PatchFault::TooLarge, its message naming the text as
/// `what` says (`the base text`), past kMaxTextBytes or kMaxTextLines.
[[nodiscard]] std::expected<std::vector<std::string_view>, PatchFailure> textLines(std::string_view text,
                                                                                   std::string_view what);

/// The SHA-256 of a whole text, as `base` and `result` give it.
[[nodiscard]] Sha256Digest textHash(std::string_view text);

/// The SHA-256 of lines each followed by `\n`, as a dropped range gives it.
[[nodiscard]] Sha256Digest linesHash(std::span<const std::string_view> lines);

/// Reads a `.cpatch` file (docs/guides/script-patches.md#format). Fails with ErrorCode::Invalid and the line at fault
/// for a file that breaks the format: an unknown version, a missing or repeated header key, a bad range, a hash that is
/// not 64 hex digits, an operation out of base-line order, more than a limit allows; ErrorCode::Truncated for a file
/// that ends inside a section's header. It checks the file alone; applyPatch() checks it against a base.
[[nodiscard]] std::expected<ScriptPatch, Error> readScriptPatch(std::string_view text);

/// Writes `patch` as a `.cpatch` file that readScriptPatch() reads back equal: a header per section, then its
/// operations, one a line. A `+` line is written as it is unless a quote is needed to keep it whole (a control
/// character, or a space or tab at its end, which editors strip).
[[nodiscard]] std::string writeScriptPatch(const ScriptPatch& patch);

/// `sha256:` and the digest in lower-case hex, as a `.cpatch` writes a hash.
[[nodiscard]] std::string hashText(const Sha256Digest& digest);

} // namespace coney::patch
