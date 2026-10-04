// SPDX-License-Identifier: GPL-3.0-or-later
#include "fileio/disc.h"

#include <array>
#include <format>
#include <span>
#include <system_error>
#include <utility>

#include "fileio/reader.h"

namespace coney::io {

namespace {

// ISO 9660 layout (ECMA-119): the primary volume descriptor is sector 16; within it the logical block size is the
// little-endian half of a both-endian u16 at 128, and the root directory record is the 34 bytes at 156. A directory
// record holds its length at +0, the extent (both-endian u32, little half first) at +2, the data size at +10, the
// flags at +25 and the name length and name at +32 and +33.
constexpr std::uint64_t kPvdSector = 16;
constexpr std::size_t kBlockSizeOffset = 128;
constexpr std::size_t kRootRecordOffset = 156;
constexpr std::size_t kMinRecordLength = 34;
constexpr std::uint8_t kDirectoryFlag = 2;

// The little-endian 16-bit value at `bytes[at]`; callers have already checked the bounds.
std::uint16_t loadU16Le(std::span<const std::byte> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(bytes[at]) | (static_cast<unsigned>(bytes[at + 1]) << 8));
}

} // namespace

std::string cleanDiscName(std::string_view name) {
    std::string clean(name.substr(0, name.find(';')));
    while (!clean.empty() && clean.back() == '.') {
        clean.pop_back();
    }
    for (char& c : clean) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return clean;
}

std::expected<std::vector<IsoFile>, Error> readIsoRoot(Stream& image) {
    // Read the primary volume descriptor and check it is one, with the only block size we support.
    std::array<std::byte, kIsoSectorSize> pvd{};
    if (auto moved = image.seek(kPvdSector * kIsoSectorSize); !moved) {
        return fail(ErrorCode::Invalid, "not an ISO 9660 image (too small to hold a volume descriptor)");
    }
    if (auto done = image.read(pvd); !done) {
        return fail(ErrorCode::Invalid, "not an ISO 9660 image (too small to hold a volume descriptor)");
    }
    constexpr std::array<char, 5> kMagic{'C', 'D', '0', '0', '1'};
    bool magicMatches = pvd[0] == std::byte{1};
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        magicMatches = magicMatches && static_cast<char>(pvd[1 + i]) == kMagic[i];
    }
    if (!magicMatches) {
        return fail(ErrorCode::Invalid, "not an ISO 9660 image (no primary volume descriptor at sector 16)");
    }
    if (loadU16Le(pvd, kBlockSizeOffset) != kIsoSectorSize) {
        return fail(ErrorCode::Invalid, "unsupported ISO logical block size (only 2048-byte sectors)");
    }
    // Find the root directory's extent and read all of it.
    const auto root = std::span<const std::byte>(pvd).subspan(kRootRecordOffset, kMinRecordLength);
    const std::uint32_t rootExtent = loadU32Le(root.subspan(2, 4));
    const std::uint32_t rootSize = loadU32Le(root.subspan(10, 4));

    const std::uint64_t rootStart = std::uint64_t{rootExtent} * kIsoSectorSize;
    if (rootStart > image.size() || rootSize > image.size() - rootStart) {
        return fail(ErrorCode::Truncated, "the root directory is cut off (image truncated?)");
    }
    std::vector<std::byte> directory(rootSize);
    if (auto moved = image.seek(rootStart); !moved) {
        return std::unexpected(std::move(moved.error()));
    }
    if (auto done = image.read(directory); !done) {
        return std::unexpected(std::move(done.error()));
    }

    // Walk its records, keeping the files.
    std::vector<IsoFile> files;
    std::size_t pos = 0;
    while (pos < directory.size()) {
        const auto length = static_cast<std::size_t>(directory[pos]);
        if (length == 0) {
            // Records never span sectors; zero padding means "continue in the next sector".
            pos = (pos / kIsoSectorSize + 1) * kIsoSectorSize;
            continue;
        }
        if (length < kMinRecordLength || length > directory.size() - pos) {
            return fail(ErrorCode::Invalid,
                        std::format("damaged directory record at byte {} of the root directory", pos));
        }
        const auto record = std::span<const std::byte>(directory).subspan(pos, length);
        pos += length;
        const auto nameLength = static_cast<std::size_t>(record[32]);
        if (33 + nameLength > length) {
            return fail(ErrorCode::Invalid,
                        std::format("damaged directory record at byte {} of the root directory", pos - length));
        }
        const auto flags = static_cast<std::uint8_t>(record[25]);
        std::string name;
        for (const std::byte b : record.subspan(33, nameLength)) {
            name.push_back(static_cast<char>(b));
        }
        // Skip directories, and the "." and ".." records, whose one-byte names are 0 and 1.
        if ((flags & kDirectoryFlag) != 0 || name == std::string_view("\0", 1) || name == "\x01") {
            continue;
        }
        files.push_back(
            IsoFile{cleanDiscName(name), loadU32Le(record.subspan(2, 4)), loadU32Le(record.subspan(10, 4))});
    }
    return files;
}

std::expected<Disc, Error> Disc::open(const std::filesystem::path& path) {
    Disc disc;
    disc.m_path = path;
    std::error_code ec;
    // A folder: every regular file directly inside it is a root file.
    if (std::filesystem::is_directory(path, ec)) {
        // increment(ec) rather than a range-for: the iterator's operator++ reports errors by throwing.
        for (std::filesystem::directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
            std::error_code fileEc;
            if (!it->is_regular_file(fileEc)) {
                continue;
            }
            const std::uintmax_t size = it->file_size(fileEc);
            if (fileEc) {
                continue;
            }
            const std::u8string leaf = it->path().filename().u8string();
            disc.m_files.emplace(cleanDiscName(std::string(leaf.begin(), leaf.end())), Location{it->path(), 0, size});
        }
        if (ec) {
            return fail(ErrorCode::Io, std::format("{}: cannot list the folder ({})", path.string(), ec.message()));
        }
        return disc;
    }
    // Otherwise it must be an ISO image: list its root directory and map each file to its byte range in the image.
    if (!std::filesystem::is_regular_file(path, ec)) {
        return fail(ErrorCode::NotFound,
                    std::format("{}: not a folder or an ISO image (does it exist?)", path.string()));
    }
    auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        return fail(ErrorCode::Io, std::format("{}: cannot be read ({})", path.string(), ec.message()));
    }
    auto image = FileStream::open(path, 0, size);
    if (!image) {
        return std::unexpected(std::move(image.error()));
    }
    auto files = readIsoRoot(*image);
    if (!files) {
        return fail(files.error().code, std::format("{}: {}", path.string(), files.error().message));
    }
    disc.m_isImage = true;
    for (const IsoFile& file : *files) {
        disc.m_files.emplace(file.name,
                             Location{path, std::uint64_t{file.extent} * kIsoSectorSize, std::uint64_t{file.size}});
    }
    return disc;
}

const Disc::Location* Disc::find(std::string_view name) const {
    auto it = m_files.find(cleanDiscName(name));
    return it == m_files.end() ? nullptr : &it->second;
}

bool Disc::has(std::string_view name) const { return find(name) != nullptr; }

std::expected<std::uint64_t, Error> Disc::fileSize(std::string_view name) const {
    const Location* location = find(name);
    if (location == nullptr) {
        return fail(ErrorCode::NotFound, std::format("{}: no {} in the disc root", m_path.string(), name));
    }
    return location->size;
}

std::expected<FileStream, Error> Disc::openFile(std::string_view name) const {
    const Location* location = find(name);
    if (location == nullptr) {
        return fail(ErrorCode::NotFound, std::format("{}: no {} in the disc root", m_path.string(), name));
    }
    return FileStream::open(location->hostFile, location->offset, location->size);
}

std::expected<FileStream, Error> Disc::openFileRange(std::string_view name, std::uint64_t offset,
                                                     std::uint64_t length) const {
    const Location* location = find(name);
    if (location == nullptr) {
        return fail(ErrorCode::NotFound, std::format("{}: no {} in the disc root", m_path.string(), name));
    }
    if (offset > location->size || length > location->size - offset) {
        return fail(ErrorCode::Truncated, std::format("{}: bytes {} to {} are past the end of {} ({} bytes)",
                                                      m_path.string(), offset, offset + length, name, location->size));
    }
    return FileStream::open(location->hostFile, location->offset + offset, length);
}

std::expected<std::vector<std::byte>, Error> Disc::readFile(std::string_view name) const {
    auto stream = openFile(name);
    if (!stream) {
        return std::unexpected(std::move(stream.error()));
    }
    std::vector<std::byte> data(static_cast<std::size_t>(stream->size()));
    if (auto done = stream->read(data); !done) {
        return std::unexpected(std::move(done.error()));
    }
    return data;
}

} // namespace coney::io
