// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::bass
{

/** Who opens a block's menu: the face (FaceplateView), at the block. A block's own controls find it among their parents. */
struct MenuOpener
{
    virtual ~MenuOpener() = default;
    virtual void openMenu (const juce::String& block) = 0;
};

/** Opens `block`'s menu from a control standing on the face; false when there is no face to ask, and the caller shows
    the menu itself. */
inline bool openMenuFrom (juce::Component* from, const juce::String& block)
{
    if (from == nullptr)
        return false;
    auto* opener = dynamic_cast<MenuOpener*> (from);
    if (opener == nullptr)
        opener = from->findParentComponentOfClass<MenuOpener>();
    if (opener == nullptr)
        return false;
    opener->openMenu (block);
    return true;
}

} // namespace orbitamp::bass
