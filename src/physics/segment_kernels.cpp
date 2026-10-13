// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/segment_kernels.h"

#include <cmath>

#include "animation/anim_math.h"

namespace coney::physics {

namespace {

// The original's threshold for "parallel" and for "zero" numerators (0x358637bd).
constexpr float kTiny = 1e-6F;

} // namespace

float segmentClosestParam(anim::Vec3 p0, anim::Vec3 d, anim::Vec3 q) {
    const float k = anim::dot(anim::subtract(q, p0), d);
    if (k <= 0.0F) {
        return 0.0F;
    }
    const float dd = anim::dot(d, d);
    return dd < k ? 1.0F : k / dd;
}

anim::Vec3 segmentClosestPoint(anim::Vec3 p0, anim::Vec3 d, anim::Vec3 q) {
    return anim::add(p0, anim::scale(d, segmentClosestParam(p0, d, q)));
}

SegmentParams segmentClosestPoints(anim::Vec3 d1, anim::Vec3 d2, anim::Vec3 w) {
    const float a = anim::dot(d1, d1);
    const float b = anim::dot(d1, d2);
    const float c = anim::dot(d2, d2);
    const float d = anim::dot(d1, w);
    const float e = anim::dot(d2, w);
    const float det = a * c - b * b;
    // The unclamped s, then s clamped to [0, 1] with t recomputed for the clamped end.
    float sN = 0.0F;
    float sD = 1.0F;
    float tN = e;
    float tD = c;
    if (det >= kTiny) {
        sN = b * e - c * d;
        tN = a * e - b * d;
        sD = det;
        tD = det;
        if (sN < 0.0F) {
            sN = 0.0F;
            tN = e;
            tD = c;
        } else if (sN > sD) {
            sN = sD;
            tN = e + b;
            tD = c;
        }
    }
    // t clamped to [0, 1], with s recomputed for the clamped end.
    if (tN < 0.0F) {
        tN = 0.0F;
        if (-d < 0.0F) {
            sN = 0.0F;
        } else if (-d > a) {
            sN = sD;
        } else {
            sN = -d;
            sD = a;
        }
    } else if (tN > tD) {
        tN = tD;
        if (b - d < 0.0F) {
            sN = 0.0F;
        } else if (b - d > a) {
            sN = sD;
        } else {
            sN = b - d;
            sD = a;
        }
    }
    return SegmentParams{.s = std::fabs(sN) < kTiny ? 0.0F : sN / sD, .t = std::fabs(tN) < kTiny ? 0.0F : tN / tD};
}

bool capsuleBoxCornerCircle(float x, float y, float r, float hx, float hy, float& depth) {
    const float q = (x + hx) * (x + hx) + (y + hy) * (y + hy) - r * r;
    if (q > 0.0F) {
        return false;
    }
    depth = -q;
    return true;
}

bool capsuleBoxSideInterval(float x, float r, float hx, float& depth) {
    const float s = x + hx + r;
    if (s < 0.0F) {
        return false;
    }
    depth = s;
    return true;
}

} // namespace coney::physics
