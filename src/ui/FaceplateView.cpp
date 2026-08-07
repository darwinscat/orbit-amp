#include "FaceplateView.h"

#include "../Parameters.h"

namespace orbitamp
{

FaceplateView::FaceplateView (juce::AudioProcessorValueTreeState& state)
    : boost (state), preamp (state), reverb (state)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &reverb })
        addAndMakeVisible (*b);

    // Every block binds its own power now.
}

void FaceplateView::resized()
{
    auto lane = getLocalBounds().reduced (lanePadX, lanePadY);

    inGutter = lane.removeFromLeft (gutter);
    lane.removeFromLeft (colGap);
    outGutter = lane.removeFromRight (gutter);
    lane.removeFromRight (colGap);

    // Row 2 — power amp under boost, cabinet under preamp + reverb — is not built yet, so the lane
    // is one row. The column split will be reused for it: same boundaries, half the height.
    auto row1 = lane.removeFromTop (row1H);

    // Three columns weighted 1 : phi : 1 — the preamp is the anchor.
    const float unit = (float) (row1.getWidth() - 2 * colGap) / (2.0f + phi);

    boost.setBounds (row1.removeFromLeft (juce::roundToInt (unit)));
    row1.removeFromLeft (colGap);
    preamp.setBounds (row1.removeFromLeft (juce::roundToInt (unit * phi)));
    row1.removeFromLeft (colGap);
    reverb.setBounds (row1);
}

void FaceplateView::paint (juce::Graphics& g)
{
    // Nothing wraps the row. The outlined slab that used to sit here — and the name plate inside it
    // — were one thing: a web mockup's picture OF a device, drawn as a window on a page, with its
    // own logo strip. The blocks keep their frames (that is the captured/DSP colour code, which is
    // load-bearing); what goes is the box around them and the second mark it carried.
    paintGutter (g, inGutter,  "In");
    paintGutter (g, outGutter, "Out");
}

void FaceplateView::paintGutter (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& label) const
{
    if (area.isEmpty())
        return;

    auto r = area.toFloat();
    auto lab = r.removeFromTop (12.0f);

    g.setColour (theme::txDim);
    theme::drawTracked (g, label.toUpperCase(), lab, theme::displayFont (8.0f), 0.14f, juce::Justification::centred);

    // The meter well. Empty for now — level metering lands with the engine, not with the frame.
    const auto bar = r.withSizeKeepingCentre (34.0f, r.getHeight() - 7.0f);
    g.setColour (theme::bezel);
    g.fillRoundedRectangle (bar, theme::radiusMd);
    g.setColour (theme::hair2);
    g.drawRoundedRectangle (bar.reduced (0.5f), theme::radiusMd, 1.0f);
}

} // namespace orbitamp
