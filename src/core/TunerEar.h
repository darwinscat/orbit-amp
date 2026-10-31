// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PitchTracker.h"
#include "TunerTap.h"

#include <algorithm>
#include <array>
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

    One thing more it tells the needle's face: when what it hears is the MAINS — a cable humming at
    50 or 60 Hz, read with full confidence as G1 +35 or B1 -49 — it says so (humMains), and the face
    writes HUM 50 / HUM 60 instead of a note's name. A label only: the reading is the same either way.

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
        levelCount = 0;
        plucked    = false;
        mains      = 0;
    }

    double preparedRate() const noexcept { return sr; }

    /** The player's level floor, handed on to the tracker — see PitchTracker::setLevelFloorDb.
        Cheap: call it every tick. */
    void setLevelFloorDb (double db) { tracker.setLevelFloorDb (db); }

    /** The pitch A4 is tuned to, in Hz: what every note is named and measured from. Cheap: call it
        every tick — the needle eases to the new reading like to any other. */
    void  setReferenceHz (float hz) noexcept { a4 = hz > 0.0f ? hz : PitchTracker::concertA; }
    float referenceHz() const noexcept       { return a4; }

    /** One tick: snapshot the tap, analyse, fold into the display state. `nowMs` is any steady
        millisecond clock — the ear only ever compares differences. */
    void update (const TunerTap& tap, unsigned nowMs)
    {
        PitchTracker::Reading reading;
        double level = -1.0;
        if (sr > 0.0 && tap.read (snap))
        {
            reading = tracker.analyse (snap.data(), (int) snap.size());
            level   = tracker.windowRms();
        }

        // A NEW NOTE starts from itself. A reading after a gap — idle, or no confident window for
        // longer than a pick attack takes — is a string plucked again, and usually a string just
        // turned: the median still held the last note's readings and the needle still stood where
        // that note's tail left it, so a string tuned up from flat came back reading flat for the
        // first ticks of the note that was already right. The history goes, the needle lands.
        bool freshNote = false;

        if (reading.hz > 0.0f)
        {
            freshNote = recent.empty() || nowMs - lastValidMs > (unsigned) newNoteGapMs;
            if (freshNote)
                recent.clear();

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

            const float target = nearestNote().cents;
            needleSmoothed = freshNote ? target : needleSmoothed + needleEase * (target - needleSmoothed);
        }

        listenForHum (level, nowMs);
    }

    bool  live() const noexcept   { return displayHz > 0.0f; }
    float hz() const noexcept     { return displayHz; }
    float needle() const noexcept { return needleSmoothed; }   // smoothed cents, for drawing
    bool  green() const noexcept  { return live() && std::abs (needleSmoothed) <= inTuneCents; }

    PitchTracker::Note nearestNote() const
    {
        return live() ? PitchTracker::nearestNote (displayHz, a4) : PitchTracker::Note {};
    }

    /** What the reading is taken for: 50 or 60 — the MAINS, whichever of its lines the reading sits
        on — when it is the hum of the mains, 0 when it is a note (or nothing is heard). A label, never
        a filter: hz(), needle() and nearestNote() read exactly as they would without it; a face writes
        HUM 50 / HUM 60 where the note's name stands and dims the needle. See listenForHum. */
    int humMains() const noexcept { return mains; }

    /** The lines a mains hum reads on: 50 and 60 Hz and their 2nd to 4th harmonics, the centre of each
        within this many cents. 0 when `hz` is on none of them, else the mains it belongs to. */
    static constexpr float lineCents = 10.0f;

    static int mainsLineOf (float hz) noexcept
    {
        if (hz <= 0.0f)
            return 0;
        for (const int m : { 50, 60 })
            for (int k = 1; k <= 4; ++k)
                if (std::abs (1200.0f * std::log2 (hz / (float) (m * k))) <= lineCents)
                    return m;
        return 0;
    }

private:
    /** HUM, OR A NOTE. The reading is labelled hum when it sits on a mains line (mainsLineOf) and
        nothing has been PLUCKED. A pluck is the window's level rising 6 dB or more within half a
        second, to above anything it reached in the two seconds before (a string beating against the
        hum rises as far, but only back to where it was). What a pluck started stops being plucked
        once its level has HELD — its last half second no more than 1.5 dB under the half second
        that began two seconds before — or, when it was plucked over a reading labelled hum, once
        its level is back within 3 dB of the hum it rose from: the string has died into the hum.
        So hum that was there before the tuner looked reads HUM at once; a string reads its note
        while it decays; hum that came in with a thump reads as a note until it has held for two
        seconds, and so does a string that does not decay at all (a sustainer, an e-bow). Nothing
        is hidden: a mistake only puts the label on a reading that is still shown. A gap in the
        ticks — the tuner shut and opened again — starts over, as a fresh open does. */
    void listenForHum (double level, unsigned nowMs)
    {
        if (level >= 0.0)
        {
            if (levelCount > 0 && nowMs - levels[(size_t) newest].ms > (unsigned) humGapMs)
            {
                levelCount = 0;
                plucked    = false;
            }

            const float db = (float) (20.0 * std::log10 (std::max (level, 1.0e-8)));

            float quietest = db, loudest = noHum;
            forEachLevel ([&] (const LevelAt& l)
            {
                const unsigned age = nowMs - l.ms;
                if (age < (unsigned) onsetMs)       quietest = std::min (quietest, l.db);
                else if (age < (unsigned) heldMs)   loudest  = std::max (loudest, l.db);
            });
            if (db - quietest >= onsetDb && db > loudest)
            {
                if (! plucked)
                {
                    overHumDb = mains != 0 ? quietest : noHum;   // a string plucked again keeps what the first rose from
                    plucked   = true;
                }
                onsetAt = nowMs;
            }

            newest = (newest + 1) % (int) levels.size();
            levels[(size_t) newest] = { nowMs, db };
            levelCount = std::min (levelCount + 1, (int) levels.size());

            if (plucked && db <= overHumDb + backDb)
                plucked = false;

            if (plucked && nowMs - onsetAt >= (unsigned) heldMs)
            {
                double lateSum = 0.0, earlySum = 0.0;
                int    late = 0, early = 0;
                forEachLevel ([&] (const LevelAt& l)
                {
                    const unsigned age = nowMs - l.ms;
                    if (age < (unsigned) heldPartMs)                                             { lateSum  += l.db; ++late; }
                    else if (age >= (unsigned) (heldMs - heldPartMs) && age < (unsigned) heldMs) { earlySum += l.db; ++early; }
                });
                if (late > 0 && early > 0 && lateSum / late >= earlySum / early - heldDb)
                    plucked = false;
            }
        }

        mains = plucked ? 0 : mainsLineOf (displayHz);
    }

    struct LevelAt { unsigned ms = 0; float db = 0.0f; };

    template <typename Fn>
    void forEachLevel (Fn&& fn) const
    {
        for (int i = 0; i < levelCount; ++i)
            fn (levels[(size_t) ((newest - i + (int) levels.size()) % (int) levels.size())]);
    }

    static constexpr float onsetDb    = 6.0f;    // a pluck: the level rising this much...
    static constexpr int   onsetMs    = 500;     // ...within this
    static constexpr int   heldMs     = 2000;    // held: the last part against the part this long ago...
    static constexpr int   heldPartMs = 500;     // ...each part this long...
    static constexpr float heldDb     = 1.5f;    // ...fallen less than this
    static constexpr float backDb     = 3.0f;    // a string plucked over hum: back within this of it, gone
    static constexpr float noHum      = -1000.0f;
    static constexpr int   humGapMs   = 250;     // no tick for longer: the history is not about now

    static constexpr int holdMs      = 700;   // how long a note outlives its last confident reading
    static constexpr int medianDepth = 5;

    /** The needle's share of the way to its target per tick — the needle's weight. 0.3 at the 30 Hz
        tick is a time constant of about 95 ms: steady enough to read between two phrases, quick
        enough to follow a peg. (It was 0.45, ~55 ms, and read nervous.) */
    static constexpr float needleEase = 0.30f;

    /** No confident reading for this long, then one: a new note — see `update`. Three ticks: longer
        than a string that is ringing ever goes without one, shorter than a pick attack. */
    static constexpr int newNoteGapMs = 100;

    PitchTracker tracker;
    double sr = 0.0;

    std::array<float, TunerTap::size> snap {};

    std::vector<float> recent;
    unsigned lastValidMs = 0;
    float a4             = PitchTracker::concertA;
    float displayHz      = 0.0f;   // 0 = idle
    float needleSmoothed = 0.0f;

    std::array<LevelAt, 128> levels {};   // the window's level per tick, newest at `newest` — 4 s at 30 Hz
    int      levelCount = 0, newest = 0;
    bool     plucked    = false;
    unsigned onsetAt    = 0;
    float    overHumDb  = noHum;   // the level of the hum a pluck rose from; noHum when it rose from none
    int      mains      = 0;
};

} // namespace orbitamp::core
