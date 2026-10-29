// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/script_patch_command.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/script_patch.h"
#include "core/script_patch_apply.h"
#include "core/sha256.h"

namespace coney::platform {

namespace {

// The usage text a usage error prints after its message.
constexpr std::string_view kUsage =
    "Usage: coney --script-patch make BASE EDITED --out PATCH (--chunk HASH | --chunk-file FILE)\n"
    "                                 [--form FORM] [--name NAME] [--entry 0xHASH]\n"
    "       coney --script-patch apply PATCH BASE --out RESULT\n"
    "\n"
    "  make    writes the patch from the base text BASE to the edited copy EDITED, holding\n"
    "          none of the base's lines; HASH is the compiled chunk's SHA-256 (sha256:...),\n"
    "          or FILE the chunk to take it from; FORM is the base text's form (lua4 1)\n"
    "  apply   writes the text the patch makes of BASE, every hash checked\n";

// The largest file read: the larger of a patch's and a text's limits, so a stray huge file is not read whole.
constexpr std::uintmax_t kMaxFileBytes = std::max(patch::kMaxPatchBytes, patch::kMaxTextBytes);

// What the command line asks for, read before any file is opened.
struct Request {
    std::string_view verb;
    std::string_view first;  // make: BASE; apply: PATCH
    std::string_view second; // make: EDITED; apply: BASE
    std::optional<std::string_view> out;
    std::optional<std::string_view> chunk;
    std::optional<std::string_view> chunkFile;
    std::optional<std::string_view> form;
    std::optional<std::string_view> name;
    std::optional<std::string_view> entry;
};

// A usage error: the message and the usage, exit code 2.
int usageError(std::FILE* err, std::string_view message) {
    std::fprintf(err, "coney: %.*s\n\n%.*s", static_cast<int>(message.size()), message.data(),
                 static_cast<int>(kUsage.size()), kUsage.data());
    return 2;
}

// A refusal or a file that could not be read or written: the message, exit code 1.
int failure(std::FILE* err, std::string_view message) {
    std::fprintf(err, "coney: %.*s\n", static_cast<int>(message.size()), message.data());
    return 1;
}

// Reads the verb, the two files and the options; a message when the command line is wrong.
std::expected<Request, std::string> parse(std::span<const std::string_view> args) {
    if (args.empty() || (args[0] != "make" && args[0] != "apply")) {
        return std::unexpected("--script-patch is followed by make or apply");
    }
    Request request{.verb = args[0]};
    const bool make = request.verb == "make";
    std::size_t positional = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string_view arg = args[i];
        if (!arg.starts_with("--")) {
            if (positional == 2) {
                return std::unexpected(std::format("{} takes two files; {} is a third", request.verb, arg));
            }
            (positional++ == 0 ? request.first : request.second) = arg;
            continue;
        }
        std::optional<std::string_view>* slot = arg == "--out" ? &request.out : nullptr;
        if (make) {
            slot = arg == "--chunk"        ? &request.chunk
                   : arg == "--chunk-file" ? &request.chunkFile
                   : arg == "--form"       ? &request.form
                   : arg == "--name"       ? &request.name
                   : arg == "--entry"      ? &request.entry
                                           : slot;
        }
        if (slot == nullptr) {
            return std::unexpected(std::format("{} is not an option of --script-patch {}", arg, request.verb));
        }
        if (slot->has_value()) {
            return std::unexpected(std::format("{} given twice", arg));
        }
        if (i + 1 == args.size() || args[i + 1].empty()) {
            return std::unexpected(std::format("{} needs a value", arg));
        }
        *slot = args[++i];
    }
    if (positional < 2) {
        return std::unexpected(make ? "make needs the base text and the edited copy"
                                    : "apply needs the patch and the base text");
    }
    if (!request.out) {
        return std::unexpected(make ? "make needs --out PATCH" : "apply needs --out RESULT");
    }
    if (make && request.chunk.has_value() == request.chunkFile.has_value()) {
        return std::unexpected("make needs one of --chunk HASH and --chunk-file FILE");
    }
    return request;
}

// The whole of a file, or a message naming it.
std::expected<std::string, std::string> readFile(std::string_view path) {
    const std::filesystem::path file{std::string(path)};
    std::error_code error;
    const auto size = std::filesystem::file_size(file, error);
    if (error) {
        return std::unexpected(std::format("cannot read {}: {}", path, error.message()));
    }
    if (size > kMaxFileBytes) {
        return std::unexpected(std::format("{} is larger than {} bytes", path, kMaxFileBytes));
    }
    std::ifstream in(file, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (!in.good() && !in.eof()) {
        return std::unexpected(std::format("cannot read {}", path));
    }
    return text;
}

// Writes `text` to a file, or says why it could not.
std::expected<void, std::string> writeFile(std::string_view path, std::string_view text) {
    std::ofstream out(std::filesystem::path{std::string(path)}, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.close();
    if (!out) {
        return std::unexpected(std::format("cannot write {}", path));
    }
    return {};
}

// `--entry 0x1c2d3e4f`: `0x` and one to eight hex digits.
std::optional<std::uint32_t> parseEntry(std::string_view text) {
    if (text.size() < 3 || text.size() > 10 || !text.starts_with("0x")) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data() + 2, text.data() + text.size(), value, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

// `--chunk sha256:...` (or the 64 hex digits alone).
std::optional<Sha256Digest> parseChunk(std::string_view text) {
    if (text.starts_with("sha256:")) {
        text.remove_prefix(7);
    }
    return parseSha256Hex(text);
}

// `make`: the patch from the base text to the edited copy.
int make(const Request& request, std::FILE* out, std::FILE* err) {
    patch::PatchHeader header;
    if (request.entry) {
        const auto entry = parseEntry(*request.entry);
        if (!entry) {
            return usageError(err, "--entry is 0x and up to eight hex digits");
        }
        header.entry = entry;
    }
    if (request.chunk) {
        const auto chunk = parseChunk(*request.chunk);
        if (!chunk) {
            return usageError(err, "--chunk is sha256: and 64 hex digits");
        }
        header.chunk = *chunk;
    }
    if (request.name) {
        header.name = std::string(*request.name);
    }
    if (request.form) {
        header.form = std::string(*request.form);
    }
    if (request.chunkFile) {
        auto chunk = readFile(*request.chunkFile);
        if (!chunk) {
            return failure(err, chunk.error());
        }
        header.chunk = patch::textHash(*chunk);
    }
    auto base = readFile(request.first);
    if (!base) {
        return failure(err, base.error());
    }
    auto edited = readFile(request.second);
    if (!edited) {
        return failure(err, edited.error());
    }
    auto section = patch::makePatch(*base, *edited, header);
    if (!section) {
        return failure(err, section.error().message);
    }
    patch::ScriptPatch file;
    file.sections.push_back(*section);
    if (auto wrote = writeFile(*request.out, patch::writeScriptPatch(file)); !wrote) {
        return failure(err, wrote.error());
    }
    const patch::PatchCounts counts = patch::countOperations(*section);
    std::fprintf(out,
                 "wrote %.*s: %zu operations (%zu dropped, %zu added, %zu copied, %zu rebuilt, %zu built from another "
                 "line); result %s\n",
                 static_cast<int>(request.out->size()), request.out->data(), section->operations.size(), counts.dropped,
                 counts.added, counts.copied, counts.rebuilt, counts.borrowed,
                 patch::hashText(section->result).c_str());
    return 0;
}

// `apply`: the text the patch makes of the base, from the section whose base it is.
int apply(const Request& request, std::FILE* out, std::FILE* err) {
    auto text = readFile(request.first);
    if (!text) {
        return failure(err, text.error());
    }
    auto file = patch::readScriptPatch(*text);
    if (!file) {
        return failure(err, std::format("{}: {}", request.first, file.error().message));
    }
    auto base = readFile(request.second);
    if (!base) {
        return failure(err, base.error());
    }
    const Sha256Digest baseHash = patch::textHash(*base);
    const patch::PatchSection* section = file->sectionForBase(baseHash);
    if (section == nullptr) {
        return failure(err, std::format("no section of the patch is for this text ({}): it was made from another "
                                        "script, or another form of it",
                                        patch::hashText(baseHash)));
    }
    auto result = patch::applyPatch(*section, *base);
    if (!result) {
        return failure(err, result.error().message);
    }
    if (auto wrote = writeFile(*request.out, *result); !wrote) {
        return failure(err, wrote.error());
    }
    std::fprintf(out, "wrote %.*s: %s\n", static_cast<int>(request.out->size()), request.out->data(),
                 patch::hashText(section->result).c_str());
    return 0;
}

} // namespace

int runScriptPatchCommand(std::span<const std::string_view> args, std::FILE* out, std::FILE* err) {
    auto request = parse(args);
    if (!request) {
        return usageError(err, request.error());
    }
    return request->verb == "make" ? make(*request, out, err) : apply(*request, out, err);
}

} // namespace coney::platform
