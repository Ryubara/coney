// SPDX-License-Identifier: GPL-3.0-or-later
// The developer overlay's scaler over a bare Dear ImGui context: no window, no OpenGL. It checks that the style and
// the font follow the display's height, that open and saved windows are resized with them, and that the Display page's
// choice is kept in the settings (docs/guides/debug-menu.md#ui-scale).
#include "platform/imgui_ui_scale.h"

#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

#include "debug/debug_session.h"

using Catch::Approx;
using coney::debug::DisplayOptions;
using coney::platform::ImGuiUiScale;

namespace {

// A bare context with the scaler set up on it, as ImGuiOverlay::start() does, and no frame begun.
class BareScaler {
  public:
    BareScaler() : scaler(display) {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1920.0F, 1080.0F);
        io.DeltaTime = 1.0F / 30.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::StyleColorsDark();
        scaler.setup();
    }
    ~BareScaler() { ImGui::DestroyContext(); }
    BareScaler(const BareScaler&) = delete;
    BareScaler& operator=(const BareScaler&) = delete;
    BareScaler(BareScaler&&) = delete;
    BareScaler& operator=(BareScaler&&) = delete;

    // One whole frame with a window `name`, sized `size` the first time it opens, optionally not resizable.
    static void frameWithWindow(const char* name, ImVec2 size, ImGuiWindowFlags flags = 0) {
        ImGui::NewFrame();
        ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
        ImGui::Begin(name, nullptr, flags);
        ImGui::TextUnformatted("text");
        ImGui::End();
        ImGui::Render();
    }

    DisplayOptions display;
    ImGuiUiScale scaler;
};

} // namespace

TEST_CASE("the scaler sets the font and the style sizes for the display's height", "[debug][platform][ui-scale]") {
    BareScaler bare;
    const ImGuiStyle base = ImGui::GetStyle();
    ImGuiIO& io = ImGui::GetIO();

    // 1080: the designed size, in the pixel-clean font.
    CHECK(bare.scaler.update(1080.0F));
    CHECK(ImGui::GetStyle().FontSizeBase == Approx(13.0F));
    CHECK(ImGui::GetStyle().WindowPadding.x == Approx(base.WindowPadding.x));
    CHECK(io.FontDefault == io.Fonts->Fonts[0]);
    CHECK(bare.display.uiScaleApplied == Approx(1.0F));

    // 4K: twice the sizes, in the scalable font at twice the size (so the atlas draws the glyphs at that size).
    CHECK(bare.scaler.update(2160.0F));
    CHECK(bare.scaler.scale() == Approx(2.0F));
    CHECK(ImGui::GetStyle().FontSizeBase == Approx(26.0F));
    CHECK(ImGui::GetStyle().WindowPadding.x == Approx(base.WindowPadding.x * 2.0F));
    CHECK(ImGui::GetStyle().ScrollbarSize == Approx(base.ScrollbarSize * 2.0F));
    CHECK(io.FontDefault == io.Fonts->Fonts[1]);
    CHECK(bare.display.uiScaleApplied == Approx(2.0F));

    // The same height again changes nothing.
    CHECK_FALSE(bare.scaler.update(2160.0F));

    // Back to 1080: exactly the original again, not a halved copy of the doubled one.
    CHECK(bare.scaler.update(1080.0F));
    CHECK(ImGui::GetStyle().FontSizeBase == Approx(13.0F));
    CHECK(ImGui::GetStyle().WindowPadding.x == Approx(base.WindowPadding.x));
    CHECK(io.FontDefault == io.Fonts->Fonts[0]);
}

TEST_CASE("the text a frame draws is the scaled size", "[debug][platform][ui-scale]") {
    BareScaler bare;
    bare.scaler.update(2160.0F);
    ImGui::NewFrame();
    CHECK(ImGui::GetFontSize() == Approx(26.0F));
    ImGui::Render();
}

TEST_CASE("an override from the Display page beats the display's height", "[debug][platform][ui-scale]") {
    BareScaler bare;
    bare.display.uiScaleChoice = 4; // 2x
    bare.scaler.update(1080.0F);
    CHECK(bare.scaler.scale() == Approx(2.0F));
    // Back to Auto: the height decides again.
    bare.display.uiScaleChoice = 0;
    bare.scaler.update(1080.0F);
    CHECK(bare.scaler.scale() == Approx(1.0F));
}

TEST_CASE("a change of scale resizes the open windows by the same ratio", "[debug][platform][ui-scale]") {
    BareScaler bare;
    bare.scaler.update(1080.0F);
    BareScaler::frameWithWindow("Page", ImVec2(400.0F, 300.0F));
    BareScaler::frameWithWindow("Fixed", ImVec2(100.0F, 100.0F), ImGuiWindowFlags_NoResize);

    bare.scaler.update(2160.0F);
    const ImGuiWindow* page = ImGui::FindWindowByName("Page");
    const ImGuiWindow* fixed = ImGui::FindWindowByName("Fixed");
    REQUIRE(page != nullptr);
    REQUIRE(fixed != nullptr);
    CHECK(page->SizeFull.x == Approx(800.0F));
    CHECK(page->SizeFull.y == Approx(600.0F));
    CHECK(fixed->SizeFull.x == Approx(100.0F)); // a window that cannot be resized is left alone

    bare.scaler.update(1080.0F);
    CHECK(page->SizeFull.x == Approx(400.0F));
}

TEST_CASE("the saved choice and the scale the sizes were saved at come back from the settings",
          "[debug][platform][ui-scale]") {
    // A window saved at 1x, and a choice of 2x.
    const char* ini = "[Window][Page]\nPos=10,10\nSize=300,200\n\n[ConeyUi][Scale]\nApplied=1\nChoice=4\n\n";
    BareScaler bare;
    ImGui::LoadIniSettingsFromMemory(ini);
    CHECK(bare.display.uiScaleChoice == 4);

    // The first update sizes the saved window to the scale applied; the window then opens at that size.
    bare.scaler.update(1080.0F);
    CHECK(bare.scaler.scale() == Approx(2.0F));
    ImGui::NewFrame();
    ImGui::Begin("Page");
    ImGui::End();
    ImGui::Render();
    const ImGuiWindow* page = ImGui::FindWindowByName("Page");
    REQUIRE(page != nullptr);
    CHECK(page->SizeFull.x == Approx(600.0F));
    CHECK(page->SizeFull.y == Approx(400.0F));
}

TEST_CASE("a window saved at the scale now applied keeps its size", "[debug][platform][ui-scale]") {
    const char* ini = "[Window][Page]\nPos=10,10\nSize=300,200\n\n[ConeyUi][Scale]\nApplied=2\nChoice=4\n\n";
    BareScaler bare;
    ImGui::LoadIniSettingsFromMemory(ini);
    bare.scaler.update(1080.0F);
    ImGui::NewFrame();
    ImGui::Begin("Page");
    ImGui::End();
    ImGui::Render();
    const ImGuiWindow* page = ImGui::FindWindowByName("Page");
    REQUIRE(page != nullptr);
    CHECK(page->SizeFull.x == Approx(300.0F));
}

TEST_CASE("the settings file gets the applied scale and the choice", "[debug][platform][ui-scale]") {
    BareScaler bare;
    bare.display.uiScaleChoice = 3; // 1.5x
    bare.scaler.update(1080.0F);
    const std::string saved = ImGui::SaveIniSettingsToMemory();
    CHECK(saved.find("[ConeyUi][Scale]") != std::string::npos);
    CHECK(saved.find("Applied=1.5") != std::string::npos);
    CHECK(saved.find("Choice=3") != std::string::npos);
}

TEST_CASE("a bad choice or scale in the settings is ignored", "[debug][platform][ui-scale]") {
    BareScaler bare;
    ImGui::LoadIniSettingsFromMemory("[ConeyUi][Scale]\nApplied=99\nChoice=77\n\n");
    CHECK(bare.display.uiScaleChoice == 0);
    bare.scaler.update(1080.0F);
    CHECK(bare.scaler.scale() == Approx(1.0F));
}
