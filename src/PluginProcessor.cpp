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
    updateToneSettings();
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

    // boost -> preamp -> EQ -> reverb. Only the EQ is real so far; the captured stages need
    // profiles that do not exist yet, and the reverb is still to be written.
    if (eqOnParam->load() > 0.5f)
        tone.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
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
