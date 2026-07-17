#pragma once

#include <juce_core/juce_core.h>

namespace better::AssetPaths
{
inline juce::File findAssetsDirectoryFrom (juce::File startDirectory)
{
    if (startDirectory.existsAsFile())
        startDirectory = startDirectory.getParentDirectory();

    for (int i = 0; i < 14 && startDirectory.isDirectory(); ++i)
    {
        const auto candidate = startDirectory.getChildFile ("assets");

        if (candidate.isDirectory())
            return candidate;

        const auto parent = startDirectory.getParentDirectory();

        if (parent == startDirectory)
            break;

        startDirectory = parent;
    }

    return {};
}

inline juce::File getAssetsDirectory()
{
#if defined (BETTER_ARCHETYPE_ASSETS_DIR)
    const auto configuredAssets = juce::File (BETTER_ARCHETYPE_ASSETS_DIR);

    if (configuredAssets.isDirectory())
        return configuredAssets;
#endif

    if (auto fromWorkingDirectory = findAssetsDirectoryFrom (juce::File::getCurrentWorkingDirectory());
        fromWorkingDirectory.isDirectory())
    {
        return fromWorkingDirectory;
    }

    return findAssetsDirectoryFrom (juce::File::getSpecialLocation (juce::File::currentExecutableFile));
}

inline juce::File getAssetFile (const juce::String& fileName)
{
    const auto assetsDirectory = getAssetsDirectory();
    return assetsDirectory.isDirectory() ? assetsDirectory.getChildFile (fileName) : juce::File {};
}
} // namespace better::AssetPaths
