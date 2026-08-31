// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <felitronics/appkit/DeviceGlyph.h>

namespace orbitamp
{

/** TEMPORARY — a strip showing every device glyph at three sizes, to judge them by eye.

    Delete this file and its two call sites in PluginEditor when the glyphs are settled. It exists
    because "reads at 20 px" is a claim you can only check by looking. */
class GlyphPreview final : public juce::Component
{
public:
    GlyphPreview() = default;   // the non-copyable macro below declares a ctor, which suppresses this

    static constexpr int designHeight = 96;

    void paint (juce::Graphics& g) override
    {
        using felitronics::appkit::DeviceType;

        struct Entry { DeviceType type; const char* name; };
        static constexpr Entry entries[] = {
            { DeviceType::tube,  "tube"  },
            { DeviceType::bjt,   "bjt"   },
            { DeviceType::fet,   "fet"   },
            { DeviceType::ic,    "ic"    },
            { DeviceType::dsp,   "dsp"   },
            { DeviceType::diode, "diode" },
        };
        static constexpr float sizes[] = { 16.0f, 26.0f, 44.0f };

        const int   n  = (int) (sizeof (entries) / sizeof (entries[0]));
        const float cw = (float) getWidth() / (float) n;

        for (int i = 0; i < n; ++i)
        {
            const auto& e = entries[i];
            auto column = juce::Rectangle<float> ((float) i * cw, 0.0f, cw, (float) getHeight());

            g.setColour (theme::txFaint);
            theme::drawTracked (g, juce::String (e.name).toUpperCase(), column.removeFromBottom (12.0f),
                                theme::displayFont (8.0f), 0.12f, juce::Justification::centred);

            // The three sizes on one baseline, so they can be compared without moving your eye.
            float x = column.getX() + 8.0f;
            const float baseline = column.getCentreY();

            for (const float s : sizes)
            {
                const juce::Rectangle<float> cell { x, baseline - s * 0.5f, s, s };
                felitronics::appkit::drawDeviceGlyph (g, cell, e.type,
                                                      felitronics::appkit::deviceStroke (e.type));
                x += s + 6.0f;
            }
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlyphPreview)
};

} // namespace orbitamp
