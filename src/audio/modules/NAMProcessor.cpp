#include "audio/modules/NAMProcessor.h"
#include "audio/graph/AssetPaths.h"

namespace better
{
NAMProcessor::NAMProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    // Parameter-Pointer cachen; processAudio läuft im Audio-Thread.
    inputParam = getParameters().getRawParameterValue ("input");
    outputParam = getParameters().getRawParameterValue ("output");

    requestDefaultModelLoad();
}

NAMProcessor::~NAMProcessor()
{
    loaderPool.removeAllJobs (true, 4000);
}

ModuleDescriptor NAMProcessor::createDescriptor()
{
    return
    {
        "nam",
        "NAM Amp",
        "AMP",
        {
            toggleControl ("enabled", "Enabled", true),
            gainSlider ("input", "Input", 0.0f, -18.0f, 18.0f),
            gainSlider ("output", "Output", 0.0f, -24.0f, 12.0f)
        },
        true,
        false
    };
}

juce::String NAMProcessor::getStatusText() const
{
    const juce::ScopedLock lock (statusLock);
    return statusText;
}

void NAMProcessor::loadNAMFileAsync (const juce::File& file)
{
    const auto revision = ++loadRevision;
    modelLoaded.store (false, std::memory_order_release);

    if (! file.existsAsFile())
    {
        clearLoadedModel ("No NAM model loaded");
        return;
    }

    setStatus ("Loading " + file.getFileNameWithoutExtension() + "...");

    loaderPool.addJob ([this, file, revision]
    {
        namLoader.prepare (currentSampleRate, currentBlockSize);
        const auto loaded = namLoader.loadNAMFile (file);

        if (revision != loadRevision.load())
            return;

        if (loaded)
        {
            currentModelFile = file;
            modelLoaded.store (true, std::memory_order_release);
            setStatus ("Loaded " + file.getFileNameWithoutExtension());
            return;
        }

        currentModelFile = juce::File();
        modelLoaded.store (false, std::memory_order_release);
        setStatus ("Bypass: " + namLoader.getLastError());
    });
}

void NAMProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    namLoader.prepare (sampleRate, samplesPerBlock);
    monoInputBuffer.setSize (1, samplesPerBlock);
    monoOutputBuffer.setSize (1, samplesPerBlock);
}

void NAMProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! modelLoaded.load (std::memory_order_acquire) || ! namLoader.isModelLoaded())
        return;

    monoInputBuffer.setSize (1, numSamples, false, false, true);
    monoOutputBuffer.setSize (1, numSamples, false, false, true);

    float inDb = inputParam != nullptr ? inputParam->load() : 0.0f;
    float outDb = outputParam != nullptr ? outputParam->load() : 0.0f;

    const auto inputGain = juce::Decibels::decibelsToGain (inDb);
    const auto outputGain = juce::Decibels::decibelsToGain (outDb);

    auto* input = monoInputBuffer.getWritePointer (0);

    // NAMCore wird hier mono gefahren; Stereo wird davor zusammengefasst.
    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto mono = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            mono += sanitiseAudioSample (buffer.getSample (channel, sample));

        mono /= static_cast<float> (juce::jmax (1, buffer.getNumChannels()));
        input[sample] = juce::jlimit (-1.5f, 1.5f, mono * inputGain);
    }

    // Bei einem NAM-Fehler bleibt der Originalbuffer unverändert.
    if (! namLoader.processMonoBlock (monoInputBuffer.getReadPointer (0),
                                      monoOutputBuffer.getWritePointer (0),
                                      numSamples))
    {
        return;
    }

    // Das mono berechnete NAM-Signal geht wieder auf alle Output-Kanäle.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            samples[sample] = juce::jlimit (-2.0f, 2.0f, monoOutputBuffer.getSample (0, sample) * outputGain);
    }
}

void NAMProcessor::writeExtraState (juce::ValueTree& state)
{
    state.setProperty ("modelPath", currentModelFile.getFullPathName(), nullptr);
}

void NAMProcessor::restoreExtraState (const juce::ValueTree& state)
{
    const auto modelPath = state.getProperty ("modelPath").toString();

    if (modelPath.isNotEmpty())
    {
        loadNAMFileAsync (juce::File (modelPath));
        return;
    }

    requestDefaultModelLoad();
}

void NAMProcessor::requestDefaultModelLoad()
{
    const auto exampleModel = AssetPaths::getAssetFile ("example_amp.nam");

    if (exampleModel.existsAsFile())
    {
        loadNAMFileAsync (exampleModel);
        return;
    }

    clearLoadedModel ("Bypass: assets/example_amp.nam not found");
}

void NAMProcessor::setStatus (juce::String newStatus)
{
    const juce::ScopedLock lock (statusLock);
    statusText = std::move (newStatus);
}

void NAMProcessor::clearLoadedModel (juce::String reason)
{
    ++loadRevision;
    currentModelFile = juce::File();
    modelLoaded.store (false, std::memory_order_release);
    namLoader.clear();
    setStatus (std::move (reason));
}
} // namespace better
