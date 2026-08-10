#pragma once

#include "PitchTracker.h"
#include "TunerTap.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace orbitamp::core
{

/** The tuner's listening state — what stands between a pitch tracker and a needle you can watch.

    The tracker answers per window; a needle needs manners: a median of the recent confident
    readings (one wrong window must not twitch it), a hold across short silence (a decaying note is
    still THE note), and smoothing towards the target (a needle, not a nervous one). This is that,
    and nothing else — no drawing, no clock of its own. Whoever owns it calls update() on a steady
    tick and passes the milliseconds; both the strip's miniature and the zoomed tuner then read the
    SAME ear, so the two needles can never disagree.

    Message-thread only, JUCE-free like the tracker — the whole ear is testable headless. */
class TunerEar
{
public:
    static constexpr float inTuneCents = 2.5f;   // within this the letter and needle go green

    void prepare (double sampleRate)
    {
        tracker.prepare (sampleRate);
        sr = sampleRate;
        recent.clear();
        recent.reserve ((size_t) medianDepth);
        displayHz   = 0.0f;
        needleSmoothed = 0.0f;
    }

    double preparedRate() const noexcept { return sr; }

    /** One tick: snapshot the tap, analyse, fold into the display state. `nowMs` is any steady
        millisecond clock — the ear only ever compares differences. */
    void update (const TunerTap& tap, unsigned nowMs)
    {
        PitchTracker::Reading reading;
        if (sr > 0.0 && tap.read (snap))
            reading = tracker.analyse (snap.data(), (int) snap.size());

        if (reading.hz > 0.0f)
        {
            if ((int) recent.size() >= medianDepth)
                recent.erase (recent.begin());
            recent.push_back (reading.hz);
            lastValidMs = nowMs;
        }
        else if (recent.empty() || nowMs - lastValidMs > (unsigned) holdMs)
        {
            recent.clear();
            displayHz = 0.0f;
        }

        if (! recent.empty())
        {
            auto sorted = recent;
            std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
            displayHz = sorted[sorted.size() / 2];

            needleSmoothed += 0.45f * (nearestNote().cents - needleSmoothed);
        }
    }

    bool  live() const noexcept   { return displayHz > 0.0f; }
    float hz() const noexcept     { return displayHz; }
    float needle() const noexcept { return needleSmoothed; }   // smoothed cents, for drawing
    bool  green() const noexcept  { return live() && std::abs (needleSmoothed) <= inTuneCents; }

    PitchTracker::Note nearestNote() const
    {
        return live() ? PitchTracker::nearestNote (displayHz) : PitchTracker::Note {};
    }

private:
    static constexpr int holdMs      = 700;   // how long a note outlives its last confident reading
    static constexpr int medianDepth = 5;

    PitchTracker tracker;
    double sr = 0.0;

    std::array<float, TunerTap::size> snap {};

    std::vector<float> recent;
    unsigned lastValidMs = 0;
    float displayHz      = 0.0f;   // 0 = idle
    float needleSmoothed = 0.0f;
};

} // namespace orbitamp::core
