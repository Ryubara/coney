// SPDX-License-Identifier: GPL-3.0-or-later
#include "world_objects/radios.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include "core/name_hash.h"

namespace coney::world_objects {

namespace {

// Every radio sound is `vags/music/<name>`; a name not recovered is kept as its hash.
std::uint32_t music(std::string_view name) { return crc32(std::string("vags/music/").append(name)); }

// The announcements (`0x00512d30`): 0 is track 13 again (never played), then `djlady_01` ... `djlady_12`.
constexpr std::size_t kAnnouncements = 13;
// The DJ links of kind 2 (`0x00512df0`): `dj_rumble_01` ... `_09`, with entries 6-8 being `_08`, `_09`, `_07`.
constexpr std::array<int, 9> kRumbleLinks{1, 2, 3, 4, 5, 6, 8, 9, 7};
// The DJ links of kinds 0 and 1 hold 18 and 15 sounds whose names are not recovered.
constexpr int kKind0Links = 18;
constexpr int kKind1Links = 15;
// The track drawn: one of 12, or 18 once level 84 is complete; a next track above 11 becomes 0.
constexpr int kEarlyTracks = 12;
constexpr int kLateTracks = 18;
constexpr int kLastEarlyTrack = 11;
// The levels whose completion changes the draws.
constexpr int kLevel84 = 84;
constexpr int kLevel93 = 93;
constexpr int kLevel31 = 31;
constexpr int kLevel81 = 81;
// The retune sound and the switch click.
constexpr std::uint32_t kRetuneSound = 0x09a4be6aU;
constexpr std::uint32_t kSwitchSound = 0x5b601235U;

// The squared distance between two points.
float distanceSquared(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    const float dx = a[0] - b[0];
    const float dy = a[1] - b[1];
    const float dz = a[2] - b[2];
    return (dx * dx) + (dy * dy) + (dz * dz);
}

// Whether `level` is complete in `world`.
bool complete(const RadioWorld& world, int level) { return world.levelComplete && world.levelComplete(level); }

// A draw of `Random % n` (`0x00335498`).
int draw(GameRandom& random, int n) { return static_cast<int>(random.next() % static_cast<std::uint32_t>(n)); }

} // namespace

const std::array<std::uint32_t, 21>& Radios::tracks() {
    static const std::array<std::uint32_t, 21> kTracks{music("echoes_in_my_mind"),
                                                       music("last_of_an_ancient_breed"),
                                                       music("love_is_a_fire"),
                                                       music("nowhere_to_run"),
                                                       music("you_re_movin_too_slow"),
                                                       0x60b5c0c3U,
                                                       music("get_down_radio"),
                                                       0x73f3d710U,
                                                       0x3be47458U,
                                                       0xf33104cdU,
                                                       music("remember"),
                                                       0x46b4f271U,
                                                       music("in_the_city"),
                                                       music("baseball_furies_chase"),
                                                       music("the_fight"),
                                                       music("theme_from_the_warriors"),
                                                       music("sho_radioloop_01"),
                                                       music("alberto"),
                                                       music("punkemitter"),
                                                       music("spanish_killer_loop"),
                                                       music("tna_funk")};
    return kTracks;
}

std::uint32_t Radios::clip(int kind, int index) {
    constexpr int kAnnouncementKind = 3;
    constexpr int kRumbleKind = 2;
    constexpr int kTrack13 = 13;
    if (kind == kAnnouncementKind && index >= 0 && index < static_cast<int>(kAnnouncements)) {
        if (index == 0) {
            return tracks().at(kTrack13);
        }
        std::string name = "djlady_";
        name += index < 10 ? "0" : "";
        name += std::to_string(index);
        return music(name);
    }
    if (kind == kRumbleKind && index >= 0 && index < static_cast<int>(kRumbleLinks.size())) {
        return music("dj_rumble_0" + std::to_string(kRumbleLinks.at(static_cast<std::size_t>(index))));
    }
    return 0; // kinds 0 and 1: not named
}

void Radios::setup(double object, std::string onPickUp, int track, std::string onSegment, int djLine) {
    auto found = std::ranges::find(m_radios, object, &Radio::object);
    Radio& radio = found != m_radios.end() ? *found : m_radios.emplace_back();
    radio = Radio{.object = object, .onPickUp = std::move(onPickUp), .onSegment = std::move(onSegment)};
    radio.track = track;
    if (djLine != 0) {
        radio.next = djLine;
        radio.announcementArmed = true;
        radio.state = kStart;
    } else {
        radio.state = track > 0 ? kStart : kSwitchOff;
    }
}

void Radios::setMode(double object, int mode, RadioSound& sound) {
    const auto found = std::ranges::find(m_radios, object, &Radio::object);
    if (found == m_radios.end()) {
        return;
    }
    Radio& radio = *found;
    int state = radio.state;
    if (mode == 1) {
        if (state == kStart || state == kTrack || state == kPause || state == kPaused) {
            state = kRetune;
        }
    } else if (mode == 0 || mode == 2) {
        state = kSwitchOff;
    } else if (mode == 3) {
        state = kPause;
    }
    if (state != radio.state) {
        sound.stop(radio.sound);
        radio.sound = 0;
        radio.state = state;
    }
}

void Radios::update(const std::function<std::optional<std::array<float, 3>>(double object)>& locate,
                    const RadioWorld& world, RadioSound& sound, GameRandom& random,
                    const std::function<void(const std::string& function)>& call) {
    for (Radio& radio : m_radios) {
        if (const std::optional<std::array<float, 3>> at = locate ? locate(radio.object) : std::nullopt) {
            updateOne(radio, *at, world, sound, random, call);
        }
    }
}

void Radios::updateOne(Radio& radio, const std::array<float, 3>& at, const RadioWorld& world, RadioSound& sound,
                       GameRandom& random, const std::function<void(const std::string& function)>& call) {
    const bool hears = world.player && distanceSquared(*world.player, at) <= kHearing * kHearing;
    const int trackDraw = complete(world, kLevel84) ? kLateTracks : kEarlyTracks;
    const auto segment = [&radio, &call] {
        if (!radio.onSegment.empty() && call) {
            call(radio.onSegment);
        }
    };
    const auto stopSound = [&radio, &sound] {
        if (radio.sound != 0) {
            sound.stop(radio.sound);
            radio.sound = 0;
        }
    };
    // A playing sound follows the radio, at half volume during a scene; beyond hearing it stops.
    const auto follow = [&] {
        if (radio.sound == 0) {
            return;
        }
        if (!hears) {
            stopSound();
            radio.state = kStart; // **Coney's stand-in**: it starts again when the player comes back
            return;
        }
        sound.follow(radio.sound, at, world.scene ? kSceneVolume : 1.0F);
    };
    // An armed announcement starts when the player walks up.
    if (radio.announcementArmed && world.player && distanceSquared(*world.player, at) <= kAnnounce * kAnnounce) {
        radio.announcementArmed = false;
        stopSound();
        radio.clipKind = 3;
        radio.track = radio.next;
        radio.next = draw(random, trackDraw);
        radio.state = kClip;
    }
    switch (radio.state) {
    case kStart:
        if (hears && radio.track >= 0 && radio.track < static_cast<int>(tracks().size())) {
            radio.sound = sound.play(tracks().at(static_cast<std::size_t>(radio.track)), at);
            radio.state = kTrack;
        } else {
            stopSound();
        }
        break;
    case kTrack:
        follow();
        if (radio.state == kTrack && (radio.sound == 0 || !sound.playing(radio.sound)) && hears) {
            radio.sound = 0;
            if (radio.announcementArmed) {
                radio.announcementArmed = false;
                radio.clipKind = 3;
                radio.track = radio.next;
            } else {
                // A DJ link: 0-6 kind 1, 7-8 kind 0 (its pool shrinking with the story), 9 kind 2.
                const std::int32_t pick = random.range(0, 9);
                if (pick <= 6) {
                    radio.clipKind = 1;
                    radio.track = draw(random, kKind1Links);
                } else if (pick <= 8) {
                    int pool = kKind0Links;
                    if (complete(world, kLevel84)) {
                        pool = 14;
                    } else if (complete(world, kLevel31)) {
                        pool = 15;
                    } else if (complete(world, kLevel93)) {
                        pool = 16;
                    } else if (complete(world, kLevel81)) {
                        pool = 17;
                    }
                    radio.clipKind = 0;
                    radio.track = draw(random, pool);
                } else {
                    radio.clipKind = 2;
                    radio.track = draw(random, complete(world, kLevel93) ? 8 : 9);
                }
            }
            radio.next = draw(random, trackDraw);
            radio.state = kClip;
        }
        break;
    case kClip:
        if (hears) {
            radio.sound = sound.play(clip(radio.clipKind, radio.track), at);
        }
        segment();
        radio.state = kClipPlaying;
        break;
    case kClipPlaying:
        follow();
        if (radio.state == kClipPlaying && (radio.sound == 0 || !sound.playing(radio.sound) || world.scene)) {
            stopSound();
            radio.state = kNextTrack;
        }
        break;
    case kNextTrack:
        radio.track = radio.next > kLastEarlyTrack ? 0 : radio.next;
        radio.state = kStart;
        segment();
        break;
    case kRetune:
        stopSound();
        radio.sound = sound.play(kRetuneSound, at);
        segment();
        radio.state = kRetuning;
        break;
    case kRetuning:
        if (radio.sound == 0 || !sound.playing(radio.sound)) {
            radio.sound = 0;
            radio.state = kRetuned;
        }
        break;
    case kRetuned:
        radio.track = draw(random, trackDraw);
        radio.state = kStart;
        segment();
        break;
    case kSwitchOff:
        radio.next = radio.track;
        radio.track = -1;
        stopSound();
        radio.sound = sound.play(kSwitchSound, at);
        radio.state = kOff;
        break;
    case kOff:
        stopSound();
        break;
    case kPause:
    case kPaused:
        if (radio.state == kPause) {
            stopSound();
            radio.sound = sound.play(kSwitchSound, at);
            radio.state = kPaused;
        } else if (radio.sound == 0 || !sound.playing(radio.sound)) {
            radio.sound = 0;
            radio.track = radio.next > kLastEarlyTrack ? 0 : radio.next;
            radio.state = kStart;
        }
        break;
    default:
        break;
    }
}

const Radio* Radios::find(double object) const {
    const auto found = std::ranges::find(m_radios, object, &Radio::object);
    return found == m_radios.end() ? nullptr : &*found;
}

} // namespace coney::world_objects
