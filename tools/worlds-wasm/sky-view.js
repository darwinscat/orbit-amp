// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// CatpitAmp's sky for the site: the cockpit's space flight drawn by the app's own C++ — Flight and SkyGl (src/catpit),
// the shader resources/shaders/catpit/sky.frag — built to WebAssembly over WebGL 2. The build puts this file at the
// end of sky-wasm.mjs, after view.js (tools/worlds-wasm/CMakeLists.txt); the page imports that, and sky-wasm.wasm
// comes beside it.
//
//   const sky = createCatpitSky (canvas, { glass, windshield });   // both from cockpit.json, in the stage's units
//   createCatpitSky (canvas, { glass, windshield, channel: 2, count: 4 })   // ...flying at a channel's speed from the start
//   sky.setChannel (2);        // 0 CLEAN .. 3 HI-GAIN: the ship spools up or down to that channel's speed
//   sky.setChannel (2, 4, { settled: true })   // ...or is at it at once
//   sky.setThrottle (0..1);    // the lever anywhere along its travel
//   sky.setSteer (-1..1);      // the helm, from outside; or { steer: true } lets a drag across the canvas steer
//   sky.stop(); sky.start(); sky.destroy();
//
// The canvas lies over the glass's box; the picture in it is composed for the windshield inside that box, as the
// app's is. The rest — the quality ladder (sky.quality), the worker, onReady, sky.ready, onFallback, the bench's
// `test` and `worker: false` — is every world's, as view.js says. When WebGL 2 or the shader is not to be had,
// `onFallback (reason)` is called and nothing is drawn — show the channel's still sky (sky-<channel>.webp) in its
// place. The first frame is the same every time for the same channel and canvas aspect: the ship at the channel's
// speed, flying straight, at the start of its flight. A bench's `test` is { still: bool, drive: seconds,
// steer: -1..1 }: settled at the channel if `still`, then flown that long at 60 steps a second (sky.cpp, view_test).

const SKY = { name: 'sky', module: createSkyModule, sky: true };
worldWorker (SKY);

export function createCatpitSky (canvas, { glass, windshield, channel = 0, count = 4, ...options } = {})
{
    const place = glass && windshield ? [glass.x, glass.y, glass.w, glass.h, windshield.x, windshield.y, windshield.w, windshield.h] : null;
    const view = createWorldView (canvas, SKY, import.meta.url, { ...options, gears: count, gear: channel, place });
    const detent = (i, n) => viewClamp (i / Math.max (1, n - 1), 0, 1);
    return {
        setChannel (i, n = 4, { settled = false } = {}) { view.setThrottle (detent (i, n), { settled }); },
        setThrottle (t) { view.setThrottle (t); },
        setSteer (s) { view.setSteer (s); },
        start () { view.start (); },
        stop () { view.stop (); },
        get quality () { return view.quality; },
        /** true once the first frame is on the canvas; false if the sky fell back or was destroyed first. */
        ready: view.ready,
        destroy () { view.destroy (); },
    };
}
