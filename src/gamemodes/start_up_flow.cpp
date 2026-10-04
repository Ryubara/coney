// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/start_up_flow.h"

namespace coney {

StartUpFlow::StartUpFlow(graphics::RenderDevice& device, GameModeStack& stack,
                         const ProfileManagerMode::SheetLoader& loadSheet, const gui::GlobalStrings& strings,
                         LegalScreenSettings legal, const std::function<void(std::string_view)>& log)
    : m_stack(stack), m_services(log), m_profileManager(device, loadSheet, strings, m_services, legal.europe, log),
      m_levelFlow(device, stack, m_profileManager, m_services, log), m_memoryCard(device, stack, m_levelFlow),
      m_legal(device, loadSheet, legal, log) {}

void StartUpFlow::start() {
    for (const std::string_view movie : kStartUpMovies) {
        m_services.playMovie(movie);
    }
    m_stack.push(m_levelFlow);
    m_memoryCard.setBootCheck();
    m_stack.push(m_memoryCard);
    m_stack.push(m_legal);
}

} // namespace coney
