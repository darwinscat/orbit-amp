#pragma once

#include "CabinetBlock.h"
#include "CapturedBlockPanel.h"
#include "PowerAmpBlock.h"
#include "ReverbBlock.h"

namespace orbitamp
{

class AmpProcessor;

/** The faceplate: the whole chain at once, five blocks in two rows.

    There used to be three ways to look at this — a map of thumbs across the top, an overview of the
    blocks, and a lens that threw one block across the panel. Three ways to show one signal path, and
    a map is only worth its room while something is hidden. Nothing is hidden here: the row is the
    order, and what a block is set to is written on its face.

    Row one is the pair that makes the sound — the captured pedal and the captured preamp, an even
    half each, because neither is a layer on the other. Row two is what happens to it afterwards:
    reverb and power amp at a quarter, the cabinet at a half, because the cabinet has a grille to
    draw and mics to place on it and the other two have a knob apiece. */
class FaceplateView : public juce::Component
{
public:
    explicit FaceplateView (AmpProcessor&);

    /** The loaded device changed — the captured blocks rebuild their faces from their packs. */
    void deviceChanged() { boost.deviceChanged(); preamp.deviceChanged(); }

    void resized() override;

    // The faceplate's design size, at 100%. The editor scales from these and never re-lays out.
    static constexpr int designWidth  = 880;

    // Row 1 carries a device combo, the in/out meters, the gain column and the whole EQ console
    // under them. Row 2 is half of it: a combo and a control or two.
    static constexpr int row1H  = 360;
    static constexpr int row2H  = 180;
    static constexpr int rowGap = 12;
    static constexpr int designHeight = row1H + rowGap + row2H + 32;   // + lanePadY top and bottom

private:
    // Lane metrics. No horizontal inset: the lane's edges ARE the panel's edges, so nothing is
    // almost-aligned with anything.
    static constexpr int lanePadY = 16;
    static constexpr int colGap   = 14;

    CapturedBlockPanel boost;
    CapturedBlockPanel preamp;
    ReverbBlock        reverb;
    PowerAmpBlock      power;
    CabinetBlock       cabinet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
