// SPDX-License-Identifier: GPL-3.0-or-later
#include "gamemodes/load_entry_mode.h"

#include <array>
#include <format>
#include <map>
#include <utility>

#include "core/chunk_types.h"
#include "fileio/reader.h"

namespace coney {

namespace {

/// Loads the stream from its start as one kind of container onto fresh stacks.
std::expected<EntryLoad, Error> loadAs(io::Stream& stream, ContainerKind kind, const chunk::ChunkHandlerTable& table) {
    if (auto moved = stream.seek(0); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    EntryLoad load;
    load.kind = kind;
    auto report = kind == ContainerKind::Grouped ? chunk::loadGroupedContainer(stream, table, load.stacks)
                                                 : chunk::loadContainer(stream, table, load.stacks);
    if (!report) {
        return std::unexpected(std::move(report.error()));
    }
    load.report = std::move(*report);
    load.packageMarker = load.report.container.id == chunk::kPackageMarker;
    load.trailingBytes = stream.remaining();
    return load;
}

} // namespace

std::expected<EntryLoad, Error> loadWadEntry(const io::Wad& wad, const io::WadEntry& entry,
                                             const chunk::ChunkHandlerTable& table) {
    auto stream = wad.openEntry(entry);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    bool marker = false;
    if (stream->size() >= chunk::kHeaderSize) {
        std::array<std::byte, chunk::kHeaderSize> header{};
        if (auto read = stream->read(header); !read) {
            return std::unexpected(std::move(read.error()));
        }
        marker = io::loadU32Le(std::span<const std::byte>(header).subspan(12, 4)) == chunk::kPackageMarker;
    }
    if (marker) {
        return loadAs(*stream, ContainerKind::Grouped, table);
    }
    auto flat = loadAs(*stream, ContainerKind::Flat, table);
    if (flat) {
        return flat;
    }
    if (auto grouped = loadAs(*stream, ContainerKind::Grouped, table)) {
        return grouped;
    }
    return std::unexpected(Error{flat.error().code, "not a chunk container: " + flat.error().message});
}

std::string describeLoad(const EntryLoad& load) {
    const chunk::LoadReport& report = load.report;
    struct TypeTotal {
        std::uint64_t chunks = 0;
        std::uint64_t bytes = 0;
        std::uint64_t byHandler = 0;
    };
    std::map<std::uint32_t, TypeTotal> byType;
    std::uint64_t dataBytes = 0;
    for (const chunk::ChunkRecord& record : report.chunks) {
        TypeTotal& total = byType[record.header.type];
        ++total.chunks;
        total.bytes += record.header.size;
        total.byHandler += record.readByHandler ? 1 : 0;
        dataBytes += record.header.size;
    }

    std::string text;
    if (load.kind == ContainerKind::Grouped) {
        text += std::format("  grouped container{}: {} groups (header count {}{}), {} chunks, {} bytes of chunk data\n",
                            load.packageMarker ? " (pack)" : " (no package marker)", report.groups.size(),
                            report.container.count, report.endedByZeroGroup ? ", ended by a zero group" : "",
                            report.chunks.size(), dataBytes);
    } else {
        text += std::format("  flat container: {} chunks, {} bytes of chunk data (header says {})\n",
                            report.chunks.size(), dataBytes, report.container.payloadBytes);
    }
    for (const auto& [type, total] : byType) {
        text += std::format("    type {:#04x} {}: {} chunk{}, {} bytes{}\n", type, chunk::chunkTypeName(type),
                            total.chunks, total.chunks == 1 ? "" : "s", total.bytes,
                            total.byHandler > 0 ? std::format(" ({} read by a stream handler)", total.byHandler) : "");
    }
    text += std::format("  left on the stacks: {} chunks, {} objects; {} trailing bytes\n", load.stacks.chunks().size(),
                        load.stacks.objects().size(), load.trailingBytes);
    return text;
}

LoadEntryMode::LoadEntryMode(const io::Wad& wad, const chunk::ChunkHandlerTable& table,
                             std::vector<std::string> requests, std::function<void(std::string_view)> print)
    : m_wad(wad), m_table(table), m_requests(std::move(requests)), m_print(std::move(print)) {}

ModeResult LoadEntryMode::update(GameModeStack& /*stack*/, const FrameTime& /*frame*/) {
    if (m_next < m_requests.size()) {
        const std::string& request = m_requests[m_next++];
        auto entry = m_wad.lookup(request);
        if (!entry) {
            ++m_failures;
            m_print(std::format("{}: {}\n", request, entry.error().message));
        } else {
            const io::WadEntry& found = **entry;
            std::string text = std::format("{}: entry {}, hash {:#010x}, {} bytes\n", request, found.index,
                                           found.nameHash, found.size);
            auto load = loadWadEntry(m_wad, found, m_table);
            if (load) {
                text += describeLoad(*load);
            } else {
                ++m_failures;
                text += std::format("  {}\n", load.error().message);
            }
            m_print(text);
        }
    }
    return m_next < m_requests.size() ? ModeResult::Stay : ModeResult::Leave;
}

} // namespace coney
