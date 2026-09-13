// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

// Folds `changelog.d/*.md` into CHANGELOG.md. See changelog.d/README.md for WHY the fragments exist;
// this file is only the folding. felitronics-core's tools/changelog-collect.mjs, brought over and taught
// this book's shape: Keep a Changelog sections, merged across fragments, under `## [X.Y.Z] — date`.
//
//   node tools/changelog-collect.mjs --preview                              what the next release would say
//   node tools/changelog-collect.mjs --release 0.8.0 --headline "the words"  folds and deletes the fragments
//
// A fragment is `### Added` / `### Changed` / `### Fixed` / … sections with their bullets. Each section of
// the release gathers that section from every fragment, in fragment order; the sections themselves go in
// Keep a Changelog's order, with Dependencies last. Anything in a fragment ABOVE its first section is
// prose for the top of the release, and `release.md` — when there is one — goes first of all.
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
const preview  = args.includes('--preview');
const release  = args.includes('--release');
const version  = valueOf('--release').replace(/^v/, '');
const headline = valueOf('--headline');

if (preview === release)
{
    console.error('usage: changelog-collect.mjs --preview | --release X.Y.Z [--headline "the words"]');
    process.exit(2);
}
if (release && ! /^\d+\.\d+\.\d+$/.test(version))
{
    console.error(`--release needs a version like 0.8.0, got "${version}"`);
    process.exit(2);
}

const SECTIONS = ['Added', 'Changed', 'Deprecated', 'Removed', 'Fixed', 'Security', 'Dependencies'];

const rank = (name) => {
    if (name === 'release.md') return -1;
    const m = /^(\d+)\b/.exec(name);
    return m ? Number(m[1]) : Number.MAX_SAFE_INTEGER;
};

const files = readdirSync(dir)
    .filter(f => f.endsWith('.md') && f !== 'README.md')
    .sort((a, b) => (rank(a) - rank(b)) || a.localeCompare(b));

if (files.length === 0) { console.error('changelog.d/ holds no fragments'); process.exit(preview ? 0 : 1); }

// Each fragment, split into its prose (above the first `###`) and its sections.
const prose    = [];
const sections = new Map();
const unknown  = [];

for (const f of files)
{
    const text  = readFileSync(join(dir, f), 'utf8').replace(/\r\n/g, '\n');
    const parts = text.split(/^### +(.+?)\s*$/m);          // [prose, title, body, title, body, …]

    if (parts[0].trim()) prose.push(parts[0].trim());

    for (let i = 1; i < parts.length; i += 2)
    {
        const title = parts[i].trim();
        const body  = (parts[i + 1] || '').replace(/^\n+|\s+$/g, '');
        if (! body) continue;

        if (! SECTIONS.includes(title)) unknown.push(`${f}: ### ${title}`);
        if (! sections.has(title)) sections.set(title, []);
        sections.get(title).push(body);
    }
}

if (unknown.length)
{
    console.error(`sections this book does not have — use ${SECTIONS.join(' / ')}:\n  ${unknown.join('\n  ')}`);
    process.exit(1);
}

const folded = [
    ...prose,
    ...SECTIONS.filter(s => sections.has(s)).map(s => `### ${s}\n` + sections.get(s).join('\n')),
].join('\n\n');

if (preview)
{
    console.log(`# ${files.length} fragment(s), in release order\n`);
    for (const f of files) console.log(`  ${f}`);
    console.log('\n' + '-'.repeat(78) + '\n');
    console.log(folded);
    process.exit(0);
}

const date    = new Date().toISOString().slice(0, 10);
const heading = `## [${version}] — ${date}` + (headline ? ` — ${headline}` : '');
let text      = readFileSync(book, 'utf8');

if (new RegExp(`^## \\[${version.replace(/\./g, '\\.')}\\]`, 'm').test(text))
{
    console.error(`CHANGELOG.md already has a ${version} section`);
    process.exit(1);
}

// The release opens above the newest one.
const first = text.search(/^## /m);
const at    = first === -1 ? text.length : first;
text = text.slice(0, at) + heading + '\n\n' + folded + '\n\n' + text.slice(at);

writeFileSync(book, text);
for (const f of files) unlinkSync(join(dir, f));
console.log(`CHANGELOG.md: ${heading.slice(3)}, ${files.length} fragment(s) folded and removed.`);
