// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The Orange's van cabin for the site: California, 1967, seen from the van, drawn by the app's own C++ —
// CaliforniaDrive, CaliforniaWorld and CaliforniaGl (src/california/view) — built to WebAssembly over WebGL 2. The
// build puts this file at the end of california-wasm.mjs, after view.js and the list of the world's files
// (california-files.js, written by tools/worlds-wasm/CMakeLists.txt); the page imports that, and california-wasm.wasm
// comes beside it.
//
//   const view = createCaliforniaView (canvas, { files: 'california/', gear: 1, hold: true, onFallback });
//   view.setGear (2);          // the lever: CLEAN and DIRTY are the gears 1 and 2 — the van eases to that gear's speed
//   view.setGear (2, { settled: true })   // ...or is at it at once; createCaliforniaView (canvas, { gear: 2 }) starts so
//   view.setPark (on);         // PARK, the amp bypassed whole: the van pulls onto the shoulder, the monarchs come to
//                              // the glass; CLEAN or DIRTY again (setPark (false)) pulls it away
//   view.hear (peak);          // the guitar: the amp's input's peak since the last call, linear (1 = full scale), what
//                              // the IN meter shows — once a frame or so from the page's audio
//   view.stop(); view.start(); view.destroy();
//
// The same calls as every world's view, so a page drives it as it drives the sea floor: the lever's place as the gear,
// made held and settled at it (`hold: true`, `gear`). The Orange has no TURBO: view.setTurbo does nothing. The van
// drives itself round its loop: there is no wheel to steer (view.setSteer does nothing that shows).
//
// THE GUITAR, HEARD: an onset in its level is a hit — the near poppies flare, petals and butterflies lift off, the
// pendant is kicked (CaliforniaDrive::hear; the level between -50 and -12 dBFS, worlds/Play.h). Not called, it is
// silence. A page's HIT button is view.hear (1) after a quiet moment.
//
// THE STAGE: the canvas lies over the cabin's whole stage (1672 x 941 in the app) and the world is seen in the cabin's
// camera — a 16 mm lens, the half-width's tangent 18/16 at any aspect, the centre at the stage's centre, the world
// 5 degrees down (CaliforniaDrive). By default the world is alone behind the glass, as in the app's posters: the page
// lays the cabin over the canvas, its windscreen cut out. With { cabin: true } the world draws the dashboard itself,
// over the whole stage, and the pendant over it, as in the app's live frames (dashboard.webp is fetched only then).
//
// THE PENDANT: the peace pendant under the mirror swings with the road, the curves and the hits. Drawn by the world —
// over the glass, under the page's cabin — unless { pendant: false }: the page's plate must then leave the pendant
// out (the kit's cabin.webp has it painted hanging still; a plate with the pendant's box taken from dashboard.webp
// instead lets the world's show). { pendant: false } with the kit's cabin.webp is the app's own face-painted look,
// the pendant still.
//
// THE FILES: everything the app's art reader reads (src/california/view/CaliforniaArt.cpp), under the URL the page gives
// as `files` — a folder laid out as resources/california is shipped (CALIFORNIA_FILES below: dashboard_pendant.webp and
// .txt, label.webp, finds/...; dashboard.webp too with the cabin). Each is fetched as the view is made, at once and all
// together, decoded where the world is drawn (in its worker, where it has one) — the WebP pictures by the browser's own
// decoder to straight RGBA, the colour under a clear alpha kept —, and handed to the renderer a folder at a time, once
// every file of it has come or failed. The first frame waits for them, `wait` milliseconds at most (10 s unless said;
// 0 does not wait): what has come by then the renderer is made with, and the first frame is the app's; what comes
// after is taken between two frames. The world runs with what it has: a file that does not come leaves its part out
// (a sign's face blank, a find its placeholder box), said in the console, as a missing file does in the app.
//
// THE CHALKS: the road's optional chalk drawings (the winks) are off on the site, as CaliforniaGl::Art says; { wink:
// true } puts them on, as in the app and in the kit's posters.
//
// The rest — the quality ladder, the worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` —
// is every world's, as view.js says. The first frame is the same every time for the same gear, PARK and canvas aspect:
// the world at its start, made in the drive's first gear, handed the gear and PARK and settled as the app's first frame
// is, nothing stepped — the kit's posters. A bench's `test` is california_view_demo's (--mode=still): { at: [x, z],
// drive: seconds, still: bool } (california.cpp, view_test), `park` as the demo's --park; its frame waits for every file.

/** A URL of the page's, made whole so it means the same in the worker; a folder's ends in a slash. */
function californiaFolder (u)
{
    if (typeof u !== 'string' || ! u) return null;
    const whole = new URL (u, document.baseURI).href;
    return whole.endsWith ('/') ? whole : whole + '/';
}

/** The part of the world a file belongs to, which takes it whole: its folder under finds/, else its name's stem. */
const californiaPart = path => path.startsWith ('finds/') ? path.split ('/').slice (0, 2).join ('/') : path.split ('.')[0];

/** A context of its own for reading a picture back exactly: straight, the colour under a clear alpha as the file has
    it, which a 2D canvas loses. false where there is none. */
let californiaReader = null;
function californiaExact (bitmap)
{
    if (californiaReader === null)
    {
        try
        {
            const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1, 1) : document.createElement ('canvas');
            californiaReader = c.getContext ('webgl2', { premultipliedAlpha: false, antialias: false, depth: false, stencil: false, alpha: true }) || false;
        }
        catch (e) { californiaReader = false; }
    }
    const gl = californiaReader;
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
async function californiaFile (base, path)
{
    try
    {
        const r = await fetch (base + path);
        if (! r.ok) throw new Error (r.status + ' ' + r.statusText);
        if (! path.endsWith ('.webp')) return { path, bytes: new Uint8Array (await r.arrayBuffer ()) };
        const bitmap = await createImageBitmap (await r.blob (), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
        const { width, height } = bitmap;
        let data = null;
        try { data = californiaExact (bitmap); } catch (e) { data = null; }
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
    catch (e) { console.warn ('california-wasm: ' + base + path + ' is left out: ' + (e && e.message || e)); return null; }
}

/** THE WORLD'S OWN, where it is drawn (view.js): PARK, the cabin, the pendant, the chalks, and the files, fetched from
    the start and handed to the module (california.cpp) a part at a time. */
function californiaOwn ({ files = null, cabin = false, pendant = true, park = false, wink = false, wait = 10000, bench = false })
{
    let M = null, gone = false;
    const waiting = [];             // parts come before the module is made
    const drawn = cabin || pendant;
    const paths = files ? CALIFORNIA_FILES.map (([path]) => path).filter (path =>
        path === 'dashboard.webp' ? cabin : path.startsWith ('dashboard_pendant.') ? drawn : true) : [];
    if (! files) console.warn ('california-wasm: no `files` folder given: the world is drawn without its files');
    const parts = new Map ();
    for (const path of paths) { const k = californiaPart (path); if (! parts.has (k)) parts.set (k, []); parts.get (k).push (path); }

    function put (path)
    {
        const name = new TextEncoder ().encode (path);
        const at = M._view_name (name.length);
        if (at) M.HEAPU8.set (name, at);
    }
    function hand (part)
    {
        for (const f of part)
        {
            if (! f) continue;
            put (f.path);
            const at = f.picture ? M._view_picture (f.picture.width, f.picture.height) : M._view_file (f.bytes.length);
            if (at) M.HEAPU8.set (f.picture ? f.picture.data : f.bytes, at);     // HEAPU8 read again: a room may grow the heap
        }
        M._view_art ();
    }
    // Every file at once; a part handed over when all of its files have come or failed.
    const all = Promise.all ([...parts.values ()].map (list =>
        Promise.all (list.map (path => californiaFile (files, path))).then (part =>
        {
            if (gone) return;
            if (M) hand (part); else waiting.push (part);
        })));

    return {
        made (module)
        {
            M = module;
            M._view_park (park ? 1 : 0);
            M._view_cabin (cabin ? 1 : 0);
            M._view_pendant (pendant ? 1 : 0);
            M._view_wink (wink ? 1 : 0);
            for (const part of waiting.splice (0)) hand (part);
            // The renderer is made once every file has come, or `wait` has gone by; a bench's frame waits for all.
            if (bench || ! (wait < Infinity)) return all;
            return Promise.race ([all, new Promise (r => setTimeout (r, Math.max (0, +wait || 0)))]);
        },
        said (m)
        {
            if (m.type === 'park') { park = m.on; if (M) M._view_park (park ? 1 : 0); }
        },
        gone () { gone = true; },
    };
}

/** ...and its calls, on the page. */
function californiaCalls (view, say)
{
    /** PARK: the amp bypassed whole. */
    view.setPark = on => say ({ type: 'park', on: !! on });
}

const CALIFORNIA = { name: 'california', module: createCaliforniaModule, ear: true, own: californiaOwn, calls: californiaCalls };
worldWorker (CALIFORNIA);

export function createCaliforniaView (canvas, options = {})
{
    const { files = null, cabin = false, pendant = true, park = false, wink = false, wait = 10000, ...rest } = options;
    return createWorldView (canvas, CALIFORNIA, import.meta.url,
                            { gears: 2, ...rest, own: { files: californiaFolder (files), cabin: !! cabin, pendant: !! pendant, park: !! park,
                                                        wink: !! wink, wait: +wait, bench: !! rest.test } });
}

/** The world's files as the page must serve them under `files`: [path, bytes] each (dashboard.webp is fetched only
    with { cabin: true }). */
export const californiaFiles = CALIFORNIA_FILES.map (([path, bytes]) => ({ path, bytes }));
