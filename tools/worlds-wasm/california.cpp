// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// California, 1967, for the web: the app's CaliforniaDrive — CaliforniaWorld and CaliforniaGl (src/california/view) —
// over a WebGL 2 context, driven from JS (view.js, california-view.js) a call at a time — made, built, set and drawn.
// One view to a module.
//
// The drive is the plug-in's own, handed what the windscreen hands it (Windscreen::renderFrame): the gear and PARK it
// holds, what was heard of the guitar, then a step and a frame. Its first frame is the app's (Windscreen::firstFrame,
// which makes the kit's posters): the world made in the drive's own first gear, then the gear and PARK asked for
// handed to it, settled as firstFrame settles it and drawn with no step, the world alone.
//
// THE RENDERER is the app's CaliforniaGl over the drive's world, set up as the drive sets up its own (the cabin's
// projection), but held here, with THE FILES the page hands over: each one the app's art reader (CaliforniaArt.cpp)
// would read, by the same path, decoded by the page (view_name, then view_picture or view_file). What has come when the
// renderer is made, it is made with; what comes after is handed to it between two frames (CaliforniaGl::takeArt), the
// parts already on the GPU kept. A file that never comes leaves its part out, as a missing file does in the app. Once
// taken, the files are let go: the GPU holds them.
//
// THE CABIN: off unless the page asks for it (view_cabin), the world alone behind the glass as in the app's posters —
// the page lays the cabin over it. Asked for, and the dashboard in, the panel and the pendant are the renderer's last
// passes, as in the app's live frames. THE PENDANT, while the cabin is not drawn, is swung over the glass by itself
// (view_pendant, CaliforniaGl::setPendant) unless the page says not to: a page that lays a plate without it sees it swing.

#include "california/view/CaliforniaDrive.h"
#include "view.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

using orbitamp::california::CaliforniaDrive;
using orbitamp::california::CaliforniaGl;

namespace
{
    /** The drive and the renderer over its world. */
    struct Van
    {
        CaliforniaDrive drive;
        CaliforniaGl gl { drive.california() };
    };
    std::unique_ptr<Van> van;
    std::string trouble;            // view_error's words, kept while the page reads them

    // The gear and PARK are the windscreen's, as in the app: held here and handed to the drive before every frame and
    // every settle (Windscreen::renderFrame), so a gear asked for before the world is made does not make it there (the
    // app's first frame is made in the drive's own first gear, then handed the gear and settled).
    int gearHeld = 1;
    bool parkHeld = false, cabinHeld = false, pendantHeld = true, winkHeld = false;
    bool created = false, artNew = false;

    // THE FILES, by their paths under the folder the app reads (resources/california as shipped): the pictures as the
    // page decodes them (straight RGBA, the top row first), the rest as they are. Taken by the renderer, then let go.
    std::map<std::string, CaliforniaGl::Picture> pictures;
    std::map<std::string, std::string> files;
    std::string naming;             // the path the next picture or file is written under (view_name)

    CaliforniaGl::Picture takePicture (const std::string& path)
    {
        const auto it = pictures.find (path);
        if (it == pictures.end())
            return {};
        auto p = std::move (it->second);
        pictures.erase (it);
        return p;
    }
    std::string fileText (const std::string& path)
    {
        const auto it = files.find (path);
        return it == files.end() ? std::string() : it->second;
    }
    /** The prototype's .png is our .webp, as CaliforniaArt.cpp reads it. */
    std::string webpOf (std::string name)
    {
        if (name.size() > 4 && name.compare (name.size() - 4, 4, ".png") == 0)
            name = name.substr (0, name.size() - 4) + ".webp";
        return name;
    }

    /** What the renderer reads, from what has come: CaliforniaArt.cpp's shippedArt over the files handed over instead of
        the factory folder — the dashboard only where the cabin is asked for, the pendant where it is drawn. */
    CaliforniaGl::Art art()
    {
        CaliforniaGl::Art a;
        const bool pendant = cabinHeld || pendantHeld;
        if (cabinHeld && pictures.count ("dashboard.webp") != 0)
            a.dashboard = [] { return takePicture ("dashboard.webp"); };
        if (pendant && pictures.count ("dashboard_pendant.webp") != 0)
            a.pendant = [] { return takePicture ("dashboard_pendant.webp"); };
        if (pendant)
            a.pendantBox = fileText ("dashboard_pendant.txt");
        if (pictures.count ("label.webp") != 0)
            a.label = [] { return takePicture ("label.webp"); };
        a.picture = [] (const std::string& path) { return takePicture ("finds/" + webpOf (path)); };
        a.text = [] (const std::string& path) { return fileText ("finds/" + path); };
        a.bytes = [] (const std::string& path)
        {
            const auto it = files.find ("finds/" + path);
            if (it == files.end())
                return std::vector<std::uint8_t>();
            std::vector<std::uint8_t> b (it->second.begin(), it->second.end());
            files.erase (it);
            return b;
        };
        a.wink = winkHeld;
        return a;
    }

    /** Taken: what is on the GPU now, and what was left with no part to go to, let go. */
    void letGo()
    {
        pictures.clear();
        files.clear();
        artNew = false;
    }
}

extern "C"
{
    /** A drive in the Orange's two gears (CLEAN, DIRTY: the world's own, `gears` is not its), lit by the guitar; the
        gear given is held and handed to the drive once the world is built. The Orange has no TURBO. */
    EMSCRIPTEN_KEEPALIVE void view_make (int /*gears*/, int gear, int /*turbo*/)
    {
        van = std::make_unique<Van>();
        van->drive.setLit (true);
        van->gl.setLens (CaliforniaDrive::tanH, CaliforniaDrive::shear);
        gearHeld = gear;
        created = false;
    }

    /** The world: the heights, the maps, the loop and everything on it, the shadow map — made and settled in the
        drive's own first gear, its pitch the cabin's, as the drive makes it in the app. */
    EMSCRIPTEN_KEEPALIVE void view_build() { van->drive.make(); }

    /** The WebGL 2 context, made current; false when the browser has none to give. */
    EMSCRIPTEN_KEEPALIVE int view_context (int keep) { return worlds::open (keep) ? 1 : 0; }

    /** The renderer's programs, buffers and textures, with the files handed over so far; false when a shader would not
        build here (view_error says why). */
    EMSCRIPTEN_KEEPALIVE int view_create()
    {
        view_build();               // a bench's fresh drive is built here, as the drive's own create() builds it
        van->gl.setCabin (cabinHeld);
        van->gl.setPendant (pendantHeld);
        van->gl.setArt (art());
        created = van->gl.create();
        letGo();
        trouble = created ? std::string() : van->gl.error();
        return created ? 1 : 0;
    }

    EMSCRIPTEN_KEEPALIVE const char* view_error() { return trouble.c_str(); }

    EMSCRIPTEN_KEEPALIVE void view_gear (int g)   { gearHeld = std::clamp (g, 1, 2); }
    EMSCRIPTEN_KEEPALIVE void view_turbo (int)    {}        // the Orange has no TURBO
    /** PARK: the amp bypassed whole — the van pulls onto the shoulder and the monarchs come to the glass. */
    EMSCRIPTEN_KEEPALIVE void view_park (int on)  { parkHeld = on != 0; }
    /** Whether the cabin is drawn over the world: before the renderer is made, as its dashboard is fetched. */
    EMSCRIPTEN_KEEPALIVE void view_cabin (int on) { cabinHeld = on != 0; }
    /** Whether the pendant swings over the glass while the cabin is not drawn: before the renderer is made. */
    EMSCRIPTEN_KEEPALIVE void view_pendant (int on) { pendantHeld = on != 0; }
    /** The optional chalks on the road (CaliforniaGl::Art::wink): before the renderer is made. */
    EMSCRIPTEN_KEEPALIVE void view_wink (int on)  { winkHeld = on != 0; }

    /** Where it stands, at once. Before the first frame as Windscreen::firstFrame settles the drive that makes the kit's
        posters — the gear and PARK handed to it, the wheel straight, then settled at the gear —; after it, the lever
        moved and settled: the speed and the place the gear and PARK ask for. Only once the world is built. */
    EMSCRIPTEN_KEEPALIVE void view_settle()
    {
        auto& d = van->drive;
        if (! d.california().isBuilt())
            return;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        if (! created)
        {
            d.setSteer (0.0f);
            d.settle (gearHeld, false);
        }
        else
            d.california().settle();
    }

    /** The guitar since the last frame: the amp's input's peak, linear (1 = full scale), handed to the drive before
        the frame that steps it (view.js, view.hear): an onset in its level is a hit (CaliforniaDrive::hear). */
    EMSCRIPTEN_KEEPALIVE void view_hear (float peak) { van->drive.hear (peak, 0.0f); }

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

    /** Room for the file named last as it is, `bytes` of it (a table, a mesh), which the page writes in. */
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

    /** One frame: the drive stepped by dt seconds with the wheel at `steer` (the van drives itself; the page has no
        wheel to give), drawn at w x h into the canvas — the files that have come since the last handed to the
        renderer first. */
    EMSCRIPTEN_KEEPALIVE void view_frame (int w, int h, double dt, float steer)
    {
        auto& d = van->drive;
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.setSteer (steer);
        d.step (dt);
        if (artNew && created)
        {
            van->gl.takeArt (art());
            letGo();
        }
        van->gl.render (w, h, 1.0f, 0, 0, 0);
    }

    /** The world as it stands, drawn with no step — the files that have come since the last frame handed over first. */
    EMSCRIPTEN_KEEPALIVE void view_draw (int w, int h)
    {
        if (artNew && created)
        {
            van->gl.takeArt (art());
            letGo();
        }
        van->gl.render (w, h, 1.0f, 0, 0, 0);
    }

    /** A bench's frame, set up as california_view_demo sets its drive and then its STILL (WindscreenDemo.cpp's
        makeDrive, Windscreen::renderFrame), in its order: a fresh drive with PARK and the gear held — set before
        anything builds the world, so it is made in them —; placed at the loop's point nearest (x, z) if `placed`;
        driven `drive` seconds at 60 steps a second, then a step of no time; and, for `still`, settled where it stands
        as the app's STILL is. The van keeps no clock and has no wheel: `clocked` and `steer` are not its. Compare
        with the demo at --glass=<the canvas's pixels> --mode=still. */
    EMSCRIPTEN_KEEPALIVE void view_test (int placed, double x, double z, double headingDegrees, int /*clocked*/, double /*seconds*/,
                                         double /*metres*/, double driveSeconds, float /*steer*/, int still)
    {
        van = std::make_unique<Van>();
        van->gl.setLens (CaliforniaDrive::tanH, CaliforniaDrive::shear);
        auto& d = van->drive;           // the demo's drive hears nothing unless told to play
        d.setPark (parkHeld);
        d.setGear (gearHeld);
        if (placed)
            d.placeAt (x, z, headingDegrees);
        if (driveSeconds > 0)
        {
            d.make();
            for (int i = 0, n = (int) std::lrint (driveSeconds * 60); i < n; ++i)
                d.step (1.0 / 60);
            d.step (0.0);
        }
        d.setGear (gearHeld);
        d.setPark (parkHeld);
        d.setSteer (0.0f);
        if (still)
            d.settle (gearHeld, false);
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them: what the app's STILL is made of. */
    EMSCRIPTEN_KEEPALIVE const std::uint8_t* view_read() { return worlds::read (van ? &van->gl : nullptr); }
    EMSCRIPTEN_KEEPALIVE int view_read_width()           { return worlds::frameW; }
    EMSCRIPTEN_KEEPALIVE int view_read_height()          { return worlds::frameH; }

    /** Everything the GPU holds, and the context. */
    EMSCRIPTEN_KEEPALIVE void view_destroy()
    {
        worlds::close (van ? &van->gl : nullptr);
        created = false;
        letGo();
    }
}
