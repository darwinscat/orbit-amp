<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# `changelog.d/` — one file per task, so two branches never collide

Every branch that changes what a player gets writes its release note **here, as a new file**, and leaves
`CHANGELOG.md` alone. Git cannot conflict on two branches adding two different files; it conflicts every
single time they append to the same place in the same file — which is what seven parallel branches did on
the way to 0.7.0, and why their notes could only be written afterwards, in the release branch, far from
the work they described.

**Name the file for the task**: `42-cab-user-irs.md`, `tuner-mute.md` — the PR number when there is one,
a dash, a couple of words. A leading number decides the order the notes appear in.

**Write the note the way `CHANGELOG.md` reads**: `### Added`, `### Changed`, `### Fixed`,
`### Dependencies` (and Keep a Changelog's `Deprecated` / `Removed` / `Security`), each with its bullets,
for a player. No version, no date, no `## [x.y.z]` heading — the release adds those. A few lines of
prose above the first section go to the top of the release; a file named `release.md`, written on the
release branch, goes above everything.

**At release time**, on the release branch:

    node tools/changelog-collect.mjs --release 0.8.0 --headline "what this release is"

folds every fragment into `CHANGELOG.md` — each section gathered from all of them — under
`## [0.8.0] — date — headline`, and deletes the fragments, in one commit where there is nobody to conflict
with. `node tools/changelog-collect.mjs --preview` prints what the next release would say without touching
anything: that is how to read the accumulated notes while they live apart.

The tool is felitronics-core's `tools/changelog-collect.mjs`, taught this book's shape.
