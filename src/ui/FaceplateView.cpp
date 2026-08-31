// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

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
      delay (processor), reverb (processor), power (processor, processor.poweramp),
      cabinet (processor)
{
    for (auto* b : { (BlockFrame*) &boost, (BlockFrame*) &preamp, (BlockFrame*) &delay,
                     (BlockFrame*) &reverb, (BlockFrame*) &power, (BlockFrame*) &cabinet })
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

    // The optional blocks stand in the row only when asked for, and the row keeps the chain's
    // order: delay, reverb, power amp, cabinet. The line down the panel's middle still holds —
    // the delay shares the LEFT half with the reverb, the power amp cuts its quarter from the
    // cabinet's half, so with everything on the row is four even quarters.
    delay.setVisible (delayShown);
    power.setVisible (powerShown);

    const int quarter = (left.getWidth() - colGap) / 2;

    if (delayShown)
    {
        delay.setBounds (left.removeFromLeft (quarter));
        left.removeFromLeft (colGap);
        reverb.setBounds (left);

        if (powerShown)
        {
            power.setBounds (row2.removeFromLeft (quarter));
            row2.removeFromLeft (colGap);
        }
    }
    else if (powerShown)
    {
        reverb.setBounds (left.removeFromLeft (quarter));
        left.removeFromLeft (colGap);
        power.setBounds (left);
    }
    else
    {
        reverb.setBounds (left);
    }

    cabinet.setBounds (row2);
}

} // namespace orbitamp
