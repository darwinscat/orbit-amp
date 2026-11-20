// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// RedGrooveBass's dome for the site: the walker in the glowing forest under a red dwarf, drawn by the app's own C++ —
// WalkerDrive, WalkerWorld and WalkerGl (src/redgroove/view) — built to WebAssembly over WebGL 2. The build puts this
// file at the end of walker-wasm.mjs, after view.js (tools/worlds-wasm/CMakeLists.txt); the page imports that, and
// walker-wasm.wasm comes beside it.
//
//   const view = createWalkerView (canvas, { gear: 1, steer: true, onFallback });
//   view.setGear (2);          // the lever: B15 walks in gear 1, SVT in gear 2 (place i is gear i + 1) — the walker
//                              // eases into that gear's pace and stride, its spores with it
//   view.setGear (2, { settled: true })   // ...or is at it at once; createWalkerView (canvas, { gear: 2 }) starts so
//   view.setSteer (-1..1);     // the hand on the glass, from outside; or { steer: true } lets a drag across the canvas
//                              // turn the walker — let go, it finds its own way among the stalks again
//   view.setTurbo (on);        // TURBO, which RedGrooveBass's face has not: a long striding run, the spores a storm
//   view.hear (peak);          // the bass: the amp's input's peak since the last call, linear (1 = full scale), what
//                              // the IN meter shows — once a frame or so from the page's audio
//   view.stop(); view.start(); view.destroy();
//
// The same calls as every rover's view, so a page drives it as it drives the ice: the lever's place as the gear, made
// held and settled at it (`hold: true`, `gear`). Its gears are RedGrooveBass's two unless `gears` says otherwise (1 to
// 3, the world's own). It walks at its own pace, as the plug-in does with no host's tempo; nothing reads a speed off it.
//
// One call more than a rover's: view.hear. The forest answers the bass from the first note, as the plug-in's does
// unless the player turns it off (Lit by the bass): the caps breathe with the level — up in under half a second, down
// in about one — and a jump in it is an attack, a flash on the near caps and sparks of spores from the ten nearest
// (WalkerDrive::hear, WalkerWorld::step; the level between -50 and -12 dBFS, worlds/Play.h). Not called, it is
// silence: the forest as it always was. A page that wants it unlit does not call it.
//
// The canvas lies over the dome's box (1672 x 525 in the app: the stage's top, the cabin's picture transparent there
// but for the dome's bars); the horizon sits 0.57 of the way down it, as the app puts it. The rest — the quality ladder, the
// worker, onReady, view.ready, onFallback, the bench's `test` and `worker: false` — is every world's, as view.js says.
// The first frame is the same every time for the same gear and canvas aspect: the walker at its world's start, made in
// its first gear and settled at the gear asked for, nothing stepped — the app's own first frame, which the kit's posters
// are. A bench's `test` is WalkerViewDemo's (--horizon=0.57): { at: [x, z, headingDegrees], drive: seconds,
// steer: -1..1, still: bool } (walker.cpp, view_test).

const WALKER = { name: 'walker', module: createWalkerModule, ear: true };
worldWorker (WALKER);

export function createWalkerView (canvas, options = {})
{
    return createWorldView (canvas, WALKER, import.meta.url, { gears: 2, ...options });
}
