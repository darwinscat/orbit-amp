// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The ENGL E530's rover cabin for the site: the metal world, seen from the heavy rover, drawn by the app's own C++ —
// MetalDrive, MetalWorld and MetalGl (src/metal/view) — built to WebAssembly over WebGL 2. The build puts this file at
// the end of metal-wasm.mjs, after view.js and the list of the world's files (metal-files.js, written by
// tools/worlds-wasm/CMakeLists.txt); the page imports that, and metal-wasm.wasm comes beside it.
//
//   const view = createMetalView (canvas, { files: 'metal/', gear: 1, hold: true, onFallback });
//   view.setGear (4);          // the lever: CLEAN GAIN LO, CLEAN GAIN HI, LEAD GAIN LO, LEAD GAIN HI are the gears 1 to 4
//                              // (4 is the storm) — the rover eases to that gear's speed and weather
//   view.setGear (4, { settled: true })   // ...or is at it at once; createMetalView (canvas, { gear: 4 }) starts so
//   view.setPark (on);         // PARK, the amp bypassed whole: the rover pulls onto the shoulder; setPark (false) pulls
//                              // it away (heavily) in the gear it holds
//   view.hear (peak);          // the guitar: the amp's input's peak since the last call, linear (1 = full scale), what
//                              // the IN meter shows — once a frame or so from the page's audio
//   view.cabinFlash            // the flash plate's share of the cabin now, 0..1: read it each frame to crossfade the
//                              // page's own plates or flash its glass (below)
//   view.stop(); view.start(); view.destroy();
//
// The same calls as every world's view, so a page drives it as it drives the sea floor: the lever's place as the gear,
// made held and settled at it (`hold: true`, `gear`). The E530 has no TURBO: view.setTurbo does nothing. The road
// steers: view.setSteer does nothing that shows.
//
// THE GUITAR, HEARD: an onset in its level is an attack, the world's thunder — the lightning, the pyro, the stage, the
// dragons' big flame (MetalDrive::hear; the level between -50 and -12 dBFS, worlds/Play.h, a jump over 0.28 of it, 0.14 s
// after the last). Not called, it is silence. A page's HIT button is view.hear (1) after a quiet moment.
//
// THE STAGE: the canvas lies over the cabin's whole stage (1672 x 941 in the app) and the world is seen in the cabin's
// camera — a 16 mm lens, the half-width's tangent 18/16 at any aspect, the centre at the stage's centre, the eye 2.6 m
// over the road, 5 degrees down (MetalDrive). By default the world is alone behind the glass, as in the app's posters:
// the page lays the cabin over the canvas, its window cut out.
//
// THE CABIN AND THE LIGHTNING: in the app the world draws the cabin's two plates itself — the night's and the flash's,
// crossfaded by its lightning: plate = night x (1 - f) + flash x f (premultiplied), then world x (1 - plate alpha) +
// plate. With { cabin: true } it does so here too, over the whole stage (dashboard.webp and dashboard_flash.webp are
// fetched only then, 6.4 MB). A page that lays its own cabin (the kit's cabin.webp) reads f as view.cabinFlash each
// frame (0 between strikes, said by the world after each frame it draws, so a frame behind it) and crossfades its own
// flash plate by it or lifts its glass.
//
// THE FILES: everything the app's art reader reads (src/metal/view/MetalArt.cpp), under the URL the page gives as
// `files` — a folder laid out as resources/metal is shipped (METAL_FILES below: world/ — the owner's meshes, each with
// its credit's .txt beside it, the groves' guitars and their index, the letters, the banners, the dragon —; the plates
// with the cabin). Each is fetched as the view is made, at once and all together, decoded where the world is drawn (in
// its worker, where it has one) — the WebP pictures by the browser's own decoder to straight RGBA, the colour under a
// clear alpha kept —, and handed over in two parts, each once every file of it has come or failed: world/ whole (the
// finds furnish the world in one fixed order, MetalWorld::furnish) and the two plates together. The first frame waits
// for them, `wait` milliseconds at most (10 s unless said; 0 does not wait): what has come by then the world is made
// with, and the first frame is the app's; what comes after is taken between two frames (the finds come into the world
// from then on). The world runs with what it has: a file that does not come leaves its part out (no guitars in the
// groves, no letters, a landmark not there, no cabin), said in the console, as a missing file does in the app. The
// module never holds them all at once: each is copied into it when the world reads it, and let go once read.
//
// The rest — the quality ladder, the worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` —
// is every world's, as view.js says. The first frame is the same every time for the same gear, PARK and canvas aspect:
// the world at its start, 280 m before the skull gate, made in the drive's first gear, handed the gear and PARK and
// settled as the app's first frame is, nothing stepped — the kit's posters. A bench's `test` is metal_view_demo's
// (--mode=still): { at: [x, z], drive: seconds } (metal.cpp, view_test), with own options `s` (the demo's --s=, metres
// round the loop), `strike` (--strike=, seconds before the drive's end), `park` (--park) and `cabin` (--cabin); its frame
// waits for every file. `skip: 'halffloat'` shows the sky's 8-bit fallback, as on a WebGL 2 without
// EXT_color_buffer_float (MetalGl::setSkip's list).

/** A URL of the page's, made whole so it means the same in the worker; a folder's ends in a slash. */
function metalFolder (u)
{
    if (typeof u !== 'string' || ! u) return null;
    const whole = new URL (u, document.baseURI).href;
    return whole.endsWith ('/') ? whole : whole + '/';
}

/** The part a file is handed over with: world/ whole, the two plates together. */
const metalPart = path => path.startsWith ('world/') ? 'world' : 'plates';

/** A context of its own for reading a picture back exactly: straight, the colour under a clear alpha as the file has
    it, which a 2D canvas loses. false where there is none. */
let metalReader = null;
function metalExact (bitmap)
{
    if (metalReader === null)
    {
        try
        {
            const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1, 1) : document.createElement ('canvas');
            metalReader = c.getContext ('webgl2', { premultipliedAlpha: false, antialias: false, depth: false, stencil: false, alpha: true }) || false;
        }
        catch (e) { metalReader = false; }
    }
    const gl = metalReader;
    if (! gl || gl.isContextLost ()) return null;
    const { width, height } = bitmap;
    const t = gl.createTexture (), fb = gl.createFramebuffer ();
    gl.bindTexture (gl.TEXTURE_2D, t);
    gl.pixelStorei (gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    gl.pixelStorei (gl.UNPACK_COLORSPACE_CONVERSION_WEBGL, gl.NONE);
    gl.pixelStorei (gl.UNPACK_FLIP_Y_WEBGL, false);
    gl.texImage2D (gl.TEXTURE_2D, 0, gl.RGBA8, gl.RGBA, gl.UNSIGNED_BYTE, bitmap);
    gl.bindFramebuffer (gl.FRAMEBUFFER, fb);
    gl.framebufferTexture2D (gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, t, 0);
    let data = null;
    if (gl.checkFramebufferStatus (gl.FRAMEBUFFER) === gl.FRAMEBUFFER_COMPLETE)
    {
        data = new Uint8Array (width * height * 4);           // the texture's row 0 is the picture's top row
        gl.pixelStorei (gl.PACK_ALIGNMENT, 1);
        gl.readPixels (0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, data);
    }
    gl.bindFramebuffer (gl.FRAMEBUFFER, null);
    gl.deleteFramebuffer (fb);
    gl.deleteTexture (t);
    return gl.getError () === gl.NO_ERROR ? data : null;
}

/** One of the world's files, fetched: a picture decoded to straight RGBA, the top row first ({ width, height, data }),
    anything else its bytes; null, said in the console, when it does not come. */
async function metalFile (base, path)
{
    try
    {
        const r = await fetch (base + path);
        if (! r.ok) throw new Error (r.status + ' ' + r.statusText);
        if (! path.endsWith ('.webp')) return { path, bytes: new Uint8Array (await r.arrayBuffer ()) };
        const bitmap = await createImageBitmap (await r.blob (), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
        const { width, height } = bitmap;
        let data = null;
        try { data = metalExact (bitmap); } catch (e) { data = null; }
        if (! data)
        {
            const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (width, height) : Object.assign (document.createElement ('canvas'), { width, height });
            const g = c.getContext ('2d', { willReadFrequently: true });
            g.drawImage (bitmap, 0, 0);
            data = g.getImageData (0, 0, width, height).data;
        }
        bitmap.close ();
        return { path, picture: { width, height, data } };
    }
    catch (e) { console.warn ('metal-wasm: ' + base + path + ' is left out: ' + (e && e.message || e)); return null; }
}

/** The word CENSORED as the prototype's page draws it (metal-gl.js): white bold, 190 px, centred on (512, 118) of a
    1024 x 220 clear canvas, squeezed to 990 px wide at the most; straight RGBA, the top row first. */
function metalCensored ()
{
    try
    {
        const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1024, 220) : Object.assign (document.createElement ('canvas'), { width: 1024, height: 220 });
        const g = c.getContext ('2d', { willReadFrequently: true });
        g.fillStyle = '#fff'; g.font = 'bold 190px "Helvetica Neue", Helvetica, Arial, sans-serif'; g.textAlign = 'center'; g.textBaseline = 'middle';
        g.fillText ('CENSORED', 512, 118, 990);
        return { path: 'CENSORED', picture: { width: 1024, height: 220, data: g.getImageData (0, 0, 1024, 220).data } };
    }
    catch (e) { return null; }
}

/** THE WORLD'S OWN, where it is drawn (view.js): PARK, the cabin, the renderer's skips and a bench's place, the files —
    fetched from the start and kept for the module (metal.cpp reads them from Module.metalArt) a part at a time —, and
    the lightning's share of the flash plate, said to the page after each frame. */
function metalOwn ({ files = null, cabin = false, park = false, wait = 10000, skip = '', s = -1, strike = -1, bench = false }, post)
{
    let M = null, gone = false, flashSaid = -1;
    const art = new Map ();         // what has come, by path, for the module to read; emptied once it is read
    const paths = files ? METAL_FILES.map (([path]) => path).filter (path => metalPart (path) === 'world' || cabin) : [];
    if (! files) console.warn ('metal-wasm: no `files` folder given: the world is drawn without its files');
    const parts = new Map ();
    for (const path of paths) { const k = metalPart (path); if (! parts.has (k)) parts.set (k, []); parts.get (k).push (path); }

    function hand (part)
    {
        for (const f of part) if (f) art.set (f.path, f.picture ? { picture: f.picture } : { bytes: f.bytes });
        if (M) M._view_art ();
    }
    // Every file at once; a part handed over when all of its files have come or failed (world/ with the word CENSORED).
    const all = Promise.all ([...parts.entries ()].map (([k, list]) =>
        Promise.all (list.map (path => metalFile (files, path))).then (part =>
        {
            if (gone) return;
            if (k === 'world') part.push (metalCensored ());
            hand (part);
        })));

    function put (text)
    {
        const name = new TextEncoder ().encode (text);
        const at = M._view_name (name.length);
        if (at) M.HEAPU8.set (name, at);
    }

    return {
        made (module)
        {
            M = module;
            M.metalArt = art;
            M._view_park (park ? 1 : 0);
            M._view_cabin (cabin ? 1 : 0);
            M._view_bench (+s, +strike);
            if (skip) { put (String (skip)); M._view_skip (); }
            if (art.size) M._view_art ();
            // The world is made once every file has come, or `wait` has gone by; a bench's frame waits for all.
            if (bench || ! (wait < Infinity)) return all;
            return Promise.race ([all, new Promise (r => setTimeout (r, Math.max (0, +wait || 0)))]);
        },
        said (m)
        {
            if (m.type === 'park') { park = m.on; if (M) M._view_park (park ? 1 : 0); }
        },
        frame ()
        {
            const f = M._view_flash ();
            if (f !== flashSaid && (Math.abs (f - flashSaid) >= 1 / 1024 || f === 0 || f === 1)) { flashSaid = f; post ({ type: 'flash', value: f }); }
        },
        gone () { gone = true; art.clear (); },
    };
}

/** ...and its calls, on the page; what it says back (the lightning's share) is kept for view.cabinFlash. */
function metalCalls (view, say)
{
    let flash = 0;
    /** PARK: the amp bypassed whole. */
    view.setPark = on => say ({ type: 'park', on: !! on });
    /** The flash plate's share of the cabin, 0..1, as the world said it after its last frame. */
    Object.defineProperty (view, 'cabinFlash', { get: () => flash, enumerable: true });
    return m => { if (m.type === 'flash') flash = +m.value || 0; };
}

const METAL = { name: 'metal', module: createMetalModule, ear: true, own: metalOwn, calls: metalCalls };
worldWorker (METAL);

export function createMetalView (canvas, options = {})
{
    const { files = null, cabin = false, park = false, wait = 10000, skip = '', s = -1, strike = -1, ...rest } = options;
    return createWorldView (canvas, METAL, import.meta.url,
                            { gears: 4, ...rest, own: { files: metalFolder (files), cabin: !! cabin, park: !! park, wait: +wait,
                                                        skip: String (skip || ''), s: +s, strike: +strike, bench: !! rest.test } });
}

/** The world's files as the page must serve them under `files`: [path, bytes] each (dashboard.webp and
    dashboard_flash.webp are fetched only with { cabin: true }). */
export const metalFiles = METAL_FILES.map (([path, bytes]) => ({ path, bytes }));
