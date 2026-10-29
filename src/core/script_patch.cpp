// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace coney::patch {

namespace {

// The width the writer pads a key to, so the `=` signs line up as in the format's description.
constexpr std::size_t kKeyWidth = 6;

// Whether `c` is a byte the writer never writes bare: a control character other than a tab, or DEL.
bool isControl(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return (byte < 0x20U && c != '\t') || byte == 0x7fU;
}

// `text` in double quotes, with a backslash, a quote and every control character escaped.
std::string quote(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\n') {
            out += "\\n";
        } else if (isControl(c)) {
            out += std::format("\\x{:02x}", static_cast<unsigned char>(c));
        } else {
            out.push_back(c);
        }
    }
    out.push_back('"');
    return out;
}

// A line range as the file writes it: `5`, `5-9` or `5-`.
std::string rangeText(const LineRange& range) {
    if (!range.last) {
        return std::format("{}-", range.first);
    }
    if (*range.last == range.first) {
        return std::format("{}", range.first);
    }
    return std::format("{}-{}", range.first, *range.last);
}

// Writes a rebuilt line's pieces after its `~ n`.
void writePieces(std::string& out, const std::vector<Piece>& pieces) {
    for (const Piece& piece : pieces) {
        out.push_back(' ');
        if (const auto* columns = std::get_if<ColumnRange>(&piece)) {
            out += columns->last ? std::format("[{}-{}]", columns->first, *columns->last)
                                 : std::format("[{}-]", columns->first);
        } else {
            out += quote(std::get<std::string>(piece));
        }
    }
}

// Writes one operation's line.
void writeOperation(std::string& out, const Operation& op) {
    switch (op.kind) {
    case OpKind::Keep:
        out += "= " + rangeText(op.lines);
        break;
    case OpKind::Drop:
        out += std::format("- {} {}", rangeText(op.lines), hashText(op.hash));
        break;
    case OpKind::Add: {
        // Bare text unless a quote is needed: a control character would not survive the file, and a space or tab at
        // the end is what editors and the repository's hooks strip.
        const bool bare =
            std::ranges::none_of(op.text, isControl) && !op.text.ends_with(' ') && !op.text.ends_with('\t');
        if (op.text.empty()) {
            out += "+";
        } else if (bare) {
            out += "+ " + op.text;
        } else {
            out += "+" + quote(op.text);
        }
        break;
    }
    case OpKind::Copy:
        out += "< " + rangeText(op.lines);
        break;
    case OpKind::Borrow:
    case OpKind::Rebuild:
        out += std::format("{} {}", op.kind == OpKind::Rebuild ? '~' : '^', op.lines.first);
        writePieces(out, op.pieces);
        break;
    }
    out.push_back('\n');
}

// Writes one `key = value` header line, the key padded so the values line up.
void writeKey(std::string& out, std::string_view key, std::string_view value) {
    out += key;
    out.append(kKeyWidth - std::min(kKeyWidth, key.size()), ' ');
    out += " = ";
    out += value;
    out.push_back('\n');
}

} // namespace

const PatchSection* ScriptPatch::sectionForBase(const Sha256Digest& base) const {
    const auto found = std::ranges::find(sections, base, &PatchSection::base);
    return found == sections.end() ? nullptr : &*found;
}

const PatchSection* ScriptPatch::sectionForChunk(const Sha256Digest& chunk) const {
    const auto found = std::ranges::find(sections, chunk, &PatchSection::chunk);
    return found == sections.end() ? nullptr : &*found;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.push_back(text.substr(start));
            return lines;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::expected<std::vector<std::string_view>, PatchFailure> textLines(std::string_view text, std::string_view what) {
    if (text.size() > kMaxTextBytes) {
        return std::unexpected(
            PatchFailure{PatchFault::TooLarge, std::format("{} is larger than {} bytes", what, kMaxTextBytes)});
    }
    auto lines = splitLines(text);
    if (lines.size() > kMaxTextLines) {
        return std::unexpected(
            PatchFailure{PatchFault::TooLarge, std::format("{} has more than {} lines", what, kMaxTextLines)});
    }
    return lines;
}

Sha256Digest textHash(std::string_view text) {
    Sha256 hash;
    hash.update(text);
    return hash.finish();
}

Sha256Digest linesHash(std::span<const std::string_view> lines) {
    Sha256 hash;
    for (const std::string_view line : lines) {
        hash.update(line);
        hash.update(std::string_view("\n"));
    }
    return hash.finish();
}

std::string writeScriptPatch(const ScriptPatch& patch) {
    std::string out = std::format("{} {}\n", kMagic, kFormatVersion);
    for (std::size_t i = 0; i < patch.sections.size(); ++i) {
        const PatchSection& section = patch.sections[i];
        if (i > 0) {
            out.push_back('\n');
        }
        if (section.entry) {
            writeKey(out, "entry", std::format("0x{:08x}", *section.entry));
        }
        if (section.name) {
            writeKey(out, "name", quote(*section.name));
        }
        writeKey(out, "chunk", quote(hashText(section.chunk)));
        writeKey(out, "form", quote(section.form));
        writeKey(out, "base", quote(hashText(section.base)));
        writeKey(out, "result", quote(hashText(section.result)));
        out += "---\n";
        for (const Operation& op : section.operations) {
            writeOperation(out, op);
        }
    }
    return out;
}

std::string hashText(const Sha256Digest& digest) { return "sha256:" + toHex(digest); }

} // namespace coney::patch
