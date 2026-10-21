// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An install folder in memory and a synthetic "known disc", for the installer's tests. Nothing here comes from the game
// (LEGAL.md, "No game data").

#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/error.h"
#include "core/sha1.h"
#include "fileio/stream.h"
#include "install/install_folder.h"
#include "install/known_discs.h"

namespace coney::test {

/// An install folder held in memory, with the failures a real drive can have: too little space, or writes that fail
/// once a number of bytes has been written (a disk that fills up).
class MemoryInstallFolder final : public install::InstallFolder {
  public:
    std::map<std::string, std::vector<std::byte>, std::less<>> files;
    std::set<std::string, std::less<>> folders;
    std::optional<std::uint64_t> free;       ///< freeBytes(); unknown by default.
    std::optional<std::uint64_t> writeLimit; ///< Writes fail once this many bytes have been written in all.
    std::uint64_t written = 0;

    std::expected<void, Error> makeFolders(std::string_view relative) override {
        std::string path(relative);
        while (!path.empty()) {
            folders.insert(path);
            const auto slash = path.rfind('/');
            path = slash == std::string::npos ? std::string{} : path.substr(0, slash);
        }
        return {};
    }
    std::optional<std::uint64_t> fileSize(std::string_view relative) const override {
        const auto it = files.find(relative);
        return it == files.end() ? std::nullopt : std::optional<std::uint64_t>(it->second.size());
    }
    std::expected<std::unique_ptr<install::OutputFile>, Error> create(std::string_view relative) override {
        const auto slash = relative.rfind('/');
        if (slash != std::string_view::npos && !folders.contains(relative.substr(0, slash))) {
            return fail(ErrorCode::Io, std::format("{}: no such folder", relative));
        }
        files[std::string(relative)].clear();
        return std::make_unique<Output>(*this, std::string(relative));
    }
    std::expected<std::unique_ptr<io::Stream>, Error> open(std::string_view relative) const override {
        const auto it = files.find(relative);
        if (it == files.end()) {
            return fail(ErrorCode::NotFound, std::format("{}: no such file", relative));
        }
        m_reads.push_back(it->second); // the stream reads a copy that outlives later writes
        return std::make_unique<io::MemoryStream>(m_reads.back());
    }
    std::expected<void, Error> rename(std::string_view from, std::string_view to) override {
        auto it = files.find(from);
        if (it == files.end()) {
            return fail(ErrorCode::NotFound, std::format("{}: no such file", from));
        }
        auto data = std::move(it->second);
        files.erase(it);
        files[std::string(to)] = std::move(data);
        return {};
    }
    std::expected<void, Error> remove(std::string_view relative) override {
        if (auto it = files.find(relative); it != files.end()) {
            files.erase(it);
        }
        return {};
    }
    std::optional<std::uint64_t> freeBytes() const override { return free; }

    /// The names of the files whose name ends with `suffix`.
    [[nodiscard]] std::vector<std::string> endingWith(std::string_view suffix) const {
        std::vector<std::string> names;
        for (const auto& [name, data] : files) {
            if (name.ends_with(suffix)) {
                names.push_back(name);
            }
        }
        return names;
    }

  private:
    // A file being written: appends to the folder's map, failing past the write limit.
    class Output final : public install::OutputFile {
      public:
        Output(MemoryInstallFolder& owner, std::string name) : m_owner(owner), m_name(std::move(name)) {}
        std::expected<void, Error> write(std::span<const std::byte> bytes) override {
            if (m_owner.writeLimit && m_owner.written + bytes.size() > *m_owner.writeLimit) {
                return fail(ErrorCode::Io, std::format("{}: the drive is full", m_name));
            }
            m_owner.written += bytes.size();
            auto& data = m_owner.files[m_name];
            data.insert(data.end(), bytes.begin(), bytes.end());
            return {};
        }
        std::expected<void, Error> close() override { return {}; }

      private:
        MemoryInstallFolder& m_owner;
        std::string m_name;
    };

    mutable std::deque<std::vector<std::byte>> m_reads;
};

/// A synthetic release to install: files with made-up contents, and the known-disc entry that lists them with their
/// sizes and SHA-1s, as known_discs.cpp lists the real disc.
class FixtureRelease {
  public:
    /// `files` are (name, contents); the first is the key file.
    explicit FixtureRelease(std::vector<std::pair<std::string, std::vector<std::byte>>> files)
        : m_contents(std::move(files)) {
        for (const auto& [name, data] : m_contents) {
            Sha1 sha;
            sha.update(std::span<const std::byte>(data));
            m_sha1s.push_back(toHex(sha.finish()));
        }
        for (std::size_t i = 0; i < m_contents.size(); ++i) {
            m_files.push_back({m_contents[i].first, m_contents[i].second.size(), m_sha1s[i]});
        }
        m_discs.push_back({"ps2-test", "A test disc", install::Platform::Ps2, m_contents.front().first, m_files});
    }
    FixtureRelease(const FixtureRelease&) = delete;
    FixtureRelease& operator=(const FixtureRelease&) = delete;
    FixtureRelease(FixtureRelease&&) = delete;
    FixtureRelease& operator=(FixtureRelease&&) = delete;
    ~FixtureRelease() = default;

    /// The known-disc list holding this release only.
    [[nodiscard]] std::span<const install::KnownDisc> known() const { return m_discs; }
    /// The files and their contents, as given.
    [[nodiscard]] const std::vector<std::pair<std::string, std::vector<std::byte>>>& contents() const {
        return m_contents;
    }
    /// Lets a test make the list wrong (a SHA-1 that no file has).
    void setSha1(std::size_t file, std::string sha1) {
        m_sha1s[file] = std::move(sha1);
        m_files[file].sha1 = m_sha1s[file];
    }

  private:
    std::vector<std::pair<std::string, std::vector<std::byte>>> m_contents;
    std::deque<std::string> m_sha1s; // a deque, so the views into it stay valid
    std::vector<install::KnownFile> m_files;
    std::vector<install::KnownDisc> m_discs;
};

} // namespace coney::test
