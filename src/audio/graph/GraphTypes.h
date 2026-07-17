#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace better
{
using GraphNodeUID = juce::uint32;

inline constexpr GraphNodeUID inputNodeUID = 1;
inline constexpr GraphNodeUID outputNodeUID = 2;
inline constexpr GraphNodeUID firstModuleNodeUID = 100;

enum class GraphNodeRole
{
    input,
    output,
    module
};

enum class ModuleControlKind
{
    toggle,
    slider
};

struct ModuleControlDescriptor
{
    ModuleControlKind kind = ModuleControlKind::slider;
    juce::String parameterId;
    juce::String name;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float interval = 0.01f;
    float defaultValue = 0.0f;
    bool defaultBool = true;
    juce::String suffix;
    int decimalPlaces = 2;
};

struct ModuleDescriptor
{
    juce::String moduleId;
    juce::String name;
    juce::String category;
    std::vector<ModuleControlDescriptor> controls;
    bool canLoadNAM = false;
    bool canLoadIR = false;
};

struct GraphNodeDescription
{
    GraphNodeUID uid = 0;
    GraphNodeRole role = GraphNodeRole::module;
    juce::String moduleId;
    juce::String name;
    float x = 0.0f;
    float y = 0.0f;
};

struct GraphConnectionDescription
{
    GraphNodeUID source = 0;
    GraphNodeUID destination = 0;

    bool operator== (const GraphConnectionDescription& other) const noexcept
    {
        return source == other.source && destination == other.destination;
    }
};

inline ModuleControlDescriptor toggleControl (juce::String parameterId,
                                              juce::String name,
                                              bool defaultValue = true)
{
    ModuleControlDescriptor descriptor;
    descriptor.kind = ModuleControlKind::toggle;
    descriptor.parameterId = std::move (parameterId);
    descriptor.name = std::move (name);
    descriptor.defaultBool = defaultValue;
    return descriptor;
}

inline ModuleControlDescriptor unitSlider (juce::String parameterId,
                                           juce::String name,
                                           float defaultValue,
                                           int decimalPlaces = 2)
{
    ModuleControlDescriptor descriptor;
    descriptor.parameterId = std::move (parameterId);
    descriptor.name = std::move (name);
    descriptor.minimum = 0.0f;
    descriptor.maximum = 1.0f;
    descriptor.interval = 0.01f;
    descriptor.defaultValue = defaultValue;
    descriptor.decimalPlaces = decimalPlaces;
    return descriptor;
}

inline ModuleControlDescriptor gainSlider (juce::String parameterId,
                                           juce::String name,
                                           float defaultValue,
                                           float minDb = -24.0f,
                                           float maxDb = 24.0f)
{
    ModuleControlDescriptor descriptor;
    descriptor.parameterId = std::move (parameterId);
    descriptor.name = std::move (name);
    descriptor.minimum = minDb;
    descriptor.maximum = maxDb;
    descriptor.interval = 0.1f;
    descriptor.defaultValue = defaultValue;
    descriptor.suffix = " dB";
    descriptor.decimalPlaces = 1;
    return descriptor;
}

inline float sanitiseAudioSample (float sample) noexcept
{
    return std::isfinite (sample) ? sample : 0.0f;
}
} // namespace better
