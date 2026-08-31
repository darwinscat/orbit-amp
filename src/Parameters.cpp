#include "Parameters.h"

#include "device/DeviceLibrary.h"

#include <felitronics/rigplayer/ToneKnobs.h>

namespace orbitamp::params
{

namespace
{
    /** The factory defaults of a captured block, read from the PACK it opens on — the device by
        name in its slot's list, its tone knobs where the pack starts them, its selectors likewise.
        A parameter's default is what a fresh instance IS, and "the middle of every slot" is not a
        sound anyone chose. With the pack absent, everything falls back to the old neutral. */
    struct BlockDefaults
    {
        int   device = 0;
        std::array<float, (size_t) boostNumMeasured> meas;
        std::array<int,   (size_t) numSelectors>     sel {};

        BlockDefaults() { meas.fill (0.5f); }
    };

    BlockDefaults blockDefaults (device::DeviceLibrary::Slot slot, const juce::String& wantedName)
    {
        BlockDefaults d;
        const auto packs = device::DeviceLibrary::scan (slot);

        for (int i = 0; i < packs.size(); ++i)
        {
            const auto& pack = packs.getReference (i);
            if (pack.displayName() != wantedName)
                continue;

            d.device = i;

            for (const auto& st : pack.rig.chain)
            {
                if (st.kind != namz::rig::StageKind::Nam)
                    continue;

                for (int t = 0; t < (int) st.tone.size() && t < boostNumMeasured; ++t)
                {
                    const auto& tone = st.tone[(size_t) t];
                    const auto  start = felitronics::rigplayer::toneStart (tone);

                    if (tone.sweep > 0)
                    {
                        double norm = 0.5;
                        if (felitronics::rigplayer::toneNorm (tone, start, norm))
                            d.meas[(size_t) t] = (float) norm;
                    }
                    else
                    {
                        const int n = (int) tone.positions.size();
                        for (int k = 0; k < n; ++k)
                            if (tone.positions[(size_t) k].value == start)
                                d.meas[(size_t) t] = n > 1 ? (float) k / (float) (n - 1) : 0.0f;
                    }
                }

                int selSlot = 0;
                for (const auto& c : st.device.controls)
                    if (c.role != namz::rig::Role::Gain && c.values.size() > 1 && selSlot < numSelectors)
                    {
                        const auto def = namz::rig::defaultValue (c);
                        for (int k = 0; k < (int) c.values.size(); ++k)
                            if (c.values[(size_t) k] == def)
                                d.sel[(size_t) selSlot] = k;
                        ++selSlot;
                    }
                break;
            }
            break;
        }

        return d;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    // The devices a fresh instance opens on, and everything the packs say about starting them.
    const auto boostDef  = blockDefaults (device::DeviceLibrary::Slot::pedal,  "TS");
    const auto preampDef = blockDefaults (device::DeviceLibrary::Slot::preamp, "IR-X Ch2");
    using Bool   = juce::AudioParameterBool;
    using Choice = juce::AudioParameterChoice;
    using Float  = juce::AudioParameterFloat;
    using Int    = juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Block power. A boost is an addition to the sound, so it starts off; the rest are the sound.
    layout.add (std::make_unique<Bool> (juce::ParameterID { boostOn,  1 }, "Boost",  false),
                std::make_unique<Bool> (juce::ParameterID { preampOn, 1 }, "Preamp", true),
                std::make_unique<Bool> (juce::ParameterID { reverbOn, 1 }, "Reverb", true));

    // The input trim, ahead of everything. Unity by default: it exists to fix a rig, not to be
    // part of a sound.
    layout.add (std::make_unique<Float> (juce::ParameterID { inTrim, 1 }, "Input Trim",
                                         juce::NormalisableRange<float> (-inTrimRangeDb, inTrimRangeDb, 0.1f),
                                         0.0f));

    layout.add (std::make_unique<Float> (juce::ParameterID { outTrim, 1 }, "Output Trim",
                                         juce::NormalisableRange<float> (-inTrimRangeDb, inTrimRangeDb, 0.1f),
                                         0.0f));

    // SAFETY out of the box: the ceiling at -0.3 — the guard rail, not a sound.
    layout.add (std::make_unique<Bool> (juce::ParameterID { limiterOn, 1 }, "Limiter", true),
                std::make_unique<Float> (juce::ParameterID { limiterCeiling, 1 }, "Limiter Ceiling",
                                         juce::NormalisableRange<float> (limiterCeilingMin,
                                                                         limiterCeilingMax, 0.1f),
                                         -0.3f));

    // The noise gate. Off out of the box — a gate is a decision about YOUR noise floor, not part
    // of the voicing. The ceiling is -10, NOT OrbitCab's -20: that range was voiced for a
    // post-trim guitar, and a raw input's bleed can sit at -20 dBFS all day — a threshold that
    // cannot climb over the signal is a gate that can never close, which read as "GR never
    // fires". Found live, against a speaker feeding the mic exactly that.
    layout.add (std::make_unique<Bool>  (juce::ParameterID { gateOn, 1 }, "Gate", false),
                std::make_unique<Float> (juce::ParameterID { gateThreshold, 1 }, "Gate Threshold",
                                         juce::NormalisableRange<float> (-80.0f, -10.0f, 1.0f), -50.0f),
                std::make_unique<Choice> (juce::ParameterID { gatePos, 1 }, "Gate Mute At",
                                          gatePositions, 1),
                std::make_unique<Choice> (juce::ParameterID { gateDecay, 1 }, "Gate Decay",
                                          gateDecayModes, 0));

    // Boost. The gain knob is CONTINUOUS: between two captured positions the player mixes the two
    // neighbours by angle, and in STEP mode it lands on the nearer one itself — the parameter never
    // has to know where the captures are.
    layout.add (std::make_unique<Float> (juce::ParameterID { boostGain, 1 }, "Boost Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f),
                std::make_unique<Float> (juce::ParameterID { boostTone, 1 }, "Boost Tone",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 10.0f));

    // Normalised 0..1: a measured control's real span is degrees on some device's dial, and the pack
    // is what knows it. The parameter stays the same shape whatever loads.
    for (int i = 0; i < boostNumMeasured; ++i)
        layout.add (std::make_unique<Float> (juce::ParameterID { boostMeasured (i), 1 },
                                             "Boost " + juce::String (i + 1),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                             boostDef.meas[(size_t) i]));

    layout.add (std::make_unique<Int> (juce::ParameterID { boostDevice, 1 }, "Boost Device",
                                       0, maxDevices - 1, boostDef.device));

    // The raw switch and the selector slots, one set per captured block — and the block's one trim.
    // IN is how hard the capture is fed, and it rides its own meter rather than being a knob among
    // knobs, because it is the question you ask first about a captured device.
    for (const char* blk : { boostId, preampId, powerId })
    {
        const juce::NormalisableRange<float> trim (blockTrimMinDb, blockTrimMaxDb, 0.1f);

        layout.add (std::make_unique<Float> (juce::ParameterID { blockIn (blk), 1 },
                                             juce::String (blk) + " In", trim, 0.0f));

        // The DEVICE's own tone by default: a captured pedal arrives wearing its own knobs, and
        // the console is the scalpel you reach for past them.
        layout.add (std::make_unique<Choice> (juce::ParameterID { blockEqMode (blk), 1 },
                                              juce::String (blk) + " EQ", eqModes, 0));

        // The dial's motion between captures. STEP by default — the captured positions are the
        // device as it was shot — and SMOOTH for whoever wants every angle between them.
        layout.add (std::make_unique<Bool> (juce::ParameterID { blockSmooth (blk), 1 },
                                            juce::String (blk) + " Smooth", false));

        const auto& def = blk == boostId ? boostDef : blk == preampId ? preampDef : BlockDefaults();
        for (int i = 0; i < numSelectors; ++i)
            layout.add (std::make_unique<Int> (juce::ParameterID { selectorId (blk, i), 1 },
                                               juce::String (blk) + " Select " + juce::String (i + 1),
                                               0, 15, def.sel[(size_t) i]));
    }

    layout.add (std::make_unique<Int> (juce::ParameterID { preampDevice, 1 }, "Preamp Device",
                                       0, maxDevices - 1, preampDef.device));

    // The captured power amp's own device, dial and tone slots — the shape the other two have.
    layout.add (std::make_unique<Int>   (juce::ParameterID { blockDevice (powerId), 1 }, "Power Device",
                                         0, maxDevices - 1, 0),
                std::make_unique<Float> (juce::ParameterID { blockGain (powerId), 1 }, "Power Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    for (int i = 0; i < boostNumMeasured; ++i)
        layout.add (std::make_unique<Float> (juce::ParameterID { blockMeasured (powerId, i), 1 },
                                             "Power " + juce::String (i + 1),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    for (int i = 0; i < preampNumMeasured; ++i)
        layout.add (std::make_unique<Float> (juce::ParameterID { preampMeasured (i), 1 },
                                             "Preamp " + juce::String (i + 1),
                                             juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
                                             preampDef.meas[(size_t) i]));

    // The hero. 0-10 is the amp-panel scale, not a dB value — it maps onto the captured detents, so
    // the number on the face is the number the capture was taken at.
    layout.add (std::make_unique<Float> (juce::ParameterID { preampGain, 1 }, "Gain",
                                         juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f));

    // Frequencies are skewed so the useful end of each sweep gets the middle of the travel.
    auto hz = [] (float lo, float hi, float centre)
    {
        return juce::NormalisableRange<float> (lo, hi, 0.1f, std::log (0.5f) / std::log ((centre - lo) / (hi - lo)));
    };

    // The EQ links, in the console grammar. Both ship FLAT, which is also what they used to ship
    // switched off as — a link with every band at zero is bit-transparent, so the enable it used to
    // carry said nothing the values did not.
    for (int l = 0; l < numEqLinks; ++l)
    {
        const juce::String name = "EQ" + juce::String (l + 1) + " ";
        const juce::NormalisableRange<float> tone { -toneRangeDb, toneRangeDb, 0.1f };
        const juce::NormalisableRange<float> q    { 0.3f, 6.0f, 0.01f, 0.5f };
        const juce::NormalisableRange<float> qN   { 1.0f, 18.0f, 0.01f, 0.5f };   // the surgical bell runs narrow

        // The cut filters, each with its slope. Off by default: they are for tightening a
        // specific rig, not part of the voicing.
        layout.add (std::make_unique<Bool>   (juce::ParameterID { eqHpfOn (l),    1 }, name + "HPF", false),
                    std::make_unique<Float>  (juce::ParameterID { eqHpfHz (l),    1 }, name + "HPF Freq",
                                              hz (0.0f, 1000.0f, 80.0f), 80.0f),
                    std::make_unique<Choice> (juce::ParameterID { eqHpfSlope (l), 1 }, name + "HPF Slope",
                                              eqSlopes, eqSlopeDefault),
                    std::make_unique<Bool>   (juce::ParameterID { eqLpfOn (l),    1 }, name + "LPF", false),
                    std::make_unique<Float>  (juce::ParameterID { eqLpfHz (l),    1 }, name + "LPF Freq",
                                              hz (1200.0f, 20000.0f, 8000.0f), 10000.0f),
                    std::make_unique<Choice> (juce::ParameterID { eqLpfSlope (l), 1 }, name + "LPF Slope",
                                              eqSlopes, eqSlopeDefault));

        // The shelves: gain and corner, no Q — a shelf with a Q is a bell wearing a coat.
        layout.add (std::make_unique<Float> (juce::ParameterID { eqLoDb (l), 1 }, name + "Lo",      tone, 0.0f),
                    std::make_unique<Float> (juce::ParameterID { eqLoHz (l), 1 }, name + "Lo Freq",
                                             hz (30.0f, 500.0f, 100.0f), 100.0f),
                    std::make_unique<Float> (juce::ParameterID { eqHiDb (l), 1 }, name + "Hi",      tone, 0.0f),
                    std::make_unique<Float> (juce::ParameterID { eqHiHz (l), 1 }, name + "Hi Freq",
                                             hz (1500.0f, 16000.0f, 6000.0f), 8000.0f));

        // The bells: two for the tone, and the narrow third that switches in — the surgical slot
        // search→treat lands in.
        layout.add (std::make_unique<Float> (juce::ParameterID { eqBellDb (l, 0), 1 }, name + "B1",      tone, 0.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellHz (l, 0), 1 }, name + "B1 Freq",
                                             hz (60.0f, 3000.0f, 400.0f), 400.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellQ (l, 0),  1 }, name + "B1 Q", q, 1.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellDb (l, 1), 1 }, name + "B2",      tone, 0.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellHz (l, 1), 1 }, name + "B2 Freq",
                                             hz (300.0f, 12000.0f, 2500.0f), 2500.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellQ (l, 1),  1 }, name + "B2 Q", q, 1.0f),
                    std::make_unique<Bool>  (juce::ParameterID { eqB3On (l),      1 }, name + "B3", false),
                    std::make_unique<Float> (juce::ParameterID { eqBellDb (l, 2), 1 }, name + "B3",      tone, 0.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellHz (l, 2), 1 }, name + "B3 Freq",
                                             hz (100.0f, 12000.0f, 3000.0f), 3000.0f),
                    std::make_unique<Float> (juce::ParameterID { eqBellQ (l, 2),  1 }, name + "B3 Q", qN, 8.0f));

        // The output level — a fader on the link, because every boost here is also "+drive into
        // the next nonlinearity", and shape deserves a say separate from push.
        layout.add (std::make_unique<Float> (juce::ParameterID { eqLevel (l), 1 }, name + "Level",
                                             juce::NormalisableRange<float> (-eqLevelRangeDb, eqLevelRangeDb, 0.1f),
                                             0.0f));
    }

    // The cabinet. Position and distance are the two halves of one gesture on the grid; distance is
    // in centimetres, matching the grid's own ladder.
    layout.add (std::make_unique<Bool> (juce::ParameterID { cabOn, 1 }, "Cabinet", true));

    // The IR's own post: two cuts, a trim and a flip, each behind a switch — off, the IR plays as
    // it was shot. The frequencies are ranged as OrbitCab ranges them.
    layout.add (std::make_unique<Bool>  (juce::ParameterID { cabHpfOn,  1 }, "Cab HPF", false),
                std::make_unique<Float> (juce::ParameterID { cabHpfHz,  1 }, "Cab HPF Hz",
                                         juce::NormalisableRange<float> (cabHpfMinHz, cabHpfMaxHz, 1.0f, 0.5f), cabHpfDefaultHz),
                std::make_unique<Choice> (juce::ParameterID { cabHpfSlope, 1 }, "Cab HPF Slope", eqSlopes, 1),
                std::make_unique<Bool>  (juce::ParameterID { cabLpfOn,  1 }, "Cab LPF", false),
                std::make_unique<Float> (juce::ParameterID { cabLpfHz,  1 }, "Cab LPF Hz",
                                         juce::NormalisableRange<float> (cabLpfMinHz, cabLpfMaxHz, 10.0f, 0.5f), cabLpfDefaultHz),
                std::make_unique<Choice> (juce::ParameterID { cabLpfSlope, 1 }, "Cab LPF Slope", eqSlopes, 1),
                std::make_unique<Bool>  (juce::ParameterID { cabTrimOn, 1 }, "Cab Trim", false),
                std::make_unique<Float> (juce::ParameterID { cabTrim,   1 }, "Cab Trim Length",
                                         juce::NormalisableRange<float> (cabTrimMin, 1.0f, 0.001f), 1.0f),
                std::make_unique<Bool>  (juce::ParameterID { cabPhase,  1 }, "Cab Phase", false));

    // The power amp is off by default: it is most useful for leads and least for tight rhythm, so
    // it should be something you reach for rather than something you find already on. What it IS
    // is a captured device now — a pack in the poweramp slot — with the same set of slots the
    // other two captured blocks have, below.
    layout.add (std::make_unique<Bool> (juce::ParameterID { powerOn, 1 }, "Power Amp", false));

    // MONO is the truth of a guitar chain and half the neural cost; STEREO is the double-track
    // option — two takes on one bus, each through its own amp; STEREO SPACE keeps the amp mono
    // and lets the reverb and everything after it go wide.
    layout.add (std::make_unique<Choice> (juce::ParameterID { stereoMode, 1 }, "Stereo Mode",
                                          stereoModes, 0));

    layout.add (std::make_unique<Choice> (juce::ParameterID { cabIr, 1 }, "Cabinet IR",
                                          cabIrNames, cabIrDefault));

    // PLATE at thirty: the shipping room. DECAY breathes around unity, PREDELAY ships at zero,
    // and the tail's HPF waits switched off at a bass-cleaning corner.
    layout.add (std::make_unique<Choice> (juce::ParameterID { reverbType, 1 }, "Reverb", reverbCharacters,
                                          reverbCharacters.indexOf ("Plate")),
                std::make_unique<Float>  (juce::ParameterID { reverbMix, 1 }, "Mix",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f),
                std::make_unique<Float>  (juce::ParameterID { reverbDecay, 1 }, "Reverb Decay",
                                          juce::NormalisableRange<float> (0.5f, 2.0f, 0.01f,
                                                                          std::log (0.5f) / std::log ((1.0f - 0.5f) / (2.0f - 0.5f))),
                                          1.0f),
                std::make_unique<Float>  (juce::ParameterID { reverbPredelay, 1 }, "Reverb Predelay",
                                          juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 0.0f),
                std::make_unique<Bool>   (juce::ParameterID { reverbHpfOn, 1 }, "Reverb HPF", false),
                std::make_unique<Float>  (juce::ParameterID { reverbHpfHz, 1 }, "Reverb HPF Hz",
                                          hz (40.0f, 500.0f, 120.0f), 120.0f));

    return layout;
}

} // namespace orbitamp::params
