// SPDX-License-Identifier: GPL-3.0-or-later
// Posing the humans on the worker threads (docs/engine/renderer-performance.md#posing-on-workers): Coney-made
// characters posed headless through prepareHumanPoses() and commitHumanPose() on pools of one and four threads, against
// each posed whole by CharacterMesh::pose() on this thread. No game data: every vertex and bone is made here.
#include "platform/human_posing.h"

#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rw.h>

#include "animation/anim_math.h"
#include "animation/anim_pose.h"
#include "animation/skeleton.h"
#include "characters/character_model.h"
#include "characters/character_rig.h"
#include "core/worker_pool.h"
#include "human/target_human.h"
#include "platform/character_mesh.h"
#include "platform/render_engine.h"
#include "support/character_fixtures.h"

using coney::anim::Vec3;
using coney::human::TargetSnapshot;
using coney::platform::CharacterMesh;
using coney::platform::HumanPoseJob;
using coney::platform::RenderBackend;
using coney::platform::RenderEngine;

namespace {

// The headless renderer the meshes are made on.
std::unique_ptr<RenderEngine> startHeadless() {
    coney::platform::WindowDesc desc;
    desc.width = 640;
    desc.height = 448;
    auto engine = RenderEngine::start(RenderBackend::Null, desc);
    REQUIRE(engine.has_value());
    return std::move(*engine);
}

// Human `n`'s snapshot at one step (`late` the later of two): the skeleton's bind pose with bone 3 turned about z by an
// angle of its own, standing somewhere of his own and facing his own way.
TargetSnapshot snapshotOf(const coney::anim::Skeleton& skeleton, std::size_t n, bool late) {
    TargetSnapshot snapshot;
    snapshot.pose.rotations = skeleton.bindRotations;
    const float angle = (0.07F * static_cast<float>(n)) + (late ? 0.3F : 0.0F);
    snapshot.pose.rotations[3] = coney::anim::Quat{0.0F, 0.0F, std::sin(angle / 2.0F), std::cos(angle / 2.0F)};
    snapshot.feet = Vec3{static_cast<float>(n % 7) - 3.0F + (late ? 0.1F : 0.0F), static_cast<float>(n / 7), 0.0F};
    snapshot.heading = (0.4F * static_cast<float>(n)) + (late ? 0.05F : 0.0F);
    return snapshot;
}

// What a posed mesh holds that a draw reads: its skinning matrices, both atomics' frames and bounding spheres, and its
// first atomic's vertices and normals.
struct PosedMesh {
    std::vector<coney::anim::Mat34> skinning;
    std::vector<float> numbers;
};

// Reads what a draw of `mesh` would see, as plain numbers compared exactly.
PosedMesh readPosed(const CharacterMesh& mesh) {
    PosedMesh posed;
    posed.skinning.assign(mesh.skinning().begin(), mesh.skinning().end());
    for (rw::Atomic* atomic : {mesh.atomic(), mesh.bloodAtomic()}) {
        const rw::Matrix* frame = atomic->getFrame()->getLTM();
        for (const rw::V3d& v : {frame->right, frame->up, frame->at, frame->pos}) {
            posed.numbers.insert(posed.numbers.end(), {v.x, v.y, v.z});
        }
        const rw::Sphere& sphere = atomic->boundingSphere;
        posed.numbers.insert(posed.numbers.end(), {sphere.center.x, sphere.center.y, sphere.center.z, sphere.radius});
        const rw::MorphTarget& target = atomic->geometry->morphTargets[0];
        for (rw::int32 i = 0; i < atomic->geometry->numVertices; ++i) {
            posed.numbers.insert(posed.numbers.end(),
                                 {target.vertices[i].x, target.vertices[i].y, target.vertices[i].z});
            if (target.normals != nullptr) {
                posed.numbers.insert(posed.numbers.end(),
                                     {target.normals[i].x, target.normals[i].y, target.normals[i].z});
            }
        }
    }
    return posed;
}

bool samePose(const PosedMesh& a, const PosedMesh& b) {
    if (a.skinning.size() != b.skinning.size() || a.numbers != b.numbers) {
        return false;
    }
    for (std::size_t i = 0; i < a.skinning.size(); ++i) {
        const coney::anim::Mat34& m = a.skinning[i];
        const coney::anim::Mat34& n = b.skinning[i];
        if (m.x != n.x || m.y != n.y || m.z != n.z || m.t != n.t) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("humans posed on one worker or four match each posed alone", "[platform][humans][workers]") {
    // Expected: docs/engine/renderer-performance.md#posing-on-workers: posing on the workers does the same arithmetic
    // per human as CharacterMesh::pose() on the main thread (his blend, his matrices, his scale, his placement), so 40
    // humans, some blended between two steps, some given a pose (a scene's), some scaled, come out bit for bit as
    // posed one by one, with a pool of one thread and of four.
    auto engine = startHeadless();
    auto model = coney::characters::decodeCharacterModel(
        coney::test::characterClump(coney::test::testCharacterFields()).span(), coney::test::testBoneOffsets().span());
    REQUIRE(model.has_value());
    const coney::anim::Skeleton skeleton = coney::characters::characterSkeleton(*model);
    constexpr std::size_t kHumans = 40;
    std::vector<TargetSnapshot> before;
    std::vector<TargetSnapshot> after;
    for (std::size_t n = 0; n < kHumans; ++n) {
        before.push_back(snapshotOf(skeleton, n, false));
        after.push_back(snapshotOf(skeleton, n, true));
    }
    constexpr float kAlpha = 0.37F;
    // Every third human is given his later snapshot as a scene would; every fifth is scaled.
    const auto scaleOf = [](std::size_t n) { return n % 5 == 0 ? 1.25F : 1.0F; };
    const auto givenPose = [](std::size_t n) { return n % 3 == 0; };

    // Each posed alone, whole, on this thread.
    std::vector<PosedMesh> expected;
    {
        for (std::size_t n = 0; n < kHumans; ++n) {
            CharacterMesh mesh(*model, nullptr, coney::platform::Skinning::Gpu);
            const TargetSnapshot pose =
                givenPose(n) ? after[n] : coney::human::interpolate(before[n], after[n], kAlpha);
            mesh.pose(coney::platform::scaledSkinning(skeleton, *model, pose.pose, scaleOf(n)),
                      coney::platform::characterPlacement(pose.feet, pose.heading, 0.0F));
            expected.push_back(readPosed(mesh));
        }
    }
    for (const std::size_t threads : {std::size_t{1}, std::size_t{4}}) {
        coney::WorkerPool pool(threads);
        std::vector<std::unique_ptr<CharacterMesh>> meshes;
        std::vector<HumanPoseJob> jobs;
        for (std::size_t n = 0; n < kHumans; ++n) {
            meshes.push_back(std::make_unique<CharacterMesh>(*model, nullptr, coney::platform::Skinning::Gpu));
            HumanPoseJob job{.mesh = meshes.back().get(), .skeleton = &skeleton, .model = &*model, .scale = scaleOf(n)};
            if (givenPose(n)) {
                job.pose = after[n];
            } else {
                job.previous = &before[n];
                job.current = &after[n];
                job.alpha = kAlpha;
            }
            jobs.push_back(job);
        }
        coney::platform::prepareHumanPoses(pool, jobs);
        for (const HumanPoseJob& job : jobs) {
            coney::platform::commitHumanPose(job);
        }
        for (std::size_t n = 0; n < kHumans; ++n) {
            INFO("threads " << threads << ", human " << n);
            CHECK(samePose(readPosed(*meshes[n]), expected[n]));
        }
    }
}
