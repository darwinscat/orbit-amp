// Gate for the measured controls MAKING SOUND. A pedal's tone knobs arrive as magnitude tables rather
// than as models, so unlike the gain knob — which picks a captured file — they only exist if something
// builds a filter from them and runs it. For a long while nothing did: the curves were drawn on the
// face and never reached the audio. This is the check that says they do.
//
// The design itself is felitronics::lineareq's and tested there. What is checked here is ours: that a
// pack's table becomes a filter, that moving the knob moves the sound, that the reference position
// costs nothing, and that switching a control does not shift the audio in time.
//
// Skips cleanly when no pack is installed.

#include <juce_events/juce_events.h>

#include "core/CapturedStage.h"
#include "core/MeasuredFilter.h"

#include <cmath>
#include <cstdio>
#include <vector>

using orbitamp::core::MeasuredFilter;
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

    /** Level at one frequency, in dB, through the filter. A sine in, a Goertzel out, measured after
        the convolver's own latency has passed so the reading is steady state. */
    double responseDb (MeasuredFilter& f, double hz)
    {
        constexpr int n = 16384;
        juce::AudioBuffer<float> buf (2, blockSize);

        double re = 0.0, im = 0.0;
        int counted = 0;

        for (int off = 0; off < n; off += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double s = std::sin (2.0 * juce::MathConstants<double>::pi * hz * (off + i) / sampleRate);
                buf.setSample (0, i, (float) s);
                buf.setSample (1, i, (float) s);
            }

            f.process (buf);

            if (off < n / 2)          // let the convolver's partitions fill first
                continue;

            for (int i = 0; i < blockSize; ++i)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * hz * (off + i) / sampleRate;
                re += buf.getSample (0, i) * std::cos (w);
                im += buf.getSample (0, i) * std::sin (w);
                ++counted;
            }
        }

        const double mag = 2.0 * std::sqrt (re * re + im * im) / juce::jmax (1, counted);
        return 20.0 * std::log10 (juce::jmax (1.0e-9, mag));
    }

    /** Build the filter and let it arrive. juce::dsp::Convolution hands the kernel to its own
        background thread and swaps it in on a later block, so a measurement taken immediately reads
        the filter that is not there yet. In the plugin nobody notices — a few milliseconds after a
        knob moves — but a test that does not wait measures silence and calls it a failure. */
    void arm (MeasuredFilter& f, const namz::rig::Measured& m, double norm)
    {
        f.prepare (sampleRate, blockSize, 2);
        f.setPosition (m, norm);

        juce::AudioBuffer<float> quiet (2, blockSize);
        for (int i = 0; i < 40; ++i)
        {
            quiet.clear();
            f.process (quiet);
            juce::Thread::sleep (5);
        }
    }
}

int main()
{
    const juce::ScopedJuceInitialiser_GUI juceInit;
    std::printf ("orbitamp measured-filter gate\n\n");

    auto packs = DeviceLibrary::scan();

    if (packs.isEmpty())
    {
        std::printf ("no packs in %s\n", DeviceLibrary::directory().getFullPathName().toRawUTF8());
        std::printf ("nothing to check — install an .orbitrig to exercise this gate\n");
        return 0;
    }

    const auto& pack = packs.getReference (0);
    std::printf ("device: %s\n", juce::String (pack.name).toRawUTF8());

    orbitamp::core::CapturedStage stage;
    stage.prepare (sampleRate, blockSize);
    stage.setPack (&pack);

    const auto* controls = stage.measured();

    // The first control measured over a sweep — a switch's two positions are a different question.
    const namz::rig::Measured* sweep = nullptr;
    if (controls != nullptr)
        for (const auto& m : *controls)
            if (m.positions.size() > 2) { sweep = &m; break; }

    if (sweep == nullptr)
    {
        std::printf ("no swept measured control in this pack — nothing to check\n");
        return 0;
    }

    std::printf ("control: %s, %d positions, %d grid points %.0f..%.0f Hz\n\n",
                 juce::String (sweep->name).toRawUTF8(), (int) sweep->positions.size(),
                 sweep->grid.points, sweep->grid.fLo, sweep->grid.fHi);

    // What the pack SAYS the control does, at its extremes. The curves are the truth the filter is
    // measured against — if the pack itself is flat there is nothing for this gate to prove.
    const auto& first = sweep->positions.front().db;
    const auto& last  = sweep->positions.back().db;

    double packSpread = 0.0;
    for (size_t i = 0; i < juce::jmin (first.size(), last.size()); ++i)
        packSpread = juce::jmax (packSpread, std::abs (last[i] - first[i]));

    report ("the pack's own curves differ end to end", packSpread > 1.0,
            juce::String (packSpread, 1) + " dB");

    // The frequency where they differ most — where the control is doing its work, and so where the
    // filter has to show it.
    double workHz = 1000.0, worst = 0.0;
    const int pts = sweep->grid.points;
    for (int i = 0; i < pts && i < (int) juce::jmin (first.size(), last.size()); ++i)
    {
        const double d = std::abs (last[(size_t) i] - first[(size_t) i]);
        if (d > worst)
        {
            worst = d;
            workHz = sweep->grid.fLo * std::pow (sweep->grid.fHi / sweep->grid.fLo,
                                                 (double) i / (double) (pts - 1));
        }
    }

    // Away from the edges, where a convolution measurement is cleanest.
    workHz = juce::jlimit (80.0, 8000.0, workHz);

    std::printf ("trusted: levels %d, band %.0f..%.0f Hz\n\n",
                 sweep->trusted.levels, sweep->trusted.loHz, sweep->trusted.hiHz);

    MeasuredFilter down, up;
    arm (down, *sweep, 0.0);
    arm (up,   *sweep, 1.0);

    report ("the filter is built at all", down.isActive() && up.isActive());

    const double dbDown = responseDb (down, workHz);
    const double dbUp   = responseDb (up,   workHz);

    std::printf ("\nat %.0f Hz: knob min %.2f dB, knob max %.2f dB\n\n", workHz, dbDown, dbUp);

    report ("turning the knob changes the sound", std::abs (dbUp - dbDown) > 1.0,
            juce::String (std::abs (dbUp - dbDown), 2) + " dB apart");

    // The sound must move the way the pack says, not merely move. A filter wired backwards would pass
    // the check above and be wrong at every setting.
    {
        int idx = juce::jlimit (0, pts - 1,
                                (int) std::lround (std::log (workHz / sweep->grid.fLo)
                                                   / std::log (sweep->grid.fHi / sweep->grid.fLo)
                                                   * (pts - 1)));
        const double expected = last[(size_t) idx] - first[(size_t) idx];
        const double got = dbUp - dbDown;

        report ("...and by what the pack says, in the right direction",
                std::abs (got - expected) < 2.0,
                "pack " + juce::String (expected, 2) + " dB, filter " + juce::String (got, 2) + " dB");
    }

    // A reference position is flat by construction, and a convolution that does nothing is a waste of
    // a core. Nothing to apply must mean nothing applied.
    {
        MeasuredFilter flat;
        flat.prepare (sampleRate, blockSize, 2);

        namz::rig::Measured noop = *sweep;
        for (auto& p : noop.positions)
            p.db.assign (p.db.size(), 0.0);

        flat.setPosition (noop, 0.5);
        report ("a flat curve costs no convolution", ! flat.isActive());
    }

    // Minimum phase, and the reason for it: a tone knob must not move the audio in time. An impulse in,
    // and the energy has to arrive at the front rather than half a kernel later.
    {
        MeasuredFilter f;
        arm (f, *sweep, 1.0);

        juce::AudioBuffer<float> buf (2, blockSize);
        buf.clear();
        buf.setSample (0, 0, 1.0f);
        buf.setSample (1, 0, 1.0f);
        f.process (buf);

        int peak = 0;
        float best = 0.0f;
        for (int i = 0; i < blockSize; ++i)
            if (const float v = std::abs (buf.getSample (0, i)); v > best) { best = v; peak = i; }

        report ("the filter does not delay the audio", peak < 32,
                "peak at sample " + juce::String (peak));
    }

    // A pack that never stated a trusted band still must not be applied at the very edges of the
    // grid. SM7 states none, and both its EQ curves turn upwards at 20 Hz — the measurement's own
    // noise floor, four to seven decibels of it, drawn as a spike and played as an infrasonic boost.
    {
        namz::rig::Measured silent = *sweep;
        silent.trusted = {};

        const auto band = MeasuredFilter::bandFor (silent);
        report ("a pack that says nothing still gets a band",
                band.lo > 20.0 && band.hi < 20000.0,
                juce::String (band.lo, 0) + ".." + juce::String (band.hi, 0) + " Hz");

        MeasuredFilter f;
        arm (f, silent, 1.0);

        // Held flat below the band: whatever the grid claims at 20 Hz, what plays there is what the
        // curve says at the edge of where it was believed.
        const double atEdge = responseDb (f, band.lo);

        MeasuredFilter below;
        arm (below, silent, 1.0);
        const double under = responseDb (below, 25.0);

        report ("...and nothing new happens below it", std::abs (under - atEdge) < 1.5,
                juce::String (atEdge, 2) + " dB at the edge, " + juce::String (under, 2) + " under it");
    }

    // A control the pack says reproduced nowhere is a warning, and the widest possible permission is
    // the wrong way to read it.
    {
        namz::rig::Measured failed = *sweep;
        failed.trusted.levels = 3;
        failed.trusted.loHz = 2000.0;
        failed.trusted.hiHz = 1000.0;    // collapsed: tested and failed everywhere

        MeasuredFilter f;
        f.prepare (sampleRate, blockSize, 2);
        f.setPosition (failed, 1.0);

        report ("an untrusted curve is not applied", ! f.isActive());
    }

    std::printf ("\n%s\n", failures != 0 ? "FAILURES" : "all checks passed");
    return failures;
}
