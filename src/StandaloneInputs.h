// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Parameters.h"

#include <felitronics/appkit/InputPick.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cstdint>

/** THE INPUTS BY NUMBER, in the standalone alone (the owner, 15.11.2026: his DI in IN 4, the cabinet's mic in IN 3). The
    app opens every input of the interface, meters them all, and the amp hears the ones the player picks from a list,
    IN 1 to IN n, without repatching and without ticking inputs in Audio/MIDI Settings: felitronics-appkit's InputPick
    and InputList, handed every open input by JUCE's standalone (cmake/patches/juce-standalone-inputs.patch). MONO,
    STEREO SPACE and every bass play one input or two summed at half each; STEREO plays a pair, each side through its
    own amp. The choice is a workplace setting, kept for each interface by its name in the standalone's own settings,
    the one for STEREO apart; never in a preset or a session. Inside a host none of this exists, and the menus keep
    Left, Right and L+R (params::InputSource), which the host's routing feeds. */
namespace orbitamp
{

class AmpProcessor;

namespace standaloneInputs
{

using felitronics::appkit::InputChoice;

/** Starts it for this run, once: the picker set in JUCE's holder, and an interface seen for the first time opened on
    every input. Nothing inside a host, and nothing a second time. */
void start();

/** True in the standalone once started, with an interface open: the menus list its inputs by number. */
bool listed();

/** Where the choice stands, as a title says it: "IN 4", "IN 3 + IN 4", "IN 1, IN 2"; empty where not listed(). */
juce::String state();

/** The list into `menu` — the interface's inputs, each with its meter, the ones heard lit — then a line on how to click,
    in capitals where the menu is in them. A click picks an input; with Shift it adds a second (a sum; in STEREO, the
    right side). The menu stays open while the strings are plucked. False, and nothing added, where not listed(). */
bool addTo (juce::PopupMenu& menu, bool capitals);

// ----------------------------------------------------------------------------------------------------------------
// The rules, apart from JUCE's standalone so the gates can hold them.

/** The first choices for an interface not seen before, so it plays what it played: the inputs open (`open`, input k's bit)
    as JUCE handed them, the first open one the amp's left and the second its right, read through the session's
    Left / Right / L+R — and in STEREO the first two as a pair. */
struct Seed { InputChoice mono, stereo; };

inline Seed seed (std::uint64_t open, params::InputSource source)
{
    int first = -1, second = -1;
    for (int in = 0; in < InputChoice::maxInputs && second < 0; ++in)
        if (((open >> in) & 1u) != 0)
            (first < 0 ? first : second) = in;
    if (first < 0)
        return { InputChoice::single (0), InputChoice::paired (0, 1) };
    if (second < 0)
        return { InputChoice::single (first), InputChoice::paired (first, first) };

    const auto mono = source == params::InputSource::right ? InputChoice::single (second)
                    : source == params::InputSource::both  ? InputChoice::summed (first, second)
                                                           : InputChoice::single (first);
    return { mono, InputChoice::paired (first, second) };
}

/** What a click on input `in` makes of `now`: alone, or with Shift beside the one playing — two summed, the lower first,
    or alone again where it is that one; in STEREO a click sets the left side and Shift the right. */
inline InputChoice clicked (InputChoice now, int in, bool shift, bool stereo)
{
    if (stereo)
        return shift ? InputChoice::paired (now.a, in) : InputChoice::paired (in, now.kind == InputChoice::Kind::one ? now.a : now.b);
    if (! shift || in == now.a)
        return InputChoice::single (in);
    return InputChoice::summed (std::min (now.a, in), std::max (now.a, in));
}

/** The words for a choice: "IN 4", "IN 3 + IN 4", "IN 1, IN 2". */
inline juce::String words (InputChoice c)
{
    const auto in = [] (int i) { return "IN " + juce::String (i + 1); };
    switch (c.kind)
    {
        case InputChoice::Kind::sum:  return in (c.a) + " + " + in (c.b);
        case InputChoice::Kind::pair: return in (c.a) + ", " + in (c.b);
        case InputChoice::Kind::one:  break;
    }
    return in (c.a);
}

} // namespace standaloneInputs
} // namespace orbitamp
