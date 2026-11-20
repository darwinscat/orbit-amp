// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The metal world's prototype dumped for tests/MetalWorldTests.cpp: `node dump.mjs <prototype dir> > metal-world.txt`, the
// directory holding metal-world.js, metal-gl.js and finds/ (the dragon's files are where the page finds them,
// <prototype dir>/../rogue/finds/dragon3d). The prototype is not touched: metal-world.js is copied into a temporary
// folder with its makers exported too; what metal-gl.js works out on the CPU (the fires, the ribbons, the readers of
// the finds' files, the letters' atlas, the guitars dealt round the groves, glassBounds) is cut out of its own text by
// its own lines and run as it is, with the browser's parts (the canvas, the GL, the fetches) stubbed. One entry a line:
// label, kind, count, values. Kinds: d a double (within the test's tolerance), f an f32 value (exact), i an integer, a
// 32-bit FNV-1a hash or a string's UTF-8 bytes (exact).
//
// The finds are applied to every world in one fixed order (MetalWorld::furnish): the dragon's head, the letters' table,
// the guitars, then the landmarks' tops in the finds' folders' order by name. Listings are read by name, as a server's
// directory listing sorts them.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const findsDir = path.join (dir, 'finds');
const dragonBase = path.join (dir, '..', 'rogue', 'finds', 'dragon3d', 'dragon');
const worldSrc = fs.readFileSync (path.join (dir, 'metal-world.js'), 'utf8');
const glSrc = fs.readFileSync (path.join (dir, 'metal-gl.js'), 'utf8');
const tmp = fs.mkdtempSync (path.join (os.tmpdir (), 'metal-dump-'));
fs.writeFileSync (path.join (tmp, 'metal-world.js'), worldSrc
    + '\nexport { makeDetail, makeNoise, makeCells, LOOP_S, loopPoint, frontAt, headingAt, STUDS_N };\n');

// metal-gl.js's own lines, by their text.
const lineStart = i => glSrc.lastIndexOf ('\n', i) + 1;
const lineEnd = i => { const e = glSrc.indexOf ('\n', i); return e < 0 ? glSrc.length : e; };
const at = (text, after = 0) => { const i = glSrc.indexOf (text, after); if (i < 0) throw new Error (`metal-gl.js has no "${text}"`); return i; };
/** From the line holding `from` through the line holding `to` (or up to the line before it). */
const cut = (from, to, { after = 0, before = false } = {}) =>
{
    const a = at (from, after), b = at (to, a + from.length);
    return glSrc.slice (lineStart (a), before ? lineStart (b) : lineEnd (b));
};
const line = (text, after = 0) => { const a = at (text, after); return glSrc.slice (lineStart (a), lineEnd (a)); };
/** A function by its braces. */
const fn = name =>
{
    const a = at (`function ${name} `);
    let i = glSrc.indexOf ('{', a), depth = 0;
    for (; i < glSrc.length; ++i) { if (glSrc[i] === '{') ++depth; else if (glSrc[i] === '}' && --depth === 0) break; }
    return glSrc.slice (lineStart (a), i + 1);
};
const scene = at ('function drawScene');
const parts = `
import { DRAGON, smoothstep, modL, LOOP_LEN, clamp } from './metal-world.js';
${line ('const ZN = 0.5, ZF = 9000.0, FAR_CUT = 1500.0;')}
${line ('const FIRE_P = 48, MAX_EMIT = 96, MAX_RIB = 2400;')}
${line ('const norm3 = v =>')}
const noop = () => {};
const ctx2d = new Proxy ({}, { get: () => noop, set: () => true });
const document = { createElement: () => ({ width: 0, height: 0, getContext: () => ctx2d }) };
const glStub = new Proxy ({}, { get: () => () => null });
${fn ('glassBounds')}
${fn ('cameraBasis')}
${fn ('censoredCanvas')}
export function fires (world, s)
{
    ${line ('const ccx = Math.floor (s.x), ccz = Math.floor (s.z);', scene)}
    ${line ('const fx = Math.sin (s.heading), fz = -Math.cos (s.heading);', scene)}
    const fireBuf = { data: new Float32Array (MAX_EMIT * 12) };
${cut ('const hitAge = Math.min (s.hitT, 1e4);', 'lights.slice (0, 2).forEach', { after: scene })}
    return { em: fireBuf.data.slice (0, ne * 12), n: ne, pl };
}
export function ribbons (world, s, skip = new Set ())
{
    ${line ('const ccx = Math.floor (s.x), ccz = Math.floor (s.z);', scene)}
    ${line ('const time = s.time % 100000;', scene)}
    ${line ('const hitAge = Math.min (s.hitT, 1e4);', scene)}
    const ribBuf = { data: new Float32Array (MAX_RIB * 16) };
${cut ('// THE RIBBONS: the lightning', 'const setCommon = p =>', { after: scene, before: true })}
    return { rb: ribBuf.data.slice (0, nr * 16), n: nr };
}
export function letters (t, pngs, dims, world)
{
${cut ('const rows = {};', 'const items = Object.values (rows);')}
    let got = items.map (it => dims[it.file] ? { ...it, img: { width: dims[it.file][0], height: dims[it.file][1] } } : null);
    let texLetters = null, lettersAtlas = [2048, 1024], lettersOwn = false, censoredCell = null;
    const extra = [], texFromImage = () => 0;
${cut ('got = got.filter (Boolean);', 'world.buildLetters (table); censoredCell')}
    return { table, lettersAtlas, censoredCell, got, texLetters, lettersOwn };
}
${cut ('const parseHeadstocks = txt =>', '}));')}
${line ('const creditOf = txt =>')}
${cut ('const roleOf = name =>', ': name;')}
export { parseHeadstocks, creditOf, roleOf };
export function guitars (t, world)
{
    let headstocks = [], guitarSet = [];
    const loadMesh = noop, finds = { dir: '' }, gd = '';
${cut ('const set = [];', 'const slots = world.placeGuitars (items);')}
    chosen.forEach ((g, i) => { ${line ('slots[i].name = g.name.replace')} });
    return { set, chosen, items, slots, headstocks };
}
export function readMesh (buf)
{
    const gl = glStub;
${cut ('if (! buf || buf.byteLength < 16) return;', 'if (z > m.zmax) m.zmax = z; }')}
    return m;
}
export function landmarkTop (world, role, m)
{
${cut ('const lm = world.landmarks.find (q => q.role === role);', 'if (lm) world.landmarkCame (role, (m.zmax)')}
}
export function dragonHead (anim, meshNv)
{
    const dv = new DataView (anim), tryAt = noop, i = 0;
${line ("if (new TextDecoder ().decode (new Uint8Array (anim, 0, 7)) !== 'CBANIM1')")}
${line ('const nv = dv.getUint32 (8, true), nf = dv.getUint32 (12, true), unit = dv.getFloat32 (20, true);')}
    const m = { nv: meshNv };
${line ('if (m.nv !== nv) return;')}
    let out = null;
    const world = { setDragonHead: h => { out = h; } };
${cut ('const P3 = (f, i) =>', 'world.setDragonHead ({ mouth, mid });')}
    return out;
}
export function banners (t, w, h)
{
    const bannerCells = [], id = 1;
    let texBanners = null;
${cut ("for (const line of t.split ('\\n'))", 'if (bannerCells.length) texBanners = id;', { after: at ("the banners' art") })}
    return bannerCells;
}
export function jumpTo (world, pl)
{
${cut ('const at = (((pl.at - pl.seen - 3 * world.speedOf ())', 'return { s: Math.round (at), lap };')}
}
`;
fs.writeFileSync (path.join (tmp, 'gl-parts.mjs'), parts);
const W = await import (pathToFileURL (path.join (tmp, 'metal-world.js')).href);
const G = await import (pathToFileURL (path.join (tmp, 'gl-parts.mjs')).href);

const out = [];
const sha = f => crypto.createHash ('sha256').update (f).digest ('hex').slice (0, 16);
out.push (`# The metal world's prototype, metal-world.js (sha256 ${sha (worldSrc)}...) and metal-gl.js (sha256 ${sha (glSrc)}...), 19.11.2026,`
        + ` dumped by node ${process.version} (V8, ${process.arch}, ${process.platform}) for tests/MetalWorldTests.cpp. One entry a line:`);
out.push ('# label, kind, count, values. Kinds: d a double (within the test\'s tolerance), f an f32 value (exact), i an integer, a 32-bit'
        + ' FNV-1a hash or a string\'s UTF-8 bytes (exact).');
const E = (label, kind, values) => out.push (`${label} ${kind} ${values.length} ${values.map (v => String (v)).join (' ')}`);

/** FNV-1a over the bytes of a typed array (little-endian, as both sides lay them out). */
const fnv = arr =>
{
    const b = new Uint8Array (arr.buffer, arr.byteOffset, arr.byteLength);
    let h = 0x811c9dc5;
    for (let i = 0; i < b.length; ++i) { h ^= b[i]; h = Math.imul (h, 0x01000193) >>> 0; }
    return h >>> 0;
};
const utf8 = s => new TextEncoder ().encode (s);
const hstr = s => (s === null || s === undefined) ? 0 : fnv (utf8 (s));
const chars = s => [...utf8 (s)];
const sample = (arr, n) => { const o = []; for (let k = 0; k < n; ++k) o.push (arr[Math.floor (k * arr.length / n)]); return o; };
const meshE = (label, m) => E (label, 'i', [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx), m.idx.BYTES_PER_ELEMENT]);
const readBuf = f => { const b = fs.readFileSync (f); return b.buffer.slice (b.byteOffset, b.byteOffset + b.byteLength); };
const pngDims = f => { const b = fs.readFileSync (f); return [b.readUInt32BE (16), b.readUInt32BE (20)]; };
const listing = d => fs.readdirSync (d).sort ();

// THE FINDS' FILES, read by the page's own code.
const dirs = listing (findsDir).filter (d => /3d$/.test (d) && fs.statSync (path.join (findsDir, d)).isDirectory ());
const gd = dirs.find (d => G.roleOf (d) === 'guitars');
E ('parse.roles', 'i', dirs.map (d => hstr (G.roleOf (d))));
for (const d of dirs)
    for (const f of listing (path.join (findsDir, d)).filter (q => q.endsWith ('.mesh')))
    {
        const m = G.readMesh (readBuf (path.join (findsDir, d, f)));
        E (`parse.mesh.${d}/${f}`, 'd', m ? [1, m.nv, m.count, m.zmin, m.zmax] : [0]);
    }
const dragonMesh = G.readMesh (readBuf (dragonBase + '.mesh'));
E ('parse.mesh.dragon', 'd', [1, dragonMesh.nv, dragonMesh.count, dragonMesh.zmin, dragonMesh.zmax]);
for (const d of dirs)
{
    const mesh = listing (path.join (findsDir, d)).find (q => q.endsWith ('.mesh'));
    const txt = path.join (findsDir, d, mesh.replace (/\.mesh$/, '.txt'));
    const c = fs.existsSync (txt) ? G.creditOf (fs.readFileSync (txt, 'utf8')) : null;
    E (`parse.credit.${d}`, 'i', c === null ? [-1] : chars (c));
}
{ const c = G.creditOf (fs.readFileSync (dragonBase + '.txt', 'utf8')); E ('parse.credit.dragon', 'i', c === null ? [-1] : chars (c)); }
const guitarsTxt = fs.readFileSync (path.join (findsDir, gd, 'guitars.txt'), 'utf8');
E ('parse.headstocks', 'd', G.parseHeadstocks (guitarsTxt).flatMap (h => [hstr (h.name), ...h.c, ...h.s]));
E ('parse.headstocks.pair', 'd', G.parseHeadstocks (fs.readFileSync (path.join (findsDir, gd, 'guitars_pair.txt'), 'utf8')).flatMap (h => [hstr (h.name), ...h.c, ...h.s]));
const deal = G.guitars (guitarsTxt, { placeGuitars: items => items.map (() => ({})) });
E ('parse.guitars', 'd', deal.set.flatMap (g => [hstr (g.name), hstr (g.file), g.box ? 1 : 0, ...(g.box ? [...g.box.c, ...g.box.s] : [0, 0, 0, 0, 0, 0]), g.paint ? 1 : 0, ...(g.paint || [0, 0, 0])]));
E ('parse.deal', 'i', deal.chosen.map (g => deal.set.indexOf (g.src)));
E ('parse.deal.items', 'i', deal.items.flatMap (q => [q.grove, q.star ? 1 : 0]));
{
    const ad = pngDims (path.join (findsDir, 'banners', 'atlas.png'));
    E ('parse.png.banners', 'i', ad);
    E ('parse.banners', 'd', G.banners (fs.readFileSync (path.join (findsDir, 'banners', 'banners.txt'), 'utf8'), ad[0], ad[1]).flat ());
}
const pngs = listing (path.join (findsDir, 'letters')).filter (q => q.endsWith ('.png'));
const dims = Object.fromEntries (pngs.map (f => [f, pngDims (path.join (findsDir, 'letters', f))]));
E ('parse.png.letters', 'i', pngs.flatMap (f => dims[f]));
let letterTable = null;
const lettersOut = G.letters (fs.readFileSync (path.join (findsDir, 'letters', 'letters.txt'), 'utf8'), pngs, dims, { buildLetters: t => { letterTable = t; } });
E ('parse.letters.atlas', 'd', [...lettersOut.lettersAtlas, ...(lettersOut.censoredCell || [-1, -1, -1, -1])]);
E ('parse.letters.pieces', 'd', lettersOut.got.filter (g => g.cell).flatMap (g => [hstr (g.name), hstr (g.file || ''), ...g.cell]));
const head = G.dragonHead (readBuf (dragonBase + '.anim'), dragonMesh.nv);
// what the world is furnished with: the strings as their bytes, so the test can furnish its worlds without the files
E ('finds.letters.n', 'i', [letterTable.length]);
letterTable.forEach ((t, k) => { E (`finds.letters.${k}.name`, 'i', chars (t.name)); E (`finds.letters.${k}`, 'd', [...t.cell, ...t.size, t.up, t.once ? 1 : 0, t.colour ? 1 : 0, ...(t.colour || [0, 0, 0, 0])]); });
const slotsDealt = G.guitars (guitarsTxt, { placeGuitars: items => items.map (() => ({})) });
E ('finds.guitars', 'i', slotsDealt.items.flatMap (q => [q.grove, q.star ? 1 : 0]));
slotsDealt.slots.forEach ((s, i) => E (`finds.guitars.name.${i}`, 'i', chars (s.name)));
const landmarkTops = [];
{
    const probeWorld = W.makeWorld ({ textures: false });
    for (const d of dirs.filter (q => q !== gd))
    {
        const mesh = listing (path.join (findsDir, d)).find (q => q.endsWith ('.mesh'));
        const role = G.roleOf (d), m = G.readMesh (readBuf (path.join (findsDir, d, mesh)));
        let top = null;
        G.landmarkTop ({ landmarks: probeWorld.landmarks, landmarkCame: (r, t) => { top = t; } }, role, m);
        if (top !== null) landmarkTops.push ([role, top]);
    }
}
E ('finds.landmarks.n', 'i', [landmarkTops.length]);
landmarkTops.forEach (([role, top], k) => { E (`finds.landmarks.${k}.role`, 'i', chars (role)); E (`finds.landmarks.${k}`, 'd', [top]); });
E ('finds.dragon', 'd', [head.mouth.length, ...head.mouth.flat (), ...head.mid.flat ()]);

const furnish = w =>
{
    w.setDragonHead (head);
    w.buildLetters (letterTable);
    G.guitars (guitarsTxt, w);
    for (const [role, top] of landmarkTops) w.landmarkCame (role, top);
};

// THE HASHES, THE HALF FLOATS, THE TEXTURES, THE LOOP
E ('hash.ih', 'i', [0, 1, 12345, 0x7fffffff, 0x80000000, 4294967295].map (W.ih));
E ('hash.hcell', 'f', [[0, 0, 0], [1, 2, 3], [-5, 7, 101], [255, 255, 707], [-1, -1, 1010], [999, 3, 1]].map (([a, b, c]) => W.hcell (a, b, c)));
E ('hash.hs', 'f', [[1, 1], [7, 92], [120, 16], [0, 0], [241, 77], [15011234567, 51], [-3, 840], [2.5e9, 71], [-7e10, 49]].map (([a, b]) => W.hs (a, b)));
E ('half', 'd', [0, 1e-6, -3e-5, 0.1, 0.333, 1, 1.0009765625, 2047.7, -1599.3, 70000, -1e9, 2.9e-8, 0.49999].map (W.toHalf));
{
    const base = W.makeBase (), detail = W.makeDetail (), noise = W.makeNoise (), cells = W.makeCells ();
    E ('tex.base', 'i', [base.length, fnv (base)]);
    E ('tex.base.sample', 'f', sample (base, 64));
    E ('tex.detail', 'i', [detail.length, fnv (detail), ...sample (detail, 32)]);
    E ('tex.noise', 'i', [noise.length, fnv (noise), ...sample (noise, 32)]);
    E ('tex.cells', 'i', [cells.length, fnv (cells), ...sample (cells, 32)]);
}
E ('loop.len', 'd', [W.LOOP_LEN, W.STUDS_N]);
E ('loop.table', 'd', sample (W.LOOP_S, 64));
E ('loop.R', 'd', [0, 1, 2.5, 4, 6.2].flatMap (th => W.loopR (th)));
E ('loop.theta', 'd', [0, 100, 1234.5, 5000, -20, 5147.1, 9999].map (W.thetaAt));
E ('loop.point', 'd', [0, 300, 1777.7, 4000].flatMap (s => [...W.roadPointAt (s, 0), ...W.roadPointAt (s, -12.5), ...W.tangentAt (W.thetaAt (s)), W.headingAt (s)]));
E ('loop.front', 'd', [[24000, 24000, 600, 30], [25100, 24100, 2000, -20], [24520, 25500, 3500, 0]].flatMap (([x, z, a, t]) => W.frontAt (x, z, a, t)));
E ('camera', 'd', [[0, 0], [1.3, -0.087], [-2.9, 0.04], [7.5, -0.12]].flatMap (([h, p]) => { const b = G.cameraBasis (h, p); return [...b.r, ...b.u, ...b.f]; }));
E ('loop.modL', 'd', [-1, 0, 5147.139620441408, 12000.5, -77777].map (W.modL));

// THE WORLD: its places, furnished
const pitchOffset = -3 * Math.PI / 180;     // page.js's
const fresh = ({ gear = 1, parked = false } = {}) =>
{
    const w = W.makeWorld ();
    furnish (w);
    w.setGear (gear); w.setParked (parked); w.settle ();
    w.state.pitchOffset = W.clamp (pitchOffset, -0.5, 0.5);
    return w;
};
const run = (w, seconds) => { const n = Math.round (seconds * 60); for (let i = 0; i < n; ++i) w.step (1 / 60); };
const MODES = ['roam', 'swoop', 'toPerch', 'perch', 'clang', 'leave'], KINDS = ['front', 'circle', 'park', 'prep'], GESTS = ['look', 'wings', 'shuffle', 'stretch'];
const st = (label, w) =>
{
    const s = w.state;
    E (`${label}.state`, 'd', [s.sr, s.lap, s.lat, s.latV, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.aurora, s.speed, s.storm, s.calm, s.park,
                               s.hitT, s.hitS, s.hits, s.flash, s.findFade, s.beat, s.stageHitT, s.gear, s.parked ? 1 : 0]);
};
const dr = (label, w) => E (`${label}.dragons`, 'd', w.dragons.flatMap (u => [MODES.indexOf (u.mode), u.x, u.y, u.z, u.vx, u.vy, u.vz, u.mt, u.as, u.lat, u.up, u.front ? 1 : 0,
    KINDS.indexOf (u.kind), u.follow, u.loopR, u.loopW, u.anchorT, u.beat, u.rate, ...u.U, ...u.F, ...u.L, ...u.mouth, ...u.headDir, u.flameT, u.hicT, u.nextHic, u.ringT,
    u.ring ? 1 : 0, ...(u.ring ? [...u.ring.at, ...u.ring.dir] : [0, 0, 0, 0, 0, 0]), u.perchT, u.tumble, u.shake, u.nextGest, u.g.hy, u.g.hp, u.g.wing, u.g.pitch, u.g.yaw, u.g.side,
    ...(u.gest ? [GESTS.indexOf (u.gest.kind), u.gest.t, u.gest.dur, u.gest.a, u.gest.b] : [-1, 0, 0, 0, 0]), ...(u.perch ? [1, u.perch.x, u.perch.y, u.perch.z, u.perch.yaw] : [0, 0, 0, 0, 0])]));
const orbs = (label, w) => E (`${label}.orbs`, 'd', w.orbs.flatMap (o => [o.x, o.y, o.z, o.age, o.life, o.n, o.as, o.lat, o.up, o.ph]));
const lightning = (label, w) =>
{
    E (`${label}.bolts`, 'd', w.bolts.flatMap (b => [b.t, b.k, b.n, ...b.at, b.lines.length, ...b.lines.flatMap (L => [L.w, L.pts.length, ...L.pts.flat ()])]));
    E (`${label}.sparks`, 'd', w.sparks.flatMap (q => [q.t, ...q.at, q.k]));
    const ts = w.teslaState ();
    E (`${label}.tesla`, 'd', [ts.arc.i, ts.arc.t, ts.arc.dur, ts.chain, ...w.shakes]);
};
const fires = (label, w) => { const f = G.fires (w, w.state); E (`${label}.fires`, 'i', [f.n]); E (`${label}.fires.em`, 'f', [...f.em]); E (`${label}.fires.pl`, 'f', [...f.pl]); };
const ribbons = (label, w) => { const r = G.ribbons (w, w.state); E (`${label}.rib`, 'i', [r.n, fnv (r.rb)]); E (`${label}.rib.sample`, 'f', sample (r.rb, 96)); };
{
    const w = fresh ();
    const pts = [[24520, 24680], [23900, 24100], [25280.5, 24680.25], [24000, 25500], [26000, 23000], [10, 47990], [24300.7, 23950.1], [24130, 24600], [25000, 25300]];
    for (const s of [0, 230, 617, 1290, 2316, 3047, 3800, 4790]) for (const d of [0, 6, -30, 120]) pts.push (W.roadPointAt (s, d));
    E ('height.floor', 'd', pts.map (([x, z]) => w.floorHeight (x, z)));
    E ('height.terrain', 'd', pts.map (([x, z]) => w.terrainH (x, z)));
    E ('height.probe', 'd', pts.flatMap (([x, z]) => { const p = w.probe (x, z); return [p.hot, p.mtn]; }));
    E ('height.road', 'd', pts.flatMap (([x, z]) => w.roadTap (x, z)));
    const t = w.textures ();
    E ('tex.road', 'i', [t.roadMap.length, fnv (t.roadMap)]);
    E ('tex.road.sample', 'f', sample (t.roadMap, 64));
    E ('crags', 'd', w.crags.flatMap (c => [...c.a, ...c.b, c.r, c.h, hstr (c.name)]));
    E ('props', 'd', w.props.flatMap (q => [hstr (q.mesh), q.x, q.y, q.z, q.yaw, q.s, q.p, q.idx, hstr (q.name)]));
    E ('banners', 'd', w.banners.flatMap (b => [b.x, b.y, b.z, b.yaw, b.w, b.h, b.seed, b.cell]));
    E ('targets', 'd', w.targets.flatMap (q => [q.x, q.y, q.z, q.w, q.hit ? 1 : 0, q.tesla !== undefined ? q.tesla : -1]));
    E ('perches', 'd', w.perches.flatMap (q => [q.x, q.y, q.z, q.yaw, hstr (q.name), q.tower !== undefined ? q.tower : -1]));
    E ('spires', 'd', w.spires.flatMap (q => [q.x, q.y, q.z, q.prop]));
    E ('gate', 'd', [w.gate.at, w.gate.x, w.gate.z, w.gate.y, w.gate.prop.idx]);
    E ('citadel', 'd', [w.citadel.at, w.citadel.x, w.citadel.z, w.citadel.y, ...w.citadel.towers.flatMap (q => [q.x, q.y, q.z, q.h, q.tip, q.prop.idx])]);
    E ('warrior', 'd', [w.warrior.at, w.warrior.x, w.warrior.y, w.warrior.z, w.warrior.yaw]);
    E ('landmarks', 'd', w.landmarks.flatMap (q => [hstr (q.role), q.at, q.x, q.y, q.z, q.yaw, q.came ? 1 : 0]));
    E ('tesla', 'd', w.tesla.flatMap (q => [q.x, q.y, q.z, q.top, q.prop.idx]));
    E ('amps', 'd', [w.amps.at, w.amps.x, w.amps.z, w.amps.y, w.amps.fx, w.amps.fy, w.amps.fz]);
    E ('henge', 'd', [w.henge.at, w.henge.x, w.henge.z, w.henge.y]);
    E ('anvil', 'd', [w.anvil.at, w.anvil.x, w.anvil.z, w.anvil.y]);
    E ('cat', 'd', [w.cat.at, w.cat.x, w.cat.z, w.cat.y, w.cat.nx, w.cat.nz]);
    E ('stage', 'd', [w.stage.at, w.stage.x, w.stage.z, w.stage.y, w.stage.nx, w.stage.nz, w.stage.yaw]);
    E ('pyro', 'd', w.pyro.flatMap (q => [q.x, q.y, q.z, q.at, q.k, q.i]));
    E ('fountains', 'd', w.fountains.flatMap (q => [q.x, q.y, q.z, q.at, q.next, q.t, q.n, q.i]));
    E ('tunnel', 'd', [w.tunnel.from, w.tunnel.to, w.tunnel.hw]);
    E ('taken', 'd', w._taken ());
    E ('groves', 'd', w.groves ().flatMap (g => [g.g, g.at, g.both ? 1 : 0, g.starSide, g.members.length]));
    E ('guitars', 'd', w.guitars ().flatMap (q => [q.i, q.at, q.x, q.y, q.z, q.yaw, q.k, ...q.lean, q.grove, q.star ? 1 : 0, q.side, q.off, q.along, hstr (q.name)]));
    E ('letters', 'd', w.letters ().flatMap (q => [hstr (q.name), q.at, q.x, q.z, q.y, q.yaw, q.nx, q.nz, q.hw, q.hh, ...q.cell, q.colour ? 1 : 0, ...(q.colour || [0, 0, 0, 0]), q.seed, q.big ? 1 : 0]));
    const GROUPS = ['finds', 'big', 'road', 'signs', 'sky'];
    const pl = w.places ();
    E ('places', 'd', pl.flatMap (q => [GROUPS.indexOf (q.group), hstr (q.name), q.at, q.seen, q.lap !== undefined ? q.lap : -1, q.kind !== undefined ? q.kind : -1]));
    meshE ('mesh.hill', w.hillMesh ());
    const PM = W.propMeshes ();
    for (const name of Object.keys (PM).sort ()) meshE (`mesh.${name}`, PM[name]);
    st ('start', w); dr ('start', w); orbs ('start', w);
    E ('start.nextFind', 'd', [w.nextFindAt ()]);
    w.step (1 / 60); st ('start1', w);
    for (let lap = 0; lap < 5; ++lap)
    {
        w.setAt (w.state.sr, lap);
        E (`finds.lap${lap}`, 'd', w.findsNow ().flatMap (f => [f.kind, f.x, f.z, f.y, f.yaw, f.at, f.d]));
    }
    // the jump list: where a jump puts the drive, and the drive a little after
    for (const k of [0, 7, 15, 22, pl.length - 1])
    {
        const j = G.jumpTo (w, pl[k]);
        E (`jump.${k}`, 'd', [j.s, j.lap]); st (`jump.${k}`, w);
        run (w, 2); st (`jump.${k}.2`, w);
    }
}
// THE DRIVE in every gear, PARK in and out, the storm
for (let g = 1; g <= 4; ++g)
{
    const w = fresh ({ gear: g });
    run (w, 3); st (`g${g}.3`, w);
    run (w, 7); st (`g${g}.10`, w); lightning (`g${g}.10`, w);
}
{
    const w = fresh ({ gear: 2 });
    run (w, 2); w.setParked (true);
    run (w, 3); st ('park.in3', w);
    run (w, 9); st ('park.in12', w); dr ('park.in12', w);
    w.setParked (false);
    run (w, 2); st ('park.out2', w);
    run (w, 6); st ('park.out8', w);
}
{
    const w = fresh ({ gear: 1 });
    run (w, 2); w.setGear (4);
    run (w, 1); st ('storm.1', w);
    const flash = [];
    for (let i = 0; i < 360; ++i) { w.step (1 / 60); flash.push (w.state.flash); }
    E ('storm.flash', 'd', flash);
    st ('storm.7', w); lightning ('storm.7', w); fires ('storm.7', w); ribbons ('storm.7', w);
    w.setGear (3);
    run (w, 3); st ('storm.off3', w);
}
// AN ATTACK on the pyro stretch: the flash over the following second, the fires, the ribbons
{
    const w = fresh ({ gear: 1 });
    w.setAt (200);
    run (w, 1); w.hit (0.8);
    const flash = [];
    for (let i = 0; i < 60; ++i)
    {
        w.step (1 / 60); flash.push (w.state.flash);
        if (i === 11) { st ('hit.0.2', w); lightning ('hit.0.2', w); dr ('hit.0.2', w); fires ('hit.0.2', w); ribbons ('hit.0.2', w); }
    }
    E ('hit.flash', 'd', flash);
    st ('hit.1', w); fires ('hit.1', w);
}
{   // ...at the festival stage
    const w = fresh ({ gear: 2 });
    w.setAt (0.77 * W.LOOP_LEN - 600);
    run (w, 2); w.hit (1); run (w, 0.5);
    st ('stage', w); fires ('stage', w); ribbons ('stage', w);
}
{   // ...by the Tesla towers in the storm (s=330&gear=4): the discharge down the chain
    const w = fresh ({ gear: 4 });
    w.setAt (330);
    run (w, 3); w.hit (1); run (w, 0.1);
    st ('tesla.0.1', w); lightning ('tesla.0.1', w); ribbons ('tesla.0.1', w);
    run (w, 5);
    st ('tesla.5', w); lightning ('tesla.5', w); ribbons ('tesla.5', w); fires ('tesla.5', w);
}
// THE DRAGONS: roaming, perched on the citadel and taking off; a swoop with its smoke ring; a clang on lap 1; PARK
{
    const w = fresh ({ gear: 2 });
    for (let k = 1; k <= 26; ++k) { run (w, 5); dr (`dragons.${5 * k}`, w); }
    st ('dragons.130', w); orbs ('dragons.130', w);
}
{
    const w = fresh ({ gear: 3 });
    w.setAt (0.70 * W.LOOP_LEN - 900);
    for (let k = 1; k <= 40; ++k) { run (w, 2); dr (`swoop.${2 * k}`, w); fires (`swoop.${2 * k}`, w); }
}
{
    const w = fresh ({ gear: 2 });
    w.setAt (W.LOOP_LEN - 280, 1);
    for (let k = 1; k <= 16; ++k) { run (w, 5); dr (`lap2.${5 * k}`, w); }
    lightning ('lap2.80', w);
}
{   // lap 1 by the citadel: one flies into a spire, tumbles, the spire shakes
    const w = fresh ({ gear: 2 });
    w.setAt (w.citadel.at - 500, 1);
    for (let k = 1; k <= 30; ++k) { run (w, 1); dr (`clang.${k}`, w); }
    lightning ('clang.30', w);
}
{
    const w = fresh ({ gear: 1, parked: true });
    for (let k = 1; k <= 8; ++k) { run (w, 5); dr (`parkd.${5 * k}`, w); }
    fires ('parkd.40', w);
}
// THE BALL LIGHTNING
{
    const w = fresh ({ gear: 3 });
    for (let k = 1; k <= 6; ++k) { run (w, 10); orbs (`orbs.${10 * k}`, w); lightning (`orbs.${10 * k}`, w); }
}
// A BURNING TEXT in view
{
    const w = fresh ({ gear: 1 });
    w.setAt (w.letters ()[1].at - 150);
    run (w, 1); fires ('letter', w);
}
// THE CABIN'S PLATE: the box round its clear pixels
{
    const wd = 64, ht = 40, rgba = new Uint8Array (wd * ht * 4).fill (255);
    for (let y = 9; y < 27; ++y) for (let x = 5; x < 50; ++x) rgba[(y * wd + x) * 4 + 3] = (x * 7 + y) % 255;
    E ('glass', 'd', [...G.glassBounds (rgba, wd, ht, 3), ...G.glassBounds (rgba, wd, ht, 0), G.glassBounds (new Uint8Array (16).fill (255), 2, 2, 1) === null ? 1 : 0]);
}

fs.rmSync (tmp, { recursive: true, force: true });
process.stdout.write (out.join ('\n') + '\n');
