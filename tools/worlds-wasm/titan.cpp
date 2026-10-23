// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// Titan's methane sea for the web: the app's TitanWorld and TitanGl (src/amberpaw/view) over a WebGL 2 context,
// driven from JS (view.js, titan-view.js) a call at a time — made, built, set and drawn. One view to a module.

#include "amberpaw/view/TitanGl.h"
#include "amberpaw/view/TitanWorld.h"
#include "view.h"

#include <cmath>
#include <cstdint>
#include <memory>

using orbitamp::amberpaw::TitanGl;
using orbitamp::amberpaw::TitanWorld;

namespace
{
    std::unique_ptr<TitanWorld> world;
    std::unique_ptr<TitanGl> view;

    // The wipers' keys and the light switches are the windscreen's, as in the app: held here and handed to the
    // world before every frame (Windscreen::renderFrame), not before a drive. The windscreen starts at INT, lights off.
    int wipersHeld = TitanWorld::wipersInterval;
    bool lightsHeld[2] { false, false };

    void handOver()
    {
        world->setWipers (wipersHeld);
        world->setLights (lightsHeld[0], lightsHeld[1]);
    }
}

extern "C"
{
    /** A world in the product's gears, at the gear and TURBO given; settled at them once it is built. */
    EMSCRIPTEN_KEEPALIVE void view_make (int gears, int gear, int turbo)
    {
        view.reset();
        world = std::make_unique<TitanWorld> (gears);
        world->setGear (gear);
        world->setTurbo (turbo != 0);
    }

    /** The sea and the search for the start: the long part. Puts the craft at the start, at the clock's zero. */
    EMSCRIPTEN_KEEPALIVE void view_build() { world->build(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The renderer's programs, buffers and textures; false when a shader would not build (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view = std::make_unique<TitanGl> (*world);
        return view->create() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return view != nullptr ? view->error().c_str() : ""; }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)     { world->setGear (g); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int on)   { world->setTurbo (on != 0); }

    /** Where it stands, at once as the drive would be once settled (TitanWorld::settle): the speed and the dash the
        gear asks for, the lights as they are switched. Only once the sea is built. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        if (! world->isBuilt())
            return;
        handOver();
        world->settle (world->gear(), world->isTurbo());
    }

    /** The wipers' keys: 0 OFF, 1 INT, 2 LO, 3 HI. */
    EMSCRIPTEN_KEEPALIVE void view_wipers (int speed)          { wipersHeld = speed; }
    /** The searchlights' switches, left and right. */
    EMSCRIPTEN_KEEPALIVE void view_lights (int left, int right) { lightsHeld[0] = left != 0; lightsHeld[1] = right != 0; }

    /** One frame: the flight stepped by dt seconds with the stick at `steer`, drawn at w x h into the canvas. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        world->setSteer (steer);
        handOver();
        world->step (dt);
        view->render (w, h, 1.0f, 0);
    }

    /** The world as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h) { view->render (w, h, 1.0f, 0); }

    /** A bench's frame, set up as AmberPawViewDemo sets its drive and then its STILL (WindscreenDemo.cpp's
        makeDrive, TitanDrive, Windscreen::renderFrame), in its order: placed at (x, z, heading in degrees) if
        `placed`; the clock set to `seconds` and `metres` driven if `clocked`; driven `drive` seconds at 60 steps a
        second with the stick held at `steer` — from the demo's 31500, 3000, heading 0, if not placed; then the
        windscreen's first frame: the wipers and lights it holds handed over and, for `still`, settled where it
        stands as the app's STILL is. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int clocked, double seconds, double metres,
                                         double drive, float steer, int still)
    {
        constexpr double pi = 3.14159265358979323846;
        world->build();
        if (placed)
            world->place (x, z, headingDegrees * pi / 180, world->gear(), world->isTurbo());
        if (clocked)
            world->setClock (seconds, metres);
        if (drive > 0)
        {
            if (! placed)
                world->place (31500, 3000, 0, world->gear(), world->isTurbo());
            world->setSteer (steer);
            for (int i = 0, n = (int) std::lrint (drive * 60); i < n; ++i)
                world->step (1.0 / 60);
        }
        world->setSteer (steer);
        handOver();
        if (still)
            world->settle (world->gear(), world->isTurbo());
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (view.get()); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (view.get());
        view.reset();
    }
}
