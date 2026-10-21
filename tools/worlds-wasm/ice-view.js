// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// FrostPawAmp's windscreen for the site: the ice moon, drawn by the app's own C++ — IceWorld and IceGl
// (src/frostpaw/view), built to WebAssembly over WebGL 2. The build puts this file at the end of ice-wasm.mjs, after
// view.js (tools/worlds-wasm/CMakeLists.txt); the page imports that, and ice-wasm.wasm comes beside it.
//
//   const view = createIceView (canvas, { gears: 3, steer: true, onFallback });
//   view.setGear (3);          // 1 CLEAN, 2 CRUNCH, 3 HI-GAIN: the rover eases to that gear's speed and its snow
//   view.setGear (3, { settled: true })   // ...or is at it at once; createIceView (canvas, { gear: 3 }) starts so
//   view.setTurbo (true);      // twice the speed, and more snow: the top gear with TURBO is the blizzard
//   view.setSteer (-1..1);     // the wheel, from outside; or { steer: true } lets a drag across the canvas steer
//   view.stop(); view.start(); view.destroy();
//
// The canvas lies over the windscreen's box (1441.5 x 271 in the app). The rest — the quality ladder, the worker,
// onReady, view.ready, onFallback, the bench's `test` and `worker: false` — is every world's, as view.js says. The
// first frame is the same every time for the same gear and canvas aspect: the rover stands at the same place,
// at the same hour of the sky. A bench's `test` is FrostPawViewDemo's:
// { at: [x, z, headingDegrees], clock: [seconds, metres], drive: seconds, steer: -1..1, still: bool } (ice.cpp, view_test).

const ICE = { name: 'ice', module: createIceModule };
worldWorker (ICE);

export function createIceView (canvas, options = {})
{
    return createWorldView (canvas, ICE, import.meta.url, { gears: 3, ...options });
}
