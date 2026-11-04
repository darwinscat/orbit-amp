// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>

namespace orbitamp
{

/** THE MICS' GLYPHS: six silhouettes, line art in one colour, hanging — the capsule at the bottom, the cable at the top —
    in a 24 x 64 box, drawn in black so a caller recolours them (Drawable::replaceColour). By the names the cabinets'
    table gives its mics (tools/cabgen/cabinets.txt). Message thread. */
inline const juce::Drawable* micGlyph (const juce::String& name)
{
    static const std::map<juce::String, const char*> svgs {
        { "md421", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v4M6.5 21 9 6h6l2.5 15M6 27 4 51q-.5 9 6 9h4q6.5 0 6-9l-2-24"/> <rect x="5" y="21" width="14" height="6" rx="1.5"/> <path stroke-width="0.8" d="m8 32-1 20m3.5-20L10 55m3.5-23 .5 23m2-23 1 20M7 57q5 2 10 0"/></svg>)svg" },
        { "sm57", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v5"/> <rect x="7.5" y="7" width="9" height="40" rx="1.5"/> <path d="M8.5 47v11q0 2 2 2h3q2 0 2-2V47M8.5 50h7"/> <path stroke-width="0.8" d="M9 53h6m-6 2.5h6M9.5 58h5"/></svg>)svg" },
        { "re20", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v5"/> <rect x="5.5" y="7" width="13" height="53" rx="3"/> <path d="M5.5 41h13m-13 3h13"/> <path stroke-width="0.8" d="M7.5 17h3m3 0h3m-9 6h3m3 0h3m-9 6h3m3 0h3M7 47h10M7 50h10M7 53h10M7.5 56h9M9 58h6"/></svg>)svg" },
        { "d112", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v8"/> <rect x="7" y="10" width="10" height="7" rx="2"/> <path d="M9.5 17v11m5-11v11M12 27C6 27 3 34 3 43s4 17 9 17 9-8 9-17-3-16-9-16Z"/> <path stroke-width="0.8" d="M5 36q7-4 14 0M3.5 43q8.5-4 17 0M4.5 50q7.5-4 15 0M7 56q5-3 10 0M9 30q-5 13 1 28M15 30q5 13-1 28"/></svg>)svg" },
        { "u87", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v5M5 29V9q0-2 2-2h10q2 0 2 2v20"/> <rect x="5" y="29" width="14" height="4"/> <path d="M5 33v20q0 7 7 7t7-7V33"/> <path stroke-width="0.8" d="M6 38h12M6 43h12M6 48h12M6 53h12M8 57h8M9 35v22m6-22v22"/></svg>)svg" },
        { "ribbon", R"svg(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 64" stroke="#000000" fill="none" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round"> <path d="M12 2v5"/> <rect x="7" y="5" width="10" height="55" rx="3"/> <rect x="9" y="32" width="6" height="24" rx="1.5"/> <path stroke-width="0.8" d="M12 35v18M9 39h6m-6 10h6"/></svg>)svg" },
    };
    static std::map<juce::String, std::unique_ptr<juce::Drawable>> parsed;

    if (auto it = parsed.find (name); it != parsed.end())
        return it->second.get();

    const auto at = svgs.find (name);
    if (at == svgs.end())
        return nullptr;

    std::unique_ptr<juce::Drawable> d;
    if (auto xml = juce::parseXML (at->second))
        d = juce::Drawable::createFromSVG (*xml);
    return (parsed[name] = std::move (d)).get();
}

} // namespace orbitamp
