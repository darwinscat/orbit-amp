// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// CatpitAmp's sky for the web: the app's Flight and SkyGl (src/catpit) over a WebGL 2 context, driven from JS
// (view.js, sky-view.js) a call at a time, the way every world is — made, set and drawn. The canvas is the glass;
// the picture in it is composed for the windshield inside it, as the app's GlSky composes it. One sky to a module.
//
// The calls are every world's (view.h). What a world calls a gear is the sky's channel, counted from 0 among
// `gears` channels, the throttle standing at its detent; the sky has no TURBO or PARK, and those two calls do
// nothing. Its own: view_throttle, the lever anywhere along its travel, and view_place, where the glass and the
// windshield are.

#include "catpit/CatpitFlight.h"
#include "catpit/CatpitSkyGl.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

using orbitamp::catpit::Flight;
using orbitamp::catpit::SkyGl;

namespace
{
    std::unique_ptr<SkyGl> view;
    Flight flight;
    int channels = 4;
    float throttle = 0.0f;

    // The glass and the windshield in the stage's units (the cockpit's own: cockpit.json), x, y, w, h each.
    // Until they are given, the windshield is the whole glass.
    float glass[4] { 0.0f, 0.0f, 1.0f, 1.0f }, windshield[4] { 0.0f, 0.0f, 1.0f, 1.0f };

    /** The lever at a channel's detent, as the app's lever stands there: the channel over the last one. */
    float detent (int channel) { return std::clamp ((float) channel / (float) std::max (1, channels - 1), 0.0f, 1.0f); }

    /** The flight as it stands, over the whole canvas, w x h: the windshield measured from the glass's corner
        in the canvas's pixels, rows from the bottom as GL counts them (GlSky::renderOpenGL). */
    void draw (int w, int h)
    {
        const float k = (float) w / glass[2];
        const float vx = (windshield[0] - glass[0]) * k, vw = windshield[2] * k, vh = windshield[3] * k;
        const float vy = (float) h - ((windshield[1] - glass[1]) + windshield[3]) * k;
        view->render (flight, 0, 0, w, h, vx, vy, vw, vh);
    }
}

extern "C"
{
    /** A sky among `gears` channels at channel `gear` (0 the first); flown at that channel's speed once settled. */
    EMSCRIPTEN_KEEPALIVE void view_make (int gears, int gear, int, int)
    {
        view.reset();
        flight = {};
        channels = gears;
        throttle = detent (gear);
    }

    /** Nothing to build: the sky is all in its shader. */
    EMSCRIPTEN_KEEPALIVE void view_build() {}

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The shader and the quad; false when the shader would not build (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view = std::make_unique<SkyGl>();
        return view->create() ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return view != nullptr ? view->error().c_str() : ""; }

    /** The lever to a channel's detent: the ship spools up or down to its speed. */
    EMSCRIPTEN_KEEPALIVE void view_gear (int g)  { throttle = detent (g); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int)   {}
    EMSCRIPTEN_KEEPALIVE void view_park (int)    {}

    /** The lever anywhere along its travel, 0 = CLEAN .. 1 = HI-GAIN, as the app's GlSky::setThrottle. */
    EMSCRIPTEN_KEEPALIVE void view_throttle (float t) { throttle = std::clamp (t, 0.0f, 1.0f); }

    /** Where the glass and the windshield are, in the stage's units: the canvas lies over the glass. */
    EMSCRIPTEN_KEEPALIVE void view_place (float gx, float gy, float gw, float gh, float wx, float wy, float ww, float wh)
    {
        if (! (gw > 0.0f && gh > 0.0f && ww > 0.0f && wh > 0.0f))
            return;
        glass[0] = gx; glass[1] = gy; glass[2] = gw; glass[3] = gh;
        windshield[0] = wx; windshield[1] = wy; windshield[2] = ww; windshield[3] = wh;
    }

    /** At the throttle's speed at once (Flight::settle). */
    EMSCRIPTEN_KEEPALIVE void view_settle() { flight.settle (throttle); }

    /** One frame: the flight stepped by dt seconds with the helm at `steer`, drawn at w x h into the canvas. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        flight.step (throttle, steer, (float) dt);
        draw (w, h);
    }

    /** The flight as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h) { draw (w, h); }

    /** A bench's frame: the flight from its start, settled at the throttle if `still`, then flown `drive` seconds at
        60 steps a second with the helm held at `steer`. A world's place and clock mean nothing to the sky. */
    EMSCRIPTEN_KEEPALIVE void view_test (int, double, double, double, int, double, double, double drive, float steer, int still)
    {
        flight = {};
        if (still)
            flight.settle (throttle);
        for (int i = 0, n = (int) std::lrint (drive * 60); i < n; ++i)
            flight.step (throttle, steer, 1.0f / 60.0f);
    }

    /** The glass's pixels as the last frame left them, RGBA, rows bottom-up as GL has them. */
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
