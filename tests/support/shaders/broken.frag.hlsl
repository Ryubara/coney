// SPDX-License-Identifier: GPL-3.0-or-later
// A fragment shader that does not compile, on purpose: tests/support/shader_refusal_check.cmake feeds it to the shader
// compiler and expects a refusal for every format. It names a variable that does not exist.

float4 main() : SV_Target0
{
    return notDeclaredAnywhere;
}
