// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The Plains of Mars for the web: the app's PlainsWorld and PlainsGl (src/redpaw/view) over a WebGL 2
// context, driven from JS (plains-view.js) a call at a time — made, built, set and drawn. One view to a module.

#include "redpaw/view/PlainsGl.h"
#include "redpaw/view/PlainsWorld.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using orbitamp::redpaw::PlainsGl;
using orbitamp::redpaw::PlainsWorld;

namespace
{
    std::unique_ptr<PlainsWorld> world;
    std::unique_ptr<PlainsGl> view;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    std::vector<std::uint8_t> frame;
    int frameW = 0, frameH = 0;
}

// The context, on the canvas the JS gave the module as Module.plainsCanvas: an OffscreenCanvas in a worker,
// else the page's own. WebGL 2, no depth, stencil or alpha of its own: the scene draws into a frame of its
// own and is blitted. `keep` holds the canvas's pixels for reading back, as a bench wants.
EM_JS (int, plains_open_context, (int keep), {
    return GL.createContext (Module['plainsCanvas'], {
        majorVersion: 2, minorVersion: 0, antialias: false, alpha: false, depth: false, stencil: false,
        premultipliedAlpha: true, preserveDrawingBuffer: !! keep, powerPreference: 'default', enableExtensionsByDefault: true });
});

extern "C"
{
    /** A world in the product's gears, started at the gear, TURBO and PARK given — already at them, not easing. */
    EMSCRIPTEN_KEEPALIVE void plains_make (int gears, int gear, int turbo, int parked)
    {
        view.reset();
        world = std::make_unique<PlainsWorld> (gears);
        world->setGear (gear);
        world->setTurbo (turbo != 0);
        world->setParked (parked != 0);
        world->settle();
    }

    /** The landscape: the long part, a few hundred milliseconds. */
    EMSCRIPTEN_KEEPALIVE void plains_build() { world->build(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int plains_context (int keep)
    {
        context = (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE) plains_open_context (keep);
        return context != 0 && emscripten_webgl_make_context_current (context) == EMSCRIPTEN_RESULT_SUCCESS;
    }

    /** The renderer's programs, buffers and textures; false when a shader would not build (plains_error says why). */
    EMSCRIPTEN_KEEPALIVE int plains_create()
    {
        view = std::make_unique<PlainsGl> (*world);
        return view->create() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* plains_error() { return view != nullptr ? view->error().c_str() : ""; }

    EMSCRIPTEN_KEEPALIVE void plains_gear (int g)     { world->setGear (g); }
    EMSCRIPTEN_KEEPALIVE void plains_turbo (int on)   { world->setTurbo (on != 0); }
    EMSCRIPTEN_KEEPALIVE void plains_park (int on)    { world->setParked (on != 0); }
    EMSCRIPTEN_KEEPALIVE void plains_settle()         { world->settle(); }

    /** One frame: the drive stepped by dt seconds with the wheel at `steer`, drawn at w x h into the canvas. */
    EMSCRIPTEN_KEEPALIVE void plains_frame (int w, int h, double dt, float steer)
    {
        world->setSteer (steer);
        world->step (dt);
        view->render (w, h, 1.0f, 0);
    }

    /** The world as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void plains_draw (int w, int h) { view->render (w, h, 1.0f, 0); }

    /** A bench's frame, set up as RedPawViewDemo sets its drive and then its STILL (WindscreenDemo.cpp's
        makeDrive, Windscreen::renderFrame): placed at (x, z, heading in degrees) if `placed`, driven `drive`
        seconds at 60 steps a second with the wheel held at `steer` — from the start, 31500, 3000, heading 0, if
        not placed — and, for `still`, settled where it stands as the app's STILL is. The Plains keep no clock. */
    EMSCRIPTEN_KEEPALIVE void plains_test (int placed, double x, double z, double headingDegrees, double drive, float steer, int still)
    {
        constexpr double pi = 3.14159265358979323846;
        world->build();
        if (placed)
            world->place (x, z, headingDegrees * pi / 180, world->gear(), world->isTurbo());
        if (drive > 0)
        {
            if (! placed)
                world->place (31500, 3000, 0, world->gear(), world->isTurbo());
            world->setSteer (steer);
            for (int i = 0, n = (int) std::lrint (drive * 60); i < n; ++i)
                world->step (1.0 / 60);
        }
        world->setSteer (steer);
        if (still)
        {
            const auto& s = world->state();
            world->place (s.x, s.z, s.heading, world->gear(), world->isTurbo());
        }
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* plains_read()
    {
        if (view == nullptr || ! view->readFrame (frame, frameW, frameH))
            frameW = frameH = 0;
        return frame.data();
    }
    EMSCRIPTEN_KEEPALIVE int plains_read_width()  { return frameW; }
    EMSCRIPTEN_KEEPALIVE int plains_read_height() { return frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void plains_destroy()
    {
        if (view != nullptr && context != 0 && emscripten_webgl_make_context_current (context) == EMSCRIPTEN_RESULT_SUCCESS)
            view->destroy();
        view.reset();
        if (context != 0)
            emscripten_webgl_destroy_context (context);
        context = 0;
    }
}
