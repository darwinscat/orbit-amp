#pragma once

#include "../core/ToneStack.h"
#include "EqCurve.h"
#include "FilterPill.h"
#include "Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** The tone stack, living in the lower half of the preamp block as its illustration.

    Collapsed it is the curve and nothing else — the response is the readout, and most of the time
    that is all you want to see. A grip along the bottom edge opens it: the curve gives up height and
    the knobs appear in what it freed. The section's own height never changes, so opening it does not
    push the block around.

    It keeps a ToneStack of its own for DRAWING only, designed on the message thread from the current
    parameter values. Reading the playing stack's coefficients would be a race and would freeze the
    curve whenever transport is stopped. */
class EqSection final : public juce::Component
{
public:
    explicit EqSection (juce::AudioProcessorValueTreeState&);
    ~EqSection() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    /** How tall the section wants to be. Constant across open and shut. */
    static constexpr int designHeight = 150;

private:
    void refreshCurve();
    void setExpanded (bool);

    /** Rebuilds the draggable points from the current values. */
    void refreshHandles();

    /** A point on the curve moved. Which parameter that is depends on the index. */
    void handleDragged (int index, double hz, double db);
    void handleDragActive (int index, bool active);

    /** Miniature versions of the four knobs, drawn on the grip while the real ones are hidden — the
        strip then says what is behind it instead of just that something is. */
    void paintMiniKnobs (juce::Graphics&, juce::Rectangle<float> grip) const;

    enum Handle { hLow, hMid, hHigh, hPresence, hHpf, hLpf, numHandles };

    juce::Rectangle<int> gripArea() const;

    static constexpr int gripHeight   = 12;
    static constexpr int knobRow      = 84;   // revealed when open; taken from the curve
    static constexpr int knobGap      = 26;
    static constexpr int pillWidth    = 62;
    static constexpr int pillHeight   = 18;
    static constexpr int pillInset    = 8;
    static constexpr int switchWidth  = 22;
    static constexpr int switchHeight = 11;

    juce::AudioProcessorValueTreeState& state;

    static constexpr double displayRate = 48000.0;
    core::ToneStack display;

    EqCurve curve { [this] (double hz) { return display.magnitudeDb (hz); } };

    Knob low      { "Low",      theme::violet, 0 };
    Knob mid      { "Mid",      theme::violet, 0 };
    Knob high     { "High",     theme::violet, 0 };
    Knob presence { "Presence", theme::violet, 0 };

    FilterPill hpf { "HPF", { 20.0, 500.0 } };
    FilterPill lpf { "LPF", { 2000.0, 20000.0 } };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowAtt, midAtt, highAtt, presAtt;
    std::unique_ptr<juce::ParameterAttachment> hpfOnAtt, hpfHzAtt, lpfOnAtt, lpfHzAtt, powerAtt, midHzAtt;

    double midHz = 600.0;

    bool expanded  = false;
    bool gripHover = false;
    bool on        = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
