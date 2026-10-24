// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/pad_pipe.h"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/pad.h"

using coney::PadSample;
using coney::platform::PadPipe;
using coney::platform::PipeView;

TEST_CASE("the pad pipe answers each frame with the driver's pad after the observation", "[pad_pipe]") {
    std::istringstream in("observe dyn_bat dyn_w_mission\npad 40 128 128 200 60\r\n\npad 0 128 128 128 128\n");
    std::ostringstream out;
    PadPipe pipe(in, out, [] { return PipeView{}; });

    const coney::PortSamples first = pipe.sample(0);
    CHECK(first[0].connected);
    CHECK(first[0].buttons == coney::pad::kCross);
    CHECK(first[0].sticks[2] == 200);
    CHECK(first[0].sticks[3] == 60);
    const coney::PortSamples second = pipe.sample(1);
    CHECK(second[0].buttons == 0);
    // The input ended: a released pad, and nothing more is written.
    const coney::PortSamples third = pipe.sample(2);
    CHECK(third[0].buttons == 0);
    CHECK(third[0].sticks[2] == coney::pad::kStickCentre);
    const coney::PortSamples fourth = pipe.sample(3);
    CHECK(fourth[0].buttons == 0);
    // No level in play: the observations say so, one per frame asked for before the input ended.
    CHECK(out.str() == "@obs {\"frame\":0,\"play\":false}\n@obs {\"frame\":1,\"play\":false}\n"
                       "@obs {\"frame\":2,\"play\":false}\n");
}

TEST_CASE("a call line runs a script function before the frame's pad and says how it went", "[pad_pipe]") {
    std::istringstream in("call P3.DealerPoizo\ncall Missing\npad 0 128 128 128 128\ncall Late\n");
    std::ostringstream out;
    std::vector<std::string> called;
    PadPipe pipe(
        in, out, [] { return PipeView{}; },
        [&called](std::string_view name) {
            called.emplace_back(name);
            return name != "Missing";
        });

    static_cast<void>(pipe.sample(0));
    CHECK(called == std::vector<std::string>{"P3.DealerPoizo", "Missing"});
    // The last call comes with no pad after it: it still runs, then the input has ended.
    static_cast<void>(pipe.sample(1));
    CHECK(called.size() == 3);
    CHECK(out.str() == "@obs {\"frame\":0,\"play\":false}\n@called P3.DealerPoizo ok\n@called Missing failed\n"
                       "@obs {\"frame\":1,\"play\":false}\n@called Late ok\n");
}

TEST_CASE("without a caller a call line is refused", "[pad_pipe]") {
    std::istringstream in("call P3.DealerPoizo\npad 0 128 128 128 128\n");
    std::ostringstream out;
    PadPipe pipe(in, out, [] { return PipeView{}; });
    static_cast<void>(pipe.sample(0));
    CHECK(out.str() == "@obs {\"frame\":0,\"play\":false}\n@error no script to call P3.DealerPoizo in\n");
}

TEST_CASE("a pad line needs five numbers in range", "[pad_pipe]") {
    PadSample pad;
    CHECK(PadPipe::parsePad("80 128 128 128 128", pad));
    CHECK(pad.buttons == coney::pad::kSquare);
    CHECK(pad.pressure.at(static_cast<std::size_t>(coney::pad::Pressure::Square)) == 255);
    CHECK_FALSE(PadPipe::parsePad("80 128 128 128", pad));
    CHECK_FALSE(PadPipe::parsePad("10000 128 128 128 128", pad));
    CHECK_FALSE(PadPipe::parsePad("0 128 128 300 128", pad));
    CHECK_FALSE(PadPipe::parsePad("x 128 128 128 128", pad));
}
