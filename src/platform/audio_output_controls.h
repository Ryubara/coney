// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

#include "debug/audio_controls.h"
#include "platform/audio_output.h"

namespace coney::platform {

/// The debug menus' Audio page over the sound output (debug::AudioControls, docs/guides/debug-menu.md#pages): the
/// volumes, the test tone and the status, through AudioOutput's own API. In the debug tools' target
/// (docs/guides/building.md#debug-tools), so the release build leaves it out.
class AudioOutputControls final : public debug::AudioControls {
  public:
    /// Acts on `output` from now on and returns these controls; null, acting on nothing, when `output` is null: the
    /// debug services' `audio` hook, asked each time the page acts.
    [[nodiscard]] debug::AudioControls* controlsOf(AudioOutput* output) {
        m_output = output;
        return output != nullptr ? this : nullptr;
    }

    [[nodiscard]] std::size_t volumeCount() const override { return m_output->volumeCount(); }
    [[nodiscard]] std::string_view volumeName(std::size_t index) const override { return m_output->volumeName(index); }
    [[nodiscard]] float volume(std::size_t index) const override { return m_output->volume(index); }
    void setVolume(std::size_t index, float volume) override { m_output->setVolume(index, volume); }
    [[nodiscard]] bool testTone() const override { return m_output->testTone(); }
    void setTestTone(bool on) override { m_output->setTestTone(on); }
    [[nodiscard]] debug::AudioStatus status() const override { return m_output->status(); }

  private:
    AudioOutput* m_output = nullptr;
};

} // namespace coney::platform
