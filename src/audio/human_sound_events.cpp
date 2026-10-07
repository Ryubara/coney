// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/human_sound_events.h"

#include <optional>
#include <utility>

#include "human/human_sounds.h"

namespace coney::audio {

namespace {

namespace mat = human::material;

// The animation sound ids (`SA.*`, docs/research/sound-events.md#anim-sounds) the table names.
namespace sa {
inline constexpr std::uint32_t kNone = 0;
inline constexpr std::uint32_t kFootstep = 1;
inline constexpr std::uint32_t kFootstepRun = 3;
inline constexpr std::uint32_t kShortFabric = 4;
inline constexpr std::uint32_t kSwooshBig = 5;
inline constexpr std::uint32_t kLongFabric = 6;
inline constexpr std::uint32_t kWhistle = 7;
inline constexpr std::uint32_t kSwooshSml = 8;
inline constexpr std::uint32_t kGruntPch = 9;
inline constexpr std::uint32_t kTrueKneeDrop = 12;
inline constexpr std::uint32_t kHitGround = 13;
inline constexpr std::uint32_t kFabricFriction = 15;
inline constexpr std::uint32_t kBossSwoosh1 = 16;
inline constexpr std::uint32_t kXHitGround = 18;
inline constexpr std::uint32_t kGrunt = 22;
inline constexpr std::uint32_t kSwooshPunch3 = 23;
inline constexpr std::uint32_t kGruntX = 28;
inline constexpr std::uint32_t kCmdMount = 29;
inline constexpr std::uint32_t kGruntLand = 30;
inline constexpr std::uint32_t kGruntLightLift = 32;
inline constexpr std::uint32_t kCmdWave = 35;
inline constexpr std::uint32_t kJumpLand = 37;
inline constexpr std::uint32_t kWinded = 39;
inline constexpr std::uint32_t kLightHitGround = 40;
inline constexpr std::uint32_t kAgony = 41;
inline constexpr std::uint32_t kThrownHitGround = 43;
inline constexpr std::uint32_t kWeaponSwoosh = 44;
inline constexpr std::uint32_t kKickToBody = 46;
inline constexpr std::uint32_t kZoom = 50;
inline constexpr std::uint32_t kBottleSmash = 52;
inline constexpr std::uint32_t kGruntStrain = 54;
inline constexpr std::uint32_t kCmdThrow = 56;
inline constexpr std::uint32_t kGruntHeavyLift = 57;
inline constexpr std::uint32_t kDie = 58;
inline constexpr std::uint32_t kGruntPain = 59;
inline constexpr std::uint32_t kUncuff = 60;
inline constexpr std::uint32_t kStab = 62;
inline constexpr std::uint32_t kKickToHead = 65;
inline constexpr std::uint32_t kFenceRattleBig = 66;
inline constexpr std::uint32_t kLightLand = 67;
inline constexpr std::uint32_t kFenceRattleSmall = 68;
inline constexpr std::uint32_t kLand = 69;
inline constexpr std::uint32_t kGrabBackGrunt = 71;
inline constexpr std::uint32_t kGruntHitFace = 72;
inline constexpr std::uint32_t kCmdRage = 82;
inline constexpr std::uint32_t kSprayLoop = 83;
inline constexpr std::uint32_t kHeadWtand = 86;
inline constexpr std::uint32_t kStealthHands = 87;
inline constexpr std::uint32_t kStealthBaton = 88;
inline constexpr std::uint32_t kStealthKnife = 89;
inline constexpr std::uint32_t kBossSwoosh2 = 90;
inline constexpr std::uint32_t kBossSwoosh3 = 91;
inline constexpr std::uint32_t kBossSwoosh = 92;
inline constexpr std::uint32_t kCmdBoss = 95;
inline constexpr std::uint32_t kCmdPoint = 97;
inline constexpr std::uint32_t kGrabGrunt = 101;
inline constexpr std::uint32_t kCmdKiyap = 103;
inline constexpr std::uint32_t kGruntFemale = 108;
inline constexpr std::uint32_t kGruntPchFemale = 109;
inline constexpr std::uint32_t kGrabGruntFemale = 110;
inline constexpr std::uint32_t kWindedFemale = 111;
inline constexpr std::uint32_t kGrabBackGruntFemale = 112;
inline constexpr std::uint32_t kGruntXFemale = 113;
inline constexpr std::uint32_t kDieFemale = 114;
inline constexpr std::uint32_t kExpire = 115;
inline constexpr std::uint32_t kExpireFemale = 116;
inline constexpr std::uint32_t kCmdFire = 139;
inline constexpr std::uint32_t kBottleStab = 140;
inline constexpr std::uint32_t kBrickSmash = 141;
inline constexpr std::uint32_t kCueballSmash = 142;
inline constexpr std::uint32_t kGruntHeavyLiftFemale = 144;
inline constexpr std::uint32_t kGruntLightLiftFemale = 145;
inline constexpr std::uint32_t kSprayFace = 146;
inline constexpr std::uint32_t kSleepSnore = 149;
inline constexpr std::uint32_t kSleepGrunt = 150;
inline constexpr std::uint32_t kPukeBig = 156;
inline constexpr std::uint32_t kPuke = 161;
} // namespace sa

// The speech commands the table says (docs/references/speech.md).
namespace command {
inline constexpr std::uint32_t kThrow = 10;
inline constexpr std::uint32_t kPain = 12;
inline constexpr std::uint32_t kMount = 14;
inline constexpr std::uint32_t kPoint = 15;
inline constexpr std::uint32_t kAgony = 34;
inline constexpr std::uint32_t kThrow2 = 38;
inline constexpr std::uint32_t kOnFire = 107;
inline constexpr std::uint32_t kHeavyLift = 151;
inline constexpr std::uint32_t kLightLift = 152;
inline constexpr std::uint32_t kRage = 160;
inline constexpr std::uint32_t kRoar = 167;
inline constexpr std::uint32_t kKiyap = 176;
inline constexpr std::uint32_t kSprayFace = 203;
} // namespace command

// Held objects' materials and the broken bottle's model (docs/research/sound-events.md#anim-pairs).
constexpr std::uint32_t kBrick = 102;
constexpr std::uint32_t kPoolBall = 60;
constexpr std::uint32_t kBottle = 37;
constexpr std::uint32_t kBrokenBottleModel = 0xc68a017c;
constexpr int kMolotovType = 8;

// Body materials of the footsteps and body falls.
constexpr std::uint32_t kKnee = 34;
constexpr std::uint32_t kHumanLight = 161;
constexpr std::uint32_t kHumanHeavy = 162;
constexpr std::uint32_t kFeetLight = 163;
constexpr std::uint32_t kFeetLand = 164;
constexpr std::uint32_t kFeet = 101;

// A footstep or body fall: its body material and volume.
struct Footstep {
    std::uint32_t body = 0;
    float volume = 1.0F;
};

// The footstep ids (docs/research/sound-events.md#footsteps).
std::optional<Footstep> footstepOf(std::uint32_t id) {
    switch (id) {
    case sa::kFootstep:
        return Footstep{.body = mat::kShoe, .volume = 1.0F};
    case sa::kFootstepRun:
        return Footstep{.body = mat::kShoe, .volume = 1.25F};
    case sa::kTrueKneeDrop:
        return Footstep{.body = kKnee};
    case sa::kHitGround:
        return Footstep{.body = mat::kHuman};
    case sa::kXHitGround:
        return Footstep{.body = mat::kTorso};
    case sa::kJumpLand:
        return Footstep{.body = kFeetLand};
    case sa::kLightHitGround:
        return Footstep{.body = kHumanLight};
    case sa::kThrownHitGround:
        return Footstep{.body = kHumanHeavy};
    case sa::kLightLand:
        return Footstep{.body = kFeetLight};
    case sa::kLand:
        return Footstep{.body = kFeet};
    default:
        return std::nullopt;
    }
}

// A vocal id: its female id (0 none), whether it plays over a line and whether it cuts.
struct Vocal {
    std::uint32_t female = 0;
    bool overLine = true;
    bool cut = false;
};

// The vocal ids that need no condition (docs/research/sound-events.md#vocal-ids).
std::optional<Vocal> vocalOf(std::uint32_t id) {
    switch (id) {
    case sa::kWhistle:
    case sa::kSleepSnore:
    case sa::kSleepGrunt:
    case sa::kPukeBig:
    case sa::kPuke:
    case sa::kGruntLand:
    case sa::kGruntStrain:
    case sa::kGruntHitFace:
        return Vocal{};
    case sa::kGruntPch:
        return Vocal{.female = sa::kGruntPchFemale};
    case sa::kGrunt:
        return Vocal{.female = sa::kGruntFemale};
    case sa::kWinded:
        return Vocal{.female = sa::kWindedFemale};
    case sa::kGrabBackGrunt:
        return Vocal{.female = sa::kGrabBackGruntFemale};
    case sa::kGrabGrunt:
        return Vocal{.female = sa::kGrabGruntFemale};
    case sa::kExpire:
        return Vocal{.female = sa::kExpireFemale, .overLine = false, .cut = true};
    default:
        return std::nullopt;
    }
}

// The human's feet as a sound position.
SoundVec placeOf(const script::HumanSoundCall& who) {
    return SoundVec{who.position[0], who.position[1], who.position[2]};
}

// The human as a sound's owner.
std::uint32_t ownerOf(const script::HumanSoundCall& who) { return static_cast<std::uint32_t>(who.human); }

// 1.5 when the human's target is a player, else 1 (the louder command lines).
float louderAtPlayer(const script::HumanSoundCall& who) { return who.targetIsPlayer ? 1.5F : 1.0F; }

} // namespace

HumanSoundEvents::HumanSoundEvents(MaterialSoundPlayer& sounds, HumanVoices& voices, RandomRange random)
    : m_sounds(sounds), m_voices(voices), m_random(std::move(random)) {}

void HumanSoundEvents::play(const script::HumanSoundCall& call, const HumanTraits& traits) {
    const human::HumanSound& sound = call.sound;
    if (sound.kind == human::HumanSound::Kind::Anim) {
        animSound(call, traits, sound.animSound);
        return;
    }
    // A hit sounds at its owner (the attacker), as a player's when he is one.
    script::HumanSoundCall owner = call;
    owner.player = sound.ownerIsPlayer;
    owner.position = {sound.at.x, sound.at.y, sound.at.z};
    impact(owner, traits, sound.volume, sound.material1, sound.material2, sound.victimDown,
           SoundVec{sound.at.x, sound.at.y, sound.at.z});
}

float HumanSoundEvents::combatFactor(const script::HumanSoundCall& who, const HumanTraits& traits) {
    return who.player && who.combatFraming ? traits.combatFactor : 1.0F;
}

bool HumanSoundEvents::playAnimSound(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id) {
    const std::optional<MatrixSounds> entry = m_sounds.matrix().nextAnimSounds(id);
    if (!entry) {
        return false;
    }
    const SoundVec at = placeOf(who);
    if (!who.player) {
        return m_sounds.playSound(entry->sounds[0], entry->volumes[0], 1.0F, at, ownerOf(who)).valid();
    }
    // A player's: column 1 (his) and column 2 at twice the volume, column 3 too under combat framing.
    const float volume = 2.0F * combatFactor(who, traits);
    const bool played =
        m_sounds.playSound(entry->sounds[0], volume * entry->volumes[0], 1.0F, at, ownerOf(who)).valid();
    static_cast<void>(m_sounds.playSound(entry->sounds[1], volume * entry->volumes[1], 1.0F, at));
    if (who.combatFraming) {
        static_cast<void>(m_sounds.playSound(entry->sounds[2], volume * entry->volumes[2], 1.0F, at));
    }
    return played;
}

void HumanSoundEvents::footstep(const script::HumanSoundCall& who, const HumanTraits& traits, float volume,
                                std::uint32_t body) {
    const MaterialSoundPlayer::FootMaterial ground = MaterialSoundPlayer::remapFootMaterial(who.ground);
    float level = volume * ground.volume * combatFactor(who, traits);
    // Sneaking in shadow: a shoe's step at half volume, without a player's doubling.
    if (body == mat::kShoe && who.hiddenInShadow) {
        level *= 0.5F;
    } else if (who.player) {
        level *= 2.0F;
    }
    m_sounds.playMaterialPair(level, body, ground.material, placeOf(who));
}

bool HumanSoundEvents::sayAnimLine(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id,
                                   std::uint32_t femaleId, bool overLine, bool cut) {
    if (!traits.canSpeak) {
        return false;
    }
    const std::uint32_t entryId = traits.female && femaleId != 0 ? femaleId : id;
    const std::optional<MatrixSounds> entry = m_sounds.matrix().nextAnimSounds(entryId);
    if (!entry || entry->sounds[0] == 0) {
        return false;
    }
    // Over a line only when asked, and then the line stops first.
    if (m_voices.speaking(who.human)) {
        if (!overLine) {
            return false;
        }
        m_voices.stopLine(who.human);
    }
    const float volume = entry->volumes[0] * (who.player ? 2.0F : 1.0F);
    return m_voices.sayLine(who, entry->sounds[0], volume, cut);
}

void HumanSoundEvents::heldContact(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t other) {
    if (!who.heldMaterial) {
        return;
    }
    const std::uint32_t held = *who.heldMaterial;
    const std::uint32_t against = other != 0 ? other : held;
    if (who.player) {
        m_sounds.playMaterialPair(2.0F * combatFactor(who, traits), held, against, placeOf(who));
    } else {
        m_sounds.playMaterialHit(1.0F, held, against, placeOf(who));
    }
}

void HumanSoundEvents::animSound(const script::HumanSoundCall& who, const HumanTraits& traits, std::uint32_t id) {
    // Footsteps and body falls.
    if (const std::optional<Footstep> step = footstepOf(id); step) {
        footstep(who, traits, step->volume, step->body);
        return;
    }
    // The vocal ids without a condition.
    if (const std::optional<Vocal> vocal = vocalOf(id); vocal) {
        static_cast<void>(sayAnimLine(who, traits, id, vocal->female, vocal->overLine, vocal->cut));
        return;
    }
    const SoundVec at = placeOf(who);
    const float doubled = who.player ? 2.0F : 1.0F;
    switch (id) {
    case sa::kNone:
        return;
    // Fixed material pairs.
    case sa::kKickToBody:
        m_sounds.playMaterialPair(doubled, mat::kShoe, mat::kTorsoProne, at);
        return;
    case sa::kKickToHead:
        m_sounds.playMaterialPair(doubled, mat::kShoe, mat::kHead, at);
        return;
    case sa::kBottleSmash: {
        if (!who.heldMaterial) {
            return;
        }
        // By the held object's material its own smash, then the material against itself.
        std::uint32_t smash = 0;
        switch (*who.heldMaterial) {
        case kBrick:
            smash = sa::kBrickSmash;
            break;
        case kPoolBall:
            smash = sa::kCueballSmash;
            break;
        case kBottle:
            smash = sa::kBottleSmash;
            break;
        default:
            break;
        }
        if (smash == 0 || !playAnimSound(who, traits, smash)) {
            heldContact(who, traits, 0);
        }
        return;
    }
    case sa::kHeadWtand:
        heldContact(who, traits, mat::kHead);
        return;
    case sa::kStab:
        static_cast<void>(
            playAnimSound(who, traits, who.heldModel == kBrokenBottleModel ? sa::kBottleStab : sa::kStab));
        return;
    // Sounds without a speaker.
    case sa::kShortFabric:
    case sa::kLongFabric:
    case sa::kFabricFriction:
        if (who.player || traits.bossClass) {
            m_sounds.playAnimSound(doubled, id, at);
        }
        return;
    case sa::kSwooshBig:
    case sa::kSwooshSml:
    case sa::kBossSwoosh1:
    case sa::kSwooshPunch3:
    case sa::kWeaponSwoosh:
    case sa::kBossSwoosh2:
    case sa::kBossSwoosh3:
    case sa::kBossSwoosh:
        m_sounds.playAnimSound(doubled, id, at);
        return;
    case sa::kZoom:
        m_sounds.playAnimSound(1.0F, id, at);
        return;
    case sa::kFenceRattleBig:
    case sa::kFenceRattleSmall:
        m_sounds.playAnimSound(1.0F, id, at, ownerOf(who));
        return;
    case sa::kSprayLoop: {
        // One loop per human: the last one stops.
        if (const auto found = m_loops.find(who.human); found != m_loops.end()) {
            m_sounds.stop(found->second);
        }
        if (const std::optional<MatrixSounds> entry = m_sounds.matrix().nextAnimSounds(id); entry) {
            m_loops[who.human] = m_sounds.playSound(entry->sounds[0], entry->volumes[0], 1.0F, at, ownerOf(who));
        }
        return;
    }
    case sa::kStealthHands:
    case sa::kStealthBaton:
    case sa::kStealthKnife:
        if (!m_voices.speaking(who.human) && traits.canSpeak) {
            if (const std::optional<MatrixSounds> entry = m_sounds.matrix().nextAnimSounds(id);
                entry && entry->sounds[0] != 0) {
                static_cast<void>(m_voices.sayLine(who, entry->sounds[0], 1.0F, true));
            }
        }
        return;
    // Vocal ids with a condition.
    case sa::kGruntPain:
        static_cast<void>(sayAnimLine(who, traits, sa::kGrunt, sa::kGruntFemale, true, false));
        return;
    case sa::kDie:
        if (!m_voices.scenePlaying()) {
            static_cast<void>(sayAnimLine(who, traits, id, sa::kDieFemale, true, true));
        }
        return;
    // Speech commands.
    case sa::kGruntX:
        if (!m_voices.sayCommand(who, command::kPain, louderAtPlayer(who), true, true)) {
            static_cast<void>(sayAnimLine(who, traits, id, sa::kGruntXFemale, true, false));
        }
        return;
    case sa::kCmdMount:
        if (m_voices.mayGesture(who)) {
            static_cast<void>(m_voices.sayCommand(who, command::kMount, louderAtPlayer(who), true, true));
        }
        return;
    case sa::kGruntLightLift:
        if (!m_voices.sayCommand(who, command::kLightLift, 1.0F, false, true)) {
            static_cast<void>(sayAnimLine(who, traits, id, sa::kGruntLightLiftFemale, false, false));
        }
        return;
    case sa::kCmdWave:
    case sa::kCmdPoint:
        if (m_voices.mayGesture(who)) {
            static_cast<void>(m_voices.sayCommand(who, command::kPoint, louderAtPlayer(who), true, true));
        }
        return;
    case sa::kAgony:
        // Duckable: Coney's humans have no interrogation lines (which make it not duckable).
        static_cast<void>(m_voices.sayCommand(who, command::kAgony, 1.0F, false, true));
        return;
    case sa::kCmdThrow:
        if (who.hasThrowTarget) {
            const std::uint32_t line = who.heldType == kMolotovType ? command::kThrow2 : command::kThrow;
            static_cast<void>(m_voices.sayCommand(who, line, 1.0F, true, true));
        }
        return;
    case sa::kGruntHeavyLift:
        if (!m_voices.sayCommand(who, command::kHeavyLift, 1.0F, true, true)) {
            static_cast<void>(sayAnimLine(who, traits, id, sa::kGruntHeavyLiftFemale, false, false));
        }
        return;
    case sa::kCmdRage:
        static_cast<void>(m_voices.sayCommand(who, command::kRage, 1.0F, true, true));
        return;
    case sa::kCmdBoss:
        static_cast<void>(m_voices.sayCommand(who, command::kRoar, 1.0F, true, true));
        return;
    case sa::kCmdKiyap:
        if (m_voices.mayGesture(who)) {
            static_cast<void>(m_voices.sayCommand(who, command::kKiyap, louderAtPlayer(who), true, true));
        }
        return;
    case sa::kCmdFire:
        if (who.burning) {
            static_cast<void>(m_voices.sayCommand(who, command::kOnFire, 1.0F, true, true));
        }
        return;
    case sa::kSprayFace:
        static_cast<void>(m_voices.sayCommand(who, command::kSprayFace, 1.0F, true, true));
        return;
    // The uncuff's prepared grab sound is not built (Coney's stand-in): its own entry plays, as when none was
    // prepared.
    case sa::kUncuff:
    default:
        static_cast<void>(playAnimSound(who, traits, id));
        return;
    }
}

void HumanSoundEvents::impact(const script::HumanSoundCall& who, const HumanTraits& traits, float volume,
                              std::uint32_t m1, std::uint32_t m2, bool victimDown, SoundVec at) {
    // On a victim knocked down, the torso and the head sound as the prone torso.
    if (victimDown) {
        const auto prone = [](std::uint32_t m) { return m == mat::kTorso || m == mat::kHead ? mat::kTorsoProne : m; };
        m1 = prone(m1);
        m2 = prone(m2);
    }
    const std::optional<MatrixSounds> entry =
        m_sounds.matrix().nextMaterialSounds(m1, m2, MaterialSoundPlayer::kDefaultMaterial);
    if (!entry) {
        return;
    }
    if (!who.player) {
        static_cast<void>(m_sounds.playSound(entry->sounds[0], volume * entry->volumes[0], 1.0F, at, ownerOf(who)));
        return;
    }
    // A player's: columns 1 and 2 at twice the volume, and under combat framing column 3 at a pitch 1 ± 0-19 %.
    const float level = 2.0F * volume * combatFactor(who, traits);
    static_cast<void>(m_sounds.playSound(entry->sounds[0], level * entry->volumes[0], 1.0F, at, ownerOf(who)));
    static_cast<void>(m_sounds.playSound(entry->sounds[1], level * entry->volumes[1], 1.0F, at, ownerOf(who)));
    if (who.combatFraming) {
        constexpr std::int32_t kPitchSpread = 19;
        const float pitch = 1.0F + (static_cast<float>(m_random ? m_random(-kPitchSpread, kPitchSpread) : 0) / 100.0F);
        static_cast<void>(m_sounds.playSound(entry->sounds[2], level * entry->volumes[2], pitch, at, ownerOf(who)));
    }
}

} // namespace coney::audio
