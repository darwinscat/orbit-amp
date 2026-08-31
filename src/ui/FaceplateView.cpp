#include "FaceplateView.h"

#include "../Parameters.h"
#include "../PluginProcessor.h"

namespace orbitamp
{

FaceplateView::FaceplateView (AmpProcessor& processor)
    : boost (processor, processor.boost, "Boost", params::boostId, 0,
             processor.blockSpectrumTap[0], processor.blockInSpectrumTap[0]),
      preamp (processor, processor.preamp, "Preamp", params::preampId, 1,
              processor.blockSpectrumTap[1], processor.blockInSpectrumTap[1]),
      reverb (processor.apvts), power (processor, processor.poweramp), cabinet (processor)
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

    // Row two is cut from the SAME halves, not from its own quarters.
    //
    // Dividing each row on its own arithmetic looks equivalent and is not: row one spends one gap
    // and row two spends two, so a quarter came out at 188 while a half came out at 383, and the
    // cabinet's left edge missed the preamp's by seven units. Seven is exactly the distance at
    // which an edge stops being aligned and starts being a mistake nobody can name. The panel has
    // ONE line down its middle now, and everything either sits on it or spans it.
    auto left = row2.removeFromLeft (half);
    row2.removeFromLeft (colGap);
    cabinet.setBounds (row2);

    // The power amp stands in the row only when asked for; without it the reverb takes the whole
    // left half, and the line down the panel's middle still holds.
    power.setVisible (powerShown);

    if (powerShown)
    {
        const int quarter = (left.getWidth() - colGap) / 2;
        reverb.setBounds (left.removeFromLeft (quarter));
        left.removeFromLeft (colGap);
        power.setBounds (left);
    }
    else
    {
        reverb.setBounds (left);
    }
}

} // namespace orbitamp
