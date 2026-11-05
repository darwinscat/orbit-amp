// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "PluginProcessor.h"
#include "Product.h"
#include "Parameters.h"

#include <felitronics/chorus/ChorusLaw.h>
#include <felitronics/envfilter/EnvelopeFilterLaw.h>
#include <felitronics/octave/OctaveLaw.h>

#include <cstring>
#include "core/CabSynth.h"
#include <BinaryData.h>
#include "device/FactoryShelf.h"
#include "device/IrLibrary.h"


namespace orbitamp
{

AmpProcessor::FirstStart::FirstStart (const Product& product)
{
    if (product.firstStart != nullptr)
        product.firstStart (product);
}

void gatherFormerIrs (const Product& product)
{
    static PerProduct<const bool> once;
    once.of (product.name, [&product]
    {
        if (prefs::getBool (product, prefs::irsGathered, false))
            return true;

        const auto gathered = device::IrLibrary::gather (device::IrLibrary::formerDirectory (product.name),
                                                         device::IrLibrary::directory (product.irsFolder()), product.name);

        // A file that would not copy is tried again on the next start; what was copied is not copied twice.
        if (gathered.failed == 0)
            prefs::setBool (product, prefs::irsGathered, true);
        return true;
    });
}

// The machine's word for the hum and the stage's are one list in one order: the processor hands one
// to the other as a number.
static_assert ((int) prefs::Hum::off       == (int) felitronics::dehum::Mains::Off
            && (int) prefs::Hum::hz50      == (int) felitronics::dehum::Mains::Hz50
            && (int) prefs::Hum::hz60      == (int) felitronics::dehum::Mains::Hz60
            && (int) prefs::Hum::automatic == (int) felitronics::dehum::Mains::Auto);

// ...and so are the stage's verdict on a learn and the one the link hands to the windows.
using StageVerdict = AmpProcessor::HumStage::LearnVerdict;
static_assert ((int) StageVerdict::Learned      == core::HumLearnLink::learned
            && (int) StageVerdict::Disturbed    == core::HumLearnLink::disturbed
            && (int) StageVerdict::NothingHeard == core::HumLearnLink::nothingHeard
            && (int) StageVerdict::TooShort     == core::HumLearnLink::tooShort
            && (int) StageVerdict::NotLearning  == core::HumLearnLink::notLearning);

namespace
{
    /** The engine's parameters, and after them whatever the product adds of its own — each declaring,
        as its default, where a fresh instance of the product stands in that environment (params::Opening). */
    juce::AudioProcessorValueTreeState::ParameterLayout layout (const Product& product, params::StereoMode environment)
    {
        params::Opening opening;
        if (product.opening != nullptr)
            product.opening (product, opening);
        opening.stereoMode = environment;
        opening.packTones  = product.chain == Chain::bass;     // its pack's tone, where the pack opens it

        auto all = params::createLayout (product.name, opening);
        if (product.addParameters != nullptr)
            product.addParameters (product, all);
        return all;
    }
}

AmpProcessor::AmpProcessor (const Product& product)
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      ownProduct (product),
      firstStarted (product),
      apvts (*this, nullptr, stateType, layout (product, modeOfEnvironment())),
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
      // GitHub's latest release of THIS repo, the running version, and the badge's own settings file.
      // The checker asks nothing on its own — see updateChecker(). The repository releases every amp
      // together, under the family's plain vX.Y.Z, so every amp asks for that "latest release", as
      // OrbitAmp always has: no tag prefix (see Product.h).
      updater ({ .ownerRepo      = "darwinscat/orbit-amp",
                 .productName    = ownProduct.name,
                 .currentVersion = ORBITAMP_VERSION,
                 .settings       = [this] { return updateStore->file(); } })
{
    for (auto& order : blockSpectrumOrder)
        order.store (eqSpectrumOrder);
    for (auto& value : midiControl)
        value.store (-1);

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
        p.eqMode   = apvts.getRawParameterValue (params::blockEqMode (params::eqBlockId (l)));
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
    tunerMuteParam     = apvts.getRawParameterValue (params::tunerMute);
    stereoModeParam    = apvts.getRawParameterValue (params::stereoMode);
    modeAutoValue      = (int) modeOfEnvironment();     // the layout's default, and so where the mode stands
    preampSmoothParam  = apvts.getRawParameterValue (params::blockSmooth (params::preampId));
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
    boostLevelParam    = apvts.getRawParameterValue (params::boostLevel);
    preampBoostParam   = apvts.getRawParameterValue (params::preampBoost);
    for (int i = 0; i < params::numSelectors; ++i)
        preampSelectorParams[(size_t) i] = apvts.getRawParameterValue (params::selectorId (params::preampId, i));
    for (int i = 0; i < params::numChainRows; ++i)
    {
        const auto& link = params::chainLinks[(size_t) i];
        rowOn[(size_t) i] = link.onParam != nullptr ? apvts.getRawParameterValue (link.onParam) : nullptr;
        rowPresent[(size_t) i] = link.presentParam != nullptr ? apvts.getRawParameterValue (link.presentParam) : nullptr;
    }
    if (! ownProduct.tunerSwitch)
        rowOn[(size_t) params::rowTuner] = nullptr;   // it listens always (Product::tunerSwitch)

    // The bass's own parameters, where its chain has them.
    if (ownProduct.chain == Chain::bass)
    {
        const auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        bass = { raw (params::compAmount), raw (params::compAttack), raw (params::compRelease), raw (params::compCharacter),
                 raw (params::ampBlend), raw (params::ampBlendHz), raw (params::ampBlendInvert), raw (params::cabMix),
                 raw (params::bassHpf), raw (params::bassHpfHz4), raw (params::bassHpfHz5),
                 raw (params::bassLpf), raw (params::bassLpfHzFingers), raw (params::bassLpfHzSlap),
                 raw (params::octaveAmount), raw (params::octaveToneHz),
                 raw (params::filterAmount), raw (params::filterMode), raw (params::filterDirection), raw (params::filterResonance),
                 raw (params::filterRange), raw (params::filterKeepLows), raw (params::filterKeepHz),
                 raw (params::filterReleaseMs),
                 raw (params::chorusAmount), raw (params::chorusRateHz), raw (params::chorusCrossoverHz) };
        bass.hpfOn         = raw (params::bassHpfOn);
        bass.lpfOn         = raw (params::bassLpfOn);
        bass.compRatio     = raw (params::compRatio);
        bass.compThreshold = raw (params::compThreshold);
        jassert (bass.compAmount != nullptr && bass.lpfHzSlap != nullptr && bass.chorusCrossoverHz != nullptr && bass.compThreshold != nullptr);
    }

    // OrbitAmp's COMP's (Product::guitarComp): the bass's, the same parameters.
    if (ownProduct.chain != Chain::bass && ownProduct.guitarComp)
    {
        const auto raw = [this] (const char* id) { return apvts.getRawParameterValue (id); };
        bass.compAmount    = raw (params::compAmount);
        bass.compAttack    = raw (params::compAttack);
        bass.compRelease   = raw (params::compRelease);
        bass.compCharacter = raw (params::compCharacter);
        bass.compRatio     = raw (params::compRatio);
        bass.compThreshold = raw (params::compThreshold);
        jassert (bass.compAmount != nullptr && bass.compThreshold != nullptr);
    }

    gateThresholdParam = apvts.getRawParameterValue (params::gateThreshold);
    gatePosParam       = apvts.getRawParameterValue (params::gatePos);
    gateDecayParam     = apvts.getRawParameterValue (params::gateDecay);

    delaySyncParam    = apvts.getRawParameterValue (params::delaySync);
    delayTimeMsParam  = apvts.getRawParameterValue (params::delayTimeMs);
    delayDivParam     = apvts.getRawParameterValue (params::delayDiv);
    delayBpmParam     = apvts.getRawParameterValue (params::delayBpm);
    delayHostTempoParam = apvts.getRawParameterValue (params::delayHostTempo);
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

    preampGainParam = apvts.getRawParameterValue (params::preampGain);

    rescanDevices();

    // Where the engine's own pack stands in this product's list, as the layout found it a moment ago
    // (engineDefault); its first place where the list has none, as the layout's default was then.
    for (int i = 0; i < preamp.packs.size(); ++i)
        if (preamp.packs.getReference (i).displayName() == params::enginePack)
        {
            enginePlace = i;
            break;
        }

    // Every parameter's audio-thread mirror and the value it falls back to — see sanitiseParameters.
    for (auto* p : getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (auto* raw = apvts.getRawParameterValue (ranged->paramID))
            {
                // The bass's effects' sliders keep their last number through a NaN: their default, 0, would switch an
                // effect off on an invalid automation point (review round 3).
                const bool holds = ranged->paramID == params::octaveAmount || ranged->paramID == params::filterAmount
                                || ranged->paramID == params::chorusAmount || ranged->paramID == params::compAmount
                                || ranged->paramID == params::compThreshold || ranged->paramID == params::compRatio;
                const float fallback = ranged->convertFrom0to1 (engineDefault (*ranged));
                parameterGuards.push_back ({ raw, fallback, holds, fallback });
            }

    // The product's own half, where it has one. HERE, before the history takes its baseline: what it
    // settles as it is built — the cockpit's chain, its channel, its pack — is how the amp starts,
    // and must not be an edit of it.
    productEngine = ownProduct.makeEngine != nullptr ? ownProduct.makeEngine (*this) : nullptr;

    // The names go in BEFORE the history looks, so a plugin that has just opened already knows
    // which device it is playing — and knows it without that knowledge being an edit. Resetting
    // takes the baseline over the seeded tree; marking it saved undoes the dirty flag the reset
    // raises, which is right: nothing has been changed, the state was merely completed. The state says
    // which cabinet a fresh one opens on, so that one it saves is never taken for one of before
    // (takeFormerCabinet).
    seedSwitchNames();
    apvts.state.setProperty (cabOpeningId, params::cabIrDefault, nullptr);
    takeBassCabinet (apvts.state);   // a fresh bass amp's cabinet, which the catalog has none of yet
    history.reset();
    history.markSaved();

    // The saved session exists from the first moment: a host may ask for it from any thread
    // before the pump has ever run.
    apvts.state.addListener (this);
    refreshSavedState (true);

    // The two links that HAVE a tail are told what their knobs say before anyone can ask how long
    // this rings. `prepareToPlay` does this too, but a VST3 host may ask before it activates the
    // plugin at all, and an answer built out of the stages' constructor defaults is not an answer.
    updateDelaySettings();
    updateReverbSettings();

    // The machine's hum setting, read here on the constructing thread: its first read opens the
    // settings file, and the audio thread must only ever meet the cached atomic.
    humWanted = &prefs::humCache (ownProduct);
    humLastOn = &prefs::humLastOnCache (ownProduct);
    litWanted = &prefs::litCache (ownProduct);       // whether a world hears the guitar, the same way
    humPlayWanted  = &prefs::humtune::playCache (ownProduct);
    humQuietWanted = &prefs::humtune::quietCache (ownProduct);
    humUpToWanted  = &prefs::humtune::upToCache (ownProduct);

    // One steady pump for the settle timer. 30 Hz with the engine's default settle count means a
    // burst commits about 0.4 s after you stop moving.
    //
    // LAST, and not by taste: the timer ticks on the message thread whoever constructs us, and a
    // host that constructs on a worker would have had the first tick walking the history and the
    // saved session while this constructor was still building them.
    startTimerHz (30);
}

juce::MemoryBlock AmpProcessor::buildSavedState()
{
    juce::MemoryBlock out;

    // The workspace envelope carries the live parameter tree and the other three registers, so
    // a reopened session comes back with all four sounds. NOT the undo stacks: CompareHistory's
    // envelope holds the live capture and the register snapshots and nothing else, so undo starts
    // fresh on reopen. (It said otherwise here for a long time — it never did.)
    auto workspace = history.toTree();
    embedCabIrs (workspace);   // the player's own IRs travel with the session, whole
    stamp (workspace);         // ...and whose session it is

    if (auto xml = workspace.createXml())
        copyXmlToBinary (*xml, out);

    return out;
}

void AmpProcessor::refreshSavedState (bool force)
{
    // A RESTORE IS ON ITS WAY. The copy already IS the incoming session; the live state is still
    // the one it replaces, and writing that down now would hand a worker's save the session the
    // host just loaded over. The restore writes the copy itself once it has landed.
    if (pendingRestores.load (std::memory_order_acquire) > 0)
        return;

    ticksSinceSaved = juce::jmin (ticksSinceSaved + 1, savedThrottleTicks);

    bool registersMoved = history.active() != savedActive
                       || (int) savedRegisters.size() != history.numRegisters();

    for (int i = 0; ! registersMoved && i < history.numRegisters(); ++i)
    {
        const auto& r = history.registerTree (i);
        registersMoved = (r.has_value() ? *r : juce::ValueTree()) != savedRegisters[(size_t) i];
    }

    if (! force && ((! stateDirty.load (std::memory_order_relaxed) && ! registersMoved)
                    || ticksSinceSaved < savedThrottleTicks))
        return;

    auto bytes = buildSavedState();

    // Taken AFTER the build: the build itself flushes the parameters into the tree, and whatever
    // that stirred up is already in these bytes.
    stateDirty.store (false, std::memory_order_relaxed);
    ticksSinceSaved = 0;
    savedActive     = history.active();
    savedRegisters.clear();
    for (int i = 0; i < history.numRegisters(); ++i)
    {
        const auto& r = history.registerTree (i);
        savedRegisters.push_back (r.has_value() ? *r : juce::ValueTree());
    }

    const juce::ScopedLock sl (savedLock);
    savedState = std::move (bytes);
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // FROM ANYWHERE BUT THE MESSAGE THREAD, the copy the message thread keeps — see
    // refreshSavedState. Nothing the session is made of is touched from here.
    // ...and from the message thread too while a restore is still on its way to it: the live
    // state is the one being replaced, and the copy is the session the host just handed over.
    if (! juce::MessageManager::existsAndIsCurrentThread()
        || pendingRestores.load (std::memory_order_acquire) > 0)
    {
        const juce::ScopedLock sl (savedLock);
        destData = savedState;
        return;
    }

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
    pumpDeviceWork();
    pumpSwitchNames();

    // On the message thread the session is written fresh, and the kept copy becomes this one.
    refreshSavedState (true);

    const juce::ScopedLock sl (savedLock);
    destData = savedState;
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    stateWasRestored = true;   // a session's choice outranks the environment's default

    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    const auto tree = juce::ValueTree::fromXml (*xml);

    // What neither path below would load is not a session, and must not become the copy a worker's
    // save hands out: before this copy existed, an ignored load left saves describing the state
    // that actually plays. FIRST, before anything is counted or stored — a rejected blob that had
    // been counted as pending would have frozen the copy for good. The state type is spelled out
    // rather than read off the live tree, which is not this thread's to read.
    const bool loadable = (tree.hasType ("Workspace") && tree.getChildWithName ("Live").getNumChildren() > 0)
                       || tree.hasType ("state");   // the tree type the constructor gives apvts
    if (! loadable)
        return;

    // ...nor is another chain's: a guitar amp's session on the bass, or the bass's on a guitar amp (stamp, belongs).
    if (! belongs (tree))
        return;

    // The session just handed over IS the session now, whether or not the message thread has
    // applied it yet: a host that restores and saves back to back from a worker thread must get
    // back what it gave, not the state before. Counted as pending until it lands — see
    // refreshSavedState — and written down in our own words once it has.
    pendingRestores.fetch_add (1, std::memory_order_acq_rel);
    {
        const juce::ScopedLock sl (savedLock);
        savedState.replaceAll (data, (size_t) sizeInBytes);
    }

    const auto apply = [] (AmpProcessor& self, juce::ValueTree t)
    {
        // The embedded IRs go to the store and leave the tree before anyone else sees it: the
        // history would otherwise keep megabytes in its baseline and compare them every tick.
        self.takeEmbeddedIrs (t);
        unstamp (t);

        // ...and a state of 0.8.0's, its trim and its gate where they belong now; one saved while DARK
        // MATTER was the cabinet every amp opened on, on the one they open on now.
        self.takeFormerTrims (t);
        self.takeFormerCabinet (t);
        self.takeBassCabinet (t);   // ...and a bass's on a guitar cabinet, on the bass's own

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
    const auto land = [apply] (AmpProcessor& self, const juce::ValueTree& t)
    {
        apply (self, t);
        self.pendingRestores.fetch_sub (1, std::memory_order_acq_rel);
        self.refreshSavedState (true);
    };

    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
    {
        land (*this, tree);
        return;
    }

    juce::MessageManager::callAsync (
        [weak = juce::WeakReference<AmpProcessor> (this), tree, land]
        {
            if (auto* self = weak.get())
                land (*self, tree);
        });
}

void AmpProcessor::selectDemoLoop (int index)
{
    const int n = (int) std::size (params::demoLoopFiles);
    demoLoopIndex = juce::jlimit (0, n - 1, index);
    demo.setLoop (params::demoLoopsDir().getChildFile (params::demoLoopFiles[demoLoopIndex]), getSampleRate());
}

void AmpProcessor::takeFormerTrims (juce::ValueTree& tree) const
{
    const auto* former = ownProduct.formerInputZeroId;
    if (former == nullptr || ! tree.isValid())
        return;

    if (tree.hasType (stateType) && tree.hasProperty (former))
    {
        const float dialled = (float) (int) tree.getProperty (former);
        const auto shift = [&tree] (const char* id, float by, float lowest, float highest)
        {
            if (auto p = tree.getChildWithProperty ("id", juce::String (id)); p.isValid())
                p.setProperty ("value", juce::jlimit (lowest, highest, (float) p.getProperty ("value") + by), nullptr);
        };
        shift (params::inTrim,        -dialled, -params::inTrimRangeDb,   params::inTrimRangeDb);
        shift (params::gateThreshold, -dialled, params::gateThresholdMin, params::gateThresholdMax);
        tree.removeProperty (former, nullptr);
    }

    for (auto child : tree)
        takeFormerTrims (child);
}

void AmpProcessor::takeFormerCabinet (juce::ValueTree& tree)
{
    if (! tree.isValid())
        return;

    if (tree.hasType (stateType))
    {
        // A state with no note is one of before the note, when every amp opened on DARK MATTER.
        const int now    = params::cabIrDefault;
        const int former = tree.hasProperty (cabOpeningId) ? (int) tree.getProperty (cabOpeningId)
                                                           : params::cabIrNames.indexOf ("DARK MATTER");
        if (former >= 0 && former < params::cabIrNames.size() && former != now)
        {
            if (auto p = tree.getChildWithProperty ("id", juce::String (params::cabIr));
                p.isValid() && juce::roundToInt ((float) p.getProperty ("value")) == former)
                p.setProperty ("value", (float) now, nullptr);

            // A face plays the build's cabinets as IRs of its own, by their files' names, and a state
            // lands on it with no window to settle it (channels::Channels::align): the one it played
            // becomes the new one there and then — its bytes into the store, the state their key, and
            // its name and `from` the new file's, which lies in the same folder of the shelf. With no
            // bytes for it (no factory folder) it lets go instead, as a cabinet that is not there. The
            // channels' memories name the new one's file where they named the old one's.
            if (ownProduct.makeEngine != nullptr)
            {
                const auto file = [] (int n) { return juce::String::fromUTF8 (catalog::irs[n].file); };
                const auto stem = [&file] (int n) { return file (n).upToLastOccurrenceOf (".", false, false); };
                const auto was = stem (former), is = stem (now);
                if (tree.getProperty (params::cabIrUserName).toString() == was)
                {
                    const auto& ir = cabIrBytes (ownProduct, now);
                    const auto key = ir.data != nullptr ? cabIrEmbeds.add (juce::MemoryBlock (ir.data, (size_t) ir.size)) : juce::String();
                    if (key.isNotEmpty())
                    {
                        tree.setProperty (params::cabIrUserKey, key, nullptr);
                        tree.setProperty (params::cabIrUserName, is, nullptr);
                        tree.setProperty (params::cabIrUserFrom, tree.getProperty (params::cabIrUserFrom).toString()
                                                                     .replace (file (former), file (now)), nullptr);
                    }
                    else
                        for (const auto* id : { params::cabIrUserKey, params::cabIrUserName, params::cabIrUserFrom })
                            tree.removeProperty (id, nullptr);
                }
                for (auto node : tree)
                    for (auto record : node)
                        if (record.getProperty (recordCabinetId).toString() == was)
                            record.setProperty (recordCabinetId, is, nullptr);
            }
        }
        tree.setProperty (cabOpeningId, now, nullptr);
    }

    for (auto child : tree)
        takeFormerCabinet (child);
}

namespace
{
    /** What a pick of a player's IR takes of its file: its bytes, where they are an IR the cabinet plays —
        not too big to be kept inside a session (EmbeddedIrs), and audio. */
    bool readCabFile (const juce::File& file, juce::MemoryBlock& bytes)
    {
        return file.existsAsFile() && file.getSize() <= device::EmbeddedIrs::maxBytes
            && file.loadFileAsData (bytes) && core::CabinetIr::decodes (bytes.getData(), bytes.getSize());
    }

    /** ...and what it leaves on the state: the IR's name, where in the library under `root` it came from,
        and the key of its bytes in the store. */
    void nameCabFile (juce::ValueTree& tree, const juce::File& file, const juce::File& root, const juce::String& key)
    {
        // Where it came from, in the library's own terms and one spelling on every system — a tick in
        // the menu, and nothing that the sound depends on.
        const auto from = file.getRelativePathFrom (root).replaceCharacter ('\\', '/');

        // The key LAST: it is the one the pump and the face watch, and by then the name beside it is
        // already the right one.
        tree.setProperty (params::cabIrUserName, file.getFileNameWithoutExtension(), nullptr);
        tree.setProperty (params::cabIrUserFrom, from, nullptr);
        tree.setProperty (params::cabIrUserKey,  key, nullptr);
    }
}

void AmpProcessor::takeBassCabinet (juce::ValueTree& tree)
{
    if (! tree.isValid() || ownProduct.chain != Chain::bass)
        return;

    if (tree.hasType (stateType))
    {
        // What it plays: an IR of the player's the store holds, or the number `cabIr` keeps — the live
        // parameter's, for the live tree, whose own copy of it may not have caught up yet.
        const auto key = tree.getProperty (params::cabIrUserKey).toString();
        int number = params::cabIrDefault;
        if (tree == apvts.state)
        {
            const auto* p = apvts.getParameter (params::cabIr);
            number = juce::roundToInt (p->convertFrom0to1 (p->getValue()));
        }
        else if (const auto p = tree.getChildWithProperty ("id", juce::String (params::cabIr)); p.isValid())
            number = juce::roundToInt ((float) p.getProperty ("value"));

        if ((key.isNotEmpty() && cabIrEmbeds.find (key) != nullptr) || ownProduct.playsCabIr (number))
            return;

        const auto root = device::IrLibrary::directory (ownProduct.irsFolder());
        for (const auto& file : device::IrLibrary::listed (root))
        {
            juce::MemoryBlock bytes;
            if (! readCabFile (file, bytes))
                continue;

            if (const auto added = cabIrEmbeds.add (std::move (bytes)); added.isNotEmpty())
            {
                nameCabFile (tree, file, root, added);
                return;
            }
        }
        return;
    }

    for (auto child : tree)
        takeBassCabinet (child);
}

juce::String AmpProcessor::loadedAs() const
{
    // JUCE names every wrapper of the family's but one: clap-juce-extensions makes its processor as no
    // wrapper JUCE knows (CMakeLists.txt: clap_juce_extensions_plugin), so of the formats the family ships,
    // the one JUCE does not name is the CLAP.
    const auto format = wrapperType == wrapperType_Undefined ? juce::String ("CLAP") : pluginFormat();
    const auto said = juce::String (ownProduct.name) + "  " + format;

    if (wrapperType == wrapperType_Standalone)
        return said;

    if (const juce::PluginHostType host; host.type != juce::PluginHostType::UnknownHost)
        return said + " in " + host.getHostDescription();

    const auto program = juce::File (juce::PluginHostType::getHostPath()).getFileNameWithoutExtension();
    return program.isNotEmpty() ? said + " in " + program : said;
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
    bool nameFirstDevice[numCaptured] = { false };

    for (size_t b = 0; b < numCaptured; ++b)
    {
        const auto* id = deviceIdOf (b);
        auto* p = apvts.getParameter (id);
        const int was = juce::roundToInt (apvts.getRawParameterValue (id)->load());
        const int now = blockAt (b).rescan (ownProduct.name, was);

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

    if (! writes.empty() || std::find (std::begin (nameFirstDevice), std::end (nameFirstDevice), true)
                                != std::end (nameFirstDevice))
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

const AmpProcessor::IrBytes& AmpProcessor::cabIrBytes (const Product& product, int index)
{
    // The family's shelf (device/FactoryShelf.h), in the parameter's order: OrbitAmp plays it from memory,
    // read once out of the factory folder for each product, by the index `cabIr` keeps.
    static PerProduct<const std::vector<IrBytes>> shelves;
    const auto& shelf = shelves.of (product.name, [&product]
    {
        std::vector<IrBytes> out;
        for (const auto& ir : device::shelf::irs (product.name))
            out.push_back ({ ir.data, ir.size, ir.folder });
        return out;
    });

    jassert (shelf.size() == (size_t) params::cabIrNames.size());
    index = juce::jlimit (0, (int) shelf.size() - 1, index);
    const auto& ir = shelf[(size_t) index];

    // A NUMBER OF THE OTHER CHAIN'S is no cabinet at all, whatever the factory folder holds: a bass amp
    // plays no guitar cabinet — not even RED GIANT in its place — and a guitar amp no bass one.
    static const IrBytes none { nullptr, 0, nullptr };
    if (! product.playsCabIr (index))
        return none;

    // A place the product holds empty — a face plays none of the shelf's `Other` — plays RED GIANT, the
    // cabinet every guitar amp opens on: a host that moves the number there hears a cabinet the product
    // has. With the factory folder missing that one is empty too, and no bytes is what the cabinet is
    // given: the cabinet loads nothing then: where it is loaded, an IR with no bytes is passed over.
    return ir.data != nullptr ? ir : product.playsCabIr (params::cabIrDefault) ? shelf[(size_t) params::cabIrDefault] : none;
}

AmpProcessor::CabChoice AmpProcessor::cabChoice() const
{
    CabChoice c;

    const auto* p = apvts.getParameter (params::cabIr);
    c.factory = juce::jlimit (0, params::cabIrNames.size() - 1, juce::roundToInt (p->convertFrom0to1 (p->getValue())));

    const auto key = apvts.state.getProperty (params::cabIrUserKey).toString();

    if (key.isNotEmpty())
        if (const auto* bytes = cabIrEmbeds.find (key))
        {
            c.bytes = bytes;
            c.key   = key;
            c.name  = apvts.state.getProperty (params::cabIrUserName).toString();
            c.from  = apvts.state.getProperty (params::cabIrUserFrom).toString();
        }

    return c;
}

void AmpProcessor::chooseCabFactory (int index)
{
    index = juce::jlimit (0, params::cabIrNames.size() - 1, index);
    if (! ownProduct.playsCabIr (index))
        return;     // the other chain's: never this amp's to play

    const auto choice = cabChoice();

    // The IR already playing, picked again, is no edit — not for the host, not for the history.
    if (! choice.isUser() && choice.factory == index)
        return;

    // The player's own goes FIRST: a pick of the factory IR already under it moves no parameter,
    // and it is the properties leaving that makes it sound.
    for (const auto* id : { params::cabIrUserKey, params::cabIrUserName, params::cabIrUserFrom })
        apvts.state.removeProperty (id, nullptr);

    if (choice.factory != index)
    {
        auto* p = apvts.getParameter (params::cabIr);
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
        p->endChangeGesture();
    }
    else
    {
        // No parameter moved, only the state did — and a host hears of that only when told.
        updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
    }
}

bool AmpProcessor::chooseCabFile (const juce::File& file)
{
    juce::MemoryBlock bytes;
    if (! readCabFile (file, bytes))
        return false;

    const auto key = cabIrEmbeds.add (std::move (bytes));
    if (key.isEmpty())
        return false;

    nameCabFile (apvts.state, file, device::IrLibrary::directory (ownProduct.irsFolder()), key);

    // No parameter moved: without this, a host would not know the session has something to save.
    updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
    return true;
}

void AmpProcessor::requestCabSynth (const juce::String& cab, CabSetup setup, bool live)
{
    const int mine = ++cabSynthLatest;
    std::weak_ptr<int> alive = cabSynthAlive;

    cabSynthPool.addJob ([this, alive, mine, cab, setup, live]
    {
        if (mine != cabSynthLatest.load())
            return;   // superseded before it began

        static const auto table = core::cabsynth::parseTable (std::string (BinaryData::cabinets_txt, (size_t) BinaryData::cabinets_txtSize));
        const auto spot = [] (const CabSpot& s) { return core::cabsynth::Spot { s.x, s.y, s.distance, s.horn, s.mic.toStdString(), s.angle, s.hornDb }; };
        core::cabsynth::Pair pair { spot (setup.a), spot (setup.b), setup.blend, setup.align, setup.invert };
        if (! setup.bOn)
            pair.blend = 0;                 // A alone: the blend not heard
        else if (! setup.aOn)
        {
            pair.a = spot (setup.b);        // B alone, as one mic is rendered — the horn as A's says it
            pair.a.horn   = setup.a.horn;
            pair.a.hornDb = setup.a.hornDb;
            pair.blend = 0;
        }
        pair.a.back = setup.back;
        const auto ir  = core::cabsynth::renderPairParts (table, cab.toStdString(), pair);
        const auto wav = core::cabsynth::wav24 (ir.mix, 48000);
        auto bytes = std::make_shared<juce::MemoryBlock> (wav.data(), wav.size());
        auto parts = ir.a.empty() ? std::shared_ptr<const CabParts>() : std::make_shared<const CabParts> (CabParts { ir.a, ir.b });

        juce::MessageManager::callAsync ([this, alive, mine, cab, setup, bytes, parts, live]
        {
            if (alive.lock() == nullptr || bytes->getSize() == 0)
                return;

            // A hand still moving asks again before the worker is done, every time: what it finished is heard all the
            // same, unless a later one was heard already — so the sound follows the hand as fast as the worker renders.
            if (live)
            {
                if (mine <= cabSynthHeard)
                    return;
                cabSynthHeard = mine;
                this->cab.load (bytes->getData(), bytes->getSize());   // heard, not kept: the state still names the last kept
                heardCab (bytes, "live:" + juce::String (mine), false, parts);
                return;
            }

            // Kept: only the latest asked.
            if (mine != cabSynthLatest.load())
                return;
            cabSynthHeard = mine;

            // Kept: and loaded even when it is the very cabinet the state already named — a drag that ends where it
            // began has left a live one playing in between.
            cabIrStale = true;

            const auto key = cabIrEmbeds.add (std::move (*bytes));
            if (key.isEmpty())
                return;
            cabKeptPartsKey = key;
            cabKeptParts = parts;

            apvts.state.setProperty (params::cabIrUserKey, key, nullptr);
            apvts.state.setProperty (params::cabIrUserName, cab, nullptr);
            apvts.state.setProperty (params::cabIrUserFrom, cabSynthFrom (cab, setup), nullptr);
            updateHostDisplay (ChangeDetails{}.withNonParameterStateChanged (true));
        });
    });
}

void AmpProcessor::heardCab (std::shared_ptr<const juce::MemoryBlock> bytes, const juce::String& identity, bool kept,
                             std::shared_ptr<const CabParts> parts)
{
    if (kept && cabHeardNow.kept && identity == cabHeardIdentity)
        return;   // the same IR loaded again: nothing new heard
    cabHeardNow = { std::move (bytes), cabHeardNow.serial + 1, kept, std::move (parts) };
    cabHeardIdentity = identity;
}

juce::String AmpProcessor::cabSynthFrom (const juce::String& box, const CabSetup& setup)
{
    const auto& a = setup.a;
    const auto& b = setup.b;
    // The horn: 0 off, 1 on at the table's level, or its attenuator's step in dB (-6, -12) while on.
    const juce::String horn = ! a.horn ? "0" : a.hornDb < -0.5f ? juce::String (juce::roundToInt (a.hornDb)) : "1";
    return "synth:" + box + "|" + juce::String (a.x, 3) + "|" + juce::String (a.y, 3)
         + "|" + juce::String (a.distance, 3) + "|" + horn + "|" + a.mic
         + "|" + juce::String (setup.blend, 3) + "|" + (setup.align ? "1" : "0") + "|" + (setup.invert ? "1" : "0")
         + "|" + juce::String (b.x, 3) + "|" + juce::String (b.y, 3) + "|" + juce::String (b.distance, 3)
         + "|" + b.mic + "|" + juce::String (a.angle, 1) + "|" + juce::String (b.angle, 1)
         + "|" + (setup.aOn ? "1" : "0") + "|" + (setup.bOn ? "1" : "0") + "|" + juce::String (setup.back);
}

AmpProcessor::CabSynthChoice AmpProcessor::cabSynthOf (const CabChoice& choice)
{
    return choice.isUser() ? cabSynthOf (choice.from) : CabSynthChoice {};
}

AmpProcessor::CabSynthChoice AmpProcessor::cabSynthOf (const juce::String& from)
{
    CabSynthChoice out;
    if (! from.startsWith ("synth:"))
        return out;

    const auto parts = juce::StringArray::fromTokens (from.fromFirstOccurrenceOf ("synth:", false, false), "|", "");
    if (parts.size() < 5)
        return out;

    out.cab     = parts[0];
    out.setup.a = { parts[1].getFloatValue(), parts[2].getFloatValue(), parts[3].getFloatValue(), parts[4] != "0", parts[5] };
    out.setup.a.hornDb = parts[4] == "1" || parts[4] == "0" ? 0.0f : parts[4].getFloatValue();
    if (parts.size() >= 13)
    {
        out.setup.blend  = parts[6].getFloatValue();
        out.setup.align  = parts[7] == "1";
        out.setup.invert = parts[8] == "1";
        out.setup.b      = { parts[9].getFloatValue(), parts[10].getFloatValue(), parts[11].getFloatValue(), out.setup.a.horn, parts[12] };
        out.setup.b.hornDb = out.setup.a.hornDb;
        out.setup.bOn    = out.setup.blend > 0.0f;    // ...as a `from` said it before each mic was on or off: B heard by a blend
    }
    if (parts.size() >= 15)
    {
        out.setup.a.angle = parts[13].getFloatValue();
        out.setup.b.angle = parts[14].getFloatValue();
    }
    if (parts.size() >= 17)
    {
        out.setup.aOn = parts[15] == "1";
        out.setup.bOn = parts[16] == "1";
    }
    if (parts.size() >= 18)
        out.setup.back = juce::jlimit (-1, 2, parts[17].getIntValue());
    out.valid = true;
    return out;
}

void AmpProcessor::embedCabIrs (juce::ValueTree& tree) const
{
    // Every state inside the tree — a preset is one, a session's workspace holds the live one and
    // each register's — is found by its type, wherever the envelope happens to nest it.
    juce::StringArray keys;

    std::function<void (const juce::ValueTree&)> collect = [&] (const juce::ValueTree& t)
    {
        if (t.hasType (stateType))   // the constant, not the live tree: a host may save from any thread
        {
            if (const auto key = t.getProperty (params::cabIrUserKey).toString(); key.isNotEmpty())
                keys.addIfNotAlreadyThere (key);
            return;
        }

        for (const auto& child : t)
            collect (child);
    };

    collect (tree);

    if (auto node = cabIrEmbeds.pack (keys); node.isValid())
        tree.appendChild (node, nullptr);
}

void AmpProcessor::pumpDeviceWork()
{
    auto pump = [this] (auto& block, auto& gainParam, auto& smoothParam, auto measuredId, const char* blk)
    {
        // The device parameter first of all: a restored session or an automating host moves it
        // without calling anyone, and every read below is about whatever pack it names.
        block.selectIfMoved (juce::roundToInt (
            apvts.getRawParameterValue (params::blockDevice (blk))->load()));

        // WHERE THE PLAYING PACK'S BOOST IS: the first of its switches called that — as the AMP's BOOST
        // key finds it — among the slots its switches are played from. The audio thread reads that
        // slot's parameter for boostOn(); the pack itself it cannot read. A pack with none is boosted
        // up its dial instead (params::preampBoost), which the gain dial below is told.
        int slot = -1;
        {
            const auto list = block.selectors();
            for (int i = 0; i < list.size() && i < params::numSelectors && slot < 0; ++i)
                if (list.getReference (i).name.trim().equalsIgnoreCase ("boost"))
                    slot = i;
            boostSlot.store (slot, std::memory_order_relaxed);
        }
        const bool turbo = slot < 0 && preampBoostParam->load() > 0.5f && ownProduct.chain != Chain::bass;

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

        // On the bass, no choice: a pack with a tone of its own plays it, and one with none the bass's standard.
        block.setRaw (bassEq() ? ! block.hasWearableTone() : mode == params::EqMode::ours);
        block.applySelectors (selectors);
        block.setGain (gainParam->load(), smoothParam->load() > 0.5f, turbo);

        std::array<float, (size_t) core::CapturedBlock::numMeasured> values {};
        for (int i = 0; i < (int) values.size(); ++i)
            values[(size_t) i] = apvts.getRawParameterValue (measuredId (i))->load();

        block.updateToneIfMoved (values);

        // The player's housekeeping and its load jobs — on the pool, or right here for a driver
        // that has no message loop to bring them back on.
        block.pump (inlineLoads ? nullptr : &modelPool);
    };

    pump (preamp,   preampGainParam, preampSmoothParam, params::preampMeasured, params::preampId);

    // The AMP's BLEND, the capture against its clean low end: the player's crossover, told from here as every
    // other setting of the device is. BLEND 1 — the crossover's amount 0 — is the player without it. This pump
    // runs from the message thread's timer and from prepareToPlay, which a host may call from a thread of its own:
    // the message thread tells it here (or the only thread there is, with no MessageManager at all), and a prepare elsewhere tells it itself
    // (tellBlend, from prepareToPlay) — both under one control-side lock, never the audio callback's.
    if (bass.blend != nullptr
        && (juce::MessageManager::existsAndIsCurrentThread() || juce::MessageManager::getInstanceWithoutCreating() == nullptr))
        tellBlend();

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

    // What the player does to the cabinet: baked into the IR off this pump, so the convolution
    // never learns of it. Rebuilt only when something moved — and set BEFORE an IR loads below, so
    // an IR loading into a freshly prepared cabinet is built once, cuts and all. The convolver takes
    // one handover at a time: the other order handed it the bare IR and then the cut one, and the
    // second waits for a message-thread retry that a render with no message loop never makes.
    {
        core::CabinetIr::Post post;
        // The bass trims nothing: its face has no trim (the place under its picture is its IRs' two doors), and a low
        // end needs the IR's length.
        post.trimOn       = ownProduct.chain != Chain::bass && cabTrimOnParam->load() > 0.5f;
        post.trimFraction = cabTrimParam->load();
        const auto slopeDb = [] (float v)
        { return params::eqSlopeValues[juce::jlimit (0, (int) std::size (params::eqSlopeValues) - 1,
                                                     juce::roundToInt (v))]; };
        // The bass's cuts are a stage of their own after DI/CAB; the cabinet's, hidden on its face, are never in its
        // sound, whatever their parameters say.
        const bool cabCuts = ownProduct.chain != Chain::bass;
        post.hpfOn        = cabCuts && cabHpfOnParam->load() > 0.5f;
        post.hpfHz        = cabHpfHzParam->load();
        post.hpfSlope     = slopeDb (cabHpfSlopeParam->load());
        post.lpfOn        = cabCuts && cabLpfOnParam->load() > 0.5f;
        post.lpfHz        = cabLpfHzParam->load();
        post.lpfSlope     = slopeDb (cabLpfSlopeParam->load());
        post.phase        = cabPhaseParam->load() > 0.5f;
        cab.setPost (post);
    }

    // The cabinet IR: choosing one is picking a FILE, so it loads here — the convolution's own
    // background loader swaps it in without a click.
    // An IR of the player's own is the same kind of pick — its bytes are already in memory. It is
    // named by the state TREE, which is the message thread's.
    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        // THE AUTOMATION LANE WINS. `cab_ir` is the host's handle on the cabinet, and a lane that
        // moves it while an IR of the player's own is playing used to move nothing at all: the
        // player's IR outranks the parameter underneath it. A move the host makes now lets the
        // player's IR go, and the shelf's pick plays. A recall is not a move — a register, an undo,
        // a preset or a session replaces the whole state, player's IR and number together — so the
        // sighting after one is only a new baseline. Picks from the menu need no exception: a
        // factory pick lets the player's IR go itself, and a file pick moves no parameter.
        {
            const auto* p = apvts.getParameter (params::cabIr);
            const int now = juce::roundToInt (p->convertFrom0to1 (p->getValue()));

            if (! cabIrParamBaselineStale && now != lastCabIrParam)
            {
                if (apvts.state.hasProperty (params::cabIrUserKey))
                    for (const auto* id : { params::cabIrUserKey, params::cabIrUserName, params::cabIrUserFrom })
                        apvts.state.removeProperty (id, nullptr);

                // ...and a number the product does not play — a bass amp's lane over the guitar's shelf —
                // plays the bass's own: the first of the player's bass IRs again, or no cabinet.
                takeBassCabinet (apvts.state);
            }

            cabIrParamBaselineStale = false;
            lastCabIrParam = now;
        }

        const auto choice = cabChoice();
        cabUserIr = choice.bytes;   // what a prepare off this thread will load — see below

        if (cabIrStale.exchange (false) || choice.identity() != lastCabIr)
        {
            lastCabIr = choice.identity();

            if (choice.isUser())
            {
                cab.load (choice.bytes->getData(), choice.bytes->getSize());
                heardCab (std::make_shared<const juce::MemoryBlock> (*choice.bytes), choice.identity(), true,
                          choice.key == cabKeptPartsKey ? cabKeptParts : nullptr);
            }
            else if (const auto& bytes = cabIrBytes (ownProduct, choice.factory); bytes.data != nullptr)
            {
                cab.load (bytes.data, (size_t) bytes.size);     // none, with the factory folder missing
                heardCab (std::make_shared<const juce::MemoryBlock> (bytes.data, (size_t) bytes.size), choice.identity(), true);
            }
        }
    }
    else if (cabIrStale.exchange (false))
    {
        // `prepareToPlay` runs this pump on whatever thread the host prepares from, and the engine
        // it just prepared must not wait for a timer — a render with no message loop would never get
        // its cabinet. The tree is out of reach here; what the message thread last saw is not. The
        // bytes never move (the store keeps them for its whole life), and the tick re-checks.
        if (const auto* own = cabUserIr.load())
            cab.load (own->getData(), own->getSize());
        else if (const auto& shelf = cabIrBytes (ownProduct, juce::roundToInt (apvts.getRawParameterValue (params::cabIr)->load()));
                 shelf.data != nullptr)
            cab.load (shelf.data, (size_t) shelf.size);
    }

    // An IR the convolver turned away mid-crossfade gets its next chance here.
    if (juce::MessageManager::existsAndIsCurrentThread())
        cab.flushPending();
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
    // In series: the captured block's models — a capture taken at another rate is resampled on the
    // way in and out, and that has a length: a 44.1 kHz session on 48 kHz packs reports 61 samples,
    // and a 96 kHz one 96. It was four until the kernel behind the rate match was replaced; the
    // number is the resampler's geometry and it will move again, which is why nothing downstream of
    // here may write it down.
    const int pr = preamp.latencySamples();
    const int total = pr + limiterLatency.load();      // ...and the limiter's lookahead and grid, always

    // 🔴 THE WIRE IS SIZED FROM THE NUMBER THE BLOCK ACTUALLY REPORTS, here and nowhere else.
    // `latencySamples()` came out of `NamStage::rateMatch` and therefore out of the rates that are
    // really in play; every ceiling this class used to carry was computed from a model rate NOBODY
    // PROMISED, and such a ceiling is not too low, it is wrong — the run rate can walk, without
    // limit while audio runs, and no static number survives that.
    fitWire (0, pr);

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
        for (int i = 0; i < (int) numCaptured; ++i)
            if (wire[(size_t) i].everShortened())
            {
                wireRefusalSeen = true;

                const int asked   = blockAt ((size_t) i).latencySamples();
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
    preparedBlock.store (block, std::memory_order_relaxed);

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
    {
        limiterReady = limiter.prepare (sampleRate, block, juce::jmax (2, channels), core::door::config());
        limiterLatency.store (limiterReady ? limiter.latencySamples() : 0);
        limiterLate.prepare (limiterLatency.load(), juce::jmax (2, channels));
        limiterCeilingSet = 1.0e9f;
    }

    cab.setReference (ownProduct.chain == Chain::bass ? core::CabinetIr::Reference::bass : core::CabinetIr::Reference::guitar);
    cab.prepare (sampleRate, block, channels);
    cabIrStale = true;   // the pump reloads the chosen IR into the freshly prepared engine

    // Seeded from the parameter: a session saved gate-ON has to start already gated, not fade in
    // over a block of ungated hum.
    gate.prepare (sampleRate, block, channels);
    gate.seedEnabled (linkWorks (params::rowGate));

    // The hum stage over the channels the INPUT carries — the hum is the input's, and a channel the
    // host only gives us to write into has none. Given its mode before it is prepared, so the restart
    // starts in it rather than ramping into it — the machine's, or under OFF the last one that was not,
    // OFF's crossfade starting where OFF says. A prepare it refuses (a layout with no input at all)
    // leaves it OFF, and the mode in force says OFF rather than the machine's AUTO.
    // With HUM switched off in the build (Features.h) it is never prepared at all: it stays out of the
    // sound and costs nothing, whatever the machine says or a session saved with HUM on — the way a
    // refused prepare leaves it, OFF in force and no LEARN of its own.
    {
        const int wanted = humWanted->load (std::memory_order_relaxed);
        auto p = humStage.params();
        p.mains = (felitronics::dehum::Mains) (wanted != (int) prefs::Hum::off ? wanted : humLastOn->load (std::memory_order_relaxed));
        humTunedPlay = humTunedQuiet = humTunedUpTo = -1.0f;      // handed over whatever it was before
        tuneHum (p);
        humStage.setParams (p);
        const bool ready = features::hum && humStage.prepare (sampleRate, block, getTotalNumInputChannels());
        humApplied = (int) p.mains;
        humRuns.store (ready ? humApplied : -1, std::memory_order_relaxed);
        humShown   = wanted;
        humDry.setSize (juce::jmax (1, getTotalNumInputChannels()), block, false, true, true);
        humMix     = wanted != (int) prefs::Hum::off ? 1.0f : 0.0f;
        humMixStep = (float) (1.0 / (HumStage::kRampSeconds * sampleRate));
        humLearnSamples = -1;    // a prepare is a restart, and a restart ends a learn
        humRate = sampleRate;
        humInForce.store (ready ? wanted : (int) prefs::Hum::off, std::memory_order_relaxed);
        humLearn.on.store (ready && wanted != (int) prefs::Hum::off, std::memory_order_relaxed);
        humLearn.running.store (false, std::memory_order_relaxed);
        humLearn.learnMs.store (-1.0f, std::memory_order_relaxed);
        humFamily.store (0, std::memory_order_relaxed);
        humHz.store (0.0f, std::memory_order_relaxed);
        humLocked.store (false, std::memory_order_relaxed);
        humDb.store ((float) HumStage::kSilentDb, std::memory_order_relaxed);
    }

    // Over the channels the bus carries — one or two, by the layouts this plugin accepts. A prepare
    // either refuses keeps that link out of the sound (see delayReady), the way the limiter's does.
    // A chain with no place for them never prepares them: they hold no memory and never run. (The bass's has them, out of
    // its strip until the player adds them: runSpaceStage.)
    delayReady  = chainHolds (params::rowDelay)  && delay.prepare (sampleRate, block, channels);
    reverbReady = chainHolds (params::rowReverb) && reverb.prepare (sampleRate, block, channels);
    preamp.prepare (sampleRate, block, channels);
    if (bass.compAmount != nullptr)   // the bass's COMP, or OrbitAmp's — and its right twin where the chain can be stereo
    {
        (void) bassComp.prepare (sampleRate, block);   // a refusal leaves it a wire (BassComp::process)
        bassComp.snapAmount (bass.compAmount->load());
        if (ownProduct.chain != Chain::bass)
        {
            (void) compRight.prepare (sampleRate, block);
            compRight.snapAmount (bass.compAmount->load());
            compRightRan = false;
        }
        compTap.prepare (sampleRate);
    }
    if (ownProduct.chain != Chain::bass)   // the guitar's input cut: snapped where it stands at the first block
        guitarInputCut.prepare (sampleRate);
    if (ownProduct.chain == Chain::bass)
    {
        octaveTap->prepare (sampleRate);
        filterTap.prepare (sampleRate);
        filterTapLive = false;
        bassCuts.prepare (sampleRate);
        for (auto& c : innerCuts)
            c.prepare (sampleRate);
        diNow  = juce::jlimit (0.0f, 1.0f, (100.0f - bass.cabMix->load()) * 0.01f);
        diStep = (float) (1000.0 / (juce::jmax (8000.0, sampleRate) * 20.0));   // the prepared rate, not getSampleRate()'s

        // The three effects: mono up to the chorus, which takes the bus's planes. TOLD WHERE THEY STAND BEFORE THEIR
        // PREPARES, which snap to what they were told: a slider at 0 or standing by is idle from the first sample (CHORUS's
        // stage used to arrive at its last knobs and fade out of them over 20 ms, wet at amount 0); one up arrives on its
        // knobs, not gliding to them. Their own doors are theirs (core).
        octaveToldValid = filterToldValid = chorusToldValid = false;
        tellOctave();
        tellFilter();
        tellChorus();
        octaverReady   = octaver.prepare (sampleRate, block, 1);
        envFilterReady = envFilter.prepare (sampleRate, block, 1);
        chorusReady    = chorus.prepare (sampleRate, block, juce::jlimit (1, 2, channels));
        chorusTap.prepare (sampleRate, chorus);   // its phase where the stage's restart put it
        for (auto& w : effectWorking)
            w.store (false, std::memory_order_relaxed);
        filterKey.setSize (1, block);

        // THE BACK HALF'S WIDTH: two planes from CHORUS on, on a stereo bus — ALWAYS, the chorus idle or not. Collapsing
        // to one while it idles would start the second plane's cabinet cold the moment it engages: an IR's length of
        // left and right apart. So the cabinet, the DI, the cuts and the limiter cost two planes here, and the load
        // window says so (LoadBreakdown). A narrower call is played at this width (processBlock).
        bassWide.store (juce::jlimit (1, 2, channels), std::memory_order_relaxed);
        narrowBus.setSize (juce::jlimit (1, 2, channels), block);
        diDry.setSize (juce::jmax (2, channels), block);

        // The DELAY and the REVERB: their input kept aside, the low cut on what they add, their pictures' taps.
        spaceDry.setSize (2, block);
        for (auto& stage : wetCuts)
            for (auto& c : stage)
            {
                c.design (true, params::bassWetCutHz, sampleRate);
                c.reset();
            }
        delayTap.prepare (sampleRate);
        reverbTap.prepare (sampleRate);
        spaceRunning = {};
    }
    demo.prepare (sampleRate);
    ear.prepare (sampleRate);
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
    preamp.onLanded = [this] { reportLatency(); };

    // Snapped, not faded: a chain that arrives switched off is silent from its first sample.
    for (int i = 0; i < params::numChainRows; ++i)
    {
        blockFade[(size_t) i].prepare (sampleRate);
        blockFade[(size_t) i].snapTo (linkWorks ((params::ChainRow) i));
    }
    // The bass's DELAY and REVERB fade on their place in the strip, not on their switch: standing by, they ring out.
    if (ownProduct.chain == Chain::bass)
        for (const auto row : { params::rowDelay, params::rowReverb })
            blockFade[(size_t) row].snapTo (linkInRig (row));
    if (bass.compAmount != nullptr)   // the bass's COMP is in toward its knob up as well (processChunk)
        blockFade[(size_t) params::rowComp].snapTo (linkWorks (params::rowComp) && bass.compAmount->load() > 0.0f);

    pumpDeviceWork();

    // ...and the AMP's BLEND as the state has it, whatever thread prepares: a host that sets it, prepares on a worker and
    // renders at once — an offline render, its message thread busy — plays it from the first block.
    if (bass.blend != nullptr)
        tellBlend();

    // The boost's level where the state has it — after the pump, which has found the pack's Boost.
    boostGain.reset (sampleRate, boostRampSeconds);
    boostGain.setCurrentAndTargetValue (boostTarget());

    // Reported ALWAYS, whether a block is switched on or not: a bypass path carries the same
    // delay, so a toggle never shifts the timing of everything downstream. A latency that changes
    // with a switch is what makes hosts re-align mid-song.
    reportLatency();

    updateEqSettings();
    updateDelaySettings();
    updateReverbSettings();
}

bool AmpProcessor::boostOn() const noexcept
{
    // No BOOST on the bass (the owner, 31.10): no key, no lift, no push up the dial.
    if (! linkWorks (params::rowPreamp) || ownProduct.chain == Chain::bass)
        return false;

    // The pack's own Boost, past its first position — the BOOST key lights the same way.
    if (const int slot = boostSlot.load (std::memory_order_relaxed); juce::isPositiveAndBelow (slot, params::numSelectors))
        return juce::roundToInt (preampSelectorParams[(size_t) slot]->load()) > 0;

    // ...or, on a pack that has none, the capture played up its dial.
    return preampBoostParam->load() > 0.5f;
}

float AmpProcessor::boostTarget() const noexcept
{
    return boostOn() ? juce::Decibels::decibelsToGain (boostLevelParam->load()) : 1.0f;
}

void AmpProcessor::updateDelaySettings() noexcept
{
    // The time: the division against the conducting tempo when sync is on, the free knob when it
    // is off. The host's tempo conducts when there is one and the player has not asked for the
    // block's own; the BPM field conducts otherwise — the standalone, where nobody else is counting.
    // The host's tempo is published whatever the switches say, so the face can offer it.
    double hostBpm = 0.0;

    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0)
                hostBpm = *bpm;

    hostTempo.store ((float) hostBpm, std::memory_order_relaxed);

    float ms = delayTimeMsParam->load();

    if (delaySyncParam->load() > 0.5f)
    {
        const double bpm = hostBpm > 0.0 && delayHostTempoParam->load() > 0.5f
                               ? hostBpm
                               : (double) delayBpmParam->load();

        const int div = juce::jlimit (0, params::delayDivisions.size() - 1,
                                      juce::roundToInt (delayDivParam->load()));
        // The delay's own ladder, at the conducting tempo however slow a host goes; a note longer
        // than the line is cut to the line by the delay itself. Not divisionMs: that floors the
        // tempo at the BPM field's 40, and a slower host's echo would leave its time.
        ms = (float) ((double) felitronics::delay::kDivisionBeats[div] * 60000.0 / bpm);
    }

    delay.setTimeMs (ms);
    delay.setRepeats (delayRepeatsParam->load() * 0.01f);   // the face reads percent

    // DARK is a percent of darkness; the corner it buys falls log-evenly from the bright
    // ceiling to the dark floor, so every degree of the knob darkens by the same ear-step.
    delay.setDarkHz (felitronics::delay::darkHzForPercent (delayDarkParam->load()));

    delay.setOffsetMs (delayOffsetParam->load());
    delay.setMix (delayMixParam->load() * 0.01f);
}

void AmpProcessor::updateReverbSettings() noexcept
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1,
                                    juce::roundToInt (reverbTypeParam->load()));

    reverb.setCharacter (static_cast<felitronics::reverb::Character> (index));
    reverb.setMix (reverbMixParam->load() * 0.01f);   // the face reads percent
    reverb.setDecay (reverbDecayParam->load());
    reverb.setPredelayMs (reverbPredelayParam->load());
    reverb.setHpfHz (reverbHpfHzParam->load());
}

template <typename Stage>
void AmpProcessor::runSpaceStage (Stage& stage, bool ready, params::ChainRow row, int which, core::SpaceTap& tap,
                                  const std::atomic<bool>& watched, bool adds, float* const* channels, int numChannels,
                                  int numSamples) noexcept
{
    // PRESENCE ALONE decides whether it runs — a call that carries no channel (a gap, law 11c) included.
    const bool inRig = linkInRig (row);
    const auto span  = blockFade[(size_t) row].advance (numSamples, inRig);
    const bool run   = ready && core::BypassFade::runs (span, inRig);

    // OUT OF THE STRIP: cleared once its fade is done, and not run at all — a stage added again starts silent.
    const auto clear = [&]
    {
        stage.reset();
        for (auto& c : wetCuts[(size_t) which])
            c.reset();
        tap.breakTrace();
        spaceRunning[(size_t) which] = false;
        spaceFresh[(size_t) which] = true;
    };

    if (! run)
    {
        if (spaceRunning[(size_t) which])
            clear();
        return;
    }
    spaceRunning[(size_t) which] = true;

    // Its picture written only while on screen AND at work — in the chain, switched on, its MIX up: standing by (its
    // tail ringing out or not) or at 0, it stands still (the owner, 31.10).
    const bool fed   = linkWorks (row);
    const bool write = watched.load (std::memory_order_relaxed) && fed && adds;

    // ADDED AGAIN, ITS FIRST CALL UNFED. A cleared stage's door stands wherever its first call's feed puts it, with no
    // travel (TapeDelay, AmpReverb) — open, it took the note already sounding mid-wave, and the echo gave that edge back
    // a delay later, a click at full level (the owner, 03.11: "not at once"). Shut for this one call, it opens on the
    // next over its own fade, as the switch opens it.
    const bool feed = fed && ! std::exchange (spaceFresh[(size_t) which], false);

    if (numChannels <= 0 || channels == nullptr)
    {
        // A GAP: the stage takes it as its own (its channels stop, its clocks go on); the wet's cuts and the picture's
        // hops run over the silence it adds there.
        (void) stage.process (nullptr, 0, numSamples, feed);
        for (auto& c : wetCuts[(size_t) which])
            for (int i = 0; i < numSamples; ++i)
                (void) c.process (0.0f);
        for (int i = 0; i < numSamples; ++i)
            tap.take (0.0f, write);
    }
    else
    {
        const int nch   = juce::jmin (numChannels, 2);
        const int chunk = juce::jmax (1, spaceDry.getNumSamples());

        // In pieces of the kept input's length: a host's longer call is played as several, the same samples.
        const int   ramp = juce::jlimit (0, numSamples, span.ramp);
        const float step = span.moving && ramp > 0 ? (span.to - span.from) / (float) ramp : 0.0f;

        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (chunk, numSamples - done);
            float* part[2] = { channels[0] + done, nch > 1 ? channels[1] + done : nullptr };

            for (int ch = 0; ch < nch; ++ch)
                juce::FloatVectorOperations::copy (spaceDry.getWritePointer (ch), part[ch], n);

            (void) stage.process (part, nch, n, feed);   // prepared for the bus's planes and its block: it cannot refuse

            for (int i = 0; i < n; ++i)
            {
                const int   at   = done + i;
                const float gain = ! span.moving ? span.to : (at < ramp ? span.from + step * (float) (at + 1) : span.to);
                float loudest = 0.0f;
                for (int ch = 0; ch < nch; ++ch)
                {
                    const float dry = spaceDry.getReadPointer (ch)[i];
                    const float wet = wetCuts[(size_t) which][(size_t) ch].process (part[ch][i] - dry) * gain;
                    part[ch][i] = dry + wet;
                    loudest = juce::jmax (loudest, std::abs (wet));
                }
                tap.take (loudest, write);
            }
            done += n;
        }
    }

    // ITS FADE OUT OF THE STRIP ENDED IN THIS CALL: cleared now, not a call later — added again in the very next call, it
    // starts silent, its old tail gone.
    if (! inRig && ! (span.to > 0.0f))
        clear();
}

void AmpProcessor::tellBlend()
{
    const std::scoped_lock control (blendControl);
    preamp.setCrossover (1.0f - bass.blend->load(), bass.blendHz->load(), bass.blendInvert->load() > 0.5f);   // BLEND 1: the capture alone
}

namespace
{
    /** The same knobs, byte for byte. Sound only while the structs hold their fields and nothing between them — no
        padding a memcmp would read: the sizes below are the sums of their fields, and a field added in core stops the
        build here, to be looked at. */
    template <typename P>
    bool sameKnobs (const P& a, const P& b) noexcept { return std::memcmp (&a, &b, sizeof (P)) == 0; }

    static_assert (sizeof (felitronics::octave::OctaverParams) == 7 * sizeof (float), "OctaverParams: 7 floats, no padding");
    static_assert (sizeof (felitronics::envfilter::EnvelopeFilterParams)
                       == 9 * sizeof (float) + sizeof (felitronics::envfilter::Direction) + sizeof (felitronics::envfilter::Mode),
                   "EnvelopeFilterParams: 9 floats and 2 enums, no padding");
    static_assert (sizeof (felitronics::chorus::BassChorusParams) == 5 * sizeof (float), "BassChorusParams: 5 floats, no padding");
}

void AmpProcessor::tellOctave() noexcept
{
    // The menu is the stage's current knobs — what it was last told, the law's own fields with them — and the menu's
    // fields from their parameters (the family's contract: paramsForAmount's menu is the stage's current params).
    auto menu = octaveToldValid ? octaveTold : felitronics::octave::OctaverParams {};
    menu.sub2Top = 0.0f;
    menu.toneHz  = bass.octaveToneHz->load();
    menu.subHpHz = params::octaveSubHpHz;
    menu.gateDb  = linkWorks (params::rowGate) && gateThresholdParam != nullptr
                     ? juce::jlimit (-90.0f, -20.0f, gateThresholdParam->load()) : params::octaveGateIdleDb;
    const float amount = linkWorks (params::rowOctave) ? bass.octave->load() : 0.0f;
    octaveLive = amount > 0.0f;   // at work: its picture written (runOctave)
    octaveWorks.store (octaveLive, std::memory_order_relaxed);
    const auto  p = felitronics::octave::paramsForAmount (amount, menu);
    if (! octaveToldValid || ! sameKnobs (p, octaveTold))
    {
        octaver.setParams (p);
        ++effectTold[0];
        octaveTold = p;
        octaveToldValid = true;
    }
}

felitronics::envfilter::EnvelopeFilterParams AmpProcessor::filterKnobsNow (felitronics::envfilter::EnvelopeFilterParams menu) const noexcept
{
    if (bass.filter == nullptr)
        return menu;
    const int range = juce::jlimit (0, 2, juce::roundToInt (bass.filterRange->load()));
    menu.attackMs   = params::filterAttackMs;
    menu.releaseMs  = bass.filterReleaseMs->load();
    menu.sweepLoHz  = params::filterRangeHz[range][0];
    menu.sweepHiHz  = params::filterRangeHz[range][1];
    menu.direction  = juce::roundToInt (bass.filterDirection->load()) == 1 ? felitronics::envfilter::Direction::down
                                                                            : felitronics::envfilter::Direction::up;
    menu.mode       = juce::roundToInt (bass.filterMode->load()) == 1 ? felitronics::envfilter::Mode::bandPass
                                                                       : felitronics::envfilter::Mode::lowPass;
    menu.resonance  = bass.filterResonance->load();
    menu.keepLows   = bass.filterKeepLows->load() > 0.5f ? 1.0f : 0.0f;
    menu.keepLowsHz = bass.filterKeepHz->load();
    const float amount = linkWorks (params::rowFilter) ? bass.filter->load() : 0.0f;
    return felitronics::envfilter::paramsForAmount (amount, menu);
}

void AmpProcessor::tellFilter() noexcept
{
    const auto p = filterKnobsNow (filterToldValid ? filterTold : felitronics::envfilter::EnvelopeFilterParams {});
    filterLive = bass.filter != nullptr && linkWorks (params::rowFilter) && bass.filter->load() > 0.0f;   // its picture written
    if (! filterToldValid || ! sameKnobs (p, filterTold))
    {
        envFilter.setParams (p);
        ++effectTold[1];
        filterTold = p;
        filterToldValid = true;
    }
}

void AmpProcessor::tellChorus() noexcept
{
    auto menu = chorusToldValid ? chorusTold : chorus.params();
    menu.rateHz      = bass.chorusRateHz->load();
    menu.crossoverHz = bass.chorusCrossoverHz->load();
    menu.delayMs     = params::chorusCentreMs;
    const float amount = linkWorks (params::rowChorus) ? bass.chorus->load() : 0.0f;
    chorusLive = amount > 0.0f;   // at work: its picture written (runChorus)
    const auto  p = felitronics::chorus::paramsForAmount (amount, menu);
    if (! chorusToldValid || ! sameKnobs (p, chorusTold))
    {
        chorus.setParams (p);
        ++effectTold[2];
        chorusTold = p;
        chorusToldValid = true;
    }
}

void AmpProcessor::runOctave (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! octaverReady)
        return;
    tellOctave();
    // Its picture written only while on screen AND at work: standing by or at 0 it stands still (the owner, 31.10).
    if (numChannels < 1 || numChannels > 2 || ! octaveWatched.load (std::memory_order_relaxed) || ! octaveLive)
    {
        (void) octaver.process (channels, numChannels, numSamples);
        return;
    }

    // Its picture on screen: the stage run a piece at a time, its input kept before it adds the sub, and both handed to
    // the tap. The stage's output does not depend on how its calls are sliced (its law 8: the audio-time grid), so the
    // pieces play bit for bit what one call would.
    for (int done = 0; done < numSamples; )
    {
        const int n = juce::jmin (numSamples - done, (int) octaveDry.size());
        float* piece[2] = { channels[0] + done, numChannels > 1 ? channels[1] + done : nullptr };
        std::copy_n (piece[0], n, octaveDry.data());
        (void) octaver.process (piece, numChannels, n);
        octaveTap->write (octaveDry.data(), piece[0], n);
        done += n;
    }
}

void AmpProcessor::runFilter (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! envFilterReady)
        return;
    tellFilter();
    const bool keyed = numChannels > 0 && numSamples <= filterKey.getNumSamples();

    // ITS PICTURE ON SCREEN: the stage run a COLUMN at a time, each piece ending where the tap closes a column, so the trail
    // carries a cutoff per column, not a block's. Its output does not depend on how its calls are sliced (law 8: the
    // audio-time grid), so the pieces play bit for bit what one call would. A fresh look takes the next column whatever
    // it holds. Standing by or at 0 it is not written: the picture stands still (the owner, 31.10); at work again, a fresh
    // look.
    const bool watched = filterWatched.load (std::memory_order_relaxed) && filterLive;
    if (watched && ! filterTapLive)
        filterTap.wake();
    filterTapLive = watched;
    if (! watched || numChannels < 1)
    {
        const float* key[] = { filterKey.getReadPointer (0) };
        (void) envFilter.process (channels, numChannels, numSamples, keyed ? key : nullptr, keyed ? 1 : 0);
        if (watched)
            filterTap.write (envFilter.shownCutoffHz(), numSamples);
    }
    else
        for (int done = 0; done < numSamples; )
        {
            const int n = juce::jmin (numSamples - done, filterTap.untilColumn());
            float* piece[2] = { channels[0] + done, numChannels > 1 ? channels[1] + done : nullptr };
            const float* key[] = { filterKey.getReadPointer (0) + done };
            (void) envFilter.process (piece, numChannels, n, keyed ? key : nullptr, keyed ? 1 : 0);
            filterTap.write (envFilter.shownCutoffHz(), n);
            done += n;
        }
    effectWorking[1].store (! envFilter.isIdle(), std::memory_order_relaxed);
}

void AmpProcessor::runChorus (float* const* channels, int numChannels, int numSamples) noexcept
{
    if (! chorusReady)
        return;
    tellChorus();
    const bool wasIdle = chorus.isIdle();
    const bool ran     = chorus.process (channels, numChannels, numSamples);
    chorusTap.follow (chorus, wasIdle, ran ? numSamples : 0,
                      chorusWatched.load (std::memory_order_relaxed) && chorusLive);   // at work only: standing by, still
    effectWorking[2].store (! chorus.isIdle(), std::memory_order_relaxed);
}

void AmpProcessor::passGap (int numSamples) noexcept
{
    // A GAP IS TIME WITH SILENCE: the five cuts run over silence of its length — their memories decaying, their glides and
    // fades going on — a block at a time on the scratch bus (no allocation); the input's cut makes FILTER's key of it, as
    // it would of a silent input; the three effects take the gap as theirs (law 11c) — and DELAY and REVERB, told their
    // knobs first (the host's tempo with them), so a change made before a gap glides through it as through silence.
    sanitiseParameters();
    updateDelaySettings();
    updateReverbSettings();
    const int most = juce::jmax (1, juce::jmin (preparedBlock.load (std::memory_order_relaxed), narrowBus.getNumSamples(),
                                                filterKey.getNumSamples()));
    const int wide = juce::jlimit (1, juce::jmin (2, narrowBus.getNumChannels()), bassWide.load (std::memory_order_relaxed));
    const auto inner = innerCutSettings();
    const auto end   = bassCutSettings();
    for (int start = 0; start < numSamples; start += most)
    {
        const int len = juce::jmin (most, numSamples - start);
        float* plane[] = { narrowBus.getWritePointer (0) };
        for (int k = 0; k < (int) innerCuts.size(); ++k)
        {
            narrowBus.clear (0, 0, len);
            innerCuts[(size_t) k].process (plane, 1, len, inner);
            if (k == 0)
                filterKey.copyFrom (0, 0, narrowBus, 0, 0, len);
        }
        narrowBus.clear (0, len);
        bassCuts.process (narrowBus.getArrayOfWritePointers(), wide, len, end);

        runOctave (nullptr, 0, len);
        if (envFilterReady)
        {
            // The key is consumed through a gap (envfilter's contract) — a column at a time while ENVELOPE's picture is on
            // screen, so it follows the sweep falling through the gap as through silence.
            tellFilter();
            const bool watched = filterWatched.load (std::memory_order_relaxed) && filterLive;
            if (watched && ! filterTapLive)
                filterTap.wake();
            filterTapLive = watched;
            for (int done = 0; done < len; )
            {
                const int n = watched ? juce::jmin (len - done, filterTap.untilColumn()) : len - done;
                const float* key[] = { filterKey.getReadPointer (0) + done };
                (void) envFilter.process (nullptr, 0, n, key, 1);
                if (watched)
                    filterTap.write (envFilter.shownCutoffHz(), n);
                done += n;
            }
            effectWorking[1].store (! envFilter.isIdle(), std::memory_order_relaxed);
        }
        runChorus (nullptr, 0, len);

        // ...and DELAY and REVERB, present and running: theirs too — their channels stop (core's law 11a), their clocks,
        // their doors and their presence fades go on.
        runSpaceStage (delay, delayReady, params::rowDelay, 0, delayTap, delayWatched, delayAdds(), nullptr, 0, len);
        runSpaceStage (reverb, reverbReady, params::rowReverb, 1, reverbTap, reverbWatched, reverbAdds(), nullptr, 0, len);
    }
}

core::BassCuts::Settings AmpProcessor::innerCutSettings() const noexcept
{
    auto s  = bassCutSettings();
    s.hpfHz /= 3.0;                                                     // a third of the chosen: a tenth of a decibel of the lowest string
    s.hpfOn  = s.hpfOn && ! innerCutsOut.load (std::memory_order_relaxed);
    s.lpfOn  = false;                                                   // the LPF is the END's alone
    return s;
}

core::BassCuts::Settings AmpProcessor::bassCutSettings() const noexcept
{
    const auto hpf = static_cast<params::BassHpf> (juce::jlimit (0, 1, juce::roundToInt (bass.hpf->load())));
    const auto lpf = static_cast<params::BassLpf> (juce::jlimit (0, 1, juce::roundToInt (bass.lpf->load())));

    core::BassCuts::Settings s;
    s.hpfOn = bass.hpfOn->load() > 0.5f;
    s.hpfHz = (double) (hpf == params::BassHpf::four ? bass.hpfHz4 : bass.hpfHz5)->load();
    s.lpfOn = bass.lpfOn->load() > 0.5f;
    s.lpfHz = (double) (lpf == params::BassLpf::slap ? bass.lpfHzSlap : bass.lpfHzFingers)->load();
    return s;
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
        s.hpfOn    = ! bassEq() && p.hpfOn->load() > 0.5f;   // the bass's HPF is its own (bassCuts)
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
        s.lpfOn    = ! bassEq() && p.lpfOn->load() > 0.5f;   // ...and its LPF
        s.lpfHz    = (double) p.lpfHz->load();
        s.lpfSlope = slope (p.lpfSlope->load());

        // WHOSE TONE STACK IS IN THE SIGNAL. The two are alternatives, and until now only half of
        // that was true: `setRaw` parked the device's curves when ours was chosen, but ours went on
        // filtering when the device's was — active, and with no controls on the face, because the
        // console had swapped its row for the device's knobs. A shape somebody dialled in and left
        // behind kept playing where nothing could reach it.
        //
        // Only the TONE sections step aside. The cut filters belong to the BLOCK whichever tone is
        // playing: the walls are utility.
        //
        // Asked of the block, not of the parameter alone: a device that measured nothing has no
        // native stack to hand over, its face collapses the choice to ours, and the DSP has to
        // reach the same verdict or the block ends up with no tone stack at all.
        //
        // The parameter is looked up once (EqLinkParams::eqMode): its id is built from strings, and building it here
        // allocated twice a block on the audio thread.
        const auto mode = static_cast<params::EqMode> (juce::roundToInt (eqParams[(size_t) l].eqMode->load()));

        const auto& blockFor = blockAt ((size_t) l);

        // THE BASS'S EQ IS THE SGT-DI'S (params::bassMidMinHz): BASS, MIDDLE at its swept frequency and TREBLE — the
        // second bell and the scalpel are held out of its sound. Ours on a pack with no tone of its own; on a pack with
        // one, the pack's plays (the owner, 02.11: two kinds of EQ, the rig's and the standard) and ours stands flat.
        if (bassEq())
        {
            s.b1Hz = juce::jlimit ((double) params::bassMidMinHz, (double) params::bassMidMaxHz, s.b1Hz);
            s.b2Db = 0.0;
            s.b3On = false;
            s.b3Db = 0.0;
            if (blockFor.hasWearableTone())
                s.loDb = s.b1Db = s.hiDb = 0.0;
        }
        else if (mode == params::EqMode::native && blockFor.hasWearableTone())
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

params::StereoMode AmpProcessor::modeOfEnvironment() const
{
    // Only what the AudioProcessor itself knows, and its product, which is made before anything that
    // reads this: the layout reads it while this processor's own members are still being made. The
    // bass's chain is one signal wherever it runs (Product::chain).
    if (ownProduct.chain == Chain::bass)
        return params::StereoMode::mono;

    return wrapperType == wrapperType_Standalone ? params::StereoMode::stereoSpace
         : getTotalNumInputChannels() >= 2       ? params::StereoMode::stereo
                                                 : params::StereoMode::mono;
}

void AmpProcessor::processorLayoutsChanged()
{
    // The mode's environment default — see modeAutoValue. A host settles the buses before it activates
    // the plug-in, and a CLAP host holds a plug-in's parameters still from there: prepared and playing,
    // nobody's hand on them, they keep the values the host last read.
    if (stateWasRestored)
        return;

    auto* p = apvts.getParameter (params::stereoMode);
    if (p == nullptr)
        return;

    const int want = (int) modeOfEnvironment();
    if (juce::roundToInt (p->convertFrom0to1 (p->getValue())) == modeAutoValue && want != modeAutoValue)
    {
        p->setValueNotifyingHost (p->convertTo0to1 ((float) want));
        modeAutoValue = want;
    }
}

void AmpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // The two things the engine listens for in MIDI: a Program Change and a controller's value
    // (takeProgramChange, takeControlChange). The standalone hands over whatever the inputs switched
    // on in its settings sent; a host sends nothing to a plug-in that asks for no MIDI input. Read off
    // the raw bytes — a MidiMessage made of a long SysEx passing by would allocate.
    for (const auto event : midi)
    {
        if (event.numBytes == 2 && (event.data[0] & 0xf0) == 0xc0)
            midiProgram.store (event.data[1] & 0x7f, std::memory_order_relaxed);
        else if (event.numBytes == 3 && (event.data[0] & 0xf0) == 0xb0)
            midiControl[(size_t) (event.data[1] & 0x7f)].store (event.data[2] & 0x7f, std::memory_order_relaxed);
    }

    // A HOST'S BLOCK IS NOT ALWAYS THE BLOCK IT PROMISED. prepareToPlay names the most it will
    // send, and every stage with a scratch buffer — the room's wet copy, the fades' dry copy, the
    // captured blocks — is sized to that; a host that sends more (an offline render, a bounce, a
    // host that simply does) used to find the room adding its tail to the first part of the block
    // only, and the fades skipping. So a block longer than the promise is played as consecutive
    // blocks of the promised size, which is exactly what the chain would have heard had the host
    // kept it. The views refer to the host's own memory: nothing is allocated.
    const int total = buffer.getNumSamples();
    const int most  = preparedBlock.load (std::memory_order_relaxed);

    // THE BASS'S BACK HALF HAS ONE WIDTH, the prepared bus's (bassWide, see prepareToPlay). A call carrying fewer
    // channels is played on a scratch bus of that width — its missing plane the one signal the chain carries up to
    // CHORUS anyway — and the planes it does carry are handed back, so no stage ever sees its width change without a
    // prepare: the cuts keep every plane's coefficients, no delay line keeps a stale plane, the limiter never resets
    // under the left. 2 -> 1 -> 2 plays exactly as calls that never narrowed. A call of positive length carrying NO
    // channel is a gap (law 11c): the three effects are told it, their shared clocks running as through silence.
    if (const int wide = bassWide.load (std::memory_order_relaxed); wide > 0 && most > 0 && buffer.getNumChannels() < wide)
    {
        if (buffer.getNumChannels() == 0)
        {
            if (total > 0)
                passGap (total);
            return;
        }
        for (int start = 0; start < total; start += most)
        {
            const int len = juce::jmin (most, total - start);
            for (int ch = 0; ch < wide; ++ch)
                narrowBus.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), start, len);
            juce::AudioBuffer<float> part (narrowBus.getArrayOfWritePointers(), wide, 0, len);
            processChunk (part);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.copyFrom (ch, start, narrowBus, ch, 0, len);
        }
        return;
    }

    if (most <= 0 || total <= most)
    {
        processChunk (buffer);
        return;
    }

    for (int start = 0; start < total; start += most)
    {
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                                       start, juce::jmin (most, total - start));
        processChunk (part);
    }
}

void AmpProcessor::sanitiseParameters() noexcept
{
    // A PARAMETER THAT IS NOT A NUMBER. A host's automation point, a broken session, a controller
    // that sends garbage: JUCE clamps a value into its range, but a clamp of NaN is NaN, and every
    // stage downstream would take it — a room whose decay is NaN is a room that is NaN, healed and
    // re-poisoned every block, silent for as long as the lane holds it. One pass here, before any
    // stage reads anything: a value that is not a number reads as the parameter's default until
    // someone sends a real one. A couple of hundred compares a block.
    // One that holds its last (the bass's effects' sliders) reads as the last number it had instead.
    for (auto& guard : parameterGuards)
    {
        const float v = guard.value->load (std::memory_order_relaxed);
        if (! std::isfinite (v))
            guard.value->store (guard.holdsLast ? guard.last : guard.fallback, std::memory_order_relaxed);
        else if (guard.holdsLast)
            guard.last = v;
    }
}

/** HOW HARD THE STAGE WORKS — TEMPORARY, for tuning by ear (ui/HumTuning.h). The machine's three
    numbers into the stage's params when any of them moved, and false when none did. The widths take
    effect at once; a new reach is a new set of harmonics, which the stage ramps out and learns again.
    At 1.2 kHz the reach is the stage's own — 24 harmonics under a 2 kHz bound — the stage as it ran
    before there was a reach to choose; any other runs every harmonic the stage has, under a bound
    that keeps, at 50 Hz, the last one at or under the frequency named (the stage reckons a harmonic at
    the top of its lock range). */
bool AmpProcessor::tuneHum (felitronics::dehum::HumRemoverParams& p) noexcept
{
    const float play  = humPlayWanted->load (std::memory_order_relaxed);
    const float quiet = humQuietWanted->load (std::memory_order_relaxed);
    const float upTo  = humUpToWanted->load (std::memory_order_relaxed);
    if (juce::exactlyEqual (play, humTunedPlay) && juce::exactlyEqual (quiet, humTunedQuiet) && juce::exactlyEqual (upTo, humTunedUpTo))
        return false;
    humTunedPlay = play;
    humTunedQuiet = quiet;
    humTunedUpTo = upTo;

    const felitronics::dehum::HumRemoverParams own;
    p.bandwidthHz      = (double) play;
    p.learnBandwidthHz = (double) quiet;
    if (upTo <= prefs::humtune::upToOwn)
    {
        p.harmonics = own.harmonics;
        p.maxHz     = own.maxHz;
    }
    else
    {
        p.harmonics = HumStage::kMaxHarmonics;
        p.maxHz     = (double) upTo * (50.0 + HumStage::kMaxLockRangeHz) / 50.0;
    }

    humTuned.playHz.store ((float) p.bandwidthHz, std::memory_order_relaxed);
    humTuned.quietHz.store ((float) p.learnBandwidthHz, std::memory_order_relaxed);
    humTuned.maxHz.store ((float) p.maxHz, std::memory_order_relaxed);
    humTuned.harmonics.store (p.harmonics, std::memory_order_relaxed);
    return true;
}

void AmpProcessor::removeHum (juce::AudioBuffer<float>& buffer) noexcept
{
    // Refused at prepare, or HUM switched off in the build (Features.h): nothing to run, and the mode
    // in force already says OFF.
    if (! features::hum || ! humStage.isPrepared())
        return;

    // The machine's mode, handed over here on the audio thread — the stage's threading contract —
    // and only when it changed: a change of family ramps the old correction out and re-arms.
    // OFF IS A BYPASS, NOT A STOP. The stage goes on in the last mode that was not OFF, learning as it
    // did, and only what is heard of it fades — so OFF and back is an A/B: on the way back the hum
    // comes off at once, by what was learned, with no decision to make again and nothing to relearn.
    // While it is bypassed the mode in force says OFF, and LEARN is the gate's alone.
    const int wanted = humWanted->load (std::memory_order_relaxed);
    {
        auto p = humStage.params();
        bool moved = false;
        if (const int runs = wanted != (int) prefs::Hum::off ? wanted : humLastOn->load (std::memory_order_relaxed);
            runs != humApplied)
        {
            p.mains = (felitronics::dehum::Mains) runs;
            humApplied = runs;
            humRuns.store (runs, std::memory_order_relaxed);
            moved = true;
        }
        if (tuneHum (p))
            moved = true;
        if (moved)
            humStage.setParams (p);
    }
    if (wanted != humShown)
    {
        humShown = wanted;
        humInForce.store (wanted, std::memory_order_relaxed);
        humLearn.on.store (wanted != (int) prefs::Hum::off, std::memory_order_relaxed);
    }

    // THE HUM'S LEARN, asked for by its take (core/HumLearnTake.h), served here before this block is
    // heard: the stage learns where it runs. Its answer goes back field by field through the link (core/HumLearnLink.h)
    // — atomics, nothing locked, nothing allocated.
    if (const int ask = humLearn.ask.exchange (core::HumLearnLink::none, std::memory_order_acq_rel);
        ask == core::HumLearnLink::begin)
    {
        humStage.beginLearn();
        humLearnSamples = humStage.learning() ? 0 : -1;
        humLearnArmedMs = -1.0f;
    }
    else if (ask == core::HumLearnLink::keep || ask == core::HumLearnLink::drop)
    {
        const auto r = humStage.endLearn (ask == core::HumLearnLink::keep);
        humLearnSamples = -1;
        humLearn.tell ({ (int) r.verdict, r.kept,
                         r.mains == felitronics::dehum::Mains::Hz50 ? 50 : r.mains == felitronics::dehum::Mains::Hz60 ? 60 : 0,
                         (float) r.humDb, (float) r.settledAtMs, (float) r.floorAfterDb });
    }

    // The input's channels and nothing past them; a host that asks for fewer this block stops the rest,
    // which the stage keeps a model for and fades back in when they return.
    // AND ONLY THE ONES THE CHAIN PLAYS. In MONO and in STEREO SPACE the amp eats channel 0 alone (see
    // `nch` below), and the stage's ear sums its channels: a second input that never reaches the amp —
    // an empty socket on the interface with its gain up, hissing — was enough to keep it from ever
    // hearing the room quiet, so AUTO never decided and the hum it did take off came off slowly.
    // Outside STEREO the second channel stops, as any channel a host stops does.
    const auto playing = ownProduct.chain == Chain::bass   // the bass's chain is mono, whatever the mode says
                             ? params::StereoMode::mono
                             : static_cast<params::StereoMode> (
                                   juce::jlimit (0, params::stereoModes.size() - 1, juce::roundToInt (stereoModeParam->load())));
    const int played = playing == params::StereoMode::stereo ? buffer.getNumChannels() : 1;
    const int numIn = juce::jmin (juce::jmin (getTotalNumInputChannels(), played, buffer.getNumChannels()),
                                  humStage.maxChannels(), humDry.getNumChannels());
    const int n     = juce::jmin (buffer.getNumSamples(), humDry.getNumSamples());
    const float target = wanted != (int) prefs::Hum::off ? 1.0f : 0.0f;
    const bool  fading = humMix < 1.0f || target < 1.0f;
    if (fading)
        for (int c = 0; c < numIn; ++c)
            humDry.copyFrom (c, 0, buffer, c, 0, n);

    [[maybe_unused]] const bool ran = humStage.process (buffer.getArrayOfWritePointers(), numIn, n);
    jassert (ran);

    // OFF's crossfade, out = dry + g·(wet - dry) with g ramped over the stage's own 20 ms, a step a
    // sample. Settled at 0 it is the input as it came, bit for bit; settled at 1 nothing here runs.
    if (fading)
    {
        float g = humMix;
        for (int c = 0; c < numIn; ++c)
        {
            const float* dry = humDry.getReadPointer (c);
            float* io = buffer.getWritePointer (c);
            g = humMix;
            for (int i = 0; i < n; ++i)
            {
                g = g < target ? juce::jmin (target, g + humMixStep) : juce::jmax (target, g - humMixStep);
                io[i] = g <= 0.0f ? dry[i] : g >= 1.0f ? io[i] : dry[i] + g * (io[i] - dry[i]);
            }
        }
        humMix = numIn > 0 ? g : target;
    }

    const auto family = humStage.detected();
    humFamily.store (family == felitronics::dehum::Mains::Hz50 ? 50 : family == felitronics::dehum::Mains::Hz60 ? 60 : 0,
                     std::memory_order_relaxed);
    humHz.store ((float) humStage.mainsHz(), std::memory_order_relaxed);
    humTuned.active.store (humStage.activeHarmonics(), std::memory_order_relaxed);
    humLocked.store (humStage.locked(), std::memory_order_relaxed);
    humDb.store ((float) humStage.humDb(), std::memory_order_relaxed);

    // The learn's own clock, at the end of the block it has now heard — what a tick of the take is
    // read against. A learn the stage ended by itself (a restart) is no learn any more.
    // And when in it the stage started on a model — zeroed, with its family, past its ramp — which is
    // what the take reckons the stage's settled window from.
    if (humLearnSamples >= 0 && humStage.learning())
        humLearnSamples += buffer.getNumSamples();
    else
        humLearnSamples = -1;
    const float learnMs = humLearnSamples < 0 ? -1.0f : (float) (1000.0 * (double) humLearnSamples / humRate);
    if (humLearnSamples < 0)
        humLearnArmedMs = -1.0f;
    else if (humLearnArmedMs < 0.0f && humStage.running() && ! humStage.swapping())
        humLearnArmedMs = learnMs;
    humLearn.learnMs.store (learnMs, std::memory_order_relaxed);
    humLearn.armedAtMs.store (humLearnArmedMs, std::memory_order_relaxed);
    humLearn.running.store (humStage.running(), std::memory_order_relaxed);
}

void AmpProcessor::processChunk (juce::AudioBuffer<float>& buffer)
{
    juce::ScopedNoDenormals noDenormals;
    sanitiseParameters();
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

    // THE HUM COMES OFF BEFORE ANYTHING ELSE TOUCHES THE INPUT — the trim included. Mains hum is in
    // the signal the interface hands over, at the level the interface hands it over at; a stage after
    // the trim would hear a different hum every time the INPUT lever moved, or a preset moved it, and
    // the hum would be back for as long as the stage took to learn the new one. Here the lever, the
    // presets and everything after them only ever meet an input the hum has already left. The demo
    // loop stands in for the input, so it is heard the same way. Zero latency: nothing downstream
    // moves.
    removeHum (buffer);

    // The input trim next, straight after the hum — ahead of the tuner's ear and the gate's key,
    // like the interface knob it stands in for: everything downstream, meters included, hears the trimmed level,
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
    //
    // THE BASS'S CHAIN IS MONO, whatever the mode's parameter says (a state of another amp's, a host's
    // automation): one signal from the gate to the limiter, copied to the outputs at the end. Its stereo
    // will be its CHORUS's alone, and only above the low end.
    const auto mode = ownProduct.chain == Chain::bass
                          ? params::StereoMode::mono
                          : static_cast<params::StereoMode> (
                                juce::jlimit (0, params::stereoModes.size() - 1, juce::roundToInt (stereoModeParam->load())));
    const int nch     = mode == params::StereoMode::stereo ? numChannels : juce::jmin (1, numChannels);
    // The bass's chain turns stereo at its CHORUS (Chain::bass): one signal up to it, the bus's planes from it on —
    // the cabinet, the DI, the cuts, OUT and LIMIT per channel. A mono bus stays one voice.
    // ...never wider than it was prepared: a host handing more channels than the bus it prepared gets the left copied to
    // the rest after the limiter, as a mono bus's outputs are.
    const int nchBack = ownProduct.chain == Chain::bass   ? juce::jmin (juce::jmax (1, bassWide.load (std::memory_order_relaxed)), 2, numChannels)
                      : mode == params::StereoMode::mono   ? nch : numChannels;

    // The captured blocks take a BUFFER — this alias holds only the channels the chain works.
    juce::AudioBuffer<float> chainView (const_cast<float**> (channels), nch, numSamples);

    // gate -> preamp -> EQ -> delay -> reverb. The gate stands at the very
    // front, right after the tuner's ear: it keys off the raw guitar — the cleanest key there is —
    // and kills the hum before any dirt can multiply it. Its enable crossfade makes the toggle
    // pop-free, so it runs unconditionally and the switch is an argument.
    // THE BASS'S HPF ACROSS THE CHAIN (innerCuts): its gentle cuts at the input and after COMP, OCTAVE and the AMP.
    const auto innerSettings = bass.hpf != nullptr ? innerCutSettings() : core::BassCuts::Settings {};
    const auto innerCut = [&] (int at)
    {
        if (bass.hpf != nullptr && nch > 0)
            innerCuts[(size_t) at].process (channels, 1, numSamples, innerSettings);
    };
    const auto tGate = PerfClock::now();
    {
        // Of the channels the chain WORKS, not of everything that was handed in: in MONO and in STEREO
        // SPACE the amp eats channel 0 alone, and a second input that never reaches it must not move
        // the meter the IN trim is set by, nor light its clip.
        float peak = 0.0f;
        for (int ch = 0; ch < nch; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        gateKeyDb.store (juce::Decibels::gainToDecibels (peak, -90.0f));

        // ...and what a world hears of it (takePlay): the peak, and the stroke heard in channel 0's top —
        // only while the view is lit by the guitar (viewLit); off, nothing listens.
        if (viewLit())
        {
            if (buffer.getNumChannels() > 0)
                ear.listen (buffer.getReadPointer (0), numSamples);
            const float pick = ear.take().pick;
            const auto raise = [] (std::atomic<float>& a, float v)
            {
                for (float held = a.load (std::memory_order_relaxed); v > held && ! a.compare_exchange_weak (held, v, std::memory_order_relaxed);) {}
            };
            raise (playPeak, peak);
            raise (playPick, pick);
        }

        if (peak > 1.0f)
            inClip.store (true);   // latched; the IN rail's cap clears it
    }

    // The gate KEYS here whatever it ends up muting — dual detection: the decision is made on the
    // raw input, the attenuation lands where the parameter says. The enable crossfade makes the
    // toggle pop-free, so analyse runs unconditionally and the switch is an argument.
    // THE BASS'S INPUT CUT (innerCuts) — after the IN meter, which reads the jack (a DI's rumble that clips shows there),
    // and before the gate's key and FILTER's: no detector pumps on sub rumble.
    innerCut (0);

    // THE GUITAR'S INPUT CUT (params::guitarInputCutHz), every guitar amp's, always: at the same place — after the IN meter,
    // before every key and every stage.
    if (ownProduct.chain != Chain::bass && nch > 0)
    {
        core::BassCuts::Settings in;
        in.hpfOn = true;
        in.hpfHz = params::guitarInputCutHz;
        guitarInputCut.process (channels, nch, numSamples, in);
    }

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

    // FILTER's key, kept before COMP touches the signal: the gated input, the string itself — the filter follows the
    // playing, not the compressed signal nor the octave's sub.
    if (bass.filter != nullptr && nch > 0 && numSamples <= filterKey.getNumSamples())
    {
        filterKey.copyFrom (0, 0, buffer, 0, 0, numSamples);

        // ...gated wherever the gate mutes: at the START the copy is taken after the mute; at the END (the default) the
        // curve the gate has just analysed is laid on the copy here, so a closed gate closes the detector too and the
        // playing under the threshold does not move the sweep.
        if (! muteAtStart)
        {
            float* key[] = { filterKey.getWritePointer (0) };
            (void) gate.applyGain (key, 1, numSamples);
        }
    }

    // THE BASS'S COMP (Chain::bass), after the gate, on its one signal: ONE KNOB (core::BassComp). Its STANDBY is
    // the house's crossfade, toward the link in AND its knob up; standing by with the knob up it is fed, not heard;
    // at 0 nothing is computed and the block passes bit for bit. OrbitAmp's the same, before its AMP as a pedal stands;
    // in STEREO its right signal through a twin of its own (compRight), started fresh whenever it starts working again.
    if (bass.compAmount != nullptr && nch > 0)
    {
        const auto a = PerfClock::now();
        const double amount = (double) bass.compAmount->load();
        const auto span = blockFade[(size_t) params::rowComp].advance (numSamples, linkWorks (params::rowComp) && amount > 0.0);
        const auto attack    = static_cast<core::BassComp::Attack>    (juce::jlimit (0, 2, juce::roundToInt (bass.compAttack->load())));
        const auto release   = static_cast<core::BassComp::Release>   (juce::jlimit (0, 2, juce::roundToInt (bass.compRelease->load())));
        const auto character = static_cast<core::BassComp::Character> (juce::jlimit (0, core::BassComp::characterCount - 1, juce::roundToInt (bass.compCharacter->load())));
        const double threshold = (double) bass.compThreshold->load(), ratio = (double) bass.compRatio->load();
        bassComp.tap = compWatched.load (std::memory_order_relaxed) ? &compTap : nullptr;   // the picture on screen, or nobody
        bassComp.process (channels[0], numSamples, amount, span, attack, release, character, threshold, ratio);
        if (nch > 1 && ownProduct.chain != Chain::bass)
        {
            if (! compRightRan)
            {
                compRight.reset();
                compRight.snapAmount (amount);
            }
            compRight.process (channels[1], numSamples, amount, span, attack, release, character, threshold, ratio);
        }
        compRightRan = nch > 1;
        compGrDb.store (bassComp.grDb.load (std::memory_order_relaxed), std::memory_order_relaxed);
        innerCut (1);   // after COMP
        nsStage[stComp] = elapsedNs (a);
    }

    // THE BASS'S OCTAVE, then its FILTER (Chain::bass), on the one signal, before the AMP: the sub goes into the amp
    // with the note, and the filter sweeps both.
    if (bass.octave != nullptr && nch > 0)
    {
        const auto a = PerfClock::now();
        runOctave (channels, 1, numSamples);
        innerCut (2);   // after OCTAVE: its sub's own DC and thumps go before FILTER and the AMP hear them
        nsStage[stOctave] = elapsedNs (a);
    }
    if (bass.filter != nullptr && nch > 0)
    {
        const auto a = PerfClock::now();
        runFilter (channels, 1, numSamples);
        nsStage[stFilter] = elapsedNs (a);
    }

    // A captured block, whole: the capture, then its own EQ.
    //
    // The EQ sits AFTER the nonlinearity because that is what it is for here — colouring what the
    // device made, not deciding what the device eats. The preamp's stands after the last
    // nonlinearity, colouring what the amp made before the room and the speaker have it.
    //
    // It also goes dark with the block. The EQ is part of the block now, not a link that happens to
    // be drawn inside one, and a switch that leaves half of what it names still cutting is a switch
    // nobody can trust.
    const auto captured = [&] (auto& blk, int l, params::ChainRow row,
                               std::atomic<float>& outDbOf, int stBlock, int stEq)
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

        outDbOf.store (on ? juce::Decibels::gainToDecibels (
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

    // AMP and TONE, one captured block: the capture wearing the pack's own tone knobs (EqMode::native),
    // then its console — the bass's TONE as much as the guitar's.
    if (probing.load (std::memory_order_relaxed) && nch > 0)
        for (int i = 0; i < numSamples; ++i)
            probeAmpInPeak = juce::jmax (probeAmpInPeak, std::abs (channels[0][i]));

    captured (preamp, 0, params::rowPreamp, blockOutDb[0], stPreamp, stPreampEq);
    innerCut (3);   // after the AMP — after BLEND's sum, never inside one leg: both legs the same phase

    if (probing.load (std::memory_order_relaxed) && nch > 0)
    {
        for (int i = 0; i < numSamples; ++i)
            probeAmpOutSum += (double) channels[0][i];
        probeAmpOutCount += numSamples;
    }

    // MUTE pre-reverb — the G-String architecture, and the default: everything the chain ADDED
    // (the preamp's hiss) dies here too, and the reverb tail past it rings out.
    if (! muteAtStart)
        gate.applyGain (channels, nch, numSamples);

    // THE SEAM: where a mono chain becomes a stereo one, in STEREO SPACE — the one copy, right
    // before the wide stages: the delay spreads the two identical channels first, the reverb
    // rooms what it made.
    for (int ch = nch; ch < nchBack; ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    // THE BASS'S CHORUS (Chain::bass), on the planes the seam just made: two distinct copies on a stereo bus, so each
    // gets its own voice — above the low end only, which stays one, in the centre. Its DELAY and REVERB follow it,
    // before the cabinet, as the guitar's do (runSpaceStage).
    if (bass.chorus != nullptr && nchBack > 0)
    {
        const auto a = PerfClock::now();
        runChorus (channels, nchBack, numSamples);
        nsStage[stChorus] = elapsedNs (a);
    }

    // The echo before the space: repeats of what the preamp made, which the reverb then rooms.
    // First of the wide stages — the OFFSET is the block's stereo, so it works the back half's
    // channels.
    // STANDBY is the insert's bypass: the block keeps running, it just stops being fed, and the
    // repeats already in the line ring out into the dry. Which is also why a bypassed block still
    // costs: so does a bypassed insert, in every DAW there is.
    if (ownProduct.chain == Chain::bass)
    {
        // Its own (runSpaceStage): the wet's lows cut; out of the strip, faded and then cleared and not run.
        const auto a = PerfClock::now();
        runSpaceStage (delay, delayReady, params::rowDelay, 0, delayTap, delayWatched, delayAdds(), channels, nchBack, numSamples);
        if (spaceRunning[0])
            nsStage[stDelay] = elapsedNs (a);
        const auto b = PerfClock::now();
        runSpaceStage (reverb, reverbReady, params::rowReverb, 1, reverbTap, reverbWatched, reverbAdds(), channels, nchBack, numSamples);
        if (spaceRunning[1])
            nsStage[stReverb] = elapsedNs (b);
    }
    else if (delayReady)
    {
        const auto a = PerfClock::now();

        // Prepared for every channel the bus carries, so it cannot refuse this block.
        (void) delay.process (channels, nchBack, numSamples, linkWorks (params::rowDelay));

        nsStage[stDelay] = elapsedNs (a);
    }
    else if (chainHolds (params::rowDelay))
    {
        delay.reset();
    }

    // The room stands by the same way the echo does: unfed, still ringing out. See the delay above.
    if (ownProduct.chain == Chain::bass)
    {
        // run above, with the delay
    }
    else if (reverbReady)
        { const auto a = PerfClock::now();
          const bool fed = linkWorks (params::rowReverb);

          // The pair for the block's picture: the door before the room, the ADDED wet after it.
          // A room nobody is feeding hears silence at its door, and the picture says so.
          { auto& tin = reverbSpectrumTap[0];
            const float* d = channels[0];
            for (int i = 0; i < numSamples; ++i)
                tin.push (fed ? d[i] : 0.0f);
            tin.publishIfDue (eqSpectrumOrder,
                              juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0)); }

          (void) reverb.process (channels, nchBack, numSamples, fed);   // as the delay: it cannot refuse

          { auto& tout = reverbSpectrumTap[1];
            const float* w = reverb.addedWet (0);
            // As far as the room wrote, not as far as the host's block runs: a host that hands more
            // than it promised at prepare would have this read past the end of the room's buffer.
            for (int i = 0; i < reverb.addedLength(); ++i)
                tout.push (w[i]);
            tout.publishIfDue (eqSpectrumOrder,
                               juce::roundToInt (juce::jmax (8000.0, getSampleRate()) / 30.0)); }

          nsStage[stReverb] = elapsedNs (a); }
    else if (chainHolds (params::rowReverb))
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

      // THE CAB'S MIX: the DI is the signal the cabinet is handed, kept aside to be mixed with what the
      // cabinet makes of it — the cabinet's IR plays at sample zero, so the two line up as they are. With the
      // cabinet out there is nothing to mix against, and the share does nothing.
      //
      // The share GLIDES, owned by the audio thread (20 ms for the whole travel), so a hand, a recall or automation is
      // never a step of the difference between the two; settled, the mix is the plain one. With the cabinet fully
      // out the share lands where it is asked at once — there is nothing to glide against.
      const float diWant = bass.cabMix != nullptr ? juce::jlimit (0.0f, 1.0f, (100.0f - bass.cabMix->load()) * 0.01f) : 0.0f;   // MIX: the cabinet's share
      const bool  diMix  = bass.cabMix != nullptr && cabOn && (diWant > 0.0f || diNow > 0.0f)
                        && numSamples <= diDry.getNumSamples();
      if (bass.cabMix != nullptr && ! cabOn)
          diNow = diWant;
      if (diMix)
          for (int ch = 0; ch < nchBack; ++ch)
              juce::FloatVectorOperations::copy (diDry.getWritePointer (ch), buffer.getReadPointer (ch), numSamples);

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

      if (diMix)
      {
          const bool  gliding = diNow < diWant || diNow > diWant;
          const float step    = diStep;
          float       end     = diNow;
          for (int ch = 0; ch < nchBack; ++ch)
          {
              float*       out = channels[ch];
              const float* di  = diDry.getReadPointer (ch);
              if (gliding)
              {
                  float sh = diNow;
                  for (int i = 0; i < numSamples; ++i)
                  {
                      sh = diWant > sh ? juce::jmin (diWant, sh + step) : juce::jmax (diWant, sh - step);
                      out[i] = (1.0f - sh) * out[i] + sh * di[i];
                  }
                  end = sh;
              }
              else if (diNow >= 1.0f)   // all DI: the signal the cabinet was handed, bit for bit
                  juce::FloatVectorOperations::copy (out, di, numSamples);
              else if (diNow > 0.0f)
              {
                  juce::FloatVectorOperations::multiply (out, 1.0f - diNow, numSamples);
                  juce::FloatVectorOperations::addWithMultiply (out, di, diNow, numSamples);
              }
          }
          diNow = end;
      }

      // Once the fade is over and nothing of the cabinet is in the sum, drop what it remembers.
      // Otherwise the first IR-length after it comes back convolves what was played BEFORE it
      // stood down — a ghost, faded in over fifteen milliseconds, of a phrase from minutes ago.
      if (! cabOn)
          cab.idle (numSamples);

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

    // THE BASS'S CUTS (Chain::bass), a stage of their own after DI/CAB, so the DI is cut as the cabinet is —
    // not the cabinet's own two, baked into its IR, which the bass leaves off: HPF by the strings, LPF by the
    // hand (params::bassHpf, bassLpf). Off, the stage is a wire. Its time is the cabinet's on the breakdown.
    if (bass.hpf != nullptr)
    {
        const auto a = PerfClock::now();
        bassCuts.process (channels, nchBack, numSamples, bassCutSettings());
        nsStage[stCab] += elapsedNs (a);
    }

    // THE BOOST'S LEVEL (params::boostLevel): the lift of a boost that is on, after everything that
    // makes the sound and before the master's hand and the limiter, so the ceiling still holds at +9.
    // Thrown, picked or taken out of the chain, it ramps in a straight line; standing at unity it
    // touches nothing — a boost off, or on at 0 dB, plays exactly what it played before it had a level.
    {
        boostGain.setTargetValue (boostTarget());

        if (boostGain.isSmoothing())
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float g = boostGain.getNextValue();
                for (int ch = 0; ch < numChannels; ++ch)
                    channels[ch][i] *= g;
            }
        }
        else if (const float g = boostGain.getCurrentValue(); ! juce::exactlyEqual (g, 1.0f))
        {
            buffer.applyGain (0, numSamples, g);
        }
    }

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
          // crossfaded off like any other replacing link — against the input as late as the
          // limiter makes it, so the two line up.
          const bool limWorks = linkWorks (params::rowLimit);
          const auto limSpan  = blockFade[(size_t) params::rowLimit].advance (numSamples, limWorks);
          const bool limFade  = limSpan.moving && canFade (numSamples, nchBack);

          if (limiterReady)
          {
              if (const float ceiling = limiterCeilParam->load(); ceiling != limiterCeilingSet)
              {
                  limiter.setParams (core::door::params (ceiling));
                  limiterCeilingSet = ceiling;
              }

              if (limFade)
              {
                  // A switch under way: the limited signal and the input as late, crossfaded.
                  limiterLate.pass (channels, fadeDry.getArrayOfWritePointers(), nchBack, numSamples);
                  (void) limiter.process (channels, nchBack, numSamples);
                  core::BypassFade::blend (channels, fadeDry.getArrayOfReadPointers(),
                                           nchBack, numSamples, limSpan);
              }
              else if (limWorks)
              {
                  // In: the wire keeps up with the input, and the limiter takes it.
                  limiterLate.feed (channels, nchBack, numSamples);
                  (void) limiter.process (channels, nchBack, numSamples);
              }
              else
              {
                  // Out: the limiter stays warm on a copy, and the input goes on, late and untouched.
                  if (canFade (numSamples, nchBack))
                  {
                      for (int ch = 0; ch < nchBack; ++ch)
                          juce::FloatVectorOperations::copy (fadeDry.getWritePointer (ch),
                                                             buffer.getReadPointer (ch), numSamples);
                      (void) limiter.process (fadeDry.getArrayOfWritePointers(), nchBack, numSamples);
                  }
                  limiterLate.pass (channels, channels, nchBack, numSamples);
              }
          }

          nsStage[stLimit] = elapsedNs (a); }
        limiterGrDb.store (limiterReady && linkWorks (params::rowLimit) ? (float) limiter.gainReductionDb() : 0.0f);

        // The mono chain becomes the stereo output HERE — one copy, after everything. (In STEREO
        // SPACE the copy was made at the seam, and there is nothing left to copy.)
        for (int ch = nchBack; ch < numChannels; ++ch)
            buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

        // The tuner's MUTE, last of all and before the meter reads — silence at the jack is
        // silence on the rail. Ramped over the block like the trims, so muting mid-note is a fade
        // rather than a click; a tuner standing by does not mute.
        {
            const float target = linkWorks (params::rowTuner) && tunerMuteParam->load() > 0.5f ? 0.0f : 1.0f;
            buffer.applyGainRamp (0, numSamples, lastMuteGain, target);
            lastMuteGain = target;
        }

        // THE DOOR. Nothing that is not a number leaves the box. Every link heals its own state, but
        // healing is per block and the cabinet drains rather than heals — a NaN that got in rides
        // its impulse out for a second and more — and one of those samples on a host's bus takes
        // the whole mix with it: a sum with a NaN in it is a NaN, and a DAW's master goes silent for
        // every track, not just this one. Here it becomes silence for this plugin, for as long as
        // it lasts. No option: there is no one who wants the other thing.
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* d = buffer.getWritePointer (ch);
            for (int i = 0; i < numSamples; ++i)
                if (! std::isfinite (d[i]))
                    d[i] = 0.0f;
        }

        if (limiterGrDb.load() < -0.01f)
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
    return ownProduct.makeEditor (*this);
}

} // namespace orbitamp

