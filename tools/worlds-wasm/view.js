// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// A WORLD'S VIEW FOR THE SITE, whichever world it is: the app's own C++ — the world and its renderer — built to
// WebAssembly over WebGL 2, and what drives it from the page. The build puts this file at the end of each world's
// module (tools/worlds-wasm/CMakeLists.txt), the world's own file after it — plains-view.js, ice-view.js,
// titan-view.js — which names the world and exports its view: createPlainsView, createIceView, createTitanView,
// each the call below with its module. The page imports the module; its .wasm comes beside it. No dependencies.
//
// Every world's module answers the same calls (view.h): made in the product's gears at the gear, TURBO and PARK
// asked for, built, given a context, its renderer created, then a frame at a time. So every view works the same:
//
//   view.setGear (n);          // the gear: the drive eases to its speed and its weather
//   view.setGear (n, { settled: true })   // ...or is at it at once; create...View (canvas, { gear: n }) starts so
//   view.setTurbo (on);        // TURBO
//   view.setPark (on);         // PARK: the drive comes to a stand; steering only turns the view
//   view.setPark (on, { settled: true })  // ...or stands at once
//   view.setSteer (-1..1);     // the wheel, from outside; or { steer: true } lets a drag across the canvas steer
//   view.stop(); view.start(); view.destroy();
//
// and a world with a canopy (Titan's) has view.setWipers (0..3) and view.setLights (left, right) too, options
// { wipers, lights } setting them from the start. CatpitAmp's sky is one more such view (sky-view.js): its gears are
// the channels, counted from 0, and it has view.setThrottle (0..1, { settled }) — the lever anywhere along its
// travel — and an option `place`, where the glass and the windshield are, which the module is given once it is made.
//
// The canvas lies over the windscreen's box. It renders at the screen's pixels (at most twice the CSS size) and
// comes down to 0.75 and 0.5 of that by itself when frames run late, climbing back when they have room; it stops
// while off screen or hidden. Where the browser can — a canvas handed to a worker with WebGL 2 there — the world is
// made and every frame drawn in a worker, the same module loaded there, and the page's thread only listens;
// elsewhere it is all on the page's thread, the world made in one go. Either way `onReady ()` is called once the
// first frame is on the canvas (`view.ready` is the same as a promise: true, or false when the view fell back or
// was destroyed first). Until then the canvas is blank — keep the poster over it. The first frame is the same every
// time for the same gear, PARK and canvas aspect. When WebGL 2 or a shader is not to be had, `onFallback (reason)`
// is called with the driver's log and nothing is drawn — show a still in its place.
//
// Two options are a bench's, not the view's: `test`, a frame set up as the app's view demo sets its own (see
// worldDrawn below) and handed to `onFrame ({ width, height, data })`, its RGBA rows top-down; and `worker: false`,
// the page's thread whatever the browser can do.

const VIEW_RUNGS = [1.0, 0.75, 0.5];
const viewClamp = (v, lo, hi) => Math.min (hi, Math.max (lo, v));
const viewAttributes = { antialias: false, alpha: false, depth: false, stencil: false, premultipliedAlpha: true, powerPreference: 'default' };

//==============================================================================
/** THE VIEW ITSELF, wherever it runs: `world`'s module ({ name, module }) made on `canvas` (a worker's
    OffscreenCanvas or the page's own), the world, the first frame, then a frame each animation frame while the page
    wants it. What it has to say goes to `post`; what the page says comes to the function returned. */
function worldDrawn (canvas, options, post, world)
{
    const { gears = 2, test = null, place = null } = options;
    let gear = options.gear ?? 1, turbo = !! options.turbo, parked = !! options.parked;
    let wipers = options.wipers ?? 0, lights = options.lights || [false, false];
    let cssW = options.cssW || 0, cssH = options.cssH || 0, dpr = options.dpr || 1;
    let throttle = null;            // the sky's lever, once moved (sky-view.js)
    let M = null, failed = false, ready = false, drawn = false, active = !! options.on, steer = 0;
    let rung = 0, width = 0, height = 0, frame = 0, last = 0;
    const raf = typeof requestAnimationFrame === 'function' ? requestAnimationFrame : f => setTimeout (() => f (performance.now ()), 16);
    const caf = typeof cancelAnimationFrame === 'function' ? cancelAnimationFrame : clearTimeout;
    const pause = () => new Promise (r => setTimeout (r, 0));

    function halt () { if (frame) caf (frame); frame = 0; }
    function fail (reason) { if (failed) return; failed = true; halt (); post ({ type: 'fallback', reason }); }
    const lost = e => { e.preventDefault (); fail ('context lost'); };

    // The canopy's switches, where the world has them: handed over as they are set.
    function canopy ()
    {
        if (M && ! failed && M._view_wipers) { M._view_wipers (wipers); M._view_lights (lights[0] ? 1 : 0, lights[1] ? 1 : 0); }
    }

    // The pixels: the screen's, at most twice the CSS size, times the rung the ladder stands on.
    function size ()
    {
        const k = dpr * VIEW_RUNGS[rung];
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
        if (slowFor > 3 && rung < VIEW_RUNGS.length - 1) { ++rung; slowFor = 0; late.length = 0; }
        if (calmFor > 20 && rung > 0) { --rung; calmFor = 0; late.length = 0; }
        if (rung !== was) post ({ type: 'quality', value: VIEW_RUNGS[rung] });
    }

    function tick (now)
    {
        frame = raf (tick);
        const t = now / 1000, dt = last ? Math.min (0.05, Math.max (0, t - last)) : 0;
        last = t;
        if (dt > 0) ladder (dt);
        size ();
        M._view_frame (width, height, dt, steer);
        if (! drawn) { drawn = true; post ({ type: 'ready' }); }
    }
    function run () { if (ready && active && ! failed && ! test && ! frame) { last = 0; frame = raf (tick); } }

    (async () =>
    {
        try { M = await world.module ({ viewCanvas: canvas, print: () => {}, printErr: t => console.warn (world.name + '-wasm:', t) }); }
        catch (e) { fail ('the module did not load: ' + (e && e.message || e)); return; }
        if (failed) return;
        M._view_make (gears, gear, turbo ? 1 : 0, parked ? 1 : 0);
        if (place && M._view_place) M._view_place (...place);
        if (throttle !== null && M._view_throttle) M._view_throttle (throttle);
        canopy ();
        if (! M._view_context (test ? 1 : 0)) { fail ('no WebGL 2'); return; }
        canvas.addEventListener ('webglcontextlost', lost);
        M._view_build ();
        await pause ();             // on the page's thread, the world and the shaders are two tasks, not one
        if (failed) return;
        if (test)
        {
            // The app's view demo's drive and STILL, step for step (view_test in the world's file).
            const at = test.at, clock = test.clock;
            M._view_test (at ? 1 : 0, at ? +at[0] || 0 : 0, at ? +at[1] || 0 : 0, at ? +at[2] || 0 : 0,
                          clock ? 1 : 0, clock ? +clock[0] || 0 : 0, clock ? +clock[1] || 0 : 0,
                          +test.drive || 0, +test.steer || 0, test.still ? 1 : 0);
        }
        else M._view_settle ();     // ...at the gear and PARK asked for, whatever was asked for meanwhile
        if (! M._view_create ())
        {
            fail (M.UTF8ToString (M._view_error ()) || 'the renderer did not start');
            return;
        }
        ready = true;
        if (test)
        {
            // The bench's frame: drawn with no step after it and kept; the scene's own pixels, as the app's STILL.
            size ();
            M._view_draw (width, height);
            const at = M._view_read (), w = M._view_read_width (), h = M._view_read_height ();
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
            case 'size':   cssW = m.cssW; cssH = m.cssH; dpr = m.dpr; break;
            case 'gear':   gear = m.n; if (M && ! failed) { M._view_gear (gear); if (m.settled) M._view_settle (); } break;
            case 'turbo':  turbo = m.on; if (M && ! failed) M._view_turbo (turbo ? 1 : 0); break;
            case 'park':   parked = m.on; if (M && ! failed) { M._view_park (parked ? 1 : 0); if (m.settled) M._view_settle (); } break;
            case 'throttle': throttle = m.t; if (M && ! failed && M._view_throttle) { M._view_throttle (throttle); if (m.settled) M._view_settle (); } break;
            case 'steer':  steer = m.v; break;
            case 'wipers': wipers = m.speed; canopy (); break;
            case 'lights': lights = [m.left, m.right]; canopy (); break;
            case 'run':    active = m.on; active ? run () : halt (); break;
            case 'destroy':
                halt ();
                failed = true;
                canvas.removeEventListener ('webglcontextlost', lost);
                if (M) M._view_destroy ();
                post ({ type: 'gone' });
                break;
        }
    };
}

//==============================================================================
/** THE WORKER: the world's module, loaded as a module worker by createWorldView, given the canvas. The world's own
    file calls this as the module loads; anywhere but in a worker it does nothing. */
function worldWorker (world)
{
    if (typeof WorkerGlobalScope === 'undefined' || ! (self instanceof WorkerGlobalScope))
        return;
    let said = null;
    self.onmessage = ({ data }) =>
    {
        if (data.type === 'init') said = worldDrawn (data.canvas, data.options, (m, transfer) => self.postMessage (m, transfer || []), world);
        else if (said) said (data);
    };
}

/** Whether a canvas handed to a worker can have WebGL 2 there: asked once, of a canvas of one pixel. */
let viewOffscreenGl = null;
function viewOffscreenWebGL2 ()
{
    if (viewOffscreenGl === null)
    {
        viewOffscreenGl = false;
        try
        {
            const gl = typeof OffscreenCanvas === 'function' ? new OffscreenCanvas (1, 1).getContext ('webgl2') : null;
            if (gl) { viewOffscreenGl = true; gl.getExtension ('WEBGL_lose_context')?.loseContext (); }
        }
        catch (e) { viewOffscreenGl = false; }
    }
    return viewOffscreenGl;
}

//==============================================================================
/** The view of `world` ({ name, module, canopy }) on `canvas`: the API the head of this file describes. `url` is
    the module's own, which a worker loads. */
function createWorldView (canvas, world, url, { steer = false, onFallback = () => {}, onReady = () => {}, onFrame = () => {},
                                                gears = 2, gear = 1, parked = false, turbo = false, wipers = 0, lights = [false, false],
                                                place = null, test = null, worker = true } = {})
{
    let failed = false, quality = VIEW_RUNGS[0], settleReady = null, gone = () => {};
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
    const options = { gears, gear, parked, turbo, place: place && place.map (Number),
                      wipers: viewClamp (Math.round (+wipers) || 0, 0, 3), lights: [!! (lights && lights[0]), !! (lights && lights[1])],
                      test: test && { at: test.at || null, clock: test.clock || null, drive: +test.drive || 0, steer: +test.steer || 0, still: !! test.still },
                      on: ! document.hidden, ...measured () };

    // Where the canvas goes to a worker, everything else goes with it; else the view is here, on a canvas that
    // has WebGL 2 or never draws.
    let send;
    if (worker && typeof Worker === 'function' && typeof canvas.transferControlToOffscreen === 'function' && viewOffscreenWebGL2 ())
    {
        const offscreen = canvas.transferControlToOffscreen ();
        const w = new Worker (url, { type: 'module', name: world.name });
        w.onmessage = e => said (e.data);
        w.onerror = e => { e.preventDefault (); said ({ type: 'fallback', reason: 'the worker did not start: ' + (e.message || 'no module workers here') }); };
        w.postMessage ({ type: 'init', canvas: offscreen, options }, [offscreen]);
        send = m => w.postMessage (m);
        gone = () => w.terminate ();
    }
    else
    {
        if (! canvas.getContext ('webgl2', { ...viewAttributes, preserveDrawingBuffer: !! test })) { onFallback ('no WebGL 2'); return worldInert (world); }
        send = worldDrawn (canvas, options, said, world);
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
    // the drive. Parked, it only turns the view in place.
    let outside = 0, hand = 0, downX = null;
    const steered = () => send ({ type: 'steer', v: viewClamp (outside + hand, -1, 1) });
    const onDown = e => { downX = e.clientX; canvas.setPointerCapture (e.pointerId); };
    const onMove = e => { if (downX !== null) { hand = viewClamp ((e.clientX - downX) / Math.max (40, canvas.clientWidth * 0.25), -1, 1); steered (); } };
    const onUp = () => { downX = null; hand = 0; steered (); };
    if (steer)
    {
        canvas.style.touchAction = 'pan-y';          // a sideways drag on a phone steers, an upward one still scrolls
        canvas.addEventListener ('pointerdown', onDown);
        canvas.addEventListener ('pointermove', onMove);
        canvas.addEventListener ('pointerup', onUp);
        canvas.addEventListener ('pointercancel', onUp);
    }

    const view = {
        setGear (n, { settled = false } = {}) { send ({ type: 'gear', n, settled }); },
        setTurbo (on) { send ({ type: 'turbo', on: !! on }); },
        setPark (on, { settled = false } = {}) { send ({ type: 'park', on: !! on, settled }); },
        setSteer (v) { outside = viewClamp (+v || 0, -1, 1); steered (); },
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
    if (world.sky)
    {
        /** The lever anywhere along its travel, 0 CLEAN .. 1 HI-GAIN: the ship spools to its speed, or is at it at once. */
        view.setThrottle = (t, { settled = false } = {}) => send ({ type: 'throttle', t: viewClamp (+t || 0, 0, 1), settled: !! settled });
    }
    if (world.canopy)
    {
        /** The wipers' keys: 0 OFF, 1 INT, 2 LO, 3 HI. */
        view.setWipers = speed => send ({ type: 'wipers', speed: viewClamp (Math.round (+speed) || 0, 0, 3) });
        /** The searchlights' switches, left and right. */
        view.setLights = (left, right) => send ({ type: 'lights', left: !! left, right: !! right });
    }
    return view;
}

function worldInert (world)
{
    const view = { setGear () {}, setTurbo () {}, setPark () {}, setSteer () {}, start () {}, stop () {}, get quality () { return 0; }, ready: Promise.resolve (false), destroy () {} };
    if (world.canopy) { view.setWipers = () => {}; view.setLights = () => {}; }
    if (world.sky) view.setThrottle = () => {};
    return view;
}
