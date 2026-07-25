#pragma once

#include "Theme.h"

namespace orbitamp
{

/** The HPF / LPF cut control: a small pill that reads "HPF 80" — click toggles it, vertical drag
    sweeps the corner. Two controls in the footprint of one, because these cuts are occasional
    rig-tightening moves, not part of the voicing, and should not take knob space on the face. */
class FilterPill : public juce::Component
{
public:
    FilterPill (juce::String pillLabel, juce::Range<double> freqRange)
        : label (std::move (pillLabel)), range (freqRange) {}

    void setState (bool isOn, double freqHz)
    {
        on = isOn;
        hz = juce::jlimit (range.getStart(), range.getEnd(), freqHz);
        repaint();
    }

    bool  isOn() const noexcept    { return on; }
    double getHz() const noexcept  { return hz; }

    std::function<void (bool)>   onToggled;
    std::function<void (double)> onFreqChanged;
    std::function<void (bool)>   onDragActive;   // brackets the sweep as one host gesture

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colour (0xff0d0d14));
        g.fillRoundedRectangle (r, theme::radiusSm);
        g.setColour (on ? theme::violet : theme::hair2);
        g.drawRoundedRectangle (r.reduced (0.5f), theme::radiusSm, 1.0f);

        g.setColour (on ? theme::lilac : theme::txFaint);
        theme::drawTracked (g, label + " " + formatHz(), r.reduced (6.0f, 0.0f),
                            theme::displayFont (8.0f), 0.06f, juce::Justification::centred);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragStartHz = hz;
        dragged = false;
        juce::ignoreUnused (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragged && e.getDistanceFromDragStartY() != 0)
        {
            dragged = true;
            if (onDragActive) onDragActive (true);
        }

        if (! dragged)
            return;

        // Dragging up raises the corner. A decade per 120 px keeps the whole sweep inside one
        // comfortable gesture without making it twitchy.
        const double decades = -e.getDistanceFromDragStartY() / 120.0;
        const double next = juce::jlimit (range.getStart(), range.getEnd(), dragStartHz * std::pow (10.0, decades));

        if (next != hz)
        {
            hz = next;
            repaint();
            if (onFreqChanged) onFreqChanged (hz);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragged)
        {
            dragged = false;
            if (onDragActive) onDragActive (false);
            return;
        }

        on = ! on;
        repaint();
        if (onToggled) onToggled (on);
    }

private:
    juce::String formatHz() const
    {
        return hz >= 1000.0 ? juce::String (hz / 1000.0, hz >= 10000.0 ? 0 : 1) + "K"
                            : juce::String (juce::roundToInt (hz));
    }

    juce::String label;
    juce::Range<double> range;
    bool   on = false;
    double hz = 100.0;
    double dragStartHz = 100.0;
    bool   dragged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FilterPill)
};

} // namespace orbitamp
