// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// RedPawAmp's windscreen for the site: the Plains of Mars, drawn by the app's own C++ — PlainsWorld and PlainsGl
// (src/redpaw/view), built to WebAssembly over WebGL 2. The build puts this file at the end of plains-wasm.mjs, after
// view.js (tools/worlds-wasm/CMakeLists.txt); the page imports that, and plains-wasm.wasm comes beside it.
//
//   const view = createPlainsView (canvas, { gears: 2, steer: true, onFallback });
//   view.setGear (2);          // 1 CRUNCH, 2 HI-GAIN: the rover eases to that gear's speed
//   view.setGear (2, { settled: true })   // ...or is at it at once; createPlainsView (canvas, { gear: 2 }) starts so
//   view.setTurbo (true);      // twice the speed, and the dust the gear raises: the top gear is the full storm
//   view.setPark (true);       // the rover comes to a stand and the dust settles; steering only turns the view
//   view.setPark (true, { settled: true })   // ...or stands at once, as setGear (n, { settled: true }) is at its gear
//   view.setSteer (-1..1);     // the wheel, from outside; or { steer: true } lets a drag across the canvas steer
//   view.stop(); view.start(); view.destroy();
//
// The canvas lies over the windscreen's box (1440.5 x 269.5 in the app). The rest — the quality ladder, the worker,
// onReady, view.ready, onFallback, the bench's `test` and `worker: false` — is every world's, as view.js says. The
// first frame is the same every time for the same gear, PARK and canvas aspect: the rover stands at the same place.
// A bench's `test` is RedPawViewDemo's: { at: [x, z, headingDegrees], drive: seconds, steer: -1..1, still: bool }
// (plains.cpp, view_test).

const PLAINS = { name: 'plains', module: createPlainsModule };
worldWorker (PLAINS);

export function createPlainsView (canvas, options = {})
{
    return createWorldView (canvas, PLAINS, import.meta.url, { gears: 2, ...options });
}
