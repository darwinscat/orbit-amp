<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# `changelog.d/` — one file per task, so two branches never collide

Every branch that changes what a player gets writes its release note **here, as a new file**, and leaves
`CHANGELOG.md` alone. Git cannot conflict on two branches adding two different files; it conflicts every
single time they append to the same place in the same file — which is what seven parallel branches did on
the way to 0.7.0, and why their notes could only be written afterwards, in the release branch, far from
the work they described.

**Name the file for the task**: `42-cab-user-irs.md`, `tuner-mute.md` — the PR number when there is one,
a dash, a couple of words. A leading number decides the order the notes appear in.

**Say which amp it is about, on its first line.** `CHANGELOG.md` is the family's book — Darwin's Cat Amps,
every amp released together — and each release in it has a part for every amp at once and one for each
amp of the family. A fragment opens with the amps it is about, by the names `CMakeLists.txt`'s `add_amp()`
gives them:

    Amps: all                     — the engine, the build, what every amp does alike: `### Every amp`
    Amps: OrbitAmp                — one amp: its own part
    Amps: CatpitAmp, RedPawAmp    — several: said in each one's part

A note that tells different amps different things is two fragments. Its `### Dependencies` go to `Every amp`
whatever the line names — what the tree is built from is every amp's. A fragment about amps the release
does not carry (none of them in `installer/family.ini` yet) waits in `changelog.d/` for the release that
does. A fragment without the line, or naming an amp the tree does not build, stops the tool.

**Then write the note the way `CHANGELOG.md` reads**: `### Added`, `### Changed`, `### Fixed`,
`### Dependencies` (and Keep a Changelog's `Deprecated` / `Removed` / `Security`), each with its bullets,
for a player. No version, no date, no `## [x.y.z]` heading — the release adds those, and turns the sections
into its parts' `####`. A few lines of prose above the first section go to the top of the fragment's part —
of the release, for `Amps: all`; a file named `release.md`, written on the release branch, needs no `Amps:`
line and goes above everything.

**At release time**, on the release branch:

    node tools/changelog-collect.mjs --release 0.8.0 --headline "what this release is"

folds every fragment into `CHANGELOG.md` — each part's sections gathered from all of its fragments — under
`## [0.8.0] — date — headline`, and deletes the fragments it took, in one commit where there is nobody to
conflict with. `node tools/changelog-collect.mjs --preview` prints what the next release would say without
touching anything, and which fragments wait: that is how to read the accumulated notes while they live apart.
The release workflow publishes that section as the GitHub Release's notes
(`node tools/changelog-collect.mjs --notes 0.8.0`), and refuses a tag whose version has none.

The tool is felitronics-core's `tools/changelog-collect.mjs`, taught this book's shape.
