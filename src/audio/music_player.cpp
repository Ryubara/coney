// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/music_player.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "audio/sound_data.h"

namespace coney::audio {

namespace {

// The channel that holds the queued request.
constexpr std::size_t kRequest = 2;
// How far ahead of its voice a music stream is decoded: a second of frames, so a slow step never starves it.
constexpr double kAheadSeconds = 1.0;
// Fade lengths of the system music by mood change, in bars (docs/research/sound.md#music).
constexpr int kFadeIntoFight = 0;
constexpr int kFadeIntoIdle = 4;
constexpr int kFadeOther = 2;

} // namespace

MusicPlayer::MusicPlayer(Mixer& mixer, SoundTables& tables) : m_mixer(mixer), m_tables(tables) {}

MusicPlayer::~MusicPlayer() {
    for (Channel& channel : m_channels) {
        halt(channel);
    }
}

void MusicPlayer::configure(std::uint32_t hash, float barMs, float volume) {
    if (MusicRecord* record = m_tables.findMusic(hash)) {
        record->volume = volume;
    }
    // The bar length lives in the player's track list; Coney keeps it on the channel when the track starts.
    m_barMs[hash] = barMs;
}

void MusicPlayer::play(std::uint32_t hash, bool loop, std::string callback, int fadeBars) {
    if (!m_allowed || m_tables.findMusic(hash) == nullptr) {
        return;
    }
    // A newer request replaces one still waiting.
    Channel& request = m_channels.at(kRequest);
    halt(request);
    request.state = MusicState::Queued;
    request.hash = hash;
    request.loop = loop;
    request.fadeBars = std::max(fadeBars, 0);
    request.callback = std::move(callback);
}

void MusicPlayer::stop() {
    // A channel starting or playing fades out over one bar (Coney's length: the page does not give it); one not yet
    // started stops at once.
    for (Channel& channel : m_channels) {
        switch (channel.state) {
        case MusicState::Playing:
        case MusicState::FadeIn:
            fadeOut(channel, m_now, channel.barMs);
            break;
        case MusicState::FadeOut:
            break;
        default:
            halt(channel);
            break;
        }
    }
}

void MusicPlayer::setSceneDuck(bool enabled, float factor) {
    m_duckEnabled = enabled;
    m_duck = factor;
}

void MusicPlayer::setPaused(bool paused) {
    m_paused = paused;
    for (Channel& channel : m_channels) {
        m_mixer.setPaused(channel.voice, paused);
    }
}

void MusicPlayer::setSystemTracks(int mood, std::array<std::uint32_t, kTracksPerMood> tracks) {
    if (mood >= 0 && mood < kMoods) {
        m_moodTracks.at(static_cast<std::size_t>(mood)) = tracks;
    }
}

std::uint32_t MusicPlayer::currentTrack() const {
    for (const Channel& channel : m_channels) {
        if (channel.state == MusicState::Playing || channel.state == MusicState::FadeIn) {
            return channel.hash;
        }
    }
    return 0;
}

// Whether a channel holds a track (anything but idle).
bool MusicPlayer::active(const Channel& channel) const { return channel.state != MusicState::Idle; }

// Stops a channel's voice and frees its stream pair.
void MusicPlayer::halt(Channel& channel) {
    m_mixer.stop(channel.voice);
    channel = Channel{};
}

// Opens the track on a stream pair and starts decoding it, the voice not yet started.
void MusicPlayer::startPreload(Channel& channel, SoundFiles* files) {
    const MusicRecord* record = m_tables.findMusic(channel.hash);
    if (record == nullptr || files == nullptr || record->channels != 2) {
        halt(channel);
        return;
    }
    const StreamLayout layout{.file = SoundFile::Music,
                              .offset = record->offset,
                              .channels = 2,
                              .interleave = record->interleave,
                              .blocks = record->blocks,
                              .lastBlock = record->lastBlock,
                              .loops = channel.loop};
    const auto rate = static_cast<int>(record->sampleRate);
    auto feeder = StreamFeeder::create(layout, rate, static_cast<std::uint32_t>(rate * kAheadSeconds));
    if (!feeder) {
        halt(channel);
        return;
    }
    channel.feeder = std::move(*feeder);
    channel.trackVolume = record->volume;
    const auto bar = m_barMs.find(channel.hash);
    channel.barMs = bar != m_barMs.end() ? bar->second : kDefaultBarMs;
    if (auto fed = channel.feeder->pump(*files); !fed) {
        ++m_readErrors;
    }
    channel.state = MusicState::PreLoading;
}

// Starts a pre-loaded channel's voice now, fading it in over its fade bars or at full volume.
void MusicPlayer::start(Channel& channel, double now, bool fadeIn) {
    const MusicRecord* record = m_tables.findMusic(channel.hash);
    channel.voice = m_mixer.play(channel.feeder->stream(),
                                 VoiceParams{.bus = Bus::Music, .volume = 0.0F, .priority = 255, .paused = m_paused});
    if (record != nullptr) {
        m_mixer.setRate(channel.voice, static_cast<float>(record->sampleRate));
    }
    channel.playStart = now;
    channel.sent = -1.0F;
    if (fadeIn && channel.fadeBars > 0) {
        channel.state = MusicState::FadeIn;
        channel.fade = 0.0F;
        channel.fadeStart = now;
        channel.fadeLength = static_cast<double>(channel.barMs) * channel.fadeBars;
    } else {
        channel.state = MusicState::Playing;
        channel.fade = 1.0F;
    }
}

// Fades a channel out over `lengthMs` from its current level; 0 stops it at once.
void MusicPlayer::fadeOut(Channel& channel, double now, float lengthMs) {
    if (lengthMs <= 0.0F) {
        halt(channel);
        return;
    }
    channel.state = MusicState::FadeOut;
    channel.fadeStart = now;
    channel.fadeLength = lengthMs;
}

// The system music (docs/research/sound.md#music): on a mood change, or when forced, a random one of the mood's
// tracks plays looping, with a fade that depends on the change; with no track the music stops.
// @orig 0x0010e7d0 SystemMusic_Update (unknown)
void MusicPlayer::updateSystemMusic(const std::function<std::int32_t(std::int32_t, std::int32_t)>& random) {
    if (!m_systemMusic || (m_mood == m_playingMood && !m_forceMood)) {
        return;
    }
    m_forceMood = false;
    const int from = m_playingMood;
    m_playingMood = m_mood;
    if (m_onMood) {
        m_onMood(m_mood);
    }
    if (m_mood < 0 || m_mood >= kMoods) {
        stop();
        return;
    }
    const auto& tracks = m_moodTracks.at(static_cast<std::size_t>(m_mood));
    const auto count = static_cast<std::int32_t>(std::ranges::count_if(tracks, [](std::uint32_t t) { return t != 0; }));
    if (count == 0) {
        stop();
        return;
    }
    const std::int32_t pick = random ? std::clamp(random(0, count - 1), 0, count - 1) : 0;
    int fadeBars = kFadeOther;
    if (m_mood == 1) {
        fadeBars = kFadeIntoFight;
    } else if (m_mood == 0 && (from == 1 || from == 2)) {
        fadeBars = kFadeIntoIdle;
    }
    play(tracks.at(static_cast<std::size_t>(pick)), true, {}, fadeBars);
}

void MusicPlayer::update(double now, SoundFiles* files,
                         const std::function<std::int32_t(std::int32_t, std::int32_t)>& random) {
    m_now = now;
    updateSystemMusic(random);

    // 1. A queued request takes the stream pair the old track is not using (an idle channel no stereo sound holds,
    // else the one fading out, else an idle one taken over from a prepared scene soundtrack, which then waits for a
    // pair again; docs/research/sound.md#stream-pairs) and starts pre-loading. With none it stays queued.
    Channel& request = m_channels.at(kRequest);
    if (request.state == MusicState::Queued) {
        const auto idleSlot = [this](bool takeOver) {
            for (std::size_t i = 0; i < 2; ++i) {
                if (m_channels.at(i).state == MusicState::Idle && (!m_pairGate || m_pairGate(i, takeOver))) {
                    return m_channels.begin() + static_cast<std::ptrdiff_t>(i);
                }
            }
            return m_channels.begin() + 2;
        };
        auto slot = idleSlot(false);
        if (slot == m_channels.begin() + 2) {
            slot = std::ranges::find_if(m_channels.begin(), m_channels.begin() + 2,
                                        [](const Channel& c) { return c.state == MusicState::FadeOut; });
        }
        if (slot == m_channels.begin() + 2) {
            slot = idleSlot(true);
        }
        if (slot != m_channels.begin() + 2) {
            const int pair = slot == m_channels.begin() ? 1 : 3;
            halt(*slot);
            *slot = std::move(request);
            request = Channel{};
            slot->pair = pair;
            startPreload(*slot, files);
        }
    }

    // 2-4. Each playing channel by its state.
    for (std::size_t i = 0; i < 2; ++i) {
        Channel& channel = m_channels.at(i);
        Channel& other = m_channels.at(1 - i);
        switch (channel.state) {
        case MusicState::PreLoading:
            if (!channel.feeder->ready()) {
                break;
            }
            if (other.state == MusicState::Playing) {
                // Wait for the playing track's next bar boundary.
                const double bar = std::max(other.barMs, 1.0F);
                channel.nextBar = other.playStart + (std::ceil((now - other.playStart) / bar) * bar);
                channel.state = MusicState::BarSync;
            } else if (other.state == MusicState::FadeIn || other.state == MusicState::FadeOut) {
                start(channel, now, false);
                fadeOut(other, now, other.barMs * static_cast<float>(channel.fadeBars));
            } else {
                start(channel, now, true);
            }
            break;
        case MusicState::BarSync:
            if (now >= channel.nextBar - kBarLeadMs) {
                start(channel, now, false);
                fadeOut(other, now, other.barMs * static_cast<float>(channel.fadeBars));
            }
            break;
        case MusicState::FadeIn:
            channel.fade = static_cast<float>(std::min(1.0, (now - channel.fadeStart) / channel.fadeLength));
            if (channel.fade >= 1.0F) {
                channel.state = MusicState::Playing;
            }
            break;
        case MusicState::FadeOut: {
            const auto level = static_cast<float>(1.0 - ((now - channel.fadeStart) / channel.fadeLength));
            channel.fade = std::min(channel.fade, std::max(level, 0.0F));
            if (level <= 0.0F) {
                halt(channel);
            }
            break;
        }
        case MusicState::Playing:
            // A track played once has ended when its stream is all played.
            if (channel.feeder->done() && !m_mixer.isPlaying(channel.voice)) {
                const std::string callback = channel.callback;
                halt(channel);
                if (!callback.empty() && m_onTrackEnd) {
                    m_onTrackEnd(callback);
                }
            }
            break;
        default:
            break;
        }
    }

    // Feed the streams and send the volumes (docs/research/sound.md#music: fade × track volume × music volume ×
    // options' music volume, ducked while a scene plays).
    for (std::size_t i = 0; i < 2; ++i) {
        Channel& channel = m_channels.at(i);
        if (channel.feeder && files != nullptr && active(channel)) {
            if (auto fed = channel.feeder->pump(*files); !fed) {
                ++m_readErrors;
            }
        }
        if (!channel.voice.valid()) {
            continue;
        }
        float volume = channel.fade * channel.trackVolume * m_volume * m_optionsVolume;
        if (m_scenePlaying && m_duckEnabled) {
            volume *= m_duck;
        }
        volume = std::clamp(volume, 0.0F, 1.0F);
        if (volume != channel.sent) {
            m_mixer.setStereoVolume(channel.voice, volume, volume);
            channel.sent = volume;
        }
    }
}

} // namespace coney::audio
