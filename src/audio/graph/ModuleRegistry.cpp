#include "audio/graph/ModuleRegistry.h"

#include "audio/modules/DriveProcessor.h"
#include "audio/modules/IRCabProcessor.h"
#include "audio/modules/NAMProcessor.h"
#include "audio/modules/AmpProcessor.h"
#include "audio/modules/RouterProcessor.h"

namespace better
{
const std::vector<ModuleDescriptor>& ModuleRegistry::getModules()
{
    static const std::vector<ModuleDescriptor> modules
    {
        DriveProcessor::createDescriptor(),
        NAMProcessor::createDescriptor(),
        IRCabProcessor::createDescriptor(),
        AmpProcessor::createDescriptor(),
        RouterProcessor::createDescriptor()
    };

    return modules;
}

const ModuleDescriptor* ModuleRegistry::findModule (const juce::String& moduleId)
{
    for (const auto& module : getModules())
        if (module.moduleId == moduleId)
            return &module;

    return nullptr;
}

std::unique_ptr<AudioModuleProcessor> ModuleRegistry::createProcessor (const juce::String& moduleId)
{
    if (moduleId == "drive") return std::make_unique<DriveProcessor>();
    if (moduleId == "nam")   return std::make_unique<NAMProcessor>();
    if (moduleId == "ir")    return std::make_unique<IRCabProcessor>();
    if (moduleId == "amp_basic") return std::make_unique<AmpProcessor>();
    if (moduleId == "router_basic") return std::make_unique<RouterProcessor>();
    
    return {};
}
} // namespace better
