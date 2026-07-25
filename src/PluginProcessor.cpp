#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"

namespace orbitamp
{

AmpProcessor::AmpProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "state", params::createLayout())
{
    eqOnParam    = apvts.getRawParameterValue (params::eqOn);
    eqLowParam   = apvts.getRawParameterValue (params::eqLow);
    eqMidParam   = apvts.getRawParameterValue (params::eqMid);
    eqHighParam  = apvts.getRawParameterValue (params::eqHigh);
    eqHpfOnParam = apvts.getRawParameterValue (params::eqHpfOn);
    eqHpfHzParam = apvts.getRawParameterValue (params::eqHpfHz);
    eqLpfOnParam = apvts.getRawParameterValue (params::eqLpfOn);
    eqLpfHzParam = apvts.getRawParameterValue (params::eqLpfHz);

    reverbOnParam   = apvts.getRawParameterValue (params::reverbOn);
    reverbTypeParam = apvts.getRawParameterValue (params::reverbType);
    reverbMixParam  = apvts.getRawParameterValue (params::reverbMix);
}

void AmpProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AmpProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

void AmpProcessor::prepareToPlay (double sampleRate, int)
{
    tone.prepare (sampleRate, juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels()));
    reverb.prepare (sampleRate);

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
    s.highDb = (double) eqHighParam->load();
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

    // Clear any output channel the host gave us beyond what the input carries.
    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateToneSettings();
    updateReverbSettings();

    auto* const* channels = buffer.getArrayOfWritePointers();
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();

    // boost -> preamp -> EQ -> reverb. The captured stages are still silent passthrough: they need
    // profiles that do not exist yet.
    if (eqOnParam->load() > 0.5f)
        tone.process (channels, numChannels, numSamples);

    if (reverbOnParam->load() > 0.5f)
        reverb.process (channels, numChannels, numSamples);
    else
        reverb.reset();   // so re-enabling it does not spill the tail of what was playing before
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
