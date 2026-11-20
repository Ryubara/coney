// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <span>
#include <vector>

#include "core/error.h"
#include "fileio/stream.h"

namespace coney::io {

/// The virtual disk inside a QEMU copy-on-write image (qcow2, versions 2 and 3): the hard-disk image xemu keeps
/// (`xbox_hdd.qcow2`). It reads the header and maps each read through the two-level cluster tables; a cluster never
/// written reads as zeros. Only reading: the image is never changed, and its internal snapshots are not looked at
/// (the disk as it is now is read).
///
/// What it does not read, refused when open() sees it: a backing file, encryption, an external data file, extended L2
/// entries and the "corrupt" mark. A compressed cluster fails the read that reaches it (xemu does not write them).
///
/// Research: docs/research/save.md#xbox-containers
/// @coney-only reads xemu's disk image for the importer
class Qcow2Image final : public Stream {
  public:
    /// The four bytes every qcow2 image starts with: `QFI` and 0xfb.
    static constexpr std::uint32_t kMagic = 0x514649fb;

    /// Whether `stream` starts with kMagic. Leaves the stream's position unspecified.
    [[nodiscard]] static bool looksLikeQcow2(Stream& stream);

    /// Reads the header and the L1 table of the image in `file`, which must outlive the image and not be moved by
    /// anyone else while it is read. Fails with ErrorCode::Invalid when the magic, version or cluster size is not a
    /// qcow2 image's or it uses a feature named above, and ErrorCode::Truncated when the file ends before its tables.
    [[nodiscard]] static std::expected<Qcow2Image, Error> open(Stream& file);

    Qcow2Image(Qcow2Image&&) = default;
    Qcow2Image& operator=(Qcow2Image&&) = delete;
    ~Qcow2Image() override = default;

    // The Stream interface, over the virtual disk; its doc comments give the contracts. A read also fails with
    // ErrorCode::Invalid when it reaches a compressed cluster or a table entry outside the file.
    [[nodiscard]] std::expected<void, Error> read(std::span<std::byte> destination) override;
    [[nodiscard]] std::expected<void, Error> seek(std::uint64_t position) override;
    [[nodiscard]] std::uint64_t tell() const override { return m_position; }
    [[nodiscard]] std::uint64_t size() const override { return m_virtualSize; }

    /// Bytes in a cluster (2 to the header's cluster bits).
    [[nodiscard]] std::uint32_t clusterBytes() const { return m_clusterBytes; }

  private:
    explicit Qcow2Image(Stream& file) : m_file(&file) {}

    // Where virtual cluster `cluster` lies in the file, or 0 when it reads as zeros.
    [[nodiscard]] std::expected<std::uint64_t, Error> hostOffset(std::uint64_t cluster);

    Stream* m_file;
    std::uint32_t m_clusterBits = 0;
    std::uint32_t m_clusterBytes = 0;
    std::uint64_t m_virtualSize = 0;
    std::uint32_t m_version = 0;
    std::vector<std::uint64_t> m_l1;
    std::map<std::uint64_t, std::vector<std::uint64_t>> m_l2; // L2 tables read so far, by their file offset
    std::uint64_t m_position = 0;
};

} // namespace coney::io
