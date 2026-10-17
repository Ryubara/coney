// coney: opens the window and runs the main loop until it is closed. No game behaviour yet.

#include <cstdio>
#include <string_view>
#include <vector>

#include "core/clock.hpp"
#include "core/command_line.hpp"
#include "core/log.hpp"
#include "core/main_loop.hpp"
#include "platform/platform.hpp"

namespace {

using namespace coney;

class App final : public core::LoopClient {
  public:
    App(platform::Platform& platform, platform::GpuContext& gpu, const core::Settings& settings)
        : platform_(platform), gpu_(gpu), settings_(settings) {}

    bool begin_frame() override {
        while (const auto event = platform_.poll_event()) {
            if (std::holds_alternative<platform::CloseRequested>(*event)) {
                platform_.logger().info(core::log_category::kApp, "close requested");
                return false;
            }
            if (const auto* resized = std::get_if<platform::Resized>(&*event)) {
                platform_.logger().debug(core::log_category::kApp, "window is now {}x{} pixels", resized->width,
                                         resized->height);
            }
        }
        return !failed_;
    }

    void tick() override {}

    void draw(double /*alpha*/) override { failed_ = !gpu_.clear(settings_.clear_colour); }

    [[nodiscard]] bool failed() const noexcept { return failed_; }

  private:
    platform::Platform& platform_;
    platform::GpuContext& gpu_;
    const core::Settings& settings_;
    bool failed_ = false;
};

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string_view> args(argv + (argc > 0 ? 1 : 0), argv + argc);
    const auto command_line = core::parse_command_line(args);
    if (!command_line) {
        std::fprintf(stderr, "coney: %s\n%.*s", command_line.error().c_str(),
                     static_cast<int>(core::command_line_help().size()), core::command_line_help().data());
        return 2;
    }
    if (command_line->help) {
        std::printf("%.*s", static_cast<int>(core::command_line_help().size()), core::command_line_help().data());
        return 0;
    }
    const core::Settings& settings = command_line->settings;

    const core::SteadyClock clock;
    core::Logger logger(clock, settings.log.level);
    logger.add_sink(std::make_unique<core::StderrSink>());
    if (!settings.log.file.empty()) {
        auto file = core::FileSink::open(settings.log.file);
        if (!file) {
            logger.error(core::log_category::kApp, "cannot open the log file {}", settings.log.file);
            return 1;
        }
        logger.add_sink(std::move(file));
    }

    auto platform = platform::Platform::start(logger);
    if (!platform) {
        logger.error(core::log_category::kPlatform, "{}", platform.error());
        return 1;
    }
    auto window = platform::Window::open(**platform, settings.window);
    if (!window) {
        logger.error(core::log_category::kWindow, "{}", window.error());
        return 1;
    }
    auto gpu = platform::GpuContext::create(**window);
    if (!gpu) {
        logger.error(core::log_category::kGpu, "{}", gpu.error());
        return 1;
    }

    App app(**platform, **gpu, settings);
    const auto summary = core::run_main_loop(app, clock, settings.loop, logger, {.max_frames = command_line->frames});
    logger.info(core::log_category::kApp, "stopped after {} frames and {} ticks", summary.frames, summary.ticks);
    return app.failed() ? 1 : 0;
}
