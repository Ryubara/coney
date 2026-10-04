// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/texture_lookup.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <rw.h>

namespace coney::platform {

namespace {

// The registered dictionaries, newest first. librw's find callback is a plain function, so the list is global, as
// RenderWare's own dictionary list is.
std::vector<rw::TexDictionary*>& registry() {
    static std::vector<rw::TexDictionary*> dictionaries;
    return dictionaries;
}

// librw's find callback: the current dictionary when one is set, otherwise every registered one, newest first.
rw::Texture* findTexture(const char* name) {
    if (rw::TexDictionary* current = rw::TexDictionary::getCurrent(); current != nullptr) {
        return current->find(name);
    }
    for (rw::TexDictionary* dictionary : registry()) {
        if (rw::Texture* texture = dictionary->find(name); texture != nullptr) {
            return texture;
        }
    }
    return nullptr;
}

} // namespace

void installGlobalTextureLookup() {
    // librw makes a default dictionary current when it starts; the game leaves none current, so lookups go through
    // every dictionary.
    rw::TexDictionary::setCurrent(nullptr);
    rw::Texture::findCB = findTexture;
    rw::Texture::setLoadTextures(0);
    rw::Texture::setCreateDummies(0);
}

TextureLookupEntry::TextureLookupEntry(rw::TexDictionary* dictionary) : m_dictionary(dictionary) {
    registry().insert(registry().begin(), dictionary);
}

TextureLookupEntry::TextureLookupEntry(TextureLookupEntry&& other) noexcept
    : m_dictionary(std::exchange(other.m_dictionary, nullptr)) {}

TextureLookupEntry& TextureLookupEntry::operator=(TextureLookupEntry&& other) noexcept {
    if (this != &other) {
        release();
        m_dictionary = std::exchange(other.m_dictionary, nullptr);
    }
    return *this;
}

TextureLookupEntry::~TextureLookupEntry() { release(); }

void TextureLookupEntry::release() noexcept {
    if (m_dictionary == nullptr) {
        return;
    }
    std::vector<rw::TexDictionary*>& list = registry();
    if (const auto found = std::ranges::find(list, m_dictionary); found != list.end()) {
        list.erase(found);
    }
    m_dictionary = nullptr;
}

} // namespace coney::platform
