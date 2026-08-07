#pragma once

#include <felitronics/poweramp/PowerAmpStage.h>
#include <juce_dsp/juce_dsp.h>

namespace orbitamp::core
{

/** The power amp: felitronics' white-box tube stage, calibrated for this chain.

    The module exposes ten controls and a ~25-field voicing. Most of that is what a power amp IS
    rather than what you set on it, so it is baked here and two knobs reach the face: Drive and Sag.
    The rest — topology, bias, transformer, speaker load — belongs to the model, and a model is
    chosen, not dialled.

    Drive changes the SOUND, not the loudness. Across its range the stage loses about 7 dB as it
    compresses, and a knob that makes things quieter as you turn it up teaches the wrong thing; the
    loss is measured and given back. That is the same rule the captured stages follow — loudness is
    our own layer, not a side effect. */
class PowerAmp
{
public:
    /** The output bottles. The module is built around a per-tube voicing table — this is it.

        Ordered by headroom, least first, which is also the order they break up in: measured across
        the Drive sweep an EL84 loses 12.5 dB to compression where a KT88 loses 4.5. */
    enum class Tube { el84, el34, sixL6, kt88, count };

    /** One bottle or two. Not decoration: one output tube IS single-ended class A, two are
        push-pull, and the module takes that as a first-class parameter. Each combination carries
        its own measured make-up, because the two topologies compress differently. */
    static constexpr int minTubes = 1;
    static constexpr int maxTubes = 2;

    void setTube (Tube t) noexcept
    {
        if (t != tube && t < Tube::count)
        {
            tube = t;
            apply();
        }
    }

    void setTubeCount (int n) noexcept
    {
        const int c = juce::jlimit (minTubes, maxTubes, n);
        if (c != tubeCount)
        {
            tubeCount = c;
            apply();
        }
    }

    /** The knob's span in the stage's own drive dB, from the calibration bench:

            knob  driveDb   THD
              0     -6      0.1 %
              5     +6      2.8 %
             10    +18     26 %

        Past +18 the level collapses and THD flattens near 43 % — mush, not a louder amp, so the
        knob does not go there. */
    static constexpr float driveDbAtZero = -6.0f;
    static constexpr float driveDbAtTen  = 18.0f;

    /** The oversampling factor is fixed at prepare() by the module, so changing it re-prepares. That
        is why it lives in the footer rather than on a block: it is a property of the run, not of the
        sound. */
    void setOversampling (int factor)
    {
        const int f = juce::jlimit (2, 16, factor);
        if (f == oversampleFactor)
            return;

        oversampleFactor = f;

        // Re-prepare only if there is something to re-prepare. Called before the first prepare — as
        // the processor does, so the factor is in place when the stage is built — this just stores it.
        if (preparedRate > 0.0)
            prepare (preparedRate, preparedBlock, preparedChannels);
    }

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        preparedRate = sampleRate; preparedBlock = maxBlock; preparedChannels = numChannels;
        stage.prepare (sampleRate, maxBlock, oversampleFactor);
        stage.reset();

        // The bypass path is delayed by exactly the stage's latency, so switching the block on and
        // off cannot shift the timing of everything downstream.
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlock),
                                      (juce::uint32) juce::jmax (1, numChannels) };
        bypass.prepare (spec);
        bypass.setMaximumDelayInSamples (juce::jmax (1, stage.latencySamples() + 1));
        bypass.setDelay ((float) stage.latencySamples());
        bypass.reset();

        apply();
    }

    void reset()
    {
        stage.reset();
        bypass.reset();
    }

    int latencySamples() const noexcept { return stage.latencySamples(); }

    void setDrive (float knob0to10) noexcept
    {
        const float d = juce::jlimit (0.0f, 10.0f, knob0to10) * 0.1f;
        if (! juce::approximatelyEqual (d, drive))
        {
            drive = d;
            apply();
        }
    }

    void setSag (float knob0to10) noexcept
    {
        const float s = juce::jlimit (0.0f, 10.0f, knob0to10) * 0.1f;
        if (! juce::approximatelyEqual (s, sag))
        {
            sag = s;
            apply();
        }
    }

    void process (float* const* channels, int numChannels, int numSamples, bool enabled) noexcept
    {
        if (enabled)
        {
            stage.process (channels, numChannels, numSamples);
            return;
        }

        // Off: the dry signal still pays the latency, so the block's switch is silent in time as
        // well as in level.
        for (int c = 0; c < numChannels; ++c)
        {
            auto* data = channels[c];
            for (int i = 0; i < numSamples; ++i)
            {
                bypass.pushSample (c, data[i]);
                data[i] = bypass.popSample (c);
            }
        }
    }

private:
    /** Make-up from the bench: the stage's own gain at nine measured drive settings, per tube and per
        topology, which the knob gives back so its sweep is a change of character at a steady level.

        Eight curves because the two topologies compress differently — a single-ended stage starts
        bending earlier and keeps going, so one shared curve would leave the knob quieter at the top
        on half the settings. Rows: driveDb -6, -3, 0, 3, 6, 9, 12, 15, 18. */
    static constexpr int    kSteps = 9;
    static constexpr float  kGain[2][(int) Tube::count][kSteps] = {
        {   // push-pull, two tubes
            { 0.36f, 0.29f,  0.01f, -0.76f, -2.27f, -4.42f, -6.96f, -9.70f, -12.53f },   // EL84
            { 1.52f, 1.48f,  1.37f,  1.06f,  0.31f, -1.11f, -3.19f, -5.70f,  -8.43f },   // EL34
            { 0.34f, 0.32f,  0.26f,  0.11f, -0.25f, -1.04f, -2.47f, -4.53f,  -7.01f },   // 6L6
            { 0.35f, 0.33f,  0.30f,  0.22f,  0.05f, -0.32f, -1.09f, -2.47f,  -4.48f },   // KT88
        },
        {   // single-ended, one tube
            { 0.15f, -0.10f, -0.67f, -1.80f, -3.60f, -5.94f, -8.59f, -11.38f, -14.26f },
            { 1.37f,  1.20f,  0.86f,  0.20f, -0.96f, -2.73f, -5.03f,  -7.65f, -10.44f },
            { 0.28f,  0.19f,  0.01f, -0.33f, -0.99f, -2.13f, -3.86f,  -6.11f,  -8.70f },
            { 0.32f,  0.28f,  0.20f,  0.03f, -0.30f, -0.91f, -1.99f,  -3.64f,  -5.83f },
        },
    };

    float makeUpDb (float knob) const
    {
        const auto& row = kGain[tubeCount == 1 ? 1 : 0][(int) tube];

        const float t = juce::jlimit (0.0f, 1.0f, knob) * (kSteps - 1);
        const int   i = juce::jlimit (0, kSteps - 2, (int) t);

        return -juce::jmap (t - (float) i, row[i], row[i + 1]);
    }

    void apply() noexcept
    {
        felitronics::poweramp::Params p;
        p.driveDb     = juce::jmap (drive, driveDbAtZero, driveDbAtTen);
        p.outputDb    = makeUpDb (drive);
        p.singleEnded = (tubeCount == 1);   // one bottle is class A; the count IS the topology
        p.autoComp    = 1.0f;    // small-signal gain stays put; only the character moves
        p.sag         = sag;

        // Fixed at the model's character. These are the amp, not its controls.
        p.load     = 0.7f;
        p.iron     = 0.5f;
        p.presence = 0.0f;   // the face already has one, in the tone stack
        p.depth    = 0.0f;
        p.bias     = 0.4f;

        stage.setParams (p, voicing());
    }

    /** The tube table. Differences are the ones a player would name: how much headroom before it
        bends, how hard it sags, and where the transformer gives up.

        These numbers are a STARTING POINT chosen for a sane, ordered spread of THD, not four tuned
        amps. The spread is real and measured; whether an EL34 here sounds like an EL34 wants ears on
        a guitar. */
    felitronics::poweramp::Voicing voicing() const
    {
        felitronics::poweramp::Voicing v;
        v.driveScale = 1.0f;
        v.k = 2.0f; v.bSE = 0.18f; v.vbPP = 0.30f;
        v.evenLeak = 0.02f;        // a touch of asymmetry — an exactly-cancelling push-pull is a maths object

        v.sagFastMs = 8.0f; v.sagRecoveryMs = 150.0f;
        v.sagMaxDroop = 0.28f; v.sagBiasDepth = 0.35f;

        v.presenceMaxDb = 0.0f; v.depthMaxDb = 0.0f; v.nfbOpen = 0.0f;
        v.midDb = 0.0f;

        v.loadResHz = 100.0f; v.loadResQ = 1.0f; v.loadResDb = 4.0f;    // cone resonance
        v.loadRiseHz = 1500.0f; v.loadRiseDb = 3.0f;                    // inductive HF rise
        v.otLfHz = 150.0f; v.otSatK = 1.8f; v.otHfHz = 12000.0f;        // output transformer

        switch (tube)
        {
            case Tube::el84:   // smallest bottle, least headroom, sags hardest
                v.driveScale = 1.9f; v.k = 2.4f; v.bSE = 0.22f; v.evenLeak = 0.05f;
                v.sagMaxDroop = 0.38f; v.sagRecoveryMs = 110.0f;
                v.otLfHz = 190.0f; v.otSatK = 2.2f; v.otHfHz = 9500.0f;
                break;

            case Tube::el34:   // British, mid-forward, crunches before a 6L6 does
                v.driveScale = 1.35f; v.k = 2.15f; v.evenLeak = 0.03f;
                v.sagMaxDroop = 0.30f;
                v.midHz = 700.0f; v.midDb = 1.6f; v.midQ = 0.8f;
                v.otLfHz = 165.0f; v.otHfHz = 11000.0f;
                break;

            case Tube::sixL6:  // American, clean headroom, tight bottom
                v.sagMaxDroop = 0.24f;
                v.loadResDb = 4.5f; v.otLfHz = 140.0f; v.otHfHz = 13000.0f;
                break;

            case Tube::kt88:   // biggest and stiffest — most headroom, least sag
                v.driveScale = 0.72f; v.k = 1.85f; v.evenLeak = 0.015f;
                v.sagMaxDroop = 0.16f; v.sagRecoveryMs = 190.0f;
                v.otLfHz = 120.0f; v.otSatK = 1.5f; v.otHfHz = 15000.0f;
                break;

            case Tube::count:
            default: break;
        }

        return v;
    }

    felitronics::poweramp::PowerAmpStage stage;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypass { 64 };

    double preparedRate = 0.0;
    int    preparedBlock = 0, preparedChannels = 2;
    int    oversampleFactor = 4;

    Tube  tube      = Tube::sixL6;
    int   tubeCount = 2;
    float drive     = 0.5f;
    float sag       = 0.0f;
};

} // namespace orbitamp::core
