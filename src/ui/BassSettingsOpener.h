// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace orbitamp::bass
{

/** Who opens a block's settings: the face (FaceplateView). A block's own controls find it among their parents. */
struct SettingsOpener
{
    virtual ~SettingsOpener() = default;
    virtual void openSettings (const juce::String& block) = 0;
};

/** Opens `block`'s settings from a control standing on the face; false when there is no face that has them (a guitar
    amp's), and the caller shows its menu instead. */
inline bool openSettingsFrom (juce::Component* from, const juce::String& block)
{
    if (from == nullptr)
        return false;
    if (auto* opener = dynamic_cast<SettingsOpener*> (from))
    {
        opener->openSettings (block);
        return true;
    }
    if (auto* opener = from->findParentComponentOfClass<SettingsOpener>())
    {
        opener->openSettings (block);
        return true;
    }
    return false;
}

} // namespace orbitamp::bass
