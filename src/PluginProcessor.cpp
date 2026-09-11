// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

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
               [this] (const juce::ValueTree& t)
               {
                   apvts.replaceState (t.createCopy());
                   markSwitchAimsPending();   // a recalled register carries its own named positions
               },
               felitronics::appkit::CompareHistory::Config {}),
      // GitHub's release list for THIS repo, the running version, and the badge's own settings file.
      // The checker asks nothing on its own — see updateChecker().
      updater ({ .ownerRepo      = "darwinscat/orbit-amp",
                 .productName    = "OrbitAmp",
                 .currentVersion = ORBITAMP_VERSION,
                 .settings       = [this] { return updateStore->file(); } })
{
    // One steady pump for the settle timer. 30 Hz with the engine's default settle count means a
    // burst commits about 0.4 s after you stop moving.
    startTimerHz (30);

    for (auto& order : blockSpectrumOrder)
        order.store (eqSpectrumOrder);

    for (int l = 0; l < params::numEqLinks; ++l)
    {
        auto& p = eqParams[(size_t) l];
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
    stereoModeParam    = apvts.getRawParameterValue (params::stereoMode);
    boostInParam       = apvts.getRawParameterValue (params::blockIn (params::boostId));
    preampInParam      = apvts.getRawParameterValue (params::blockIn (params::preampId));
    boostSmoothParam   = apvts.getRawParameterValue (params::blockSmooth (params::boostId));
    preampSmoothParam  = apvts.getRawParameterValue (params::blockSmooth (params::preampId));
    cabIrParam         = apvts.getRawParameterValue (params::cabIr);
    cabHpfOnParam      = apvts.getRawParameterValue (params::cabHpfOn);
    cabHpfHzParam      = apvts.getRawParameterValue (params::cabHpfHz);
    cabHpfSlopeParam   = apvts.getRawParameterValue (params::cabHpfSlope);
    cabLpfOnParam      = apvts.getRawParameterValue (params::cabLpfOn);
    cabLpfHzParam      = apvts.getRawParameterValue (params::cabLpfHz);
    cabLpfSlopeParam   = apvts.getRawParameterValue (params::cabLpfSlope);
    cabTrimOnParam     = apvts.getRawParameterValue (params::cabTrimOn);
    cabTrimParam       = apvts.getRawParameterValue (params::cabTrim);
    cabPhaseParam      = apvts.getRawParameterValue (params::cabPhase);
    limiterCeilParam   = apvts.getRawParameterValue (params::limiterCeiling);
    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];
        rowOn[(size_t) i]      = link.onParam      != nullptr ? apvts.getRawParameterValue (link.onParam)      : nullptr;
        rowPresent[(size_t) i] = link.presentParam != nullptr ? apvts.getRawParameterValue (link.presentParam) : nullptr;
    }

    gateThresholdParam = apvts.getRawParameterValue (params::gateThreshold);
    gatePosParam       = apvts.getRawParameterValue (params::gatePos);
    gateDecayParam     = apvts.getRawParameterValue (params::gateDecay);

    delaySyncParam    = apvts.getRawParameterValue (params::delaySync);
    delayTimeMsParam  = apvts.getRawParameterValue (params::delayTimeMs);
    delayDivParam     = apvts.getRawParameterValue (params::delayDiv);
    delayBpmParam     = apvts.getRawParameterValue (params::delayBpm);
    delayRepeatsParam = apvts.getRawParameterValue (params::delayRepeats);
    delayDarkParam    = apvts.getRawParameterValue (params::delayDark);
    delayOffsetParam  = apvts.getRawParameterValue (params::delayOffset);
    delayMixParam     = apvts.getRawParameterValue (params::delayMix);

    reverbTypeParam = apvts.getRawParameterValue (params::reverbType);
    reverbMixParam  = apvts.getRawParameterValue (params::reverbMix);
    reverbDecayParam    = apvts.getRawParameterValue (params::reverbDecay);
    reverbPredelayParam = apvts.getRawParameterValue (params::reverbPredelay);
    reverbHpfHzParam    = apvts.getRawParameterValue (params::reverbHpfHz);

    packCompParam    = apvts.getRawParameterValue (params::packLevelComp);
    boostGainParam  = apvts.getRawParameterValue (params::boostGain);
    preampGainParam = apvts.getRawParameterValue (params::preampGain);

    rescanDevices();

    // The names go in BEFORE the history looks, so a plugin that has just opened already knows
    // which device it is playing — and knows it without that knowledge being an edit. Resetting
    // takes the baseline over the seeded tree; marking it saved undoes the dirty flag the reset
    // raises, which is right: nothing has been changed, the state was merely completed.
    seedSwitchNames();
    history.reset();
    history.markSaved();

    // The two links that HAVE a tail are told what their knobs say before anyone can ask how long
    // this rings. `prepareToPlay` does this too, but a VST3 host may ask before it activates the
    // plugin at all, and an answer built out of the stages' constructor defaults is not an answer.
    updateDelaySettings();
    updateReverbSettings();
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Switches and devices ride in the tree by NAME, and the names are written when they MOVE, so
    // a save normally has nothing to do here. The one gap is the tick: a control moved in the
    // 33 ms before the pump next runs would be saved as a new NUMBER beside its old NAME, and the
    // name wins on the way back — so the session would reopen one position behind. Closing that
    // gap is this pair, and it writes only what the move already earned.
    //
    // The device pump goes FIRST and it is not optional: naming a device from what is loaded,
    // when the number has moved and nothing has loaded it yet, writes the name of the device that
    // is leaving beside the number of the one arriving — which is worse than the gap it closes.
    //
    // Guarded because a host may save from any thread and a ValueTree is the message thread's.
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        pumpDeviceWork();
        pumpSwitchNames();
    }


    // The workspace envelope carries the live parameter tree and the other three registers, so
    // a reopened session comes back with all four sounds. NOT the undo stacks: CompareHistory's
    // envelope holds the live capture and the register snapshots and nothing else, so undo starts
    // fresh on reopen. (It said otherwise here for a long time — it never did.)
    if (auto xml = history.toTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    stateWasRestored = true;   // a session's choice outranks the environment's default

    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    const auto apply = [] (AmpProcessor& self, const juce::ValueTree& t)
    {
        // The aim's bookkeeping belongs to the thread that runs it. Arming it here rather than at
        // the top of setStateInformation also means it is armed AFTER the tree has landed, which
        // is what lets it take the restored values as its baseline instead of the outgoing ones.
        if (self.history.fromTree (t))
        {
            self.markSwitchAimsPending();
            return;
        }

        // Sessions saved before the workspace existed hold a bare parameter tree. Load the sound
        // and start a fresh history around it rather than dropping the session on the floor.
        if (t.hasType (self.apvts.state.getType()))
        {
            self.apvts.replaceState (t);
            self.history.reset();
            self.markSwitchAimsPending();   // after the tree lands, like the path above
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
    const int n = (int) std::size (params::demoLoopFiles);
    demo.setLoop (params::demoLoopsDir().getChildFile (params::demoLoopFiles[juce::jlimit (0, n - 1, index)]),
                  getSampleRate());
}

void AmpProcessor::rescanDevices()
{
    // Each block asks for its own kind. A preamp offered as a pedal is not a wrong sound, it is a
    // wrong LIST — the block says what it is for, and the list has to agree with it.
    // And the block STAYS on the device it is playing: a rescan happens when a pack has just been
    // imported, an import re-sorts the list, and re-selecting by the old number is how a block ends
    // up playing its new neighbour. The block answers where it now stands and the parameter follows
    // it — quietly, because nothing about the sound changed and there is nothing to undo.
    // What the rescan wants to change, decided before anything is written. The suppression scope
    // has a cost of its own — it commits whatever burst is open when it starts and can drop a redo
    // when it ends — so it is opened only when there is actually something to suppress, and a
    // rescan that changes nothing (the usual one) leaves the timeline exactly as it found it.
    std::vector<std::pair<juce::RangedAudioParameter*, float>> writes;
    bool nameFirstDevice[numCaptured] = { false, false };

    for (size_t b = 0; b < numCaptured; ++b)
    {
        const auto* id = deviceIdOf (b);
        auto* p = apvts.getParameter (id);
        const int was = juce::roundToInt (apvts.getRawParameterValue (id)->load());
        const int now = blockAt (b).rescan (was);

        // Out of the parameter's reach: a folder can hold more devices than this can address, and
        // writing a clamped number would land on the neighbour. Nothing good happens past here —
        // the block is playing the right pack for one moment and the next pump reloads whatever
        // the unchanged number points at — but a clamped write would be wrong immediately and
        // permanently. A folder of more than a hundred and twenty-eight devices is a limit of the
        // parameter, and it is on the list to be lifted rather than papered over.
        if (now >= 0 && now != was && now < params::maxDevices && p != nullptr)
            writes.emplace_back (p, p->convertTo0to1 ((float) now));

        // A device that has a name for the first time: the folder was empty when this instance
        // was built, so the seed had nothing to write, and nothing has MOVED since — the parameter
        // still reads the same number it always did. Without this the first pack a player installs
        // is saved by number until they change the selector, which is the whole disease.
        nameFirstDevice[b] = ! apvts.state.hasProperty (juce::Identifier (juce::String (id) + deviceAimSuffix))
                             && blockAt (b).selectedName().isNotEmpty();
    }

    if (! writes.empty() || nameFirstDevice[0] || nameFirstDevice[1])
    {
        // Following a device that the FOLDER moved is the plugin agreeing with itself, not an
        // edit: recorded, it would be an undo step whose undo puts the number back on the wrong
        // pack.
        const felitronics::appkit::CompareHistory::ScopedSuppress hush (history);

        for (auto& [p, v] : writes)
            p->setValueNotifyingHost (v);

        for (size_t b = 0; b < numCaptured; ++b)
            if (nameFirstDevice[b])
                noteBlockNames (b, false);   // the rescan has just selected: they agree
    }

    // A NAME THAT CAN BE HONOURED NOW. A session opened before its pack was installed keeps the
    // name it could not reach; installing the pack is a rescan, and this is the moment the name
    // becomes answerable. Without it the player would have to reopen the session to hear the
    // device they just installed.
    for (size_t b = 0; b < numCaptured; ++b)
    {
        const juce::Identifier key (juce::String (deviceIdOf (b)) + deviceAimSuffix);
        const auto want = apvts.state.getProperty (key).toString();

        if (want.isNotEmpty() && want != blockAt (b).selectedName()
            && blockAt (b).indexOfName (want) >= 0)
        {
            markSwitchAimsPending();
            break;
        }
    }
}

const AmpProcessor::IrBytes& AmpProcessor::cabIrBytes (int index)
{
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
}


void AmpProcessor::pumpDeviceWork()
{
    auto pump = [this] (auto& block, auto& gainParam, auto& smoothParam, auto measuredId, const char* blk)
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

        // WHOSE tone controls this block wears, which is the same question as whether the pack's
        // measured curves are in the signal at all. OURS bypasses them because our parametric is
        // standing in their place; NATIVE plays them. They are alternatives, never layers.
        const auto mode = static_cast<params::EqMode> (juce::roundToInt (
            apvts.getRawParameterValue (params::blockEqMode (blk))->load()));

        block.setRaw (mode == params::EqMode::ours);
        block.applySelectors (selectors);
        block.setGain (gainParam->load(), smoothParam->load() > 0.5f);

        std::array<float, (size_t) core::CapturedBlock::numMeasured> values {};
        for (int i = 0; i < (int) values.size(); ++i)
            values[(size_t) i] = apvts.getRawParameterValue (measuredId (i))->load();

        block.updateToneIfMoved (values);

        // The player's housekeeping and its load jobs — on the pool, or right here for a driver
        // that has no message loop to bring them back on.
        block.pump (inlineLoads ? nullptr : &modelPool);
    };

    pump (boost,    boostGainParam,  boostSmoothParam,  params::boostMeasured,  params::boostId);
    pump (preamp,   preampGainParam, preampSmoothParam, params::preampMeasured, params::preampId);

    // A model that landed may carry rate-matching the host has to know about.
    reportLatency();

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

    // ...and what the player does to it: baked into the IR off this pump, so the convolution
    // never learns of it. Rebuilt only when something moved.
    {
        core::CabinetIr::Post post;
        post.trimOn       = cabTrimOnParam->load() > 0.5f;
        post.trimFraction = cabTrimParam->load();
        const auto slopeDb = [] (float v)
        { return params::eqSlopeValues[juce::jlimit (0, (int) std::size (params::eqSlopeValues) - 1,
                                                     juce::roundToInt (v))]; };
        post.hpfOn        = cabHpfOnParam->load() > 0.5f;
        post.hpfHz        = cabHpfHzParam->load();
        post.hpfSlope     = slopeDb (cabHpfSlopeParam->load());
        post.lpfOn        = cabLpfOnParam->load() > 0.5f;
        post.lpfHz        = cabLpfHzParam->load();
        post.lpfSlope     = slopeDb (cabLpfSlopeParam->load());
        post.phase        = cabPhaseParam->load() > 0.5f;
        cab.setPost (post);
    }
}

void AmpProcessor::fitWire (int index, int lat)
{
    // ALLOCATE HERE, ADOPT THERE. This builds the bigger rows on the message thread, where nothing
    // waits on it; the audio thread takes them itself, at the top of its next block, with pointer
    // swaps and one copy of the window and no allocation at all.
    //
    // 🔴 AND NO LOCK, on purpose. The first version of this held `getCallbackLock()` for the swap —
    // which VST3, AU and the standalone do hold around `processBlock`, and **CLAP does not**: the
    // shipped wrapper calls it with no lock of any kind. A lock only some of the shipped formats
    // take is not a lock, it is a data race with a comment on it.
    //
    // `suspendProcessing` was the other short answer and is also wrong: it hands the host a block
    // of silence, which is exactly the click the warm window was added to remove. Trading a comb
    // for a hole is not a fix.
    (void) wire[(size_t) index].reserve (lat);
}

void AmpProcessor::reportLatency()
{
    // In series: each captured block's models — a capture taken at another rate is resampled on
    // the way in and out, and that has a length. TWO stages sum here, so a 44.1 kHz session on
    // 48 kHz packs reports 122 samples, and a 96 kHz one 192. It was eight until the kernel behind
    // the rate match was replaced; the number is the resampler's geometry and it will move again,
    // which is why nothing downstream of here may write it down.
    const int bo = boost.latencySamples();
    const int pr = preamp.latencySamples();
    const int total = bo + pr;

    // 🔴 THE WIRE IS SIZED FROM THE NUMBER THE BLOCK ACTUALLY REPORTS, here and nowhere else.
    // `latencySamples()` came out of `NamStage::rateMatch` and therefore out of the rates that are
    // really in play; every ceiling this class used to carry was computed from a model rate NOBODY
    // PROMISED, and such a ceiling is not too low, it is wrong — the run rate can walk, without
    // limit while audio runs, and no static number survives that.
    fitWire (0, bo);
    fitWire (1, pr);

    // THE WIRE'S REFUSAL, PICKED UP OFF THE AUDIO THREAD. `BypassWire` cannot say anything from
    // inside `process` without allocating or writing to a stream on the audio thread, so it
    // latches a flag and this pump — the same one that noticed the latency — is what reads it.
    // Once, and it says WHAT WAS ASKED rather than what probably happened. An earlier draft of this
    // line explained every refusal as a moment's lag and announced that the wire had caught up —
    // which is true of the ordinary case and false of the one that matters: a delay past
    // `maxSaneDelay` is shortened for good, pump or no pump, and the two read identically once the
    // message stops naming numbers. So both numbers go in, and whether it fits now is a comparison
    // the reader can make instead of a claim they have to take.
    if (! wireRefusalSeen)
        for (int i = 0; i < 2; ++i)
            if (wire[(size_t) i].everShortened())
            {
                wireRefusalSeen = true;

                const int asked   = i == 0 ? bo : pr;
                const int carries = wire[(size_t) i].capacity();

                juce::Logger::writeToLog (
                    "OrbitAmp: bypass wire " + juce::String (i) + " was asked for more delay than it"
                    " carried, at " + juce::String (getSampleRate(), 0) + " Hz. It was asked for "
                      + juce::String (asked) + " and now carries " + juce::String (carries)
                      + (carries >= asked ? " — it has caught up, so the shortfall was the blocks"
                                            " between a model landing and this pump."
                                          : " — it has NOT caught up, so the shortfall is standing."));
                break;
            }

    if (total != getLatencySamples())
        setLatencySamples (total);
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

    for (auto& tap : blockSpectrumTap)
        tap.reset();
    for (auto& tap : blockInSpectrumTap)
        tap.reset();
    for (auto& tap : reverbSpectrumTap)
        tap.reset();

    // Seeded through the same question the chain asks, not from the parameter alone: a link that
    // arrives standing by must not apply its stored gain to the first block and then ramp out of
    // it — a bypassed +24 dB would open playback at nearly sixteen times.
    lastTrimGain = linkWorks (params::rowIn)  ? juce::Decibels::decibelsToGain (inTrimParam->load())  : 1.0f;
    lastOutGain  = linkWorks (params::rowOut) ? juce::Decibels::decibelsToGain (outTrimParam->load()) : 1.0f;
    lastBoostInGain  = juce::Decibels::decibelsToGain (boostInParam->load());
    lastPreampInGain = juce::Decibels::decibelsToGain (preampInParam->load());
    limiter.prepare (sampleRate);

    cab.prepare (sampleRate, block, channels);
    lastCabIr = -1;   // the pump reloads the chosen IR into the freshly prepared engine

    // Seeded from the parameter: a session saved gate-ON has to start already gated, not fade in
    // over a block of ungated hum.
    gate.prepare (sampleRate, block, channels);
    gate.seedEnabled (linkWorks (params::rowGate));

    // The mode's environment default — see modeAutoValue.
    if (! stateWasRestored)
    {
        const auto want = wrapperType == wrapperType_Standalone ? params::StereoMode::stereoSpace
                        : getTotalNumInputChannels() >= 2       ? params::StereoMode::stereo
                                                                : params::StereoMode::mono;

        if (juce::roundToInt (stereoModeParam->load()) == modeAutoValue && (int) want != modeAutoValue)
        {
            if (auto* p = apvts.getParameter (params::stereoMode))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) want));
            modeAutoValue = (int) want;
        }
    }

    delay.prepare (sampleRate, block);
    reverb.prepare (sampleRate, block);
    boost.prepare (sampleRate, block, channels);
    preamp.prepare (sampleRate, block, channels);
    demo.prepare (sampleRate);
    scopeDry.setSize (1, block);
    fadeDry.setSize (juce::jmax (2, channels), block);

    // ONE BLOCK OF PAST, AND THAT IS A FLOOR ON MEMORY — not a ceiling on delay. The two are
    // different numbers and confusing them is the defect this branch removed: what the wire has to
    // CARRY is the delay its block reports, which does not exist at this line (the models are
    // pumped below) and is `reportLatency`'s to fit. What the wire has to REMEMBER is another
    // matter — a wire holding nothing has nothing to bring across when it grows, so a delay born
    // mid-session would open on silence however promptly the growth happened.
    //
    // A block is the honest amount, because it is the one length the host actually promised us and
    // it costs exactly what a block costs. It bounds nothing: a delay born longer than this still
    // grows the wire, it just brings less across, and that residual is measured rather than
    // claimed away.
    for (auto& w : wire)
        w.prepare (block);

    // AND THE WIRE IS TOLD THE MOMENT A MODEL LANDS, not at the next tick. `deliver` runs on the
    // message thread inside the loader's own callback; the 30 Hz pump that used to be the only
    // place this happened is up to 33 ms later, which at 96 kHz is about fifty blocks of bypass
    // path short by the difference — or a whole fifteen-millisecond crossfade combing inside that
    // window. Reassigned on every prepare because `this` is what they capture.
    boost .onLanded = [this] { reportLatency(); };
    preamp.onLanded = [this] { reportLatency(); };

    // Snapped, not faded: a chain that arrives switched off is silent from its first sample.
    for (int i = 0; i < params::numChainRows; ++i)
    {
        blockFade[(size_t) i].prepare (sampleRate);
        blockFade[(size_t) i].snapTo (linkWorks ((params::ChainRow) i));
    }

    pumpDeviceWork();


    // Reported ALWAYS, whether a block is switched on or not: a bypass path carries the same
    // delay, so a toggle never shifts the timing of everything downstream. A latency that changes
    // with a switch is what makes hosts re-align mid-song.
    reportLatency();

    updateEqSettings();
    updateDelaySettings();
    updateReverbSettings();
}

void AmpProcessor::updateDelaySettings() noexcept
{
    // The time: the division against the conducting tempo when sync is on, the free knob when it
    // is off. The host's tempo outranks the BPM field — the field exists for the standalone,
    // where nobody else is counting.
    float ms = delayTimeMsParam->load();

    if (delaySyncParam->load() > 0.5f)
    {
        double bpm = (double) delayBpmParam->load();

        if (auto* ph = getPlayHead())
            if (const auto pos = ph->getPosition())
                if (const auto hostBpm = pos->getBpm(); hostBpm.hasValue() && *hostBpm > 0.0)
                    bpm = *hostBpm;

        const int div = juce::jlimit (0, params::delayDivisions.size() - 1,
                                      juce::roundToInt (delayDivParam->load()));
        ms = (float) ((double) params::delayDivisionBeats[div] * 60000.0 / bpm);
    }

    delay.setTimeMs (ms);
    delay.setRepeats (delayRepeatsParam->load() * 0.01f);   // the face reads percent

    // DARK is a percent of darkness; the corner it buys falls log-evenly from the bright
    // ceiling to the dark floor, so every degree of the knob darkens by the same ear-step.
    delay.setDarkHz (params::delayDarkHiHz
                     * std::pow (params::delayDarkLoHz / params::delayDarkHiHz,
                                 delayDarkParam->load() * 0.01f));

    delay.setOffsetMs (delayOffsetParam->load());
    delay.setMix (delayMixParam->load() * 0.01f);
}

void AmpProcessor::updateReverbSettings() noexcept
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1,
                                    juce::roundToInt (reverbTypeParam->load()));

    reverb.setCharacter (static_cast<core::ReverbStage::Character> (index));
    reverb.setMix (reverbMixParam->load() * 0.01f);   // the face reads percent
    reverb.setDecay (reverbDecayParam->load());
    reverb.setPredelayMs (reverbPredelayParam->load());
    reverb.setHpfHz (reverbHpfHzParam->load());
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

        // WHOSE TONE STACK IS IN THE SIGNAL. The two are alternatives, and until now only half of
        // that was true: `setRaw` parked the device's curves when ours was chosen, but ours went on
        // filtering when the device's was — active, and with no controls on the face, because the
        // console had swapped its row for the device's knobs. A shape somebody dialled in and left
        // behind kept playing where nothing could reach it.
        //
        // Only the TONE sections step aside. The cut filters and the level belong to the BLOCK
        // whichever tone is playing: the walls are utility, and the level is the block's output —
        // the grip on the OUT meter writes it, and it would go dead here.
        //
        // Asked of the block, not of the parameter alone: a device that measured nothing has no
        // native stack to hand over, its face collapses the choice to ours, and the DSP has to
        // reach the same verdict or the block ends up with no tone stack at all.
        const auto mode = static_cast<params::EqMode> (juce::roundToInt (
            apvts.getRawParameterValue (params::blockEqMode (params::eqBlockId (l)))->load()));

        const auto& blockFor = l == 0 ? boost : preamp;

        if (mode == params::EqMode::native && blockFor.hasWearableTone())
        {
            s.loDb = 0.0;
            s.b1Db = 0.0;
            s.b2Db = 0.0;
            s.b3On = false;
            s.b3Db = 0.0;
            s.hiDb = 0.0;
        }

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
    // A volume that is not working ramps to UNITY — it is never simply skipped. Skipping leaves
    // the last gain applied to the previous block and none to this one, which is a step in the
    // waveform, which is a click. The same shape the captured blocks' trims already use.
    {
        const float target = linkWorks (params::rowIn)
                                 ? juce::Decibels::decibelsToGain (inTrimParam->load()) : 1.0f;
        buffer.applyGainRamp (0, buffer.getNumSamples(), lastTrimGain, target);
        lastTrimGain = target;
    }

    // The tuner listens HERE — the raw input (or the loop standing in for it), before any block
    // colours it.
    { const auto a = PerfClock::now();
      if (linkWorks (params::rowTuner))
          tunerTap.write (buffer.getReadPointer (0), buffer.getNumSamples());
      nsStage[stTuner] = elapsedNs (a); }

    updateEqSettings();
    updateDelaySettings();
    updateReverbSettings();

    // The pack's input trims, as a flag the players read at their own gains — one atomic store
    // per block per player, live from the next block.
    {
        const bool comp = packCompParam->load() > 0.5f;
        boost.setInputTrims (comp);
        preamp.setInputTrims (comp);
    }

    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // MONO by default: a guitar chain is one signal, and running the WaveNets per channel on a
    // duplicated input was paying twice for the same answer. The whole chain works channel 0;
    // the copy to the other channels happens once, after the limiter. STEREO (the double-track
    // option) restores true per-channel processing. STEREO SPACE splits the chain in two: mono
    // up to the delay — `nch` — and stereo from the delay on — `nchBack` — with the one copy
    // made at the seam, so the space is wide and the amp is paid for once. The seam stands BEFORE
    // the delay, not before the reverb: the delay's OFFSET is a stereo of its own, and a spread
    // the room then works on is wider than a spread the room has to make alone.
    const auto mode = static_cast<params::StereoMode> (
        juce::jlimit (0, params::stereoModes.size() - 1, juce::roundToInt (stereoModeParam->load())));
    const int nch     = mode == params::StereoMode::stereo ? numChannels : juce::jmin (1, numChannels);
    const int nchBack = mode == params::StereoMode::mono   ? nch         : numChannels;

    // The captured blocks take a BUFFER — this alias holds only the channels the chain works.
    juce::AudioBuffer<float> chainView (const_cast<float**> (channels), nch, numSamples);

    // gate -> boost -> EQ -> preamp -> EQ -> delay -> reverb. The gate stands at the very
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
                  linkWorks (params::rowGate), gateThresholdParam->load());
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

    // A captured block, whole: the capture, then its own EQ, then its own volume.
    //
    // The EQ sits AFTER the nonlinearity because that is what it is for here — colouring what the
    // device made, not deciding what the device eats. The boost's lands in front of the preamp,
    // which is where a real amplifier keeps its tone stack; the preamp's stands after the last
    // nonlinearity, colouring what the amp made before the room and the speaker have it.
    //
    // It also goes dark with the block. The EQ is part of the block now, not a link that happens to
    // be drawn inside one, and a switch that leaves half of what it names still cutting is a switch
    // nobody can trust.
    //
    // LEVEL comes last for the same reason: it is the block's output volume, and the OUT meter
    // beside it has to be metering everything the block did — the EQ included.
    const auto captured = [&] (auto& blk, int l, params::ChainRow row,
                               std::atomic<float>* inParam, float& lastInGain,
                               std::atomic<float>& blockOutDb, int stBlock, int stEq)
    {
        // A link that REPLACES the signal has no tail of its own to ride out, so dropping it out
        // of the path would be a step in the waveform. It is crossfaded against what it was
        // handed instead — and while nothing is moving the crossfade costs nothing at all: no
        // copy, no per-sample multiply, just the plain `if` this used to be.
        const bool works = linkWorks (row);
        const auto span  = blockFade[(size_t) row].advance (numSamples, works);
        const bool on    = core::BypassFade::runs (span, works);
        const bool fade  = span.moving && canFade (numSamples, nch);

        // THE WIRE a bypassed block has to BE — and a wire the same LENGTH as the block it
        // replaces. A rate-matching model reports a latency the host compensates for; drop it out
        // of the path and the signal arrives early by exactly that much, for as long as it is
        // bypassed. Sixty-one samples at 44.1 kHz against a 48 kHz pack, ninety-six at 96 kHz, and
        // none at all when the pack's rate is the session's — which is the usual case, and why
        // this costs nothing there.
        //
        // It is also what makes the crossfade honest: blending a block's output against an
        // UNDELAYED copy of its own input is blending a signal with an early copy of itself, and
        // that is a comb — sixty-one samples at 44.1 kHz puts the first notch near 362 Hz, in the
        // body of the guitar. Swept over fifteen milliseconds it reads as a tick rather than a
        // filter, but it is a tick that need not exist.
        //
        // Taken BEFORE this block's own IN trim: the dry end has to be the signal as it arrived.
        // Copied after the trim, the two ends of the fade differed by the trim as well as by the
        // block, and at -24 dB that is not a click, it is a bark.
        const int lat = blk.latencySamples();

        if (fade)
        {
            // `on` is always true while a fade runs, so this is the only place the dry is kept.
            if (lat > 0)
                wire[(size_t) l].process (chainView.getArrayOfReadPointers(),
                                          fadeDry.getArrayOfWritePointers(), nch, numSamples, lat);
            else
            {
                for (int ch = 0; ch < nch; ++ch)
                    juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                       chainView.getReadPointer (ch), numSamples);

                wire[(size_t) l].advance (chainView.getArrayOfReadPointers(), nch, numSamples);
            }
        }
        else if (on)
        {
            // Fully in the path: the model carries its own delay and there is nothing to imitate.
            // The wire is still FED, though, and that is the whole difference between a wire and a
            // hole. It used to be cleared here — the worry being a ghost, a handful of samples from
            // minutes ago read by the next fade-out — but a window that is never filled is not
            // ghost-free, it is COLD, and a cold window hands out its own length in silence the
            // first time the delay becomes real. Fed every block there is neither: what it holds
            // is always the signal that just went past, which is exactly what a bypass wire is.
            wire[(size_t) l].advance (chainView.getArrayOfReadPointers(), nch, numSamples);
        }
        else if (lat > 0)
        {
            // Standing by for good: the block is not run at all below, so the signal in the buffer
            // IS the output — and it has to carry the delay the model would have.
            wire[(size_t) l].process (chainView.getArrayOfReadPointers(),
                                      chainView.getArrayOfWritePointers(), nch, numSamples, lat);
        }
        else
        {
            // Bypassed AND costing nothing — the pack plays at the session's own rate, so there is
            // no delay to imitate yet. Fed anyway: a model landing at another rate turns `lat`
            // positive between two blocks, and the block after that has to be able to look back.
            wire[(size_t) l].advance (chainView.getArrayOfReadPointers(), nch, numSamples);
        }

        // IN: how hard the capture is fed. Metered immediately after, at the model's own door, so
        // the grip on the meter and the fill under it answer about one point.
        //
        // It applies only while the block is being HEARD — which includes the whole fade, so the
        // trim stays where the player put it for as long as any of the model is in the sum. Once
        // the fade is over the block is not run at all and this buffer IS the through-path: a
        // trim on it would be a trim on the bypass, which is not what a bypass is.
        //
        // The `lastInGain` reset is the whole point and cost me a click to learn. Ramping to unity
        // on the first fully-bypassed block ramps the DRY signal from the trim down to 1.0 — at
        // +12 dB that is a four-times burst decaying over one block, which is exactly the kind of
        // step this crossfade exists to remove. There is nothing to ramp: the previous block's
        // trim was applied to what fed the model, and the blend has already weighed that away.
        // So the gain simply IS unity here, with no ramp and no memory of the trim.
        if (on)
        {
            const float inTarget = juce::Decibels::decibelsToGain (inParam->load());

            // ARRIVING, not sliding. Coming back from a bypass the trim would otherwise ramp from
            // unity to what the player set across ONE block — and a block can be longer than the
            // fade. At 2048 samples the crossfade is over by sample 720, so the model spends the
            // rest of the block fully audible and still climbing toward the trim: a drive swell,
            // which is the same family of artefact as the burst this replaced. There is nothing to
            // ramp INTO: the blend weighs this path at nothing for the first sample, so the trim
            // simply starts where it belongs.
            if (! blockWasOn[(size_t) l])
                lastInGain = inTarget;

            chainView.applyGainRamp (0, numSamples, lastInGain, inTarget);
            lastInGain = inTarget;
        }
        else
        {
            lastInGain = 1.0f;   // a bypassed block is a wire, and a wire has no trim to ramp from
        }

        blockWasOn[(size_t) l] = on;

        // A block that is not working meters nothing: two full passes over the buffer for a needle
        // nobody reads, and a needle still moving on a dark face is the exception this whole rework
        // set out to delete.
        blockInDb[(size_t) l].store (on ? juce::Decibels::gainToDecibels (
                                              chainView.getMagnitude (0, 0, numSamples), -90.0f)
                                        : -90.0f);

        { const auto a = PerfClock::now();
          if (on)
              blk.process (chainView, scopeDry);
          nsStage[stBlock] = elapsedNs (a); }

        { const auto a = PerfClock::now();
          // No enable of its own: the block's switch is the EQ's switch, and a flat link is
          // bit-transparent, so a console standing at zero costs what it looks like it costs.
          // What the EQ eats, tapped before the console runs — the "before" of the pane's pair.
          if (on)
          {
              auto& tin = blockInSpectrumTap[(size_t) l];
              const float* d = buffer.getReadPointer (0);
              for (int i = 0; i < numSamples; ++i)
                  tin.push (d[i]);
              tin.publishIfDue (blockSpectrumOrder[(size_t) l].load (std::memory_order_relaxed),
                                juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
          }

          if (on)
              eqLinks[(size_t) l].process (channels, nch, numSamples);
          nsStage[stEq] = elapsedNs (a); }

        if (fade)
            core::BypassFade::blend (chainView.getArrayOfWritePointers(),
                                     fadeDry.getArrayOfReadPointers(), nch, numSamples, span);

        blockOutDb.store (on ? juce::Decibels::gainToDecibels (
                                   chainView.getMagnitude (0, 0, numSamples), -90.0f)
                             : -90.0f);

        // ONE tap per block, at the block's output, which is now also the EQ's output — the curve
        // and the spectrum drawn under it finally describe the same point. A switched-off block
        // keeps it QUIET: feeding a bypassed signal in claimed the device was doing something, and
        // starved, the pane settles to silence.
        if (on)
        {
            auto& tap = blockSpectrumTap[(size_t) l];
            const float* d = buffer.getReadPointer (0);
            for (int i = 0; i < numSamples; ++i)
                tap.push (d[i]);
            // The face's own resolution: the pair follows the picture that is up over it.
            tap.publishIfDue (blockSpectrumOrder[(size_t) l].load (std::memory_order_relaxed),
                              juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
        }
    };

    captured (boost,  0, params::rowBoost,  boostInParam,  lastBoostInGain,  boostOutDb,  stBoost,  stEq1);
    captured (preamp, 1, params::rowPreamp, preampInParam, lastPreampInGain, preampOutDb, stPreamp, stEq2);

    // MUTE pre-reverb — the G-String architecture, and the default: everything the chain ADDED
    // (boost hiss, preamp hiss) dies here too, and the reverb tail past it rings out.
    if (! muteAtStart)
        gate.applyGain (channels, nch, numSamples);

    // THE SEAM: where a mono chain becomes a stereo one, in STEREO SPACE — the one copy, right
    // before the wide stages: the delay spreads the two identical channels first, the reverb
    // rooms what it made.
    for (int ch = nch; ch < nchBack; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    // The echo before the space: repeats of what the preamp made, which the reverb then rooms.
    // First of the wide stages — the OFFSET is the block's stereo, so it works the back half's
    // channels.
    // STANDBY is the insert's bypass: the block keeps running, it just stops being fed, and the
    // repeats already in the line ring out into the dry. Only leaving the RIG clears it — that is
    // unplugging, not standing by. Which is also why a bypassed block still costs: so does a
    // bypassed insert, in every DAW there is.
    // STANDBY is unfed-and-ringing (see the engine). Leaving the RIG is the other thing: the line
    // is cleared, and a ringing tail cut in one sample is a click. So the whole contribution is
    // crossfaded out first, and only then cleared.
    {
        const bool inRig = linkInRig (params::rowDelay);
        const auto span  = blockFade[(size_t) params::rowDelay].advance (numSamples, inRig);
        const bool run   = core::BypassFade::runs (span, inRig);
        const bool fade  = span.moving && canFade (numSamples, nchBack);

        if (run)
        {
            const auto a = PerfClock::now();

            if (fade)
                for (int ch = 0; ch < nchBack; ++ch)
                    juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                       buffer.getReadPointer (ch), numSamples);

            delay.process (channels, nchBack, numSamples, linkWorks (params::rowDelay));

            if (fade)
                core::BypassFade::blend (channels, fadeDry.getArrayOfReadPointers(),
                                         nchBack, numSamples, span);

            nsStage[stDelay] = elapsedNs (a);
        }
        else
        {
            delay.reset();
        }
    }

    // The room stands by the same way the echo does: unfed, still ringing out — and leaves the rig
    // the same way too, faded rather than cut. See the delay above.
    const bool reverbInRig = linkInRig (params::rowReverb);
    const auto reverbSpan  = blockFade[(size_t) params::rowReverb].advance (numSamples, reverbInRig);
    const bool reverbFade  = reverbSpan.moving && canFade (numSamples, nchBack);

    if (core::BypassFade::runs (reverbSpan, reverbInRig))
        { const auto a = PerfClock::now();
          const bool fed = linkWorks (params::rowReverb);

          if (reverbFade)
              for (int ch = 0; ch < nchBack; ++ch)
                  juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                     buffer.getReadPointer (ch), numSamples);

          // The pair for the block's picture: the door before the room, the ADDED wet after it.
          // A room nobody is feeding hears silence at its door, and the picture says so.
          { auto& tin = reverbSpectrumTap[0];
            const float* d = channels[0];
            for (int i = 0; i < numSamples; ++i)
                tin.push (fed ? d[i] : 0.0f);
            tin.publishIfDue (eqSpectrumOrder,
                              juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0)); }

          reverb.process (channels, nchBack, numSamples, fed);

          { auto& tout = reverbSpectrumTap[1];
            const float* w = reverb.addedWet (0);
            for (int i = 0; i < numSamples; ++i)
                tout.push (w[i]);
            tout.publishIfDue (eqSpectrumOrder,
                               juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0)); }

          if (reverbFade)
              core::BypassFade::blend (channels, fadeDry.getArrayOfReadPointers(),
                                       nchBack, numSamples, reverbSpan);

          nsStage[stReverb] = elapsedNs (a); }
    else
        reverb.reset();

    // The cabinet closes the tone: the IR speaks last, before the master's hand and the safety.
    // Its picture's spectra tap the door and the exit, channel 0, only while it is on.
    // The IR is a replacing link too: its own tail is not separable from the signal, so standing
    // it down is a crossfade to what it was handed, not a ring-out. (A cab sim in a DAW bypasses
    // exactly this way, and the alternative — dry plus a ghost of the cabinet — is a third sound
    // rather than a bypass.)
    { const auto a = PerfClock::now();
      const bool cabWorks = linkWorks (params::rowCab);
      const auto cabSpan  = blockFade[(size_t) params::rowCab].advance (numSamples, cabWorks);
      const bool cabOn    = core::BypassFade::runs (cabSpan, cabWorks);
      const bool cabFade  = cabSpan.moving && canFade (numSamples, nchBack);

      if (cabFade)
          for (int ch = 0; ch < nchBack; ++ch)
              juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                 buffer.getReadPointer (ch), numSamples);

      if (cabOn)
      {
          const float* d = buffer.getReadPointer (0);
          for (int i = 0; i < numSamples; ++i)
              cabSpectrumTap[0].push (d[i]);
          cabSpectrumTap[0].publishIfDue (eqSpectrumOrder,
                                          juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
      }
      cab.process (channels, nchBack, numSamples, cabOn);

      if (cabFade)
          core::BypassFade::blend (channels, fadeDry.getArrayOfReadPointers(), nchBack, numSamples, cabSpan);

      // Once the fade is over and nothing of the cabinet is in the sum, drop what it remembers.
      // Otherwise the first IR-length after it comes back convolves what was played BEFORE it
      // stood down — a ghost, faded in over fifteen milliseconds, of a phrase from minutes ago.
      if (! cabOn)
          cab.reset();

      if (cabOn)
      {
          const float* d = buffer.getReadPointer (0);
          for (int i = 0; i < numSamples; ++i)
              cabSpectrumTap[1].push (d[i]);
          cabSpectrumTap[1].publishIfDue (eqSpectrumOrder,
                                          juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0));
      }
      cabOutDb.store (juce::Decibels::gainToDecibels (
          buffer.getMagnitude (0, 0, numSamples), -90.0f));
      nsStage[stCab] = elapsedNs (a); }

    const auto tOut = PerfClock::now();

    // The output trim closes the chain — the master's hand on the way out, ramped per block
    // against zipper noise — and the OUT rail reads the result, clip cap latched past 0 dBFS.
    {
        const float target = linkWorks (params::rowOut)
                                 ? juce::Decibels::decibelsToGain (outTrimParam->load()) : 1.0f;
        buffer.applyGainRamp (0, numSamples, lastOutGain, target);
        lastOutGain = target;

        // The safety after the master's hand: nothing downstream of here touches gain, so the
        // ceiling it enforces is the ceiling that leaves the box. The meter reads AFTER it —
        // the truth on the rail is the truth at the jack.
        { const auto a = PerfClock::now();
          // Switching a limiter off returns whatever it was holding down in ONE sample: three
          // decibels of grip released instantly is a step upward, which is a click. Its grip is
          // crossfaded off like any other replacing link.
          const bool limWorks = linkWorks (params::rowLimit);
          const auto limSpan  = blockFade[(size_t) params::rowLimit].advance (numSamples, limWorks);
          const bool limFade  = limSpan.moving && canFade (numSamples, nchBack);

          if (limFade)
              for (int ch = 0; ch < nchBack; ++ch)
                  juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                     buffer.getReadPointer (ch), numSamples);

          limiter.process (channels, nchBack, numSamples,
                           core::BypassFade::runs (limSpan, limWorks), limiterCeilParam->load());

          if (limFade)
              core::BypassFade::blend (channels, fadeDry.getArrayOfReadPointers(),
                                       nchBack, numSamples, limSpan);

          nsStage[stLimit] = elapsedNs (a); }
        limiterGrDb.store (juce::Decibels::gainToDecibels (limiter.lastMinGain(), -90.0f));

        // The mono chain becomes the stereo output HERE — one copy, after everything. (In STEREO
        // SPACE the copy was made at the seam, and there is nothing left to copy.)
        for (int ch = nchBack; ch < numChannels; ++ch)
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

    // MINUS the limiter, which was measured separately inside this same scope. Left in, the two
    // rows double-counted and the breakdown stopped adding up — which is the one thing a breakdown
    // is for.
    nsStage[stOut]   = juce::jmax (0.0, elapsedNs (tOut) - nsStage[stLimit]);
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
