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
    /** The knob's span in the stage's own drive dB, from the calibration bench:

            knob  driveDb   THD
              0     -6      0.1 %
              5     +6      2.8 %
             10    +18     26 %

        Past +18 the level collapses and THD flattens near 43 % — mush, not a louder amp, so the
        knob does not go there. */
    static constexpr float driveDbAtZero = -6.0f;
    static constexpr float driveDbAtTen  = 18.0f;

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        stage.prepare (sampleRate, maxBlock, 4);
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
    /** Make-up from the bench: the stage's own gain at nine measured drive settings, which the knob
        gives back so its sweep is a change of character at a steady level. */
    static float makeUpDb (float knob)
    {
        // Measured gain (dB) at driveDb -6, -3, 0, 3, 6, 9, 12, 15, 18 — the knob's span in nine steps.
        static constexpr float measured[] = { 0.33f, 0.31f, 0.25f, 0.10f, -0.26f, -1.05f, -2.48f, -4.54f, -7.02f };
        constexpr int n = (int) (sizeof (measured) / sizeof (measured[0]));

        const float t = juce::jlimit (0.0f, 1.0f, knob) * (n - 1);
        const int   i = juce::jlimit (0, n - 2, (int) t);

        return -juce::jmap (t - (float) i, measured[i], measured[i + 1]);
    }

    void apply() noexcept
    {
        felitronics::poweramp::Params p;
        p.driveDb     = juce::jmap (drive, driveDbAtZero, driveDbAtTen);
        p.outputDb    = makeUpDb (drive);
        p.singleEnded = false;   // push-pull: the model's, not the player's
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

    /** The one model so far. A second entry is what "NAM 1 / NAM 2" become when captures arrive —
        those are profiles rather than voicings, but the block's choice is the same shape.

        These numbers are a STARTING POINT measured for sane THD, not a tuned amp: driveScale, the
        bias pair and the transformer corners want ears on a guitar before they are called done. */
    static felitronics::poweramp::Voicing voicing()
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
        return v;
    }

    felitronics::poweramp::PowerAmpStage stage;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> bypass { 64 };

    float drive = 0.5f;
    float sag   = 0.0f;
};

} // namespace orbitamp::core
