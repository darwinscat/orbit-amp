// Gate for reading an .orbitrig pack and playing what is in it. Not a test of NAM inference —
// felitronics owns that — but of the part that is ours: finding the pack, reading the captured gain
// positions, choosing the file a knob position means, and actually making sound with it.
//
// Skips cleanly with a message when no pack is installed, so a machine without devices still builds
// and runs green rather than failing for the wrong reason.

#include <juce_events/juce_events.h>

#include "core/CapturedStage.h"

#include <algorithm>
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

    /** The stage's output for a sine, kept — see shapeDifference for why the samples matter and the
        level does not. */
    std::vector<float> run (CapturedStage& stage, double amp)
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

        return std::vector<float> (left.begin() + n / 2, left.end());
    }

    double rms (const std::vector<float>& x)
    {
        double sum = 0.0;
        for (const float s : x)
            sum += (double) s * s;

        return std::sqrt (sum / (double) std::max<size_t> (1, x.size()));
    }

    /** How different two outputs are once their levels are matched, as a percentage of signal.

        Level is the wrong question for a distortion, and this gate had been asking it. A pedal turned
        up is not louder past a point — it is a different shape. SM7 happened to gain 11 dB across its
        sweep and passed; Big Muff Pi moves 1.6 dB and would have been called broken, while its
        waveform changes out of all recognition. */
    double shapeDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        const double ra = rms (a), rb = rms (b);
        if (ra < 1.0e-9 || rb < 1.0e-9 || a.size() != b.size())
            return 0.0;

        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
        {
            const double d = a[i] / ra - b[i] / rb;
            diff += d * d;
        }

        return 100.0 * std::sqrt (diff / (double) a.size());
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

    // The list the combo shows: green to red by how much gain, the user's own after anything shipped.
    {
        std::printf ("\ndevices found\n");
        for (const auto& p : packs)
            std::printf ("  %-24s character %d  %s%s%s\n", p.displayName().toRawUTF8(), p.character,
                         p.bundled ? "bundled" : "user", p.loose ? ", loose model" : "",
                         p.alias.isNotEmpty() ? "  (alias)" : "");

        bool ordered = true;
        for (int i = 1; i < packs.size(); ++i)
        {
            const auto& a = packs.getReference (i - 1);
            const auto& b = packs.getReference (i);

            if (a.bundled != b.bundled)      { ordered = ordered && a.bundled; continue; }
            ordered = ordered && a.character <= b.character;
        }

        report ("the list runs green to red, shipped before added", ordered);
        report ("every device has a name to show", [&packs]
        {
            for (const auto& p : packs)
                if (p.displayName().isEmpty())
                    return false;
            return true;
        }());

        // An alias is what the pack goes out under; the model name is only the fallback.
        report ("an alias, where there is one, is what shows", [&packs]
        {
            for (const auto& p : packs)
                if (p.alias.isNotEmpty() && p.displayName() != p.alias)
                    return false;
            return true;
        }());
    }

    std::printf ("\n");
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

    const auto quiet = run (stage, 0.05);
    report ("it makes sound", rms (quiet) > 1.0e-6, "rms " + juce::String (rms (quiet), 6));

    // A captured gain knob picks a DIFFERENT MODEL, so the top of the sweep has to be audibly not
    // the bottom. Same input, two captures, compared.
    stage.selectGainIndex (positions.size() - 1);
    report ("the top capture loads too", stage.isReady(), "file for gain " + positions[positions.size() - 1]);

    const auto loud = run (stage, 0.05);
    report ("top and bottom captures are different devices",
            shapeDifference (quiet, loud) > 5.0,
            juce::String (shapeDifference (quiet, loud), 1) + "% different");

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

    // The whole sweep, the way the plugin maps it: knob 0..10 -> nearest captured position. If the
    // sound does not move across this, the knob is decoration.
    {
        std::printf ("\nknob -> position -> rms\n");
        std::vector<float> first;
        double worst = 0.0;

        for (int step = 0; step <= 10; ++step)
        {
            const float knob = (float) step;
            const int index = juce::jlimit (0, positions.size() - 1,
                                            juce::roundToInt (knob * 0.1f * (float) (positions.size() - 1)));

            CapturedStage one;
            one.prepare (sampleRate, blockSize);
            one.setPack (&pack);
            one.selectGainIndex (index);

            const auto out = run (one, 0.05);

            if (first.empty())
                first = out;
            else
                worst = juce::jmax (worst, shapeDifference (first, out));

            std::printf ("%4.0f   %6s   %.5f\n", knob, positions[index].toRawUTF8(), rms (out));
        }

        report ("the sweep actually changes the sound", worst > 10.0,
                juce::String (worst, 1) + "% away from the bottom of the dial");
    }

    // ALIASES. A combination with no capture of its own points at a neighbour and says how much
    // softer to feed it — the bottom notches of a gain dial, where the hardware gives hiss and little
    // else, played through the model above them with less going in. The player has to apply that
    // attenuation, or those positions come out louder and dirtier than the capture intended.
    //
    // No pack here has one (SM7 is twenty-one files for twenty-one positions), so the pack is bent by
    // hand: point its lowest position at the model two above it, softened.
    {
        auto aliased = pack;
        auto* stageDef = const_cast<namz::rig::Stage*> (aliased.rig.firstKnown());

        if (stageDef == nullptr || stageDef->device.files.size() < 3)
        {
            std::printf ("\nno device to alias — skipping\n");
        }
        else
        {
            const auto lowest = stageDef->device.files.front().settings;
            const auto borrowed = stageDef->device.files[2].id;

            stageDef->device.files.front().id = borrowed;
            stageDef->device.files.front().inputDb = 12.0;   // a big, unmistakable step down

            CapturedStage soft, loud;
            soft.prepare (sampleRate, blockSize);
            loud.prepare (sampleRate, blockSize);

            soft.setPack (&aliased);
            soft.selectGainIndex (0);          // the alias: model #2, fed 12 dB softer

            loud.setPack (&pack);
            loud.selectGainIndex (2);          // the same model, fed as it is

            const auto softOut = run (soft, 0.05);
            const auto loudOut = run (loud, 0.05);
            const double softRms = rms (softOut), loudRms = rms (loudOut);

            std::printf ("\nalias: %s at -12 dB in -> rms %.5f; the same model straight -> %.5f\n",
                         juce::String (borrowed).toRawUTF8(), softRms, loudRms);
            std::printf ("shape differs by %.1f%%\n", shapeDifference (softOut, loudOut));

            report ("an alias plays its neighbour's model", softRms > 1.0e-6);

            // Not "quieter by twelve decibels": twelve decibels less going IN to a fuzz comes out
            // barely two decibels down, because squashing that is the whole point of the circuit.
            // What the attenuation must do is arrive — the model is hit less hard, so what comes out
            // is a different shape — and it must not come out louder.
            report ("...with the input attenuation the pack asked for",
                    shapeDifference (softOut, loudOut) > 5.0 && softRms <= loudRms * 1.02,
                    juce::String (shapeDifference (softOut, loudOut), 1) + "% different, "
                        + juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, softRms / loudRms)), 1)
                        + " dB");

            juce::ignoreUnused (lowest);
        }
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
