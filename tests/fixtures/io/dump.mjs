// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Io's prototype dumped for tests/IoWorldTests.cpp: `node dump.mjs <prototype dir> > io-world.txt`, the directory holding
// io-world.js, io-gl.js, io-shaders.js, cabin.html and finds/. A copy of the prototype is made in a temporary folder with
// its inner makers exported and a few lines of io-gl.js's drawScene made to hand over what it uploads (the hardware's and
// the rocks' instances, the plumes' bodies, the emitters); the prototype itself is not touched. Its frames are drawn by
// its own createIoView against a WebGL 2 that records the uniforms it is given and draws nothing. What the view parses
// out of the finds' files is stated here as its code has it, and checked to be its code word for word. One entry a line:
// label, kind, count, values. Kinds: d a double (within the test's tolerance), f an f32 value (exact), i an integer or
// a 32-bit FNV-1a hash (exact).

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const read = f => fs.readFileSync (path.join (dir, f), 'utf8');
const worldJs = read ('io-world.js'), glJs = read ('io-gl.js'), cabinHtml = read ('cabin.html');

// THE COPY: io-world.js with its makers exported; io-gl.js with its rocks' mesh exported and drawScene's uploads handed over
const tmp = fs.mkdtempSync (path.join (os.tmpdir (), 'io-dump-'));
fs.copyFileSync (path.join (dir, 'io-shaders.js'), path.join (tmp, 'io-shaders.js'));
fs.writeFileSync (path.join (tmp, 'io-world.js'), worldJs + '\nexport { makeDetail, makeNoise, makeCells, loopPoint, LOOP_S, sunLook };\n');
const patch = (src, find, add) =>
{
    const n = src.split (find).length - 1;
    if (n !== 1) throw new Error (`io-gl.js: "${find}" is there ${n} times, not once: the dump needs its hooks moved`);
    return src.replace (find, find + add);
};
let gl2 = glJs;
gl2 = patch (gl2, 'programs[what] = p;', ' p.label = what;');
gl2 = patch (gl2, 'b.nSh0 = nS;', ' (globalThis.__cap.structs ??= []).push ({ n: b.n, nSh: b.nSh, data: b.data.slice (0, b.nSh * 8) });');
gl2 = patch (gl2, 'for (const R of rocks) if (R.n) {', '');
gl2 = gl2.replace ('for (const R of rocks) if (R.n) {', 'globalThis.__cap.rocks = rocks.map (R => R.data.slice (0, R.n * 8)); for (const R of rocks) if (R.n) {');
gl2 = patch (gl2, 'vols.slice (0, n).forEach ((v, i) => volBuf.data.set (v.e, i * 12));', ' globalThis.__cap.vols = vols.slice (0, n).map (v => new Float32Array (v.e)); globalThis.__cap.volsIn = nIn;');
gl2 = patch (gl2, 'const counts = [PLUME_P, FOUNT_P, PUFF_P];', ' globalThis.__cap.emit = lists.map (l => l.slice (0, MAX_EMIT).map (e => new Float32Array (e)));');
fs.writeFileSync (path.join (tmp, 'io-gl.js'), gl2 + '\nexport { rockMesh };\n');

// THE STAGE: a page with no DOM, a WebGL 2 that records the uniforms
let raf = null;
globalThis.window = { devicePixelRatio: 1 };
globalThis.document = { hidden: false, addEventListener () {}, removeEventListener () {} };
globalThis.requestAnimationFrame = cb => { raf = cb; return 1; };
globalThis.cancelAnimationFrame = () => {};
globalThis.__cap = { u: {} };
const consts = new Map ();
const constOf = k => { if (! consts.has (k)) consts.set (k, 0x10000 + consts.size); return consts.get (k); };
const put = (loc, v, kind) => { if (loc) globalThis.__cap.u[`${loc.prog.label}.${loc.name}`] = { kind, v: Array.from (v) }; };
const impl = {
    getExtension: () => null, getShaderParameter: () => true, getProgramParameter: () => true, getShaderInfoLog: () => '', getProgramInfoLog: () => '',
    checkFramebufferStatus: () => constOf ('FRAMEBUFFER_COMPLETE'), getUniformLocation: (p, n) => ({ prog: p, name: n }), getAttribLocation: () => 0,
    getParameter: () => 0, isContextLost: () => false,
    uniform1f: (l, a) => put (l, [a], 'd'), uniform2f: (l, a, b) => put (l, [a, b], 'd'), uniform3f: (l, a, b, c) => put (l, [a, b, c], 'd'),
    uniform4f: (l, a, b, c, d) => put (l, [a, b, c, d], 'd'), uniform1i: (l, a) => put (l, [a], 'i'),
};
for (const k of ['uniform1fv', 'uniform2fv', 'uniform3fv', 'uniform4fv']) impl[k] = (l, a) => put (l, a, a instanceof Float32Array ? 'f' : 'd');
const gl = new Proxy ({}, { get (t, k)
{
    if (typeof k !== 'string') return undefined;
    if (/^[A-Z0-9_]+$/.test (k)) return constOf (k);
    if (k in impl) return impl[k];
    if (k.startsWith ('create')) return () => ({});
    return () => {};
} });
const canvas = { getContext: () => gl, clientWidth: 836, clientHeight: 470, dataset: {}, width: 0, height: 0 };

const W = await import (pathToFileURL (path.join (tmp, 'io-world.js')).href);
const G = await import (pathToFileURL (path.join (tmp, 'io-gl.js')).href);

const out = [];
const sha = t => crypto.createHash ('sha256').update (t).digest ('hex').slice (0, 16);
out.push (`# Io's prototype, io-world.js (sha256 ${sha (worldJs)}...) and io-gl.js (sha256 ${sha (glJs)}...), PERF PASS 20.11.2026, dumped by`
        + ` node ${process.version} (V8, ${process.arch}, ${process.platform}) for tests/IoWorldTests.cpp. One entry a line: label, kind, count,`);
out.push ('# values. Kinds: d a double (within the test\'s tolerance), f an f32 value (exact), i an integer or a 32-bit FNV-1a hash (exact).');
const E = (label, kind, values) => out.push (`${label} ${kind} ${values.length} ${values.map (v => String (typeof v === 'boolean' ? +v : v)).join (' ')}`);

/** FNV-1a over the bytes of a typed array (little-endian, as both sides lay them out), or of a string's UTF-8. */
const fnvBytes = b => { let h = 0x811c9dc5; for (let i = 0; i < b.length; ++i) { h ^= b[i]; h = Math.imul (h, 0x01000193) >>> 0; } return h >>> 0; };
const fnv = arr => fnvBytes (new Uint8Array (arr.buffer, arr.byteOffset, arr.byteLength));
const fnvS = s => fnvBytes (Buffer.from (s, 'utf8'));
const sample = (arr, n) => { const o = []; for (let k = 0; k < n; ++k) o.push (arr[Math.floor (k * arr.length / n)]); return o; };
const run = (w, seconds) => { const n = Math.round (seconds * 60); for (let i = 0; i < n; ++i) w.step (1 / 60); };
const fresh = ({ gear = 1, parked = false, knobs = null } = {}) =>
{
    const w = W.makeWorld ();
    w.setGear (gear); w.setParked (parked);
    if (knobs) for (const k of ['drive', 'blend', 'presence']) if (knobs[k] !== undefined) w.setKnob (k, knobs[k]);
    w.settle ();
    return w;
};
const sunV = S => [S.sep, S.vis, ...S.hue, S.light, S.flat, S.graze, S.ring, S.depth, S.u, S.eclAir, S.eclExpo, S.eclDark,
                   ...S.dir, S.el, S.az, S.eA, S.gate, S.night, S.dark, S.airglow, S.expo, S.cabin];
const st = (label, w) =>
{
    const s = w.state;
    E (`${label}.state`, 'd', [s.sr, s.lap, s.lat, s.latV, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.park, s.gear, s.parked,
                               s.drive, s.blend, s.presence, s.sunK, s.dark, s.jupLit, s.cabinDay, s.act, s.actE, s.actHold, s.auto, s.autoNext, s.autoN,
                               s.hitT, s.hitS, s.hits, s.dose, s.doseV, s.doseE, s.doseJ, s.doseN]);
    E (`${label}.sun`, 'd', sunV (s.sun));
    E (`${label}.puffs`, 'd', s.puffs.flatMap (p => [p.x, p.y, p.z, p.t, p.k, p.R, p.seed]));
    E (`${label}.hist`, 'i', [fnv (w.hist), ...sample (w.hist, 16)]);
};
const structV = q => [q.variant, q.x, q.z, q.y, q.yaw, q.k, q.lean[0], q.lean[1], q.onFlow, q.site, q.ci, q.cj, q.seed];

// THE HASHES, THE TEXTURES
E ('hash.ih', 'i', [0, 1, 12345, 0x7fffffff, 0x80000000, 4294967295].map (W.ih));
E ('hash.hcell', 'f', [[0, 0, 0], [1, 2, 3], [-5, 7, 101], [255, 255, 707], [-1, -1, 1010], [350.5, 352, 602]].map (([a, b, c]) => W.hcell (a, b, c)));
E ('hash.hs', 'f', [[1, 1], [7, 92], [120, 16], [0, 0], [241, 77], [-1003, 625]].map (([a, b]) => W.hs (a, b)));
E ('half', 'd', [0, 1e-6, -3e-5, 6.2e-5, 0.1, 0.3333, 1, 2.5, -17.77, 1000.3, 65519, 70000, -1e6].map (W.toHalf));
const w0 = fresh ();
{
    const T = w0.textures ();
    E ('tex.base', 'i', [T.base.length, fnv (T.base)]); E ('tex.base.sample', 'f', sample (T.base, 64));
    for (const k of ['detail', 'noise', 'cells', 'feat']) E (`tex.${k}`, 'i', [T[k].length, fnv (T[k]), ...sample (T[k], 32)]);
    E ('tex.road', 'i', [T.roadMap.length, fnv (T.roadMap)]); E ('tex.road.sample', 'f', sample (T.roadMap, 64));
    for (const k of ['detail', 'noise', 'cells']) { const a = W[`make${k[0].toUpperCase ()}${k.slice (1)}`] (); E (`tex.${k}.made`, 'i', [a.length, fnv (a)]); }
}
// THE LOOP
E ('loop.len', 'd', [W.LOOP_LEN]);
// (the tables of doubles are hashed as f32 and sampled densely as doubles: see tests/IoWorldTests.cpp, "V8 14's sin and cos")
E ('loop.table', 'd', sample (W.LOOP_S, 512));
E ('loop.table.hash', 'i', [W.LOOP_S.length, fnv (new Float32Array (W.LOOP_S))]);
{
    const at = [0, 1, 160, 777.7, 1500, 2600.25, 4100, 5100, -30, 6000];
    E ('loop.theta', 'd', at.map (W.thetaAt));
    E ('loop.point', 'd', at.flatMap (s => [...W.roadPointAt (s, 0), ...W.roadPointAt (s, 7.5), ...W.roadPointAt (s, -40)]));
    E ('loop.tangent', 'd', at.flatMap (s => [...W.tangentAt (W.thetaAt (s)), ...W.rightAt (W.thetaAt (s)), W.headingAt (s)]));
    E ('loop.near', 'd', [[24520, 24680], [25300, 24680], [23950, 25110], [24000, 24000]].flatMap (([x, z]) => { const n = W.nearestOnLoop (x, z); return [n.at, n.d]; }));
    E ('loop.basis', 'd', [[0, 0], [1.2, -0.05], [-2.9, 0.1]].flatMap (([h, p]) => { const b = G.cameraBasis (h, p); return [...b.r, ...b.u, ...b.f]; }));
}
// THE SKY
E ('sky.jupiter', 'd', [W.JUP_RHO, W.JUP_EL, W.JUP_AZ, ...W.JUP_DIR, ...W.JUP_FRAME.R, ...W.JUP_FRAME.U, ...W.JUP_FRAME.axis, W.SUN_RHO]);
E ('sky.moons', 'd', W.MOONS.flatMap (m => [...m.dir, m.rho, ...m.alb]));
const sweep = [0, 0.02, 0.058, 0.06, 0.1, 0.2, 0.3, 0.35, 0.42, 0.45, 0.5, 0.55, 0.6, 0.8, 1];
E ('sky.height', 'd', sweep.map (W.sunHeightAt));
E ('sky.sep', 'd', sweep.map (W.sunSepAt));
for (const d of [0, 0.3, 0.45, 0.5, 0.8, 1])
    for (const b of [0, 0.03, 0.058, 0.1, 0.2, 0.5, 1])
    {
        const S = W.sunSolve (d, b);
        E (`sky.solve.${d}.${b}`, 'd', sunV (S));
        const R = G.ringLevel (S), L = G.surfaceLight (S);
        E (`light.${d}.${b}`, 'd', [R.level, R.t, ...G.jupShineOf (S), ...L.dir, ...L.key, L.k, ...L.adir, ...L.amb]);
    }
E ('sky.path', 'd', [0, 0.2, 0.4, 0.41, 0.6, 0.9, 0.95, 1].flatMap (x => [W.pathSepAt (x), ...sunV (W.pathSun (x))]));
E ('sky.look', 'd', [[W.JUP_RHO + 0.1, 0.6], [W.JUP_RHO + 0.02, 0.6], [W.JUP_RHO - 0.01, 0.8]].flatMap (([s, l]) => sunV ({ ...W.sunLook (s, l), dir: [0, 0, 0] }).slice (0, 15)));

// THE WORLD AS MADE
{
    const pts = [[24520, 24680], [25280, 24700], [23800, 24300], [24110.5, 25402.25], [26010, 23020], [22900, 26500], [24800, 23900], [25000, 25000]];
    for (const c of w0.calderas) pts.push ([c.x + 3, c.z - 2], [c.x + c.R + c.W * 0.5, c.z]);
    for (const k of [0, 500, 1200, 2050, 3333, 4800]) { const [x, z] = W.roadPointAt (k, 0); pts.push ([x, z], [x + 9, z - 4]); }
    E ('height.floor', 'd', pts.map (([x, z]) => w0.floorHeight (x, z)));
    E ('height.terrain', 'd', pts.map (([x, z]) => w0.terrainH (x, z, 50)));
    E ('height.road', 'd', pts.flatMap (([x, z]) => w0.roadTap (x, z)));
    E ('height.feat', 'd', pts.flatMap (([x, z]) => w0.featTap (x, z)));
}
E ('calderas', 'd', w0.calderas.flatMap (c => [c.i, c.at, c.d, c.R, c.W, c.D, c.rim, c.lake, c.th, !! c.big, !! c.near, !! c.pit, c.x, c.z, c.baseY, c.out, c.lakeR]));
E ('plumes', 'd', w0.plumes.flatMap (p => [p.i, p.x, p.z, p.y, p.row, p.ring, p.near.at, p.near.d, p.H, p.th, p.kind === 'pele', p.base || 0, p.view ?? -1]));
E ('fountains', 'd', w0.fountains.flatMap (f => [f.i, f.cald, f.x, f.z, f.y, f.row, f.H, f.th, f.toward]));
E ('flows', 'i', [w0.flows.length, ...w0.flows.flatMap (f => [f.pts.length, fnv (new Float32Array (f.pts.flat ())), f.fissure ? 1 : 0])]);
E ('flows.sample', 'd', w0.flows.flatMap (f => f.pts.filter ((p, i) => i % 5 === 2).flat ()));
E ('flows.fresh', 'd', w0.flows.map (f => f.fresh));
E ('flows.pts', 'd', w0.flows.flatMap (f => [...f.pts[0], ...f.pts[f.pts.length - 1]]));
E ('sites', 'd', w0.sites ().flatMap (g => [g.at, g.items.length, ...g.items.flatMap (structV)]));
E ('sites.names', 'i', w0.sites ().map (g => fnvS (g.name)));
E ('stations', 'd', w0.stations ().flatMap (g => [g.at, g.seen, g.r, g.items.length, ...g.items.flatMap (structV)]));
E ('stations.names', 'i', w0.stations ().map (g => fnvS (g.name)));
{
    const P = w0.places ();
    E ('places.at', 'd', P.map (p => p.at)); E ('places.seen', 'd', P.map (p => p.seen));
    E ('places.names', 'i', P.map (p => fnvS (p.name))); E ('places.groups', 'i', P.map (p => fnvS (p.group)));
}
for (const [x, z] of [[24520 + 760, 24680], [W.roadPointAt (1300, 0)[0], W.roadPointAt (1300, 0)[1]], [23700, 25200]])
{
    const list = w0.structuresNear (x, z);
    E (`structs.near.${Math.round (x)}.${Math.round (z)}`, 'd', [list.length, ...list.flatMap (structV)]);
}
E ('structs.at', 'd', [[349, 352], [356, 340], [-3, 7], [1000, 352], [341, 361]].flatMap (([i, j]) => { const q = w0.structureAt (i, j); return q ? [1, ...structV (q)] : [0]; }));
for (const [s, fx, fz] of [[0, 0, -1], [870, 0.6, 0.8], [3100, -1, 0]])
{
    const [x, z] = W.roadPointAt (s, 1.9), list = w0.rocksNear (x, z, fx, fz, 85);
    E (`rocks.${s}`, 'd', [list.length, ...list.flatMap (r => [r.x, r.z, r.y, r.yaw, r.sx, r.sy, r.sz, r.tint])]);
}
E ('act.curves', 'd', [0, 0.1, 0.18, 0.3, 0.55, 0.7, 0.9, 1].flatMap (b => [...w0.plumes.map (p => w0.plumeAct (p, b)), ...w0.fountains.map (f => w0.fountainAct (f, b)), ...w0.calderas.map (c => w0.lakeAct (c, b))]));
// THE MESHES
E ('mesh.struct', 'i', W.structureMeshes ().flatMap (m => [m.length, fnv (m)]));
E ('mesh.rock', 'i', [true, false].flatMap (f => { const m = G.rockMesh (f); return [m.length, fnv (m)]; }));
{
    const w = 16, h = 8, rgba = new Uint8Array (w * h * 4).fill (255);
    for (const [x, y] of [[5, 2], [9, 3], [7, 6]]) rgba[(y * w + x) * 4 + 3] = 100;
    E ('glass', 'd', [...G.glassBounds (rgba, w, h, 2), G.glassBounds (new Uint8Array (16).fill (255), 2, 2, 1) === null]);
}

// THE DRIVE, THE KNOBS, THE VOLCANOES, THE DOSIMETER
{   const w = fresh (); st ('drive1.t0', w); run (w, 12); st ('drive1.t12', w); run (w, 30); st ('drive1.t42', w); }
{   const w = fresh ({ gear: 2 }); run (w, 20); st ('drive2.t20', w); }
{
    const w = fresh ({ gear: 2, parked: true });
    run (w, 8); st ('park.in', w);
    w.setParked (false); run (w, 2); st ('park.pull2', w); run (w, 5); st ('park.out', w);
    w.setParked (true); run (w, 3); st ('park.again', w);
    w.setParked (false); run (w, 1); w.setGear (1); run (w, 3); st ('park.regear', w);
}
{
    const w = fresh ();
    w.setKnob ('drive', 0.5); run (w, 0.5); st ('knobs.drive05', w); run (w, 3); st ('knobs.drive35', w);
    w.setKnob ('blend', 0); run (w, 1); st ('knobs.blend1', w); run (w, 5); st ('knobs.eclipse', w);
    w.setKnob ('presence', 1); run (w, 2); st ('knobs.presence', w);
    w.setKnob ('drive', 1); w.setKnob ('blend', 0.2); run (w, 6); st ('knobs.night', w);
}
{   const w = fresh ({ knobs: { drive: 0.8, blend: 0.058, presence: 0.6 } }); st ('knobs.settled', w); run (w, 1); st ('knobs.settled1', w); }
{   const w = fresh (); run (w, 2); w.hit (0.8); run (w, 0.3); st ('hit.03', w); run (w, 2); st ('hit.23', w); }
{   const w = fresh (); w.setAuto (true); run (w, 15); st ('auto.15', w); w.setAuto (false); run (w, 12); st ('auto.off12', w); }
{   const w = fresh (); w.setActivity (0.7); run (w, 4); st ('activity.held', w); w.setActivity (0.3, false); run (w, 6); st ('activity.free', w); }
{
    const w = fresh (), P = w.places ();
    for (const k of [0, 5, 13, 21, P.length - 1])
    {
        const pl = P[k], at = (((pl.at - pl.seen - 3 * w.speedOf ()) % W.LOOP_LEN) + W.LOOP_LEN) % W.LOOP_LEN;
        w.setAt (at, w.state.lap);
        E (`jump.${k}`, 'd', [Math.round (at), w.state.lap]);
        run (w, 1); st (`jump.${k}`, w);
    }
}
// THE DOSIMETER'S NEEDLE, as cabin.html's dial and io-gl.js's post pass turn it
for (const line of ['const dial = { x: 119.2, y: 564.9, r: 36.8, from: 210, to: -30 };'])
    if (! cabinHtml.includes (line)) throw new Error ('cabin.html: its dial is not the dump\'s: ' + line);
for (const line of ["dial = d ? { x: d.x, y: d.y, r: d.r, from: (d.from ?? 210) * DEG, to: (d.to ?? -30) * DEG, stage } : null;",
                    'const a = dial.from + (dial.to - dial.from) * clamp (s.dose, 0, 1);', "gl.uniform2f (p.u ('uNeedle'), Math.cos (a), -Math.sin (a));"])
    if (! glJs.includes (line)) throw new Error ('io-gl.js: the needle is not the dump\'s: ' + line);
{
    const DEG = Math.PI / 180, from = 210 * DEG, to = -30 * DEG;
    E ('needle', 'd', [-0.2, 0, 0.13, 0.5, 0.9, 1, 1.3].flatMap (d => { const a = from + (to - from) * W.clamp (d, 0, 1); return [Math.cos (a), -Math.sin (a)]; }));
}

// THE FINDS' FILES, as the view reads them (its code, checked to be io-gl.js's)
{
    const code = ["const creditOf = txt => { const m = txt.match (/credit:?\\s*([^\\n]*)/); return m && ! /^none\\b/i.test (m[1].trim ()) ? m[1].trim () : null; };",
                  "const pr = role === 7 && t.match (/(?:pit|radius)\\s*[:=]?\\s*([\\d.]+)/i);",
                  "const roleOf = name => { const n = name.toLowerCase (); return VARIANTS.findIndex (v => n.startsWith (v)); };",
                  "if (new TextDecoder ().decode (new Uint8Array (buf, 0, 7)) !== 'CBMESH1' || buf.byteLength < 16 + nv * 40 + ni * 4) return;"];
    for (const line of code) if (! glJs.includes (line)) throw new Error ('io-gl.js: the readers are not the dump\'s: ' + line);
    const creditOf = txt => { const m = txt.match (/credit:?\s*([^\n]*)/); return m && ! /^none\b/i.test (m[1].trim ()) ? m[1].trim () : null; };
    const pitOf = t => { const pr = t.match (/(?:pit|radius)\s*[:=]?\s*([\d.]+)/i); return pr ? +pr[1] : null; };
    const roleOf = name => { const n = name.toLowerCase (); return W.VARIANTS.findIndex (v => n.startsWith (v)); };
    const meshOf = buf =>
    {
        if (buf.byteLength < 16) return [0, 0, 0];
        const dv = new DataView (buf.buffer, buf.byteOffset, buf.byteLength), nv = dv.getUint32 (8, true), ni = dv.getUint32 (12, true);
        if (new TextDecoder ().decode (buf.subarray (0, 7)) !== 'CBMESH1' || buf.byteLength < 16 + nv * 40 + ni * 4) return [0, nv, ni];
        return [1, nv, ni];
    };
    const folders = fs.readdirSync (path.join (dir, 'finds')).filter (d => /3d$/.test (d)).sort ();
    E ('finds.roles', 'i', [...folders, 'Mast3d', 'bus3d', 'stationary'].map (roleOf));
    for (const d of folders)
    {
        const name = d.replace (/3d$/, ''), txt = fs.readFileSync (path.join (dir, 'finds', d, `${name}.txt`));
        const t = txt.toString ('utf8'), c = creditOf (t), pr = roleOf (d) === 7 ? pitOf (t) : null;
        E (`finds.${d}.txt`, 'i', [txt.length, fnvBytes (txt), c === null ? 0 : 1, c === null ? 0 : fnvS (c), pr === null ? 0 : 1]);
        if (pr !== null) E (`finds.${d}.pit`, 'd', [pr]);
    }
    for (const d of ['dish3d'])
    {
        const b = fs.readFileSync (path.join (dir, 'finds', d, `${d.replace (/3d$/, '')}.mesh`));
        E (`finds.${d}.mesh`, 'i', [b.length, fnvBytes (b), ...meshOf (new Uint8Array (b)), ...meshOf (new Uint8Array (b.subarray (0, b.length - 1))), ...meshOf (new Uint8Array (b.subarray (0, 12)))]);
    }
    const own = ['credit: none: our own model', 'credit:  Jane Doe, CC-BY 4.0 \nmore', 'credit:None of ours', 'nothing here', 'Credit: nonesuch',
                 'pit radius 13.0', 'a pit at 5 m, radius: 7.25', 'radius=.5', 'pit 1.2.3'];
    E ('finds.credit', 'i', own.map (t => { const c = creditOf (t); return c === null ? 0 : fnvS (c); }));
    E ('finds.pitOf', 'd', own.map (t => { const p = pitOf (t); return p === null ? -1 : Number.isNaN (p) ? -2 : p; }));
}

// THE FRAMES: the prototype's own createIoView, its drawScene's uniforms and uploads
const FRAME_U = {
    ground: ['uSun', 'uSunCol', 'uSunK', 'uJup', 'uJupShine', 'uDark', 'uTerm', 'uAirglow', 'uJupSunCol', 'uFlat', 'uAir', 'uPL[0]', 'uPLC[0]', 'uPLN',
             'uSL[0]', 'uSLN', 'uSLC', 'uSLBox', 'uHitT', 'uCaldA[0]', 'uCaldB[0]', 'uCaldC[0]', 'uCaldN', 'uCamCell', 'uCamFrac', 'uCamRel', 'uOff0', 'uOff1',
             'uOff2', 'uTime', 'uLamps', 'uLoopC', 'uRoadOrg', 'uFeatOrg'],
    sky: ['uSun', 'uSunCol', 'uSunK', 'uJup', 'uJupShine', 'uGKey', 'uGKeyCol', 'uGAmb', 'uSunUp', 'uJupK', 'uAurora', 'uFlux', 'uFar', 'uSep', 'uJupT',
          'uRho', 'uJupSin', 'uJupCos', 'uMoon[0]', 'uMoonAlb[0]', 'uJupR', 'uJupU', 'uJupAxis'],
    snow: ['uExpo', 'uTravel', 'uSpeed', 'uSnow', 'uSunV', 'uLavaEnv'],
};
const tick = (label) =>
{
    globalThis.__cap = { u: {} };
    raf (1000 * (1 + out.length));
    const C = globalThis.__cap;
    for (const [prog, names] of Object.entries (FRAME_U))
        for (const n of names)
        {
            const u = C.u[`${prog}.${n}`];
            if (! u) throw new Error (`frame ${label}: no ${prog}.${n}`);
            E (`frame.${label}.${prog}.${n}`, u.kind, u.v);
        }
    E (`frame.${label}.structs`, 'i', (C.structs || []).flatMap (b => [b.n, b.nSh, b.data.length, fnv (b.data)]));
    E (`frame.${label}.rocks`, 'i', (C.rocks || []).flatMap (r => [r.length, fnv (r)]));
    E (`frame.${label}.vols`, 'f', [(C.vols || []).length, C.volsIn || 0, ...(C.vols || []).flatMap (v => Array.from (v))]);
    for (let k = 0; k < 3; ++k) E (`frame.${label}.emit${k}`, 'f', [((C.emit || [])[k] || []).length, ...((C.emit || [])[k] || []).flatMap (v => Array.from (v))]);
};
{   // by day, Jupiter dead ahead; an attack
    const v = G.createIoView (canvas, { stepFixed: 0 }), w = v._world;
    w.setAt (87, 0); run (w, 1);
    tick ('day1'); tick ('day2');
    w.hit (1); run (w, 0.3); tick ('hit');
    st ('frame.hit', w);
}
{   // at night, the volcanoes awake, by the lava lake nearest the road
    const v = G.createIoView (canvas, { stepFixed: 0, knobs: { drive: 0.8, blend: 1, presence: 0.7 } }), w = v._world;
    w.setActivity (1);
    const pl = w.places ().find (p => p.name === "the road's closest pass to a lava lake");
    w.setAt (pl.at - 120, 0); run (w, 2);
    tick ('night1'); tick ('night2');
    w.hit (0.9); run (w, 0.2); tick ('night3');
    st ('frame.night', w);
}
{   // a low Sun over the skyline, then the eclipse
    const v = G.createIoView (canvas, { stepFixed: 0, gear: 2, knobs: { drive: 0.45, blend: 0.3 } }), w = v._world;
    w.setAt (87, 0); run (w, 1);
    tick ('dusk1'); tick ('dusk2');
    w.setKnob ('drive', 0.47); w.setKnob ('blend', 0.0); w.settle (); run (w, 0.5);
    tick ('eclipse');
    st ('frame.eclipse', w);
}

fs.rmSync (tmp, { recursive: true, force: true });
process.stdout.write (out.join ('\n') + '\n');
