# SPDX-License-Identifier: GPL-3.0-or-later
# Coney Studio's editor (docs/guides/studio/prototype.md): the project, the level document, the Level view's camera
# and picking, the Inspector, the theme, Run in Coney and the docked interface, all drawn with Dear ImGui and touching
# no window, graphics API or operating system (that is the host in src/platform/). A debug-tools build only, as Studio
# is (docs/guides/studio/technology.md#program): it uses the debug menus' model and renderer and the sandbox. The
# executable, coney-studio, is made in src/platform/ beside coney.
add_library(
    coney_studio STATIC
    inspector.cpp
    level_camera.cpp
    level_document.cpp
    level_templates.cpp
    project.cpp
    project_manifest.cpp
    run_in_coney.cpp
    studio_app.cpp
    theme.cpp)
coney_target_defaults(coney_studio)
# coney_devtools for the debug menus' model and their Dear ImGui renderer (ImGuiModelView), the sandbox and Dear ImGui.
target_link_libraries(coney_studio PUBLIC coney_devtools)
