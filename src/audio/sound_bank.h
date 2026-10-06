// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "audio/pcm_sound.h"
#include "core/error.h"

namespace coney::io {
class Wad;
}

namespace coney::audio {

class SoundTables;

/// A sound bank decoded: the samples of `<name>.msb`, found by name hash through `<name>.msd`. The original streams
/// the `.msb` into sound RAM and plays a sample by hash; Coney decodes every sample once, at load, to PCM.
/// Research: docs/research/formats/audio.md#banks, docs/research/sound.md#banks
class SoundBank {
  public:
    /// Parses a `.msd`: `{u32 hash, u32 offset}` pairs into the `.msb`, ended by a zero pair (or the data's end).
    [[nodiscard]] static std::vector<std::pair<std::uint32_t, std::uint32_t>>
    parseIndex(std::span<const std::byte> msd);
    /// Decodes the bank `name` from its index `msd` and samples `msb`. Each sample's size, rate and class come from
    /// its record in `tables`; a hash with no record, or a span past the `.msb`, is skipped and counted in
    /// skipped(). A sample of a looping class loops over its whole length.
    [[nodiscard]] static SoundBank decode(std::string name, std::span<const std::byte> msd,
                                          std::span<const std::byte> msb, const SoundTables& tables);

    /// The bank `none`: no samples.
    SoundBank() = default;

    /// The decoded sample of `hash`, or nullptr.
    [[nodiscard]] std::shared_ptr<const PcmSound> find(std::uint32_t hash) const;
    /// The bank's name (`sound`, `menu`, `load_03` ...), `none` for the empty bank.
    [[nodiscard]] const std::string& name() const { return m_name; }
    /// How many samples it holds.
    [[nodiscard]] std::size_t size() const { return m_samples.size(); }
    /// How many index entries could not be decoded.
    [[nodiscard]] std::size_t skipped() const { return m_skipped; }

  private:
    std::string m_name = "none";
    std::map<std::uint32_t, std::shared_ptr<const PcmSound>> m_samples;
    std::size_t m_skipped = 0;
};

/// Reads the bank `name` (`./ee_files/<name>.msb` and `.msd`) from `wad` and decodes it. Fails with NotFound when
/// either file is missing and as the WAD's reads do.
/// @orig 0x0014c620 AudioDevice_LoadBank (msaudiodevice.cpp)
[[nodiscard]] std::expected<SoundBank, Error> loadSoundBank(const io::Wad& wad, std::string_view name,
                                                            const SoundTables& tables);

} // namespace coney::audio
