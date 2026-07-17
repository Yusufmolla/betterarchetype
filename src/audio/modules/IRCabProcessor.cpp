#include "audio/modules/IRCabProcessor.h"

namespace better
{
IRCabProcessor::IRCabProcessor()
    : AudioModuleProcessor (createDescriptor())
{
}

IRCabProcessor::~IRCabProcessor()
{
    loaderPool.removeAllJobs (true, 4000);
}

ModuleDescriptor IRCabProcessor::createDescriptor()
{
    return
    {
        "ir",
        "IR / Cab",
        "CAB",
        {
            toggleControl ("enabled", "Enabled", true),
            gainSlider ("level", "Level", 0.0f, -24.0f, 12.0f)
        },
        false,
        true
    };
}

juce::String IRCabProcessor::getStatusText() const
{
    const juce::ScopedLock lock (statusLock);
    return statusText;
}

void IRCabProcessor::loadIRFileAsync (const juce::File& file)
{
    const auto revision = ++loadRevision;
    irLoaded.store (false, std::memory_order_release);

    if (! file.existsAsFile())
    {
        clearLoadedIR ("No IR loaded");
        return;
    }

    setStatus ("Loading " + file.getFileNameWithoutExtension() + "...");

    loaderPool.addJob ([this, file, revision]
    {
        auto ir = IRLoader::loadIRFile (file);

        if (revision != loadRevision.load())
            return;

        if (ir != nullptr && ! ir->samples.empty())
        {
            convolver.setImpulseResponse (ir->samples, ir->sampleRate);
            currentIRFile = file;
            irLoaded.store (true, std::memory_order_release);
            setStatus ("Loaded " + file.getFileNameWithoutExtension());
            return;
        }

        currentIRFile = juce::File();
        convolver.clear();
        irLoaded.store (false, std::memory_order_release);
        setStatus ("Bypass: IR could not be loaded");
    });
}

void IRCabProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    convolver.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()), samplesPerBlock);
}

void IRCabProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! irLoaded.load (std::memory_order_acquire) || ! convolver.hasImpulseResponse())
        return;

    convolver.process (buffer, numSamples);

    const auto levelGain = juce::Decibels::decibelsToGain (getFloatParameter ("level", 0.0f));

    if (std::abs (levelGain - 1.0f) < 0.0001f)
        return;

    buffer.applyGain (0, numSamples, levelGain);
}

void IRCabProcessor::writeExtraState (juce::ValueTree& state)
{
    state.setProperty ("irPath", currentIRFile.getFullPathName(), nullptr);
}

void IRCabProcessor::restoreExtraState (const juce::ValueTree& state)
{
    const auto irPath = state.getProperty ("irPath").toString();

    if (irPath.isNotEmpty())
    {
        loadIRFileAsync (juce::File (irPath));
        return;
    }

    clearLoadedIR ("No IR loaded");
}

void IRCabProcessor::setStatus (juce::String newStatus)
{
    const juce::ScopedLock lock (statusLock);
    statusText = std::move (newStatus);
}

void IRCabProcessor::clearLoadedIR (juce::String reason)
{
    ++loadRevision;
    currentIRFile = juce::File();
    convolver.clear();
    irLoaded.store (false, std::memory_order_release);
    setStatus (std::move (reason));
}
} // namespace better
