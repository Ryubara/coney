// SPDX-License-Identifier: GPL-3.0-or-later
#include "animation/anim_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace coney::anim {

Vec3 add(Vec3 a, Vec3 b) { return Vec3{a.x + b.x, a.y + b.y, a.z + b.z}; }

Vec3 subtract(Vec3 a, Vec3 b) { return Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 scale(Vec3 a, float s) { return Vec3{a.x * s, a.y * s, a.z * s}; }

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(Vec3 a) { return std::sqrt(dot(a, a)); }

float distance(Vec3 a, Vec3 b) { return length(subtract(a, b)); }

Vec3 normalise(Vec3 a) {
    const float l = length(a);
    return l > 0.0F ? scale(a, 1.0F / l) : a;
}

Vec3 cross(Vec3 a, Vec3 b) { return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

Vec3 lerp(Vec3 a, Vec3 b, float t) { return add(a, scale(subtract(b, a), t)); }

float dot(Quat a, Quat b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

Quat normalise(Quat q) {
    const float l = std::sqrt(dot(q, q));
    if (l <= 0.0F) {
        return Quat{};
    }
    return Quat{q.x / l, q.y / l, q.z / l, q.w / l};
}

Quat nlerp(Quat a, Quat b, float t) {
    // q and -q are the same rotation; pick the one on a's side so the blend takes the short way.
    if (dot(a, b) < 0.0F) {
        b = Quat{-b.x, -b.y, -b.z, -b.w};
    }
    return normalise(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
}

Quat slerp(Quat a, Quat b, float t) {
    float cosine = dot(a, b);
    if (cosine < 0.0F) {
        b = Quat{-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    // Nearly the same rotation: the sine below would be close to 0, and nlerp is as good there.
    constexpr float kNearlyEqual = 0.9995F;
    if (cosine > kNearlyEqual) {
        return nlerp(a, b, t);
    }
    const float angle = std::acos(std::clamp(cosine, -1.0F, 1.0F));
    const float sine = std::sin(angle);
    const float wa = std::sin((1.0F - t) * angle) / sine;
    const float wb = std::sin(t * angle) / sine;
    return normalise(Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

Quat quatFromMatrix(const Mat34& m) {
    // Shepperd's method: build from the largest of w, x, y, z to keep the square root away from 0. The matrix entry
    // at row r, column c is axis c's component r (m.x is column 0).
    const float m00 = m.x.x;
    const float m11 = m.y.y;
    const float m22 = m.z.z;
    const float traceValue = m00 + m11 + m22;
    Quat q;
    if (traceValue > 0.0F) {
        const float s = std::sqrt(traceValue + 1.0F) * 2.0F;
        q = Quat{(m.y.z - m.z.y) / s, (m.z.x - m.x.z) / s, (m.x.y - m.y.x) / s, 0.25F * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0F + m00 - m11 - m22) * 2.0F;
        q = Quat{0.25F * s, (m.y.x + m.x.y) / s, (m.z.x + m.x.z) / s, (m.y.z - m.z.y) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0F + m11 - m00 - m22) * 2.0F;
        q = Quat{(m.y.x + m.x.y) / s, 0.25F * s, (m.z.y + m.y.z) / s, (m.z.x - m.x.z) / s};
    } else {
        const float s = std::sqrt(1.0F + m22 - m00 - m11) * 2.0F;
        q = Quat{(m.z.x + m.x.z) / s, (m.z.y + m.y.z) / s, 0.25F * s, (m.x.y - m.y.x) / s};
    }
    return normalise(q);
}

Mat34 matrixFromQuat(Quat q) {
    q = normalise(q);
    const float xx = q.x * q.x;
    const float yy = q.y * q.y;
    const float zz = q.z * q.z;
    const float xy = q.x * q.y;
    const float xz = q.x * q.z;
    const float yz = q.y * q.z;
    const float wx = q.w * q.x;
    const float wy = q.w * q.y;
    const float wz = q.w * q.z;
    Mat34 m;
    m.x = Vec3{1.0F - 2.0F * (yy + zz), 2.0F * (xy + wz), 2.0F * (xz - wy)};
    m.y = Vec3{2.0F * (xy - wz), 1.0F - 2.0F * (xx + zz), 2.0F * (yz + wx)};
    m.z = Vec3{2.0F * (xz + wy), 2.0F * (yz - wx), 1.0F - 2.0F * (xx + yy)};
    return m;
}

Mat34 transform(Quat rotation, Vec3 translation) {
    Mat34 m = matrixFromQuat(rotation);
    m.t = translation;
    return m;
}

Mat34 multiply(const Mat34& a, const Mat34& b) {
    return Mat34{transformDirection(a, b.x), transformDirection(a, b.y), transformDirection(a, b.z),
                 transformPoint(a, b.t)};
}

Vec3 transformPoint(const Mat34& m, Vec3 p) { return add(transformDirection(m, p), m.t); }

Vec3 transformDirection(const Mat34& m, Vec3 d) { return add(add(scale(m.x, d.x), scale(m.y, d.y)), scale(m.z, d.z)); }

Mat34 inverseRigid(const Mat34& m) {
    Mat34 inverse;
    inverse.x = Vec3{m.x.x, m.y.x, m.z.x};
    inverse.y = Vec3{m.x.y, m.y.y, m.z.y};
    inverse.z = Vec3{m.x.z, m.y.z, m.z.z};
    inverse.t = scale(transformDirection(inverse, m.t), -1.0F);
    return inverse;
}

float maxDifference(const Mat34& a, const Mat34& b) {
    const std::array<Vec3, 4> as{a.x, a.y, a.z, a.t};
    const std::array<Vec3, 4> bs{b.x, b.y, b.z, b.t};
    float most = 0.0F;
    for (std::size_t i = 0; i < as.size(); ++i) {
        most = std::max({most, std::abs(as[i].x - bs[i].x), std::abs(as[i].y - bs[i].y), std::abs(as[i].z - bs[i].z)});
    }
    return most;
}

} // namespace coney::anim
