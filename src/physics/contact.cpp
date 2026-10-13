// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/contact.h"

#include <algorithm>

namespace coney::physics {

void insertSorted(Contact& contact, std::vector<Contact*>& sorted, BodyOwner* ownOwner, BodyOwner* otherOwner,
                  bool otherShape) {
    // The moving body always queries with a shape of its own; the other half's word is 0 for the level.
    contact.halves[0].owner = ownOwner;
    contact.halves[0].shape = true;
    contact.halves[1].owner = otherOwner;
    contact.halves[1].shape = otherShape;
    // Before the first strictly greater fraction, so after any equal ones.
    const auto at =
        std::ranges::find_if(sorted, [&contact](const Contact* c) { return c->fraction > contact.fraction; });
    sorted.insert(at, &contact);
}

} // namespace coney::physics
