// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>

// The small vector, quaternion and matrix arithmetic the animation player and the skinning need. Plain values with
// no hidden state, so sampling and skinning are pure and give the same result on every platform for the same input.
// The interpolation functions are the original's maths helpers (docs/research/formats/animation.md#original-structure).

namespace coney::anim {

/// A point or a direction.
struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

/// A rotation as a unit quaternion (x, y, z, w); the default is no rotation.
struct Quat {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;

    friend bool operator==(const Quat&, const Quat&) = default;
};

/// An affine transform acting on column vectors: `p' = x * p.x + y * p.y + z * p.z + t`. The three axes are the
/// images of the unit vectors, laid out as RenderWare's `right`, `up`, `at` and `pos` (a frame's matrix in a clump
/// reads straight into it).
struct Mat34 {
    Vec3 x{1.0F, 0.0F, 0.0F};
    Vec3 y{0.0F, 1.0F, 0.0F};
    Vec3 z{0.0F, 0.0F, 1.0F};
    Vec3 t;
};

/// `a + b`, `a - b`, `a * s`.
[[nodiscard]] Vec3 add(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 subtract(Vec3 a, Vec3 b);
[[nodiscard]] Vec3 scale(Vec3 a, float s);
/// The dot product and the length.
[[nodiscard]] float dot(Vec3 a, Vec3 b);
[[nodiscard]] float length(Vec3 a);
/// The distance between two points.
[[nodiscard]] float distance(Vec3 a, Vec3 b);
/// `a` scaled to length 1; `a` itself when its length is 0.
[[nodiscard]] Vec3 normalise(Vec3 a);
/// The cross product `a × b`.
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b);

/// Linear interpolation, `a + (b - a) * t`.
/// @orig 0x00336bb8 VectorLerp (unknown)
[[nodiscard]] Vec3 lerp(Vec3 a, Vec3 b, float t);

/// The four-component dot product of two quaternions.
[[nodiscard]] float dot(Quat a, Quat b);
/// `q` scaled to length 1; no rotation when its length is 0.
[[nodiscard]] Quat normalise(Quat q);

/// Normalised linear interpolation: `b` is negated first when the dot product is negative, so the blend takes the
/// short way round, then the components are lerped and the result normalised. The keyframe sampler's interpolation.
/// @orig 0x00336bf8 QuaternionNlerp (unknown)
[[nodiscard]] Quat nlerp(Quat a, Quat b, float t);

/// Spherical linear interpolation at constant angular speed, the short way round (as nlerp() decides it); falls back
/// to nlerp() when the two are nearly equal. The pose blender's interpolation.
/// @orig 0x00336a00 QuaternionSlerp (unknown)
[[nodiscard]] Quat slerp(Quat a, Quat b, float t);

/// The quaternion of a rotation matrix's 3 × 3 part, which must be a rotation (orthonormal, determinant 1).
[[nodiscard]] Quat quatFromMatrix(const Mat34& m);

/// The rotation matrix of `q` (normalised first), with no translation.
[[nodiscard]] Mat34 matrixFromQuat(Quat q);

/// The transform that rotates by `rotation`, then moves by `translation`.
[[nodiscard]] Mat34 transform(Quat rotation, Vec3 translation);

/// `a * b`: the transform that applies `b`, then `a`.
[[nodiscard]] Mat34 multiply(const Mat34& a, const Mat34& b);

/// `m` applied to a point (with its translation) and to a direction (without).
[[nodiscard]] Vec3 transformPoint(const Mat34& m, Vec3 p);
[[nodiscard]] Vec3 transformDirection(const Mat34& m, Vec3 d);

/// The inverse of a rigid transform (rotation and translation only): the transposed rotation and the translation
/// taken back through it.
[[nodiscard]] Mat34 inverseRigid(const Mat34& m);

/// The largest absolute difference between the twelve entries of two transforms, for tests and checks.
[[nodiscard]] float maxDifference(const Mat34& a, const Mat34& b);

} // namespace coney::anim
