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
        p.on       = apvts.getRawParameterValue (params::eqOn (l));
        p.hpfOn    = apvts.getRawParameterValue (params::eqHpfOn (l));
        p.hpfHz    = apvts.getRawParameterValue (params::eqHpfHz (l));
        p.hpfSlope = apvts.getRawParameterValue (params::eqHpfSlope (l));
        p.loDb     = apvts.getRawParameterValue (params::eqLoDb (l));
        p.loHz     = apvts.getRawParameterValue (params::eqLoHz (l));
        p.hiDb     = apvts.getRawParameterValue (params::eqHiDb (l));
        p.hiHz     = apvts.getRawParameterValue (params::eqHiHz (l));
        p.lpfOn    = apvts.getRawParameterValue (params::eqLpfOn (l));
        p.lpfHz    = apvts.getRawParameterValue (params::eqLpfHz (l));
        p.lpfSlope = apvts.getRawParameterValue (params::eqLpfSlope (l));
        p.level    = apvts.getRawParameterValue (params::eqLevel (l));
        p.b3On     = apvts.getRawParameterValue (params::eqB3On (l));

        for (int b = 0; b < 3; ++b)
        {
            p.bellDb[b] = apvts.getRawParameterValue (params::eqBellDb (l, b));
            p.bellHz[b] = apvts.getRawParameterValue (params::eqBellHz (l, b));
            p.bellQ[b]  = apvts.getRawParameterValue (params::eqBellQ (l, b));
        }
    }

    inTrimParam        = apvts.getRawParameterValue (params::inTrim);
    outTrimParam       = apvts.getRawParameterValue (params::outTrim);
    limiterOnParam     = apvts.getRawParameterValue (params::limiterOn);
    stereoModeParam    = apvts.getRawParameterValue (params::stereoMode);
    cabOnParam         = apvts.getRawParameterValue (params::cabOn);
    boostLevelParam    = apvts.getRawParameterValue (params::blockLevel (params::boostId));
    preampLevelParam   = apvts.getRawParameterValue (params::blockLevel (params::preampId));
    cabIrParam         = apvts.getRawParameterValue (params::cabIr);
    limiterCeilParam   = apvts.getRawParameterValue (params::limiterCeiling);
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
        { BinaryData::ggg_wav,                BinaryData::ggg_wavSize },
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

namespace
{
    struct IrBytes { const char* data; int size; };

    /** The shelf, in cabIrNames order — the embedded orbitcab set. */
    const IrBytes& cabIrBytes (int index)
    {
        static const IrBytes shelf[] = {
            { BinaryData::_01cookiemonster_wav,       BinaryData::_01cookiemonster_wavSize },
            { BinaryData::_02darthgenocider_wav,      BinaryData::_02darthgenocider_wavSize },
            { BinaryData::_03kittenslayer_wav,        BinaryData::_03kittenslayer_wavSize },
            { BinaryData::_04kaijutamer_wav,          BinaryData::_04kaijutamer_wavSize },
            { BinaryData::_05iceburnsuicide_wav,      BinaryData::_05iceburnsuicide_wavSize },
            { BinaryData::_06verticallipstabber_wav,  BinaryData::_06verticallipstabber_wavSize },
            { BinaryData::_07manslaughterjoe_wav,     BinaryData::_07manslaughterjoe_wavSize },
            { BinaryData::_08bigbubba_wav,            BinaryData::_08bigbubba_wavSize },
            { BinaryData::_09devilscunnilingus_wav,   BinaryData::_09devilscunnilingus_wavSize },
            { BinaryData::_10october32th_wav,         BinaryData::_10october32th_wavSize },
            { BinaryData::_11wumbo_wav,               BinaryData::_11wumbo_wavSize },
            { BinaryData::_12worldcollider_wav,       BinaryData::_12worldcollider_wavSize },
            { BinaryData::_13cannibalchoir_wav,       BinaryData::_13cannibalchoir_wavSize },
            { BinaryData::_14cathoderayfleshburn_wav, BinaryData::_14cathoderayfleshburn_wavSize },
            { BinaryData::_15impalerjim_wav,          BinaryData::_15impalerjim_wavSize },
            { BinaryData::_16nachoguacamole_wav,      BinaryData::_16nachoguacamole_wavSize },
            { BinaryData::_17picklepunisher_wav,      BinaryData::_17picklepunisher_wavSize },
            { BinaryData::_18wasabiwarrior_wav,       BinaryData::_18wasabiwarrior_wavSize },
            { BinaryData::_19pestopaladin_wav,        BinaryData::_19pestopaladin_wavSize },
            { BinaryData::_20donspinacio_wav,         BinaryData::_20donspinacio_wavSize },
            { BinaryData::_21killdill_wav,            BinaryData::_21killdill_wavSize },
        };

        return shelf[(size_t) juce::jlimit (0, (int) std::size (shelf) - 1, index)];
    }
} // namespace

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

        // The packs' measured EQ curves are ignored WHOLESALE, his order: a NAM player and
        // nothing else — no FIRs before or after the model. The raw parameter stays in the tree
        // for the day this becomes a choice again; the pump simply stops asking it.
        block.setRaw (true);
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

    // The cabinet IR: choosing one is picking a FILE, so it loads here — the convolution's own
    // background loader swaps it in without a click.
    if (const int ir = juce::roundToInt (cabIrParam->load()); ir != lastCabIr)
    {
        lastCabIr = ir;
        const auto& bytes = cabIrBytes (ir);
        cab.load (bytes.data, (size_t) bytes.size);
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

    for (auto& tap : eqSpectrumTap)
        tap.reset();

    for (auto& tap : blockSpectrumTap)
        tap.reset();

    lastTrimGain = juce::Decibels::decibelsToGain (inTrimParam->load());
    lastOutGain  = juce::Decibels::decibelsToGain (outTrimParam->load());
    lastBoostLevelGain  = juce::Decibels::decibelsToGain (boostLevelParam->load());
    lastPreampLevelGain = juce::Decibels::decibelsToGain (preampLevelParam->load());
    limiter.prepare (sampleRate);

    cab.prepare (sampleRate, block, channels);
    lastCabIr = -1;   // the pump reloads the chosen IR into the freshly prepared engine

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
    const auto slope = [] (float v)
    {
        return params::eqSlopeValues[juce::jlimit (0, params::eqSlopes.size() - 1, juce::roundToInt (v))];
    };

    for (int l = 0; l < params::numEqLinks; ++l)
    {
        const auto& p = eqParams[(size_t) l];

        core::EqLink::Settings s;
        s.hpfOn    = p.hpfOn->load() > 0.5f;
        s.hpfHz    = (double) p.hpfHz->load();
        s.hpfSlope = slope (p.hpfSlope->load());
        s.loDb     = (double) p.loDb->load();
        s.loHz     = (double) p.loHz->load();
        s.b1Db     = (double) p.bellDb[0]->load();
        s.b1Hz     = (double) p.bellHz[0]->load();
        s.b1Q      = (double) p.bellQ[0]->load();
        s.b2Db     = (double) p.bellDb[1]->load();
        s.b2Hz     = (double) p.bellHz[1]->load();
        s.b2Q      = (double) p.bellQ[1]->load();
        s.b3On     = p.b3On->load() > 0.5f;
        s.b3Db     = (double) p.bellDb[2]->load();
        s.b3Hz     = (double) p.bellHz[2]->load();
        s.b3Q      = (double) p.bellQ[2]->load();
        s.hiDb     = (double) p.hiDb->load();
        s.hiHz     = (double) p.hiHz->load();
        s.lpfOn    = p.lpfOn->load() > 0.5f;
        s.lpfHz    = (double) p.lpfHz->load();
        s.lpfSlope = slope (p.lpfSlope->load());
        s.levelDb  = (double) p.level->load();

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

    // The per-stage load meter, orbitcab's recipe verbatim: a cheap monotonic read around each
    // stage, published at the tail as a smoothed share of the block's budget.
    using PerfClock = std::chrono::steady_clock;
    const auto tStart = PerfClock::now();
    auto elapsedNs = [] (PerfClock::time_point a) noexcept
    { return std::chrono::duration<double, std::nano> (PerfClock::now() - a).count(); };
    double nsStage[numStages] {};

    // Clear any output channel the host gave us beyond what the input carries.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // The demo replaces the input rather than mixing into it: a loop you can hear over your own
    // playing is a loop you cannot judge anything by.
    demo.fill (buffer);

    // The input trim FIRST — ahead of the tuner's ear and the gate's key, like the interface
    // knob it stands in for: everything downstream, meters included, hears the trimmed level,
    // which is what closes the gain-staging loop. Ramped per block against zipper noise.
    {
        const float target = juce::Decibels::decibelsToGain (inTrimParam->load());
        buffer.applyGainRamp (0, buffer.getNumSamples(), lastTrimGain, target);
        lastTrimGain = target;
    }

    // The tuner listens HERE — the raw input (or the loop standing in for it), before any block
    // colours it.
    { const auto a = PerfClock::now();
      tunerTap.write (buffer.getReadPointer (0), buffer.getNumSamples());
      nsStage[stTuner] = elapsedNs (a); }

    updateEqSettings();
    updateReverbSettings();

    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // MONO by default: a guitar chain is one signal, and running the WaveNets per channel on a
    // duplicated input was paying twice for the same answer. The whole chain works channel 0;
    // the copy to the other channels happens once, after the limiter. STEREO (the double-track
    // option) restores true per-channel processing.
    const bool stereo = stereoModeParam->load() > 0.5f;
    const int  nch    = stereo ? numChannels : juce::jmin (1, numChannels);

    // The captured blocks take a BUFFER — this alias holds only the channels the chain works.
    juce::AudioBuffer<float> chainView (const_cast<float**> (channels), nch, numSamples);

    // gate -> eq1 -> boost -> eq2 -> preamp -> reverb -> power amp. The gate stands at the very
    // front, right after the tuner's ear: it keys off the raw guitar — the cleanest key there is —
    // and kills the hum before any dirt can multiply it. Its enable crossfade makes the toggle
    // pop-free, so it runs unconditionally and the switch is an argument.
    const auto tGate = PerfClock::now();
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        gateKeyDb.store (juce::Decibels::gainToDecibels (peak, -90.0f));

        if (peak > 1.0f)
            inClip.store (true);   // latched; the IN rail's cap clears it
    }

    // The gate KEYS here whatever it ends up muting — dual detection: the decision is made on the
    // raw input, the attenuation lands where the parameter says. The enable crossfade makes the
    // toggle pop-free, so analyse runs unconditionally and the switch is an argument.
    gate.analyse (channels, nch, numSamples,
                  gateOnParam->load() > 0.5f, gateThresholdParam->load());
    gateMeterDb.store (juce::Decibels::gainToDecibels (gate.currentGain(), -90.0f));

    // The accident latch: pressing while the key stands above the threshold means a live note
    // got chopped — the badge shows the dot until somebody looks.
    if (gate.currentGain() < 0.9f && gateKeyDb.load() > gateThresholdParam->load())
        gateWorked.store (true);

    const bool muteAtStart = juce::roundToInt (gatePosParam->load()) == 0;

    // MUTE at the start: the nonlinearities themselves fall silent between notes.
    if (muteAtStart)
        gate.applyGain (channels, nch, numSamples);

    nsStage[stGate] = elapsedNs (tGate);

    // The EQ links are links of their own, with their own switches: eq1 decides what reaches the
    // first nonlinearity, eq2 colours what the boost made before the preamp distorts it again.
    // Each link's output peak feeds its LEVEL column, and its samples feed the analyser tap —
    // both measured at the link's place in the chain whether it is processing or passing
    // through, because the meters' question is "what leaves this point", not "what did the EQ
    // do".
    const auto meterEqOut = [this, &buffer, nch, numSamples] (int l)
    {
        // nch, not numChannels: in mono mode the other channel is stale until the tail copy.
        float peak = 0.0f;
        for (int c = 0; c < nch; ++c)
            peak = juce::jmax (peak, buffer.getMagnitude (c, 0, numSamples));
        eqOutDb[(size_t) l].store (juce::Decibels::gainToDecibels (peak, -90.0f));

        // A switched-off link keeps its spectrum QUIET: feeding the tap with the bypassed
        // signal claimed the device was doing something. Starved, the pane settles to silence.
        if (eqParams[(size_t) l].on->load() > 0.5f)
        {
            auto& tap = eqSpectrumTap[(size_t) l];
            const float* d = buffer.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
                tap.push (d[i]);
            tap.publishIfDue (eqSpectrumOrder,
                              juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
        }
    };

    { const auto a = PerfClock::now();
      if (eqParams[0].on->load() > 0.5f)
          eqLinks[0].process (channels, nch, numSamples);
      nsStage[stEq1] = elapsedNs (a); }

    meterEqOut (0);

    { const auto a = PerfClock::now();
      if (boostOnParam->load() > 0.5f)
          boost.process (chainView, scopeDry);

      // The block's own LEVEL — the pedal's volume knob, ramped against zipper.
      const float target = juce::Decibels::decibelsToGain (boostLevelParam->load());
      chainView.applyGainRamp (0, numSamples, lastBoostLevelGain, target);
      lastBoostLevelGain = target;

      boostOutDb.store (juce::Decibels::gainToDecibels (
          chainView.getMagnitude (0, 0, numSamples), -90.0f));
      nsStage[stBoost] = elapsedNs (a); }

    if (boostOnParam->load() > 0.5f)
    {
        auto& tap = blockSpectrumTap[0];
        const float* d = buffer.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            tap.push (d[i]);
        tap.publishIfDue (eqSpectrumOrder,
                          juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
    }

    { const auto a = PerfClock::now();
      if (eqParams[1].on->load() > 0.5f)
          eqLinks[1].process (channels, nch, numSamples);
      nsStage[stEq2] = elapsedNs (a); }

    meterEqOut (1);

    { const auto a = PerfClock::now();
      if (preampOnParam->load() > 0.5f)
          preamp.process (chainView, scopeDry);

      const float target = juce::Decibels::decibelsToGain (preampLevelParam->load());
      chainView.applyGainRamp (0, numSamples, lastPreampLevelGain, target);
      lastPreampLevelGain = target;

      preampOutDb.store (juce::Decibels::gainToDecibels (
          chainView.getMagnitude (0, 0, numSamples), -90.0f));
      nsStage[stPreamp] = elapsedNs (a); }

    if (preampOnParam->load() > 0.5f)
    {
        auto& tap = blockSpectrumTap[1];
        const float* d = buffer.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            tap.push (d[i]);
        tap.publishIfDue (eqSpectrumOrder,
                          juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
    }

    // MUTE pre-reverb — the G-String architecture, and the default: everything the chain ADDED
    // (boost hiss, preamp hiss) dies here too, and the reverb tail past it rings out.
    if (! muteAtStart)
        gate.applyGain (channels, nch, numSamples);

    if (reverbOnParam->load() > 0.5f)
        { const auto a = PerfClock::now();
          reverb.process (channels, nch, numSamples);
          nsStage[stReverb] = elapsedNs (a); }
    else
        reverb.reset();   // so re-enabling it does not spill the tail of what was playing before

    power.setTube (static_cast<core::PowerAmp::Tube> (
        juce::jlimit (0, (int) core::PowerAmp::Tube::count - 1, juce::roundToInt (powerTubeParam->load()))));
    power.setTubeCount (juce::roundToInt (powerCountParam->load()) + 1);   // index 0 = one bottle
    power.setDrive (powerDriveParam->load());
    power.setSag (powerSagParam->load());
    { const auto a = PerfClock::now();
      power.process (channels, nch, numSamples, powerOnParam->load() > 0.5f);
      nsStage[stPower] = elapsedNs (a); }

    // The cabinet closes the tone: the IR speaks last, before the master's hand and the safety.
    { const auto a = PerfClock::now();
      cab.process (channels, nch, numSamples, cabOnParam->load() > 0.5f);
      cabOutDb.store (juce::Decibels::gainToDecibels (
          buffer.getMagnitude (0, 0, numSamples), -90.0f));
      nsStage[stCab] = elapsedNs (a); }

    const auto tOut = PerfClock::now();

    // The output trim closes the chain — the master's hand on the way out, ramped per block
    // against zipper noise — and the OUT rail reads the result, clip cap latched past 0 dBFS.
    {
        const float target = juce::Decibels::decibelsToGain (outTrimParam->load());
        buffer.applyGainRamp (0, numSamples, lastOutGain, target);
        lastOutGain = target;

        // The safety after the master's hand: nothing downstream of here touches gain, so the
        // ceiling it enforces is the ceiling that leaves the box. The meter reads AFTER it —
        // the truth on the rail is the truth at the jack.
        { const auto a = PerfClock::now();
          limiter.process (channels, nch, numSamples,
                           limiterOnParam->load() > 0.5f, limiterCeilParam->load());
          nsStage[stLimit] = elapsedNs (a); }
        limiterGrDb.store (juce::Decibels::gainToDecibels (limiter.lastMinGain(), -90.0f));

        // The mono chain becomes the stereo output HERE — one copy, after everything.
        if (! stereo)
            for (int ch = 1; ch < numChannels; ++ch)
                buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

        if (limiter.lastMinGain() < 0.999f)
            limiterWorked.store (true);

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        outDb.store (juce::Decibels::gainToDecibels (peak, -90.0f));

        if (peak > 1.0f)
            outClip.store (true);
    }

    nsStage[stOut]   = elapsedNs (tOut);
    nsStage[stTotal] = elapsedNs (tStart);

    // The per-stage publication: one-pole EMA at orbitcab's coefficient, so the two meters
    // read on the same ruler.
    if (const double budgetNs = getSampleRate() > 0.0
                                    ? (double) numSamples / getSampleRate() * 1.0e9 : 0.0;
        budgetNs > 0.0)
    {
        for (int i = 0; i < numStages; ++i)
        {
            const double pct = nsStage[i] / budgetNs * 100.0;
            stageLoad[i].store (stageLoad[i].load (std::memory_order_relaxed)
                                    + 0.08f * ((float) pct
                                               - stageLoad[i].load (std::memory_order_relaxed)),
                                std::memory_order_relaxed);

            // The worst-hold: a spike an EMA would smooth away is exactly the block that drops.
            if ((float) pct > stageWorst[i].load (std::memory_order_relaxed))
                stageWorst[i].store ((float) pct, std::memory_order_relaxed);
        }

        if (nsStage[stTotal] > budgetNs)
            overruns.fetch_add (1, std::memory_order_relaxed);

        // The strip chart: a column closes every ~33 ms carrying the worst block inside it.
        histWorst = juce::jmax (histWorst, (float) (nsStage[stTotal] / budgetNs * 100.0));
        histSamples += numSamples;

        if (const int bucket = juce::roundToInt (getSampleRate() / 30.0); histSamples >= bucket)
        {
            const int next = (loadHistPos.load (std::memory_order_relaxed) + 1) % loadHistSize;
            loadHist[next].store (histWorst, std::memory_order_relaxed);
            loadHistPos.store (next, std::memory_order_release);
            histWorst   = 0.0f;
            histSamples = 0;
        }
    }

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
