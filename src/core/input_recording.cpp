// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the test commit (orig fails-first): compiles the new tests and does what the code did before the change,
// which is nothing: no recording, no header.
#include "core/input_recording.h"

#include <string>
#include <utility>

namespace coney {

InputRecorder::InputRecorder() = default;

void InputRecorder::recordPort(std::string& /*out*/, std::uint64_t /*frame*/, std::size_t /*port*/,
                               const PadSample& /*sample*/) {}

std::string InputRecorder::record(std::uint64_t /*frame*/, const PortSamples& /*samples*/) { return {}; }

std::string inputRecordingHeader(const InputRecordingHeader& /*header*/) { return {}; }

RecordingInput::RecordingInput(InputSource& inner, Sink sink, std::function<bool()> started)
    : m_inner(inner), m_sink(std::move(sink)), m_started(std::move(started)) {}

RecordingInput::~RecordingInput() = default;

PortSamples RecordingInput::sample(std::uint64_t frame) { return m_inner.sample(frame); }

} // namespace coney
