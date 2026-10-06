// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/memory_card_mode.h"

#include <format>
#include <utility>

#include "core/game_timer.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/profile_manager_mode.h"
#include "gui/text_layout.h"

namespace coney {

MemoryCardMode::MemoryCardMode(graphics::RenderDevice& device, GameModeStack& stack, LevelFlowMode& levelFlow,
                               SheetLoader loadSheet, const gui::GlobalStrings& strings, std::uint64_t checkingMs,
                               std::function<void(std::string_view)> log)
    : m_device(device), m_stack(stack), m_levelFlow(levelFlow), m_loadSheet(std::move(loadSheet)), m_strings(strings),
      m_checkingMs(checkingMs), m_log(std::move(log)) {
    // The centred message is in big_font; a font that did not load draws nothing.
    m_canvas.fonts = [this](int /*slot*/) -> const graphics::Font* { return m_bigFont ? &*m_bigFont : nullptr; };
    m_canvas.textBatch = [this](int /*slot*/) -> graphics::SpriteBatch* { return m_bigBatch ? &*m_bigBatch : nullptr; };
}

void MemoryCardMode::startLoadSequence() { startSequence(Kind::Load); }

void MemoryCardMode::startDeleteSequence() { startSequence(Kind::Delete); }

void MemoryCardMode::startSequence(Kind kind) {
    if (m_stack.topId() != kId) {
        m_kind = kind;
        m_stack.push(*this);
    }
}

void MemoryCardMode::enter() {
    // The original scans the card and reads or writes its save here. Coney's profiles are files: a load reads them
    // again; a delete has nothing to write, as the store removed the file when PM_Delete asked.
    if (m_kind == Kind::Load) {
        if (m_profiles != nullptr) {
            m_profiles->reload();
        }
        ++m_loads;
    }
    // The "checking" message's font.
    auto sheet = m_loadSheet
                     ? m_loadSheet(gui::kBigFontSheet)
                     : std::expected<graphics::SpriteSheet, Error>(fail(ErrorCode::NotFound, "no sheet loader"));
    auto font = sheet ? graphics::Font::fromSheet(std::move(*sheet))
                      : std::expected<graphics::Font, Error>(std::unexpected(std::move(sheet.error())));
    if (font) {
        m_bigBatch.emplace(font->sheet(), kTextCapacity, kTextDepth);
        m_bigFont = std::move(*font);
    } else if (m_log) {
        m_log(std::format("memory card: {}: {}\n", gui::kBigFontSheet, font.error().message));
    }
    m_showPending = true;
}

ModeResult MemoryCardMode::update(GameModeStack& /*stack*/, const FrameTime& frame) {
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    // The sprites of the step before are drawn; this step lists its own.
    m_pass.empty();
    // The boot check's message (0x0015a328): "checking memory card" for its time, centred.
    if (m_showPending) {
        m_showPending = false;
        m_box.showTimed(m_strings.get(kCheckingString), m_checkingMs, gui::MessageStyle::Centre, nowMs);
    }
    m_box.update(gui::GuiFrame{nowMs, nullptr});
    // Coney's scan has found nothing to ask about: the mode is done when the message is.
    if (!m_box.open()) {
        return ModeResult::Leave;
    }
    m_box.render(m_canvas);
    if (m_bigBatch) {
        m_pass.queue(*m_bigBatch);
    }
    return ModeResult::Stay;
}

void MemoryCardMode::render(const RenderTime& /*time*/) {
    m_device.beginFrame(graphics::kBlack);
    m_pass.draw(m_device, m_camera);
    m_device.present();
}

void MemoryCardMode::exit() {
    m_bootCheck = BootCheck::Done;
    m_kind = Kind::Load;
    // The stack calls exit() after removing this mode, so its top is the mode that was below.
    if (m_stack.top() == &m_levelFlow) {
        m_levelFlow.cancelFrontEndLoad();
    }
    if (m_profileManager != nullptr && m_stack.top() == m_profileManager) {
        m_profileManager->memoryCardDone();
    }
    // The queue points at the batch released below.
    m_pass.empty();
    m_box.close();
    m_bigBatch.reset();
    m_bigFont.reset();
}

} // namespace coney
