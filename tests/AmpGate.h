// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

/** WHAT AN AMP'S GATES HOLD IT TO, AND WHICH AMP IT IS. The cockpit and the rovers are one engine under
    faces of their own (channels/Engine.h), and so their gates are one too: AmpSetTests.cpp for the set as
    it leaves the binary, AmpChannelTests.cpp for the channels with no window — each built once for every
    product, and run as it (CMakeLists.txt: amp_set_test, amp_console). What tells the products apart is
    said here, once, in the table below — apart from each product's lineup, so that a lineup that drifts
    is caught rather than read back: the captures its channels play and what each opens on, whether they
    go in pairs, a gear and its TURBO, whether it has PARK, the marks on its DRIVE's dial, the variable that
    tells its channel gate the home is its own (tests/gates.txt), and the words its gate says what it found
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

namespace gate
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

    bool park;                      // PARK: the lever's top end, the whole amp out of the sound
    const char* gears;              // where the channels go in pairs, a gear and its TURBO, what its gate says of them; none where they do not
    std::vector<DialMark> dial;     // none where DRIVE plays one capture

    // What its gate says it found, where the products' words differ.
    const char* installed;
    const char* tones;
    const char* opening;
};

/** THE PRODUCTS, each as its gate knows it. */
inline const Product& product()
{
    const auto rover = [] (const char* name, std::vector<Channel> channels, const char* gears)
    {
        return Product { name, "rover", "ROVER_GATE_OWNS_HOME", std::move (channels), true, gears, {},
                         "all the product's channels' packs are installed",
                         "every channel's tone knobs are its pack's tones, as many as it lists, knob i its tone i",
                         "...and every one on the build's DARK MATTER" };
    };

    static const Product products[] {
        // The cockpit: the X88-IR's four channels, one to a detent of the throttle and each its own
        // memory, no PARK. The two clean ones stand in a hall with a little delay behind them, the two
        // driven ones each in a room of its own and no delay, all four at the room's own default share.
        // Each pack is captures shot at several hours of the gear's own knob, and DRIVE stands on them.
        { "CatpitAmp", "cockpit", "CATPIT_GATE_OWNS_HOME",
          {
              { "x88-ir-normal-bae8",    "", "Hall",     30.0f, 15.0f, "after INIT the first channel stands in a hall, with a little delay" },
              { "x88-ir-altered-c14c",   "", "Hall",     30.0f, 15.0f, "...and so does the second" },
              { "x88-ir-crunch-2abb",    "", "Plate",    30.0f, 0.0f,  "the third opens on a plate, and no delay" },
              { "x88-ir-overdrive-0344", "", "Ambience", 30.0f, 0.0f,  "...the fourth on an ambience, and none either" },
          },
          false, nullptr,
          {
              { 1.0f, 5.0,  "the capture at the top of DRIVE was shot at five o'clock" },
              { 0.5f, 12.0, "...the one in the middle at twelve" },
              { 0.0f, 9.0,  "...and the bottom plays the one shot at nine, fed softer" },
          },
          "all four channels' packs are installed",
          "every channel's tone knobs are its pack's three tones, knob i its tone i",
          "...and all four on the build's DARK MATTER" },

        // On Mars the Friedman IR-X's two channels, each a gear: Ch1 the CRUNCH and Ch2 the HI-GAIN, its
        // TURBO the same pack with the Boost thrown. Ch1 on a plate, Ch2 in an ambience with no delay.
        rover ("RedPawAmp",
               {
                   { "ir-x-ch1-poweramp-3e66", "Boost=off", "Plate",    15.0f, 15.0f },
                   { "ir-x-ch1-poweramp-3e66", "Boost=on",  "Plate",    15.0f, 15.0f },
                   { "ir-x-ch2-poweramp-67e9", "Boost=off", "Ambience", 15.0f, 0.0f  },
                   { "ir-x-ch2-poweramp-67e9", "Boost=on",  "Ambience", 15.0f, 0.0f  },
               },
               "each gear is one of the IR-X's channels, its TURBO the same with the Boost on"),
        rover ("FrostPawAmp",
               {
                   { "revolt-clean-poweramp-bf62",  "Boost=off", "Hall",     15.0f, 15.0f },
                   { "revolt-clean-poweramp-bf62",  "Boost=on",  "Hall",     15.0f, 15.0f },
                   { "revolt-crunch-poweramp-73a7", "Boost=off", "Plate",    15.0f, 15.0f },
                   { "revolt-crunch-poweramp-73a7", "Boost=on",  "Plate",    15.0f, 15.0f },
                   { "revolt-lead-poweramp-f5dc",   "Boost=off", "Ambience", 15.0f, 0.0f  },
                   { "revolt-lead-poweramp-f5dc",   "Boost=on",  "Ambience", 15.0f, 0.0f  },
               },
               "each gear is one of the ReVolt's channels, its TURBO the same with the Boost on"),
        // Over Titan the Victory V4's three, a gear each: CLEAN the Copper in a hall, CRUNCH the Sheriff on a
        // plate, HI-GAIN the Kraken in an ambience with no delay — its TURBO the same pack with the Boost on.
        rover ("AmberPawAmp",
               {
                   { "v4-copper-4ae0",      "Boost=off", "Hall",     15.0f, 15.0f },
                   { "v4-copper-4ae0",      "Boost=on",  "Hall",     15.0f, 15.0f },
                   { "v4-sheriff-ch1-82ab", "Boost=off", "Plate",    15.0f, 15.0f },
                   { "v4-sheriff-ch1-82ab", "Boost=on",  "Plate",    15.0f, 15.0f },
                   { "v4-kraken-ch1-f7fc",  "Boost=off", "Ambience", 15.0f, 0.0f  },
                   { "v4-kraken-ch1-f7fc",  "Boost=on",  "Ambience", 15.0f, 0.0f  },
               },
               "each gear is one of the V4's three, its TURBO the same with the Boost on"),
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

} // namespace gate
