// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace orbitamp
{

/** TAPPING A TEMPO, counted the way the family's consoles count it (face::Console::tapTempo): the tempo
    is the mean of the last few gaps between taps, the first tap only starts the count, and a pause
    longer than the slowest beat worth counting starts it again. The numbers are the console's own, so a
    player tapping the cockpit's DELAY word and OrbitAmp's TAP lands on the same BPM.

    Arithmetic only — a time in, a tempo out. Who writes the tempo where, and what one count is to the
    undo history, is the owner's. (The console keeps its own copy of these few lines; the two can become
    one when this moves to the family's kit.) */
class TapCount
{
public:
    static constexpr double longestGapMs = 2000.0;   // slower than 30 BPM is a pause, not a beat
    static constexpr std::size_t gapsKept = 4;

    /** A tap at `nowMs` (any steady millisecond clock). The tempo the taps so far make, in BPM — or 0
        for a tap that only starts the count. */
    double tap (double nowMs)
    {
        double bpm = 0.0;

        if (counting && nowMs - lastTap <= longestGapMs)
        {
            gaps.push_back (nowMs - lastTap);
            if (gaps.size() > gapsKept)
                gaps.erase (gaps.begin());

            double mean = 0.0;
            for (const double gap : gaps)
                mean += gap / (double) gaps.size();

            bpm = mean > 0.0 ? 60000.0 / mean : 0.0;
        }
        else
            gaps.clear();

        counting = true;
        lastTap = nowMs;
        return bpm;
    }

    /** Whether a count is open — tapped, and not yet paused past the longest gap. */
    bool isCounting (double nowMs) const noexcept { return counting && nowMs - lastTap <= longestGapMs; }

    /** Whether a count WAS open and the pause has just closed it — true once, for the owner to close what
        it opened when the count began. */
    bool closedBy (double nowMs) noexcept
    {
        if (! counting || nowMs - lastTap <= longestGapMs)
            return false;

        counting = false;
        gaps.clear();
        return true;
    }

private:
    std::vector<double> gaps;   // the last few gaps between taps, ms
    double lastTap  = 0.0;
    bool   counting = false;
};

} // namespace orbitamp
