// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/body_size.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace coney::characters {

namespace {

// The brain types the size reads: a cop and a gang member may grow, a Warrior has no random size.
constexpr int kCopBrain = 1;
constexpr int kGangBrain = 2;
constexpr int kWarriorBrain = 3;
// The categories with no random size.
constexpr int kSkippedCategoryA = 12;
constexpr int kSkippedCategoryB = 13;
// The class types with no random size.
constexpr std::array<int, 14> kSkippedTypes{122, 221, 222, 303, 304, 305, 307, 351, 414, 415, 416, 425, 458, 459};
// The draw is 0-99; its bands for a man, and the gang kind below which a gang member grows.
constexpr std::int32_t kDrawMax = 99;
constexpr std::int32_t kPlainUpTo = 30;
constexpr std::int32_t kSizedUpTo = 54;
constexpr std::int32_t kTallerBelow = 90;
constexpr int kGrowingGangKinds = 23;
// A woman loses 0.01 for each whole quarter of the draw, and a hat keeps her at 0.99 or more.
constexpr std::int32_t kWomanStep = 25;
constexpr float kWomanShrink = 0.01F;
constexpr float kHattedWomanLeast = 0.99F;

} // namespace

std::optional<float> randomBodySize(const BodySizeInput& input, GameRandom& random) {
    // The humans the size skips make no draw.
    if (input.brainType == kWarriorBrain || input.category == kSkippedCategoryA ||
        input.category == kSkippedCategoryB ||
        std::ranges::find(kSkippedTypes, input.classType) != kSkippedTypes.end()) {
        return std::nullopt;
    }
    const std::int32_t r = random.range(RandomStream::Gameplay, 0, kDrawMax);
    if (input.woman) {
        // The original divides in integers first (0x00218008): only whole quarters count, so 24 still gives 1.0.
        const std::int32_t wholeQuarters = r / kWomanStep;
        const float size = 1.0F - (static_cast<float>(wholeQuarters) * kWomanShrink);
        return input.hat && !(kHattedWomanLeast < size) ? kHattedWomanLeast : size;
    }
    if (r <= kPlainUpTo) {
        return 1.0F;
    }
    // 31-54 without a hat: a cop, or a gang member of a gang whose kind is below 23, grows; anyone else shrinks.
    if (r <= kSizedUpTo && !input.hat) {
        const bool grows = input.brainType == kCopBrain ||
                           (input.brainType == kGangBrain && input.gangKind && *input.gangKind < kGrowingGangKinds);
        return grows ? 1.02F : 0.98F;
    }
    return r < kTallerBelow ? 1.01F : 1.0F;
}

} // namespace coney::characters
