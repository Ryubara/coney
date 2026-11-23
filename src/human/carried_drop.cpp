// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/carried_drop.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace coney::human {

namespace {

// The brain types the drop tells apart (ai::BrainType's numbers), and ped type 5.
constexpr int kCopBrain = 1;
constexpr int kCivilianBrain = 4;
constexpr int kDealerBrain = 5;
constexpr int kDealerPed = 5;
// The neighbour sectors round a human.
constexpr int kSectors = 8;
// A draw of rand(0, 100) below this drops the cuffs, and keeps a player's carried object.
constexpr int kDrawBelow = 51;
constexpr int kDrawMost = 100;
// The inventory items whose durations time each kind of drop.
constexpr int kReviveItem = 1;
constexpr int kMoneyItem = 2;
constexpr int kCuffsItem = 5;
constexpr int kKeyItem = 6;
constexpr int kOtherItem = 10;

// Whether `object` names nothing: `none`, or the empty name a Coney human starts with (Coney's choice: the original's
// `+0x257` always holds a name).
// @orig 0x00232c60 Human_DropCarried (unknown)
bool nothing(std::string_view object) { return object.empty() || object == kNoObject; }

// The item whose duration times a dropped carried object: a flash, a key or cuffs by name, else 10.
// @orig 0x00232c60 Human_DropCarried (unknown)
int carriedRemovalItem(std::string_view object) {
    if (object == "dyn_revival") {
        return kReviveItem;
    }
    if (object == "dyn_keys") {
        return kKeyItem;
    }
    if (object == "dyn_cuffs") {
        return kCuffsItem;
    }
    return kOtherItem;
}

// The pocket's items: `count` objects at his feet, or for a dealer (round = true) each in the next sector round him
// that is not blocked, the search going on from the last one taken and starting again at 0 when all eight are.
// @orig 0x00232c60 Human_DropCarried (unknown)
void dropPocket(const DropInput& input, int count, bool round, const SectorBlocked& blocked, CarriedDrop& drop) {
    int sector = 0;
    for (int made = 0; made < count; ++made) {
        DroppedObject object{.type = input.pocketObject, .money = 0, .removalItem = 0, .sector = std::nullopt};
        if (round) {
            // The original asks sector 8 too before it stops; its answer changes nothing, so it is not asked.
            while (sector < kSectors && blocked(sector)) {
                ++sector;
            }
            if (sector == kSectors) {
                sector = 0;
            }
            object.sector = sector;
            ++sector;
        }
        drop.objects.push_back(std::move(object));
    }
}

// The money he drops (`brain` his brain's type): a civilian his class's share (at least 1), another AI all of it, a
// player a draw of his class's range, at most what his inventory holds, taken from it.
// @orig 0x00232c60 Human_DropCarried (unknown)
int moneyDropped(const DropInput& input, int brain, GameRandom& random, CarriedDrop& drop) {
    if (brain == kCivilianBrain) {
        if (input.money <= 0) {
            return 0;
        }
        const int share = static_cast<int>(static_cast<float>(input.money) * input.dropClass.moneyShare);
        return std::max(share, 1);
    }
    if (!input.player) {
        return input.money;
    }
    const int drawn = std::min(random.range(RandomStream::Gameplay, input.dropClass.moneyMin, input.dropClass.moneyMax),
                               input.playerMoney);
    drop.playerMoneyTaken += drawn;
    return drawn;
}

} // namespace

CarriedDrop dropCarried(const DropInput& input, GameRandom& random, const SectorBlocked& blocked) {
    CarriedDrop drop{.objects = {}, .money = input.money, .carried = input.carried, .playerMoneyTaken = 0};
    // 1. No brain, nothing.
    if (!input.brainType.has_value()) {
        return drop;
    }
    const int brain = *input.brainType;
    // 2. The pocket: a dealer drops one object round him, anyone else its whole count at his feet; then nothing more
    // unless he is a dealer or of the dealers' ped type. **Coney's choice**: a count at or below 0 drops none (the
    // original's byte read signed and compared unsigned would loop for ever).
    if (input.pocketItem != 0) {
        const bool round = brain == kDealerBrain;
        dropPocket(input, round ? 1 : input.pocketCount, round, blocked, drop);
        if (input.pedType != kDealerPed && brain != kDealerBrain) {
            return drop;
        }
    }
    // 3. Power cuffs: a cuff holder on a draw below 51, in the game mode or with the unlock; then nothing else.
    if (input.cuffs != 0 && random.range(RandomStream::Gameplay, 0, kDrawMost) < kDrawBelow &&
        (input.gameMode || input.cuffsUnlocked)) {
        drop.objects.push_back(DroppedObject{
            .type = std::string(kPowerCuffsObject), .money = 0, .removalItem = kCuffsItem, .sector = std::nullopt});
        return drop;
    }
    // 4. The carried object: an AI drops it unless its chance is -1; a player keeps it on a draw below 51, and
    // otherwise drops it and pays its price in money. Either way it is all he drops.
    if (!nothing(input.carried)) {
        if (!input.player && input.carriedChance == -1) {
            return drop;
        }
        if (input.player) {
            if (random.range(RandomStream::Gameplay, 0, kDrawMost) < kDrawBelow) {
                return drop;
            }
            drop.playerMoneyTaken += static_cast<int>(static_cast<float>(input.carriedPrice) * input.moneyScale);
        }
        drop.objects.push_back(DroppedObject{.type = input.carried,
                                             .money = 0,
                                             .removalItem = carriedRemovalItem(input.carried),
                                             .sector = std::nullopt});
        drop.carried = std::string(kNoObject);
        return drop;
    }
    // 5. A cop of a class whose `+0xb4` is 0 drops a key once it is unlocked, and keeps his money.
    if (brain == kCopBrain && input.dropClass.carriedChance == 0 && input.keysUnlocked) {
        drop.objects.push_back(DroppedObject{
            .type = std::string(kKeyObject), .money = 0, .removalItem = kKeyItem, .sector = std::nullopt});
        return drop;
    }
    // 6. Otherwise his money: $1 or more makes a dyn_money holding it, and he has none left. Each object's removalItem
    // names the inventory duration of its removal time, which the play mode does not apply yet (a stand-in,
    // LevelPickups::dropCarried()).
    const int amount = moneyDropped(input, brain, random, drop);
    if (amount < 1) {
        return drop;
    }
    drop.objects.push_back(DroppedObject{.type = std::string(kMoneyObject),
                                         .money = static_cast<std::uint32_t>(amount),
                                         .removalItem = kMoneyItem,
                                         .sector = std::nullopt});
    drop.money = 0;
    return drop;
}

} // namespace coney::human
