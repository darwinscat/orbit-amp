// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The ice moon for the web: the app's IceWorld and IceGl (src/frostpaw/view) over a WebGL 2 context, driven from
// JS (view.js, ice-view.js) a call at a time — made, built, set and drawn. One view to a module.

#include "frostpaw/view/IceGl.h"
#include "frostpaw/view/IceWorld.h"
#include "view.h"

#include <cmath>
#include <cstdint>
#include <memory>

using orbitamp::frostpaw::IceGl;
using orbitamp::frostpaw::IceWorld;

namespace
{
    std::unique_ptr<IceWorld> world;
    std::unique_ptr<IceGl> view;
}

extern "C"
{
    /** A world in the product's gears, started at the gear, TURBO and PARK given — already at them, not easing. */
    EMSCRIPTEN_KEEPALIVE void view_make (int gears, int gear, int turbo, int parked)
    {
        view.reset();
        world = std::make_unique<IceWorld> (gears);
        world->setGear (gear);
        world->setTurbo (turbo != 0);
        world->setParked (parked != 0);
        world->settle();
    }

    /** The ice: the long part, a few hundred milliseconds. Puts the rover at the start, at the clock's hour. */
    EMSCRIPTEN_KEEPALIVE void view_build() { world->build(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The renderer's programs, buffers and textures; false when a shader would not build (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view = std::make_unique<IceGl> (*world);
        return view->create() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return view != nullptr ? view->error().c_str() : ""; }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)     { world->setGear (g); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int on)   { world->setTurbo (on != 0); }
    EMSCRIPTEN_KEEPALIVE void view_park (int on)    { world->setParked (on != 0); }
    EMSCRIPTEN_KEEPALIVE void view_settle()         { world->settle(); }

    /** One frame: the drive stepped by dt seconds with the wheel at `steer`, drawn at w x h into the canvas. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        world->setSteer (steer);
        world->step (dt);
        view->render (w, h, 1.0f, 0);
    }

    /** The world as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h) { view->render (w, h, 1.0f, 0); }

    /** A bench's frame, set up as FrostPawViewDemo sets its drive and then its STILL (WindscreenDemo.cpp's
        makeDrive, IceDrive, Windscreen::renderFrame), in its order: placed at (x, z, heading in degrees) if
        `placed`; the clock set to `seconds` and `metres` driven if `clocked`; driven `drive` seconds at 60 steps a
        second with the wheel held at `steer` — from the start, 31500, 3000, heading 0, if not placed — and, for
        `still`, settled where it stands as the app's STILL is. A clock with no place of its own is the hour the
        rover was put at the start by the making of the ice: the demo sets it on a drive not yet built. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int clocked, double seconds, double metres,
                                         double drive, float steer, int still)
    {
        constexpr double pi = 3.14159265358979323846;
        world->build();
        if (clocked && ! placed)
        {
            world->setClock (seconds, metres);
            const auto& s = world->state();
            world->place (s.x, s.z, s.heading, world->gear(), world->isTurbo(), world->isParked());
        }
        if (placed)
        {
            world->place (x, z, headingDegrees * pi / 180, world->gear(), world->isTurbo(), world->isParked());
            if (clocked)
                world->setClock (seconds, metres);
        }
        if (drive > 0)
        {
            if (! placed)
                world->place (31500, 3000, 0, world->gear(), world->isTurbo(), world->isParked());
            world->setSteer (steer);
            for (int i = 0, n = (int) std::lrint (drive * 60); i < n; ++i)
                world->step (1.0 / 60);
        }
        world->setSteer (steer);
        if (still)
        {
            const auto& s = world->state();
            world->place (s.x, s.z, s.heading, world->gear(), world->isTurbo(), world->isParked());
        }
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
