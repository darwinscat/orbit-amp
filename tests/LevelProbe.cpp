// The level probe: how HOT does each captured block leave the signal? A -20 dBFS guitar-range
// sine goes in; the RMS after boost and after preamp comes out. If boost hands the preamp a
// signal tens of dB above pickup level, "sounds wrong together" stops being a mystery — the
// second model is being played in a range nobody captured.

#include "core/CapturedBlock.h"

#include <cmath>
#include <cstdio>

int main (int argc, char** argv)
{
    using namespace orbitamp;
    using Block = core::CapturedBlock<params::boostNumMeasured>;

    constexpr double rate = 48000.0;
    constexpr int    n    = 512;

    Block boost (device::DeviceLibrary::Slot::pedal);
    Block preamp (device::DeviceLibrary::Slot::preamp);

    boost.prepare (rate, n, 1);
    preamp.prepare (rate, n, 1);
    boost.rescan (0);
    preamp.rescan (0);

    // Find the two suspects by name.
    const auto pick = [] (Block& b, const juce::String& name)
    {
        for (int i = 0; i < b.packs.size(); ++i)
            if (b.packs[i].displayName().containsIgnoreCase (name))
            {
                b.select (i);
                return true;
            }
        return false;
    };

    if (! pick (boost, "Muff") || ! pick (preamp, "IR"))
    {
        std::printf ("packs not found\n");
        return 1;
    }

    const float bGain = argc > 1 ? juce::String (argv[1]).getFloatValue() : 5.0f;
    const float pGain = argc > 2 ? juce::String (argv[2]).getFloatValue() : 5.0f;
    boost.loadIfGainMoved (bGain);
    preamp.loadIfGainMoved (pGain);
    std::printf ("gains: boost %.1f  preamp %.1f\n", bGain, pGain);

    std::printf ("boost:  %s\npreamp: %s\n",
                 boost.deviceName().toRawUTF8(), preamp.deviceName().toRawUTF8());

    juce::AudioBuffer<float> buf (1, n), dry (1, n);

    const auto rmsDb  = [&] { return juce::Decibels::gainToDecibels (buf.getRMSLevel (0, 0, n), -120.0f); };
    const auto dc     = [&]
    {
        double m = 0.0;
        for (int i = 0; i < n; ++i)
            m += buf.getSample (0, i);
        return (float) (m / n);
    };
    const auto peakDb = [&] { return juce::Decibels::gainToDecibels (buf.getMagnitude (0, 0, n), -120.0f); };

    const auto sine = [&] (double phase0)
    {
        double ph = phase0;
        for (int i = 0; i < n; ++i)
        {
            buf.setSample (0, i, 0.1f * (float) std::sin (ph));   // -20 dBFS peak
            ph += juce::MathConstants<double>::twoPi * 110.0 / rate;
        }
        return ph;
    };

    // Settle both networks, then read one block's levels at each tap.
    double ph = 0.0;
    for (int i = 0; i < 100; ++i)
    {
        ph = sine (ph);
        boost.process (buf, dry);
        preamp.process (buf, dry);
    }

    ph = sine (ph);
    const float inDb = rmsDb(), inPk = peakDb();

    boost.process (buf, dry);
    const float postBoostDb = rmsDb(), boostPk = peakDb();
    const float boostDc = dc();

    preamp.process (buf, dry);
    const float postPreampDb = rmsDb(), preampPk = peakDb();
    const float preampDc = dc();

    std::printf ("\n-20 dBFS sine, 110 Hz:\n");
    std::printf ("in:          %6.1f dB RMS  %6.1f pk   DC %+7.4f\n", inDb, inPk, 0.0f);
    std::printf ("after boost: %6.1f dB RMS  %6.1f pk   DC %+7.4f  (%+.1f dB rms)\n",
                 postBoostDb, boostPk, boostDc, postBoostDb - inDb);
    std::printf ("after preamp:%6.1f dB RMS  %6.1f pk   DC %+7.4f\n",
                 postPreampDb, preampPk, preampDc);

    return 0;
}
