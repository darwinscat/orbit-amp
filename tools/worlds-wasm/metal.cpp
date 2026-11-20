// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The metal world, the ENGL E530's, for the web: the app's MetalDrive — MetalWorld and MetalGl (src/metal/view) — over
// a WebGL 2 context, driven from JS (view.js, metal-view.js) a call at a time — made, built, set and drawn. One view to
// a module.
//
// The drive is the plug-in's own, handed what the windscreen hands it (Windscreen::renderFrame): the gear and PARK it
// holds, what was heard of the guitar, then a step and a frame. Its first frame is the app's (Windscreen::firstFrame,
// which makes the kit's posters): a fresh drive made with the art, created — the world made, furnished and settled in
// its own first gear —, then handed the gear, PARK and the cabin asked for and settled as firstFrame settles it, drawn
// with no step.
//
// THE FILES: the drive is made with the art the app's art reader (MetalArt.cpp) would read, by the same paths, from what
// the page has handed over so far (metal-view.js keeps them, decoded, where the world is drawn; each read here copies
// the one asked for into the module, as the app reads a file when it is asked for, so the module never holds them all
// at once). A MetalDrive reads its finds as it is made, so it is made when the renderer is (view_create), with what has
// come by then; what comes after is taken between two frames (MetalDrive::takeArt: the finds furnish the world from
// then on, the renderer takes what it has not got). A file that never comes leaves its part out, as a missing file
// does in the app. Once taken, the files are let go: the GPU holds them.
//
// THE CABIN: off unless the page asks for it (view_cabin), the world alone behind the glass as in the app's posters —
// the page lays the cabin over it and reads the lightning's share of the flash plate each frame (view_flash). Asked
// for, and both plates in, the renderer draws them in its post pass, crossfaded by the lightning, as in the app.

#include "metal/view/MetalDrive.h"
#include "view.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

using orbitamp::metal::MetalDrive;
using orbitamp::metal::MetalGl;

// What the page has handed over and the renderer has not taken yet (metal-view.js keeps it in Module.metalArt, a Map
// of path to { picture: { width, height, data } } or { bytes }): a picture's width and height, a file's length and 0,
// -1 when it is not there; copied into the module when asked for; the paths under a folder; let go once taken.
EM_JS (int, metal_art_width, (const char* path), {
    const f = Module['metalArt'] && Module['metalArt'].get (UTF8ToString (path));
    return f ? (f.picture ? f.picture.width : f.bytes.length) : -1;
});
EM_JS (int, metal_art_height, (const char* path), {
    const f = Module['metalArt'] && Module['metalArt'].get (UTF8ToString (path));
    return f && f.picture ? f.picture.height : 0;
});
EM_JS (void, metal_art_copy, (const char* path, std::uint8_t* to), {
    const f = Module['metalArt'].get (UTF8ToString (path));
    HEAPU8.set (f.picture ? f.picture.data : f.bytes, to);
});
EM_JS (int, metal_art_paths_length, (), {
    Module['metalPaths'] = new TextEncoder ().encode (Module['metalArt'] ? [...Module['metalArt'].keys ()].join (String.fromCharCode (10)) : "");
    return Module['metalPaths'].length;
});
EM_JS (void, metal_art_paths_copy, (char* to), {
    HEAPU8.set (Module['metalPaths'], to);
    Module['metalPaths'] = null;
});
EM_JS (void, metal_art_let_go, (), {
    if (Module['metalArt']) Module['metalArt'].clear ();
});

namespace
{
    std::unique_ptr<MetalDrive> rover;
    std::string trouble;            // view_error's words, kept while the page reads them

    // The gear and PARK are the windscreen's, as in the app: held here and handed to the drive before every frame and
    // every settle (Windscreen::renderFrame).
    int gearHeld = 1;
    bool parkHeld = false, cabinHeld = false;
    bool created = false, artNew = false;
    std::string naming;             // what the page wrote last (view_name)
    std::string skipHeld;           // the renderer's parts left out (view_skip)

    /** A bench's frame (view_test), set up when the drive is made: metal_view_demo's makeDrive and STILL. */
    struct Bench
    {
        bool on = false, placed = false, still = false;
        double x = 0, z = 0, heading = 0, drive = 0;
        double loopAt = -1, strike = -1;        // the demo's --s= and --strike= (view_bench)
    } bench;

    std::vector<std::string> pathsNow()
    {
        std::string all ((size_t) metal_art_paths_length(), '\0');
        if (! all.empty())
            metal_art_paths_copy (all.data());
        std::vector<std::string> out;
        for (size_t at = 0; at <= all.size() && ! all.empty();)
        {
            const size_t end = std::min (all.find ('\n', at), all.size());
            out.push_back (all.substr (at, end - at));
            at = end + 1;
        }
        return out;
    }

    /** A picture handed over, copied into the module: straight RGBA, the top row first; invalid when it has not come. */
    MetalGl::Picture pictureOf (const std::string& path)
    {
        MetalGl::Picture p;
        const int w = metal_art_width (path.c_str()), h = metal_art_height (path.c_str());
        if (w <= 0 || h <= 0)
            return p;
        p.width = w;
        p.height = h;
        p.rgba.resize ((size_t) w * (size_t) h * 4);
        metal_art_copy (path.c_str(), p.rgba.data());
        return p;
    }

    /** What the drive reads, from what has come: MetalArt.cpp's shippedArt over the files handed over instead of the
        factory folder. A folder's names are those of what has come under it (files and folders), sorted as the app
        sorts them; a picture's size is its decoded one; the word CENSORED is drawn by the page, as the prototype's
        page draws it on a canvas, and handed over beside the files. */
    MetalGl::Art art()
    {
        MetalGl::Art a;
        a.bytes = [] (const std::string& path)
        {
            std::vector<std::uint8_t> b;
            const int n = metal_art_width (path.c_str());
            if (n > 0 && metal_art_height (path.c_str()) == 0)
            {
                b.resize ((size_t) n);
                metal_art_copy (path.c_str(), b.data());
            }
            return b;
        };
        a.list = [] (const std::string& folder)
        {
            std::set<std::string> names;
            const std::string head = folder + "/";
            for (const auto& p : pathsNow())
                if (p.compare (0, head.size(), head) == 0)
                    names.insert (p.substr (head.size(), p.find ('/', head.size()) - head.size()));
            return std::vector<std::string> (names.begin(), names.end());
        };
        a.picture = pictureOf;
        a.pictureSize = [] (const std::string& path)
        {
            const int h = metal_art_height (path.c_str());
            return h > 0 ? std::array<int, 2> { metal_art_width (path.c_str()), h } : std::array<int, 2> { 0, 0 };
        };
        a.censored = [] { return pictureOf ("CENSORED"); };
        return a;
    }

    /** What has come since the last frame, taken: what is on the GPU now, the rest let go. */
    void takeNew()
    {
        if (! artNew || ! created)
            return;
        rover->takeArt (art());
        metal_art_let_go();
        artNew = false;
    }
}

extern "C"
{
    /** A drive in the E530's four gears (CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO, LEAD GAIN HI: the world's own,
        `gears` is not its) at the gear given, held and handed to the drive once it is made. The E530 has no TURBO. */
    EMSCRIPTEN_KEEPALIVE void view_make (int /*gears*/, int gear, int /*turbo*/)
    {
        rover.reset();
        gearHeld = std::clamp (gear, 1, orbitamp::metal::MetalWorld::numGears);
        created = false;
        bench = {};
    }

    /** The world is made with the drive, once its finds are known (view_create): nothing to build before. */
    EMSCRIPTEN_KEEPALIVE void view_build() {}

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The drive, made with the files handed over so far, and its renderer — then, as Windscreen::firstFrame sets a
        fresh drive, the gear, PARK and the cabin handed to it and settled; or a bench's frame set up as
        metal_view_demo sets its drive (view_test). False when a shader would not build here (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        rover = std::make_unique<MetalDrive> (art());
        auto& d = *rover;
        d.renderer().setSkip (skipHeld);
        if (bench.on)
        {
            // metal_view_demo's makeDrive, in its order: PARK, the loop's place, the gear, the place, the drive and
            // the attack struck that long before its end.
            d.setPark (parkHeld);
            if (bench.loopAt >= 0)
            {
                d.settle (gearHeld, false);
                d.metal().startAt (bench.loopAt);
            }
            d.setGear (gearHeld);
            if (bench.placed)
                d.placeAt (bench.x, bench.z, bench.heading);
            if (bench.drive > 0)
            {
                const bool striking = bench.strike >= 0 && bench.strike < bench.drive;
                d.settle (gearHeld, false);
                for (int i = 0, n = (int) std::lrint ((striking ? bench.drive - bench.strike : bench.drive) * 60); i < n; ++i)
                    d.step (1.0 / 60);
                if (striking)
                {
                    d.metal().hit (1.0);
                    for (int i = 0, n = (int) std::lrint (bench.strike * 60); i < n; ++i)
                        d.step (1.0 / 60);
                }
            }
        }
        else
            d.setLit (true);
        created = d.create();
        metal_art_let_go();
        artNew = false;
        trouble = created ? std::string() : d.error();
        if (! created)
            return 0;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.setCabin (cabinHeld);
        d.settle (gearHeld, false);
        return 1;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return trouble.c_str(); }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)   { gearHeld = std::clamp (g, 1, orbitamp::metal::MetalWorld::numGears); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int)    {}        // the E530 has no TURBO
    /** PARK: the amp bypassed whole — the rover pulls onto the shoulder. */
    EMSCRIPTEN_KEEPALIVE void view_park (int on)  { parkHeld = on != 0; }
    /** Whether the world draws the cabin's two plates over itself: before the renderer is made, as they are fetched. */
    EMSCRIPTEN_KEEPALIVE void view_cabin (int on) { cabinHeld = on != 0; if (rover) rover->setCabin (cabinHeld); }

    /** Before the first frame, nothing: the drive is made and settled with it (view_create). After it, the lever moved
        and settled: the speed, the storm and the calm where the gear and PARK are taking them, at once. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        if (! created)
            return;
        rover->setGear (gearHeld);
        rover->setPark (parkHeld);
        rover->metal().settle();
    }

    /** The guitar since the last frame: the amp's input's peak, linear (1 = full scale), handed to the drive before
        the frame that steps it (view.js, view.hear): an onset in its level is an attack, the world's thunder
        (MetalDrive::hear). */
    EMSCRIPTEN_KEEPALIVE void view_hear (float peak) { if (rover) rover->hear (peak, 0.0f); }

    /** The flash plate's share of the cabin this frame, 0..1: the world's lightning (MetalWorld::cabinFlash), what the
        renderer crossfades its two plates by — for a page that lays the cabin itself. */
    EMSCRIPTEN_KEEPALIVE float view_flash() { return rover && created ? (float) rover->metal().cabinFlash() : 0.0f; }

    /** Room for `bytes` of UTF-8 (no terminator) the page writes in: what view_skip reads. */
    EMSCRIPTEN_KEEPALIVE char* view_name (int bytes)
    {
        naming.assign ((size_t) std::max (0, bytes), '\0');
        return naming.data();
    }

    /** The renderer's parts left out, as written last (view_name): MetalGl::setSkip's list — `halffloat` takes a
        half-float frame as not drawable, as on a WebGL 2 without EXT_color_buffer_float, to see the 8-bit fallback. */
    EMSCRIPTEN_KEEPALIVE void view_skip()
    {
        skipHeld = naming;
        if (rover) rover->renderer().setSkip (skipHeld);
    }

    /** A bench's own beyond view_test's: metal_view_demo's --s= (metres round the loop; negative: its own start) and
        --strike= (seconds before the drive's end an attack is struck; negative: none). */
    EMSCRIPTEN_KEEPALIVE void view_bench (double loopAt, double strike)
    {
        bench.loopAt = loopAt;
        bench.strike = strike;
    }

    /** Something new was handed over (metal-view.js): before the drive is made, it is made with it; once it is, taken
        between this frame and the next. */
    EMSCRIPTEN_KEEPALIVE void view_art() { artNew = true; }

    /** One frame: the drive stepped by dt seconds (the road steers: `steer` is not its), drawn at w x h into the
        canvas — the files that have come since the last handed to it first. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float /*steer*/)
    {
        rover->setGear (gearHeld);
        rover->setPark (parkHeld);
        rover->step (dt);
        takeNew();
        rover->render (w, h, 1.0f, 0, 0, 0);
    }

    /** The world as it stands, drawn with no step — the files that have come since the last frame handed over first. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h)
    {
        takeNew();
        rover->render (w, h, 1.0f, 0, 0, 0);
    }

    /** A bench's frame, set up as metal_view_demo sets its drive and then its STILL (WindscreenDemo.cpp's makeDrive,
        Windscreen::firstFrame), when the drive is made (view_create, once every file has come): PARK and the gear
        held; put at the loop's point nearest (x, z) if `placed`; driven `drive` seconds at 60 steps a second, an
        attack struck view_bench's seconds before the end; then created, handed the gear, PARK and the cabin and
        settled. The rover keeps no clock and has no wheel: `clocked` and `steer` are not its; `still` is always so.
        Compare with the demo at --glass=<the canvas's pixels> --mode=still. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int /*clocked*/, double /*seconds*/,
                                         double /*metres*/, double driveSeconds, float /*steer*/, int still)
    {
        bench.on = true;
        bench.placed = placed != 0;
        bench.x = x; bench.z = z; bench.heading = headingDegrees;
        bench.drive = driveSeconds;
        bench.still = still != 0;
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (rover ? &rover->renderer() : nullptr); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (rover ? &rover->renderer() : nullptr);
        created = false;
        metal_art_let_go();
        artNew = false;
    }
}
