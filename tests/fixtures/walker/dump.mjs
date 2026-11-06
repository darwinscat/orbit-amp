// Dumps walker-world.js's state for WalkerWorldTests.cpp: `node dump.mjs <walker-world.js> [find]`.
// One line per entry: `<label> <kind> <values...>`; kind d = a double (1e-6), x = a double that must be exact,
// f = an f32 value (exact), i = an integer or a hash (exact). The C++ test makes the same labels the same way.
const DENSE_A = [2952, 6757], DENSE_B = [7243, 5583];
import { pathToFileURL } from 'node:url';
const W = await import (pathToFileURL (process.argv[2]).href);
const lines = [];
const out = (label, kind, vals) => lines.push (`${label} ${kind} ${vals.length} ${vals.map (v => String (v)).join (' ')}`);
const fnv = it => { let h = 0x811c9dc5; for (const w of it) { h ^= w; h = Math.imul (h, 0x01000193); } return h >>> 0; };
const words = fa => new Uint32Array (fa.buffer, fa.byteOffset, fa.length);
const sum = a => { let t = 0; for (const v of a) t += v; return t; };
const DT = 1 / 60;

const SCALARS = ['x', 'z', 'heading', 'heading0', 'pitch', 'roll', 'nod', 'time', 'travel', 'rate', 'stepLen', 'speed', 'psi',
    'heave', 'sway', 'eyeX', 'eyeZ', 'camY', 'groundY', 'pace', 'brake', 'squeeze', 'spores', 'storm', 'breath', 'bass',
    'hit', 'hitGlow', 'beat', 'bpm', 'footfall'];
function dumpState (label, w, full = false)
{
    const s = w.state;
    out (label + '.s', 'd', SCALARS.map (k => s[k]));
    for (let i = 0; i < 2; ++i)
    {
        const L = s.legs[i];
        out (`${label}.leg${i}`, 'd', [L.side, L.off, ...L.foot, ...L.from, ...L.to, L.lift, ...L.hip, ...L.knee, ...L.pos]);
    }
    out (label + '.n', 'i', [s.burstAt, s.printAt, s.legs[0].swing ? 1 : 0, s.legs[1].swing ? 1 : 0, fnv (words (s.bursts)), fnv (words (s.prints))]);
    out (label + '.sums', 'd', [sum (s.bursts), sum (s.prints)]);
    if (full) { out (label + '.bursts', 'f', Array.from (s.bursts)); out (label + '.prints', 'f', Array.from (s.prints)); }
}
function make ({ gear = 1, bpm = 0, turbo = false, at = null } = {})
{
    const w = W.makeWorld ();
    if (at) { w.state.x = w.state.eyeX = at[0]; w.state.z = w.state.eyeZ = at[1]; }
    w.setGear (gear); w.setTurbo (turbo); w.setBpm (bpm);
    w.build (); w.settle ();
    return w;
}
const run = (w, steps, each) => { for (let i = 0; i < steps; ++i) { if (each) each (w, i); w.step (DT); } };

if (process.argv[3] === 'find')
{
    // A start that walks into a dense cluster: the rings give (squeeze), the stride shortens, the heading swings.
    const res = [];
    for (let i = 0; i < 12; ++i)
        for (let j = 0; j < 12; ++j)
        {
            const at = [500 + 613 * i, 300 + 587 * j], w = make ({ gear: 2, at });
            let minBrake = 1, maxSq = 0, turn = 0, h = w.state.heading;
            run (w, 60 * 40, () => { const s = w.state; minBrake = Math.min (minBrake, s.brake); maxSq = Math.max (maxSq, s.squeeze); turn += Math.abs (s.heading - h); h = s.heading; });
            res.push ({ at, minBrake, maxSq, turn });
        }
    res.sort ((a, b) => (b.maxSq - a.maxSq) || (a.minBrake - b.minBrake));
    for (const r of res.slice (0, 8)) console.log (JSON.stringify (r));
    process.exit (0);
}

// The hashes and the noise's corners.
out ('hash.ih', 'i', [0, 1, 12345, 0x7fffffff, 0xffffffff, -5].map (W.ih));
out ('hash.hcell', 'f', [[0, 0, 0], [3, 7, 101], [-5, 7, 117], [4095, 4095, 133], [204, -1, 1101], [-2147483648, 2147483647, 5505]].map (a => W.hcell (...a)));
// Transcendentals at the constant arguments the drive feeds (exact, or the port's choices could part).
out ('math.stalk', 'x', [0, 1, 2].flatMap (v => [W.stalkRadius (v, 0.3), W.stalkRadius (v, 0), W.stalkRadius (v, 1)]));
out ('math.ease', 'x', [0.45, 0.6, 0.7, 0.4, 1.1, 0.06, 0.8, 1.2, W.HIT_FLASH, W.HIT_GLOW].map (t => 1 - Math.exp (-DT / t)));
out ('math.shapes', 'd', [0, 0.05, 0.1, 0.3, 0.5, 0.77, 0.999].flatMap (u => [W.stepRise (u), W.stepThud (u)]));
{
    const k = [0, 0, 0];
    W.kneeOf ([2.1, 3.0, 0.6], [2.7, 0.4, -5.2], W.FEMUR, W.TIBIA, [0.15, 0.55, -1], k); out ('math.knee', 'd', k.slice ());
    W.kneeOf ([2.1, 3.0, 0.6], [2.1, 3.0, 0.6], W.FEMUR, W.TIBIA, [0.15, 0.55, -1], k); out ('math.knee0', 'd', k.slice ());
    W.kneeOf ([-2.1, 3.0, 0.6], [-9, -6, -12], W.FEMUR, W.TIBIA, [-0.15, 0.55, -1], k); out ('math.kneeFar', 'd', k.slice ());
}

// The meshes the renderer makes.
const meshOut = (label, m, stride) => out (label, 'i', [m.verts.length, m.idx.length, fnv (words (m.verts)), fnv (m.idx)]);
for (let k = 0; k < W.CLASSES.length; ++k)
    for (let v = 0; v < W.VARIANTS; ++v) { const m = W.mushroomMesh (v, W.CLASSES[k].segs); meshOut (`mesh.mush${k}${v}`, m); out (`mesh.mush${k}${v}.sum`, 'd', [sum (m.verts)]); }
{
    const PI = W.PI;
    const sphere = (r, n) => { const p = []; for (let i = 1; i < n; ++i) { const a = -PI / 2 + PI * i / n; p.push ([r * Math.cos (a), r * Math.sin (a)]); } return p; };
    const pieces = {
        femur: [[0.30, 0.0], [0.44, 0.06], [0.42, 0.45], [0.36, 0.92], [0.28, 1.0]],
        tibia: [[0.30, 0.0], [0.31, 0.05], [0.25, 0.45], [0.19, 0.93], [0.15, 1.0]],
        joint: sphere (0.56, 12),
        foot: [[0.60, 0.0], [0.95, 0.05], [0.95, 0.20], [0.70, 0.34], [0.38, 0.46], [0.30, 0.58]],
    };
    for (const [name, p] of Object.entries (pieces)) { const m = W.lathe (p, 28); meshOut (`mesh.${name}`, m); out (`mesh.${name}.sum`, 'd', [sum (m.verts)]); }
    out ('mesh.jointProfile', 'd', pieces.joint.flat ());
    const g = W.polarGrid (68, 96, 0.5, 420.0); meshOut ('mesh.ground', g); out ('mesh.ground.sum', 'd', [sum (g.verts)]);
}

// The textures and the ground under the start.
{
    const w = make ();
    const { mips, detail } = w.textures ();
    for (let l = 0; l < mips.length; ++l) out (`tex.mip${l}`, 'i', [mips[l].length, fnv (words (mips[l]))]);
    const px = mips[0], at = [[0, 0], [17, 300], [511, 511], [256, 128], [400, 3]];
    out ('tex.samples', 'f', at.flatMap (([x, y]) => Array.from (px.subarray ((y * 512 + x) * 4, (y * 512 + x) * 4 + 4))));
    out ('tex.mip1samples', 'f', Array.from (mips[1].subarray (0, 8)));
    out ('tex.detail', 'i', [detail.length, fnv (detail), ...Array.from (detail.subarray (0, 8)), ...Array.from (detail.subarray (4 * (64 * 33 + 17), 4 * (64 * 33 + 17) + 4))]);
    out ('ground', 'd', [[2033, 2125], [0.3, 8191.7], [5000.25, 123.5], [-3, -7], [1023.9, 1024.1]].map (([x, z]) => w.groundH (x, z)));
    // The mushrooms of a few cells, every field.
    const F = ['x', 'z', 'size', 'variant', 'hue', 'phase', 'rot', 'lean', 'glow', 'stalkR', 'clear', 'capR', 'capY'];
    for (const [k, ci, cj] of [[0, 1016, 1062], [0, -3, 4100], [1, 254, 265], [1, 255, 265], [1, 1030, -2], [2, 50, 53], [2, 51, 53], [2, -1, 204]])
    {
        const m = w.mushroomAt (k, ci, cj);
        out (`mush.${k}.${ci}.${cj}`, 'd', m ? [1, ...F.map (f => m[f])] : [0]);
    }
    // The start window's instance lists, as the renderer places it.
    const s = w.state, fx = Math.sin (s.heading), fz = -Math.cos (s.heading);
    for (let k = 0; k < W.CLASSES.length; ++k)
    {
        const C = W.CLASSES[k], shift = C.w * C.cell * 0.3;
        const bx = Math.floor ((s.x + fx * shift) / C.cell) - C.w / 2, bz = Math.floor ((s.z + fz * shift) / C.cell) - C.w / 2;
        const per = w.instancesOf (k, bx, bz);
        out (`inst.${k}.base`, 'i', [bx, bz]);
        for (let v = 0; v < W.VARIANTS; ++v)
        {
            out (`inst.${k}.${v}`, 'i', [per[v].length, fnv (words (per[v]))]);
            out (`inst.${k}.${v}.first`, 'f', Array.from (per[v].subarray (0, 8)));
        }
    }
    dumpState ('start', w, true);
}

// The walks: gears 1 and 2, free and at 100 bpm; 1, 5, 20 and 120 s.
for (const gear of [1, 2])
    for (const bpm of [0, 100])
    {
        const w = make ({ gear, bpm });
        let t = 0;
        for (const T of [1, 5, 20, 120]) { run (w, (T - t) * 60); t = T; dumpState (`walk.g${gear}.b${bpm}.t${T}`, w); }
    }
// Gear 3: TURBO free, and the tempo lock with TURBO; a shift from 1 to 2 at 100 bpm, TURBO on and off.
for (const [label, opts] of [['g3turbo', { gear: 3, turbo: true }], ['g3b100turbo', { gear: 3, bpm: 100, turbo: true }], ['g2turbo', { gear: 2, turbo: true }]])
{
    const w = make (opts);
    let t = 0;
    for (const T of [1, 5, 20]) { run (w, (T - t) * 60); t = T; dumpState (`walk.${label}.t${T}`, w); }
}
{
    const w = make ({ gear: 1, bpm: 100 });
    run (w, 600); w.setGear (2); run (w, 60); dumpState ('shift.t11', w); run (w, 240); dumpState ('shift.t15', w);
    w.setTurbo (true); run (w, 300); dumpState ('shift.t20', w); w.setGear (3); w.setTurbo (false); w.setBpm (0); run (w, 300); dumpState ('shift.t25', w);
}

// The bass: a held low note, then a jump in the level (an onset), then an attack by hand.
{
    const w = make ({ gear: 2 });
    run (w, 180, w => w.setBass (0.85)); dumpState ('bass.t3', w);
    run (w, 12, w => { w.setBass (0); w.setLevel (0.9); }); run (w, 18, w => w.setLevel (0)); dumpState ('bass.t3_5', w, true);
    run (w, 90); w.hit (0.8); dumpState ('bass.hit', w, true);
    run (w, 30); w.hit (); w.hit (0.3); run (w, 60); dumpState ('bass.t7', w, true);
}

// A drag across the glass: 5 s free, right at 0.8 for 6 s, left at full for 3 s, let go for 10 s.
{
    const w = make ({ gear: 2 });
    run (w, 300); dumpState ('steer.t5', w);
    run (w, 360, w => w.setSteer (0.8)); dumpState ('steer.t11', w);
    run (w, 180, w => w.setSteer (-1)); dumpState ('steer.t14', w);
    run (w, 600, w => w.setSteer (0)); dumpState ('steer.t24', w);
}

// Through dense clusters (found by `find`): a trace every half second, so a different choice shows where it is made.
for (const [label, at, gear] of [['dense.a', DENSE_A, 2], ['dense.b', DENSE_B, 2], ['dense.c', DENSE_A, 1]])
{
    const w = make ({ gear, at });
    for (let i = 1; i <= 120; ++i) { run (w, 30); const s = w.state; out (`${label}.trace${i}`, 'd', [s.x, s.z, s.heading, s.brake, s.squeeze, s.pace]); }
    dumpState (`${label}.end`, w);
}

// Across the wrap: x past WORLD_PERIOD and z below 0, so the legs, bursts, prints and dead ends shift with it.
{
    const w = make ({ gear: 2, at: [8186, 6] });
    run (w, 240, w => w.setSteer (0.6)); dumpState ('wrap.t4', w, true);
    run (w, 360, w => w.setSteer (0.6)); dumpState ('wrap.t10', w, true);
    run (w, 600, w => w.setSteer (0)); dumpState ('wrap.t20', w, true);
}
// Before build: no ground, every grove dense.
{
    const w = W.makeWorld (), F = ['x', 'z', 'size', 'variant', 'hue', 'phase', 'rot', 'lean', 'glow', 'stalkR', 'clear', 'capR', 'capY'];
    const m = w.mushroomAt (1, 254, 265), q = w.mushroomAt (0, 1016, 1062);
    out ('prebuild', 'd', [w.groundH (2033, 2125), m ? 1 : 0, ...(m ? F.map (f => m[f]) : []), q ? 1 : 0]);
}
process.stdout.write (lines.join ('\n') + '\n');
