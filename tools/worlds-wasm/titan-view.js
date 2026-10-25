// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// AmberPawAmp's canopy for the site: Titan's methane sea, drawn by the app's own C++ — TitanWorld and TitanGl
// (src/amberpaw/view), built to WebAssembly over WebGL 2. The build puts this file at the end of titan-wasm.mjs, after
// view.js (tools/worlds-wasm/CMakeLists.txt); the page imports that, and titan-wasm.wasm comes beside it.
//
//   const view = createTitanView (canvas, { gears: 3, steer: true, onFallback });
//   view.setGear (3);          // 1 CLEAN, 2 CRUNCH, 3 HI-GAIN: the helicopter eases to that gear's speed
//   view.setGear (3, { settled: true })   // ...or is at it at once; createTitanView (canvas, { gear: 3 }) starts so
//   view.setTurbo (true);      // a dash at twice the speed: the nose dips, the downwash tears a streak of foam ahead
//   view.setSteer (-1..1);     // the stick, from outside; or { steer: true } lets a drag across the canvas steer
//   view.setWipers (0..3);     // the four keys under the radio: OFF, INT (a wipe a minute), LO, HI; { wipers: 1 } starts so
//   view.setLights (left, right);   // the two searchlights under the nose; { lights: [false, false] } starts so
//   view.stop(); view.start(); view.destroy();
//
// The canvas lies over the canopy's box (1672 x 418 in the app: the cabin's top rows, its three panes). The rest —
// the quality ladder, the worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` — is every
// world's, as view.js says. The first frame is the same every time for the same gear, lights and canvas
// aspect: the craft stands where the app's search puts it, at the clock's zero. A bench's `test` is
// AmberPawViewDemo's: { at: [x, z, headingDegrees], clock: [seconds, metres], drive: seconds, steer: -1..1,
// still: bool } (titan.cpp, view_test).

const TITAN = { name: 'titan', module: createTitanModule, canopy: true };
worldWorker (TITAN);

export function createTitanView (canvas, options = {})
{
    return createWorldView (canvas, TITAN, import.meta.url, { gears: 3, wipers: 1, ...options });
}
