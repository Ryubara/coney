// SPDX-License-Identifier: GPL-3.0-or-later
// @coney-only Coney's own save container: the sections inside its frame and their codecs, shared by the frame
// (save_game.cpp) and the current version's section codecs (save_sections.cpp); not part of SaveGame's interface
// (docs/research/save.md#save-format)
#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/assert.h"
#include "core/error.h"
#include "fileio/reader.h"
#include "warriors/save_game.h"

namespace coney::save_sections {

/// The longest string the format holds: its length is one byte.
inline constexpr std::size_t kMaxString = 0xff;

/// The section whose payload starts with the profile's name in every version (save.md#save-format).
inline constexpr std::string_view kProfile = "profile";

/// One framed section: its name and payload.
struct Section {
    std::string name;               ///< 1-64 bytes of a-z, 0-9, '-' and ':'.
    std::vector<std::byte> payload; ///< The section's bytes, as framed.
};

/// Appends little-endian values to a growing payload.
class Writer {
  public:
    /// One byte.
    void u8(std::uint8_t value) { m_out.push_back(static_cast<std::byte>(value)); }
    /// A 32-bit value.
    void u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>(value >> shift));
        }
    }
    /// An IEEE 754 single by its bits, so every value (a NaN too) comes back exactly.
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    /// A flag: one byte, 0 or 1.
    void flag(bool value) { u8(value ? 1 : 0); }
    /// Raw bytes.
    void bytes(std::span<const std::uint8_t> values) {
        for (const std::uint8_t value : values) {
            u8(value);
        }
    }
    /// Raw bytes, as std::byte.
    void raw(std::span<const std::byte> values) { m_out.insert(m_out.end(), values.begin(), values.end()); }
    /// A string: its length in one byte, then its bytes. The caller keeps it to kMaxString.
    void string(std::string_view text) {
        CONEY_ASSERT(text.size() <= kMaxString);
        u8(static_cast<std::uint8_t>(text.size()));
        for (const char c : text) {
            u8(static_cast<std::uint8_t>(c));
        }
    }
    /// The payload written so far.
    [[nodiscard]] std::vector<std::byte> take() { return std::move(m_out); }

  private:
    std::vector<std::byte> m_out;
};

/// Reads one section's payload. The first failure is kept and every later read returns zero, so a decoder reads its
/// fields straight through and checks once at the end (finish).
class PayloadReader {
  public:
    /// A reader of `payload`, the section `section`, named in its errors.
    PayloadReader(std::string_view section, std::span<const std::byte> payload)
        : m_section(section), m_reader(payload) {}

    /// One byte.
    std::uint8_t u8() { return take(m_reader.readU8()).value_or(0); }
    /// A 32-bit value.
    std::uint32_t u32() { return take(m_reader.readU32Le()).value_or(0); }
    /// A single by its bits.
    float f32() { return std::bit_cast<float>(u32()); }
    /// A flag; any byte but 0 or 1 is damage.
    bool flag() {
        const std::uint8_t value = u8();
        if (value > 1) {
            invalid(std::format("a flag holds {}", value));
        }
        return value == 1;
    }
    /// Raw bytes into `out`.
    void bytes(std::span<std::uint8_t> out) {
        if (auto read = take(m_reader.readBytes(out.size())); read) {
            std::ranges::transform(*read, out.begin(), [](std::byte b) { return static_cast<std::uint8_t>(b); });
        }
    }
    /// Raw bytes, as many as asked.
    std::vector<std::byte> raw(std::size_t count) {
        auto read = take(m_reader.readBytes(count));
        return read ? std::vector<std::byte>(read->begin(), read->end()) : std::vector<std::byte>{};
    }
    /// A string of at most `longest` bytes and at least `shortest`.
    std::string string(std::size_t shortest, std::size_t longest) {
        const std::size_t length = u8();
        std::string text;
        if (auto read = take(m_reader.readBytes(length)); read) {
            std::ranges::transform(*read, std::back_inserter(text), [](std::byte b) { return static_cast<char>(b); });
        }
        if (length < shortest || length > longest) {
            invalid(std::format("a string of {} bytes, outside {}-{}", length, shortest, longest));
        }
        return text;
    }
    /// Bytes left unread.
    [[nodiscard]] std::size_t remaining() const { return m_reader.remaining(); }
    /// Whether every read so far was in bounds and within its rules.
    [[nodiscard]] bool ok() const { return !m_error.has_value(); }
    /// Notes a value outside its rules; the first one noted is the one reported.
    void invalid(std::string why) {
        if (!m_error) {
            m_error = Error{ErrorCode::Invalid, std::format("save section {}: {}", m_section, why)};
        }
    }
    /// The first failure, or one for bytes left over: a section is exactly its fields.
    [[nodiscard]] std::expected<void, Error> finish() {
        if (!m_error && m_reader.remaining() != 0) {
            invalid(std::format("{} bytes past its fields", m_reader.remaining()));
        }
        if (m_error) {
            return std::unexpected(*m_error);
        }
        return {};
    }

  private:
    // Keeps the first failure of a read; a section that ends early is damage of the section, not of the file.
    template <typename T> std::optional<T> take(std::expected<T, Error> read) {
        if (read) {
            return std::move(*read);
        }
        invalid("ends before its fields");
        return std::nullopt;
    }

    std::string_view m_section;
    io::Reader m_reader;
    std::optional<Error> m_error;
};

/// The sections of the current version for `save`: the faithful ones in the mapping's order, then `story` when it has
/// entries, then the mod sections by name (save.md#save-mapping).
/// @coney-only Coney's save container
[[nodiscard]] std::vector<Section> encode(const SaveGame& save);

/// Reads the sections of the current version, in any order, into a save; a section missing, repeated, unknown, of
/// the wrong length or with a value outside its rules fails with ErrorCode::Invalid (save.md#save-damaged).
/// @coney-only Coney's save container
[[nodiscard]] std::expected<SaveGame, Error> decode(const std::vector<Section>& sections);

/// The name at the start of a `profile` section's payload, the same in every version; empty when it cannot be read.
/// @coney-only Coney's save container
[[nodiscard]] std::string profileName(const Section& profile);

} // namespace coney::save_sections
