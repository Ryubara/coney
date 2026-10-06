// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// The parked cars' types and how their models are laid out (docs/research/cars.md).

namespace coney::world_objects {

/// The six car type names `CarSpawn` takes, in index order (the table at 0x00512ba8). A type's Object List record is
/// found by its name; its model is `<name>_geo` (docs/research/cars.md#types).
inline constexpr std::array<std::string_view, 6> kCarTypeNames{"car_osedan", "car_coupe", "car_wagon",
                                                               "car_copcar", "car_van",   "car_sullycar"};

/// A car's parts (body, roof, bumpers, doors, wheels and the rest; docs/research/cars.md#type-record). Atomic `p` of
/// the car's model is part `p`, so the first kCarParts atomics make the undamaged car; the rest are the damaged
/// forms of parts 1 to 21, atomic `p + 25` for part `p` (docs/research/cars.md#model).
inline constexpr std::size_t kCarParts = 26;

} // namespace coney::world_objects
