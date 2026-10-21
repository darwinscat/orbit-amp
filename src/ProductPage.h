// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_core/juce_core.h>

namespace orbitamp
{

/** THE PRODUCT'S PAGE ON THE MAKER'S SITE, as every link to it is built — a face's badge, the version
    window's product line, OrbitAmp's wordmark: https://darwinscat.com/felitronics/amps/<slug>, the slug the
    product's name in lower case and no language prefix (the site picks the reader's), with the site's
    analytics tag after it: utm_source the slug, utm_medium "plugin" where a host loaded the amp and "app"
    where it stands alone. */
inline juce::String productPageUrl (const juce::String& productName, bool standalone)
{
    const auto slug = productName.toLowerCase();
    return "https://darwinscat.com/felitronics/amps/" + slug + "?utm_source=" + slug
         + "&utm_medium=" + (standalone ? "app" : "plugin");
}

/** ...and the tag the family's other links carry, as they always have — the maker's own page, and the tip jar,
    whose hop adds from=<slug> and the rest itself (felitronics::appkit::brand::feedTheCatLink). Not the
    product page's: the tip jar's statistics stay as they were. */
inline juce::String familyLinkTag (const juce::String& productName)
{
    return "utm_source=" + productName.toLowerCase() + "&utm_medium=plugin";
}

} // namespace orbitamp
