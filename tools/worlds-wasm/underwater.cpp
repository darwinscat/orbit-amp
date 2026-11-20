// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The bathyscaphe's sea floor for the web: the app's UnderwaterDrive — UnderwaterWorld and UnderwaterGl
// (src/underwater/view) — over a WebGL 2 context, driven from JS (view.js, underwater-view.js) a call at a time — made,
// built, set and drawn. One view to a module.
//
// The drive is the plug-in's own, handed what the windscreen hands it (Windscreen::renderFrame): the gear and PARK it
// holds, the steering, what was heard of the bass, then a step and a frame. Its first frame is the app's
// (Windscreen::firstFrame, which makes the kit's posters): the world made in the drive's own first gear, then the gear
// and PARK asked for settled in and drawn with no step, the world alone.
//
// THE RENDERER is the app's UnderwaterGl over the drive's world, set up as the drive sets up its own (the cabin's
// projection, its art), but held here: the art comes from the page — the cabin's two pictures before the renderer is
// made, with it; the passing bathyscaphe's whenever it arrives, handed to the renderer already made between two frames
// (UnderwaterGl::setPassing): its texture, nothing else.
//
// THE CABIN: off unless the page asks for it (view_cabin), the world alone behind the glass as in the app's STILL —
// the page paints the cabin over it. Asked for, and its two pictures in, the panel is the renderer's last pass, as in
// the app's live frames. Never asked for, no picture of it is made and nothing of it is drawn.

#include "underwater/view/UnderwaterDrive.h"
#include "view.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

using orbitamp::underwater::UnderwaterDrive;
using orbitamp::underwater::UnderwaterGl;

namespace
{
    /** The drive and the renderer over its world. */
    struct Sea
    {
        UnderwaterDrive drive;
        UnderwaterGl gl { drive.sea() };
    };
    std::unique_ptr<Sea> sea;
    std::string trouble;            // view_error's words, kept while the page reads them

    // The gear and PARK are the windscreen's, as in the app: held here and handed to the drive before every frame and
    // every settle (Windscreen::renderFrame), so a gear asked for before the world is made does not make it there (the
    // app's first frame is made in the drive's own first gear, then settled at the gear) and a settle sees the gear it
    // is asked to settle at as a change (UnderwaterDrive::settle settles only a change).
    int gearHeld = 1;
    bool parkHeld = false, cabinHeld = false;

    // THE ART, as the page hands it over (view_picture, view_sprites): straight RGBA, the top row first.
    enum { dashboard, seaLayer, atlas, numPictures };
    std::array<UnderwaterGl::Picture, numPictures> pictures;
    std::string sprites, spritesIn;
    bool created = false, passingNew = false;

    /** What the renderer is made with: the cabin's pictures where it is asked for and both are in; the passing
        bathyscaphe's atlas, handed over once — it is the largest of them — with its table. */
    UnderwaterGl::Art art()
    {
        UnderwaterGl::Art a;
        if (cabinHeld && pictures[dashboard].valid() && pictures[seaLayer].valid())
        {
            a.dashboard = [] { return pictures[dashboard]; };
            a.sea = [] { return pictures[seaLayer]; };
        }
        if (pictures[atlas].valid() && ! sprites.empty())
        {
            a.atlas = [] { return std::move (pictures[atlas]); };
            a.sprites = sprites;
        }
        return a;
    }

    /** A sea in the drive's first gear, lit by the bass or not, its renderer in the cabin's projection. */
    std::unique_ptr<Sea> made (bool lit)
    {
        auto s = std::make_unique<Sea>();
        s->drive.setLit (lit);
        s->gl.setLens (UnderwaterDrive::tanH, UnderwaterDrive::shear);
        return s;
    }
}

extern "C"
{
    /** A drive in the bass's three gears (`gears` is the world's own: 3), lit by the bass; the gear given is held and
        settled in once the world is built. The bass has no TURBO. */
    EMSCRIPTEN_KEEPALIVE void view_make (int /*gears*/, int gear, int /*turbo*/)
    {
        sea = made (true);
        gearHeld = gear;
    }

    /** The world: the heights, the detail, the caustics and the jellyfish, tens of milliseconds — made and settled in
        the drive's own first gear, its pitch the cabin's, as UnderwaterDrive::create makes it in the app. */
    EMSCRIPTEN_KEEPALIVE void view_build()
    {
        if (! sea->drive.sea().isBuilt())
            sea->drive.settle (sea->drive.sea().gear(), false);
    }

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The renderer's programs, buffers and textures, with the art handed over so far; false when a shader would not
        build here (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view_build();               // a bench's fresh drive is built here, as the drive's own create() builds it
        sea->gl.setArt (art());
        sea->gl.setCabin (cabinHeld);
        created = sea->gl.create();
        passingNew = false;
        trouble = created ? std::string() : sea->gl.error();
        return created ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return trouble.c_str(); }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)   { gearHeld = g; }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int)    {}        // the bass has no TURBO
    /** PARK: the amp bypassed whole — the bathyscaphe stops and the near jellyfish come to the glass. */
    EMSCRIPTEN_KEEPALIVE void view_park (int on)  { parkHeld = on != 0; }
    /** Whether the cabin is drawn over the world: before the renderer is made, as its pictures are. */
    EMSCRIPTEN_KEEPALIVE void view_cabin (int on) { cabinHeld = on != 0; }

    /** Where it stands, at once as the drive would be once settled (UnderwaterDrive::settle): the speed the gear and
        PARK ask for. Only once the world is built. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        if (! sea->drive.sea().isBuilt())
            return;
        sea->drive.setPark (parkHeld);
        sea->drive.settle (gearHeld, false);
    }

    /** The bass since the last frame: the amp's input's peak, linear (1 = full scale), handed to the drive before the
        frame that steps it (view.js, view.hear) — the largest of several before a step is kept. */
    EMSCRIPTEN_KEEPALIVE void view_hear (float peak) { sea->drive.hear (peak, 0.0f); }

    /** Room for a picture of the art, w x h straight RGBA, the top row first, which the page writes in: 0 the
        dashboard, 1 its sea layer, 2 the passing bathyscaphe's atlas. Taken by the renderer as view_art says. */
    EMSCRIPTEN_KEEPALIVE std::uint8_t* view_picture (int which, int w, int h)
    {
        if (which < 0 || which >= numPictures || w <= 0 || h <= 0)
            return nullptr;
        auto& p = pictures[(size_t) which];
        p.width = w;
        p.height = h;
        p.rgba.assign ((size_t) w * (size_t) h * 4, 0);
        return p.rgba.data();
    }

    /** Room for sprites.txt as it is, `bytes` of it, which the page writes in. */
    EMSCRIPTEN_KEEPALIVE char* view_sprites (int bytes)
    {
        spritesIn.assign ((size_t) std::max (0, bytes), '\0');
        return spritesIn.data();
    }

    /** The art written is in: the cabin's, before the renderer is made, is made with it; the passing bathyscaphe's,
        once the renderer is, from the next frame on (never in the first one). */
    EMSCRIPTEN_KEEPALIVE void view_art()
    {
        if (! spritesIn.empty())
            sprites = std::move (spritesIn);
        spritesIn.clear();
        passingNew = created && pictures[atlas].valid() && ! sprites.empty();
    }

    /** One frame: the drive stepped by dt seconds with the hand on the glass at `steer`, drawn at w x h into the
        canvas — the passing bathyscaphe's art handed to the renderer first where it has come since the last. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        auto& d = sea->drive;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.setSteer (steer);
        d.step (dt);
        if (passingNew)
        {
            passingNew = false;
            sea->gl.setPassing (pictures[atlas], sprites);
            pictures[atlas] = {};       // on the GPU now: the largest of the pictures is let go
        }
        sea->gl.render (w, h, 1.0f, 0, 0, 0);
    }

    /** The world as it stands, drawn with no step. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h) { sea->gl.render (w, h, 1.0f, 0, 0, 0); }

    /** A bench's frame, set up as UnderwaterViewDemo sets its drive and then its STILL (WindscreenDemo.cpp's makeDrive,
        Windscreen::renderFrame), in its order: a fresh drive with PARK, the gear held — the demo sets them before
        anything builds the world, so it is made in that gear —; placed at (x, z, heading in degrees) if `placed`;
        driven `drive` seconds at 60 steps a second with the hand held at `steer`, the gear settled first, and then a
        step of no time; and, for `still`, settled where it stands as the app's STILL is. The sea floor has no clock:
        `clocked` is not its. Compare with the demo at --glass=<the canvas's pixels> --mode=still. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int /*clocked*/, double /*seconds*/,
                                         double /*metres*/, double driveSeconds, float steer, int still)
    {
        constexpr double pi = 3.14159265358979323846;
        sea = made (false);         // the demo's drive hears nothing unless told to play
        auto& d = sea->drive;
        d.setPark (parkHeld);
        d.setGear (gearHeld);
        if (placed)
            d.placeAt (x, z, headingDegrees * pi / 180);
        if (driveSeconds > 0)
        {
            d.setSteer (steer);
            d.settle (gearHeld, false);
            for (int i = 0, n = (int) std::lrint (driveSeconds * 60); i < n; ++i)
                d.step (1.0 / 60);
            d.step (0.0);
        }
        d.setPark (parkHeld);
        d.setSteer (steer);
        if (still)
            d.settle (gearHeld, false);
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (sea ? &sea->gl : nullptr); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (sea ? &sea->gl : nullptr);
        created = false;
    }
}
