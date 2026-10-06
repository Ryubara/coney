// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace coney::audio {

/// The game's speech commands: the kinds of line a human can say (`attack`, `pain`, `cheer1` ...), by id. The names are
/// the game's (the table at 0x0050aaa8, id equal to the index; docs/references/speech.md).
inline constexpr std::size_t kSpeechCommands = 207;

/// The name of speech command `command`, empty past the table.
[[nodiscard]] std::string_view speechCommandName(std::uint32_t command);

/// The name of line `line` (1-based) of voice set `voiceSet` for speech command `command`:
/// `vags/character/voices/<set>/<command>_<nn>`, the set in decimal and the line in two digits (the names the sound
/// list holds).
[[nodiscard]] std::string voiceLineName(int voiceSet, std::uint32_t command, int line);

/// The voice table (docs/research/sound.md#voice-table): for each voice set and speech command, how many lines the set
/// has, which one it says next and the chance that it speaks at all. A set's lines for a command play in turn, not at
/// random.
class VoiceTable {
  public:
    /// The most lines counted for one set and command.
    static constexpr int kMaxLines = 54;
    /// Draws a whole number in [low, high].
    using RandomRange = std::function<std::int32_t(std::int32_t low, std::int32_t high)>;

    /// `SndAllocateCharacterVoices(sets)`: makes `sets` voice sets and counts each one's lines per command, probing
    /// line 1, 2 ... with `exists` (whether the sound list holds a hash) until one is missing or kMaxLines are found.
    /// Every chance starts at 100.
    /// @orig 0x001164a8 VoiceTable_Build (unknown)
    void build(int sets, const std::function<bool(std::uint32_t hash)>& exists);
    /// The voice sets built (0 before build()).
    [[nodiscard]] int sets() const { return m_sets; }

    /// `SndSetCommandSoundPercent(set, command, percent)`: the chance that `voiceSet` (-1: every set) says `command`.
    /// A set or command out of range does nothing.
    /// @orig 0x00115bb0 VoiceTable_SetPercent (unknown)
    void setPercent(int voiceSet, std::uint32_t command, std::uint8_t percent);

    /// The hash of the next line `voiceSet` says for `command`, or nothing: with a chance below 100 it rolls 0-99 with
    /// `random` and says nothing when the roll is higher than the chance; a set with no lines for the command says
    /// nothing; otherwise it takes the next line and moves on to the one after, wrapping at the count.
    /// @orig 0x00114b20 VoiceTable_NextLine (unknown)
    [[nodiscard]] std::optional<std::uint32_t> nextLine(int voiceSet, std::uint32_t command, const RandomRange& random);

    /// How many lines `voiceSet` has for `command` (0 out of range).
    [[nodiscard]] int lines(int voiceSet, std::uint32_t command) const;
    /// The chance `voiceSet` says `command`, percent (0 out of range).
    [[nodiscard]] int percent(int voiceSet, std::uint32_t command) const;

  private:
    // One set's entry for one command: the game's three bytes.
    struct Entry {
        std::uint8_t next = 0;
        std::uint8_t count = 0;
        std::uint8_t percent = 100;
    };
    [[nodiscard]] const Entry* entry(int voiceSet, std::uint32_t command) const;
    [[nodiscard]] Entry* entry(int voiceSet, std::uint32_t command);

    int m_sets = 0;
    std::vector<Entry> m_entries; // m_sets × kSpeechCommands
};

} // namespace coney::audio
