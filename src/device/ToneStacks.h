// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace orbitamp::device
{

/** The classic passive tone stacks, as CIRCUITS rather than as curves.

    A captured preamp arrives with its own tone stack baked in at whatever position it was set to,
    so the amp's own controls cannot be recovered. What CAN be offered is the network itself: the
    Fender-Marshall-Vox ladder, whose whole musical point is that its three controls are NOT
    independent. Turn the bass down and the mids come up; put the mid on zero and the famous scoop
    appears; the thing never sits flat at all, because a passive divider always costs level. That
    wrongness is what makes a tone stack sound like an amplifier, and no amount of parametric bands
    reproduces it — which is exactly why this exists beside our own EQ rather than instead of it.

    THE MODEL is the third-order analytical transfer function of the FMV network,

        H(s) = (b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3)

    with every coefficient a polynomial in the three wiper positions and the seven component values
    below (Yeh & Smith, "Discretization of the '59 Fender Bassman Tone Stack", DAFx-06). It is
    evaluated for magnitude on the message thread whenever a knob moves and handed to the same
    minimum-phase FIR design the measured packs use — the network is a passive RC ladder and is
    therefore minimum-phase itself, so that path reproduces its phase as well as its magnitude.

    THE TRAP, and it is worth the paragraph because it costs 5.4 dB to fall into: this is NOT the
    naive network where the mid pot is a rheostat. C3 hangs off the mid pot's WIPER and the whole
    R3 track stays in circuit, which is where the m(1-m)R3^2 terms come from. Transcribe it as a
    rheostat and the response is wrong by up to 5.4 dB — plausible-looking wrong, curve-shaped
    wrong, the kind that survives a listen. Check the implementation against the fixture before
    trusting it: the paper's coefficients are famous for arriving mistyped.

    NO LOOKUP TABLE. The data this was built from shipped an 11x11x11x31 grid per stack; the grid is
    a sampled shadow of the seven numbers below, and keeping it would mean eight megabytes, values
    quantised to eleven knob positions per axis, and nothing gained. The table survives as a TEST
    FIXTURE (`.private/fixtures/`) against which the analytical implementation is checked — the
    published coefficients are notoriously easy to transcribe wrong, and 1331 nodes per stack is a
    thorough way to catch it.

    TWO, NOT FOUR. The source carried four networks. Measured against each other across the whole
    grid they are two: the 56k-slope pair differs by 0.18 dB on average and the 33k-slope pair by
    0.06 dB — inaudible, and two names for one sound is the museum this product refuses to be. What
    actually separates them is the slope resistor: at noon 56k digs an 11.8 dB hole at 800 Hz where
    33k digs 7.8 at 630 — deeper, and lower down. So they are named for what they DO. The discarded pair's values are recorded below; they are
    seven numbers each and nothing is lost by not shipping them. */
struct ToneStack
{
    const char* name;

    double treblePotOhm;
    double bassPotOhm;
    double midPotOhm;
    double slopeOhm;      // the one that decides how deep the scoop goes

    double c1F;           // treble cap
    double c2F;
    double c3F;
};

inline constexpr std::array<ToneStack, 2> toneStacks {{
    { "Scooped", 250000.0, 1000000.0, 25000.0, 56000.0, 2.5e-10, 2e-08, 2e-08 },
    { "Forward", 250000.0, 1000000.0, 25000.0, 33000.0, 5e-10, 2.2e-08, 2.2e-08 },
}};

// Recorded rather than shipped — see the note above. Slope 56k puts the first in Scooped's family,
// 33k puts the second in Forward's; the only other difference is the treble cap.
//   early 56k/250p : R1 250k, R2 1M, R3 25k, R4 56k, C1 250p, C2 22n, C3 22n
//   hot 33k/470p   : R1 250k, R2 1M, R3 25k, R4 33k, C1 470p, C2 22n, C3 22n

/** The bass control's taper. The analytical circuit wants an ELECTRICAL wiper position; the knob on
    the face is a UI position, and the real pot is an audio taper. Mid and treble are linear.
    audio/log approximation; x^3.321928 so midpoint maps to 10% */
inline constexpr double bassTaperExp = 3.321928;

inline double bassWiper (double uiPosition) noexcept
{
    return std::pow (juce::jlimit (0.0, 1.0, uiPosition), bassTaperExp);
}

} // namespace orbitamp::device
