// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "PolyTuner.h"
#include "TunerEar.h"
#include "TunerTap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace orbitamp::core
{

/** THE TUNINGS A PRODUCT OFFERS its polyphonic tuner — a guitar amp the guitar's, a bass amp the bass's —
    as a run of PolyTuner::tunings, which lists the guitar's first and the bass's after them. */
inline std::span<const PolyTuner::Tuning> polyTunings (bool bass) noexcept
{
    constexpr std::size_t guitars = 4;
    return bass ? std::span<const PolyTuner::Tuning> (PolyTuner::tunings).subspan (guitars)
                : std::span<const PolyTuner::Tuning> (PolyTuner::tunings).first (guitars);
}

/** The tuning of `id` among the product's own — or, where it has none of that name (none chosen yet, or one
    of the other instrument's), its first: the standard one. */
inline const PolyTuner::Tuning& polyTuning (const char* id, bool bass) noexcept
{
    const auto own = polyTunings (bass);
    for (const auto& t : own)
        if (id != nullptr && std::strcmp (t.id, id) == 0)
            return t;
    return own.front();
}

/** What a tuning is called where the player picks it — the instrument is the product's, so not that:
    `menu` in a menu, `pill` in SETUP's row of choices, where a cell is a word wide. */
struct PolyTuningName
{
    const char* menu;
    const char* pill;
};

inline PolyTuningName polyTuningName (const PolyTuner::Tuning& t) noexcept
{
    static constexpr struct { const char* id; PolyTuningName name; } names[] {
        { "guitar",            { "Standard",        "STANDARD"   } },
        { "guitar-drop-d",     { "Drop D",          "DROP D"     } },
        { "guitar-half-down",  { "Half step down",  "HALF DOWN"  } },
        { "guitar-whole-down", { "Whole step down", "WHOLE DOWN" } },
        { "bass-4",            { "4 strings",       "4 STR"      } },
        { "bass-5",            { "5 strings",       "5 STR"      } },
        { "bass-6",            { "6 strings",       "6 STR"      } },
        { "bass-drop-d",       { "Drop D",          "DROP D"     } },
        { "bass-half-down",    { "Half step down",  "HALF DOWN"  } },
    };
    for (const auto& n : names)
        if (std::strcmp (n.id, t.id) == 0)
            return n.name;
    return { t.name, t.name };
}

//==============================================================================
/** ONE STRING'S LANE on a polyphonic face: what every face draws, string by string, from the analysis's
    reading — the one mapping, so no two faces can tell a strum two ways.

    A lane is its string's TARGET (the note it is tuned to) and one of four marks: nothing heard of it (a
    dash: not played, not yet read, or the tuner off); MASKED — what it rang is only other strings'
    partials, muted or in unison with one, so the face asks for it alone; NEAR — read, `cents` from its
    note, held to the ±50 the lanes show; FAR — read, but further than a semitone off. Near and within the
    needle's own in-tune band, it is `green`. */
struct PolyLane
{
    enum class Mark : std::uint8_t { none, masked, near, far };

    int   midi  = -1;      // the target, MIDI (69 = A4)
    Mark  mark  = Mark::none;
    float cents = 0.0f;    // near: the deviation, within ±showCents
    bool  green = false;   // near and in tune
    bool  sharp = false;   // near or far: above the target

    static constexpr float showCents = 50.0f;
};

struct PolyLanes
{
    int count = 0;   // the tuning's strings, lowest first
    std::array<PolyLane, PolyTuner::maxStrings> lane {};
};

/** The lanes of `tuning` as `reading` has its strings; `listening` false — the tuner off — reads nothing
    heard, whatever the analysis last kept. Green within `inTuneCents`, the needle's band. */
inline PolyLanes polyLanes (const PolyTuner::Reading& reading, const PolyTuner::Tuning& tuning, bool listening,
                            float inTuneCents = TunerEar::inTuneCents) noexcept
{
    PolyLanes out;
    out.count = std::clamp (tuning.strings, 0, PolyTuner::maxStrings);
    for (int s = 0; s < out.count; ++s)
    {
        auto& lane = out.lane[(size_t) s];
        lane.midi  = tuning.midi[(size_t) s];
        if (! listening || s >= reading.strings)
            continue;

        const auto& str = reading.string[(size_t) s];
        switch (str.heard)
        {
            case PolyTuner::Heard::no:     break;
            case PolyTuner::Heard::masked: lane.mark = PolyLane::Mark::masked; break;
            case PolyTuner::Heard::near:
                lane.mark  = PolyLane::Mark::near;
                lane.cents = std::clamp (str.cents, -PolyLane::showCents, PolyLane::showCents);
                lane.green = std::abs (str.cents) <= inTuneCents;
                lane.sharp = str.cents > 0.0f;
                break;
            case PolyTuner::Heard::far:
                lane.mark  = PolyLane::Mark::far;
                lane.sharp = str.cents > 0.0f;
                break;
        }
    }
    return out;
}

//==============================================================================
/** THE POLYPHONIC TUNER AS THE FACES SEE IT: the analysis (PolyTuner), run on the processor's tuner pump
    only while the player reads by POLY and the tuner is open, and what the faces draw from it.

    The pump calls tune() every tick — the tuning and the A, so a face shows the right strings even while
    the tuner is off — then update() while it runs, or stop() when it does not. Nothing here allocates but
    the analysis's prepare(), the first tick at a sample rate. Message thread, as the pump and the faces. */
class TunerPoly
{
public:
    /** Whether the faces draw lanes: the player reads by POLY. Set by update(), and kept by stop (true)
        while the tuner is off or closed, so the face still shows what it is. */
    bool on() const noexcept { return shown; }

    /** Whether the analysis ran on the last tick. */
    bool running() const noexcept { return run; }

    /** The tuning the lanes show and the A its targets are counted from. Retunes the analysis only when
        either changes: retuning clears its reading. */
    void tune (const PolyTuner::Tuning& tuning, float a4)
    {
        if (tunedId != nullptr && std::strcmp (tunedId, tuning.id) == 0 && same (a4, tunedA4))
            return;
        tunedId = tuning.id;
        tunedA4 = a4;
        ear.setTuning (tuning);
        ear.setReferenceHz (a4);
    }

    /** One tick while POLY is chosen and the tuner is open: take what the tap got since the last one.
        Prepares the analysis — its only allocation — at a new rate. */
    void update (const TunerTap& tap, double sampleRate, double floorDb)
    {
        shown = true;
        if (sampleRate <= 0.0)
            return;
        if (! same (sampleRate, rate))
        {
            ear.prepare (sampleRate);
            rate = sampleRate;
        }
        if (! floorSet || ! same (floorDb, floor))
        {
            ear.setLevelFloorDb (floorDb);
            floor = floorDb;
            floorSet = true;
        }
        ear.update (tap);
        run = true;
    }

    /** Not run this tick — another display chosen, or the tuner off or closed. The next run starts afresh,
        from what the tap still holds. `stillShown`: POLY is the display all the same. */
    void stop (bool stillShown = false) noexcept
    {
        shown = stillShown;
        if (run)
        {
            ear.reset();
            run = false;
        }
    }

    const PolyTuner::Reading& reading() const noexcept { return ear.reading(); }
    const PolyTuner::Tuning&  tuning() const noexcept  { return ear.currentTuning(); }
    float referenceHz() const noexcept { return ear.referenceHz(); }

    /** What the faces draw: see polyLanes. */
    PolyLanes lanes (bool listening) const noexcept { return polyLanes (reading(), tuning(), listening); }

private:
    static bool same (double a, double b) noexcept { return ! (a < b) && ! (b < a); }   // the very same setting

    PolyTuner ear;
    const char* tunedId = nullptr;
    float  tunedA4 = 0.0f;
    double rate = 0.0, floor = 0.0;
    bool   floorSet = false;
    bool   shown = false, run = false;
};

} // namespace orbitamp::core
