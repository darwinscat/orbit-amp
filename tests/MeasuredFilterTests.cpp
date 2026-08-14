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

    /** What the filter ACTUALLY does at a knob position, as the worst departure from unity in dB.

        The pack's curve being flat and the played result being flat are two different claims: the
        first belongs to whoever measured, the second to us, and the interpolation, the band hold and
        the FIR design all sit in between. A neutral that is flat on paper and audible in the room is
        our bug, not theirs, and only this half of the gate would ever find it.

        Sampled inside 80..8000 Hz, where a convolution measurement is cleanest — the same window the
        checks below use, and for the same reason. */
    double playedFlatnessDb (const namz::rig::Measured& m, double norm)
    {
        MeasuredFilter f;
        arm (f, m, norm);

        if (! f.isActive())
            return 0.0;          // nothing applied is exactly flat, and costs nothing to be so

        const auto band = MeasuredFilter::bandFor (m);
        const double lo = juce::jmax (80.0, band.lo);
        const double hi = juce::jmin (8000.0, band.hi);

        if (hi <= lo)
            return 0.0;          // the trusted band does not reach the window we can measure in

        constexpr int points = 8;
        double worst = 0.0;

        for (int i = 0; i < points; ++i)
            worst = juce::jmax (worst, std::abs (responseDb (f, lo * std::pow (hi / lo, (double) i / (points - 1)))));

        return worst;
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

    // ---- THE NEUTRAL, across every installed pack ------------------------------------------------
    //
    // A pack states its curves AGAINST its reference position — the setting every model of the stage
    // was captured at (`Measured::reference`). At that position the curve is therefore zero by
    // definition, and anything applied there colours a capture that already carries the colour.
    //
    // Getting those zeros wrong is what once made the measured controls sound bad enough to switch
    // them off wholesale, and it is the kind of mistake no amount of DSP catches: the curve stays
    // smooth, stays plausible, and is wrong at every setting of the knob. It is a data fault, so it
    // is caught here, on the data, for every control of every pack rather than for the one this file
    // happens to exercise below.
    //
    // The tolerance is half a decibel. A correct pack lands on exactly zero — SM7 does — so this is
    // not a budget to spend, it is room for a producer that rounds.
    {
        constexpr double neutralTolDb = 0.5;

        int checked = 0;
        double worst = 0.0;
        juce::String worstWhere;

        for (const auto& p : packs)
        {
            orbitamp::core::CapturedStage probe;
            probe.prepare (sampleRate, blockSize);
            probe.setPack (&p);

            const auto* list = probe.measured();
            if (list == nullptr)
                continue;

            for (const auto& m : *list)
            {
                const auto where = juce::String (p.name) + " / " + juce::String (m.name);

                // Ascending norms are what every reader indexes and lerps against, this one included.
                // A pack that ships them descending — "300, 240, … 0" is a normal way to write a knob
                // that reads ten to zero — inverts the control silently and looks fine doing it.
                bool ascends = true;
                for (size_t i = 1; i < m.positions.size(); ++i)
                    ascends = ascends && m.positions[i].norm >= m.positions[i - 1].norm;

                report (("norms ascend: " + where).toRawUTF8(), ascends);

                // A reference that names no position leaves the zero unknowable — the curves are
                // stated against something the pack does not contain.
                const namz::rig::MeasuredPosition* ref = nullptr;
                for (const auto& pos : m.positions)
                    if (juce::String (pos.value).trim() == juce::String (m.reference).trim())
                    {
                        ref = &pos;
                        break;
                    }

                if (ref == nullptr)
                {
                    report (("reference names a position: " + where).toRawUTF8(), false,
                            m.reference.empty() ? "none stated"
                                                : "no position \"" + juce::String (m.reference) + "\"");
                    continue;
                }

                double peak = 0.0;
                bool   finite = true;

                for (const double v : ref->db)
                {
                    finite = finite && std::isfinite (v);
                    peak = juce::jmax (peak, std::abs (v));
                }

                ++checked;
                if (peak > worst) { worst = peak; worstWhere = where; }

                report (("the neutral is flat: " + where).toRawUTF8(),
                        finite && peak <= neutralTolDb,
                        "at " + juce::String (m.reference) + ": " + juce::String (peak, 3) + " dB"
                            + (finite ? "" : " (NOT FINITE)"));

                // ...and flat once it has been through our own machinery, which is the other half of
                // the promise and the half we are answerable for.
                if (finite)
                {
                    const double played = playedFlatnessDb (m, ref->norm);
                    report (("...and stays flat when played: " + where).toRawUTF8(),
                            played <= neutralTolDb, juce::String (played, 3) + " dB");
                }
            }
        }

        std::printf ("\n%d control%s checked; worst neutral %.3f dB%s\n\n",
                     checked, checked == 1 ? "" : "s", worst,
                     worstWhere.isEmpty() ? "" : (" (" + worstWhere + ")").toRawUTF8());
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

    // ONE of the ends has to build a filter, not both. A pack's reference position is flat by
    // construction — it is the setting the models were captured at, so the curve against it is zero
    // everywhere — and a flat curve deliberately costs no convolution. Guitar Butler's Bass is flat
    // at the top of its travel; SM7's EQ-Lo was flat in the middle. Demanding both was demanding that
    // no pack use its top position as the reference.
    report ("a filter is built where the curve is not flat", down.isActive() || up.isActive(),
            juce::String (down.isActive() ? "min " : "") + juce::String (up.isActive() ? "max" : ""));

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
                band.hi > band.lo && band.lo > 0.0,
                juce::String (band.lo, 0) + ".." + juce::String (band.hi, 0) + " Hz");

        // And a pack that claims MORE than the instrument has is narrowed to it. Whoever measured is
        // the first to say the wide claim is the measurement's, not the pedal's.
        namz::rig::Measured greedy = *sweep;
        greedy.trusted.levels = 4;
        greedy.trusted.loHz = 0.5;
        greedy.trusted.hiHz = 40000.0;

        const auto clamped = MeasuredFilter::bandFor (greedy);
        report ("...and a pack claiming the whole grid is narrowed to it",
                clamped.lo >= band.lo && clamped.hi <= band.hi,
                juce::String (clamped.lo, 0) + ".." + juce::String (clamped.hi, 0) + " Hz");

        // A collapsed band is a verdict, not a range, and must survive the clamp as one.
        namz::rig::Measured failed2 = *sweep;
        failed2.trusted.levels = 3;
        failed2.trusted.loHz = 2000.0;
        failed2.trusted.hiHz = 1000.0;

        const auto verdict = MeasuredFilter::bandFor (failed2);
        report ("...while \"failed everywhere\" is not widened into a band",
                verdict.hi <= verdict.lo);

        // Holding, measured on a band narrow enough to sit in the middle of the audible range —
        // the default band's own edges are where a convolution measurement is least reliable, and
        // what is being checked is the mechanism, not today's two numbers.
        namz::rig::Measured narrow = *sweep;
        narrow.trusted.levels = 4;
        narrow.trusted.loHz = 400.0;
        narrow.trusted.hiHz = 4000.0;

        MeasuredFilter held;
        arm (held, narrow, 1.0);

        const double atEdge = responseDb (held, 400.0);
        const double under  = responseDb (held, 150.0);

        report ("...and nothing new happens below the band", std::abs (under - atEdge) < 1.5,
                juce::String (atEdge, 2) + " dB at 400, " + juce::String (under, 2) + " at 150");
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
