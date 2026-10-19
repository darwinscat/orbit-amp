// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// RedPawAmp's windscreen for the site: the Plains of Mars, drawn by the app's own C++ — PlainsWorld and PlainsGl
// (src/redpaw/view), built to WebAssembly over WebGL 2. The build puts this file at the end of plains-wasm.mjs
// (tools/worlds-wasm/CMakeLists.txt); the page imports that, and plains-wasm.wasm comes beside it. No dependencies.
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
// The canvas lies over the windscreen's box (1440.5 x 269.5 in the app). It renders at the screen's pixels (at
// most twice the CSS size) and comes down to 0.75 and 0.5 of that by itself when frames run late, climbing back
// when they have room; it stops while off screen or hidden. Where the browser can — a canvas handed to a worker
// with WebGL 2 there — the landscape is made and every frame drawn in a worker, this same file loaded there, and
// the page's thread only listens; elsewhere it is all on the page's thread, the landscape made in one go. Either
// way `onReady ()` is called once the first frame is on the canvas (`view.ready` is the same as a promise: true,
// or false when the view fell back or was destroyed first). Until then the canvas is blank — keep the poster over
// it. The first frame is the same every time for the same gear, PARK and canvas aspect: the rover stands at the
// same place. When WebGL 2 or a shader is not to be had, `onFallback (reason)` is called with the driver's log
// and nothing is drawn — show a still of the plains in its place.
//
// Two options are a bench's, not the view's: `test`, a frame set up as RedPawViewDemo sets its own (see
// plainsDrawn below) and handed to `onFrame ({ width, height, data })`, its RGBA rows top-down; and
// `worker: false`, the page's thread whatever the browser can do.

const PLAINS_RUNGS = [1.0, 0.75, 0.5];
const plainsClamp = (v, lo, hi) => Math.min (hi, Math.max (lo, v));
const plainsAttributes = { antialias: false, alpha: false, depth: false, stencil: false, premultipliedAlpha: true, powerPreference: 'default' };

//==============================================================================
/** THE VIEW ITSELF, wherever it runs: the module made on `canvas` (a worker's OffscreenCanvas or the page's own),
    the landscape, the first frame, then a frame each animation frame while the page wants it. What it has to say
    goes to `post`; what the page says comes to the function returned. */
function plainsDrawn (canvas, options, post)
{
    const { gears = 2, test = null } = options;
    let gear = options.gear ?? 1, turbo = !! options.turbo, parked = !! options.parked;
    let cssW = options.cssW || 0, cssH = options.cssH || 0, dpr = options.dpr || 1;
    let M = null, failed = false, ready = false, drawn = false, active = !! options.on, steer = 0;
    let rung = 0, width = 0, height = 0, frame = 0, last = 0;
    const raf = typeof requestAnimationFrame === 'function' ? requestAnimationFrame : f => setTimeout (() => f (performance.now ()), 16);
    const caf = typeof cancelAnimationFrame === 'function' ? cancelAnimationFrame : clearTimeout;
    const pause = () => new Promise (r => setTimeout (r, 0));

    function halt () { if (frame) caf (frame); frame = 0; }
    function fail (reason) { if (failed) return; failed = true; halt (); post ({ type: 'fallback', reason }); }
    const lost = e => { e.preventDefault (); fail ('context lost'); };

    // The pixels: the screen's, at most twice the CSS size, times the rung the ladder stands on.
    function size ()
    {
        const k = dpr * PLAINS_RUNGS[rung];
        const w = Math.max (2, Math.round (cssW * k)), h = Math.max (2, Math.round (cssH * k));
        if (w !== width || h !== height) { canvas.width = width = w; canvas.height = height = h; }
    }

    // THE LADDER: frames that keep running late (under ~45 a second for three seconds) take a rung down;
    // twenty seconds with room to spare (over ~57) take one back up.
    const late = [];
    let slowFor = 0, calmFor = 0;
    function ladder (dt)
    {
        late.push (dt); if (late.length > 30) late.shift ();
        const sorted = [...late].sort ((a, b) => a - b), mid = sorted[sorted.length >> 1];
        if (late.length < 30) return;
        if (mid > 1 / 45) { slowFor += dt; calmFor = 0; } else if (mid < 1 / 57) { calmFor += dt; slowFor = 0; } else { slowFor = calmFor = 0; }
        const was = rung;
        if (slowFor > 3 && rung < PLAINS_RUNGS.length - 1) { ++rung; slowFor = 0; late.length = 0; }
        if (calmFor > 20 && rung > 0) { --rung; calmFor = 0; late.length = 0; }
        if (rung !== was) post ({ type: 'quality', value: PLAINS_RUNGS[rung] });
    }

    function tick (now)
    {
        frame = raf (tick);
        const t = now / 1000, dt = last ? Math.min (0.05, Math.max (0, t - last)) : 0;
        last = t;
        if (dt > 0) ladder (dt);
        size ();
        M._plains_frame (width, height, dt, steer);
        if (! drawn) { drawn = true; post ({ type: 'ready' }); }
    }
    function run () { if (ready && active && ! failed && ! test && ! frame) { last = 0; frame = raf (tick); } }

    (async () =>
    {
        try { M = await createPlainsModule ({ plainsCanvas: canvas, print: () => {}, printErr: t => console.warn ('plains-wasm:', t) }); }
        catch (e) { fail ('the module did not load: ' + (e && e.message || e)); return; }
        if (failed) return;
        M._plains_make (gears, gear, turbo ? 1 : 0, parked ? 1 : 0);
        if (! M._plains_context (test ? 1 : 0)) { fail ('no WebGL 2'); return; }
        canvas.addEventListener ('webglcontextlost', lost);
        M._plains_build ();
        await pause ();             // on the page's thread, the landscape and the shaders are two tasks, not one
        if (failed) return;
        if (test)
        {
            // RedPawViewDemo's drive and STILL, step for step (plains.cpp, plains_test).
            const at = test.at;
            M._plains_test (at ? 1 : 0, at ? +at[0] || 0 : 0, at ? +at[1] || 0 : 0, at ? +at[2] || 0 : 0, +test.drive || 0, +test.steer || 0, test.still ? 1 : 0);
        }
        else M._plains_settle ();   // ...at the gear and PARK asked for, whatever was asked for meanwhile
        if (! M._plains_create ())
        {
            fail (M.UTF8ToString (M._plains_error ()) || 'the renderer did not start');
            return;
        }
        ready = true;
        if (test)
        {
            // The bench's frame: drawn with no step after it and kept; the scene's own pixels, as the app's STILL.
            size ();
            M._plains_draw (width, height);
            const at = M._plains_read (), w = M._plains_read_width (), h = M._plains_read_height ();
            const data = new Uint8ClampedArray (w * h * 4), row = w * 4;
            for (let y = 0; y < h; ++y) data.set (M.HEAPU8.subarray (at + (h - 1 - y) * row, at + (h - y) * row), y * row);
            post ({ type: 'frame', width: w, height: h, data }, [data.buffer]);
            drawn = true;
            post ({ type: 'ready' });
            return;
        }
        run ();
    }) ();

    return m =>
    {
        switch (m.type)
        {
            case 'size':  cssW = m.cssW; cssH = m.cssH; dpr = m.dpr; break;
            case 'gear':  gear = m.n; if (M && ! failed) { M._plains_gear (gear); if (m.settled) M._plains_settle (); } break;
            case 'turbo': turbo = m.on; if (M && ! failed) M._plains_turbo (turbo ? 1 : 0); break;
            case 'park':  parked = m.on; if (M && ! failed) { M._plains_park (parked ? 1 : 0); if (m.settled) M._plains_settle (); } break;
            case 'steer': steer = m.v; break;
            case 'run':   active = m.on; active ? run () : halt (); break;
            case 'destroy':
                halt ();
                failed = true;
                canvas.removeEventListener ('webglcontextlost', lost);
                if (M) M._plains_destroy ();
                post ({ type: 'gone' });
                break;
        }
    };
}

//==============================================================================
/** THE WORKER: this file, loaded as a module worker by createPlainsView, given the canvas. */
if (typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope)
{
    let said = null;
    self.onmessage = ({ data }) =>
    {
        if (data.type === 'init') said = plainsDrawn (data.canvas, data.options, (m, transfer) => self.postMessage (m, transfer || []));
        else if (said) said (data);
    };
}

/** Whether a canvas handed to a worker can have WebGL 2 there: asked once, of a canvas of one pixel. */
let plainsOffscreenGl = null;
function plainsOffscreenWebGL2 ()
{
    if (plainsOffscreenGl === null)
    {
        plainsOffscreenGl = false;
        try
        {
            const gl = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1, 1).getContext ('webgl2') : null;
            if (gl) { plainsOffscreenGl = true; gl.getExtension ('WEBGL_lose_context')?.loseContext (); }
        }
        catch (e) { plainsOffscreenGl = false; }
    }
    return plainsOffscreenGl;
}

//==============================================================================
export function createPlainsView (canvas, { steer = false, onFallback = () => {}, onReady = () => {}, onFrame = () => {}, gears = 2, gear = 1, parked = false, turbo = false, test = null, worker = true } = {})
{
    let failed = false, quality = PLAINS_RUNGS[0], settleReady = null, gone = () => {};
    const readyPromise = new Promise (r => { settleReady = r; });
    function said (m)
    {
        if (m.type === 'ready')         { if (! failed) { settleReady (true); onReady (); } }
        else if (m.type === 'fallback') { if (! failed) { failed = true; settleReady (false); onFallback (m.reason); } }
        else if (m.type === 'quality')  quality = m.value;
        else if (m.type === 'frame')    onFrame ({ width: m.width, height: m.height, data: m.data });
        else if (m.type === 'gone')     gone ();
    }

    const measured = () => ({ cssW: canvas.clientWidth, cssH: canvas.clientHeight, dpr: Math.min (2, globalThis.devicePixelRatio || 1) });
    const options = { gears, gear, parked, turbo, test: test && { at: test.at || null, drive: +test.drive || 0, steer: +test.steer || 0, still: !! test.still },
                      on: ! document.hidden, ...measured () };

    // Where the canvas goes to a worker, everything else goes with it; else the view is here, on a canvas that
    // has WebGL 2 or never draws.
    let send;
    if (worker && typeof Worker === 'function' && typeof canvas.transferControlToOffscreen === 'function' && plainsOffscreenWebGL2 ())
    {
        const offscreen = canvas.transferControlToOffscreen ();
        const w = new Worker (import.meta.url, { type: 'module', name: 'plains' });
        w.onmessage = e => said (e.data);
        w.onerror = e => { e.preventDefault (); said ({ type: 'fallback', reason: 'the worker did not start: ' + (e.message || 'no module workers here') }); };
        w.postMessage ({ type: 'init', canvas: offscreen, options }, [offscreen]);
        send = m => w.postMessage (m);
        gone = () => w.terminate ();
    }
    else
    {
        if (! canvas.getContext ('webgl2', { ...plainsAttributes, preserveDrawingBuffer: !! test })) { onFallback ('no WebGL 2'); return plainsInert (); }
        send = plainsDrawn (canvas, options, said);
    }

    // Off screen, in a hidden tab or stopped, no frames.
    let wanted = true, onScreen = true;
    const run = () => send ({ type: 'run', on: wanted && onScreen && ! document.hidden });
    const seen = new IntersectionObserver (([e]) => { onScreen = e.isIntersecting; run (); });
    seen.observe (canvas);
    document.addEventListener ('visibilitychange', run);
    const resized = new ResizeObserver (() => send ({ type: 'size', ...measured () }));
    resized.observe (canvas);

    // The steering: the hand's drag and the wheel from outside, summed, as the app's Steering is. A drag across
    // the glass steers, as in the app: a quarter of its width either way is full lock; letting go straightens
    // the rover. Parked, it only turns the view in place.
    let outside = 0, hand = 0, downX = null;
    const steered = () => send ({ type: 'steer', v: plainsClamp (outside + hand, -1, 1) });
    const onDown = e => { downX = e.clientX; canvas.setPointerCapture (e.pointerId); };
    const onMove = e => { if (downX !== null) { hand = plainsClamp ((e.clientX - downX) / Math.max (40, canvas.clientWidth * 0.25), -1, 1); steered (); } };
    const onUp = () => { downX = null; hand = 0; steered (); };
    if (steer)
    {
        canvas.style.touchAction = 'pan-y';          // a sideways drag on a phone steers, an upward one still scrolls
        canvas.addEventListener ('pointerdown', onDown);
        canvas.addEventListener ('pointermove', onMove);
        canvas.addEventListener ('pointerup', onUp);
        canvas.addEventListener ('pointercancel', onUp);
    }

    return {
        setGear (n, { settled = false } = {}) { send ({ type: 'gear', n, settled }); },
        setTurbo (on) { send ({ type: 'turbo', on: !! on }); },
        setPark (on, { settled = false } = {}) { send ({ type: 'park', on: !! on, settled }); },
        setSteer (v) { outside = plainsClamp (+v || 0, -1, 1); steered (); },
        start () { wanted = true; run (); },
        stop () { wanted = false; run (); },
        get quality () { return quality; },
        /** true once the first frame is on the canvas; false if the view fell back or was destroyed first. */
        ready: readyPromise,
        destroy ()
        {
            if (! failed) { failed = true; settleReady (false); }
            seen.disconnect (); resized.disconnect (); document.removeEventListener ('visibilitychange', run);
            canvas.removeEventListener ('pointerdown', onDown); canvas.removeEventListener ('pointermove', onMove);
            canvas.removeEventListener ('pointerup', onUp); canvas.removeEventListener ('pointercancel', onUp);
            send ({ type: 'destroy' });
        },
    };
}

function plainsInert ()
{
    return { setGear () {}, setTurbo () {}, setPark () {}, setSteer () {}, start () {}, stop () {}, get quality () { return 0; }, ready: Promise.resolve (false), destroy () {} };
}
