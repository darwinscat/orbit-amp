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
#include <functional>
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

    /** The stage's output for a sine WITH a model change dropped into the middle of it, kept whole —
        the switch is the thing being looked at, so nothing may be trimmed away. `pump` stands in for
        the processor's thirty-a-second message-thread tick. */
    std::vector<float> runAcrossSwitch (CapturedStage& stage, double amp, int switchAtBlock,
                                        const std::function<void()>& pump)
    {
        constexpr int n = 24000;   // half a second at 48k — a warm, a fade and a tail after it
        std::vector<float> left ((size_t) n), right ((size_t) n);

        for (int i = 0; i < n; ++i)
            left[(size_t) i] = right[(size_t) i]
                = (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sampleRate));

        for (int off = 0, block = 0; off < n; off += blockSize, ++block)
        {
            if (block == switchAtBlock)
                pump();

            float* io[2] = { left.data() + off, right.data() + off };
            stage.process (io, 2, juce::jmin (blockSize, n - off));

            stage.collectGarbage();   // the pump also lands finished handovers and parked requests
        }

        return left;
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

    /** The output's level hop by hop — a short-window RMS every 2.7 ms. */
    std::vector<double> envelope (const std::vector<float>& x, size_t from, size_t to)
    {
        constexpr size_t window = 512, hop = 128;   // ~11 ms of window, two cycles of the 220 Hz probe
        std::vector<double> out;

        for (size_t at = from; at + window <= std::min (to, x.size()); at += hop)
            out.push_back (rms (std::vector<float> (x.begin() + (long) at,
                                                    x.begin() + (long) (at + window))));

        return out;
    }

    /** The most the level moves between two neighbouring hops, as a ratio.

        This is the question a model change has to answer, and the reason the two obvious ones are
        wrong. Sample-to-sample jumps barely notice a swap: the top of the SM7's dial is a squared-off
        wave whose own steepest edge dwarfs any seam. Nor does the floor — a NAM dropped in with empty
        buffers still makes a plausible level almost at once, so nothing VANISHES either. What the ear
        actually catches is the SPEED: the level of the whole thing arriving somewhere else within one
        block instead of walking there. A crossfade cannot move faster than its own ramp; a swap moves
        as fast as the two captures differ. */
    double steepestLevelMove (const std::vector<float>& x, size_t from, size_t to)
    {
        const auto env = envelope (x, from, to);
        double worst = 1.0;

        for (size_t i = 1; i < env.size(); ++i)
        {
            const double a = std::max (1.0e-9, env[i - 1]), b = std::max (1.0e-9, env[i]);
            worst = std::max (worst, std::max (a / b, b / a));
        }

        return worst;
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

    // THE HANDOVER. Turning the dial one notch is loading a whole other model, and done bare that is
    // a step: the transfer changes between two samples, the newcomer's buffers are empty, and the
    // loudness makeup jumps. The stage carries a second model for exactly this — warm it silently,
    // fade it in — so the proof is that a switch mid-note leaves the waveform no rougher than the
    // two captures are on their own. Levels and spectra both look fine through a click; the first
    // difference does not.
    {
        CapturedStage swapping;
        swapping.prepare (sampleRate, blockSize, 2);
        swapping.setPack (&pack);
        swapping.selectGainIndex (0);

        constexpr int switchAtBlock = 20;                       // ~213 ms in — well past any settling
        const size_t  switchAt = (size_t) (switchAtBlock * blockSize);

        const int top = positions.size() - 1;
        const auto crossed = runAcrossSwitch (swapping, 0.05, switchAtBlock,
                                              [&swapping, top] { swapping.selectGainIndex (top); });

        // Before, during, after. "During" is the warm plus the fade plus room to spare — and it has
        // the whole distance between two very different captures to cover, 0 to 300 on the dial.
        const double before = steepestLevelMove (crossed, 4000,            switchAt);
        const double during = steepestLevelMove (crossed, switchAt,        switchAt + 6000);
        const double after  = steepestLevelMove (crossed, switchAt + 8000, crossed.size());

        // How far apart the two captures are in level — the distance the handover has to cover. It
        // is the pack's business, not this gate's: SM7 spans eleven decibels across its dial and the
        // next device will span something else, so the limit is derived from it rather than typed in.
        const double levelBefore = rms ({ crossed.begin() + 4000, crossed.begin() + (long) switchAt });
        const double levelAfter  = rms ({ crossed.end() - 6000, crossed.end() });
        const double span = std::max (levelBefore, levelAfter)
                          / std::max (1.0e-9, std::min (levelBefore, levelAfter));

        // Forty milliseconds of fade is fifteen hops, so no single hop may carry more than about a
        // tenth of the distance — and never less than what the steady waveform already wobbles by,
        // or a device whose dial barely changes level would be gated on measurement noise.
        const double allowed = std::max (1.0 + (span - 1.0) * 0.1, std::max (before, after) * 1.10);

        std::printf ("\nhandover: steepest level move  before %.3fx  during %.3fx  after %.3fx"
                     "   (span %.2fx, allowed %.3fx)\n", before, during, after, span, allowed);

        report ("a model change walks in, never lands", during < allowed,
                juce::String (during, 3) + "x per hop against " + juce::String (allowed, 3) + "x");

        // ...and it has to actually be the other capture on the far side. A handover that never
        // happened is perfectly smooth, and perfectly useless.
        report ("...and the far side is the capture it was sent to",
                shapeDifference (std::vector<float> (crossed.begin() + 4000, crossed.begin() + 10000),
                                 std::vector<float> (crossed.end() - 6000, crossed.end())) > 5.0,
                positions[0] + " -> " + positions[top]);
    }

    // ALIASES. A combination with no capture of its own points at a neighbour and says how much
    // softer to feed it — the bottom notches of a gain dial, where the hardware gives hiss and little
    // else, played through the model above them with less going in. The player has to apply that
    // attenuation, or those positions come out as loud and as dirty as the capture they borrowed.
    //
    // No pack here has one, so it is built by hand — and built as the SAME file twice, once plain and
    // once softened, rather than by pointing one dial position at another's model. Borrowing across
    // positions compares two different captures as much as it compares the attenuation, and on a
    // device with more than one selecting control it may not even land on the pair it meant to.
    {
        auto aliased = pack;
        auto* stageDef = const_cast<namz::rig::Stage*> (aliased.rig.firstKnown());

        if (stageDef == nullptr || stageDef->device.files.empty())
        {
            std::printf ("\nno device to alias — skipping\n");
        }
        else
        {
            // Every file, because which one a dial position resolves to is namz's business and not
            // this gate's — marking only the first meant marking one that never played.
            const auto borrowed = stageDef->device.files.front().id;
            for (auto& f : stageDef->device.files)
                f.inputDb = 12.0;

            CapturedStage soft, loud;
            soft.prepare (sampleRate, blockSize);
            loud.prepare (sampleRate, blockSize);

            soft.setPack (&aliased);
            soft.selectGainIndex (0);

            loud.setPack (&pack);
            loud.selectGainIndex (0);

            const auto softOut = run (soft, 0.05);
            const auto loudOut = run (loud, 0.05);
            const double softRms = rms (softOut), loudRms = rms (loudOut);

            std::printf ("\nalias: %s fed 12 dB softer -> rms %.5f; the same file plain -> %.5f\n",
                         juce::String (borrowed).toRawUTF8(), softRms, loudRms);
            std::printf ("shape differs by %.1f%%\n", shapeDifference (softOut, loudOut));

            report ("an alias plays the model it borrowed", softRms > 1.0e-6);

            // Not "quieter by twelve decibels": twelve decibels less going into a distortion comes
            // back a fraction of that, because squashing it is what the circuit is for. What the
            // attenuation must do is ARRIVE, and arriving shows differently by device: hit a
            // nonlinearity less hard and a different SHAPE comes out; feed a capture that is still
            // clean at this position and the shape barely moves but the LEVEL follows the input
            // down. Either is proof; an alias that ignored its inputDb would show neither. And it
            // must not come out louder.
            const bool arrived = shapeDifference (softOut, loudOut) > 2.0
                              || softRms < loudRms * 0.5;   // at least half the 12 dB asked
            report ("...with the input attenuation the pack asked for",
                    arrived && softRms <= loudRms * 1.02,
                    juce::String (shapeDifference (softOut, loudOut), 1) + "% different, "
                        + juce::String (20.0 * std::log10 (juce::jmax (1.0e-9, softRms / loudRms)), 1)
                        + " dB");
        }
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
