// Gate for reading an .orbitrig pack and playing what is in it. Not a test of NAM inference —
// felitronics owns that — but of the part that is ours: finding the pack, reading the captured gain
// positions, choosing the file a knob position means, and actually making sound with it.
//
// Skips cleanly with a message when no pack is installed, so a machine without devices still builds
// and runs green rather than failing for the wrong reason.

#include <juce_events/juce_events.h>

#include "core/CapturedStage.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::CapturedStage;
using orbitamp::device::DeviceLibrary;

namespace
{
    int failures = 0;

    void report (const char* what, bool ok, const juce::String& detail = {})
    {
        if (! ok)
            ++failures;

        std::printf ("%-52s %s  %s\n", what, ok ? "ok" : "FAIL", detail.toRawUTF8());
    }

    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    /** RMS of a sine pushed through the stage. */
    double runRms (CapturedStage& stage, double amp)
    {
        constexpr int n = 12000;
        std::vector<float> left ((size_t) n), right ((size_t) n);

        for (int i = 0; i < n; ++i)
            left[(size_t) i] = right[(size_t) i]
                = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate));

        for (int off = 0; off < n; off += blockSize)
        {
            float* io[2] = { left.data() + off, right.data() + off };
            stage.process (io, 2, juce::jmin (blockSize, n - off));
        }

        double sum = 0.0;
        for (int i = n / 2; i < n; ++i)
            sum += (double) left[(size_t) i] * left[(size_t) i];

        return std::sqrt (sum / (n / 2));
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("orbitamp captured-stage gate\n\n");

    auto packs = DeviceLibrary::scan();

    if (packs.isEmpty())
    {
        std::printf ("no packs in %s\n", DeviceLibrary::directory().getFullPathName().toRawUTF8());
        std::printf ("nothing to check — install an .orbitrig to exercise this gate\n");
        return 0;
    }

    const auto& pack = packs.getReference (0);
    std::printf ("pack: %s  (%s)\n\n", pack.name.toRawUTF8(), pack.zipped ? "zip" : "folder");

    CapturedStage stage;
    stage.prepare (sampleRate, blockSize);
    stage.setPack (&pack);

    const auto positions = stage.gainPositions();
    report ("the pack lists captured gain positions", positions.size() > 1,
            juce::String (positions.size()) + " positions: " + positions.joinIntoString (" "));

    report ("nothing plays before a model is chosen", ! stage.isReady());

    stage.selectGainIndex (0);
    report ("the lowest capture loads", stage.isReady(), "file for gain " + positions[0]);

    const double quiet = runRms (stage, 0.05);
    report ("it makes sound", quiet > 1.0e-6, "rms " + juce::String (quiet, 6));

    // A captured gain knob picks a DIFFERENT MODEL, so the top of the sweep has to be audibly not
    // the bottom. Same input, two captures, compared.
    stage.selectGainIndex (positions.size() - 1);
    report ("the top capture loads too", stage.isReady(), "file for gain " + positions[positions.size() - 1]);

    const double loud = runRms (stage, 0.05);
    report ("top and bottom captures are different devices",
            loud > quiet * 1.2 || quiet > loud * 1.2,
            "rms " + juce::String (quiet, 5) + " vs " + juce::String (loud, 5));

    // Every position must resolve to a file. A gap would be a knob detent that silently keeps the
    // previous sound — the worst kind of wrong, because nothing looks broken.
    {
        bool allLoad = true;
        for (int i = 0; i < positions.size(); ++i)
        {
            CapturedStage one;
            one.prepare (sampleRate, blockSize);
            one.setPack (&pack);
            one.selectGainIndex (i);
            allLoad = allLoad && one.isReady();
        }

        report ("every captured position resolves to a model", allLoad,
                juce::String (positions.size()) + " checked");
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
