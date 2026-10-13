// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "../Parameters.h"
#include "../core/EqLink.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** AN EQ LINK'S SETTINGS AS THE PARAMETERS HAVE THEM — for a face that wants to DRAW the curve the
    link is playing. It hands a display copy of `core::EqLink` these and asks it for magnitudes; the
    audio path builds its own from the same parameters, on its own thread.

    One place, because every face that draws the curve must read every band the same way: a console
    that drew three bands of a four-band link would be a picture of some other equaliser. */
inline core::EqLink::Settings eqSettingsFor (const juce::AudioProcessorValueTreeState& state, int link)
{
    // The PARAMETER, not the apvts atomic: listener lists notify newest-first, so a callback can run
    // before the atomic cache updates, and a curve read from the cache there is one click behind.
    // The parameter object itself is written before anyone is told.
    const auto raw = [&state] (const juce::String& id)
    {
        if (const auto* p = state.getParameter (id))
            return p->convertFrom0to1 (p->getValue());
        return 0.0f;
    };
    const auto slope = [] (float v)
    {
        return params::eqSlopeValues[juce::jlimit (0, params::eqSlopes.size() - 1, juce::roundToInt (v))];
    };

    core::EqLink::Settings s;
    s.hpfOn    = raw (params::eqHpfOn (link)) > 0.5f;
    s.hpfHz    = (double) raw (params::eqHpfHz (link));
    s.hpfSlope = slope (raw (params::eqHpfSlope (link)));
    s.loDb     = (double) raw (params::eqLoDb (link));
    s.loHz     = (double) raw (params::eqLoHz (link));
    s.b1Db     = (double) raw (params::eqBellDb (link, 0));
    s.b1Hz     = (double) raw (params::eqBellHz (link, 0));
    s.b1Q      = (double) raw (params::eqBellQ (link, 0));
    s.b2Db     = (double) raw (params::eqBellDb (link, 1));
    s.b2Hz     = (double) raw (params::eqBellHz (link, 1));
    s.b2Q      = (double) raw (params::eqBellQ (link, 1));
    s.b3On     = raw (params::eqB3On (link)) > 0.5f;
    s.b3Db     = (double) raw (params::eqBellDb (link, 2));
    s.b3Hz     = (double) raw (params::eqBellHz (link, 2));
    s.b3Q      = (double) raw (params::eqBellQ (link, 2));
    s.hiDb     = (double) raw (params::eqHiDb (link));
    s.hiHz     = (double) raw (params::eqHiHz (link));
    s.lpfOn    = raw (params::eqLpfOn (link)) > 0.5f;
    s.lpfHz    = (double) raw (params::eqLpfHz (link));
    s.lpfSlope = slope (raw (params::eqLpfSlope (link)));
    return s;
}

} // namespace orbitamp
