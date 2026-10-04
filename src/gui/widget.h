// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>

#include "core/pad.h"
#include "graphics/sprite_batch.h"
#include "gui/text_layout.h"

namespace coney::gui {

/// What a widget's update is given each frame: the game time and the pad of the player the widget belongs to.
struct GuiFrame {
    std::uint64_t timeMs = 0; ///< Game time in milliseconds; never goes down.
    const Pad* pad = nullptr; ///< The player's pad record; null when the widget has no player.
};

/// Where a widget's render puts its sprites: the fonts by slot and the sprite batch of each font slot. A sprite widget
/// has its own batch (BaseWidget).
struct GuiCanvas {
    FontLookup fonts;                                          ///< The font in each slot; null for none.
    std::function<graphics::SpriteBatch*(int slot)> textBatch; ///< The batch of each font slot; null for none.
};

/// The base of every screen element: created, initialised once (`Init` makes its children and takes its resources),
/// updated (input and animation) and rendered (sprites added to batches) every frame, and shut down.
///
/// The original's base (`0x001a8e30`) has many more virtual slots (gui.md's widget vtable): setup with a rectangle,
/// the rectangle, focus, text reveal; Coney's widgets take what they need through their own setters.
///
/// Research: docs/research/gui.md#widgets
/// @orig 0x001a8e30 Widget::Widget (unknown)
class Widget {
  public:
    virtual ~Widget() = default;
    Widget() = default;
    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    Widget(Widget&&) = delete;
    Widget& operator=(Widget&&) = delete;

    /// Runs onInit() the first time it is called after construction or shutdown(), and nothing otherwise (the
    /// original marks it done at `+0x0c`).
    void init();
    /// Releases what onInit() made; the next init() runs onInit() again.
    void shutdown();
    /// Whether init() has run since construction or the last shutdown().
    [[nodiscard]] bool initialised() const { return m_initialised; }

    /// One frame of input and animation.
    virtual void update(const GuiFrame& frame) { (void)frame; }
    /// Adds the widget's sprites for this frame. Does nothing while the widget is hidden.
    virtual void render(const GuiCanvas& canvas) const { (void)canvas; }

    /// Whether render() draws anything (vtable slot `+0x48`).
    [[nodiscard]] bool visible() const { return m_visible; }
    /// Shows or hides the widget.
    void setVisible(bool visible) { m_visible = visible; }

  protected:
    /// Makes the widget's children and takes its resources; init() calls it once.
    virtual void onInit() {}
    /// Releases them; shutdown() calls it when the widget is initialised.
    virtual void onShutdown() {}

  private:
    bool m_initialised = false;
    bool m_visible = true;
};

} // namespace coney::gui
