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

    eqOnParam    = apvts.getRawParameterValue (params::eqOn);
    eqLowParam   = apvts.getRawParameterValue (params::eqLow);
    eqMidParam   = apvts.getRawParameterValue (params::eqMid);
    eqHighParam  = apvts.getRawParameterValue (params::eqHigh);
    eqPresParam  = apvts.getRawParameterValue (params::eqPresence);
    eqMidHzParam = apvts.getRawParameterValue (params::eqMidHz);
    eqHpfOnParam = apvts.getRawParameterValue (params::eqHpfOn);
    eqHpfHzParam = apvts.getRawParameterValue (params::eqHpfHz);
    eqLpfOnParam = apvts.getRawParameterValue (params::eqLpfOn);
    eqLpfHzParam = apvts.getRawParameterValue (params::eqLpfHz);

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

    if (history.fromTree (tree))
        return;

    // Sessions saved before the workspace existed hold a bare parameter tree. Load the sound and
    // start a fresh history around it rather than dropping the session on the floor.
    if (tree.hasType (apvts.state.getType()))
    {
        apvts.replaceState (tree);
        history.reset();
    }
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
    devicePacks = device::DeviceLibrary::scan();
    selectBoostDevice (juce::roundToInt (apvts.getRawParameterValue (params::boostDevice)->load()));
}

void AmpProcessor::selectBoostDevice (int index)
{
    // A saved session names a device by position in a list that is whatever is on disk today. If it
    // is gone, the first one stands in rather than nothing loading — silence is a worse answer than
    // the wrong pedal, and the name in the combo says which it is.
    boost.setPack (juce::isPositiveAndBelow (index, devicePacks.size())
                     ? &devicePacks.getReference (index)
                     : (devicePacks.isEmpty() ? nullptr : &devicePacks.getReference (0)));

    lastBoostGainIndex = -1;
    lastBoostTone.fill (-1.0f);
}

void AmpProcessor::loadBoostModelIfChanged()
{
    const auto positions = boost.gainPositions();
    if (positions.isEmpty())
        return;

    // The knob reads 0..10; the captures sit at whatever angles the device was taken at, evenly
    // spaced across that travel. Nearest position wins — between two captures there is nothing.
    const float t = juce::jlimit (0.0f, 1.0f, boostGainParam->load() * 0.1f);
    const int index = juce::jlimit (0, positions.size() - 1,
                                    juce::roundToInt (t * (float) (positions.size() - 1)));

    if (index == lastBoostGainIndex)
        return;

    lastBoostGainIndex = index;
    boost.selectGainIndex (index);
}

void AmpProcessor::updateBoostToneIfChanged()
{
    const auto* measured = boost.measured();

    for (int i = 0; i < params::boostNumMeasured; ++i)
    {
        auto& filter = boostTone[(size_t) i];

        if (measured == nullptr || i >= (int) measured->size())
        {
            filter.clear();
            continue;
        }

        const float v = apvts.getRawParameterValue (params::boostMeasured (i))->load();
        if (juce::approximatelyEqual (v, lastBoostTone[(size_t) i]))
            continue;

        lastBoostTone[(size_t) i] = v;
        filter.setPosition ((*measured)[(size_t) i], (double) v);
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

void AmpProcessor::prepareToPlay (double sampleRate, int)
{
    const int channels = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());

    tone.prepare (sampleRate, channels);
    reverb.prepare (sampleRate);
    boost.prepare (sampleRate, getBlockSize());
    demo.prepare (sampleRate);
    scopeDry.setSize (1, juce::jmax (1, getBlockSize()));

    for (auto& f : boostTone)
        f.prepare (sampleRate, getBlockSize(), channels);

    lastBoostTone.fill (-1.0f);
    updateBoostToneIfChanged();
    lastBoostGainIndex = -1;
    loadBoostModelIfChanged();

    // Choose the factor BEFORE preparing, so prepare() builds with it and nothing has to re-prepare
    // from inside a prepare.
    lastOversample = juce::jlimit (0, params::oversampleFactors.size() - 1,
                                   juce::roundToInt (oversampleParam->load()));
    power.setOversampling (params::oversampleValues[lastOversample]);
    power.prepare (sampleRate, getBlockSize(), channels);

    // Reported ALWAYS, whether the power amp is switched on or not: its bypass path carries the same
    // delay, so a toggle never shifts the timing of everything downstream. A latency that changes
    // with a switch is what makes hosts re-align mid-song.
    setLatencySamples (power.latencySamples());

    updateToneSettings();
    updateReverbSettings();
}

void AmpProcessor::updateReverbSettings() noexcept
{
    const int index = juce::jlimit (0, params::reverbCharacters.size() - 1,
                                    juce::roundToInt (reverbTypeParam->load()));

    reverb.setCharacter (static_cast<core::ReverbStage::Character> (index));
    reverb.setMix (reverbMixParam->load() * 0.01f);   // the face reads percent
}

void AmpProcessor::updateToneSettings() noexcept
{
    core::ToneStack::Settings s;
    s.lowDb  = (double) eqLowParam->load();
    s.midDb  = (double) eqMidParam->load();
    s.midHz  = (double) eqMidHzParam->load();
    s.highDb     = (double) eqHighParam->load();
    s.presenceDb = (double) eqPresParam->load();
    s.hpfOn  = eqHpfOnParam->load() > 0.5f;
    s.hpfHz  = (double) eqHpfHzParam->load();
    s.lpfOn  = eqLpfOnParam->load() > 0.5f;
    s.lpfHz  = (double) eqLpfHzParam->load();

    tone.setSettings (s);
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

    updateToneSettings();
    updateReverbSettings();

    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // boost -> preamp -> EQ -> reverb -> power amp.
    if (boostOnParam->load() > 0.5f)
    {
        // Keep the input so the block can draw what it did to it, not just what came out.
        scopeDry.setSize (1, numSamples, false, false, true);
        scopeDry.copyFrom (0, 0, buffer, 0, 0, numSamples);

        boost.process (channels, numChannels, numSamples);

        // The measured controls sit AFTER the capture — `placement: post` — because that is where
        // they sit in the pedal.
        for (auto& f : boostTone)
            f.process (buffer);

        boostScope.write (scopeDry.getReadPointer (0), buffer.getReadPointer (0), numSamples);
    }

    if (eqOnParam->load() > 0.5f)
        tone.process (channels, numChannels, numSamples);

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
