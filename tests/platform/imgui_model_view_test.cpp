// SPDX-License-Identifier: GPL-3.0-or-later
// The developer overlay's generic renderer over a bare Dear ImGui context: no window, no OpenGL. Together with "every
// item kind has a renderer in the pad menu" (tests/gui/debug_menu_view_test.cpp), it checks that both front ends
// draw every item kind (docs/guides/debug-menu.md#adding-a-feature).
#include "platform/imgui_model_view.h"

#include <cstddef>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName

#include "debug/debug_session.h"
#include "debug/menu_model.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::ItemKind;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;
using coney::platform::ImGuiModelView;

namespace {

// One ImGui frame on a fresh context with no backend: begun on construction, rendered and destroyed on destruction.
// The atlas is left to a renderer that "has textures", so no font upload is needed.
class BareFrame {
  public:
    BareFrame() {
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0F, 720.0F);
        io.DeltaTime = 1.0F / 30.0F;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        ImGui::NewFrame();
    }
    ~BareFrame() {
        ImGui::Render();
        ImGui::DestroyContext();
    }
    BareFrame(const BareFrame&) = delete;
    BareFrame& operator=(const BareFrame&) = delete;
    BareFrame(BareFrame&&) = delete;
    BareFrame& operator=(BareFrame&&) = delete;
};

// One item of every kind, labelled by the kind's name.
std::vector<MenuItem> oneOfEachKind() {
    std::vector<MenuItem> items;
    items.push_back(coney::debug::actionItem("action", [] {}));
    items.push_back(coney::debug::toggleItem("toggle", [] { return true; }, [](bool) {}));
    items.push_back(coney::debug::numberItem("number", [] { return 2.5; }, [](double) {}, 0, 5, 0.5, false));
    items.push_back(
        coney::debug::choiceItem("choice", {"first", "second"}, [] { return std::size_t{1}; }, [](std::size_t) {}));
    items.push_back(
        coney::debug::textItem("text", [] { return std::string("typed"); }, [](const std::string&) {}, false));
    items.push_back(coney::debug::submenuItem("submenu", [] { return std::make_shared<MenuPage>("Sub"); }));
    items.push_back(coney::debug::watchItem("watch", [] { return std::string("live"); }));
    items.push_back(coney::debug::logItem("log", [] { return std::vector<std::string>{"log line"}; }));
    return items;
}

} // namespace

TEST_CASE("every item kind has a renderer in the developer overlay", "[debug][platform]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    ImGuiModelView view(session.model());
    const BareFrame frame;
    ImGui::SetNextWindowSize(ImVec2(1200.0F, 700.0F));
    ImGui::Begin("Kinds");
    const std::vector<MenuItem> items = oneOfEachKind();
    REQUIRE(items.size() == coney::debug::kItemKindCount);
    std::set<ItemKind> kinds;
    for (const MenuItem& item : items) {
        INFO(item.label);
        kinds.insert(item.kind);
        // A renderer says so and draws something.
        const int before = ImGui::GetWindowDrawList()->VtxBuffer.Size;
        CHECK(view.drawItem(item, "Kinds/" + item.label));
        CHECK(ImGui::GetWindowDrawList()->VtxBuffer.Size > before);
    }
    CHECK(kinds.size() == coney::debug::kItemKindCount);
    // A value outside the enum has no renderer.
    MenuItem stray = coney::debug::actionItem("stray", [] {});
    stray.kind = static_cast<ItemKind>(coney::debug::kItemKindCount);
    CHECK_FALSE(view.drawItem(stray, "Kinds/stray"));
    ImGui::End();
}

TEST_CASE("the developer overlay draws every item of the session's pages", "[debug][platform]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    ImGuiModelView view(session.model());
    const BareFrame frame;
    ImGui::Begin("Walk");
    // The pages and the pages their submenus open, as the overlay's windows and tree nodes show them.
    std::size_t drawn = 0;
    for (const std::string& title : session.model().pageTitles()) {
        const std::shared_ptr<MenuPage> page = session.model().openPage(title);
        REQUIRE(page != nullptr);
        for (const MenuItem& item : page->items()) {
            INFO(title << "/" << item.label);
            CHECK(view.drawItem(item, title + "/" + item.label));
            ++drawn;
            if (item.kind != ItemKind::Submenu || !item.open) {
                continue;
            }
            const std::shared_ptr<MenuPage> sub = item.open();
            REQUIRE(sub != nullptr);
            for (const MenuItem& child : sub->items()) {
                CHECK(view.drawItem(child, title + "/" + item.label + "/" + child.label));
                ++drawn;
            }
        }
    }
    CHECK(drawn > 1000); // every binding of every category among them
    ImGui::End();
}

TEST_CASE("the developer overlay keeps its open page windows in the settings file", "[debug][platform]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    std::string saved;
    {
        ImGuiModelView view(session.model());
        const BareFrame frame;
        view.keepOpenPagesInSettings();
        view.setPageOpen("Time", true);
        view.setPageOpen("Input", false);
        saved = ImGui::SaveIniSettingsToMemory();
    }
    CHECK(saved.find("[ConeyPages][Open]\nInput=0\nTime=1\n") != std::string::npos);
    ImGuiModelView reread(session.model());
    const BareFrame frame;
    reread.keepOpenPagesInSettings();
    ImGui::LoadIniSettingsFromMemory(saved.c_str(), saved.size());
    CHECK(reread.pageOpen("Time"));
    CHECK_FALSE(reread.pageOpen("Input"));
}

TEST_CASE("the developer overlay opens page windows from its menu bar", "[debug][platform]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    ImGuiModelView view(session.model());
    view.setPageOpen("Time", true);
    view.setPageOpen("Favourites", true);
    const BareFrame frame;
    view.draw("F1 hides");
    CHECK(view.pageOpen("Time"));
    CHECK_FALSE(view.pageOpen("Natives"));
    CHECK(ImGui::FindWindowByName("Time") != nullptr);
    CHECK(ImGui::FindWindowByName("Favourites") != nullptr);
}
