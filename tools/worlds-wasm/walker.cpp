// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The walker's glowing forest for the web: the app's WalkerDrive — WalkerWorld and WalkerGl (src/redgroove/view) —
// over a WebGL 2 context, driven from JS (view.js, walker-view.js) a call at a time — made, built, set and drawn. One
// view to a module.
//
// The drive is the plug-in's own, made as RedGrooveEditor::makeDrive makes it — in the product's gears, the horizon
// 0.57 of the dome from its top — and handed what the windscreen hands it (Windscreen::renderFrame): the gear and TURBO
// it holds, the steering, what was heard of the bass, then a step and a frame. Its first frame is the app's
// (Windscreen::firstFrame, which made the kit's posters): the world made in the drive's own first gear, then the gear
// asked for settled in and drawn with no step.
//
// THE BASS, HEARD: the forest answers the bass from the first note, as the plug-in's does unless the player says not to
// (RedGrooveEditor: viewLitByDefault) — the drive is lit, and the peak the page hands view_hear before a frame is the
// amp's input's (WalkerDrive::hear: the caps breathe with its level, a jump in it is an attack's flash and sparks).
// Nothing handed is silence, the forest as it always was; a page that wants it unlit hands nothing.

#include "redgroove/view/WalkerDrive.h"
#include "view.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

using orbitamp::redgroove::WalkerDrive;

namespace
{
    /** Where the horizon sits in the dome, a fraction of it from its top: RedGrooveEditor::makeDrive's. */
    constexpr double horizon = 0.57;

    std::unique_ptr<WalkerDrive> drive;
    int gearsMade = 2;
    std::string trouble;            // view_error's words, kept while the page reads them

    // The gear and TURBO are the windscreen's, as in the app: held here and handed to the drive before every frame
    // and every settle (Windscreen::renderFrame), so a gear asked for before the world is made does not make it there
    // (the app's first frame is made in the drive's own first gear, then settled at the gear) and a settle sees the
    // gear it is asked to settle at as a change (WalkerDrive::settle settles only a change).
    int gearHeld = 1;
    bool turboHeld = false;
}

extern "C"
{
    /** A drive in the product's gears, lit by the bass; the gear and TURBO given are held, and settled in once the world
        is built. */
    EMSCRIPTEN_KEEPALIVE void view_make (int gears, int gear, int turbo)
    {
        gearsMade = gears;
        drive = std::make_unique<WalkerDrive> (gears, horizon);
        drive->setLit (true);
        gearHeld = gear;
        turboHeld = turbo != 0;
    }

    /** The world: the heightfield and the moss's detail, tens of milliseconds. Stands the walker at its world's start, in
        the drive's first gear, as WalkerDrive::create makes it in the app. */
    EMSCRIPTEN_KEEPALIVE void view_build() { drive->walker().build(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. The light map is a half-float frame
        the caps' glow is added into (WalkerGl): WebGL 2 draws into one only with EXT_color_buffer_float, or
        EXT_color_buffer_half_float where a browser has only that. With neither, view_create says so. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep)
    {
        if (! worlds::open (keep))
            return 0;
        if (! emscripten_webgl_enable_extension (worlds::context, "EXT_color_buffer_float"))
            emscripten_webgl_enable_extension (worlds::context, "EXT_color_buffer_half_float");
        return 1;
    }

    /** The renderer's programs, buffers and textures; false when a shader would not build or the light map is not
        renderable here (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        const bool made = drive->create();
        trouble = made ? std::string() : drive->error();
        return made ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return trouble.c_str(); }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)     { gearHeld = g; }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int on)   { turboHeld = on != 0; }

    /** Where it stands, at once as the drive would be once settled (WalkerDrive::settle): the pace, the stride, the
        spores and the storm the gear and TURBO ask for. Only once the world is built. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        if (! drive->walker().isBuilt())
            return;
        drive->settle (gearHeld, turboHeld);
    }

    /** The bass since the last frame: the amp's input's peak, linear (1 = full scale), handed to the drive before the
        frame that steps it (view.js, view.hear) — the largest of several before a step is kept. */
    EMSCRIPTEN_KEEPALIVE void view_hear (float peak) { drive->hear (peak, 0.0f); }

    /** One frame: the walk stepped by dt seconds with the hand on the glass at `steer`, drawn at w x h into the canvas. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        drive->setGear (gearHeld);
        drive->setTurbo (turboHeld);
        drive->setSteer (steer);
        drive->step (dt);
        drive->render (w, h, 1.0f, 0, 0, 0);
    }

    /** The world as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h) { drive->render (w, h, 1.0f, 0, 0, 0); }

    /** A bench's frame, set up as WalkerViewDemo sets its drive and then its STILL (WindscreenDemo.cpp's makeDrive,
        WalkerDrive, Windscreen::renderFrame), in its order: a fresh drive in the gear and TURBO held — the demo sets
        them before anything builds the world, so it is made in that gear —; placed at (x, z, heading in degrees) if
        `placed`; walked `drive` seconds at 60 steps a second with the hand held at `steer`, the gear settled first —
        from the world's own start if not placed —; and, for `still`, settled where it stands as the app's STILL is.
        The walker's world has no clock: `clocked` is not its. Compare with the demo at --horizon=0.57. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int /*clocked*/, double /*seconds*/,
                                         double /*metres*/, double driveSeconds, float steer, int still)
    {
        constexpr double pi = 3.14159265358979323846;
        drive = std::make_unique<WalkerDrive> (gearsMade, horizon);
        drive->setGear (gearHeld);
        drive->setTurbo (turboHeld);
        if (placed)
            drive->placeAt (x, z, headingDegrees * pi / 180);
        if (driveSeconds > 0)
        {
            drive->setSteer (steer);
            drive->settle (gearHeld, turboHeld);
            for (int i = 0, n = (int) std::lrint (driveSeconds * 60); i < n; ++i)
                drive->step (1.0 / 60);
        }
        drive->setSteer (steer);
        if (still)
            drive->settle (gearHeld, turboHeld);
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (drive.get()); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (drive.get());
    }
}
