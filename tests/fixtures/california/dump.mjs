// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The California world's prototype dumped for tests/CaliforniaWorldTests.cpp: `node dump.mjs <prototype dir> > california-world.txt`,
// the directory holding california-gl.js, california-shaders.js, finds/ and cabin-van/. The prototype exports only a
// few of its makers, so a copy of it is made in a temporary folder with the rest exported too; the prototype itself is
// not touched. The files the world reads are parsed here as the prototype's view parses them (its code, line for line),
// from the prototype's folder; the test reads its copies of them (tests/fixtures/california/files), and the entries
// file.* (size and hash) say the copies are the prototype's. One entry a line: label, kind, count, values. Kinds: d a
// double (within the test's tolerance), f an f32 value (exact), i an integer or a 32-bit FNV-1a hash (exact).

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const gl = fs.readFileSync (path.join (dir, 'california-gl.js'), 'utf8');
const tmp = fs.mkdtempSync (path.join (os.tmpdir (), 'california-dump-'));
fs.copyFileSync (path.join (dir, 'california-shaders.js'), path.join (tmp, 'california-shaders.js'));
fs.writeFileSync (path.join (tmp, 'california-gl.js'), gl
    + '\nexport { ih, hcell, hs, toHalf, makeHeight, makeDetail, makeNoise, LOOP_S, loopPoint, icosphere, oakMesh, poleMesh, pumpTowerMesh,'
    + ' pumpFanMesh, floraMeshes, butterflyMesh, tiltOf, SUN, WIND, POLES_N, POLE_DS, DASHES_N, SAUCER, SUB };\n');
const P = await import (pathToFileURL (path.join (tmp, 'california-gl.js')).href);

const out = [];
const sha = crypto.createHash ('sha256').update (gl).digest ('hex');
out.push (`# The California world's prototype, california-gl.js (sha256 ${sha.slice (0, 16)}..., 20.11.2026), dumped by node ${process.version}`
        + ` (V8, ${process.arch}, ${process.platform}) for tests/CaliforniaWorldTests.cpp. One entry a line: label, kind, count,`);
out.push ('# values. Kinds: d a double (within the test\'s tolerance), f an f32 value (exact), i an integer or a 32-bit FNV-1a hash (exact).');
const E = (label, kind, values) => out.push (`${label} ${kind} ${values.length} ${values.map (v => String (v)).join (' ')}`);

/** FNV-1a over the bytes of a typed array (little-endian, as both sides lay them out), or of a string's UTF-8. */
const fnvBytes = b => { let h = 0x811c9dc5; for (let i = 0; i < b.length; ++i) { h ^= b[i]; h = Math.imul (h, 0x01000193) >>> 0; } return h >>> 0; };
const fnv = arr => fnvBytes (new Uint8Array (arr.buffer, arr.byteOffset, arr.byteLength));
const fnvS = s => fnvBytes (Buffer.from (String (s ?? ''), 'utf8'));
const sample = (arr, n) => { const o = []; for (let k = 0; k < n; ++k) o.push (arr[Math.floor (k * arr.length / n)]); return o; };
const L = P.LOOP_LEN;

// The hashes, the half floats.
E ('hash.ih', 'i', [0, 1, 12345, 0x7fffffff, 0x80000000, 4294967295].map (P.ih));
E ('hash.hcell', 'f', [[0, 0, 0], [1, 2, 3], [-5, 7, 101], [255, 255, 707], [-1, -1, 1010], [3299, 17, 51]].map (([a, b, c]) => P.hcell (a, b, c)));
E ('hash.hs', 'f', [[1, 1], [7, 92], [120, 16], [0, 0], [241, 77], [45, 801]].map (([a, b]) => P.hs (a, b)));
E ('half', 'd', [0, 1e-9, -3e-6, 6.1e-5, 6.2e-5, 0.1, 0.33333, -0.7, 1, 1.0004, 2047.6, 70000, -1e6, 2.9802322387695312e-08, -0.49999].map (P.toHalf));

// The textures.
const height = P.makeHeight (), detail = P.makeDetail (), noise = P.makeNoise ();
E ('tex.height', 'i', [height.length, fnv (height)]);
E ('tex.height.sample', 'f', sample (height, 64));
E ('tex.detail', 'i', [detail.length, fnv (detail), ...sample (detail, 32)]);
E ('tex.noise', 'i', [noise.length, fnv (noise), ...sample (noise, 32)]);

// The loop.
E ('loop.len', 'd', [L, P.POLE_DS]);
E ('loop.n', 'i', [P.LOOP_S.length, P.POLES_N, P.DASHES_N]);
E ('loop.table', 'd', [0, 1, 2, 100, 1000, 2048, 4096, 6000, 8191, 8192].map (i => P.LOOP_S[i]));
const ths = [0, 0.5, 1, 2, 3.14, 4, 5.5, 6.2, -1, 10];
E ('loop.R', 'd', ths.flatMap (th => P.loopR (th)));
E ('loop.point', 'd', ths.flatMap (th => P.loopPoint (th)));
E ('loop.tangent', 'd', ths.flatMap (th => [...P.road.tangentAt (th), ...P.road.rightAt (th)]));
E ('loop.s', 'd', ths.map (th => P.road.sOfTheta (th)));
const ss = [0, 1, 10.5, 1000, 2000, 2560, 4300, 5000, 5146, -20, 6000, 12345.6];
E ('loop.theta', 'd', ss.map (P.thetaAt));
E ('loop.road', 'd', ss.flatMap ((s, k) => P.road.roadPointAt (s, [0, 1.65, -8.6, 4.3, 200, -343][k % 6])));
E ('sun', 'd', [...P.SUN, ...P.WIND]);
E ('camera', 'd', [[0.3, -0.05], [-2.1, 0.2]].flatMap (([h, p]) => { const b = P.cameraBasis (h, p); return [...b.r, ...b.u, ...b.f]; }));
E ('tilt', 'f', [[[0, 0], 1], [[0.1, -0.2], 1], [[0.3, 0.05], 4], [[0, 0], 10]].flatMap (([l, k]) => [...P.tiltOf (l, k)]));

// The meshes.
{
    const ico = P.icosphere (2);
    E ('mesh.ico2', 'i', [ico.V.length, fnv (new Float64Array (ico.V.flat ())), ico.F.length, fnv (new Uint32Array (ico.F.flat ()))]);
    const mesh = m => [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx)];
    [0, 1, 2].forEach (d => E (`mesh.oak${d}`, 'i', mesh (P.oakMesh (d))));
    E ('mesh.pole', 'i', mesh (P.poleMesh ()));
    E ('mesh.pumpTower', 'i', mesh (P.pumpTowerMesh ()));
    E ('mesh.pumpFan', 'i', mesh (P.pumpFanMesh ()));
    P.floraMeshes ().forEach ((m, k) => E (`mesh.flora${k}`, 'i', mesh (m)));
    E ('mesh.butterfly', 'i', mesh (P.butterflyMesh ()));
}
{   // the windshield's box from a dashboard's alpha (a made-up one: a pane, a pinhole; then none at all)
    const w = 40, h = 30, px = new Uint8Array (w * h * 4).fill (255);
    for (let y = 3; y < 12; ++y) for (let x = 5; x < 20; ++x) px[(y * w + x) * 4 + 3] = 0;
    px[(25 * w + 33) * 4 + 3] = 254;
    E ('glassBounds', 'd', [...P.glassBounds (px, w, h, 2), ...(P.glassBounds (new Uint8Array (16).fill (255), 2, 2, 2) || [-1])]);
}

// The world: the ground and the sky's ridges.
const pitchOffset = -3.0 * Math.PI / 180;        // cabin.html's
const fresh = ({ gear = 1, parked = false, at = null, lap = null } = {}) =>
{
    const w = P.makeWorld ();
    w.setGear (gear); w.setParked (parked); w.settle ();
    w.state.pitchOffset = pitchOffset;
    if (at !== null) w.setAt (at, lap);
    return w;
};
const run = (w, seconds) => { const n = Math.round (seconds * 60); for (let i = 0; i < n; ++i) w.step (1 / 60); };
const st = (label, w) =>
{
    const s = w.state;
    E (`${label}.state`, 'd', [s.sr, s.lap, s.lat, s.latV, s.latA, s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.aFwd, s.aLeft,
                               s.dt, s.pitchOffset, s.park, s.parkT, s.hitT, s.hitS, s.hits, s.startleT, s.startleS,
                               s.pend.th, s.pend.thV, s.pend.ph, s.pend.phV, ...s.pollenFlow, ...s.fluffFlow, s.findFade, w.speedOf ()]);
};
const more = (label, w) =>
{
    const s = w.state, f = s.flock;
    E (`${label}.bursts`, 'd', s.bursts.flatMap (b => [b.t, b.x, b.z, b.fx, b.fz, b.level, b.seed]));
    E (`${label}.flock`, 'd', [f.on ? 1 : 0, f.n, f.x, f.y, f.z, f.fx, f.fz, f.t, f.vis]);
    E (`${label}.glass`, 'd', s.glass.flatMap (g => [g.st, g.t, g.x, g.y, g.k, g.open, g.a, g.rot, g.fx, g.fy, g.fk]));
};
const bfl = (label, w) => E (`${label}.bfly`, 'd', w.butterflies.flatMap (b => [...b.rel, b.yaw, b.flap, b.pitch, b.vis, b.dist, b.phase]));
const MODE = { roam: 0, swoop: 1, pair: 2 }, KIND = { front: 0, side: 1, park: 2, prep: 3 };
const sau = (label, w) => E (`${label}.saucers`, 'd', [w.saucerSeen (), ...w.saucers.flatMap (u => [u.x, u.y, u.z, u.vx, u.vy, u.vz, u.up, u.front ? 1 : 0, u.ph,
    u.loopR, u.loopW, MODE[u.mode], u.mt, ...u.lean, u.yaw, u.beam, u.beamOn ? 1 : 0, u.beamT, u.anchorT, u.as, u.lat, KIND[u.kind], u.follow])]);
const sub = (label, w) => { const b = w.sub; E (`${label}.sub`, 'd', [b.on ? 1 : 0, b.t, b.as, b.from, b.up, b.yb, b.x, b.y, b.z, b.yaw, ...b.lean, b.fade, b.pass, b.k, b.lift ?? 1]); };
const all = (label, w) => { st (label, w); more (label, w); bfl (label, w); sau (label, w); sub (label, w); };
const box = b => [b.x, b.y, b.z, b.yaw, b.hx, b.hy, b.hz, b.part, ...b.rect, b.vis, b.flutter];
const finds = w => w.findsNow ().flatMap (f => f.kind === 0 ? [f.kind, f.x, f.z, f.y, ...f.u, ...f.v, f.R, f.k] : [f.kind, f.x, f.z, f.y, f.nx, f.nz]);

// The files the world reads, as the view parses them.
const FILES = ['finds/dish/sprites.txt', 'finds/tower/sprites.txt', 'finds/signs/signs.txt', 'finds/chalk/chalk.txt', 'finds/flags/flags.txt',
               'cabin-van/pendant_a.txt', 'cabin-van/pendant_b.txt', 'cabin-van/pendant_c.txt',
               'finds/van3d/van.txt', 'finds/guitars3d/guitars.txt', 'finds/barn3d/barn.txt', 'finds/barn3d/barn.mesh'];
const bytes = rel => fs.readFileSync (path.join (dir, rel));
const text = rel => bytes (rel).toString ('utf8');
for (const rel of FILES) { const b = bytes (rel); E (`file.${rel}`, 'i', [b.length, fnvBytes (b)]); }
const pngSize = rel => { const b = bytes (rel); return [b.readUInt32BE (16), b.readUInt32BE (20)]; };
const signAtlas = pngSize ('finds/signs/atlas.png'), chalkAtlas = pngSize ('finds/chalk/atlas.png'), flagAtlas = pngSize ('finds/flags/atlas.png');
E ('atlas.sizes', 'i', [...signAtlas, ...chalkAtlas, ...flagAtlas]);
const spriteTable = txt =>
{
    const t = { cell: [512, 288], scale: 0, grid: [8, 4], anchor: [256, 144], count: 0 };
    for (const line of txt.split ('\n'))
    {
        const w = line.trim ().split (/\s+/);
        if (line.startsWith ('#') && line.includes ('px per metre'))
        {
            const m = line.match (/cells (\d+) x (\d+) px, ([\d.]+) px per metre/);
            if (m) { t.cell = [+m[1], +m[2]]; t.scale = +m[3]; }
            const g = line.match (/atlas (\d+) x (\d+)/);
            if (g) t.grid = [+g[1], +g[2]];
        }
        else if (w[0] === 'anchor') t.anchor = [+w[1], +w[2]];
        else if (w[0] === 'cell') ++t.count;
    }
    return t;
};
for (const what of ['dish', 'tower']) { const t = spriteTable (text (`finds/${what}/sprites.txt`)); E (`table.${what}`, 'd', [...t.cell, t.scale, ...t.grid, ...t.anchor, t.count]); }
E ('table.none', 'd', (t => [...t.cell, t.scale, ...t.grid, ...t.anchor, t.count]) (spriteTable ('')));
const pendantBox = txt =>
{
    const b = { box: null, pivot: null };
    for (const line of txt.split ('\n'))
    {
        const w = line.trim ().split (/\s+/);
        if (w[0] === 'box') b.box = [+w[1], +w[2], +w[3], +w[4]];
        else if (w[0] === 'pivot') b.pivot = [+w[1], +w[2]];
    }
    return b.box && b.pivot ? b : null;
};
for (const c of ['a', 'b', 'c']) { const b = pendantBox (text (`cabin-van/pendant_${c}.txt`)); E (`pendant.${c}`, 'd', [...b.box, ...b.pivot]); }
E ('pendant.none', 'i', [pendantBox ('box 1 2 3 4\n') ? 1 : 0]);
const signTable = (() =>
{
    const t = [];
    for (const line of text ('finds/signs/signs.txt').split ('\n'))
    {
        const w = line.trim ().split (/\s+/);
        if (w[0] !== 'sign' || w.length < 11) continue;
        t.push ({ name: w[1], shape: w[2], cell: [+w[3], +w[4], +w[5], +w[6]], size: [+w[7], +w[8]], bottom: +w[9], post: w[10] });
    }
    return t;
}) ();
E ('signs.table', 'd', signTable.flatMap (sg => [...sg.cell, ...sg.size, sg.bottom]));
E ('signs.names', 'i', signTable.flatMap (sg => [fnvS (sg.name), fnvS (sg.shape), fnvS (sg.post)]));
const flagCells = (() =>
{
    const [w, h] = flagAtlas, cells = [];
    for (const line of text ('finds/flags/flags.txt').split ('\n'))
    {
        const nums = line.trim ().split (/\s+/).map (Number).filter (v => ! Number.isNaN (v)).slice (-4);
        if (! line.trim ().startsWith ('#') && nums.length === 4) cells.push ([nums[0] / w, nums[1] / h, (nums[0] + nums[2]) / w, (nums[1] + nums[3]) / h]);
    }
    return cells;
}) ();
E ('flags.cells', 'd', flagCells.flat ());
const chalkTable = (() =>
{
    const [w, h] = chalkAtlas, t = [];
    for (const line of text ('finds/chalk/chalk.txt').split ('\n'))
    {
        if (line.trim ().startsWith ('#')) continue;
        const nums = line.trim ().split (/\s+/).map (Number).filter (v => ! Number.isNaN (v));
        if (nums.length >= 6) t.push ({ name: line.trim ().split (/\s+/)[1], optional: /\boptional\b/.test (line), cell: [nums[0] / w, nums[1] / h, (nums[0] + nums[2]) / w, (nums[1] + nums[3]) / h], size: [nums[4], nums[5]] });
    }
    return t;
}) ();
E ('chalk.table', 'd', chalkTable.flatMap (c => [c.optional ? 1 : 0, ...c.cell, ...c.size]));
E ('chalk.names', 'i', chalkTable.map (c => fnvS (c.name)));
const meshInfo = txt =>
{
    const texFiles = [], censor = [];
    for (const line of txt.split ('\n'))
    {
        const w = line.trim ().split (/\s+/);
        if (w[0] === 'texture') texFiles[+w[1] - 1] = w[2];
        if (w[0] === 'headstock' && w.length >= 8) censor.push ({ name: w[1], c: w.slice (2, 5).map (Number), s: w.slice (5, 8).map (Number) });
    }
    return { texFiles, censor };
};
for (const rel of ['finds/van3d/van.txt', 'finds/guitars3d/guitars.txt', 'finds/barn3d/barn.txt'])
{
    const m = meshInfo (text (rel)), key = path.basename (rel, '.txt');
    E (`meshinfo.${key}.textures`, 'i', [m.texFiles.length, ...Array.from (m.texFiles, f => fnvS (f ?? ''))]);
    E (`meshinfo.${key}.censor`, 'd', [m.censor.length, ...m.censor.flatMap (c => [fnvS (c.name), ...c.c, ...c.s])]);
}
const cbmesh = (u8, parts) =>
{
    const buf = u8.buffer.slice (u8.byteOffset, u8.byteOffset + u8.byteLength);
    if (! buf || buf.byteLength < 16) return null;
    const dv = new DataView (buf), nv = dv.getUint32 (8, true), ni = dv.getUint32 (12, true);
    if (new TextDecoder ().decode (new Uint8Array (buf, 0, 7)) !== 'CBMESH1' || buf.byteLength < 16 + nv * 40 + ni * 4) return null;
    const m = { nv, ni, verts: new Uint8Array (buf, 16, nv * 40) };
    let idx = new Uint32Array (buf, 16 + nv * 40, ni);
    if (parts)
    {
        const px = i => dv.getFloat32 (16 + i * 40, true), bins = parts.map (() => []);
        for (let t = 0; t < ni; t += 3)
        {
            const cx = (px (idx[t]) + px (idx[t + 1]) + px (idx[t + 2])) / 3, b = parts.findIndex (([a, z]) => cx >= a && cx <= z);
            if (b >= 0) bins[b].push (idx[t], idx[t + 1], idx[t + 2]);
        }
        const o = new Uint32Array (bins.reduce ((n, b) => n + b.length, 0));
        m.parts = []; let first = 0;
        bins.forEach ((b, k) => { o.set (b, first); m.parts.push ([first, b.length, (parts[k][0] + parts[k][1]) / 2]); first += b.length; });
        idx = o;
    }
    m.idx = idx;
    return m;
};
{
    const raw = bytes ('finds/barn3d/barn.mesh');
    const m = cbmesh (raw, null), mp = cbmesh (raw, [[-8.0, 0.0], [0.0, 8.0], [20.0, 30.0]]);
    E ('cbmesh.barn', 'i', [m.nv, m.ni, fnv (m.verts), m.idx.length, fnv (m.idx)]);
    E ('cbmesh.barn.parts', 'i', [mp.idx.length, fnv (mp.idx), ...mp.parts.flatMap (p => [p[0], p[1]])]);
    E ('cbmesh.barn.cx', 'd', mp.parts.map (p => p[2]));
    const bad = Uint8Array.from (raw.subarray (0, 4096)); bad[0] = 0x58;
    E ('cbmesh.refused', 'i', [cbmesh (bad, null) ? 1 : 0, cbmesh (raw.subarray (0, 4096), null) ? 1 : 0, cbmesh (raw.subarray (0, 12), null) ? 1 : 0]);
}

// The world as made: the ground, the things on the loop, the oaks and the poles, the shadow map.
{
    const w = fresh ();
    const pts = [[46960, 46200], [45000, 46000], [46200, 46200], [44700, 47700], [47500.125, 45555.9], [0, 0], [92399.5, 17.25], [-3.5, 46812.75], [100000, -5000]];
    E ('floor.h', 'd', pts.map (([x, z]) => w.floorHeight (x, z)));
    E ('floor.sun', 'd', pts.map (([x, z]) => w.sunAt (x, z)));
    E ('ridge', 'd', [[0, 0, 2], [0.13, 1, 40], [0.5, 2, -10], [0.77, 0, 20], [1.3, 1, 0], [-0.2, 2, 60]].map (([a, k, h]) => w.ridgeTan (a, k, h)));
    const T = w.textures ();
    E ('tex.road', 'i', [T.roadMap.length, fnv (T.roadMap)]);
    E ('tex.road.sample', 'f', sample (T.roadMap, 64));
    E ('tex.land', 'i', [T.landMap.length, fnv (T.landMap)]);
    E ('tex.land.sample', 'f', sample (T.landMap, 64));
    E ('tex.height.world', 'i', [fnv (T.height), fnv (T.detail), fnv (T.noise)]);
    st ('start', w); more ('start', w); sau ('start', w); sub ('start', w);
    E ('start.bfly.init', 'd', w.butterflies.flatMap (b => [b.ax, b.az, b.alt, b.scale, b.A, b.w1, b.w2, b.p1, b.p2, b.p3, b.p4, b.rate, b.glideW, b.phase]));
    E ('places.sprites', 'd', w.spritePlaces.flatMap (p => [p.at, p.x, p.z, p.y, p.nx, p.nz, p.vis, p.scale, p.pull ?? 0, p.seen, p.hill ? 1 : 0, p.paint ? 1 : 0,
                                                              p.noBox ? 1 : 0, p.noJump ? 1 : 0, ...(p.part || [0, 0])]));
    E ('places.sprites.names', 'i', w.spritePlaces.flatMap (p => [fnvS (p.name), fnvS (p.label), fnvS (p.mesh), fnvS (p.group)]));
    E ('places.dishes', 'd', w.dishes.flatMap (d => [d.x, d.z, d.y, d.nx, d.nz]));
    E ('places.pumps', 'd', w.pumps.flatMap (p => [p.x, p.z, p.y, p.vis, p.phase]));
    E ('places.flags', 'd', w.flags.flatMap (box));
    E ('places.boxes', 'd', [w.signBoxes ().length, ...w.signBoxes ().flatMap (box)]);
    E ('places.boxes.ph', 'i', w.signBoxes ().map (b => fnvS (b.ph)));
    E ('finds.lap0', 'd', finds (w));
    E ('finds.next', 'd', [w.nextFindAt ()]);
    const oaks = w.listOaks ();
    E ('oaks.count', 'i', [oaks.length]);
    E ('oaks.first', 'd', oaks.slice (0, 80).flatMap (o => [o.x, o.z, o.y, o.R, o.s, o.yaw, o.tint, o.vis]));
    E ('oaks.sums', 'd', ['x', 'z', 'y', 'R', 's', 'yaw', 'tint', 'vis'].map (k => oaks.reduce ((a, o) => a + o[k], 0)));
    E ('poles', 'd', w.listPoles ().flatMap (p => [p.k, p.x, p.z, p.y, p.h, p.yaw, p.lean, ...p.side]));
    E ('poles.far', 'd', [-3, 0, 122, 500].flatMap (k => { const p = w.poleAt (k); return [p.k, p.x, p.z, p.y, p.h]; }));
    const c = w.updateShadow ();
    E ('shadow.start', 'i', [fnv (w.shadow.pix), w.shadow.updates.length, ...w.shadow.updates.flat (), ...w.shadow.corner ()]);
    E ('shadow.start.at', 'd', [...c, w.lightAtVan ()]);
    w.shadow.updates.length = 0;
    // driving on: the strips that come in, every frame as the renderer asks
    const lights = [];
    for (let i = 0; i < 1200; ++i) { w.step (1 / 60); w.updateShadow (); if (i % 120 === 0) lights.push (w.lightAtVan ()); }
    E ('shadow.drive', 'i', [fnv (w.shadow.pix), w.shadow.updates.length, fnv (new Int32Array (w.shadow.updates.flat ())), ...w.shadow.corner ()]);
    E ('shadow.drive.light', 'd', lights);
    E ('oaks.drive', 'i', [w.listOaks ().length]);
    // the signs and the chalk come from their tables: the boxes, the places, the jump list
    w.buildSigns (signTable, signAtlas);
    E ('signs.boxes', 'd', [w.signBoxes ().length, ...w.signBoxes ().flatMap (box)]);
    w.updateShadow ();
    E ('shadow.signs', 'i', [fnv (w.shadow.pix), ...w.shadow.corner ()]);
    w.placeChalk (chalkTable, true);
    E ('chalk.places', 'd', w.chalks ().flatMap (c => [c.x, c.z, c.tx, c.tz, c.hw, c.hl, ...c.cell, c.at, c.d]));
    E ('chalk.places.names', 'i', w.chalks ().map (c => fnvS (c.name)));
    const pl = w.places ();
    E ('places.list', 'd', pl.flatMap (q => [q.at, q.seen, q.lap ?? -1, q.kind ?? -1]));
    E ('places.list.names', 'i', pl.flatMap (q => [fnvS (q.group), fnvS (q.name)]));
    w.spriteCame ('tower'); w.spriteCame ('barn');
    E ('places.came', 'd', [w.signBoxes ().length, ...w.signBoxes ().slice (-6).flatMap (box)]);
    E ('places.came.ph', 'i', w.signBoxes ().map (b => fnvS (b.ph)));
    w.placeChalk (chalkTable, false);
    E ('chalk.nowink', 'd', w.chalks ().flatMap (c => [c.x, c.z, c.at, c.d]));
    // the jump list's jumps (the view's jumpTo)
    for (const k of [0, 5, pl.length - 1])
    {
        const q = pl[k], at = (((q.at - q.seen - 3 * w.speedOf ()) % L) + L) % L, lap = q.lap !== undefined ? q.lap : w.state.lap;
        w.setAt (at, lap);
        E (`jump${k}`, 'd', [Math.round (at), lap]);
        st (`jump${k}`, w); sau (`jump${k}`, w);
    }
}
{   // the finds lap by lap, and over the loop's start
    const w = fresh ({ at: 0, lap: 1 }); E ('finds.lap1', 'd', finds (w));
    w.setAt (0, 2); E ('finds.lap2', 'd', finds (w));
    w.setAt (L - 40, 0); w.setGear (2); run (w, 5); st ('lapwrap', w); E ('finds.lapwrap', 'd', finds (w)); E ('finds.lapwrap.next', 'd', [w.nextFindAt ()]);
}

// The drive.
{   // cabin.html?t=14: the drive advanced, then the first frame (dt 0)
    const w = fresh ();
    run (w, 14); w.step (0);
    all ('cabin14', w);
}
for (const gear of [1, 2])
{
    const w = fresh ({ gear });
    for (const t of [5, 30]) { run (w, t - w.state.time); st (`gear${gear}.t${t}`, w); bfl (`gear${gear}.t${t}`, w); }
}
{   // a gear change on the way
    const w = fresh ();
    run (w, 3); w.setGear (2); run (w, 3); st ('shift', w);
    w.setGear (1); run (w, 1); st ('shift.down', w);
}
{   // PARK: standing on the shoulder, the butterflies on the glass; then pulling away, and flying off
    const w = fresh ({ parked: true });
    run (w, 14); all ('park14', w);
    w.setParked (false); run (w, 2); st ('pull2', w); more ('pull2', w);
    run (w, 6); st ('pull8', w); more ('pull8', w);
}
{   // stopping while driving, then on again at DIRTY with a gear change during the pull-away
    const w = fresh ({ gear: 2 });
    run (w, 4); w.setParked (true); run (w, 6); st ('stop6', w); more ('stop6', w);
    w.setParked (false); run (w, 1); w.setGear (1); run (w, 1); st ('pullshift', w);
}
{   // two attacks
    const w = fresh ();
    run (w, 14); w.hit (0.8); run (w, 0.2); st ('hit', w); more ('hit', w); bfl ('hit', w);
    w.hit (1.5); run (w, 0.1); st ('hit2', w); more ('hit2', w);
    run (w, 6); st ('hit.after', w);
}
{   // the pendant over 10 s, through bends at DIRTY
    const w = fresh ({ gear: 2, at: 1500 });
    const v = [];
    for (let k = 0; k < 20; ++k) { run (w, 0.5); const p = w.state.pend; v.push (p.th, p.thV, p.ph, p.phV); }
    E ('pendant.swing', 'd', v);
}
{   // two minutes at CLEAN: the saucers' roaming, beams, a pair, the swoops elsewhere, the flock
    const w = fresh ();
    for (let k = 1; k <= 13; ++k) { run (w, 10); sau (`long${k * 10}`, w); }
    st ('long', w); more ('long', w);
}
{   // the swooping stretch
    const w = fresh ({ gear: 2, at: P.SAUCER.swoopAt - 500 });
    for (let k = 1; k <= 15; ++k) { run (w, 2); sau (`swoop${k}`, w); }
}
{   // PARK with the saucers near, a while
    const w = fresh ({ parked: true });
    run (w, 40); sau ('park40', w);
}
{   // the yellow submarine, crossing
    const w = fresh ({ at: P.SUB.at[0] - 10 });
    for (const t of [3, 12, 20, 31]) { run (w, t - w.state.time); sub (`sub${t}`, w); }
    st ('sub', w);
}

fs.rmSync (tmp, { recursive: true, force: true });
process.stdout.write (out.join ('\n') + '\n');
