// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Folds `changelog.d/*.md` into CHANGELOG.md, the family's one book. See changelog.d/README.md for WHY the
// fragments exist and how one says which amp it is about; this file is only the folding.
// felitronics-core's tools/changelog-collect.mjs, brought over and taught this book's shape: Keep a
// Changelog sections, merged across fragments, under `## [X.Y.Z] — date`, one part for every amp.
//
//   node tools/changelog-collect.mjs --preview                              what the next release would say
//   node tools/changelog-collect.mjs --release 0.8.0 --headline "the words"  folds and deletes the fragments
//   node tools/changelog-collect.mjs --notes 0.8.0                          prints that release's section of
//                                                                           CHANGELOG.md — the GitHub Release's notes
//
// A fragment opens with the line `Amps: all` or `Amps: OrbitAmp, CatpitAmp` — the amps it is about, by
// the names CMakeLists.txt's add_amp() gives them — and then has `### Added` / `### Changed` / `### Fixed`
// / … sections with their bullets. The release has a part for every amp at once, `### Every amp`, and one
// for each amp of the family (installer/family.ini), in that file's order; in each part, each section
// gathers that section from every fragment about it, in fragment order, and the sections go in Keep a
// Changelog's order, with Dependencies last. A fragment about several amps is said in each one's part;
// its Dependencies, what the tree is built from, are every amp's and go to `Every amp` whatever it names.
// Anything in a fragment ABOVE its first section is prose for the top of its part — of the release, for
// `Amps: all` — and `release.md`, when there is one, goes first of all and needs no `Amps:` line.
//
// A fragment about amps the release does not carry — none of them in family.ini yet — WAITS: it is shown
// by --preview, left out of the release, and stays in changelog.d/ for the release that carries one of
// them.
//
// Fragment order is by a leading number (`42-cab-irs.md` before `118-tuner.md` — numeric, not
// lexicographic), then by name. README.md is not a fragment.

import { readdirSync, readFileSync, writeFileSync, unlinkSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const dir  = join(root, 'changelog.d');
const book = join(root, 'CHANGELOG.md');

const args     = process.argv.slice(2);
const valueOf  = (flag) => { const i = args.indexOf(flag); return i === -1 ? '' : (args[i + 1] || '').trim(); };
const modes    = ['--preview', '--release', '--notes'].filter(m => args.includes(m));
const mode     = modes.length === 1 ? modes[0] : '';
const version  = mode === '--preview' ? '' : valueOf(mode).replace(/^v/, '');
const headline = valueOf('--headline');

if (! mode)
{
    console.error('usage: changelog-collect.mjs --preview | --release X.Y.Z [--headline "the words"] | --notes X.Y.Z');
    process.exit(2);
}
if (mode !== '--preview' && ! /^\d+\.\d+\.\d+$/.test(version))
{
    console.error(`${mode} needs a version like 0.8.0, got "${version}"`);
    process.exit(2);
}

const sectionOf = (text, ver) => {   // the body of `## [ver]` in the book, or null
    const lines = text.replace(/\r\n/g, '\n').split('\n');
    const start = lines.findIndex(l => l.startsWith(`## [${ver}]`));
    if (start === -1) return null;
    let end = lines.findIndex((l, i) => i > start && l.startsWith('## '));
    if (end === -1) end = lines.length;
    return lines.slice(start + 1, end).join('\n').trim();
};

if (mode === '--notes')
{
    const body = sectionOf(readFileSync(book, 'utf8'), version);
    if (! body)
    {
        console.error(`CHANGELOG.md has no ${version} section — fold the notes first (--release ${version})`);
        process.exit(1);
    }
    console.log(body);
    process.exit(0);
}

const SECTIONS = ['Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security', 'Dependencies'];
const EVERY    = 'Every amp';

// The amps the tree builds, and the ones this release carries.
const treeAmps   = [...readFileSync(join(root, 'CMakeLists.txt'), 'utf8').matchAll(/^add_amp\((\w+)/gm)].map(m => m[1]);
const familyLine = /^amps=(.*)$/m.exec(readFileSync(join(root, 'installer', 'family.ini'), 'utf8'));
const family     = familyLine ? familyLine[1].trim().split(/\s+/).filter(Boolean) : [];
if (treeAmps.length === 0 || family.length === 0)
{
    console.error('no add_amp() in CMakeLists.txt, or no amps= in installer/family.ini');
    process.exit(1);
}

const rank = (name) => {
    if (name === 'release.md') return -1;
    const m = /^(\d+)\b/.exec(name);
    return m ? Number(m[1]) : Number.MAX_SAFE_INTEGER;
};

const files = readdirSync(dir)
    .filter(f => f.endsWith('.md') && f !== 'README.md')
    .sort((a, b) => (rank(a) - rank(b)) || a.localeCompare(b));

if (files.length === 0) { console.error('changelog.d/ holds no fragments'); process.exit(mode === '--preview' ? 0 : 1); }

// Each part of the release — every amp at once, then each amp of the family — with its prose and sections.
const parts   = new Map([EVERY, ...family].map(p => [p, { prose: [], sections: new Map() }]));
const top     = [];   // release.md
const folded  = [];   // the fragments this release takes
const waiting = [];   // ...and the ones it leaves for a later one
const wrong   = [];

for (const f of files)
{
    let text = readFileSync(join(dir, f), 'utf8').replace(/\r\n/g, '\n');
    let audience = [EVERY];

    if (f !== 'release.md')
    {
        const m = /^Amps:[ \t]*(.*)$/m.exec(text);
        if (! m || text.slice(0, m.index).trim())
        {
            wrong.push(`${f}: no "Amps:" line to open it — "Amps: all" or "Amps: <Amp>, <Amp>"`);
            continue;
        }
        text = text.slice(m.index + m[0].length);
        const named = m[1].split(',').map(s => s.trim()).filter(Boolean);
        if (named.length !== 1 || named[0] !== 'all')
        {
            const unknown = named.filter(a => ! treeAmps.includes(a));
            if (named.length === 0 || unknown.length)
            {
                wrong.push(`${f}: "Amps: ${m[1]}" — the amps are ${treeAmps.join(', ')}, or all`);
                continue;
            }
            audience = named.filter(a => family.includes(a));
            if (audience.length === 0) { waiting.push(`${f}  (${named.join(', ')})`); continue; }
        }
    }

    const split = text.split(/^### +(.+?)\s*$/m);          // [prose, title, body, title, body, …]
    const prose = split[0].trim();
    folded.push(f);

    if (f === 'release.md') { if (prose) top.push(prose); }
    else if (prose) for (const a of audience) parts.get(a).prose.push(prose);

    for (let i = 1; i < split.length; i += 2)
    {
        const title = split[i].trim();
        const body  = (split[i + 1] || '').replace(/^\n+|\s+$/g, '');
        if (! body) continue;

        if (! SECTIONS.includes(title)) wrong.push(`${f}: ### ${title} — use ${SECTIONS.join(' / ')}`);
        // What the tree is built from is every amp's, whichever amp the fragment is about.
        for (const a of title === 'Dependencies' ? [EVERY] : audience)
        {
            const sections = parts.get(a).sections;
            if (! sections.has(title)) sections.set(title, []);
            sections.get(title).push(body);
        }
    }
}

if (wrong.length)
{
    console.error(`fragments this book cannot read (changelog.d/README.md):\n  ${wrong.join('\n  ')}`);
    process.exit(1);
}

// The release-wide prose stands above every part; each amp's own heads its part.
const everyProse = parts.get(EVERY).prose;
parts.get(EVERY).prose = [];
const rendered = [...parts]
    .filter(([, p]) => p.prose.length || p.sections.size)
    .map(([name, p]) => [
        `### ${name}`,
        ...p.prose,
        ...SECTIONS.filter(s => p.sections.has(s)).map(s => `#### ${s}\n` + p.sections.get(s).join('\n')),
    ].join('\n\n'));
const release = [...top, ...everyProse, ...rendered].join('\n\n');

if (mode === '--preview')
{
    console.log(`# ${folded.length} fragment(s), in release order — the family: ${family.join(', ')}\n`);
    for (const f of folded) console.log(`  ${f}`);
    if (waiting.length)
    {
        console.log(`\n# ${waiting.length} waiting for a release that carries their amps (installer/family.ini)\n`);
        for (const w of waiting) console.log(`  ${w}`);
    }
    console.log('\n' + '-'.repeat(78) + '\n');
    console.log(release);
    process.exit(0);
}

if (folded.length === 0) { console.error('no fragment is about an amp this release carries'); process.exit(1); }

const date    = new Date().toISOString().slice(0, 10);
const heading = `## [${version}] — ${date}` + (headline ? ` — ${headline}` : '');
let text      = readFileSync(book, 'utf8');

if (sectionOf(text, version) !== null)
{
    console.error(`CHANGELOG.md already has a ${version} section`);
    process.exit(1);
}

// The release opens above the newest one.
const first = text.search(/^## /m);
const at    = first === -1 ? text.length : first;
text = text.slice(0, at) + heading + '\n\n' + release + '\n\n' + text.slice(at);

writeFileSync(book, text);
for (const f of folded) unlinkSync(join(dir, f));
console.log(`CHANGELOG.md: ${heading.slice(3)}, ${folded.length} fragment(s) folded and removed`
            + (waiting.length ? `, ${waiting.length} waiting` : '') + '.');
