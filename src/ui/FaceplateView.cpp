#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : tuner (processor.tunerEar),
      gate (processor.apvts, processor.gateMeterDb, processor.gateKeyDb,
            [&processor] { return processor.demo.isPlaying(); }),
      eq1 (processor.apvts, 0), eq2 (processor.apvts, 1),
      boost (processor, processor.boost, "Boost", params::boostId),
      preamp (processor, processor.preamp, "Preamp", params::preampId),
      reverb (processor.apvts), power (processor.apvts), cabinet (processor.apvts)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &reverb, (BlockFrame*) &power, (BlockFrame*) &cabinet })
        addAndMakeVisible (*b);

    // The service links and the EQ links live in the zoom: on the overview their thumbs in the
    // strip say everything a miniature can, and opening one is a single click.
    addChildComponent (tuner);
    addChildComponent (gate);
    addChildComponent (eq1);
    addChildComponent (eq2);

    // Every block binds its own power now.
}

std::array<juce::Component*, (size_t) numChainLinks> FaceplateView::linkFaces()
{
    return { &tuner, &gate, &eq1, &boost, &eq2, &preamp, &reverb, &power, &cabinet };
}

void FaceplateView::setZoom (std::optional<ChainLink> link)
{
    if (zoomed == link)
        return;

    zoomed = link;

    // Whatever is about to be looked at re-reads its power parameter — insurance against any
    // widget that went stale while nobody was watching.
    for (auto* f : linkFaces())
        if (auto* b = dynamic_cast<BlockFrame*> (f))
            b->resyncPower();

    resized();
    repaint();
}

void FaceplateView::resized()
{
    auto lane = getLocalBounds().reduced (lanePadX, lanePadY);

    inGutter = lane.removeFromLeft (gutter);
    lane.removeFromLeft (colGap);
    outGutter = lane.removeFromRight (gutter);
    lane.removeFromRight (colGap);

    auto faces = linkFaces();

    // Zoomed: the open link takes the whole lane and everything else steps aside. Nothing is
    // re-laid-out per zoom level — the block's own layout answers to whatever bounds it gets.
    if (zoomed.has_value())
    {
        auto* open = faces[(size_t) *zoomed];

        for (auto* f : faces)
            f->setVisible (f == open);

        open->setBounds (lane);
        return;
    }

    for (auto* f : faces)
        f->setVisible (f != &tuner && f != &gate && f != &eq1 && f != &eq2);

    auto row1 = lane.removeFromTop (row1H);
    lane.removeFromTop (rowGap);
    auto row2 = lane.removeFromTop (row2H);

    // Three columns weighted 1 : phi : 1 — the preamp is the anchor.
    const float unit = (float) (row1.getWidth() - 2 * colGap) / (2.0f + phi);
    const int   col1 = juce::roundToInt (unit);

    boost.setBounds (row1.removeFromLeft (col1));
    row1.removeFromLeft (colGap);
    preamp.setBounds (row1.removeFromLeft (juce::roundToInt (unit * phi)));
    row1.removeFromLeft (colGap);
    reverb.setBounds (row1);

    // Row 2 reuses row 1's boundary: the power amp sits under the boost, and the cabinet will take
    // the rest — under the preamp and the reverb together.
    power.setBounds (row2.removeFromLeft (col1));
    row2.removeFromLeft (colGap);
    cabinet.setBounds (row2);
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
