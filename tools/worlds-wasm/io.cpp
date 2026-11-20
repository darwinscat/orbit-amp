// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// Io for the web: the app's IoDrive — IoWorld and IoGl (src/io/view) — over a WebGL 2 context, driven from JS (view.js,
// io-view.js) a call at a time — made, built, set and drawn. One view to a module.
//
// The drive is the plug-in's own, handed what the windscreen hands it (Windscreen::renderFrame): the gear and PARK it
// holds, what was heard of the bass, then a step and a frame; and the amp's three knobs as the face hands them
// (IoEditor::tellWorld: DRIVE, BLEND, PRESENCE, each 0..1), taken by the world at its next step. Its first frame is the
// app's (Windscreen::firstFrame, which makes the kit's posters): the world made in the drive's own first gear under the
// knobs given, then the gear and PARK asked for handed to it, settled as firstFrame settles it and drawn with no step,
// the world alone. How dark the cabin is (IoDrive::cabinDark, the dark plate's share) is read after every frame.
//
// THE RENDERER is the app's IoGl over the drive's world, set up as the drive sets up its own (the cabin's projection),
// but held here, with THE FILES the page hands over: each one the app's art reader (IoArt.cpp) would read, by the same
// path under resources/io, decoded by the page (view_name, then view_picture or view_file). What has come when the
// renderer is made, it is made with; what comes after is handed to it between two frames (IoGl::takeArt), the parts
// already on the GPU kept. A file that never comes leaves its part out, as a missing file does in the app. Once taken,
// the files are let go: the GPU holds them.
//
// THE CABIN: off unless the page asks for it (view_cabin), the world alone behind the glass as in the app's posters —
// the page lays the cabin over it. Asked for, and both plates in, the day's and the dark's plates are crossfaded by the
// world as the Sun goes and the dosimeter's needle is drawn on its dial, as in the app's live frames. THE NEEDLE, while
// the cabin is not drawn, is drawn on the day plate's dial in the dial's box by itself (view_needle, IoGl::setNeedle)
// unless the page says not to: a page that lays a plate with the dial's face cut out of it sees the needle move.

#include "io/view/IoDrive.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

using orbitamp::io::IoDrive;
using orbitamp::io::IoGl;

namespace
{
    /** The drive and the renderer over its world. */
    struct Rover
    {
        IoDrive drive;
        IoGl gl { drive.io() };
    };
    std::unique_ptr<Rover> rover;
    std::string trouble;            // view_error's words, kept while the page reads them

    // The gear and PARK are the windscreen's, as in the app: held here and handed to the drive before every frame and
    // every settle (Windscreen::renderFrame), so a gear asked for before the world is made does not make it there (the
    // app's first frame is made in the drive's own first gear, then handed the gear and settled).
    int gearHeld = 1;
    bool parkHeld = false, cabinHeld = false, needleHeld = true;
    double knobsHeld[3] { -1, -1, -1 };     // the knobs as the page last gave them (-1: not given), for a bench's fresh drive
    bool created = false, artNew = false;

    // THE FILES, by their paths under the folder the app reads (resources/io as shipped): the pictures as the page
    // decodes them (straight RGBA, the top row first), the rest as they are. Taken by the renderer, then let go.
    std::map<std::string, IoGl::Picture> pictures;
    std::map<std::string, std::string> files;
    std::string naming;             // the path the next picture or file is written under (view_name)

    IoGl::Picture takePicture (const std::string& path)
    {
        const auto it = pictures.find (path);
        if (it == pictures.end())
            return {};
        auto p = std::move (it->second);
        pictures.erase (it);
        return p;
    }

    /** What the renderer reads, from what has come: IoArt.cpp's shippedArt over the files handed over instead of the
        factory folder — the day's plate where the cabin or the needle is drawn, the dark's only with the cabin; the
        finds as shippedArt lists their folder (each <variant>3d folder's first mesh, the folders in their names'
        order), of those whose mesh has come. */
    IoGl::Art art()
    {
        IoGl::Art a;
        if ((cabinHeld || needleHeld) && pictures.count ("dashboard.webp") != 0)
            a.day = [] { return takePicture ("dashboard.webp"); };
        if (cabinHeld && pictures.count ("dashboard_dark.webp") != 0)
            a.dark = [] { return takePicture ("dashboard_dark.webp"); };
        a.finds = []
        {
            std::vector<std::string> out;
            std::string folder;
            for (const auto& [path, bytes] : files)         // sorted by path: the folders in their names' order
            {
                if (path.compare (0, 6, "finds/") != 0 || path.size() < 11 || path.compare (path.size() - 5, 5, ".mesh") != 0)
                    continue;
                const std::string rest = path.substr (6);
                const auto slash = rest.find ('/');
                if (slash == std::string::npos || rest.find ('/', slash + 1) != std::string::npos)
                    continue;
                const auto dir = rest.substr (0, slash);
                if (dir.size() < 2 || dir.compare (dir.size() - 2, 2, "3d") != 0 || dir == folder)
                    continue;               // a folder's first mesh only
                folder = dir;
                out.push_back (rest.substr (0, rest.size() - 5));
            }
            return out;
        };
        a.text = [] (const std::string& path)
        {
            const auto it = files.find ("finds/" + path);
            return it == files.end() ? std::string() : it->second;
        };
        a.bytes = [] (const std::string& path)
        {
            const auto it = files.find ("finds/" + path);
            if (it == files.end())
                return std::vector<std::uint8_t>();
            std::vector<std::uint8_t> b (it->second.begin(), it->second.end());
            files.erase (it);
            return b;
        };
        return a;
    }

    /** Taken: what is on the GPU now, and what was left with no part to go to, let go. */
    void letGo()
    {
        pictures.clear();
        files.clear();
        artNew = false;
    }

    void takeNew()
    {
        if (artNew && created)
        {
            rover->gl.takeArt (art());
            letGo();
        }
    }
}

extern "C"
{
    /** A drive in the RBI's two gears (CRUISE SLOW, CRUISE FAST: the world's own, `gears` is not its), lit by the bass;
        the gear given is held and handed to the drive once the world is built. The RBI has no TURBO. */
    EMSCRIPTEN_KEEPALIVE void view_make (int /*gears*/, int gear, int /*turbo*/)
    {
        rover = std::make_unique<Rover>();
        rover->drive.setLit (true);
        rover->gl.setLens (IoDrive::tanH, IoDrive::shear);
        gearHeld = std::clamp (gear, 1, orbitamp::io::IoWorld::numGears);
        created = false;
    }

    /** The world: the loop, the ground, the volcanoes, the hardware — made and settled in the drive's own first gear under
        the knobs given so far, its pitch the cabin's, as the drive makes it in the app. */
    EMSCRIPTEN_KEEPALIVE void view_build() { rover->drive.make(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. The scene's frame keeps the light
        above 1 until the post pass's knee in R11F_G11F_B10F (IoGl::ensureFrame), which WebGL 2 draws into only with
        EXT_color_buffer_float, as the prototype has it; without it the frame is RGBA8 and the highlights clip before
        the knee, as IoGl falls back to in the app. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep)
    {
        if (! worlds::open (keep))
            return 0;
        emscripten_webgl_enable_extension (worlds::context, "EXT_color_buffer_float");
        return 1;
    }

    /** The renderer's programs, buffers and textures, with the files handed over so far; false when a shader would not
        build here (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view_build();               // a bench's fresh drive is built here, as the drive's own create() builds it
        rover->gl.setCabin (cabinHeld);
        rover->gl.setNeedle (needleHeld);
        rover->gl.setArt (art());
        created = rover->gl.create();
        letGo();
        trouble = created ? std::string() : rover->gl.error();
        return created ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return trouble.c_str(); }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)   { gearHeld = std::clamp (g, 1, orbitamp::io::IoWorld::numGears); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int)    {}        // the RBI has no TURBO
    /** PARK: the amp bypassed whole — the rover pulls onto the shoulder; the world goes on as it is. */
    EMSCRIPTEN_KEEPALIVE void view_park (int on)  { parkHeld = on != 0; }
    /** Whether the cabin is drawn over the world: before the renderer is made, as its plates are fetched. */
    EMSCRIPTEN_KEEPALIVE void view_cabin (int on) { cabinHeld = on != 0; }
    /** Whether the dial and its needle are drawn while the cabin is not: before the renderer is made. */
    EMSCRIPTEN_KEEPALIVE void view_needle (int on) { needleHeld = on != 0; }

    /** THE AMP'S KNOBS, 0..1: 0 DRIVE (the time of day), 1 BLEND (Jupiter's phase), 2 PRESENCE (the sulphur snow) —
        the world is made under them if not made yet, else eases toward them from its next step (IoDrive::setDrive...). */
    EMSCRIPTEN_KEEPALIVE void view_knob (int which, double value)
    {
        if (! rover || ! (value == value) || which < 0 || which > 2)
            return;
        knobsHeld[which] = std::clamp (value, 0.0, 1.0);
        switch (which)
        {
            case 0: rover->drive.setDrive (value); break;
            case 1: rover->drive.setBlend (value); break;
            case 2: rover->drive.setPresence (value); break;
            default: break;
        }
    }

    /** How dark the cabin is, 0 (the day's plate) .. 1 (the dark's): IoDrive::cabinDark as the last frame left it, for
        the page's night glow. */
    EMSCRIPTEN_KEEPALIVE float view_dark() { return rover ? rover->drive.cabinDark() : 0.0f; }

    /** Where it stands, at once. Before the first frame as Windscreen::firstFrame settles the drive that makes the kit's
        posters — the gear and PARK handed to it, then settled at the gear —; after it, the lever moved and settled: a
        gear changed is settled at once (IoDrive::settle). Only once the world is built. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        auto& d = rover->drive;
        if (! d.io().isBuilt())
            return;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.setSteer (0.0f);
        d.settle (gearHeld, false);
    }

    /** The bass since the last frame: the amp's input's peak, linear (1 = full scale), handed to the drive before the
        frame that steps it (view.js, view.hear): an onset in its level is a hit (IoDrive::hear). */
    EMSCRIPTEN_KEEPALIVE void view_hear (float peak) { rover->drive.hear (peak, 0.0f); }

    /** The path, `bytes` of UTF-8 (no terminator), the next picture or file is written under: the page writes it in. */
    EMSCRIPTEN_KEEPALIVE char* view_name (int bytes)
    {
        naming.assign ((size_t) std::max (0, bytes), '\0');
        return naming.data();
    }

    /** Room for the picture named last, w x h straight RGBA, the top row first, which the page writes in. */
    EMSCRIPTEN_KEEPALIVE std::uint8_t* view_picture (int w, int h)
    {
        if (naming.empty() || w <= 0 || h <= 0)
            return nullptr;
        auto& p = pictures[naming];
        p.width = w;
        p.height = h;
        p.rgba.assign ((size_t) w * (size_t) h * 4, 0);
        return p.rgba.data();
    }

    /** Room for the file named last as it is, `bytes` of it (a mesh, a table), which the page writes in. */
    EMSCRIPTEN_KEEPALIVE char* view_file (int bytes)
    {
        if (naming.empty())
            return nullptr;
        auto& f = files[naming];
        f.assign ((size_t) std::max (0, bytes), '\0');
        return f.data();
    }

    /** What was written is in: before the renderer is made, it is made with it; once it is, taken between this frame
        and the next. */
    EMSCRIPTEN_KEEPALIVE void view_art() { artNew = true; }

    /** One frame: the drive stepped by dt seconds (the rover drives itself; `steer` is not its), drawn at w x h into the
        canvas — the files that have come since the last handed to the renderer first. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float /*steer*/)
    {
        auto& d = rover->drive;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.step (dt);
        takeNew();
        rover->gl.render (w, h, 1.0f, 0, 0, 0);
    }

    /** The world as it stands, drawn with no step — the files that have come since the last frame handed over first. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h)
    {
        takeNew();
        rover->gl.render (w, h, 1.0f, 0, 0, 0);
    }

    /** A bench's frame, set up as io_view_demo sets its drive and then its STILL (WindscreenDemo.cpp's makeDrive,
        Windscreen::renderFrame), in its order: a fresh drive (it hears nothing) with PARK, the gear and the knobs held —
        set before anything builds the world, so it is made in them —, made; put at the loop's point nearest (x, z) if
        `placed`; driven `drive` seconds at 60 steps a second; and, for `still`, settled where it stands as the app's
        STILL is. The rover keeps no clock and has no wheel: `clocked`, `seconds`, `metres` and `steer` are not its.
        Compare with the demo at --glass=<the canvas's pixels> --mode=still --knobs=<the same>. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int /*clocked*/, double /*seconds*/,
                                         double /*metres*/, double driveSeconds, float /*steer*/, int still)
    {
        rover = std::make_unique<Rover>();
        rover->gl.setLens (IoDrive::tanH, IoDrive::shear);
        auto& d = rover->drive;
        d.setPark (parkHeld);
        d.setGear (gearHeld);
        if (knobsHeld[0] >= 0) d.setDrive (knobsHeld[0]);
        if (knobsHeld[1] >= 0) d.setBlend (knobsHeld[1]);
        if (knobsHeld[2] >= 0) d.setPresence (knobsHeld[2]);
        d.make();
        if (placed)
            d.placeAt (x, z, headingDegrees);
        for (int i = 0, n = (int) std::lrint (std::max (0.0, driveSeconds) * 60); i < n; ++i)
            d.step (1.0 / 60);
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        if (still)
            d.settle (gearHeld, false);
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (rover ? &rover->gl : nullptr); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (rover ? &rover->gl : nullptr);
        created = false;
        letGo();
    }
}
