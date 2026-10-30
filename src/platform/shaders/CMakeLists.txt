# SPDX-License-Identifier: GPL-3.0-or-later
# The GPU renderer's shaders: every *.hlsl file in this folder, compiled by the build to SPIR-V, DXIL and MSL and embedded
# in the library coney_shaders (docs/guides/shaders.md). Added by the top CMakeLists.txt when CONEY_SHADERS is on.
coney_add_shader_library(coney_shaders DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
