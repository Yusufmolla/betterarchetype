#pragma once

#include "audio/graph/GraphTypes.h"

#include <memory>

namespace better
{
class AudioModuleProcessor;

class ModuleRegistry
{
public:
    static const std::vector<ModuleDescriptor>& getModules();
    static const ModuleDescriptor* findModule (const juce::String& moduleId);
    static std::unique_ptr<AudioModuleProcessor> createProcessor (const juce::String& moduleId);
};
} // namespace better
