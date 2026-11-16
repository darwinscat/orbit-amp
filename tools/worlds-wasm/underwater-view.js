// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// The bass amp's cabin for the site: the sea floor of an ocean world seen from the bathyscaphe, drawn by the app's own
// C++ — UnderwaterDrive, UnderwaterWorld and UnderwaterGl (src/underwater/view) — built to WebAssembly over WebGL 2.
// The build puts this file at the end of underwater-wasm.mjs, after view.js (tools/worlds-wasm/CMakeLists.txt); the
// page imports that, and underwater-wasm.wasm comes beside it.
//
//   const view = createUnderwaterView (canvas, { gear: 1, hold: true, onFallback });
//   view.setGear (2);          // the lever: CLEAN, DIRT, DRIVE are the gears 1, 2, 3 — the bathyscaphe eases to that
//                              // gear's crawl along the bottom
//   view.setGear (2, { settled: true })   // ...or is at it at once; createUnderwaterView (canvas, { gear: 2 }) starts so
//   view.hear (peak);          // the bass: the amp's input's peak since the last call, linear (1 = full scale), what
//                              // the IN meter shows — once a frame or so from the page's audio
//   view.setArt ({ atlas, sprites })      // the passing bathyscaphe's art, by URL, whenever the page has it
//   view.setPark (on);         // PARK, the amp bypassed whole: the bathyscaphe stops, the near jellyfish come to the glass
//   view.stop(); view.start(); view.destroy();
//
// The same calls as every world's view, so a page drives it as it drives the walker: the lever's place as the gear,
// made held and settled at it (`hold: true`, `gear`). The bass has no TURBO: view.setTurbo does nothing. The steering
// is every world's (view.setSteer, or { steer: true } for a drag across the canvas).
//
// THE BASS, HEARD: an onset in its level lights the living things, running out from the vehicle (UnderwaterDrive::hear;
// the level between -50 and -12 dBFS, worlds/Play.h). Not called, it is silence; a page that wants it unlit does not
// call it.
//
// THE STAGE: the canvas lies over the cabin's whole stage (1672 x 941 in the app) and the world is seen in the cabin's
// camera — a 13 mm lens, the half-width's tangent 18/13 at any aspect, the centre slid to -0.2339, the world 11 degrees
// down (UnderwaterDrive). By default the world is alone behind the glass, as in the app's STILL and its posters: the
// page paints the cabin over the canvas, its window cut out, and nothing of the cabin is fetched or drawn here. With
// { cabin: true, art: { dashboard, sea } } (the URLs of dashboard.webp and sea.webp) the world draws the panel itself,
// over the whole stage, with the caustics moving in its glass, as in the app's live frames: the two pictures are
// fetched as the module loads and the first frame waits for them; one that does not come leaves the panel out.
//
// THE PASSING BATHYSCAPHE: another vehicle crosses the view now and then — the first 40 s in, then a minute after the
// last — drawn from its atlas (atlas.webp) and its table (sprites.txt, plain text, as it is). Their URLs come as the
// option `art: { atlas, sprites }`, fetched once the first frame is drawn, or later by view.setArt; until both are in,
// none passes, and the first frame never waits for them or shows them. Every picture is fetched and decoded where the
// world is drawn (in its worker, where it has one) to straight RGBA, and taken by the renderer between two frames.
// The URLs are the page's, relative to its document.
//
// The rest — the quality ladder, the worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` —
// is every world's, as view.js says. The first frame is the same every time for the same gear and canvas aspect: the
// world at its start, made in the drive's first gear and settled at the gear asked for, nothing stepped — the app's own
// first frame, which the kit's posters are. A bench's `test` is UnderwaterViewDemo's (--mode=still): { at: [x, z,
// headingDegrees], drive: seconds, steer: -1..1, still: bool } (underwater.cpp, view_test); `park` as the demo's --park;
// its frame is made with all the art it is given, as the demo's is — the passing bathyscaphe's too.

/** A URL of the page's, made whole so it means the same in the worker. */
const underwaterUrl = u => typeof u === 'string' && u ? new URL (u, document.baseURI).href : null;

/** The art's URLs as handed to where the world is drawn: the cabin's two only where it is asked for. */
function underwaterArt ({ dashboard, sea, atlas, sprites } = {}, cabin)
{
    return { dashboard: cabin ? underwaterUrl (dashboard) : null, sea: cabin ? underwaterUrl (sea) : null,
             atlas: underwaterUrl (atlas), sprites: underwaterUrl (sprites) };
}

/** A picture, fetched and decoded to straight RGBA, the top row first; null, said in the console, when it does not
    come. */
async function underwaterPicture (url)
{
    try
    {
        const r = await fetch (url);
        if (! r.ok) throw new Error (r.status + ' ' + r.statusText);
        const bitmap = await createImageBitmap (await r.blob (), { premultiplyAlpha: 'none', colorSpaceConversion: 'none' });
        const { width, height } = bitmap;
        const c = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (width, height) : Object.assign (document.createElement ('canvas'), { width, height });
        const g = c.getContext ('2d', { willReadFrequently: true });
        g.drawImage (bitmap, 0, 0);
        bitmap.close ();
        return { width, height, data: g.getImageData (0, 0, width, height).data };
    }
    catch (e) { console.warn ('underwater-wasm: ' + url + ' is left out: ' + (e && e.message || e)); return null; }
}

/** A text file as it is; null, said in the console, when it does not come. */
async function underwaterText (url)
{
    try
    {
        const r = await fetch (url);
        if (! r.ok) throw new Error (r.status + ' ' + r.statusText);
        return await r.text ();
    }
    catch (e) { console.warn ('underwater-wasm: ' + url + ' is left out: ' + (e && e.message || e)); return null; }
}

/** THE WORLD'S OWN, where it is drawn (view.js): PARK, the cabin and the art, handed to the module (underwater.cpp). */
function underwaterOwn ({ park = false, cabin = false, art = {}, bench = false })
{
    let M = null, gone = false, past = false, passing = art.atlas && art.sprites ? art : null;
    const cabinPictures = cabin && art.dashboard && art.sea ? Promise.all ([underwaterPicture (art.dashboard), underwaterPicture (art.sea)]) : null;

    function put (which, picture)
    {
        const at = M._view_picture (which, picture.width, picture.height);
        if (at) M.HEAPU8.set (picture.data, at);
    }

    // The passing bathyscaphe's: both or neither.
    async function pass ({ atlas, sprites })
    {
        const [picture, text] = await Promise.all ([underwaterPicture (atlas), underwaterText (sprites)]);
        if (gone || ! picture || text === null) return;
        put (2, picture);
        const bytes = new TextEncoder ().encode (text);
        const at = M._view_sprites (bytes.length);
        if (at) M.HEAPU8.set (bytes, at);
        M._view_art ();
    }

    return {
        made (module)
        {
            M = module;
            M._view_park (park ? 1 : 0);
            M._view_cabin (cabin ? 1 : 0);
            // The cabin's pictures before the renderer; the passing bathyscaphe's once it is made, after the first frame —
            // but for a bench's frame, which is the demo's, made with all of its art.
            const cabinIn = cabinPictures ? cabinPictures.then (([dash, sea]) =>
            {
                if (gone || ! dash || ! sea) return;
                put (0, dash); put (1, sea);
                M._view_art ();
            }) : Promise.resolve ();
            if (bench) return passing ? Promise.all ([cabinIn, pass (passing)]) : cabinIn;
            cabinIn.then (() => setTimeout (() => { past = true; if (passing && ! gone) pass (passing); }, 0));
            return cabinIn;
        },
        said (m)
        {
            if (m.type === 'park') { park = m.on; if (M) M._view_park (park ? 1 : 0); }
            else if (m.type === 'art' && m.art.atlas && m.art.sprites) { passing = m.art; if (past && ! gone) pass (passing); }
        },
        gone () { gone = true; },
    };
}

/** ...and its calls, on the page. */
function underwaterCalls (view, say)
{
    /** PARK: the amp bypassed whole. */
    view.setPark = on => say ({ type: 'park', on: !! on });
    /** The passing bathyscaphe's art: the URLs of its atlas and of its table. */
    view.setArt = ({ atlas, sprites } = {}) => say ({ type: 'art', art: underwaterArt ({ atlas, sprites }, false) });
}

const UNDERWATER = { name: 'underwater', module: createUnderwaterModule, ear: true, own: underwaterOwn, calls: underwaterCalls };
worldWorker (UNDERWATER);

export function createUnderwaterView (canvas, options = {})
{
    const { cabin = false, park = false, art = null, ...rest } = options;
    return createWorldView (canvas, UNDERWATER, import.meta.url,
                            { gears: 3, ...rest, own: { cabin: !! cabin, park: !! park, art: underwaterArt (art || {}, !! cabin), bench: !! rest.test } });
}
