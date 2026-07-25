#include "FaceplateView.h"

#include "../Parameters.h"

namespace orbitamp
{

FaceplateView::FaceplateView (juce::AudioProcessorValueTreeState& state)
    : preamp (state), reverb (state), eq (state)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &reverb, (BlockFrame*) &eq })
        addAndMakeVisible (*b);

    // Every block but boost binds its own power; boost is still a bare frame.
    boost.attachPower (*state.getParameter (params::boostOn));
}

void FaceplateView::resized()
{
    auto body = getLocalBounds().reduced (pad);
    body.removeFromTop (namePlateH);

    auto lane = body.reduced (lanePadX, lanePadY);

    inGutter = lane.removeFromLeft (gutter);
    lane.removeFromLeft (colGap);
    outGutter = lane.removeFromRight (gutter);
    lane.removeFromRight (colGap);

    auto row1 = lane.removeFromTop (row1H);
    lane.removeFromTop (rowGap);
    eq.setBounds (lane.removeFromTop (row2H));

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
    const auto shell = getLocalBounds().toFloat().reduced (0.5f);

    // The device shell: a top-lit slab, darkening through 42% down — the spec's gradient.
    juce::ColourGradient body (theme::deviceTop, 0.0f, shell.getY(),
                               theme::deviceBot, 0.0f, shell.getBottom(), false);
    body.addColour (0.42, theme::deviceMid);
    g.setGradientFill (body);
    g.fillRoundedRectangle (shell, theme::radiusXl);

    g.setColour (theme::hair2);
    g.drawRoundedRectangle (shell, theme::radiusXl, 1.0f);

    paintNamePlate (g, getLocalBounds().reduced (pad).removeFromTop (namePlateH));
    paintGutter (g, inGutter,  "In");
    paintGutter (g, outGutter, "Out");
}

void FaceplateView::paintNamePlate (juce::Graphics& g, juce::Rectangle<int> area) const
{
    auto r = area.toFloat().withTrimmedBottom (16.0f);

    // The orbit mark is the family's, drawn from appkit — never redrawn per product.
    const float markSize = 44.0f;
    felitronics::appkit::brand::drawOrbitRings (g, r.getX() + markSize * 0.5f, r.getCentreY(), markSize);

    auto text = r.withTrimmedLeft (markSize + 15.0f);
    const auto font = theme::displayFont (26.0f);

    // "ORBIT" in the face colour, "AMP" in the accent — the mark and the name share one baseline.
    const float wOrbit = theme::trackedWidth ("ORBIT", font, 0.06f);
    g.setColour (theme::tx);
    theme::drawTracked (g, "ORBIT", text, font, 0.06f, juce::Justification::centredLeft);
    g.setColour (theme::violet);
    theme::drawTracked (g, "AMP", text.withTrimmedLeft (wOrbit + 0.06f * font.getHeight()),
                        font, 0.06f, juce::Justification::centredLeft);

    g.setColour (theme::hair);
    g.fillRect (area.toFloat().removeFromBottom (1.0f));
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
