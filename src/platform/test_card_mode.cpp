// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/test_card_mode.h"

#include "platform/render_engine.h"

namespace coney::platform {

// Stub until the cards land: every name is the first card.
std::expected<std::unique_ptr<TestCardMode>, Error> TestCardMode::create(RenderEngine& engine, std::string_view name) {
    (void)name;
    return std::unique_ptr<TestCardMode>(new TestCardMode(engine, Card::Shapes));
}

TestCardMode::TestCardMode(RenderEngine& engine, Card card) : m_engine(engine), m_card(card) {}

TestCardMode::~TestCardMode() = default;

// Stub until the cards land: a black frame.
void TestCardMode::render(const RenderTime& /*time*/) {
    m_engine.beginFrame(graphics::kBlack);
    m_engine.present();
}

void TestCardMode::drawShapes() {}

void TestCardMode::drawTextures() {}

void TestCardMode::drawTriangles() {}

} // namespace coney::platform
