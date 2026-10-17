#include "core/command_line.hpp"

#include <charconv>
#include <format>

namespace coney::core {

namespace {

template <typename T> std::optional<T> parse_number(std::string_view text, T lowest) {
    T value{};
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size() || value < lowest) {
        return std::nullopt;
    }
    return value;
}

} // namespace

std::string_view command_line_help() noexcept {
    return "usage: coney [options]\n"
           "  --frames N          stop after N frames\n"
           "  --title TEXT        window title\n"
           "  --width N           window width in pixels\n"
           "  --height N          window height in pixels\n"
           "  --max-catch-up N    most logic ticks run in one frame after a stall\n"
           "  --log-level LEVEL   debug, info, warning, error or off\n"
           "  --log-file PATH     also append the log to PATH\n"
           "  --help              show this text\n";
}

Result<CommandLine, std::string> parse_command_line(std::span<const std::string_view> args,
                                                    const Settings& defaults) {
    CommandLine out;
    out.settings = defaults;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string_view option = args[i];
        if (option == "--help" || option == "-h") {
            out.help = true;
            continue;
        }
        if (i + 1 >= args.size()) {
            return Failure{std::format("unknown option or missing value: {}", option)};
        }
        const std::string_view value = args[++i];
        auto bad_value = [&] { return Failure{std::format("bad value for {}: {}", option, value)}; };

        if (option == "--frames") {
            const auto n = parse_number<std::uint64_t>(value, 1);
            if (!n) {
                return bad_value();
            }
            out.frames = *n;
        } else if (option == "--title") {
            out.settings.window.title = std::string(value);
        } else if (option == "--width" || option == "--height") {
            const auto n = parse_number<int>(value, 1);
            if (!n) {
                return bad_value();
            }
            (option == "--width" ? out.settings.window.width : out.settings.window.height) = *n;
        } else if (option == "--max-catch-up") {
            const auto n = parse_number<int>(value, 1);
            if (!n) {
                return bad_value();
            }
            out.settings.loop.max_catch_up_ticks = *n;
        } else if (option == "--log-level") {
            const auto level = parse_level(value);
            if (!level) {
                return bad_value();
            }
            out.settings.log.level = *level;
        } else if (option == "--log-file") {
            out.settings.log.file = std::string(value);
        } else {
            return Failure{std::format("unknown option: {}", option)};
        }
    }
    return out;
}

} // namespace coney::core
