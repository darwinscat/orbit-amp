#pragma once

#include "BlockFrame.h"
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

    // Row 1 carries a device combo, the in/out meters, the gain column with the picture beside it,
    // and the whole EQ console under them. The console is the part that will not compress: knobs at
    // reading size plus a curve big enough to aim at. Row 2 is a combo and a control or two.
    static constexpr int row1H  = 460;
    static constexpr int row2H  = 220;
    static constexpr int rowGap = 12;
    // Lane metrics. No horizontal inset: the lane's edges ARE the panel's edges, so nothing is
    // almost-aligned with anything.
    //
    // Four, not sixteen. The gap a player actually SEES above the first block is this plus the
    // toolbar's own ten plus the twelve a frame leaves above its line for the switch riding it —
    // three separate paddings that only ever get added together, and at sixteen they came to
    // thirty-eight units of nothing under the logo.
    static constexpr int lanePadY = 4;

    static constexpr int designHeight = row1H + rowGap + row2H + 2 * lanePadY;

    /** Where the blocks' drawn frames start and stop inside the faceplate. The in/out gutters
        standing either side line themselves up with THESE rather than with the component, so the
        four vertical edges across the panel are one line each. */
    static constexpr int contentTop    = lanePadY + BlockFrame::borderTopInset;
    static constexpr int contentBottom = lanePadY;

private:
    static constexpr int colGap = 14;

    CapturedBlockPanel boost;
    CapturedBlockPanel preamp;
    ReverbBlock        reverb;
    PowerAmpBlock      power;
    CabinetBlock       cabinet;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FaceplateView)
};

} // namespace orbitamp
