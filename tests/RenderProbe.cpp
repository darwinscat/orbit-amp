// The listening probe: the demo loop through Muff alone, IR-X alone, and both — three wavs on
// the desktop. If "wrong together" survives OFFLINE, the bug lives in the blocks; if these
// render clean, it lives in the plugin's wiring. Ears decide.

#include "core/CapturedBlock.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdio>

int main (int argc, char** argv)
{
    using namespace orbitamp;
    using Block = core::CapturedBlock<params::boostNumMeasured>;

    constexpr double rate = 48000.0;
    constexpr int    n    = 512;

    const float bGain = argc > 1 ? juce::String (argv[1]).getFloatValue() : 9.5f;
    const float pGain = argc > 2 ? juce::String (argv[2]).getFloatValue() : 7.0f;
    const juce::String boostName  = argc > 3 ? argv[3] : "Muff";
    const juce::String preampName = argc > 4 ? argv[4] : "IR";

    // The loop, straight from the sibling's assets — the same file the demo strip plays.
    juce::File loopFile ("~/IdeaProjects/orbit-nam-capture/app/assets/loops/eleven-light-years.wav");

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (loopFile));

    if (reader == nullptr)
    {
        std::printf ("no loop at %s\n", loopFile.getFullPathName().toRawUTF8());
        return 1;
    }

    juce::AudioBuffer<float> loop (1, (int) reader->lengthInSamples);
    reader->read (&loop, 0, (int) reader->lengthInSamples, 0, true, false);

    const auto render = [&] (bool useMuff, bool useIrx, const juce::String& name)
    {
        Block boost (device::DeviceLibrary::Slot::pedal);
        Block preamp (device::DeviceLibrary::Slot::preamp);
        boost.prepare (rate, n, 1);
        preamp.prepare (rate, n, 1);
        boost.rescan (0);
        preamp.rescan (0);

        // Matches the display name OR the pack file's name — three ReVolts share a face and
        // only the file tells them apart.
        const auto pick = [] (Block& b, const juce::String& want)
        {
            for (int i = 0; i < b.packs.size(); ++i)
                if (b.packs[i].displayName().containsIgnoreCase (want)
                    || b.packs[i].location.getFileName().containsIgnoreCase (want))
                {
                    b.select (i);
                    return;
                }
        };

        pick (boost, boostName);
        pick (preamp, preampName);
        boost.loadIfGainMoved (bGain);
        preamp.loadIfGainMoved (pGain);

        juce::AudioBuffer<float> out (1, loop.getNumSamples()), block (1, n), dry (1, n);

        for (int pos = 0; pos + n <= loop.getNumSamples(); pos += n)
        {
            block.copyFrom (0, 0, loop, 0, pos, n);

            if (useMuff) boost.process (block, dry);
            if (useIrx)  preamp.process (block, dry);

            out.copyFrom (0, pos, block, 0, 0, n);
        }

        const juce::File f = juce::File ("~/Desktop").getChildFile (name);
        f.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (
            wav.createWriterFor (new juce::FileOutputStream (f), rate, 1, 24, {}, 0));
        w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        w.reset();

        std::printf ("%-28s peak %6.1f dB  rms %6.1f dB\n", name.toRawUTF8(),
                     juce::Decibels::gainToDecibels (out.getMagnitude (0, 0, out.getNumSamples()), -120.0f),
                     juce::Decibels::gainToDecibels (out.getRMSLevel (0, 0, out.getNumSamples()), -120.0f));
    };

    std::printf ("gains: %s %.1f, %s %.1f\n", boostName.toRawUTF8(), bGain,
                 preampName.toRawUTF8(), pGain);
    render (true, false, "probe-1-" + boostName.toLowerCase() + "-only.wav");
    render (false, true, "probe-2-" + preampName.toLowerCase() + "-only.wav");
    render (true, true,  "probe-3-" + boostName.toLowerCase() + "-plus-"
                             + preampName.toLowerCase() + ".wav");

    return 0;
}
