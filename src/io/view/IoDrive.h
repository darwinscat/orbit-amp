// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "IoGl.h"
#include "IoWorld.h"
#include "../../redpaw/view/Drive.h"
#include "../../underwater/view/UnderwaterWorld.h"     // Glass
#include "../../worlds/Play.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

namespace orbitamp::io
{

/** IO AS A DRIVE: IoWorld and IoGl behind the windscreen's face. A product passes one to the windscreen —
    `Windscreen (std::make_unique<io::IoDrive> (art))` — and the view is the rover cabin's whole stage, 1672 x 941, in its
    cabin camera's projection as cabin.html hands it to the prototype: a 16 mm lens over the stage's width (the
    half-width's tangent 18/16 at any aspect), the centre at the stage's centre (no shear), the eye 2.6 m over the road
    (IoWorld::eye), the world 5 degrees down (the world's 2, and the page's 3 more). The cabin (the day's and the dark's
    plates crossfaded as the sunlight goes, the dosimeter's live needle) is drawn over it while its plates are in
    (drawsCabin); the 1993 landscape wears Mars's colours, the nearest of its looks to Io's sulphur plains under a black
    sky.

    The SansAmp RBI's lever: CRUISE SLOW and CRUISE FAST are the gears 1 and 2 (no TURBO), and PARK pulls the rover onto
    the shoulder (the amp bypassed; the world goes on as it is). THE AMP'S KNOBS ARE THE WORLD'S INPUTS: setDrive (the
    time of day, turned up the day: 7 o'clock the deepest night, 9 o'clock night, the Sun on the skyline at 0.35, 12
    o'clock a low Sun, 3 o'clock full day, 5 o'clock the highest Sun; IoWorld::sunSolve), setBlend (Jupiter's phase, down
    to the eclipse at 0; its light on the land the full Jupiter's at any phase), setPresence (the sulphur snow), each 0..1 and
    eased by the world (IoWorld::setKnob); cabinDark is how dark the cabin is (1 - cabinDay: the dark plate's share), for
    the face's night glow. The bass heard is the attack answer, as the sea's is: an onset in its level is a hit — the
    aurora flares, the nearest vents puff, the dosimeter kicks, the volcanoes take their share. No steering: the rover
    drives itself round the loop. The world is made the first time it is needed and settled then, as the prototype's
    view makes it: a gear, PARK or a knob asked for before that is what it is made in. The knobs and cabinDark may be
    called from any thread (the face's); everything else from the windscreen's. */
class IoDrive final : public redpaw::Drive
{
public:
    /** The cabin's projection, as cabin.html hands it to the prototype. */
    static constexpr double tanH = 18.0 / 16.0, shear = 0.0;
    static constexpr double pitchOffset = -3.0 * IoWorld::pi / 180;   // the world's 2 degrees down, made 5 (page.js)

    /** With the cabin's art (IoGl::Art; none draws no cabin and the finds as their stand-ins). The day's plate is looked
        at once here, for where its window is (glassAt): call on the message thread, before the windscreen. */
    explicit IoDrive (IoGl::Art art = {})
    {
        if (art.day)
        {
            const auto day = art.day();
            if (day.valid())
                glass = underwater::UnderwaterWorld::Glass::of (day.rgba.data(), day.width, day.height, 4);
        }
        gl.setArt (std::move (art));
        gl.setLens (tanH, shear);
    }

    IoWorld& io() noexcept { return world; }

    const char* name() const noexcept override { return "Io"; }
    int numGears() const noexcept override     { return IoWorld::numGears; }
    void setGear (int g) override            { world.setGear (std::clamp (g, 1, IoWorld::numGears)); }
    void setTurbo (bool) override            {}    // the RBI has no TURBO
    void setSteer (float) override           {}    // the rover drives itself
    void setPark (bool on) override          { world.setParked (on); }

    /** THE AMP'S KNOBS, 0..1, any thread: taken by the world at its next step (made with them, if not made yet). */
    void setDrive (double v) noexcept    { knob[0].store (std::clamp (v, 0.0, 1.0)); }
    void setBlend (double v) noexcept    { knob[1].store (std::clamp (v, 0.0, 1.0)); }
    void setPresence (double v) noexcept { knob[2].store (std::clamp (v, 0.0, 1.0)); }
    /** How dark the cabin is, 0 (the day's plate) .. 1 (the dark one): 1 - cabinDay, as the last step left it. Any thread. */
    float cabinDark() const noexcept { return dark.load(); }

    /** THE BASS, heard: the amp's input's peak since the last step, made a level between what counts as sounding and
        its top (worlds::Play's floorDb..topDb); a jump in it is an attack, as UnderwaterDrive hears one: over 0.28 of
        the span, 0.14 s after the last, its strength the jump's. */
    void hear (float peak, float /*pick*/) override { if (lit) heard = std::max (heard, std::abs (peak)); }
    /** Not lit by the bass: no answer at all, and one running is called off. */
    void setLit (bool on) override
    {
        if (lit && ! on)
            world.quiet();
        lit = on;
        if (! on) { heard = 0.0f; levelLP = 0.0; }
    }

    void step (double dt) override
    {
        made();
        takeKnobs();
        if (lit && dt > 0)
        {
            const float db = 20.0f * std::log10 (std::max (heard, 1.0e-6f));
            const double level = std::clamp ((db - worlds::Play::floorDb) / (worlds::Play::topDb - worlds::Play::floorDb), 0.0f, 1.0f);
            const double jump = level - levelLP;
            if (jump > onsetJump && time - lastOnset > onsetGap) { lastOnset = time; world.hit (std::clamp (jump * 1.6, 0.3, 1.0)); }
            levelLP += (level - levelLP) * (1 - std::exp (-dt / onsetSeconds));
        }
        heard = 0.0f;
        time += dt;
        if (dt > 0)                 // the prototype's view steps only by a time (a still is no step)
            world.step (dt);
        dark.store ((float) (1 - world.state().cabinDay));
    }

    /** A gear or PARK changed is settled at once, as the prototype's view settles one; a still otherwise keeps the
        drive where it is. */
    void settle (int g, bool) override
    {
        const bool changed = std::clamp (g, 1, IoWorld::numGears) != world.gear();
        setGear (g);
        if (! world.isBuilt())
            made();
        else if (changed)
            world.settle();
        if (world.state().time <= 0)
            world.settleCamera();
        dark.store ((float) (1 - world.state().cabinDay));
    }

    /** The rover put at the point of the loop nearest (x, z), on the lap it is on; the heading is the road's (it drives
        itself one way round). */
    void placeAt (double x, double z, double /*heading*/) override
    {
        made();
        world.startAt (IoWorld::nearestOnLoop (x, z).at, -1);
    }

    /** The rover put at arclength `sr` round the loop on lap `lap` (< 0: the lap it is on), the world made first if it
        is not: the prototype's setAt (its ?s=&lap=), the gear kept. */
    void placeOnLoop (double sr, int lap)
    {
        made();
        world.startAt (sr, lap);
    }

    /** The world made and settled in the gear, PARK and the knobs asked for, as the prototype's view makes it — the
        camera left where the world's first steps take it: what a drive run on from here, as the prototype's ?t= runs it,
        starts from. */
    void make() { made(); }

    /** The prototype's ?off= (IoGl::setSkip): draws left out, for timing the rest. */
    void setSkip (const std::string& list) { gl.setSkip (list); }

    float speed() const noexcept override { return (float) world.state().speed; }
    float speedFor (int g, bool) const noexcept override
    {
        return (float) IoWorld::gearSpeed[(size_t) (std::clamp (g, 1, IoWorld::numGears) - 1)];
    }
    Ease ease() const noexcept override { return { IoWorld::speedSeconds }; }
    /** The 1993 look: Mars's, the nearest of Mars1993's (mars, ice, titan, walker, underwater) to Io's sulphur-yellow
        and rust plains. */
    redpaw::Mars1993::Look look1993() const noexcept override { return redpaw::Mars1993::Look::mars; }

    bool create() override                                       { made(); takeKnobs(); return gl.create(); }
    std::string error() const override                           { return gl.error(); }
    void destroy() override                                      { gl.destroy(); }
    void render (int w, int h, float scale, uint32_t target, int x, int y) override { gl.render (w, h, scale, target, x, y); }
    float gpuMillis() const noexcept override                    { return gl.gpuMillis(); }
    bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) override { return gl.readFrame (rgba, w, h); }
    int frameWidth() const noexcept override                     { return gl.frameWidth(); }
    int frameHeight() const noexcept override                    { return gl.frameHeight(); }
    void setRepeats (int times) override                         { gl.setRepeats (times); }
    bool drawsCabin() const override                             { return gl.drawsCabin(); }
    void setCabin (bool drawn) override                          { gl.setCabin (drawn); }
    /** The window, where the day plate's alpha is low; everywhere when no plate was given. Any thread. */
    bool glassAt (float x01, float y01) const override           { return glass.open (x01, y01); }

private:
    static constexpr double onsetJump = 0.28, onsetGap = 0.14, onsetSeconds = 0.06;
    bool lit = false;
    float heard = 0.0f;
    double levelLP = 0.0, time = 0.0, lastOnset = -10.0;
    std::array<std::atomic<double>, 3> knob { { 0.0, 1.0, 0.2 } };    // drive, blend, presence: the prototype's defaults
    std::array<double, 3> taken { -1.0, -1.0, -1.0 };
    std::atomic<float> dark { 0.0f };
    underwater::UnderwaterWorld::Glass glass;       // made once, read only after
    IoWorld world;
    IoGl gl { world };

    /** The knobs asked for since the last step, handed to the world (it eases toward them). */
    void takeKnobs()
    {
        static constexpr std::array<IoWorld::Knob, 3> which { IoWorld::Knob::drive, IoWorld::Knob::blend, IoWorld::Knob::presence };
        for (size_t k = 0; k < 3; ++k)
            if (const double v = knob[k].load(); std::memcmp (&v, &taken[k], sizeof v) != 0)     // a new value, bit for bit
            {
                taken[k] = v;
                world.setKnob (which[k], v);
            }
    }

    /** The world made and settled, once: what the prototype's view does as it starts (makeWorld, the gear, PARK and the
        knobs, settle; then the page's pitch). */
    void made()
    {
        if (world.isBuilt())
            return;
        const int g = world.gear();
        const bool parked = world.isParked();
        world.build();
        world.setGear (g);
        world.setParked (parked);
        takeKnobs();
        world.settle();
        world.setPitchOffset (pitchOffset);
        dark.store ((float) (1 - world.state().cabinDay));
    }
};

} // namespace orbitamp::io
