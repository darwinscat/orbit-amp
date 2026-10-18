// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Headless gate for amberpaw::TitanWorld — the methane sea's CPU half against the view's prototype. The
// start search reads the shore through the generated noise, so finding the prototype's start, to the
// last digit, says the noise is the prototype's; a scripted flight from it says the drive, the events
// placed on the course and the canopy's drops and wipers step as the prototype steps them. The numbers
// below are the prototype's own, read off its scripted drives (window.titan.drive). Then what is the
// port's own: the four gears and a product of fewer, Saturn never gone, three wipers on three panes.
// Returns non-zero on any failure.
//
// JUCE-free, like the code under test.

#include "amberpaw/view/TitanWorld.h"

#include <cmath>
#include <cstdio>

using orbitamp::amberpaw::TitanWorld;

namespace
{
    int failures = 0;

    void check (const char* what, double got, double want, double tol)
    {
        const bool ok = std::abs (got - want) <= tol;
        if (! ok) ++failures;
        std::printf ("%s %-44s got %.12g want %.12g\n", ok ? "  ok " : "FAIL ", what, got, want);
    }

    struct Flight { int gear; bool turbo, park; double time, seconds; };
    struct Seen { double x, z, headingDeg, speed, boost, down; int drops, trails, fields, buoys; double wiper, saturn, erupt; };

    /** The prototype's drive(): placed at the start at `time`, flown `seconds` at 60 steps a second. */
    void fly (const char* name, const Flight& f, const Seen& want)
    {
        TitanWorld w (4);
        w.build();
        const auto start = w.start();
        w.place (start[0], start[1], 0, f.gear, f.turbo, f.park);
        w.setClock (f.time, 0);
        for (int i = 0, n = (int) std::lround (f.seconds * 60); i < n; ++i)
            w.step (1.0 / 60);
        const auto& s = w.state();
        double wf = -1;
        const bool wiping = w.wiperFraction (s.time, wf);
        std::printf ("%s\n", name);
        check ("x", s.x, want.x, 1e-6);
        check ("z", s.z, want.z, 1e-6);
        check ("heading, degrees", s.heading / TitanWorld::deg, want.headingDeg, 1e-7);
        check ("speed", s.speed, want.speed, 1e-9);
        check ("TURBO dash", s.boost, want.boost, 1e-9);
        check ("downwash", s.down, want.down, 1e-9);
        check ("drops on the glass", (double) w.drops().size(), want.drops, 0);
        check ("trails", (double) w.trails().size(), want.trails, 0);
        check ("nitrogen fields", (double) w.fields().size(), want.fields, 0);
        check ("buoys", (double) w.buoys().size(), want.buoys, 0);
        check ("wiper stroke (-1 = parked)", wiping ? wf : -1, want.wiper, 1e-6);
        check ("Saturn", w.saturnVisibility (s.time), want.saturn, 1e-9);
        check ("eruption", w.eruption (s.time), want.erupt, 1e-9);
    }
}

int main()
{
    {
        TitanWorld w (4);
        w.build();
        std::printf ("the start\n");
        check ("x", w.start()[0], 256659.01279076934, 1e-6);
        check ("z", w.start()[1], 208723.99419546127, 1e-6);
    }

    // gear 1 cruising; mid-wipe at the 60 s cycle; the top gear's TURBO dash (the prototype's gear 3 is
    // this one's 4: both 13.5 m/s); PARK, a hover; Saturn out of the haze with the volcano erupting.
    fly ("gear 1", { 1, false, false, 500, 20 },
         { 256670.5237666793, 208677.9750098219, 20.84581875896388, 2.4, 0, 0.922, 18, 0, 1, 3, -1, 0.9977801343954082, 1 });
    fly ("mid-wipe", { 1, false, false, 500, 60.5 },
         { 256631.1051304885, 208598.59017425295, -35.840917064358074, 2.4, 0, 0.922, 102, 0, 2, 5, 0.4750092592586006, 0.62, 1 });
    fly ("TURBO dash", { 4, true, false, 500, 20 },
         { 256788.5112697613, 208206.27835703193, 20.84581875896388, 27, 1, 0.25, 73, 900, 1, 3, -1, 0.9977801343954082, 1 });
    fly ("PARK", { 1, false, true, 500, 20 },
         { 256659.01279076934, 208723.99419546127, 0, 0, 0, 1, 18, 0, 0, 0, -1, 0.9977801343954082, 1 });
    fly ("Saturn up", { 1, false, false, 455, 10 },
         { 256655.92652608608, 208700.3944584778, -22.529391457508343, 2.4, 0, 0.922, 21, 0, 1, 2, -1, 1, 0.7344960103876313 });

    std::printf ("the gears\n");
    {
        const TitanWorld four (4), three (3), one (1);
        const double want[4] { 2.4, 5.0, 8.6, 13.5 };
        for (int g = 1; g <= 4; ++g) check ("four gears: the gear's speed", four.speedFor (g, false), want[g - 1], 0);
        for (int g = 1; g <= 3; ++g) check ("three gears: the first three", three.speedFor (g, false), want[g - 1], 0);
        check ("three gears: a fourth asked for is the third", three.speedFor (4, false), 8.6, 0);
        check ("one gear: the first", one.speedFor (1, false), 2.4, 0);
        check ("TURBO doubles it", four.speedFor (4, true), 27.0, 0);
    }

    std::printf ("Saturn is never gone\n");
    {
        const TitanWorld w (4);
        double lo = 1, hi = 0;
        for (double t = 0; t < 3600; t += 0.25) { const double v = w.saturnVisibility (t); lo = std::min (lo, v); hi = std::max (hi, v); }
        check ("lowest in an hour", lo, 0.62, 1e-12);
        check ("highest in an hour", hi, 1.0, 1e-12);
    }

    std::printf ("three wipers, each on its own pane\n");
    {
        const auto& wipers = TitanWorld::wipers();
        check ("wipers", (double) wipers.size(), 3, 0);
        for (const auto& wp : wipers)
        {
            // the blade's middle, half-way through the stroke, lies on the wiper's own pane
            const double th = 0.5 * (wp.th0 + wp.th1), r = 0.5 * (wp.rIn + wp.rOut);
            const auto X = TitanWorld::cabinXY (wp.p[0] + r * std::cos (th), wp.p[1] + r * std::sin (th));
            const bool on = X[0] > wp.pane[0] + wp.pane[1] * X[1] && X[0] < wp.pane[2] + wp.pane[3] * X[1];
            check ("mid-stroke blade on its pane", on ? 1 : 0, 1, 0);
        }
    }

    std::printf ("the wipers' four speeds\n");
    {
        // From the start, 20 s at each: OFF none, INT none yet (a minute's wait), LO on and on with a
        // rest, HI on and on at its quicker stroke.
        const auto wipesIn = [] (int speed, double& stroke)
        {
            TitanWorld w (4);
            w.build();
            const auto start = w.start();
            w.place (start[0], start[1], 0, 1, false, false);
            w.setClock (500, 0);
            w.setWipers (speed);
            int wipes = 0;
            double last = w.wipeCurrent();
            for (int i = 0; i < 20 * 60; ++i)
            {
                w.step (1.0 / 60);
                if (w.wipeCurrent() != last) { ++wipes; last = w.wipeCurrent(); }
            }
            stroke = w.strokeCurrent().out;
            return wipes;
        };
        double stroke = 0;
        check ("OFF: wipes in 20 s", wipesIn (TitanWorld::wipersOff, stroke), 0, 0);
        check ("INT: wipes in 20 s", wipesIn (TitanWorld::wipersInterval, stroke), 0, 0);
        check ("LO: wipes in 20 s, one each 2.9 s", wipesIn (TitanWorld::wipersSlow, stroke), 7, 0);
        check ("LO: the stroke", stroke, TitanWorld::strokeSeconds, 0);
        check ("HI: wipes in 20 s, one each 1.22 s", wipesIn (TitanWorld::wipersFast, stroke), 17, 0);
        check ("HI: the stroke", stroke, TitanWorld::fastStroke, 0);
    }

    if (failures == 0) std::printf ("\nall passed\n");
    else std::printf ("\n%d FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
