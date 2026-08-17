#pragma once

#include "audio/modules/ModuleProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <utility>

namespace better
{
class GraphAudioProcessor;

class ModuleProcessorHandle final
{
public:
    ModuleProcessorHandle() = default;

    AudioModuleProcessor* get() const noexcept
    {
        return node != nullptr
             ? dynamic_cast<AudioModuleProcessor*> (node->getProcessor())
             : nullptr;
    }

    AudioModuleProcessor* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return get() != nullptr; }

    void reset() { node = nullptr; }

private:
    using NodePtr = juce::AudioProcessorGraph::Node::Ptr;

    explicit ModuleProcessorHandle (NodePtr nodeIn)
        : node (std::move (nodeIn))
    {
        if (get() == nullptr)
            node = nullptr;
    }

    NodePtr node;

    friend class GraphAudioProcessor;
};
} // namespace better
