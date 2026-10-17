#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/settings.hpp"
#include "result.hpp"

namespace coney::core {

struct CommandLine {
    Settings settings;
    std::optional<std::uint64_t> frames; // --frames N: stop after N frames
    std::string write_bindings;          // --write-bindings PATH: write the bindings in use to PATH and stop
    bool help = false;
};

// Reads the options after the program name over a copy of `defaults`. The error text names the bad option.
[[nodiscard]] Result<CommandLine, std::string> parse_command_line(std::span<const std::string_view> args,
                                                                  const Settings& defaults = {});

[[nodiscard]] std::string_view command_line_help() noexcept;

} // namespace coney::core
