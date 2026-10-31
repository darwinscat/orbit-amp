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
        sound      = Sound::unknown;
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
            level   = newestRms();
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
        within this many cents. 0 when `hz` is on none of them, else the mains it belongs to.

        As narrow as hum allows. The tracker reads hum within 0.66 cents of its line — measured on a
        sine, a rectifier's buzz, hum led by its 2nd to 4th harmonics, and hum swinging +-4 dB at 1 and
        0.1 Hz and +-6 dB at 0.5 Hz; 44.1, 48 and 96 kHz; -30 to -58 dBFS over hiss at -90 and -70 —
        and a grid holds its 50 or 60 Hz within +-0.05 Hz, 1.7 cents. A string has to stand almost
        exactly on a line to be taken for it, and a peg turned through one is past it in a moment. */
    static constexpr float lineCents = 3.5f;

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
    /** HUM, OR A NOTE — what the sound has DONE decides, never the pitch alone. Its LEVEL is the RMS of
        the newest 100 ms of input (whole periods of 50 and 60 Hz, so hum reads flat), and the sound is
        UNKNOWN, a STRING or STEADY:
        - a PLUCK is the level standing 3 dB or more over where it stood 100 ms before, when it had not
          been rising in the 100 ms before that: a pick's attack is that sudden; hum swinging +-4 dB at
          1 Hz rises 2.5 dB in that time, and was rising already. Over hum (the level stood at the hum
          heard last within 0.4 s) a rise out of a dip more than 3 dB under the hum is no pluck: it is a
          dying string beating against the hum. A pluck makes the sound a STRING; another, louder or
          softer, renews it.
        - a STRING turns STEADY once, 2.5 s after its last pluck, its level has stopped falling (a slope
          fitted over those 2.5 s above -0.25 dB/s) — or, plucked over hum, once it is back within 3 dB of
          the hum it rose from. A note held by a sustainer or flattened by a compressor ahead of the amp
          does stop falling, and turns STEADY; a string re-plucked before it has fallen 3 dB is not seen
          as plucked again.
        - with no history — the tuner just opened, or no tick for 1.5 s — the sound is UNKNOWN: a STRING
          if in its first 0.8 s the level falls 1 dB without rising, STEADY if not. A shorter stall keeps
          what the sound was.
        The label is HUM 50 / HUM 60 while the sound is STEADY and the reading stands on a mains line
        (mainsLineOf). Nothing is hidden: a mistake only puts the label on a reading that is still shown. */
    void listenForHum (double level, unsigned nowMs)
    {
        if (level >= 0.0)
        {
            if (levelCount > 0 && nowMs - levels[(size_t) newest].ms > (unsigned) forgetMs)
                levelCount = 0;
            if (levelCount == 0)
            {
                sound     = Sound::unknown;
                humDb     = noHum;
                firstAt   = nowMs;
                unknownLow = unknownHigh = noHum;
                unknownRose = false;
            }

            const float db = (float) (20.0 * std::log10 (std::max (level, 1.0e-8)));

            // Where the level stood one and two steps (100 ms) before.
            const LevelAt* before = nullptr;
            const LevelAt* prior  = nullptr;
            const auto nearest = [nowMs] (const LevelAt*& best, const LevelAt& l, int at)
            {
                const int off = std::abs ((int) (nowMs - l.ms) - at);
                if (off <= stepMs * 3 / 10 && (best == nullptr || off < std::abs ((int) (nowMs - best->ms) - at)))
                    best = &l;
            };
            forEachLevel ([&] (const LevelAt& l)
            {
                nearest (before, l, stepMs);
                nearest (prior, l, 2 * stepMs);
            });

            const bool humAround = humDb > noHum && nowMs - humSeenAt <= (unsigned) humAroundMs;
            const bool sudden    = before != nullptr && db - before->db >= pluckDb
                                   && (prior == nullptr || before->db - prior->db < risingDb);
            const bool outOfDip  = humAround && before != nullptr && before->db < humDb - dipDb;
            if (sudden && ! (sound == Sound::steady && outOfDip))
            {
                if (sound != Sound::string)
                    overHumDb = sound == Sound::steady && humAround ? before->db : noHum;   // a renewal keeps the first's
                sound   = Sound::string;
                onsetAt = nowMs;
            }

            if (sound == Sound::unknown)
            {
                unknownRose = unknownRose || (unknownLow > noHum && db > unknownLow + unknownDb);
                unknownLow  = unknownLow > noHum ? std::min (unknownLow, db) : db;
                unknownHigh = std::max (unknownHigh, db);
                if (nowMs - firstAt >= (unsigned) decideMs)
                {
                    sound   = ! unknownRose && unknownHigh - db >= unknownDb ? Sound::string : Sound::steady;
                    onsetAt = nowMs;
                }
            }
            else if (sound == Sound::string)
            {
                if (db < overHumDb + backDb)
                {
                    sound = Sound::steady;
                    humDb = overHumDb;   // the hum it died into is still there
                    humSeenAt = nowMs;
                }
                else if (nowMs - onsetAt >= (unsigned) (heldMs + settleMs) && slopeOver (heldMs, nowMs, db) > -heldSlopeDb)
                    sound = Sound::steady;
            }

            if (levelCount == 0 || nowMs - slotOpenedAt >= (unsigned) spacingMs)
            {
                newest = (newest + 1) % (int) levels.size();
                levelCount = std::min (levelCount + 1, (int) levels.size());
                slotOpenedAt = nowMs;
            }
            levels[(size_t) newest] = { nowMs, db };   // a faster pump: the newest slot follows it

            // The hum's level follows what reads HUM — but not down into a beat's dip.
            mains = sound == Sound::steady ? mainsLineOf (displayHz) : 0;
            if (mains != 0 && (! humAround || db >= humDb - dipDb))
                humDb = db;
            if (humDb > noHum && db >= humDb - dipDb)
                humSeenAt = nowMs;
        }
        else
        {
            mains = sound == Sound::steady ? mainsLineOf (displayHz) : 0;
        }
    }

    /** The RMS of the newest 100 ms of the snapshot, its mean taken out. */
    double newestRms() const
    {
        const int n = std::min ((int) snap.size(), std::max (1, (int) std::lround (sr * levelMs / 1000.0)));
        double sum = 0.0, sq = 0.0;
        for (int i = (int) snap.size() - n; i < (int) snap.size(); ++i)
        {
            sum += snap[(size_t) i];
            sq  += (double) snap[(size_t) i] * snap[(size_t) i];
        }
        const double mean = sum / n;
        return std::sqrt (std::max (0.0, sq / n - mean * mean));
    }

    /** The level's slope over the last `spanMs`, fitted, in dB per second — `db` as it is now included. */
    double slopeOver (int spanMs, unsigned nowMs, float db) const
    {
        double st = 0.0, sl = 0.0, stt = 0.0, stl = 0.0;
        int    n  = 0;
        const auto add = [&] (double t, double l) { st += t; sl += l; stt += t * t; stl += t * l; ++n; };
        add (0.0, db);
        forEachLevel ([&] (const LevelAt& l)
        {
            if (nowMs - l.ms < (unsigned) spanMs && l.ms != nowMs)
                add (-(double) (nowMs - l.ms) / 1000.0, l.db);
        });
        const double den = n * stt - st * st;
        return n >= 8 && den > 0.0 ? (n * stl - st * sl) / den : -1.0e9;
    }

    struct LevelAt { unsigned ms = 0; float db = 0.0f; };
    enum class Sound { unknown, string, steady };

    template <typename Fn>
    void forEachLevel (Fn&& fn) const
    {
        for (int i = 0; i < levelCount; ++i)
            fn (levels[(size_t) ((newest - i + (int) levels.size()) % (int) levels.size())]);
    }

    static constexpr int   levelMs       = 100;     // the level: this much of the newest input
    static constexpr float pluckDb       = 3.0f;    // a pluck: the level this much over...
    static constexpr int   stepMs        = 100;     // ...where it stood this long before...
    static constexpr float risingDb      = 1.0f;    // ...when it was not already rising by this
    static constexpr float dipDb         = 3.0f;    // over hum: a rise from further under it is a beat...
    static constexpr int   humAroundMs   = 400;     // ...while the level stood at the hum this recently
    static constexpr int   heldMs        = 2500;    // a string held: no fall over this...
    static constexpr int   settleMs      = 150;     // ...counted from this after its pluck...
    static constexpr float heldSlopeDb   = 0.25f;   // ...steeper than this, dB per second
    static constexpr float backDb        = 3.0f;    // plucked over hum: back within this of it, gone
    static constexpr int   decideMs      = 800;     // UNKNOWN: this long to tell...
    static constexpr float unknownDb     = 1.0f;    // ...a fall of this, without a rise of this, from steady
    static constexpr int   forgetMs      = 1500;    // no tick for longer: the history is not about now
    static constexpr int   spacingMs     = 20;      // the history keeps a level no closer than this...
    static constexpr float noHum         = -1000.0f;

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

    std::array<LevelAt, 256> levels {};   // ...so this holds 5 s at any pump rate; newest at `newest`
    int      levelCount  = 0, newest = 0;
    Sound    sound       = Sound::unknown;
    unsigned onsetAt     = 0, firstAt = 0, slotOpenedAt = 0;
    float    overHumDb   = noHum;   // the level of the hum a pluck rose from; noHum when it rose from none
    float    humDb       = noHum;   // the level of the hum last heard as hum
    unsigned humSeenAt   = 0;       // ...and when the level last stood there
    float    unknownLow  = noHum, unknownHigh = noHum;
    bool     unknownRose = false;
    int      mains       = 0;
};

} // namespace orbitamp::core
