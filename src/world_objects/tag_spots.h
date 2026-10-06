// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace coney::world_objects {

/// One graffiti tag spot: the record a `part_spray_tag` particle object keeps (docs/research/crimes.md#tag-spots).
struct TagSpot {
    /// The fade modes (`+0x04`).
    static constexpr int kStill = 0;
    static constexpr int kFadingOut = 1;
    static constexpr int kFadingIn = 2;
    /// The spray modes (`+0x0c`): what the next spray does.
    static constexpr int kPaintIn = 7;
    static constexpr int kWipeOut = 5;

    double handle = 0;              ///< The particle object's handle.
    std::uint32_t sprite = 0x50000; ///< The finished tag's sprite word (`CfgTagSettings`, message `0x27`).
    float start = 0.5F;             ///< `+0xc0` (message `0x37`); its reader is not traced.
    float fadeStep = 0.005F;        ///< `+0x14`: the fraction's change per update while fading (message `0x38`).
    float depth = 1.0F;             ///< `+0x18` (message `0x3b`); the batch's depth is 10 + depth × 0.1.
    int fadeMode = kStill;          ///< `+0x04`.
    int sprayMode = kPaintIn;       ///< `+0x0c`.
    float fraction = 0.0F;          ///< `+0x10` / object `+0xc8`: painted, 0-1 (the drawn opacity, the game's start).
    double tagger = 0;              ///< `+0x1c`: the human spraying it, 0 for none.
};

/// The level's tag spots, by their particle objects' handles: `CfgTagSettings` configures one, `ProcessTag` sets its
/// state, a spray fills it in or wipes it out. Each method is one of the spot's messages (`0x003fc8d8`); a spot's
/// record is made with its defaults the first time a message reaches its handle.
///
/// Research: docs/research/crimes.md#tag-spots, docs/references/bindings/level.md#processtag
class TagSpots {
  public:
    /// The messages a spot sends its tagger: `0x13` (done), `0x17` (state 1: go and spray, with the tag's position).
    static constexpr int kTaggerDone = 0x13;
    static constexpr int kTaggerSpray = 0x17;
    /// The fade starts sending spray particles while the fraction is in this range.
    static constexpr float kSprayFrom = 0.03F;
    static constexpr float kSprayTo = 0.95F;

    /// Sends the tagger `message` (and, for kTaggerSpray, the tag's handle).
    using TaggerMessage = std::function<void(double human, int message, double tag)>;
    /// Sets or clears path flag 8 on the path polygon at the tag (`on` false clears it).
    using PathFlag = std::function<void(double tag, bool on)>;

    void setTaggerMessage(TaggerMessage send) { m_send = std::move(send); }
    void setPathFlag(PathFlag flag) { m_flag = std::move(flag); }

    /// `CfgTagSettings(object, sprite, start, fade, depth)`: messages `0x27`, `0x37`, `0x38` and `0x3b`; a negative
    /// depth counts as 0.
    /// @orig 0x0039bc28 Tag_Configure (unknown)
    void configure(double tag, std::uint32_t sprite, float start, float fade, float depth);
    /// Message `0x00`: the tagger is stored, then as show().
    void setTagger(double tag, double human);
    /// Message `0x12` (shown): path flag 8 set; when the next spray has something to do (paint in below 1, wipe out
    /// above 0) and there is a tagger, he is told to spray (`0x17`) and the fade starts; otherwise the tagger, if any,
    /// is told he is done (`0x13`).
    void show(double tag);
    /// Message `0x13` (hidden): path flag 8 cleared, the tagger (if any) told `0x13`, the fade stopped.
    void hide(double tag);
    /// Message `0x15`: the spot is removed.
    void remove(double tag);
    /// Message `0x19`: state 3 stops the fade and tells the tagger `0x13`; 4 makes it blank with paint-in next; 5 and
    /// 7 start the fade out and in; 6 makes it fully painted.
    /// @orig 0x003fc8d8 SprayTag_HandleMessage (unknown)
    void setState(double tag, int state);
    /// Messages `0x39` / `0x3a`: the next spray paints in (true) or wipes out; the look is unchanged.
    void setSprayMode(double tag, bool paintIn);
    /// Message `0x41`: the painted fraction.
    void setFraction(double tag, float fraction);

    /// The tag's side of a spray's end (`Tag_End`, `0x0022e848`): finished, the spot is marked sprayed (message
    /// `0x41` 1.0, then `0x19` state 3); either way it is then told `0x13` (hidden).
    /// @orig 0x0022e848 Tag_End (unknown)
    void endSpray(double tag, bool finished);
    /// A tagger's spray step on `tag` for a caller that runs no stick game (Rumble's CPU taggers): `human` becomes the
    /// tagger, the painted fraction becomes `fraction`, and with `finished` the spray ends as endSpray() ends it.
    /// Returns whether the spot is now finished (fully painted and its spray ended).
    bool spray(double tag, double human, float fraction, bool finished);

    /// The spots' update (every second frame): a fading spot moves its fraction by its step, sending itself state 3
    /// on reaching 1 (in) or 0 (out).
    /// @orig 0x003fca68 SprayTag_Update (unknown)
    void update();

    /// The spot `tag`, or null.
    [[nodiscard]] const TagSpot* find(double tag) const;
    /// Every spot.
    [[nodiscard]] const std::vector<TagSpot>& all() const { return m_spots; }
    /// Forgets every spot: the level is unloaded.
    void clear() { m_spots.clear(); }

  private:
    // The spot `tag`, made with its defaults when it has none.
    TagSpot& spot(double tag);
    // Tells the spot's tagger, if any, `message`.
    void tell(const TagSpot& spot, int message);

    std::vector<TagSpot> m_spots;
    TaggerMessage m_send;
    PathFlag m_flag;
};

} // namespace coney::world_objects
