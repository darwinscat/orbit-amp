#pragma once

#include "../core/ToneStack.h"
#include "EqCurve.h"
#include "FilterPill.h"
#include "Knob.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace orbitamp
{

/** The tone stack inside the preamp block: knobs in the upper half, the response curve across the
    lower one.

    NOT a Component. Its widgets become children of the block itself, because they live in two
    separate regions of it — a container would have to be either non-rectangular or two containers,
    and both are worse than owning the widgets and letting the block place them.

    It keeps a ToneStack of its own for DRAWING only, designed on the message thread from the current
    parameter values. Reading the playing stack's coefficients would be a race and would freeze the
    curve whenever transport is stopped. */
class EqSection final
{
public:
    explicit EqSection (juce::AudioProcessorValueTreeState&);
    ~EqSection();

    /** Hands the widgets to the block that will place them. */
    void addTo (juce::Component& parent);

    /** The curve and the two cut pills, across the block's lower half. */
    void layOutCurve (juce::Rectangle<int> area);

    /** How much of the block's height the curve takes. */
    static constexpr int designHeight = 150;

    /** The knob cluster, laid out by the owner in the block's upper half: Low / Mid / High in a row
        with Presence above them. Presence is not a fourth sibling — it sits over the tone trio. */
    void layOutKnobs (juce::Rectangle<int> area);

private:
    void refreshCurve();

    /** Rebuilds the draggable points from the current values. */
    void refreshHandles();

    /** A point on the curve moved. Which parameter that is depends on the index. */
    void handleDragged (int index, double hz, double db);
    void handleDragActive (int index, bool active);

    enum Handle { hLow, hMid, hHigh, hPresence, hHpf, hLpf, numHandles };

    static constexpr int knobGap      = 16;
    static constexpr int presenceGap  = 6;
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

    bool on = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqSection)
};

} // namespace orbitamp
