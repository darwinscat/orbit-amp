#include "TunerPanel.h"

#include "../PluginProcessor.h"
#include "Theme.h"

#include <algorithm>

namespace orbitamp
{

namespace
{
    constexpr float inTuneCents = 2.5f;    // within this the letter and needle go green
    constexpr int   holdMs      = 700;     // how long a note outlives its last confident reading
    constexpr int   medianDepth = 5;

    const juce::Colour inTune { 0xff5fc97a };   // the character ramp's clean green
}

TunerPanel::TunerPanel (AmpProcessor& processor) : amp (processor)
{
    setWantsKeyboardFocus (true);

    closeButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (closeButton);

    recent.reserve (medianDepth);
}

void TunerPanel::open()
{
    recent.clear();
    displayHz   = 0.0f;
    needleCents = 0.0f;

    setVisible (true);
    toFront (false);
    grabKeyboardFocus();
    startTimerHz (30);
}

void TunerPanel::visibilityChanged()
{
    // The scrim click and the ✕ both land here, so however the window closes, the clock stops.
    if (! isVisible())
        stopTimer();
}

void TunerPanel::timerCallback()
{
    if (const double sr = amp.currentSampleRate(); sr > 0.0 && ! juce::approximatelyEqual (sr, preparedSr))
    {
        tracker.prepare (sr);
        preparedSr = sr;
    }

    core::PitchTracker::Reading reading;
    if (preparedSr > 0.0 && amp.tunerTap.read (snap))
        reading = tracker.analyse (snap.data(), (int) snap.size());

    const auto now = juce::Time::getMillisecondCounter();

    if (reading.hz > 0.0f)
    {
        if ((int) recent.size() >= medianDepth)
            recent.erase (recent.begin());
        recent.push_back (reading.hz);
        lastValidMs = now;
    }
    else if (recent.empty() || now - lastValidMs > (juce::uint32) holdMs)
    {
        recent.clear();
        displayHz = 0.0f;
    }

    if (! recent.empty())
    {
        auto sorted = recent;
        std::nth_element (sorted.begin(), sorted.begin() + (long) sorted.size() / 2, sorted.end());
        displayHz = sorted[sorted.size() / 2];

        const auto note = core::PitchTracker::nearestNote (displayHz);
        needleCents += 0.45f * (note.cents - needleCents);
    }

    repaint();
}

void TunerPanel::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (panelW, panelH);

    const auto header = panel.reduced (16, 12).removeFromTop (26);
    closeButton.setBounds (header.withLeft (header.getRight() - 24).reduced (1));
}

void TunerPanel::paint (juce::Graphics& g)
{
    // The scrim IS the modality: the device stays visible but unmistakably behind.
    g.fillAll (theme::ground.withAlpha (0.78f));

    const auto p = panel.toFloat();
    g.setColour (theme::panel);
    g.fillRoundedRectangle (p, theme::radiusLg);
    g.setColour (theme::hair2);
    g.drawRoundedRectangle (p.reduced (0.75f), theme::radiusLg, 1.5f);

    auto r = panel.reduced (16, 12);
    g.setColour (theme::tx);
    theme::drawTracked (g, "TUNER", r.removeFromTop (26).toFloat(), theme::displayFont (13.0f),
                        0.15f, juce::Justification::centredLeft);

    const bool live  = displayHz > 0.0f;
    const auto note  = live ? core::PitchTracker::nearestNote (displayHz) : core::PitchTracker::Note {};
    const bool green = live && std::abs (needleCents) <= inTuneCents;

    // ---- the note: one big letter, its octave small beside it ----
    auto noteArea = r.removeFromTop (92).toFloat();
    if (live)
    {
        const auto name   = juce::String (core::PitchTracker::noteName (note.midi));
        const auto octave = juce::String (core::PitchTracker::noteOctave (note.midi));
        const auto font   = theme::displayFont (56.0f);

        const float w = theme::trackedWidth (name, font, 0.05f);
        g.setColour (green ? inTune : theme::tx);
        theme::drawTracked (g, name, noteArea.withWidth (w).withX (noteArea.getCentreX() - w * 0.5f - 8.0f),
                            font, 0.05f, juce::Justification::centred);

        g.setColour (theme::txDim);
        theme::drawTracked (g, octave,
                            noteArea.withWidth (20.0f).withX (noteArea.getCentreX() + w * 0.5f)
                                    .withTrimmedTop (noteArea.getHeight() * 0.5f),
                            theme::displayFont (16.0f), 0.05f, juce::Justification::centredLeft);
    }
    else
    {
        // Not a glyph — the display face owes us no dash, and a missing one reads as garbage.
        g.setColour (theme::txFaint);
        g.fillRoundedRectangle (noteArea.getCentreX() - 16.0f, noteArea.getCentreY() - 2.0f,
                                32.0f, 4.0f, 2.0f);
    }

    // ---- the ruler: -50..+50 cents, the centre tick tallest ----
    const auto ruler = r.removeFromTop (44).reduced (24, 0).toFloat();
    const float mid  = ruler.getY() + 26.0f;

    for (int c = -50; c <= 50; c += 10)
    {
        const float x = ruler.getX() + ruler.getWidth() * (float) (c + 50) / 100.0f;
        const float h = c == 0 ? 16.0f : (c % 50 == 0 ? 10.0f : 6.0f);
        g.setColour (c == 0 ? theme::hair2 : theme::hair);
        g.fillRect (juce::Rectangle<float> (x - 0.5f, mid - h, 1.0f, h));
    }

    if (live)
    {
        const float x = ruler.getX()
                      + ruler.getWidth() * (juce::jlimit (-50.0f, 50.0f, needleCents) + 50.0f) / 100.0f;
        g.setColour (green ? inTune : theme::tx);
        g.fillRoundedRectangle (x - 1.25f, mid - 22.0f, 2.5f, 28.0f, 1.25f);
    }

    // ---- the numbers, for whoever wants them ----
    if (live)
    {
        const auto cents = juce::String (juce::roundToInt (note.cents));
        g.setColour (theme::txDim);
        theme::drawTracked (g,
                            juce::String (displayHz, 1) + " HZ   "
                                + (note.cents >= 0.5f ? "+" : "") + cents + " C",
                            r.removeFromTop (20).toFloat(), theme::displayFont (10.0f), 0.15f,
                            juce::Justification::centred);
    }
}

void TunerPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! panel.contains (e.getPosition()))
        setVisible (false);
}

bool TunerPanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }

    return false;
}

} // namespace orbitamp
