// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/loading_screen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <utility>

#include "gamemodes/legal_screen_mode.h"
#include "hud/hud_canvas.h"

namespace coney {

namespace {

// The fallback picture every search ends at.
constexpr std::string_view kDefaultPicture = "default_ls_0";

// The language suffix of `W_GameState + 0x120`: none for English (0, and 5), `_sp`, `_fr`, `_it`, `_ge` for 1-4.
std::string_view languageSuffix(Language language) {
    static constexpr std::array<std::string_view, 5> kSuffixes{"", "_sp", "_fr", "_it", "_ge"};
    return kSuffixes.at(static_cast<std::size_t>(language));
}

// The first two steps of the search for `stem` (`level99_ls_1`, `rumble_12_ls_0`): with the language's suffix, then
// without; `_w` with the 16:9 option in both. Empty when neither exists.
std::string findWithLanguage(const std::string& stem, Language language, bool widescreen,
                             const std::function<bool(std::string_view)>& exists) {
    std::string base = widescreen ? stem + "_w" : stem;
    if (std::string named = base + std::string(languageSuffix(language)); exists(named)) {
        return named;
    }
    if (exists(base)) {
        return base;
    }
    return {};
}

} // namespace

std::string loadScreenPictureName(std::string_view level, int n, Language language, bool widescreen,
                                  const std::function<bool(std::string_view)>& exists, bool* fellBack) {
    // Steps 1 and 2: picture n, with and without the language.
    if (std::string found = findWithLanguage(std::format("{}_ls_{}", level, n), language, widescreen, exists);
        !found.empty()) {
        return found;
    }
    // Step 3: picture 0 (so a level with one picture repeats it, and the caller stops).
    std::string first = std::format("{}_ls_0{}", level, widescreen ? "_w" : "");
    if (exists(first)) {
        return first;
    }
    // Step 4: the default picture.
    if (fellBack != nullptr) {
        *fellBack = true;
    }
    return std::string(kDefaultPicture);
}

std::string rumbleLoadScreenPictureName(int gameType, Language language, bool widescreen,
                                        const std::function<bool(std::string_view)>& exists, bool* fellBack) {
    if (std::string found = findWithLanguage(std::format("rumble_{}_ls_0", gameType), language, widescreen, exists);
        !found.empty()) {
        return found;
    }
    // Taken without a check. Whether the original marks this as a fallback is not on the page; Coney does.
    if (fellBack != nullptr) {
        *fellBack = true;
    }
    return std::string(kDefaultPicture);
}

LoadScreenPictures loadScreenPictures(std::string_view level, int levelNumber, int rumbleGameType, Language language,
                                      bool widescreen, const std::function<bool(std::string_view)>& exists) {
    LoadScreenPictures pictures;
    if (levelNumber > kLastStoryLoadScreenLevel) {
        pictures.names.push_back(
            rumbleLoadScreenPictureName(rumbleGameType, language, widescreen, exists, &pictures.fellBack));
        return pictures;
    }
    constexpr int kMaxPictures = 3;
    for (int n = 0; n < kMaxPictures; ++n) {
        bool fellBack = false;
        std::string name = loadScreenPictureName(level, n, language, widescreen, exists, &fellBack);
        if (!pictures.names.empty() && name == pictures.names.back()) {
            break;
        }
        pictures.fellBack = pictures.fellBack || fellBack;
        pictures.names.push_back(std::move(name));
    }
    return pictures;
}

LoadScreenPictures memoryCardPictures(Language language, bool widescreen,
                                      const std::function<bool(std::string_view)>& exists) {
    LoadScreenPictures pictures;
    // Picture 0 as a level's first two steps; the plain name when neither form is there.
    std::string first = findWithLanguage("memory_card_screen", language, widescreen, exists);
    pictures.names.push_back(first.empty() ? std::string(widescreen ? "memory_card_screen_w" : "memory_card_screen")
                                           : std::move(first));
    pictures.names.emplace_back(widescreen ? "memory_card_loading_w" : "memory_card_loading");
    return pictures;
}

graphics::Rgba loadScreenBarColour(int levelNumber) {
    static constexpr std::array<int, 5> kGreyLevels{11, 20, 82, 83, 92};
    if (std::ranges::contains(kGreyLevels, levelNumber)) {
        return graphics::Rgba{223, 223, 223, 255};
    }
    return graphics::Rgba{170, 43, 43, 255};
}

graphics::LogicalQuad loadScreenBarQuad(const graphics::OverlayCamera& camera, bool widescreen, float p,
                                        graphics::Rgba colour) {
    // The mode table's interlaced rows; the bar's size is in the PS2's pixels, which are the logical screen's.
    const float x0 = widescreen ? 0.225F : 0.04F;
    const float y0 = widescreen ? -0.352F : -0.328F;
    const float fullWidth = widescreen ? 212.0F : 273.0F;
    constexpr float kHeight = 8.0F;
    const graphics::LogicalPoint corner = camera.project(graphics::OverlayPoint{x0, y0, 1.0F});
    return graphics::LogicalQuad{.x = corner.x,
                                 .y = corner.y,
                                 .width = fullWidth * std::clamp(p, 0.0F, 1.0F),
                                 .height = kHeight,
                                 .uv = {},
                                 .colour = colour};
}

int LoadScreenTimeline::picture(std::uint64_t now, int count) const {
    if (count <= 0 || end <= start) {
        return 0;
    }
    const double elapsed = now > start ? static_cast<double>(now - start) : 0.0;
    const auto index = static_cast<int>(std::floor(elapsed / static_cast<double>(end - start) * count));
    return std::min(index, count - 1);
}

std::uint8_t LoadScreenTimeline::alpha(std::uint64_t now) const {
    // 255 over 200 ms both ways; the fade out is written after the fade in, so it wins where both hold.
    constexpr double kPerMillisecond = 1.275;
    double a = 255.0;
    const std::uint64_t sinceStart = now - start; // unsigned, as the original's
    if (sinceStart < kFadeMilliseconds) {
        a = static_cast<double>(sinceStart) * kPerMillisecond;
    }
    const std::uint64_t untilEnd = end - now; // unsigned: past the end it is huge, and the alpha stays 255
    if (untilEnd < kFadeMilliseconds) {
        a = static_cast<double>(untilEnd) * kPerMillisecond;
    }
    return static_cast<std::uint8_t>(std::clamp(a, 0.0, 255.0));
}

float LoadScreenTimeline::progress(std::uint64_t now) const {
    if (end <= start) {
        return 1.0F;
    }
    const double elapsed = now > start ? static_cast<double>(now - start) : 0.0;
    return static_cast<float>(std::min(elapsed / static_cast<double>(end - start), 1.0));
}

LoadingScreen::LoadingScreen(graphics::RenderDevice& device, SheetLoader loadSheet, ResourceExists exists,
                             LoadScreenSettings settings, LoadScreenSounds sounds,
                             std::function<void(std::string_view)> log)
    : m_device(device), m_loadSheet(std::move(loadSheet)), m_exists(std::move(exists)), m_settings(settings),
      m_sounds(std::move(sounds)), m_log(std::move(log)) {}

void LoadingScreen::begin(std::string_view level, int levelNumber, int rumbleGameType, std::uint64_t nowMs) {
    m_levelNumber = levelNumber;
    const auto exists = [this](std::string_view name) { return m_exists && m_exists(name); };
    m_pictures =
        loadScreenPictures(level, levelNumber, rumbleGameType, m_settings.language, m_settings.widescreen, exists);
    // Each picture loaded before the clock starts, as the original waits until each is resident.
    m_memoryCard = false;
    m_sheets.clear();
    for (const std::string& name : m_pictures.names) {
        loadPicture(name);
    }
    m_timeline = LoadScreenTimeline{.start = nowMs, .end = nowMs + loadScreenTimelineMilliseconds(levelNumber)};
    m_active = true;
    std::string names;
    for (const std::string& name : m_pictures.names) {
        names += names.empty() ? name : ", " + name;
    }
    m_log(std::format("loading screen: {} ({} picture{}: {}{})\n", level, m_pictures.names.size(),
                      m_pictures.names.size() == 1 ? "" : "s", names, m_pictures.fellBack ? ", the default" : ""));
}

void LoadingScreen::loadPicture(const std::string& name) {
    std::optional<graphics::SpriteSheet>& slot = m_sheets.emplace_back();
    if (!m_loadSheet) {
        return;
    }
    auto sheet = m_loadSheet(name);
    if (!sheet) {
        m_log(std::format("loading screen: {}: {}\n", name, sheet.error().message));
    } else if (sheet->page.rects.empty() || sheet->texture == nullptr) {
        m_log(std::format("loading screen: {}: the sprite sheet has no picture\n", name));
    } else {
        slot = std::move(*sheet);
    }
}

void LoadingScreen::beginMemoryCard(std::uint64_t nowMs) {
    const auto exists = [this](std::string_view name) { return m_exists && m_exists(name); };
    m_pictures = memoryCardPictures(m_settings.language, m_settings.widescreen, exists);
    m_memoryCard = true;
    m_sheets.clear();
    for (const std::string& name : m_pictures.names) {
        loadPicture(name);
    }
    // The spinner's sheet, for its pulse over picture 1.
    m_parts.reset();
    if (m_loadSheet) {
        if (auto parts = m_loadSheet(kSpinnerSheet); parts) {
            constexpr std::size_t kSpinnerSprites = 4;
            constexpr float kSpinnerDepth = 11000.0F;
            m_parts.emplace(std::move(*parts), kSpinnerSprites, kSpinnerDepth);
        } else {
            m_log(std::format("loading screen: {}: {}\n", kSpinnerSheet, parts.error().message));
        }
    }
    m_timeline = LoadScreenTimeline{.start = nowMs, .end = nowMs + kMemoryCardTimelineMilliseconds};
    m_active = true;
    m_log(std::format("loading screen: the memory-card screen ({}, {})\n", m_pictures.names[0], m_pictures.names[1]));
}

void LoadingScreen::renderMemoryCard(std::uint64_t nowMs, const graphics::OverlayCamera& camera) const {
    const std::uint8_t alpha = m_timeline.alpha(nowMs);
    const bool second = nowMs - m_timeline.start >= kMemoryCardFirstPictureMilliseconds;
    const std::optional<graphics::SpriteSheet>& sheet = m_sheets.at(second ? 1 : 0);
    constexpr std::uint8_t kMinimumAlpha = 10;
    if (sheet.has_value() && alpha > kMinimumAlpha) {
        graphics::LogicalQuad quad = legalScreenQuad(
            camera, legalScreenFactors(LegalScreenSettings{.widescreen = m_settings.widescreen}), sheet->page.rect(0));
        quad.colour = graphics::Rgba{255, 255, 255, alpha};
        m_device.drawQuads(sheet->texture.get(), std::span(&quad, 1));
    }
    // Over picture 1 the spinner is turned on and pulses, leaving its colour on it.
    if (second && m_parts.has_value()) {
        hud::Spinner& spinner = m_spinner != nullptr ? *m_spinner : m_ownSpinner;
        spinner.set(true);
        hud::HudCanvas canvas;
        canvas.parts = &*m_parts;
        spinner.drawPulse(canvas, nowMs);
        m_parts->render(m_device, graphics::OverlayCamera());
        m_parts->clear();
    }
}

void LoadingScreen::startSounds() {
    if (m_sounds.start) {
        m_sounds.start();
    }
}

void LoadingScreen::stopSounds() {
    if (m_sounds.stop) {
        m_sounds.stop();
    }
}

void LoadingScreen::finish(std::uint64_t nowMs) { m_timeline.end = nowMs + LoadScreenTimeline::kFadeMilliseconds; }

bool LoadingScreen::finished(std::uint64_t nowMs) const {
    return nowMs + LoadScreenTimeline::kFinishMarginMilliseconds >= m_timeline.end;
}

void LoadingScreen::end() {
    m_sheets.clear();
    m_parts.reset();
    m_memoryCard = false;
    m_timeline = {};
    m_active = false;
}

void LoadingScreen::render(std::uint64_t nowMs) const {
    m_device.beginFrame(graphics::kBlack);
    if (!m_active) {
        m_device.present();
        return;
    }
    const graphics::OverlayCamera camera = m_settings.widescreen
                                               ? graphics::OverlayCamera(1.1F, graphics::OverlayCamera::kWideViewAspect)
                                               : graphics::OverlayCamera();
    if (m_memoryCard) {
        renderMemoryCard(nowMs, camera);
        m_device.present();
        return;
    }
    const std::uint8_t alpha = m_timeline.alpha(nowMs);
    const int index = m_timeline.picture(nowMs, static_cast<int>(m_sheets.size()));
    const std::optional<graphics::SpriteSheet>* sheet =
        m_sheets.empty() ? nullptr : &m_sheets.at(static_cast<std::size_t>(index));
    const bool resident = sheet != nullptr && sheet->has_value();
    // The picture, placed and scaled as the legal screen, white with the alpha.
    constexpr std::uint8_t kMinimumAlpha = 10;
    if (resident && alpha > kMinimumAlpha) {
        graphics::LogicalQuad quad =
            legalScreenQuad(camera, legalScreenFactors(LegalScreenSettings{.widescreen = m_settings.widescreen}),
                            (*sheet)->page.rect(0));
        quad.colour = graphics::Rgba{255, 255, 255, alpha};
        m_device.drawQuads((*sheet)->texture.get(), std::span(&quad, 1));
    }
    // The bar, a clock: full alpha while the picture is missing.
    graphics::Rgba colour = loadScreenBarColour(m_levelNumber);
    colour.a = resident ? alpha : std::uint8_t{255};
    const graphics::LogicalQuad bar =
        loadScreenBarQuad(camera, m_settings.widescreen, m_timeline.progress(nowMs), colour);
    m_device.drawQuads(nullptr, std::span(&bar, 1));
    m_device.present();
}

} // namespace coney
