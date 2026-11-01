// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

/** ON FIRST, RULED OFF (the owner, 01.11: in every menu): a menu opened from a block opens with the block's STANDBY/ON —
    the switch its strip arrow writes, ticked while it is on — and a rule, then what it held. A pick turns the switch over
    in one gesture, as the arrow does; on a guitar face a block switched off leaves the face, as it does by its arrow. */
namespace orbitamp::onfirst
{

/** Whose switch a menu's ON is: a block's frame (BlockFrame). */
struct Owner
{
    virtual ~Owner() = default;
    virtual juce::RangedAudioParameter* blockSwitch() const noexcept = 0;
};

inline constexpr int item = 0x3f000001;   // ON's id, clear of every menu's own

/** A gate's hook: set, a menu about to be shown comes here instead — true: taken, not shown. */
using Catcher = std::function<bool (const juce::PopupMenu&, const std::function<void (int)>&)>;
inline Catcher& catcher()
{
    static Catcher c;
    return c;
}

/** The switch of the block `from` stands in, or null outside one. */
inline juce::RangedAudioParameter* switchOf (juce::Component* from)
{
    for (auto* c = from; c != nullptr; c = c->getParentComponent())
        if (auto* owner = dynamic_cast<Owner*> (c))
            return owner->blockSwitch();
    return nullptr;
}

/** `m` with ON first and a rule, ticked while `on` is, then every item it held. */
inline juce::PopupMenu withOn (const juce::PopupMenu& m, juce::RangedAudioParameter& on)
{
    juce::PopupMenu out;
    out.addItem (item, "ON", true, on.getValue() > 0.5f);
    out.addSeparator();
    for (juce::PopupMenu::MenuItemIterator it (m); it.next();)
        out.addItem (it.getItem());
    return out;
}

/** Shows `m`, opened from `from`: inside a block with ON first (withOn); ON's pick is the switch's, every other `done`'s. */
inline void show (const juce::PopupMenu& m, juce::Component* from, const juce::PopupMenu::Options& options, std::function<void (int)> done)
{
    auto* on = switchOf (from);
    auto menu = on != nullptr ? withOn (m, *on) : m;
    std::function<void (int)> answer = [on, done = std::move (done)] (int r)
    {
        if (on != nullptr && r == item)
        {
            on->beginChangeGesture();
            on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
            on->endChangeGesture();
            return;
        }
        if (done != nullptr)
            done (r);
    };
    if (auto& c = catcher(); c != nullptr && c (menu, answer))
        return;
    menu.showMenuAsync (options, answer);
}

} // namespace orbitamp::onfirst
