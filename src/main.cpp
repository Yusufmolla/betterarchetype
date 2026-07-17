#include "core/GraphAudioProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new better::GraphAudioProcessor();
}
