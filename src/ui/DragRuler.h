// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Theme.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace orbitamp
{

/** A runner's OWN scale, summoned by the hand: while the runner is being dragged, this ruler
    slides out beside its column — frosted over whatever lives there — with the ladder the runner
    actually moves on, and a line in the runner's colour at the value in hand. Let go, gone.

    Data-driven so every runner can bring its ladder: the marks, the dB-to-height mapping and the
    accent are wired in by the editor. The mapping MUST mirror the strip's own (and the ruler's
    bounds its column's vertical extent), so the marks land at the runner's true heights.

    A projection, not a control: it intercepts nothing, and it repaints itself while summoned —
    nobody underneath can be trusted to. */
class DragRuler final : public juce::Component,
                        private juce::Timer
{
public:
    DragRuler() { setInterceptsMouseClicks (false, false); }

    struct Mark
    {
        float db;
        bool  major;
    };

    std::vector<Mark> marks;

    /** Numbers and ticks hug the edge nearest the column: left when the ruler stands right of
        it (IN), right when it stands left (OUT). */
    bool ticksOnLeft = true;

    /** 0 = whole-dB labels with a plus on the positives; 1 = one decimal, sign as it falls. */
    int labelDecimals = 0;

    juce::Colour accent = juce::Colour (0xffff8a3d);

    /** dB -> y inside the ruler's reduced(2) area — the strip's own formula, transplanted. */
    std::function<float (juce::Rectangle<float>, float)> yOfDb;

    /** The value in hand, plain dB — the accent line. */
    std::function<float()> currentDb;

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (30);
        else
            stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        if (yOfDb == nullptr)
            return;

        auto r = getLocalBounds().toFloat();

        // Frosted glass, honestly faked: a deep translucent sheet with a hairline.
        g.setColour (theme::panel.withAlpha (0.88f));
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        const auto area = r.reduced (2.0f);

        for (const auto& m : marks)
        {
            const float y   = yOfDb (area, m.db);
            const float len = m.major ? 7.0f : 4.5f;

            g.setColour (juce::Colours::white.withAlpha (m.major ? 0.45f : 0.28f));

            if (ticksOnLeft)
                g.fillRect (area.getX(), y - 1.0f, len, 2.0f);
            else
                g.fillRect (area.getRight() - len, y - 1.0f, len, 2.0f);

            if (m.major)
            {
                const auto text = labelDecimals == 0
                                ? (m.db > 0 ? "+" : "") + juce::String ((int) m.db)
                                : juce::String (m.db, 1);

                g.setColour (juce::Colours::white.withAlpha (0.5f));
                theme::drawTracked (g, text,
                                    { ticksOnLeft ? area.getX() + 10.0f : area.getX() + 2.0f,
                                      y - 6.0f, area.getWidth() - 14.0f, 12.0f },
                                    theme::displayFont (10.5f), 0.04f,
                                    ticksOnLeft ? juce::Justification::centredLeft
                                                : juce::Justification::centredRight);
            }
        }

        if (currentDb != nullptr)
        {
            g.setColour (accent);
            g.fillRect (area.getX(), yOfDb (area, currentDb()) - 1.0f, area.getWidth(), 2.0f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DragRuler)
};

/** The horizontal cousin, MAGNIFIED: summoned under a thumb's gain runner, wider than the
    runner itself — a projection, not an echo. Integers major with numbers, halves minor, the
    value in hand as an orange line with its number riding it. */
class HDragRuler final : public juce::Component,
                         private juce::Timer
{
public:
    HDragRuler() { setInterceptsMouseClicks (false, false); }

    float minV = 0.0f, maxV = 10.0f;

    std::function<float()> currentValue;

    void visibilityChanged() override
    {
        if (isVisible())
            startTimerHz (30);
        else
            stopTimer();
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (theme::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        const auto area = r.reduced (10.0f, 3.0f);

        for (float v = minV; v <= maxV + 0.01f; v += 0.5f)
        {
            const bool  major = std::abs (v - std::round (v)) < 0.01f;
            const float x     = xOf (area, v);

            g.setColour (juce::Colours::white.withAlpha (major ? 0.45f : 0.28f));
            g.fillRect (x - 1.0f, area.getBottom() - (major ? 9.0f : 6.0f), 2.0f,
                        major ? 9.0f : 6.0f);

            if (major)
            {
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                theme::drawTracked (g, juce::String ((int) std::round (v)),
                                    { x - 12.0f, area.getY(), 24.0f, 13.0f },
                                    theme::displayFont (11.0f), 0.04f, juce::Justification::centred);
            }
        }

        if (currentValue != nullptr)
        {
            const float x = xOf (area, currentValue());
            g.setColour (theme::orange);
            g.fillRect (x - 1.0f, r.getY() + 2.0f, 2.0f, r.getHeight() - 4.0f);
        }
    }

private:
    void timerCallback() override { repaint(); }

    float xOf (juce::Rectangle<float> area, float v) const
    {
        return area.getX()
             + area.getWidth() * juce::jlimit (0.0f, 1.0f, (v - minV) / (maxV - minV));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HDragRuler)
};

} // namespace orbitamp
