#pragma once

#include "../core/PitchTracker.h"
#include "../core/TunerTap.h"
#include "library/MiniClose.h"

#include <vector>

namespace orbitamp
{

class AmpProcessor;

/** The TUNER window, opened from the toolbar's fork: an overlay in SETUP's language — one small
    panel on a scrim, closed by ✕, Esc, or a click outside. A note letter, a cents ruler with a
    needle, and the measured pitch underneath; green means play the next string.

    All the listening happens here, on the editor's clock: a 30 Hz timer snapshots the
    processor's tap and runs the tracker, so a closed tuner costs the audio thread nothing but
    the tap's existence. The needle answers to a median of recent readings, not the newest one —
    the difference between a needle and a nervous one. */
class TunerPanel final : public juce::Component,
                         private juce::Timer
{
public:
    explicit TunerPanel (AmpProcessor&);

    void open();

    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    AmpProcessor& amp;

    core::PitchTracker tracker;
    double preparedSr = 0.0;

    std::array<float, core::TunerTap::size> snap {};

    // What the needle answers to. `recent` holds the last few confident pitches; the display is
    // their median, and it survives `holdMs` of silence before the panel goes idle again.
    std::vector<float> recent;
    juce::uint32       lastValidMs = 0;
    float displayHz   = 0.0f;   // 0 = idle
    float needleCents = 0.0f;   // smoothed towards the median's distance

    juce::Rectangle<int> panel;   // centred; the rest of the bounds is scrim
    MiniClose closeButton;

    static constexpr int panelW = 380;
    static constexpr int panelH = 240;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerPanel)
};

} // namespace orbitamp
