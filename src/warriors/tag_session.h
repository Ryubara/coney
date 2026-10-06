// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "warriors/tag_game.h"

// A player's spray at one tag spot from start to end: the stick game (TagGame) on the spot's painted fraction, the
// paint spent from the player's inventory, the fraction sent to the spot as the game goes, and the spot's side of the
// end (Tag_End). The caller runs what is not the spot's or the paint's: the pad held, the speech, the bonus and the
// tagger's event 14. HuTag uses it for the story's tag spots, Rumble's Tag battle for the player's own.
// Research: docs/research/crimes.md#tagging

namespace coney {

class Inventory;

namespace world_objects {
class TagSpots;
} // namespace world_objects

/// One player's tagging of one spot (`Human_Tag` `0x00238db0` to `Tag_End` `0x0022e848`).
class TagSession {
  public:
    /// How the session ended, for the caller's side of the end.
    struct End {
        bool finished = false;     ///< The pattern was traced: the spot is marked sprayed.
        bool wastedCharge = false; ///< Unfinished with under 30 % of a charge left: one more charge was spent.
    };

    /// Player `player` (0 or 1, the human `human`) sprays `tag` along `path` with `tuning`, starting at the spot's
    /// painted fraction. The paint comes from `inventory`'s item 3; both must outlive the session. The spot gets no
    /// tagger (message `0x00` is an AI tagger's, whose arrival starts the spot's own fade in).
    /// @orig 0x00238db0 Human_Tag (unknown)
    TagSession(world_objects::TagSpots& spots, Inventory& inventory, int player, double human, double tag,
               std::vector<TagCell> path, TagTuning tuning);

    /// Whether player `player` has paint to start a tag (item 3); none: HuTag's refusal (speech 37 `nopaint`).
    [[nodiscard]] static bool hasPaint(const Inventory& inventory, int player);

    /// One update of the stick game with the left stick at (`stickX`, `stickY`) and `elapsedMs` gone: the spot's
    /// fraction follows the game (message `0x41`). Returns the game's state; when it leaves Playing the session has
    /// ended (end()).
    TagGame::Result update(float stickX, float stickY, std::uint32_t elapsedMs);
    /// The session ends unfinished at once (the player did something else, or the level ended).
    void abandon();

    /// The stick game.
    [[nodiscard]] const TagGame& game() const { return m_game; }
    /// The tagger's handle and the spot's.
    [[nodiscard]] double human() const { return m_human; }
    [[nodiscard]] double tag() const { return m_tag; }
    /// Whether the session has ended, and how.
    [[nodiscard]] bool ended() const { return m_ended; }
    [[nodiscard]] const End& end() const { return m_end; }

  private:
    // Spends one charge of paint; false when there is none.
    bool spendCharge();
    // The spot's side of the end and the wasted charge (Tag_End).
    void finish();

    world_objects::TagSpots* m_spots;
    Inventory* m_inventory;
    int m_player;
    double m_human;
    double m_tag;
    TagGame m_game;
    bool m_ended = false;
    End m_end;
};

} // namespace coney
