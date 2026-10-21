// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <BinaryData.h>
#include <OrbitAmpVersion.h>
#include <felitronics/appkit/VersionBadge.h>

namespace orbitamp
{

/** THE VERSION WINDOW'S CONTENTS — the build stamp, what the binary was built against, the licence, the
    trademark notice and the tip jar — for whichever face is asking. Everything the badge cannot know:
    who we are, what this build is, and the palette it draws in. The version and the GitHub slug come
    from the checker, so they can never drift.

    A face brings only what is its own: its name, its page — the whole link, its tag on it (ProductPage.h)
    — and `campaign`, the tag the maker's page and the tip jar carry, for the SITE's analytics (appkit adds
    what describes the build: the product slug, the machine, the wrapper). One processor, one build, two
    faces: the rest is the same window. */
inline felitronics::appkit::VersionBadge::Config aboutConfig (const juce::String& productName,
                                                             const juce::String& productUrl,
                                                             const juce::String& campaign)
{
    // JUCE names its own version at compile time — "JUCE v8.0.14"; the row wants the tag alone.
    const juce::String juceVersion = juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf (" ", false, false);

    // The maker's mark beside the product's, the way the window header carries both: the same
    // embedded cat, drawn from the same bytes. The popover outlives the badge that launched it,
    // so the drawable is shared rather than borrowed.
    std::shared_ptr<juce::Drawable> cat { juce::Drawable::createFromImageData (
        BinaryData::catlogo_svg, (size_t) BinaryData::catlogo_svgSize).release() };

    return { .productName  = productName,
             .productUrl   = productUrl,
             .makerUrl     = juce::String ("https://darwinscat.com?") + campaign,
             .gitHash      = version::kGitHash,
             .buildNumber  = version::kBuildNumber,
             .buildCount   = version::kBuildCount,
             .gitDirty     = version::kGitDirty,
             .os           = version::kOS,
             .arch         = version::kArch,
             .builder      = version::kBuilder,
             .licence      = "AGPL-3.0+",   // the row is one line: the long spelling truncates
             // What this binary was actually built against — sibling checkout or pin, and the
             // commit when there was a checkout to ask. The stamp header bakes it at build time.
             .dependencies = { { .label     = "felitronics-core",
                                 .version   = version::kCoreVersion,
                                 .ownerRepo = "darwinscat/felitronics-core",
                                 .commit    = version::kCoreCommit,
                                 .state     = version::kCoreState },
                               { .label     = "felitronics-guitar-core",
                                 .version   = version::kGuitarVersion,
                                 .ownerRepo = "darwinscat/felitronics-guitar-core",
                                 .commit    = version::kGuitarCommit,
                                 .state     = version::kGuitarState },
                               { .label     = "felitronics-appkit",
                                 .version   = version::kAppkitVersion,
                                 .ownerRepo = "darwinscat/felitronics-appkit",
                                 .commit    = version::kAppkitCommit,
                                 .state     = version::kAppkitState },
                               { .label     = "namz",
                                 .version   = version::kNamzVersion,
                                 .ownerRepo = "darwinscat/namz",
                                 .commit    = version::kNamzCommit,
                                 .state     = version::kNamzState },
                               { .label     = "NeuralAmpModelerCore",
                                 .version   = version::kNamCoreVersion,
                                 .ownerRepo = "sdatkinson/NeuralAmpModelerCore",
                                 .commit    = version::kNamCoreCommit,
                                 .state     = "pin" },
                               { .label     = "JUCE",
                                 .version   = juceVersion,
                                 .ownerRepo = "juce-framework/JUCE" } },
             .drawByline   = [cat] (juce::Graphics& g, float cx, float cy, float d)
                             {
                                 if (cat != nullptr)
                                     cat->drawWithin (g, { cx - d * 0.5f, cy - d * 0.5f, d, d },
                                                      juce::RectanglePlacement::centred, 1.0f);
                             },
             .accent       = theme::violet,
             .accentHover  = theme::lilac,
             .accentB      = theme::orange,
             .text         = theme::tx,
             // The trademark notice, from resources/notice.txt — a file rather than a string
             // literal, because it is a legal sentence the README carries word for word and a
             // file can be diffed against that one.
             .notice       = juce::String::fromUTF8 (BinaryData::notice_txt, BinaryData::notice_txtSize).trim(),
             .feedUrl      = juce::String (felitronics::appkit::brand::feedTheCatUrl) + "?" + campaign };
}

} // namespace orbitamp
