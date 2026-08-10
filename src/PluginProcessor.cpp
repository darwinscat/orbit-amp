#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

#include <BinaryData.h>

namespace orbitamp
{

AmpProcessor::AmpProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", params::createLayout()),
      // The history's opaque seam: it snapshots and restores the parameter tree without knowing what
      // is in it. replaceState takes a COPY so the parameter objects stay valid and every editor
      // attachment survives an undo.
      history (felitronics::appkit::CompareHistory::Mode::PerRegister,
               [this] { return apvts.copyState(); },
               [this] (const juce::ValueTree& t) { apvts.replaceState (t.createCopy()); },
               felitronics::appkit::CompareHistory::Config {})
{
    // One steady pump for the settle timer. 30 Hz with the engine's default settle count means a
    // burst commits about 0.4 s after you stop moving.
    startTimerHz (30);

    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto& p = eqParams[(size_t) l];
        p.on    = apvts.getRawParameterValue (params::eqOn (l));
        p.low   = apvts.getRawParameterValue (params::eqLow (l));
        p.mid   = apvts.getRawParameterValue (params::eqMid (l));
        p.high  = apvts.getRawParameterValue (params::eqHigh (l));
        p.pres  = apvts.getRawParameterValue (params::eqPresence (l));
        p.midHz = apvts.getRawParameterValue (params::eqMidHz (l));
        p.hpfOn = apvts.getRawParameterValue (params::eqHpfOn (l));
        p.hpfHz = apvts.getRawParameterValue (params::eqHpfHz (l));
        p.lpfOn = apvts.getRawParameterValue (params::eqLpfOn (l));
        p.lpfHz = apvts.getRawParameterValue (params::eqLpfHz (l));
    }

    gateOnParam        = apvts.getRawParameterValue (params::gateOn);
    gateThresholdParam = apvts.getRawParameterValue (params::gateThreshold);
    gatePosParam       = apvts.getRawParameterValue (params::gatePos);
    gateDecayParam     = apvts.getRawParameterValue (params::gateDecay);

    reverbOnParam   = apvts.getRawParameterValue (params::reverbOn);
    reverbTypeParam = apvts.getRawParameterValue (params::reverbType);
    reverbMixParam  = apvts.getRawParameterValue (params::reverbMix);

    powerOnParam    = apvts.getRawParameterValue (params::powerOn);
    powerDriveParam = apvts.getRawParameterValue (params::powerDrive);
    powerSagParam   = apvts.getRawParameterValue (params::powerSag);
    powerTubeParam  = apvts.getRawParameterValue (params::powerTube);
    powerCountParam = apvts.getRawParameterValue (params::powerCount);
    oversampleParam = apvts.getRawParameterValue (params::oversample);
    boostOnParam    = apvts.getRawParameterValue (params::boostOn);
    boostGainParam  = apvts.getRawParameterValue (params::boostGain);
    preampOnParam   = apvts.getRawParameterValue (params::preampOn);
    preampGainParam = apvts.getRawParameterValue (params::preampGain);

    rescanDevices();
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The workspace envelope already contains the live parameter tree, so saving it saves
    // everything: the sound, the other three registers, and each register's undo history.
    if (auto xml = history.toTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    const auto apply = [] (AmpProcessor& self, const juce::ValueTree& t)
    {
        if (self.history.fromTree (t))
            return;

        // Sessions saved before the workspace existed hold a bare parameter tree. Load the sound
        // and start a fresh history around it rather than dropping the session on the floor.
        if (t.hasType (self.apvts.state.getType()))
        {
            self.apvts.replaceState (t);
            self.history.reset();
        }
    };

    // CompareHistory's contract is message-thread only, and a host may restore from anywhere —
    // marshalled rather than raced against the settle timer (found in review).
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        apply (*this, tree);
        return;
    }

    juce::MessageManager::callAsync (
        [weak = juce::WeakReference<AmpProcessor> (this), tree, apply]
        {
            if (auto* self = weak.get())
                apply (*self, tree);
        });
}

void AmpProcessor::selectDemoLoop (int index)
{
    struct Entry { const char* data; int size; };
    const Entry loops[] = {
        { BinaryData::elevenlightyears_wav,   BinaryData::elevenlightyears_wavSize },
        { BinaryData::catshardday_wav,        BinaryData::catshardday_wavSize },
        { BinaryData::deepspaceismyhome_wav,  BinaryData::deepspaceismyhome_wavSize },
        { BinaryData::fifthdimension_wav,     BinaryData::fifthdimension_wavSize },
    };

    const int n = (int) (sizeof (loops) / sizeof (loops[0]));
    const auto& e = loops[juce::jlimit (0, n - 1, index)];
    demo.setLoop (e.data, e.size, getSampleRate());
}

void AmpProcessor::rescanDevices()
{
    // Each block asks for its own kind. A preamp offered as a pedal is not a wrong sound, it is a
    // wrong LIST — the block says what it is for, and the list has to agree with it.
    boost.rescan (juce::roundToInt (apvts.getRawParameterValue (params::boostDevice)->load()));
    preamp.rescan (juce::roundToInt (apvts.getRawParameterValue (params::preampDevice)->load()));
}

void AmpProcessor::pumpDeviceWork()
{
    auto pump = [this] (auto& block, auto& gainParam, auto measuredId, const char* blk)
    {
        // The device parameter first of all: a restored session or an automating host moves it
        // without calling anyone, and every read below is about whatever pack it names.
        block.selectIfMoved (juce::roundToInt (
            apvts.getRawParameterValue (params::blockDevice (blk))->load()));

        // Selectors next: they and the gain dial pick a file together, and a stale one would send
        // resolve looking for a combination the player is not on any more.
        std::array<int, (size_t) params::numSelectors> selectors {};
        for (int i = 0; i < params::numSelectors; ++i)
            selectors[(size_t) i] = juce::roundToInt (
                apvts.getRawParameterValue (params::selectorId (blk, i))->load());

        block.setRaw (apvts.getRawParameterValue (params::rawId (blk))->load() > 0.5f);
        block.applySelectors (selectors);
        block.loadIfGainMoved (gainParam->load());

        std::array<float, (size_t) std::decay_t<decltype (block)>::numMeasured> values {};
        for (int i = 0; i < (int) values.size(); ++i)
            values[(size_t) i] = apvts.getRawParameterValue (measuredId (i))->load();

        block.updateToneIfMoved (values);
        block.collectGarbage();
    };

    pump (boost,  boostGainParam,  params::boostMeasured,  params::boostId);
    pump (preamp, preampGainParam, params::preampMeasured, params::preampId);

    // The gate's Decay: redesigning the close ramp is message-thread work, like every other
    // moved-a-control job on this pump. Rare by nature — it only fires when the switch flipped.
    if (const float decay = gateDecayParam->load(); ! juce::approximatelyEqual (decay, lastGateDecay))
    {
        lastGateDecay = decay;
        gateCfg.closeMs = params::gateDecayModeMs[juce::jlimit (0, params::gateDecayModes.size() - 1,
                                                                juce::roundToInt (decay))];
        gate.setConfig (gateCfg);
    }
}

void AmpProcessor::applyOversamplingIfChanged()
{
    const int index = juce::jlimit (0, params::oversampleFactors.size() - 1,
                                    juce::roundToInt (oversampleParam->load()));
    if (index == lastOversample || getSampleRate() <= 0.0)
        return;

    lastOversample = index;
    power.setOversampling (params::oversampleValues[index]);

    // The factor does not change the round-trip — the module keeps it at tpp-1 across factors — but
    // report it again anyway, so a future module that does cannot silently desync the host.
    setLatencySamples (power.latencySamples());
}

void AmpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int channels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());

    // The ARGUMENT, not getBlockSize(). A host sets both and they agree, so this looked fine for as
    // long as only hosts called it — but the contract is the argument, and anything else calling
    // prepareToPlay directly got stages built for a block size nobody was going to send. The
    // convolver behind the measured controls then read past the end of its own buffers.
    const int block = juce::jmax (1, samplesPerBlock);

    for (auto& eq : eqLinks)
        eq.prepare (sampleRate, channels);

    // Seeded from the parameter: a session saved gate-ON has to start already gated, not fade in
    // over a block of ungated hum.
    gate.prepare (sampleRate, block, channels);
    gate.seedEnabled (gateOnParam->load() > 0.5f);

    reverb.prepare (sampleRate);
    boost.prepare (sampleRate, block, channels);
    preamp.prepare (sampleRate, block, channels);
    demo.prepare (sampleRate);
    scopeDry.setSize (1, block);

    pumpDeviceWork();

    // Choose the factor BEFORE preparing, so prepare() builds with it and nothing has to re-prepare
    // from inside a prepare.
    lastOversample = juce::jlimit (0, params::oversampleFactors.size() - 1,
                                   juce::roundToInt (oversampleParam->load()));
    power.setOversampling (params::oversampleValues[lastOversample]);
    power.prepare (sampleRate, block, channels);

    // Reported ALWAYS, whether the power amp is switched on or not: its bypass path carries the same
    // delay, so a toggle never shifts the timing of everything downstream. A latency that changes
    // with a switch is what makes hosts re-align mid-song.
    setLatencySamples (power.latencySamples());

    updateEqSettings();
    updateReverbSettings();
}

void AmpProcessor::updateReverbSettings() noexcept
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1,
                                    juce::roundToInt (reverbTypeParam->load()));

    reverb.setCharacter (static_cast<core::ReverbStage::Character> (index));
    reverb.setMix (reverbMixParam->load() * 0.01f);   // the face reads percent
}

void AmpProcessor::updateEqSettings() noexcept
{
    for (int l = 0; l < params::numEqLinks; ++l)
    {
        const auto& p = eqParams[(size_t) l];

        core::ToneStack::Settings s;
        s.lowDb      = (double) p.low->load();
        s.midDb      = (double) p.mid->load();
        s.midHz      = (double) p.midHz->load();
        s.highDb     = (double) p.high->load();
        s.presenceDb = (double) p.pres->load();
        s.hpfOn      = p.hpfOn->load() > 0.5f;
        s.hpfHz      = (double) p.hpfHz->load();
        s.lpfOn      = p.lpfOn->load() > 0.5f;
        s.lpfHz      = (double) p.lpfHz->load();

        eqLinks[(size_t) l].setSettings (s);
    }
}

bool AmpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono or stereo, in == out. A guitar rig is fed mono far more often than not.
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

void AmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const auto blockStart = juce::Time::getHighResolutionTicks();

    // Clear any output channel the host gave us beyond what the input carries.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // The demo replaces the input rather than mixing into it: a loop you can hear over your own
    // playing is a loop you cannot judge anything by.
    demo.fill (buffer);

    // The tuner listens HERE — the raw input (or the loop standing in for it), before any block
    // colours it.
    tunerTap.write (buffer.getReadPointer (0), buffer.getNumSamples());

    updateEqSettings();
    updateReverbSettings();

    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // gate -> eq1 -> boost -> eq2 -> preamp -> reverb -> power amp. The gate stands at the very
    // front, right after the tuner's ear: it keys off the raw guitar — the cleanest key there is —
    // and kills the hum before any dirt can multiply it. Its enable crossfade makes the toggle
    // pop-free, so it runs unconditionally and the switch is an argument.
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        gateKeyDb.store (juce::Decibels::gainToDecibels (peak, -90.0f));
    }

    // The gate KEYS here whatever it ends up muting — dual detection: the decision is made on the
    // raw input, the attenuation lands where the parameter says. The enable crossfade makes the
    // toggle pop-free, so analyse runs unconditionally and the switch is an argument.
    gate.analyse (channels, numChannels, numSamples,
                  gateOnParam->load() > 0.5f, gateThresholdParam->load());
    gateMeterDb.store (juce::Decibels::gainToDecibels (gate.currentGain(), -90.0f));

    const bool muteAtStart = juce::roundToInt (gatePosParam->load()) == 0;

    // MUTE at the start: the nonlinearities themselves fall silent between notes.
    if (muteAtStart)
        gate.applyGain (channels, numChannels, numSamples);

    // The EQ links are links of their own, with their own switches: eq1 decides what reaches the
    // first nonlinearity, eq2 colours what the boost made before the preamp distorts it again.
    if (eqParams[0].on->load() > 0.5f)
        eqLinks[0].process (channels, numChannels, numSamples);

    if (boostOnParam->load() > 0.5f)
        boost.process (buffer, scopeDry);

    if (eqParams[1].on->load() > 0.5f)
        eqLinks[1].process (channels, numChannels, numSamples);

    if (preampOnParam->load() > 0.5f)
        preamp.process (buffer, scopeDry);

    // MUTE pre-reverb — the G-String architecture, and the default: everything the chain ADDED
    // (boost hiss, preamp hiss) dies here too, and the reverb tail past it rings out.
    if (! muteAtStart)
        gate.applyGain (channels, numChannels, numSamples);

    if (reverbOnParam->load() > 0.5f)
        reverb.process (channels, numChannels, numSamples);
    else
        reverb.reset();   // so re-enabling it does not spill the tail of what was playing before

    power.setTube (static_cast<core::PowerAmp::Tube> (
        juce::jlimit (0, (int) core::PowerAmp::Tube::count - 1, juce::roundToInt (powerTubeParam->load()))));
    power.setTubeCount (juce::roundToInt (powerCountParam->load()) + 1);   // index 0 = one bottle
    power.setDrive (powerDriveParam->load());
    power.setSag (powerSagParam->load());
    power.process (channels, numChannels, numSamples, powerOnParam->load() > 0.5f);

    // Load as a share of the block's own wall time — the number the footer shows.
    if (const double budget = numSamples / juce::jmax (1.0, getSampleRate()); budget > 0.0)
    {
        const double spent = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - blockStart);
        dspLoad.store (dspLoad.load() * 0.9f + (float) (spent / budget * 100.0) * 0.1f);
    }
}

juce::AudioProcessorEditor* AmpProcessor::createEditor()
{
    return new AmpEditor (*this);
}

} // namespace orbitamp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new orbitamp::AmpProcessor();
}
