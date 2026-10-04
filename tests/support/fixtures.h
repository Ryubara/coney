// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Synthetic test data, built byte by byte in the tests. Nothing here comes from the game (LEGAL.md, "No game data").

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace coney::test {

/// Builds a little-endian byte buffer. Every method appends (or patches) and returns the builder, so calls chain.
class Bytes {
  public:
    Bytes& u8(std::uint8_t value) {
        m_data.push_back(static_cast<std::byte>(value));
        return *this;
    }
    Bytes& u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value & 0xFFU));
        return u8(static_cast<std::uint8_t>(value >> 8));
    }
    Bytes& u32(std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
        }
        return *this;
    }
    /// Four 32-bit words: one 16-byte header of the chunk system or of WARRIORS.DIR.
    Bytes& header(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
        return u32(a).u32(b).u32(c).u32(d);
    }
    /// Appends `count` copies of `value`.
    Bytes& fill(std::size_t count, std::uint8_t value) {
        m_data.insert(m_data.end(), count, static_cast<std::byte>(value));
        return *this;
    }
    /// Appends the characters as bytes, with no terminator.
    Bytes& text(std::string_view chars) {
        for (const char c : chars) {
            u8(static_cast<std::uint8_t>(c));
        }
        return *this;
    }
    Bytes& append(std::span<const std::byte> bytes) {
        m_data.insert(m_data.end(), bytes.begin(), bytes.end());
        return *this;
    }
    /// Pads with zeros up to `size` bytes in total.
    Bytes& padTo(std::size_t size) {
        if (m_data.size() < size) {
            fill(size - m_data.size(), 0);
        }
        return *this;
    }
    /// Overwrites the 32-bit value at `offset`.
    Bytes& patchU32(std::size_t offset, std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            m_data.at(offset + static_cast<std::size_t>(i)) = static_cast<std::byte>((value >> (8 * i)) & 0xFFU);
        }
        return *this;
    }

    [[nodiscard]] std::size_t size() const { return m_data.size(); }
    [[nodiscard]] const std::vector<std::byte>& data() const { return m_data; }
    [[nodiscard]] std::span<const std::byte> span() const { return m_data; }

  private:
    std::vector<std::byte> m_data;
};

/// A fresh, empty folder under the system's temporary directory, removed with everything in it at the end of the
/// test.
class TempDir {
  public:
    // A counter plus a random number, retried until unused, so parallel test processes never share a folder.
    TempDir() {
        static int counter = 0;
        const auto base = std::filesystem::temp_directory_path();
        do {
            m_path = base / ("coney-test-" + std::to_string(++counter) + "-" + std::to_string(std::rand()));
        } while (std::filesystem::exists(m_path));
        std::filesystem::create_directories(m_path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(m_path, ec);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return m_path; }

    /// Writes `bytes` to `name` inside the folder and returns the file's path.
    std::filesystem::path write(std::string_view name, std::span<const std::byte> bytes) const {
        const auto file = m_path / name;
        std::ofstream out(file, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
        return file;
    }

  private:
    std::filesystem::path m_path;
};

/// Reverses the byte order of a 32-bit value (ISO 9660 stores numbers in both orders).
inline std::uint32_t byteSwap(std::uint32_t v) {
    return (v >> 24) | ((v >> 8) & 0xFF00U) | ((v << 8) & 0xFF0000U) | (v << 24);
}

/// One file of a synthetic ISO image.
struct IsoFixtureFile {
    std::string name; ///< As stored in the directory, such as "WARRIORS.DIR;1".
    std::vector<std::byte> data;
};

/// Builds a minimal ISO 9660 image: the primary volume descriptor at sector 16, a root directory at sector 18 holding
/// "." and ".." records, one subdirectory record and `files`, and each file's data from sector 20 on.
inline std::vector<std::byte> buildIso(std::initializer_list<IsoFixtureFile> files) {
    constexpr std::uint32_t kSector = 2048;
    constexpr std::uint32_t kRootSector = 18;
    constexpr std::uint32_t kFirstFileSector = 20;

    // Appends one ECMA-119 directory record, laid out at the offsets readIsoRoot() reads.
    auto record = [](Bytes& dir, std::uint32_t extent, std::uint32_t size, std::uint8_t flags, std::string_view name) {
        const auto nameLength = static_cast<std::uint8_t>(name.size());
        auto length = static_cast<std::uint8_t>(33 + nameLength);
        if (length % 2 != 0) {
            ++length; // records have even length
        }
        const std::size_t start = dir.size();
        dir.u8(length).u8(0);
        dir.u32(extent).u32(byteSwap(extent));
        dir.u32(size).u32(byteSwap(size));
        dir.fill(7, 0); // recording date
        dir.u8(flags).u8(0).u8(0);
        dir.u16(1).u16(0x0100); // volume sequence number, both-endian
        dir.u8(nameLength).text(name);
        dir.padTo(start + length);
    };

    // The root directory: ".", "..", a subdirectory the reader must skip, then one record per file.
    Bytes dir;
    record(dir, kRootSector, kSector, 2, std::string_view("\0", 1));
    record(dir, kRootSector, kSector, 2, "\x01");
    record(dir, 19, kSector, 2, "SUBDIR");
    std::uint32_t sector = kFirstFileSector;
    for (const IsoFixtureFile& file : files) {
        record(dir, sector, static_cast<std::uint32_t>(file.data.size()), 0, file.name);
        sector += static_cast<std::uint32_t>((file.data.size() + kSector - 1) / kSector);
    }

    // The image: 16 empty system-area sectors, the descriptor, the root directory, then the files' data.
    Bytes image;
    image.padTo(std::size_t{16} * kSector);
    const std::size_t pvd = image.size();
    image.u8(1).text("CD001").u8(1);
    image.padTo(pvd + 128);
    image.u16(kSector).u16(0x0008); // logical block size, both-endian
    image.padTo(pvd + 156);
    // The root directory record inside the descriptor.
    Bytes root;
    record(root, kRootSector, kSector, 2, std::string_view("\0", 1));
    image.append(root.span());
    image.padTo(std::size_t{kRootSector} * kSector);
    image.append(dir.span());
    image.padTo(std::size_t{kFirstFileSector} * kSector);
    for (const IsoFixtureFile& file : files) {
        image.append(file.data);
        image.padTo((image.size() + kSector - 1) / kSector * kSector);
    }
    return image.data();
}

} // namespace coney::test
