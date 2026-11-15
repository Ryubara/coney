// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/lighting_lab.h"

namespace coney::debug {

LightingCounts lightingCounts(const graphics::LightingFrame& /*frame*/, const graphics::LightManager& /*manager*/) {
    return {};
}

std::string_view lightKindName(graphics::LightType /*type*/, float /*radius*/) { return {}; }

std::string_view lightsWhatName(std::uint16_t /*lights*/) { return {}; }

std::string lightOriginName(graphics::LightOrigin /*origin*/, std::uint16_t /*record*/) { return {}; }

std::string_view selectionCallerName(graphics::SelectionCaller /*caller*/) { return {}; }

std::uint32_t selectionCallerAddress(graphics::SelectionCaller /*caller*/) { return 0; }

std::uint32_t renderWareLightType(graphics::LightType /*type*/) { return 0; }

std::string lightLine(const graphics::LightManager& /*manager*/, std::uint16_t /*index*/) { return {}; }

std::vector<std::string> selectionLines(const graphics::FrameSelection& /*selection*/) { return {}; }

const graphics::FrameSelection* selectionNear(const graphics::LightingFrame& /*frame*/,
                                              graphics::SelectionCaller /*caller*/, anim::Vec3 /*at*/) {
    return nullptr;
}

DebugLines lightingLines(const graphics::LightingFrame& /*frame*/, const LightingDrawOptions& /*options*/,
                         const graphics::FrameSelection* /*selected*/) {
    return {};
}

std::vector<std::string> selectionColumns() { return {}; }

std::string selectionsCsv(const graphics::LightingFrame& /*frame*/) { return {}; }

std::string frameLightsCsv(const graphics::LightingFrame& /*frame*/) { return {}; }

std::expected<std::filesystem::path, Error> writeLightingDump(const graphics::LightingFrame& /*frame*/,
                                                              const std::filesystem::path& /*folder*/) {
    return fail(ErrorCode::Io, "not built");
}

std::size_t LightEditQueue::applyPending(graphics::LightManager* /*manager*/) {
    m_pending.clear();
    return 0;
}

} // namespace coney::debug
