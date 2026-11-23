// SPDX-License-Identifier: GPL-3.0-or-later
// The GPU renderer's screen passes (the blur pulse, the line blend, the motion blur): laid out as copies and draws by
// graphics::screenStepsOf() and recorded here, apart from GpuRenderer::record()'s uploads and passes
// (docs/engine/gpu-renderer.md#screen-passes).
#include <algorithm>
#include <span>
#include <utility>
#include <variant>

#include <SDL3/SDL.h>

#include "platform/gpu_renderer.h"
#include "platform/gpu_uniforms.h"

namespace coney::platform {

void GpuRenderer::releaseScreenCopy(graphics::ScreenCopyId id) {
    std::erase_if(m_kept, [id](const KeptFrame& frame) { return frame.id == id; });
}

GpuRenderer::KeptFrame& GpuRenderer::keptFrame(graphics::ScreenCopyId id) {
    const auto found = std::ranges::find_if(m_kept, [id](const KeptFrame& frame) { return frame.id == id; });
    if (found != m_kept.end()) {
        return *found;
    }
    // A copy RenderEngine gave out (its numbers are the GL3 renderer's, on every backend), drawn here the first time.
    KeptFrame& made = m_kept.emplace_back();
    made.id = id;
    return made;
}

SDL_GPUTexture* GpuRenderer::copyTexture(graphics::ScreenSlot slot, graphics::ScreenCopyId copy, int width,
                                         int height) {
    OwnedTexture* texture = &m_viewCopy;
    switch (slot) {
    case graphics::ScreenSlot::View:
        break;
    case graphics::ScreenSlot::Half:
        texture = &m_halfCopy;
        break;
    case graphics::ScreenSlot::Kept:
        texture = &keptFrame(copy).texture;
        break;
    }
    if (texture->texture != nullptr && texture->width == width && texture->height == height) {
        return texture->texture;
    }
    // Made again at the size asked for, in the target's format: SDL lets the old one go once the GPU has finished
    // with it.
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = static_cast<SDL_GPUTextureFormat>(targetFormat());
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = static_cast<Uint32>(width);
    info.height = static_cast<Uint32>(height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    SDL_GPUTexture* made = SDL_CreateGPUTexture(m_device, &info);
    *texture = made != nullptr ? OwnedTexture(m_device, made, width, height, true) : OwnedTexture();
    return made;
}

std::vector<GpuRenderer::ScreenPlan> GpuRenderer::planScreenPasses(const graphics::FrameDescription& frame,
                                                                   graphics::Extent size,
                                                                   graphics::OverlayBatch& batch) {
    std::vector<ScreenPlan> plans;
    const std::span<const graphics::FramePass> passes = frame.passes();
    for (std::size_t p = 0; p < passes.size(); ++p) {
        const auto* screen = std::get_if<graphics::ScreenPass>(&passes[p]);
        if (screen == nullptr) {
            continue;
        }
        // A pass over more than the target has nothing it could copy there: left out.
        const graphics::ScreenRect& area = screen->area;
        if (area.x < 0 || area.y < 0 || area.x + area.width > size.width || area.y + area.height > size.height) {
            continue;
        }
        ScreenPlan plan;
        plan.pass = p;
        // The motion blur lays its kept frame only when one of the view's size is kept, as the GL3 renderer does.
        KeptFrame* kept = nullptr;
        if (const auto* blur = std::get_if<graphics::MotionBlurOverlay>(&screen->effect); blur != nullptr) {
            plan.copy = blur->copy;
            kept = &keptFrame(blur->copy);
        }
        const bool keptReady = kept != nullptr && kept->kept && kept->texture.texture != nullptr &&
                               kept->texture.width == area.width && kept->texture.height == area.height;
        graphics::ScreenSteps steps = graphics::screenStepsOf(*screen, keptReady);
        if (kept != nullptr && steps.forgetKept) {
            kept->kept = false;
        }
        // Each draw's quad: four corners and two triangles (0, 1, 2 and 0, 2, 3), after the 2D draws'.
        plan.firstIndex.assign(steps.steps.size(), 0);
        for (std::size_t i = 0; i < steps.steps.size(); ++i) {
            if (const auto* draw = std::get_if<graphics::ScreenDrawStep>(&steps.steps[i]); draw != nullptr) {
                const auto base = static_cast<std::uint32_t>(batch.vertices.size());
                plan.firstIndex[i] = static_cast<std::uint32_t>(batch.indices.size());
                batch.vertices.insert(batch.vertices.end(), draw->corners.begin(), draw->corners.end());
                for (const std::uint32_t corner : {0U, 1U, 2U, 0U, 2U, 3U}) {
                    batch.indices.push_back(base + corner);
                }
            } else if (kept != nullptr) {
                kept->kept = true; // the copy into the kept frame: the next blur lays it
            }
        }
        plan.steps = std::move(steps.steps);
        if (!plan.steps.empty()) {
            plans.push_back(std::move(plan));
        }
    }
    return plans;
}

bool GpuRenderer::recordScreenPass(SDL_GPUCommandBuffer* commands, const ScreenPlan& plan, SDL_GPURenderPass*& pass,
                                   const std::function<bool()>& resume, const std::function<void(bool)>& bind2d) {
    for (std::size_t i = 0; i < plan.steps.size(); ++i) {
        // A copy: the render pass ended, then the target's rectangle copied in a copy pass.
        if (const auto* copy = std::get_if<graphics::ScreenCopyStep>(&plan.steps[i]); copy != nullptr) {
            if (pass != nullptr) {
                SDL_EndGPURenderPass(pass);
                pass = nullptr;
            }
            SDL_GPUTexture* into = copyTexture(copy->into, plan.copy, copy->from.width, copy->from.height);
            if (into == nullptr) {
                return true; // SDL refused the copy: the rest of the pass is left out, the frame goes on
            }
            SDL_GPUTextureLocation from{};
            from.texture = m_target.texture;
            from.x = static_cast<Uint32>(copy->from.x);
            from.y = static_cast<Uint32>(copy->from.y);
            SDL_GPUTextureLocation to{};
            to.texture = into;
            SDL_GPUCopyPass* copying = SDL_BeginGPUCopyPass(commands);
            SDL_CopyGPUTextureToTexture(copying, &from, &to, static_cast<Uint32>(copy->from.width),
                                        static_cast<Uint32>(copy->from.height), 1, false);
            SDL_EndGPUCopyPass(copying);
            continue;
        }
        // A draw: its quad from the copy, sampled linearly and clamped, as the GL3 renderer's.
        const auto& draw = std::get<graphics::ScreenDrawStep>(plan.steps[i]);
        if (pass == nullptr && !resume()) {
            return false;
        }
        SDL_GPUTexture* source = draw.from == graphics::ScreenSlot::View   ? m_viewCopy.texture
                                 : draw.from == graphics::ScreenSlot::Half ? m_halfCopy.texture
                                                                           : keptFrame(plan.copy).texture.texture;
        if (source == nullptr) {
            continue;
        }
        bind2d(draw.opaque);
        const SDL_GPUTextureSamplerBinding binding{source, samplerFor(true, graphics::TextureAddress::Clamp)};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        const AlphaUniform alpha{draw.alpha.low, draw.alpha.high, {0, 0}};
        SDL_PushGPUFragmentUniformData(commands, 0, &alpha, sizeof(alpha));
        SDL_DrawGPUIndexedPrimitives(pass, 6, 1, plan.firstIndex[i], 0, 0);
    }
    return true;
}

} // namespace coney::platform
