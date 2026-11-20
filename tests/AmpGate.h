// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHAT AN AMP'S GATES HOLD IT TO, AND WHICH AMP IT IS. The cockpit and the rovers are one engine under
    faces of their own (channels/Engine.h), and so their gates are one too: AmpSetTests.cpp for the set as
    it leaves the binary, AmpChannelTests.cpp for the channels with no window — each built once for every
    product, and run as it (CMakeLists.txt: amp_set_test, amp_console). What tells the products apart is
    said here, once, in the table below — apart from each product's lineup, so that a lineup that drifts
    is caught rather than read back: the captures its channels play and what each opens on, whether they
    go in pairs, a gear and its TURBO, the marks on its DRIVE's dial, on the bass's chain each gear's BLEND
    and the bass's console as the product has it, the variable that tells its channel gate the home is its own (tests/gates.txt), and the words its gate says what it found
    in. How many of the channels the build plays, GATE_CHANNELS, the build says. */

#include <juce_core/juce_core.h>

#include <cstdio>
#include <cstdlib>
#include <vector>

#ifndef GATE_CHANNELS
 #error "the build says how many channels the amp plays (CMakeLists.txt: CHANNELS)"
#endif

#ifndef ORBITAMP_PRODUCT_NAME
 #error "the build says which amp the gate runs as"
#endif

// ITS OWN IN EVERY FILE THAT INCLUDES IT: what is said here reads the build's GATE_CHANNELS and
// ORBITAMP_PRODUCT_NAME, which the gates binary (tests/Gates.cpp) says differently for every amp's gate in
// one program — so nothing here may be one definition the linker keeps for all of them.
namespace gate
{
namespace
{

/** EVERY CHECK SAYS WHAT IT FOUND, a line each — `ok` or `FAIL`, and what it holds the amp to — and the
    run ends on PASSED or FAILED: the lines a gate's log is read by. */
struct Checks
{
    int failed = 0;

    void operator() (bool ok, const juce::String& what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
        if (! ok)
            ++failed;
    }

    /** A check that does not apply to this amp, said so with why — never silently left out. */
    void skip (const juce::String& what, const juce::String& why)
    {
        std::printf ("skip  %s — %s\n", what.toRawUTF8(), why.toRawUTF8());
    }

    /** What main() returns: 0 when every check held. */
    int finish() const
    {
        std::printf ("%s\n", failed == 0 ? "PASSED" : "FAILED");
        return failed == 0 ? 0 : 1;
    }
};

/** One of a product's channels, as its gate expects to find it. */
struct Channel
{
    const char* capture;            // the `rig_id` its pack carries
    const char* held;               // the switch of its pack it holds, "control=value" — "" where none
    const char* reverb;             // what INIT opens it on: the room...
    float wet, echo;                // ...how much of it, and how much delay
    const char* opens = nullptr;    // where the product's gate says this channel's opening in a line of its own
    const char* cabinet = nullptr;  // where the product's cabinets are drawn (Lineup::drawnCabinets): the preset it opens on, by its stem

    /** ON THE BASS'S CHAIN, where its BLEND opens, the capture's share — 1, the capture alone, unless said — and whether
        the channel holds it there, a gear with no BLEND of its own (Lineup::Channel::blend, blendHeld). */
    float blend = 1.0f;
    bool blendHeld = false;
};

/** A mark on DRIVE's dial, where the packs are captures shot at several hours of the gear's own knob:
    DRIVE's travel, and the hour of the capture it stands on there. */
struct DialMark
{
    float travel;
    double hour;
    const char* said;
};

struct Product
{
    const char* name;               // ORBITAMP_PRODUCT_NAME
    const char* amp;                // what its gate calls it
    const char* ownsHome;           // the variable that tells its channel gate the home is its own to write in

    /** Its channels in the lineup's order — at least GATE_CHANNELS of them, and the build plays the
        first so many. */
    std::vector<Channel> channels;

    const char* gears;              // where the channels go in pairs, a gear and its TURBO, what its gate says of them; none where they do not
    std::vector<DialMark> dial;     // none where DRIVE plays one capture

    // What its gate says it found, where the products' words differ.
    const char* installed;
    const char* tones;
    const char* opening;

    /** ON THE BASS'S CHAIN, its console the bass's standard (src/Product.h, Chain::bass): its tone knobs the
        standard's three whatever the pack, and what a bass's gear keeps and the product holds (Lineup::kept,
        pinned) checked besides. */
    bool bass = false;

    /** Whether it has IRs of before to gather at its first start (gatherFormerIrs) — every amp that kept them in a
        folder of its own before the family's; not one made after. */
    bool gathers = true;

    /** ON THE BASS'S CHAIN, THE BASS'S CONSOLE AS THE PRODUCT HAS IT, beyond what every bass amp has — DRIVE, BLEND, the
        tones, COMP's threshold, OCTAVE and CHORUS, each gear's own (Lineup::kept): whether it has ENVELOPE, in the strip
        and on and its amount each gear's; MIDDLE's frequency, each gear's; DI/CAB, each gear's; and the room in its strip
        and on, or none in it (Lineup::pinned) — and what its gate says it found of them. */
    struct Console
    {
        bool envelope = true, middleFrequency = true, diCab = true, room = false;
        const char* opensOwn = nullptr;     // a gear never pulled since INIT opens on its own
        const char* keeps = nullptr;        // ...each gear comes back with what it was left with
        const char* holds = nullptr;        // ...INIT gives what the product holds
    } console {};
};

/** THE PRODUCTS, each as its gate knows it. */
inline const Product& product()
{
    // The toys' cabinets are the guitar's TOP 12, drawn (channels/TopTwelve.h, the owner, 08.11): every channel opens on
    // its first, the 4x12 SELFISH GENE, until one is said another.
    static constexpr const char* topFirst = "top12-01-4x12-selfish-gene";
    const auto rover = [] (const char* name, std::vector<Channel> channels, const char* gears)
    {
        for (auto& c : channels)
            c.cabinet = topFirst;
        return Product { name, "rover", "ROVER_GATE_OWNS_HOME", std::move (channels), gears, {},
                         "all the product's channels' packs are installed",
                         "every channel's tone knobs are its pack's tones, as many as it lists, knob i its tone i",
                         "...and every one on the TOP 12's first, drawn: the 4x12 SELFISH GENE" };
    };

    static const Product products[] {
        // The cockpit: the X88-IR's four channels, one to a detent of the throttle and each its own
        // memory. All four on a plate, the cleaner the wetter, Clean with a little delay behind it and
        // Edge with about half as much, the two driven ones with none.
        // Each pack is captures shot at several hours of the gear's own knob, and DRIVE stands on them.
        { "CatpitAmp", "cockpit", "CATPIT_GATE_OWNS_HOME",
          {
              { "x88-ir-normal-bae8",    "", "Plate",    20.0f, 15.0f, "after INIT the first channel stands on a plate, with a little delay", topFirst },
              { "x88-ir-altered-c14c",   "", "Plate",    15.0f, 8.0f,  "...and so does the second, with half the delay", topFirst },
              { "x88-ir-crunch-2abb",    "", "Plate",    12.0f, 0.0f,  "...the third on a plate too, and no delay", topFirst },
              { "x88-ir-overdrive-0344", "", "Plate",    10.0f, 0.0f,  "...the fourth the same, and none either", topFirst },
          },
          nullptr,
          {
              { 1.0f, 5.0,  "the capture at the top of DRIVE was shot at five o'clock" },
              { 0.5f, 12.0, "...the one in the middle at twelve" },
              { 0.0f, 9.0,  "...and the bottom plays the one shot at nine, fed softer" },
          },
          "all four channels' packs are installed",
          "every channel's tone knobs are its pack's three tones, knob i its tone i",
          "...and all four on the TOP 12's first, drawn: the 4x12 SELFISH GENE" },

        // On Mars the Friedman IR-X's two channels, each a gear: Ch1 the CRUNCH and Ch2 the HI-GAIN, its
        // TURBO the same pack with the Boost thrown. Both on a plate, Ch2 with no delay.
        rover ("RedPawAmp",
               {
                   { "ir-x-ch1-poweramp-3e66", "Boost=off", "Plate",    15.0f, 15.0f },
                   { "ir-x-ch1-poweramp-3e66", "Boost=on",  "Plate",    15.0f, 15.0f },
                   { "ir-x-ch2-poweramp-67e9", "Boost=off", "Plate",    10.0f, 0.0f  },
                   { "ir-x-ch2-poweramp-67e9", "Boost=on",  "Plate",    10.0f, 0.0f  },
               },
               "each gear is one of the IR-X's channels, its TURBO the same with the Boost on"),
        rover ("FrostPawAmp",
               {
                   { "revolt-clean-poweramp-bf62",  "Boost=off", "Plate",    20.0f, 15.0f },
                   { "revolt-clean-poweramp-bf62",  "Boost=on",  "Plate",    20.0f, 15.0f },
                   { "revolt-crunch-poweramp-73a7", "Boost=off", "Plate",    15.0f, 8.0f  },
                   { "revolt-crunch-poweramp-73a7", "Boost=on",  "Plate",    15.0f, 8.0f  },
                   { "revolt-lead-poweramp-f5dc",   "Boost=off", "Plate",    10.0f, 0.0f  },
                   { "revolt-lead-poweramp-f5dc",   "Boost=on",  "Plate",    10.0f, 0.0f  },
               },
               "each gear is one of the ReVolt's channels, its TURBO the same with the Boost on"),
        // Over Titan the Victory V4's three, a gear each, all on a plate: CLEAN the Copper, CRUNCH the Sheriff,
        // HI-GAIN the Kraken with no delay — its TURBO the same pack with the Boost on.
        rover ("AmberPawAmp",
               {
                   { "v4-copper-4ae0",      "Boost=off", "Plate",    20.0f, 15.0f },
                   { "v4-copper-4ae0",      "Boost=on",  "Plate",    20.0f, 15.0f },
                   { "v4-sheriff-ch1-82ab", "Boost=off", "Plate",    15.0f, 8.0f  },
                   { "v4-sheriff-ch1-82ab", "Boost=on",  "Plate",    15.0f, 8.0f  },
                   { "v4-kraken-ch1-f7fc",  "Boost=off", "Plate",    10.0f, 0.0f  },
                   { "v4-kraken-ch1-f7fc",  "Boost=on",  "Plate",    10.0f, 0.0f  },
               },
               "each gear is one of the V4's three, its TURBO the same with the Boost on"),
        // The walker: the bass's two amps, B15 and SVT, a gear each and each its own memory, on the bass's chain —
        // no room and no delay in its strip, each opening on the engine's share of a plate it never plays. Both the
        // Ampeg SGT-DI's two voices, each gear holding its Voice; each on a cabinet of its own among the bass's dozen,
        // drawn, B15 the 1x15 and SVT the 8x10 (src/redgroove/RedGrooveLineup.cpp).
        { "RedGrooveBass", "walker", "REDGROOVE_GATE_OWNS_HOME",
          {
              { "sgt-di-19a4", "Voice=B15", "Plate", 30.0f, 0.0f, nullptr, "rock-14-1x15-thump" },
              { "sgt-di-19a4", "Voice=SVT", "Plate", 30.0f, 0.0f, nullptr, "rock-03-8x10-stack" },
          },
          nullptr,
          {},
          "both gears' packs are installed",
          "both gears' tone knobs are their pack's dials on the bass's chain — the SGT-DI's Bass, Mid and Treble, its Freq and its "
          "two switches apart — or, on a pack with no tone of its own, the bass's standard's three",
          "...and each gear on its own of the bass's dozen, drawn: B15 the 1x15, SVT the 8x10",
          true,
          false,
          { true, true, true, false,
            "a bass's gear never pulled since INIT opens on its own DRIVE, BLEND, tones, MIDDLE's frequency, its pack's switches, COMP, effects and DI/CAB, not the other gear's",
            "each gear comes back with the DRIVE, BLEND, BASS, MIDDLE, TREBLE, MIDDLE's frequency, ULTRA LO and HI, COMP threshold, OCTAVE, ENVELOPE, CHORUS and DI/CAB it was left with",
            "INIT gives COMP on, three quarters of the signal through it at 4:1 on OPTO, OCTAVE, ENVELOPE and CHORUS in the strip and on, both cuts on, no delay and no room" } },
        // The bathyscaphe: the Two Notes ReVolt Bass's three channels, CLEAN, DIRT and DRIVE, a pack a gear and each its own
        // memory, on the bass's chain — every one on a plate it opens with none of, and no delay. DIRT's BLEND opens at
        // 80/20 and DRIVE's at 90/10; CLEAN has none and holds the capture alone. CLEAN on the dozen's 1x15, DIRT and DRIVE
        // on its 8x10, drawn (src/underwater/UnderwaterLineup.cpp). No ENVELOPE, no MIDDLE's frequency and no DI/CAB of a
        // gear's own; the room in the strip and on.
        { "DeepGrooveBass", "bathyscaphe", "DEEPGROOVE_GATE_OWNS_HOME",
          {
              { "revolt-bass-clean-0fba", "", "Plate", 0.0f, 0.0f, nullptr, "rock-14-1x15-thump", 1.0f, true },
              { "revolt-bass-dirt-e9d4",  "", "Plate", 0.0f, 0.0f, nullptr, "rock-03-8x10-stack", 0.8f },
              { "revolt-bass-drive-2655", "", "Plate", 0.0f, 0.0f, nullptr, "rock-03-8x10-stack", 0.9f },
          },
          nullptr,
          {},
          "all three gears' packs are installed",
          "every gear's tone knobs are its pack's dials on the bass's chain — the ReVolt Bass's CLEAN Bass and Treble, its DIRT's "
          "and DRIVE's Bass, Mid and Treble — with no frequency knob and no switch",
          "...and each gear on its own of the bass's dozen, drawn: CLEAN the 1x15, DIRT and DRIVE the 8x10",
          true,
          false,
          { false, false, false, true,
            "a bass's gear never pulled since INIT opens on its own DRIVE, BLEND, tones, COMP and effects, not the other gear's",
            "each gear comes back with the DRIVE, BLEND, BASS, MID, TREBLE, COMP threshold, OCTAVE and CHORUS it was left with",
            "INIT gives COMP on, three quarters of the signal through it at 4:1 on OPTO, OCTAVE and CHORUS in the strip and on, no ENVELOPE, both cuts on, no delay, and the room in the strip and on" } },
        // The E530 (FerroClawAmp, the owner's name, 20.11): four channels on the guitar's chain, a pack each and each
        // its own memory, the V4's as a stand-in sound — CLEAN GAIN LO the Copper, HI the Sheriff, LEAD GAIN LO the
        // Kraken and LEAD GAIN HI the Kraken again, every one holding the V4's Boost off (its key strip the E530's one
        // key, BRIGHT clean and CONTOUR lead, no Boost); all on a plate, the cleaner the wetter, the Kraken with no
        // delay; every one on the TOP 12's first, drawn (src/metal/MetalLineup.cpp). Nothing of before to gather.
        { "FerroClawAmp", "metal amp", "METAL_GATE_OWNS_HOME",
          {
              { "v4-copper-4ae0",      "Boost=off", "Plate", 20.0f, 15.0f, nullptr, topFirst },
              { "v4-sheriff-ch1-82ab", "Boost=off", "Plate", 15.0f, 8.0f,  nullptr, topFirst },
              { "v4-kraken-ch1-f7fc",  "Boost=off", "Plate", 10.0f, 0.0f,  nullptr, topFirst },
              { "v4-kraken-ch1-f7fc",  "Boost=off", "Plate", 10.0f, 0.0f,  nullptr, topFirst },
          },
          nullptr,
          {},
          "all four channels' packs are installed",
          "every channel's tone knobs are its pack's tones, as many as it lists, knob i its tone i",
          "...and every one on the TOP 12's first, drawn: the 4x12 SELFISH GENE",
          false,
          false },
    };

    for (const auto& p : products)
        if (juce::String (p.name) == ORBITAMP_PRODUCT_NAME)
            return p;

    std::printf ("FAIL  the gate knows the amp it runs as, %s\nFAILED\n", ORBITAMP_PRODUCT_NAME);
    std::exit (1);
}

/** The channels the build plays: the first GATE_CHANNELS of the product's, as far as it lists them. */
inline std::vector<Channel> played()
{
    const auto& all = product().channels;
    return { all.begin(), all.begin() + juce::jmin ((int) GATE_CHANNELS, (int) all.size()) };
}

} // namespace
} // namespace gate
