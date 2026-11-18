// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// The sea floor's prototype dumped for tests/UnderwaterWorldTests.cpp: `node dump.mjs <prototype dir> > underwater-world.txt`,
// the directory holding sea-gl.js, sea-shaders.js and passby/sprites.txt. The prototype exports only makeWorld, so a
// copy of it is made in a temporary folder with its makers exported too; the prototype itself is not touched. One
// entry a line: label, kind, count, values. Kinds: d a double (within the test's tolerance), f an f32 value (exact),
// i an integer or a 32-bit FNV-1a hash (exact).

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { pathToFileURL } from 'node:url';

const dir = process.argv[2];
const seaGl = fs.readFileSync (path.join (dir, 'sea-gl.js'), 'utf8');
const tmp = fs.mkdtempSync (path.join (os.tmpdir (), 'sea-dump-'));
fs.copyFileSync (path.join (dir, 'sea-shaders.js'), path.join (tmp, 'sea-shaders.js'));
fs.writeFileSync (path.join (tmp, 'sea-gl.js'), seaGl
    + '\nexport { ih, hcell, hs, makeHeight, makeDetail, makeCaustic, icosphere, tubeMesh, floraMeshes, bellMesh, tendrilMesh };\n');
const P = await import (pathToFileURL (path.join (tmp, 'sea-gl.js')).href);

const out = [];
const sha = crypto.createHash ('sha256').update (seaGl).digest ('hex');
out.push (`# The sea floor's prototype, sea-gl.js (sha256 ${sha.slice (0, 16)}..., 16.11.2026), dumped by node ${process.version}`
        + ` (V8, ${process.arch}, ${process.platform}) for tests/UnderwaterWorldTests.cpp. One entry a line: label, kind, count,`);
out.push ('# values. Kinds: d a double (within the test\'s tolerance), f an f32 value (exact), i an integer or a 32-bit FNV-1a hash (exact).');
const E = (label, kind, values) => out.push (`${label} ${kind} ${values.length} ${values.map (v => String (v)).join (' ')}`);

/** FNV-1a over the bytes of a typed array (little-endian, as both sides lay them out). */
const fnv = arr =>
{
    const b = new Uint8Array (arr.buffer, arr.byteOffset, arr.byteLength);
    let h = 0x811c9dc5;
    for (let i = 0; i < b.length; ++i) { h ^= b[i]; h = Math.imul (h, 0x01000193) >>> 0; }
    return h >>> 0;
};
const f32 = a => new Float32Array (a);
const sample = (arr, n) => { const o = []; for (let k = 0; k < n; ++k) o.push (arr[Math.floor (k * arr.length / n)]); return o; };

// The hashes.
E ('hash.ih', 'i', [0, 1, 12345, 0x7fffffff, 0x80000000, 4294967295].map (P.ih));
E ('hash.hcell', 'f', [[0, 0, 0], [1, 2, 3], [-5, 7, 101], [255, 255, 707], [-1, -1, 1010]].map (([a, b, c]) => P.hcell (a, b, c)));
E ('hash.hs', 'f', [[1, 1], [7, 92], [120, 16], [0, 0], [241, 77]].map (([a, b]) => P.hs (a, b)));

// The textures.
const height = P.makeHeight (), detail = P.makeDetail (), caustic = P.makeCaustic ();
E ('tex.height', 'i', [height.length, fnv (height)]);
E ('tex.height.sample', 'f', sample (height, 64));
E ('tex.detail', 'i', [detail.length, fnv (detail), ...sample (detail, 32)]);
E ('tex.caustic', 'i', [caustic.px.length, fnv (caustic.px), ...sample (caustic.px, 32)]);
E ('tex.gain', 'd', [caustic.gain]);

// The meshes.
for (const seed of [77, 78]) { const m = f32 (P.icosphere (seed)); E (`mesh.ico${seed}`, 'i', [m.length, fnv (m)]); }
{ const m = P.tubeMesh (6, 9); E ('mesh.tube', 'i', [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx)]); }
P.floraMeshes ().forEach ((m, k) => E (`mesh.flora${k}`, 'i', [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx)]));
{ const m = P.bellMesh (28, 12); E ('mesh.bell', 'i', [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx)]); }
{ const m = P.tendrilMesh (10, 14); E ('mesh.tendril', 'i', [m.v.length, fnv (m.v), m.idx.length, fnv (m.idx)]); }

// The world: the floor, the jellyfish as made, and the drive in each of its states.
const pitchOffset = (-11.0 + 3.5) * Math.PI / 180;       // cabin.html's
const fresh = ({ gear = 1, parked = false, turbo = false } = {}) =>
{
    const w = P.makeWorld ();
    w.setGear (gear); w.setParked (parked); w.setTurbo (turbo); w.settle ();
    w.state.pitchOffset = pitchOffset;
    return w;
};
const run = (w, seconds, frames) => { const n = Math.round (seconds * 60); for (let i = 0; i < n; ++i) { w.step (1 / 60); if (frames) w.updateJellies (1 / 60); } };
const st = (label, w) =>
{
    const s = w.state;
    E (`${label}.state`, 'd', [s.x, s.z, s.heading, s.pitch, s.camY, s.travel, s.time, s.speed, s.silt, s.flinch, s.curious, s.thrust,
                               ...s.snowFlow, ...s.siltFlow, ...s.bubbleFlow, s.lookHeading, s.hitT, s.hitS]);
};
const sub = (label, w) => { const b = w.state.sub; E (`${label}.sub`, 'd', [b.on ? 1 : 0, b.n, b.x, b.y, b.z, b.fx, b.fz, b.v, b.v0, b.h, b.t, b.d, b.vis]); };
const jel = (label, w) => E (`${label}.jelly`, 'd', w.jelly.flatMap (j => [j.rel[0], j.rel[1], j.rel[2], j.dist, j.outAxis[0], j.outAxis[1], j.outAxis[2], j.phase]));
{
    const w = fresh ();
    E ('floor.h', 'd', [[1200, 800], [0, 0], [30239.5, 17.25], [-3.5, 812.75], [5000.125, 29999.9], [1234.5678, 765.4321]].map (([x, z]) => w.floorHeight (x, z)));
    E ('jelly.init', 'd', w.jelly.flatMap (j => [j.ax, j.az, j.alt, j.scale, j.omega, j.phase, ...j.axis, j.tend, j.curiousW, j.giant ? 1 : 0,
                                                 j.flinchK, j.near, j.side, j.lift, j.vx, j.vz, ...j.col, ...j.glow]));
}
{   // cabin.html?t=14: the drive advanced, then the first frame (dt 0)
    const w = fresh ();
    run (w, 14, false); w.step (0); w.updateJellies (0);
    st ('cabin14', w); sub ('cabin14', w); jel ('cabin14', w);
}
{   // ten seconds of frames
    const w = fresh ();
    run (w, 10, true);
    st ('frames10', w); jel ('frames10', w);
}
{   // the top gear, steered, then let go
    const w = fresh ({ gear: 3 });
    w.setSteer (0.6); run (w, 5, true); st ('gear3.steered', w);
    w.setSteer (0); run (w, 10, true); st ('gear3.free', w); jel ('gear3.free', w);
}
{   // PARK: standing, the curious jellyfish at the glass, the hand turning the view in place, then on again
    const w = fresh ({ parked: true });
    run (w, 14, true); st ('park14', w); jel ('park14', w);
    w.setSteer (1); run (w, 3, true); st ('park.turned', w); jel ('park.turned', w);
    w.setSteer (0); w.setParked (false); run (w, 5, true); st ('park.off', w);
}
{   // a bathyscaphe sent now (SUB), through its pass
    const w = fresh ();
    w.setPassFirst (w.state.time + 0);
    for (const [t, label] of [[0.5, 'a'], [9.5, 'b'], [15, 'c'], [20, 'd']]) { run (w, t, false); sub (`sub.${label}`, w); }
    st ('sub', w);
}
{   // ...and one sent while the hand steers left
    const w = fresh ({ gear: 2 });
    w.setSteer (-0.5); w.setPassFirst (w.state.time + 0);
    run (w, 6, false); sub ('sub.left', w);
}
{   // an attack, 0.2 s on
    const w = fresh ();
    run (w, 14, false); w.hit (0.8); run (w, 0.2, true);
    st ('hit', w);
}
{   // the prototype's TURBO: the silt, the flinch, the bubbles
    const w = fresh ({ gear: 2 });
    w.setTurbo (true); run (w, 3, true); st ('turbo.on', w); jel ('turbo.on', w);
    w.setTurbo (false); run (w, 3, true); st ('turbo.off', w);
}

// The sprites' table, as createSeaView reads it.
{
    const txt = fs.readFileSync (path.join (dir, 'passby', 'sprites.txt'), 'utf8');
    let passCell = [1024, 576], passScale = 0, passGrid = [4, 4];
    const cells = [];
    for (const line of txt.split ('\n'))
    {
        const w = line.trim ().split (/\s+/);
        if (line.startsWith ('#') && line.includes ('px per metre'))
        {
            const m = line.match (/cells (\d+) x (\d+) px, ([\d.]+) px per metre/);
            if (m) { passCell = [+m[1], +m[2]]; passScale = +m[3]; }
            const g = line.match (/atlas (\d+) x (\d+)/);
            if (g) passGrid = [+g[1], +g[2]];
        }
        else if (w[0] === 'cell') cells.push ([]);
        else if (w[0] === 'lamp' && cells.length) cells[cells.length - 1].push ({ x: +w[2], y: +w[3], dx: +w[4], dy: +w[5], toward: +w[6] });
    }
    E ('sprites.head', 'd', [...passCell, passScale, ...passGrid, cells.length]);
    E ('sprites.lamps', 'd', cells.flatMap (c => [c.length, ...c.flatMap (L => [L.x, L.y, L.dx, L.dy, L.toward])]));
}

fs.rmSync (tmp, { recursive: true, force: true });
process.stdout.write (out.join ('\n') + '\n');
