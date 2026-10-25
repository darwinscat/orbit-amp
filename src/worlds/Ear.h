// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <algorithm>
#include <cmath>

namespace orbitamp::worlds
{

/** THE PICK, AS AN EAR HEARS IT: what a world's Play needs to know of the guitar, taken from the sound itself, a
    sample at a time — the amp's input after IN, the app's on its audio thread, the site's in its WebAssembly.

      peak   the largest sample since the last take, as the IN meter reads it.
      pick   how far the sound's top, above clickHz, jumped over what it had been: its energy over the last
             few milliseconds (fastSeconds) against the last tenth of a second and a half (slowSeconds), the
             largest since the last take, as a power ratio — 1 for a sound that only goes on, ten and more for
             a string struck. The click of a pick is all top; the string ringing after it is not, so a note
             held, however loud, makes none, and a riff makes one at every stroke however dense it is.

    Fed contiguous blocks — a gap or an overlap is a jump of its own. No JUCE, no allocation: the audio thread's. */
class Ear
{
public:
    static constexpr double clickHz = 3000.0, fastSeconds = 0.005, slowSeconds = 0.150;

    struct Heard
    {
        float peak = 0.0f;       // linear, 1 = full scale
        float pick = 1.0f;       // power ratio, 1 = no jump
    };

    void prepare (double sampleRate) noexcept
    {
        rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        highPass = (float) std::exp (-2.0 * 3.14159265358979323846 * clickHz / rate);
        fast = (float) (1.0 - std::exp (-1.0 / (fastSeconds * rate)));
        slow = (float) (1.0 - std::exp (-1.0 / (slowSeconds * rate)));
        reset();
    }

    double sampleRate() const noexcept { return rate; }

    void listen (const float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float y = highPass * (lastOut + x[i] - lastIn);    // one pole, above the click's corner
            lastIn = x[i]; lastOut = y;
            const float e = y * y;
            energyFast += (e - energyFast) * fast;
            energySlow += (e - energySlow) * slow;
            pickSince = std::max (pickSince, energyFast / std::max (energySlow, 1.0e-12f));
            peakSince = std::max (peakSince, std::abs (x[i]));
        }
        heardAny = heardAny || n > 0;
    }

    /** What was heard since the last take; and then nothing, until more is. Nothing heard: no peak, no jump. */
    Heard take() noexcept
    {
        const Heard h { peakSince, heardAny ? pickSince : 1.0f };
        peakSince = 0.0f; pickSince = 1.0f; heardAny = false;
        return h;
    }

    void reset() noexcept
    {
        lastIn = lastOut = energyFast = energySlow = 0.0f;
        peakSince = 0.0f; pickSince = 1.0f; heardAny = false;
    }

private:
    double rate = 48000.0;
    float highPass = 0.0f, fast = 0.0f, slow = 0.0f;
    float lastIn = 0.0f, lastOut = 0.0f, energyFast = 0.0f, energySlow = 0.0f;
    float peakSince = 0.0f, pickSince = 1.0f;
    bool heardAny = false;
};

} // namespace orbitamp::worlds
