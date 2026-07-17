#include "audio/modules/ModuleProcessor.h"

namespace better
{
AudioModuleProcessor::AudioModuleProcessor (ModuleDescriptor descriptorIn)
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      descriptor (std::move (descriptorIn)),
      parameters (*this, nullptr, "Parameters", createParameterLayout (descriptor.controls))
{
}

const juce::String AudioModuleProcessor::getName() const
{
    return descriptor.name;
}

void AudioModuleProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);
    onPrepared (currentSampleRate, currentBlockSize);
    onReset();
}

void AudioModuleProcessor::releaseResources()
{
}

void AudioModuleProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    midiMessages.clear();

    if (! isModuleEnabled() || buffer.getNumSamples() <= 0)
        return;

    processAudio (buffer, buffer.getNumSamples());

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            samples[sample] = sanitiseAudioSample (samples[sample]);
    }
}

void AudioModuleProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String AudioModuleProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void AudioModuleProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

void AudioModuleProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createModuleState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AudioModuleProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary (data, sizeInBytes))
        restoreModuleState (juce::ValueTree::fromXml (*xml));
}

bool AudioModuleProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    return input == output && (input == juce::AudioChannelSet::mono()
                            || input == juce::AudioChannelSet::stereo());
}

juce::ValueTree AudioModuleProcessor::createModuleState()
{
    juce::ValueTree state ("ModuleState");
    state.setProperty ("moduleId", descriptor.moduleId, nullptr);

    auto parameterState = parameters.copyState();
    state.addChild (parameterState, -1, nullptr);
    writeExtraState (state);

    return state;
}

void AudioModuleProcessor::restoreModuleState (const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    if (const auto parameterState = state.getChildWithName ("Parameters"); parameterState.isValid())
        parameters.replaceState (parameterState);

    restoreExtraState (state);
}

juce::String AudioModuleProcessor::getStatusText() const
{
    return {};
}

void AudioModuleProcessor::loadNAMFileAsync (const juce::File& file)
{
    juce::ignoreUnused (file);
}

void AudioModuleProcessor::loadIRFileAsync (const juce::File& file)
{
    juce::ignoreUnused (file);
}

void AudioModuleProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void AudioModuleProcessor::writeExtraState (juce::ValueTree& state)
{
    juce::ignoreUnused (state);
}

void AudioModuleProcessor::restoreExtraState (const juce::ValueTree& state)
{
    juce::ignoreUnused (state);
}

bool AudioModuleProcessor::isModuleEnabled() const
{
    if (auto* enabled = parameters.getRawParameterValue ("enabled"))
        return enabled->load() > 0.5f;

    return true;
}

float AudioModuleProcessor::getFloatParameter (const juce::String& parameterId, float fallback) const
{
    if (auto* value = parameters.getRawParameterValue (parameterId))
        return value->load();

    return fallback;
}

juce::AudioProcessorValueTreeState::ParameterLayout AudioModuleProcessor::createParameterLayout (const std::vector<ModuleControlDescriptor>& controls)
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& control : controls)
    {
        if (control.parameterId.isEmpty())
            continue;

        if (control.kind == ModuleControlKind::toggle)
        {
            layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { control.parameterId, 1 },
                                                                    control.name,
                                                                    control.defaultBool));
            continue;
        }

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { control.parameterId, 1 },
            control.name,
            juce::NormalisableRange<float> (control.minimum, control.maximum, control.interval),
            control.defaultValue,
            juce::AudioParameterFloatAttributes().withLabel (control.suffix)));
    }

    return layout;
}
} // namespace better
