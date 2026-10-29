// SPDX-License-Identifier: GPL-3.0-or-later
// coney_luac: Coney's Lua 4.0 compiler behind the command line of a stock `luac`, so the decompiler's round trip can
// compile with it in place of luac: `coney-tools lua decompile --all --roundtrip --luac <this program>`
// (docs/guides/coney-tools-fidelity.md#lua). It takes `luac`'s options as the round trip passes them:
//
//     coney_luac [-s] [-p] [-o OUT] SOURCE
//
// and always writes a stripped chunk (-s is accepted and changes nothing); -p only checks that the source compiles.
// Errors go to stderr as `coney_luac: <chunk>:<line>: <what>`, and the exit code is 1.
// @coney-only a developer tool for the round trip; the original has none

#include <cstdio>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "scripting/lua_chunk.h"
#include "scripting/lua_compiler.h"

namespace {

// Prints `message` as the tool's error and returns the failing exit code.
int failWith(const std::string& message) {
    std::fprintf(stderr, "coney_luac: %s\n", message.c_str());
    return 1;
}

} // namespace

// Reads the options, compiles the one source file and writes its chunk.
int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + 1, argv + argc);
    std::string output = "luac.out";
    std::string input;
    bool parseOnly = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "-s") {
            continue;
        }
        if (args[i] == "-p") {
            parseOnly = true;
        } else if (args[i] == "-o" && i + 1 < args.size()) {
            output = std::string(args[++i]);
        } else if (!args[i].starts_with('-') && input.empty()) {
            input = std::string(args[i]);
        } else {
            return failWith("usage: coney_luac [-s] [-p] [-o OUT] SOURCE");
        }
    }
    if (input.empty()) {
        return failWith("usage: coney_luac [-s] [-p] [-o OUT] SOURCE");
    }
    std::ifstream file(input, std::ios::binary);
    if (!file) {
        return failWith(input + ": cannot be read");
    }
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    auto proto = coney::script::compileLua(source, "@" + input);
    if (!proto) {
        return failWith(proto.error().message);
    }
    if (parseOnly) {
        return 0;
    }
    const std::vector<std::byte> chunk = coney::script::writeLuaChunk(*proto);
    std::ofstream out(output, std::ios::binary);
    out.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(chunk.size()));
    if (!out) {
        return failWith(output + ": cannot be written");
    }
    return 0;
}
