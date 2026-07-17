#include "audio/modules/RouterProcessor.h"

namespace better
{
RouterProcessor::RouterProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    // Parameter-Pointer cachen; processAudio läuft im Audio-Thread.
    monoParam = getParameters().getRawParameterValue ("mono");
}

ModuleDescriptor RouterProcessor::createDescriptor()
{
    return
    {
        "router_basic",
        "Input Router",
        "FX",
        {
            toggleControl ("enabled", "Enabled", true),
            toggleControl ("mono", "Mono (L -> LR)", true)
        },
        false,
        false
    };
}

void RouterProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    // Mono-Modus spiegelt den linken Kanal auf rechts.
    if (monoParam != nullptr && monoParam->load() > 0.5f)
    {
        if (buffer.getNumChannels() > 1)
            buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);
    }
}

} // namespace better
