// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace coney::graphics {

/// The two passes a car is drawn in (docs/research/graphics.md#car-draw).
enum class CarPass : std::uint8_t {
    Opaque, ///< After the humans: every part but the glass, Z written.
    Glass,  ///< After the water and the other see-through objects: the glass parts only, Z not written.
};

/// The parts a car model holds undamaged: atomic `p` is part `p`; atomics from here on are damaged forms.
inline constexpr std::size_t kCarModelParts = 26;
/// The glass parts, drawn only in the glass pass: 6 and 7 (the windscreen and the back window) and the door windows
/// 15, 17, 19 and 21. `CarPart_RenderCallback`'s mask, confirmed (code).
inline constexpr std::uint32_t kCarGlassMask = 0x2a80c0U;
/// The parts the car's paint tints: 0 (the body), 4 and 5 (the bonnet and boot), 10-14, 16, 18 and 20 (the side
/// panels and doors); every other part draws in white, so its texture shows as it is. `CarInstance_PartColour`'s mask,
/// confirmed (code).
inline constexpr std::uint32_t kCarPaintMask = 0x157c31U;
/// A part shows its damaged form (atomic `p + 25`) from this much damage on.
inline constexpr float kCarDamagedForm = 0.5F;
/// The first and last wheel: they have no damaged form and always draw undamaged.
inline constexpr std::size_t kCarFirstWheel = 22;
inline constexpr std::size_t kCarLastWheel = 25;

/// Whether the paint tints part `part`.
/// @orig 0x001728e0 CarInstance_PartColour (unknown)
[[nodiscard]] constexpr bool carPartPainted(std::size_t part) {
    return part < 32 && ((kCarPaintMask >> part) & 1U) != 0;
}

/// Whether part `part` is glass, drawn in the glass pass.
[[nodiscard]] constexpr bool carPartGlass(std::size_t part) { return part < 32 && ((kCarGlassMask >> part) & 1U) != 0; }

/// The part atomic `atomic` of a car model belongs to: itself for the first 26, `atomic − 25` for a damaged form.
[[nodiscard]] constexpr std::size_t carPartOf(std::size_t atomic) {
    return atomic < kCarModelParts ? atomic : atomic - (kCarModelParts - 1);
}

/// Whether atomic `atomic` of a car's model draws in `pass`, given the car's removed parts (one bit per part, its
/// `+0x11f0`) and each part's damage (0 to 1): not a removed part; the glass parts only in the glass pass and the
/// others only in the opaque one; the undamaged form while the damage is below 0.5 (a wheel's always), the damaged form
/// from 0.5 on. **Coney's choice**: the "seen" bits `+0x11fc` the visibility callback sets are not kept; every part of
/// a car that is drawn counts as seen.
///
/// Research: docs/research/graphics.md#car-draw, docs/research/cars.md#drawn
/// @orig 0x00172940 CarPart_RenderCallback (unknown)
[[nodiscard]] bool carAtomicDraws(std::size_t atomic, std::uint32_t removedParts, std::span<const float> damage,
                                  CarPass pass);

} // namespace coney::graphics
