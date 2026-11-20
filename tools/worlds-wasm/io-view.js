// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The SansAmp RBI bass's rover cabin for the site: Io, Jupiter's volcanic moon, seen from the rover, drawn by the app's
// own C++ — IoDrive, IoWorld and IoGl (src/io/view) — built to WebAssembly over WebGL 2. The build puts this file at the
// end of io-wasm.mjs, after view.js and the list of the world's files (io-files.js, written by
// tools/worlds-wasm/CMakeLists.txt); the page imports that, and io-wasm.wasm comes beside it.
//
//   const view = createIoView (canvas, { files: 'io/', gear: 1, hold: true, drive: 0.5, blend: 0.8, presence: 0.2 });
//   view.setGear (2);          // the lever: CRUISE SLOW and CRUISE FAST are the gears 1 and 2 — the rover eases to that
//                              // gear's speed
//   view.setGear (2, { settled: true })   // ...or is at it at once; createIoView (canvas, { gear: 2 }) starts so
//   view.setPark (on);         // PARK, the amp bypassed whole: the rover pulls onto the shoulder (the world goes on);
//                              // SLOW or FAST again (setPark (false)) pulls it away
//   view.setDrive (v); view.setBlend (v); view.setPresence (v);   // the amp's knobs, each 0..1 over its range: the time
//                              // of day, Jupiter's phase, the sulphur snow — eased by the world
//   view.cabinDark             // how dark the cabin is, 0 (the day's plate) .. 1 (the dark's), as the last frame left
//                              // it: for the page's night glow, read each frame (or { onDark (share) } when it changes)
//   view.hear (peak);          // the bass: the amp's input's peak since the last call, linear (1 = full scale), what
//                              // the IN meter shows — once a frame or so from the page's audio
//   view.stop(); view.start(); view.destroy();
//
// The same calls as every world's view, so a page drives it as it drives the sea floor: the lever's place as the gear,
// made held and settled at it (`hold: true`, `gear`). The RBI has no TURBO: view.setTurbo does nothing. The rover
// drives itself round its loop: there is no wheel to steer (view.setSteer does nothing that shows).
//
// THE KNOBS DRIVE THE WORLD (IoDrive): DRIVE (preamp_gain / 10) is the time of day, 0 the Sun at its highest, under the
// skyline around 0.5, the night deepening to 1; BLEND (amp_blend_wet) Jupiter's phase, 0 the total eclipse, to full at 1;
// PRESENCE (the Presence tone) the sulphur snow. Given as options { drive, blend, presence }, the world is made under
// them (0.5, 0.8 and 0.2 unless said: the app's opening, the kit's posters); set later, the world eases to them.
//
// THE BASS, HEARD: an onset in its level is a hit — the aurora flares, the nearest vents puff, the dosimeter kicks, the
// volcanoes take their share (IoDrive::hear; the level between -50 and -12 dBFS, worlds/Play.h). Not called, it is
// silence. A page's HIT button is view.hear (1) after a quiet moment.
//
// THE STAGE: the canvas lies over the cabin's whole stage (1672 x 941 in the app) and the world is seen in the cabin's
// camera — a 16 mm lens, the half-width's tangent 18/16 at any aspect, the centre at the stage's centre, the world 5
// degrees down (IoDrive). By default the world is alone behind the glass, as in the app's posters: the page lays the
// cabin over the canvas, its windscreen cut out. With { cabin: true } the world draws the cabin itself, over the whole
// stage: the day's and the dark's plates crossfaded as the Sun goes (cabinDark), the dosimeter's needle on its dial, as
// in the app's live frames (dashboard_dark.webp is fetched only then); the plates have no pictograms and no words:
// those are the face's (the page draws them over, lit by cabinDark at night).
//
// THE NEEDLE: the dosimeter's needle swings with every attack, on a spring. While the world does not draw the cabin it
// still draws the dial — the day plate's own pixels in the dial's box round its hub (stage 119.2, 564.9; the box 46 stage
// px either way) — and the needle on it, over the world, unless { needle: false }: the page lays a plate with the dial's
// face cut out (cabin-dial.webp: the kit's cabin.webp, clear in a disk of 37.8 stage px round the hub), and the world's
// dial shows through, the needle moving. { needle: false } with the kit's cabin.webp is the app's face-painted look, the
// dial with no needle; dashboard.webp is then not fetched either.
//
// THE FILES: everything the app's art reader reads (src/io/view/IoArt.cpp), under the URL the page gives as `files` — a
// folder laid out as resources/io is shipped (IO_FILES below: dashboard.webp, dashboard_dark.webp, finds/<variant>3d/
// <name>.mesh and .txt). Each is fetched as the view is made, at once and all together, decoded where the world is drawn
// (in its worker, where it has one) — the WebP plates by the browser's own decoder to straight RGBA, read back through
// WebGL so the colour under a clear alpha is kept —, and handed to the renderer a part at a time, once every file of it
// has come or failed: the two plates together, each find's folder. The first frame waits for them, `wait` milliseconds
// at most (10 s unless said; 0 does not wait): what has come by then the renderer is made with, and the first frame is
// the app's; what comes after is taken between two frames. The world runs with what it has: a file that does not come
// leaves its part out (a find its stand-in, no cabin, no dial), said in the console, as a missing file does in the app.
//
// The rest — the quality ladder, the worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` —
// is every world's, as view.js says. The first frame is the same every time for the same gear, PARK, knobs and canvas
// aspect: the world at its start, made in the drive's first gear, handed the gear and PARK and settled as the app's
// first frame is, nothing stepped — the kit's posters. A bench's `test` is io_view_demo's (--mode=still): { at: [x, z],
// drive: seconds, still: bool } (io.cpp, view_test), `park` as the demo's --park, the knobs as its --knobs; its frame
// waits for every file.

/** A URL of the page's, made whole so it means the same in the worker; a folder's ends in a slash. */
function ioFolder (u)
{
    if (typeof u !== 'string' || ! u) return null;
    const whole = new URL (u, document.baseURI).href;
    return whole.endsWith ('/') ? whole : whole + '/';
}

/** The part of the world a file belongs to, which takes it whole: its folder under finds/, else the plates. */
const ioPart = path => path.startsWith ('finds/') ? path.split ('/').slice (0, 2).join ('/') : 'plates';

/** A context of its own for reading a picture back exactly: straight, the colour under a clear alpha as the file has
    it, which a 2D canvas loses. false where there is none. */
let ioReader = null;
function ioExact (bitmap)
{
    if (ioReader === null)
    {
        try
        {
            const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1, 1) : document.createElement ('canvas');
            ioReader = c.getContext ('webgl2', { premultipliedAlpha: false, antialias: false, depth: false, stencil: false, alpha: true }) || false;
        }
        catch (e) { ioReader = false; }
    }
    const gl = ioReader;
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
async function ioFile (base, path)
{
    try
    {
        const r = await fetch (base + path);
        if (! r.ok) throw new Error (r.status + ' ' + r.statusText);
        if (! path.endsWith ('.webp')) return { path, bytes: new Uint8Array (await r.arrayBuffer ()) };
        const bitmap = await createImageBitmap (await r.blob (), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
        const { width, height } = bitmap;
        let data = null;
        try { data = ioExact (bitmap); } catch (e) { data = null; }
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
    catch (e) { console.warn ('io-wasm: ' + base + path + ' is left out: ' + (e && e.message || e)); return null; }
}

/** The knobs' places, 0..1: a number, or the opening place. */
const ioKnob = (v, d) => { const n = +v; return Number.isFinite (n) ? Math.min (1, Math.max (0, n)) : d; };

/** THE WORLD'S OWN, where it is drawn (view.js): PARK, the cabin, the needle, the knobs, the cabin's dark said back to
    the page, and the files, fetched from the start and handed to the module (io.cpp) a part at a time. */
function ioOwn ({ files = null, cabin = false, needle = true, park = false, knobs = [0.5, 0.8, 0.2], wait = 10000, bench = false }, tell)
{
    let M = null, gone = false, dark = -1;
    const waiting = [];             // parts come before the module is made
    const paths = files ? IO_FILES.map (([path]) => path).filter (path =>
        path === 'dashboard_dark.webp' ? cabin : path === 'dashboard.webp' ? cabin || needle : true) : [];
    if (! files) console.warn ('io-wasm: no `files` folder given: the world is drawn without its files');
    const parts = new Map ();
    for (const path of paths) { const k = ioPart (path); if (! parts.has (k)) parts.set (k, []); parts.get (k).push (path); }

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
        Promise.all (list.map (path => ioFile (files, path))).then (part =>
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
            M._view_needle (needle ? 1 : 0);
            knobs.forEach ((v, k) => M._view_knob (k, v));      // before the world is built: it is made under them
            for (const part of waiting.splice (0)) hand (part);
            // The renderer is made once every file has come, or `wait` has gone by; a bench's frame waits for all.
            if (bench || ! (wait < Infinity)) return all;
            return Promise.race ([all, new Promise (r => setTimeout (r, Math.max (0, +wait || 0)))]);
        },
        said (m)
        {
            if (m.type === 'park') { park = m.on; if (M) M._view_park (park ? 1 : 0); }
            else if (m.type === 'knob') { knobs[m.k] = m.v; if (M) M._view_knob (m.k, m.v); }
        },
        framed ()
        {
            const d = M._view_dark ();
            if (d !== dark) { dark = d; tell ({ type: 'dark', share: d }); }
        },
        gone () { gone = true; },
    };
}

/** ...and its calls, on the page; what the world says back (the cabin's dark) comes to the function it gives back. */
function ioCalls (view, say, own, onDark)
{
    let dark = 0;
    /** PARK: the amp bypassed whole. */
    view.setPark = on => say ({ type: 'park', on: !! on });
    /** The amp's knobs, each 0..1 over its range: DRIVE the time of day, BLEND Jupiter's phase, PRESENCE the snow. */
    view.setDrive = v => say ({ type: 'knob', k: 0, v: ioKnob (v, 0) });
    view.setBlend = v => say ({ type: 'knob', k: 1, v: ioKnob (v, 0) });
    view.setPresence = v => say ({ type: 'knob', k: 2, v: ioKnob (v, 0) });
    /** How dark the cabin is, 0 .. 1, as the last frame drawn left it. */
    Object.defineProperty (view, 'cabinDark', { get: () => dark, enumerable: true });
    return m =>
    {
        if (m.type === 'dark') { dark = m.share; if (onDark) onDark (dark); }
    };
}

const IO = { name: 'io', module: createIoModule, ear: true, own: ioOwn, calls: ioCalls };
worldWorker (IO);

export function createIoView (canvas, options = {})
{
    const { files = null, cabin = false, needle = true, park = false, drive, blend, presence, wait = 10000, onDark = null, ...rest } = options;
    const knobs = [ioKnob (drive, 0.5), ioKnob (blend, 0.8), ioKnob (presence, 0.2)];
    const world = { ...IO, calls: (view, say, own) => ioCalls (view, say, own, typeof onDark === 'function' ? onDark : null) };
    return createWorldView (canvas, world, import.meta.url,
                            { gears: 2, ...rest, own: { files: ioFolder (files), cabin: !! cabin, needle: !! needle, park: !! park,
                                                        knobs, wait: +wait, bench: !! rest.test } });
}

/** The world's files as the page must serve them under `files`: [path, bytes] each (dashboard_dark.webp is fetched only
    with { cabin: true }, dashboard.webp with the cabin or the needle). */
export const ioFiles = IO_FILES.map (([path, bytes]) => ({ path, bytes }));
