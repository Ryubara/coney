// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace coney::script {

class ScriptSystem;

/// The Lua message handlers `SetMsgHandler` gives game objects (humans, volume boxes, flags): a callback name per
/// object handle and message number, 26 slots each, and the delivery of a message to it by the arguments the
/// message's number takes (`0x00384ce0`).
///
/// Research: docs/research/scripting.md#message-handlers, docs/references/script-events.md
class MessageHandlers {
  public:
    /// The slots of an object's handler component (`+0x0c + 4 × message`, 26 of them).
    static constexpr int kMessages = 26;

    /// `SetMsgHandler(object, message, callback)`: keeps `callback` in the object's slot `message`; an empty one
    /// removes it. A message outside 0-25 is ignored (**Coney choice**: the original does not check).
    /// @orig 0x003860b8 MessageHandler_Set (unknown)
    void set(double object, int message, std::string callback);
    /// The callback of the object's slot `message`; empty for none.
    [[nodiscard]] std::string_view handler(double object, int message) const;
    /// `SetMsgHandlerEx(object, 0, callback, prompt)`: a world object's interaction prompt, its kind-1 context record
    /// (docs/research/crimes.md#context-records); an empty callback or prompt unregisters it.
    /// @orig 0x00391c98 WorldObject_RegisterContext (unknown)
    void setPrompt(double object, std::string_view callback, std::string_view prompt);
    /// The objects with a kind-1 context record, and their prompts.
    [[nodiscard]] const std::map<double, std::string>& prompts() const { return m_prompts; }
    /// Forgets every handler and prompt: a level's objects are gone.
    void clear() {
        m_handlers.clear();
        m_prompts.clear();
    }
    /// Handlers kept.
    [[nodiscard]] std::size_t size() const { return m_handlers.size(); }

    /// Delivers message `message` about `subject` to `object`'s handler in `scripts`, with `other` and `value` the
    /// record's other object and number, marshalled by the research page's table (8: `(flag, human)`, `object` the
    /// flag; a number the table lacks: `(object, other)`). Returns whether the message was taken, which only the
    /// numbers that ask for a result (0, 9, 0xc, 0xe, 0xf, 0x10) can be. Message 0 (the interaction) is taken when the
    /// callback returns a value other than nil. **Coney choice**: for the others a call that ran counts as taking it.
    /// @orig 0x00384c38 MessageHandler_Deliver (unknown)
    /// @orig 0x00384ce0 MessageHandler_Marshal (unknown)
    bool deliver(ScriptSystem& scripts, double object, int message, double subject, double other, double value) const;

  private:
    std::map<std::pair<double, int>, std::string> m_handlers;
    std::map<double, std::string> m_prompts; // the objects' kind-1 context records
};

} // namespace coney::script
