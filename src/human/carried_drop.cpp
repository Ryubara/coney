// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/carried_drop.h"

namespace coney::human {

CarriedDrop dropCarried(const DropInput& input, GameRandom& /*random*/, const SectorBlocked& /*blocked*/) {
    return CarriedDrop{.objects = {}, .money = input.money, .carried = input.carried, .playerMoneyTaken = 0};
}

} // namespace coney::human
