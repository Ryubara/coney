// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/script_patch.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace coney::patch {

namespace {

// The header keys, in the order the writer writes them.
constexpr std::array<std::string_view, 6> kKeys{"entry", "name", "chunk", "form", "base", "result"};

// The width the writer pads a key to, so the `=` signs line up as in the format's description.
constexpr std::size_t kKeyWidth = 6;

// Whether `c` is a space or a tab: the only blanks between a line's fields.
bool isBlank(char c) { return c == ' ' || c == '\t'; }

// Whether `c` is a byte the writer never writes bare: a control character other than a tab, or DEL.
bool isControl(char c) {
    const auto byte = static_cast<unsigned char>(c);
    return (byte < 0x20U && c != '\t') || byte == 0x7fU;
}

// A refusal of the file, at its line `line`.
std::unexpected<Error> invalidAt(std::size_t line, std::string_view what) {
    return fail(ErrorCode::Invalid, std::format("line {}: {}", line, what));
}

// Reads one field after another from a line of the file, left to right: what the reader's parsers share.
class Fields {
  public:
    explicit Fields(std::string_view text) : m_text(text) {}

    // Skips spaces and tabs.
    void skipBlanks() {
        while (m_at < m_text.size() && isBlank(m_text[m_at])) {
            ++m_at;
        }
    }
    // Whether only blanks, or blanks and a `#` comment, are left.
    [[nodiscard]] bool atEnd() {
        skipBlanks();
        return m_at == m_text.size() || m_text[m_at] == '#';
    }
    // The next byte, or 0 at the end.
    [[nodiscard]] char peek() const { return m_at < m_text.size() ? m_text[m_at] : '\0'; }
    // Takes `c` if it comes next.
    bool take(char c) {
        if (peek() != c || m_at == m_text.size()) {
            return false;
        }
        ++m_at;
        return true;
    }
    // The lower-case letters and hyphens that come next: a header key.
    std::string_view keyName() {
        const std::size_t start = m_at;
        while (m_at < m_text.size() && ((m_text[m_at] >= 'a' && m_text[m_at] <= 'z') || m_text[m_at] == '-')) {
            ++m_at;
        }
        return m_text.substr(start, m_at - start);
    }
    // The rest of the line, as it is.
    [[nodiscard]] std::string_view rest() const { return m_text.substr(m_at); }
    // The bytes up to the next blank or the end.
    std::string_view word() {
        const std::size_t start = m_at;
        while (m_at < m_text.size() && !isBlank(m_text[m_at])) {
            ++m_at;
        }
        return m_text.substr(start, m_at - start);
    }
    // A decimal number of at most nine digits; nothing when no digit comes next.
    std::optional<std::uint32_t> number() {
        std::size_t end = m_at;
        while (end < m_text.size() && end - m_at < 10 && m_text[end] >= '0' && m_text[end] <= '9') {
            ++end;
        }
        if (end == m_at || end - m_at > 9) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        std::from_chars(m_text.data() + m_at, m_text.data() + end, value);
        m_at = end;
        return value;
    }
    // A quoted string (the opening quote next), its escapes read; an error message when it is malformed.
    std::expected<std::string, std::string> quoted() {
        if (!take('"')) {
            return std::unexpected("a quoted string was expected");
        }
        std::string out;
        while (m_at < m_text.size()) {
            const char c = m_text[m_at++];
            if (c == '"') {
                return out;
            }
            if (isControl(c)) {
                return std::unexpected("a control character inside a quoted string must be escaped");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (m_at == m_text.size()) {
                break;
            }
            const char e = m_text[m_at++];
            if (e == '\\' || e == '"') {
                out.push_back(e);
            } else if (e == 't') {
                out.push_back('\t');
            } else if (e == 'r') {
                out.push_back('\r');
            } else if (e == 'n') {
                out.push_back('\n');
            } else if (e == 'x' && m_at + 2 <= m_text.size()) {
                unsigned value = 0;
                const auto [end, error] = std::from_chars(m_text.data() + m_at, m_text.data() + m_at + 2, value, 16);
                if (error != std::errc{} || end != m_text.data() + m_at + 2) {
                    return std::unexpected("\\x needs two hex digits");
                }
                out.push_back(static_cast<char>(value));
                m_at += 2;
            } else {
                return std::unexpected(std::format("unknown escape \\{}", e));
            }
        }
        return std::unexpected("the quoted string has no closing quote");
    }

  private:
    std::string_view m_text;
    std::size_t m_at = 0;
};

// Reads `sha256:` and 64 hex digits.
std::optional<Sha256Digest> parseHash(std::string_view text) {
    constexpr std::string_view kPrefix = "sha256:";
    if (!text.starts_with(kPrefix)) {
        return std::nullopt;
    }
    return parseSha256Hex(text.substr(kPrefix.size()));
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

// The parse of one `.cpatch` file, section by section: readScriptPatch()'s state.
class Reader {
  public:
    // Reads the whole file.
    std::expected<ScriptPatch, Error> read(std::string_view text) {
        if (text.size() > kMaxPatchBytes) {
            return fail(ErrorCode::Invalid, std::format("the patch is larger than {} bytes", kMaxPatchBytes));
        }
        std::size_t lineNumber = 0;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t end = std::min(text.find('\n', start), text.size());
            std::string_view line = text.substr(start, end - start);
            // A checkout that turned the file's newlines into CRLF must not change what it says.
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            ++lineNumber;
            if (auto done = readLine(lineNumber, line); !done) {
                return std::unexpected(std::move(done.error()));
            }
            start = end + 1;
        }
        return finish();
    }

  private:
    // Reads one line of the file, by where the reader is.
    std::expected<void, Error> readLine(std::size_t number, std::string_view line) {
        if (number == 1) {
            return readMagic(line);
        }
        Fields fields(line);
        if (fields.atEnd()) {
            return {}; // a blank or comment line, anywhere
        }
        const char first = fields.peek();
        if (first >= 'a' && first <= 'z') {
            if (!m_inHeader) {
                if (auto started = startSection(); !started) {
                    return started;
                }
            }
            return readKey(number, fields);
        }
        if (m_inHeader) {
            if (fields.rest() == "---" || fields.rest().starts_with("--- ") || fields.rest().starts_with("---#")) {
                return endHeader(number);
            }
            return invalidAt(number, "a header line is `key = value`, and the header ends with `---`");
        }
        if (!m_inSection) {
            return invalidAt(number, "an operation before any section's header");
        }
        return readOperation(number, fields);
    }

    // Reads `coney-script-patch 1`.
    std::expected<void, Error> readMagic(std::string_view line) {
        Fields fields(line);
        if (fields.word() != kMagic) {
            return invalidAt(1, "this is not a Coney script patch: it does not start with `coney-script-patch`");
        }
        fields.skipBlanks();
        const auto version = fields.number();
        if (!version || !fields.atEnd()) {
            return invalidAt(1, "`coney-script-patch` is followed by the format version");
        }
        if (*version != kFormatVersion) {
            return invalidAt(1, std::format("format version {} is not one this Coney reads (it reads {})", *version,
                                            kFormatVersion));
        }
        return {};
    }

    // Closes the section being read, if any, and opens the next.
    std::expected<void, Error> startSection() {
        if (m_patch.sections.size() == kMaxSections) {
            return fail(ErrorCode::Invalid, std::format("the patch has more than {} sections", kMaxSections));
        }
        m_patch.sections.emplace_back();
        m_inHeader = true;
        m_inSection = true;
        m_seen = {};
        m_cursor = 1;
        m_toEnd = false;
        return {};
    }

    // Reads one `key = value` line of a header.
    std::expected<void, Error> readKey(std::size_t number, Fields& fields) {
        const std::string_view key = fields.keyName();
        if (!m_inHeader) {
            return invalidAt(number, "a header line among the operations");
        }
        const auto known = std::ranges::find(kKeys, key);
        if (known == kKeys.end()) {
            return invalidAt(number, std::format("unknown key {}", key));
        }
        const auto index = static_cast<std::size_t>(known - kKeys.begin());
        if (m_seen[index]) {
            return invalidAt(number, std::format("{} given twice", key));
        }
        m_seen[index] = true;
        fields.skipBlanks();
        if (!fields.take('=')) {
            return invalidAt(number, std::format("{} is followed by `=` and its value", key));
        }
        fields.skipBlanks();
        PatchSection& section = m_patch.sections.back();
        if (key == "entry") {
            const std::string_view value = fields.word();
            std::uint32_t entry = 0;
            const bool hex = value.size() > 2 && value.size() <= 10 && value.starts_with("0x");
            const auto parsed = hex ? std::from_chars(value.data() + 2, value.data() + value.size(), entry, 16)
                                    : std::from_chars_result{value.data(), std::errc::invalid_argument};
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
                return invalidAt(number, "entry is `0x` and up to eight hex digits");
            }
            section.entry = entry;
        } else {
            auto value = fields.quoted();
            if (!value) {
                return invalidAt(number, std::format("{}: {}", key, value.error()));
            }
            if (key == "name") {
                section.name = std::move(*value);
            } else if (key == "form") {
                section.form = std::move(*value);
            } else {
                const auto hash = parseHash(*value);
                if (!hash) {
                    return invalidAt(number, std::format("{} is `\"sha256:` and 64 hex digits", key));
                }
                (key == "chunk" ? section.chunk : key == "base" ? section.base : section.result) = *hash;
            }
        }
        if (!fields.atEnd()) {
            return invalidAt(number, std::format("text after {}'s value", key));
        }
        return {};
    }

    // Ends a header at its `---` line: every key but entry and name must have been given.
    std::expected<void, Error> endHeader(std::size_t number) {
        for (std::size_t i = 2; i < kKeys.size(); ++i) {
            if (!m_seen[i]) {
                return invalidAt(number, std::format("the section's header has no {}", kKeys[i]));
            }
        }
        m_inHeader = false;
        return {};
    }

    // Reads a line range, `a`, `a-b` or (when `open` is allowed) `a-`.
    std::expected<LineRange, std::string> readRange(Fields& fields, bool open) {
        const auto first = fields.number();
        if (!first || *first == 0) {
            return std::unexpected("a base line number, from 1, was expected");
        }
        LineRange range{*first, *first};
        if (fields.take('-')) {
            range.last = fields.number();
            if (!range.last) {
                if (!open) {
                    return std::unexpected("only a keep may run to the end (`a-`)");
                }
            } else if (*range.last < *first) {
                return std::unexpected(std::format("the range {}-{} runs backwards", *first, *range.last));
            }
        }
        if (range.last.value_or(*first) > kMaxTextLines) {
            return std::unexpected(std::format("base lines stop at {}", kMaxTextLines));
        }
        return range;
    }

    // Reads a rebuilt line's pieces: `[a-b]`, `[a-]` and quoted text, columns in order.
    std::expected<std::vector<Piece>, std::string> readPieces(Fields& fields) {
        std::vector<Piece> pieces;
        std::optional<std::uint32_t> next = 0; // the first column a base piece may start at; unset after `[a-]`
        while (!fields.atEnd()) {
            if (pieces.size() == kMaxPieces) {
                return std::unexpected(std::format("a rebuilt line has at most {} pieces", kMaxPieces));
            }
            if (fields.peek() == '"') {
                auto text = fields.quoted();
                if (!text) {
                    return std::unexpected(text.error());
                }
                pieces.emplace_back(std::move(*text));
                continue;
            }
            if (!fields.take('[')) {
                return std::unexpected("a piece is `[a-b]`, `[a-]` or quoted text");
            }
            ColumnRange columns;
            const auto first = fields.number();
            if (!first || !fields.take('-')) {
                return std::unexpected("a piece's columns are `[a-b]` or `[a-]`");
            }
            columns.first = *first;
            columns.last = fields.number();
            if (!fields.take(']')) {
                return std::unexpected("a piece's columns end with `]`");
            }
            if (columns.last && *columns.last < columns.first) {
                return std::unexpected(std::format("the columns {}-{} run backwards", columns.first, *columns.last));
            }
            if (!next || columns.first < *next) {
                return std::unexpected("a rebuilt line's columns come in order, each once");
            }
            next = columns.last ? std::optional<std::uint32_t>(*columns.last + 1) : std::nullopt;
            pieces.emplace_back(columns);
        }
        return pieces;
    }

    // Reads one operation and checks that it consumes base lines in order.
    std::expected<void, Error> readOperation(std::size_t number, Fields& fields) {
        PatchSection& section = m_patch.sections.back();
        if (section.operations.size() == kMaxOperations) {
            return invalidAt(number, std::format("a section holds at most {} operations", kMaxOperations));
        }
        Operation op;
        const char symbol = fields.peek();
        fields.take(symbol);
        if (symbol == '+') {
            op.kind = OpKind::Add;
            if (fields.take(' ')) {
                op.text = std::string(fields.rest());
                section.operations.push_back(std::move(op));
                return {};
            }
            if (fields.peek() == '"') {
                auto text = fields.quoted();
                if (!text) {
                    return invalidAt(number, text.error());
                }
                op.text = std::move(*text);
            } else if (!fields.rest().empty()) {
                return invalidAt(number, "an added line is `+ text` or `+\"quoted text\"`");
            }
        } else if (symbol == '=' || symbol == '-' || symbol == '<' || symbol == '~') {
            op.kind = symbol == '='   ? OpKind::Keep
                      : symbol == '-' ? OpKind::Drop
                      : symbol == '<' ? OpKind::Copy
                                      : OpKind::Rebuild;
            fields.skipBlanks();
            auto range = readRange(fields, op.kind == OpKind::Keep);
            if (!range) {
                return invalidAt(number, range.error());
            }
            op.lines = *range;
            if (op.kind == OpKind::Rebuild && op.lines.last != op.lines.first) {
                return invalidAt(number, "a rebuild names one base line");
            }
            if (op.kind == OpKind::Drop) {
                fields.skipBlanks();
                const auto hash = parseHash(fields.word());
                if (!hash) {
                    return invalidAt(number, "a dropped range is followed by `sha256:` and its 64 hex digits");
                }
                op.hash = *hash;
            }
            if (op.kind == OpKind::Rebuild) {
                auto pieces = readPieces(fields);
                if (!pieces) {
                    return invalidAt(number, pieces.error());
                }
                op.pieces = std::move(*pieces);
            }
        } else {
            return invalidAt(number, std::format("unknown operation `{}`", symbol));
        }
        if (!fields.atEnd()) {
            return invalidAt(number, "text after the operation");
        }
        if (auto ordered = checkOrder(number, op); !ordered) {
            return ordered;
        }
        section.operations.push_back(std::move(op));
        return {};
    }

    // Checks that a keep, drop or rebuild starts where the last one ended, and moves on past it.
    std::expected<void, Error> checkOrder(std::size_t number, const Operation& op) {
        if (op.kind == OpKind::Add || op.kind == OpKind::Copy) {
            return {};
        }
        if (m_toEnd) {
            return invalidAt(number, "after a keep to the end only added and copied lines may follow");
        }
        if (op.lines.first != m_cursor) {
            return invalidAt(number, std::format("base lines {} are out of order: base line {} comes next",
                                                 rangeText(op.lines), m_cursor));
        }
        if (!op.lines.last) {
            m_toEnd = true;
        } else {
            m_cursor = *op.lines.last + 1;
        }
        return {};
    }

    // Ends the file: the last section must have finished its header, and there must be one.
    std::expected<ScriptPatch, Error> finish() {
        if (m_inHeader) {
            return fail(ErrorCode::Truncated, "the file ends inside a section's header (no `---` line)");
        }
        if (m_patch.sections.empty()) {
            return fail(ErrorCode::Invalid, "the patch holds no section");
        }
        return std::move(m_patch);
    }

    ScriptPatch m_patch;
    std::array<bool, kKeys.size()> m_seen{};
    std::uint32_t m_cursor = 1;
    bool m_inHeader = false;
    bool m_inSection = false;
    bool m_toEnd = false;
};

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
        break;
    case OpKind::Rebuild:
        out += std::format("~ {}", op.lines.first);
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

std::expected<ScriptPatch, Error> readScriptPatch(std::string_view text) { return Reader().read(text); }

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
