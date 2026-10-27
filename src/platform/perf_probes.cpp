// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/perf_probes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <new>
#include <string>

#include <rw.h>

#include "core/perf.h"

namespace coney::platform {

namespace {

// The steady clock in nanoseconds since its epoch.
std::uint64_t steadyNanoseconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

// The allocation totals, from every thread.
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_allocatedBytes{0};

// Counts one allocation and makes it with malloc (the matching deletes below free it).
void* countedAllocation(std::size_t bytes) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    g_allocatedBytes.fetch_add(bytes, std::memory_order_relaxed);
    return std::malloc(bytes == 0 ? 1 : bytes);
}

// The OpenGL counters: the main thread draws, so plain integers.
std::uint64_t g_draws = 0;
std::uint64_t g_triangles = 0;
std::uint64_t g_uploads = 0;
std::uint64_t g_uploadBytes = 0;
std::uint64_t g_programs = 0;
std::uint64_t g_textureBinds = 0;

// The scene counters: each kind's totals, and the kind the draws count as now.
std::array<DrawTotals, kDrawKinds> g_kinds{};
DrawKind g_kind = DrawKind::Other;

// The functions the wrappers forward to; null while they are not installed.
PFNGLDRAWELEMENTSPROC g_drawElements = nullptr;
PFNGLDRAWARRAYSPROC g_drawArrays = nullptr;
PFNGLBUFFERDATAPROC g_bufferData = nullptr;
PFNGLBUFFERSUBDATAPROC g_bufferSubData = nullptr;
PFNGLUSEPROGRAMPROC g_useProgram = nullptr;
PFNGLBINDTEXTUREPROC g_bindTexture = nullptr;

// The triangles a draw of `count` vertices or indices makes in `mode`.
std::uint64_t trianglesOf(GLenum mode, GLsizei count) {
    if (count <= 0) {
        return 0;
    }
    const auto n = static_cast<std::uint64_t>(count);
    switch (mode) {
    case GL_TRIANGLES:
        return n / 3;
    case GL_TRIANGLE_STRIP:
    case GL_TRIANGLE_FAN:
        return n >= 3 ? n - 2 : 0;
    default:
        return 0;
    }
}

// The vertices a draw of `count` sent.
std::uint64_t verticesOf(GLsizei count) { return count > 0 ? static_cast<std::uint64_t>(count) : 0; }

// Counts an indexed draw, then forwards it to the real glDrawElements.
void APIENTRY countDrawElements(GLenum mode, GLsizei count, GLenum type, const void* indices) {
    ++g_draws;
    g_triangles += trianglesOf(mode, count);
    recordDraw(trianglesOf(mode, count), verticesOf(count));
    g_drawElements(mode, count, type, indices);
}

// Counts an array draw, then forwards it to the real glDrawArrays.
void APIENTRY countDrawArrays(GLenum mode, GLint first, GLsizei count) {
    ++g_draws;
    g_triangles += trianglesOf(mode, count);
    recordDraw(trianglesOf(mode, count), verticesOf(count));
    g_drawArrays(mode, first, count);
}

// Counts the bytes of a buffer upload, then forwards it to the real glBufferData.
void APIENTRY countBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    if (data != nullptr) {
        ++g_uploads;
        g_uploadBytes += static_cast<std::uint64_t>(size);
    }
    g_bufferData(target, size, data, usage);
}

// Counts the bytes of a partial buffer upload, then forwards it to the real glBufferSubData.
void APIENTRY countBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    ++g_uploads;
    g_uploadBytes += static_cast<std::uint64_t>(size);
    g_bufferSubData(target, offset, size, data);
}

// Counts a shader program switch, then forwards it to the real glUseProgram.
void APIENTRY countUseProgram(GLuint program) {
    ++g_programs;
    g_useProgram(program);
}

// Counts a texture bind, then forwards it to the real glBindTexture.
void APIENTRY countBindTexture(GLenum target, GLuint texture) {
    ++g_textureBinds;
    g_bindTexture(target, texture);
}

} // namespace

void installPerfClock() { perf::setClock(&steadyNanoseconds); }

DrawKindScope::DrawKindScope(DrawKind kind) : m_previous(g_kind) { g_kind = kind; }

DrawKindScope::~DrawKindScope() { g_kind = m_previous; }

DrawKind currentDrawKind() { return g_kind; }

DrawTotals drawTotals(DrawKind kind) { return g_kinds.at(static_cast<std::size_t>(kind)); }

void recordDraw(std::uint64_t triangles, std::uint64_t vertices) {
    DrawTotals& totals = g_kinds.at(static_cast<std::size_t>(g_kind));
    ++totals.draws;
    totals.triangles += triangles;
    totals.vertices += vertices;
}

AllocationTotals allocationTotals() {
    return AllocationTotals{g_allocations.load(std::memory_order_relaxed),
                            g_allocatedBytes.load(std::memory_order_relaxed)};
}

void addAllocationCounters() {
    perf::addCounter("alloc.count", [] { return allocationTotals().allocations; });
    perf::addCounter("alloc.bytes", [] { return allocationTotals().bytes; });
}

void installGlCounters() {
    if (g_drawElements != nullptr || glad_glDrawElements == nullptr) {
        return;
    }
    g_drawElements = glad_glDrawElements;
    g_drawArrays = glad_glDrawArrays;
    g_bufferData = glad_glBufferData;
    g_bufferSubData = glad_glBufferSubData;
    g_useProgram = glad_glUseProgram;
    g_bindTexture = glad_glBindTexture;
    glad_glDrawElements = countDrawElements;
    glad_glDrawArrays = countDrawArrays;
    glad_glBufferData = countBufferData;
    glad_glBufferSubData = countBufferSubData;
    glad_glUseProgram = countUseProgram;
    glad_glBindTexture = countBindTexture;
    perf::addCounter("gl.draws", [] { return g_draws; });
    perf::addCounter("gl.triangles", [] { return g_triangles; });
    perf::addCounter("gl.uploads", [] { return g_uploads; });
    perf::addCounter("gl.upload_bytes", [] { return g_uploadBytes; });
    perf::addCounter("gl.programs", [] { return g_programs; });
    perf::addCounter("gl.textures", [] { return g_textureBinds; });
    // The scene counters, by kind (docs/guides/performance.md#counters).
    for (std::size_t i = 0; i < kDrawKinds; ++i) {
        const auto kind = static_cast<DrawKind>(i);
        const std::string base = std::string("scene.") + std::string(kDrawKindNames.at(i));
        perf::addCounter(base + ".triangles", [kind] { return drawTotals(kind).triangles; });
        perf::addCounter(base + ".vertices", [kind] { return drawTotals(kind).vertices; });
        perf::addCounter(base + ".draws", [kind] { return drawTotals(kind).draws; });
    }
}

std::string glRendererName() {
    if (glad_glGetString == nullptr) {
        return "none";
    }
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (renderer == nullptr || version == nullptr) {
        return "none";
    }
    return std::string(renderer) + ", OpenGL " + version;
}

void removeGlCounters() {
    if (g_drawElements == nullptr) {
        return;
    }
    glad_glDrawElements = g_drawElements;
    glad_glDrawArrays = g_drawArrays;
    glad_glBufferData = g_bufferData;
    glad_glBufferSubData = g_bufferSubData;
    glad_glUseProgram = g_useProgram;
    glad_glBindTexture = g_bindTexture;
    g_drawElements = nullptr;
}

} // namespace coney::platform

// The counting replacements of the global operator new and delete: every C++ allocation in the process (Coney's,
// librw's own use malloc and are not counted) adds to the totals. Linked in through allocationTotals(), which the
// profiler's counters call. The aligned forms are left to the standard library: Coney's code makes none in its loops.
void* operator new(std::size_t bytes) {
    if (void* memory = coney::platform::countedAllocation(bytes)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t bytes) {
    if (void* memory = coney::platform::countedAllocation(bytes)) {
        return memory;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    return coney::platform::countedAllocation(bytes);
}

void* operator new[](std::size_t bytes, const std::nothrow_t& /*tag*/) noexcept {
    return coney::platform::countedAllocation(bytes);
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t /*bytes*/) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t /*bytes*/) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t& /*tag*/) noexcept { std::free(memory); }
