#include "core/main_loop.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace coney::core;
using namespace std::chrono_literals;

namespace {

// A client whose frames take a scripted amount of clock time.
class ScriptedClient final : public LoopClient {
  public:
    ScriptedClient(ManualClock& clock, Nanoseconds frame_time) : clock_(clock), frame_time_(frame_time) {}

    bool begin_frame() override { return stop_after_ == 0 || begun_++ < stop_after_; }
    void tick() override { ++ticks; }
    void draw(double alpha) override {
        alphas.push_back(alpha);
        clock_.advance(frame_time_);
    }

    void stop_after(int frames) { stop_after_ = frames; }

    int ticks = 0;
    std::vector<double> alphas;

  private:
    ManualClock& clock_;
    Nanoseconds frame_time_;
    int stop_after_ = 0;
    int begun_ = 0;
};

} // namespace

TEST_CASE("main loop runs the paced ticks and one draw a frame") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Off);
    ScriptedClient client(clock, 10ms);

    const LoopSummary summary = run_main_loop(client, clock, LoopSettings{}, logger, {.max_frames = 301});

    // 300 frames of 10 ms have elapsed by the last frame: 3 s at 30 ticks a second.
    CHECK(summary.frames == 301);
    CHECK(summary.ticks == 90);
    CHECK(client.ticks == 90);
    CHECK(client.alphas.size() == 301);
    for (double alpha : client.alphas) {
        CHECK(alpha >= 0.0);
        CHECK(alpha < 1.0);
    }
}

TEST_CASE("main loop stops when the client asks") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Off);
    ScriptedClient client(clock, 10ms);
    client.stop_after(7);

    const LoopSummary summary = run_main_loop(client, clock, LoopSettings{}, logger);
    CHECK(summary.frames == 7);
}

TEST_CASE("main loop applies the catch-up cap from its settings") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Warning);
    MemorySink sink;
    logger.add_sink(sink);
    ScriptedClient client(clock, 1s); // every frame stalls for 30 ticks

    const LoopSummary summary =
        run_main_loop(client, clock, LoopSettings{.ticks_per_second = 30, .max_catch_up_ticks = 3}, logger,
                      {.max_frames = 4});
    CHECK(summary.ticks == 9);
    CHECK(summary.dropped_ticks == 81);
    REQUIRE(sink.lines.size() == 3);
    CHECK(sink.lines[0].ends_with("WARNING loop: fell behind; dropped 27 ticks"));
}

TEST_CASE("main loop logs tick and frame rates once a second at debug level") {
    ManualClock clock;
    Logger logger(clock, LogLevel::Debug);
    MemorySink sink;
    logger.add_sink(sink);
    ScriptedClient client(clock, 8ms);

    run_main_loop(client, clock, LoopSettings{}, logger, {.max_frames = 125 * 3 + 1});

    REQUIRE(sink.lines.size() == 3);
    for (const auto& line : sink.lines) {
        CHECK(line.find("DEBUG   loop: 30.0 ticks/s, 125.0 frames/s") != std::string::npos);
    }
}
