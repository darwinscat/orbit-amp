#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : tuner (processor.tunerEar),
      gate (processor.apvts, processor.gateMeterDb, processor.gateKeyDb,
            [&processor] { return processor.demo.isPlaying(); }),
      eq1 (processor.apvts, 0, processor.eqOutDb[0], processor.eqSpectrumTap[0],
           [&processor] { return processor.currentSampleRate(); }),
      eq2 (processor.apvts, 1, processor.eqOutDb[1], processor.eqSpectrumTap[1],
           [&processor] { return processor.currentSampleRate(); }),
      boost (processor, processor.boost, "Boost", params::boostId),
      preamp (processor, processor.preamp, "Preamp", params::preampId),
      reverb (processor.apvts), power (processor.apvts), cabinet (processor.apvts)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &reverb, (BlockFrame*) &power, (BlockFrame*) &cabinet })
        addAndMakeVisible (*b);

    tuner.onDismiss = [this] { if (onTunerDismiss != nullptr) onTunerDismiss(); };

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
    auto lane = getLocalBounds().reduced (0, lanePadY);

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

} // namespace orbitamp
