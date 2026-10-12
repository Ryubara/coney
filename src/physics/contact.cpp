// SPDX-License-Identifier: GPL-3.0-or-later
#include "physics/contact.h"

#include <algorithm>

namespace coney::physics {

void insertSorted(Contact& contact, std::vector<Contact*>& sorted, BodyOwner* ownOwner, BodyOwner* otherOwner,
                  bool otherShape) {
    contact.ownOwner = ownOwner;
    contact.otherOwner = otherOwner;
    contact.otherShape = otherShape;
    // Before the first strictly greater fraction, so after any equal ones.
    const auto at =
        std::ranges::find_if(sorted, [&contact](const Contact* c) { return c->fraction > contact.fraction; });
    sorted.insert(at, &contact);
}

} // namespace coney::physics
