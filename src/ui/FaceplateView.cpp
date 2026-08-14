#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : boost (processor, processor.boost, "Boost", params::boostId, 0, processor.blockSpectrumTap[0]),
      preamp (processor, processor.preamp, "Preamp", params::preampId, 1, processor.blockSpectrumTap[1]),
      reverb (processor.apvts), power (processor.apvts), cabinet (processor.apvts)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &reverb,
                     (BlockFrame*) &power, (BlockFrame*) &cabinet })
        addAndMakeVisible (*b);

    // Every block binds its own power.
}

void FaceplateView::resized()
{
    auto lane = getLocalBounds().reduced (0, lanePadY);

    auto row1 = lane.removeFromTop (row1H);
    lane.removeFromTop (rowGap);
    auto row2 = lane.removeFromTop (row2H);

    // The pair that makes the sound, an even half each.
    const int half = (row1.getWidth() - colGap) / 2;
    boost.setBounds (row1.removeFromLeft (half));
    row1.removeFromLeft (colGap);
    preamp.setBounds (row1);

    // ...and what happens to it: a quarter, a quarter, a half.
    const int quarter = (row2.getWidth() - 2 * colGap) / 4;
    reverb.setBounds (row2.removeFromLeft (quarter));
    row2.removeFromLeft (colGap);
    power.setBounds (row2.removeFromLeft (quarter));
    row2.removeFromLeft (colGap);
    cabinet.setBounds (row2);
}

} // namespace orbitamp
