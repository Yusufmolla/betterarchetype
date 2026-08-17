#pragma once

#include <juce_core/juce_core.h>

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "NAM/activations.h"
#include "NAM/container.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/linear.h"
#include "NAM/lstm.h"
#include "NAM/wavenet/model.h"

namespace better
{
class NAMModelLoader
{
public:
    NAMModelLoader() = default;
    ~NAMModelLoader() = default;

    bool prepare (double newSampleRate, int newMaxBlockSize)
    {
        if (! isSupportedSampleRate (newSampleRate))
        {
            const std::lock_guard<std::mutex> lock (modelMutex);
            sampleRate = newSampleRate;
            maxBlockSize = juce::jmax (1, newMaxBlockSize);
            lastError = unsupportedHostSampleRateError;
            clearModelUnlocked();
            return false;
        }

        try
        {
            const std::lock_guard<std::mutex> lock (modelMutex);
            sampleRate = newSampleRate;
            maxBlockSize = juce::jmax (1, newMaxBlockSize);
            reserveProcessBuffers();

            if (dspModel)
                dspModel->Reset (sampleRate, maxBlockSize);

            return true;
        }
        catch (const std::exception& e)
        {
            return failAndClear ("NAM prepare failed: " + juce::String (e.what()));
        }
        catch (...)
        {
            return failAndClear ("NAM prepare failed with an unknown error.");
        }
    }

    bool loadNAMFile (const juce::File& namFile)
    {
        {
            const std::lock_guard<std::mutex> lock (modelMutex);
            lastError.clear();
        }

        if (! namFile.existsAsFile())
            return failAndClear ("Datei existiert nicht.");

        const auto fileSize = namFile.getSize();

        if (fileSize <= 0 || fileSize > maximumNAMFileSizeBytes)
            return failAndClear ("NAM-Datei ist leer oder größer als 256 MiB.");

        {
            const std::lock_guard<std::mutex> lock (modelMutex);

            if (! isSupportedSampleRate (sampleRate))
            {
                lastError = unsupportedHostSampleRateError;
                clearModelUnlocked();
                return false;
            }
        }

        try
        {
            forceNAMCoreParserRegistration();

            auto modelJson = nlohmann::json::parse (namFile.loadFileAsString().toStdString());
            normalizeArchitectureNames (modelJson);
            validateModelDocument (modelJson);

            nam::DspLoadOptions options;
            options.prewarm = false;

            auto loadedModel = nam::get_dsp (modelJson, options);

            if (! loadedModel)
                return failAndClear ("NAMCore gab kein DSP-Objekt zurück.");

            const auto expectedSampleRate = loadedModel->GetExpectedSampleRate();

            if (! std::isfinite (expectedSampleRate)
                || (expectedSampleRate > 0.0 && ! isSupportedSampleRate (expectedSampleRate)))
            {
                return failAndClear ("Das NAM-Modell wurde nicht für 48 kHz erstellt.");
            }

            if (loadedModel->NumInputChannels() <= 0
                || loadedModel->NumInputChannels() > maximumModelChannels
                || loadedModel->NumOutputChannels() <= 0
                || loadedModel->NumOutputChannels() > maximumModelChannels)
            {
                return failAndClear ("Das NAM-Modell hat eine ungültige Kanalanzahl.");
            }

            const auto loadedModelOutputTrim = calculateModelOutputTrim (*loadedModel);

            {
                const std::lock_guard<std::mutex> lock (modelMutex);

                if (! isSupportedSampleRate (sampleRate))
                {
                    lastError = unsupportedHostSampleRateError;
                    clearModelUnlocked();
                    return false;
                }

                loadedModel->Reset (sampleRate, maxBlockSize);
                dspModel = std::move (loadedModel);
                modelOutputTrim = loadedModelOutputTrim;
                modelInputChannels = juce::jmax (1, dspModel->NumInputChannels());
                modelOutputChannels = juce::jmax (1, dspModel->NumOutputChannels());
                reserveProcessBuffers();
            }

            return true;
        }
        catch (const std::exception& e)
        {
            return failAndClear (e.what());
        }
        catch (...)
        {
            return failAndClear ("Unbekannter Fehler beim Laden des NAM-Modells.");
        }
    }

    void clear()
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        clearModelUnlocked();
    }

    bool processMonoBlock (const float* input,
                           float* output,
                           int numSamples,
                           float inputGain = 1.0f)
    {
        const float* inputChannels[] { input };
        return processInputBlock (inputChannels, 1, output, numSamples, inputGain);
    }

    bool processInputBlock (const float* const* inputChannels,
                            int numInputChannels,
                            float* output,
                            int numSamples,
                            float inputGain = 1.0f)
    {
        if (inputChannels == nullptr || numInputChannels <= 0
            || output == nullptr || numSamples <= 0)
            return false;

        for (int channel = 0; channel < numInputChannels; ++channel)
        {
            if (inputChannels[channel] == nullptr)
                return false;
        }

        std::unique_lock<std::mutex> lock (modelMutex, std::try_to_lock);

        if (! lock.owns_lock() || ! dspModel)
        {
            if (inputChannels[0] != output)
                std::copy_n (inputChannels[0], numSamples, output);

            return false;
        }

        try
        {
            const auto modelInputChannelCount = juce::jmax (1, modelInputChannels);
            const auto modelOutputChannelCount = juce::jmax (1, modelOutputChannels);
            const auto chunkCapacity = juce::jmax (1, maxBlockSize);

            for (int processedSamples = 0; processedSamples < numSamples;)
            {
                const auto samplesThisTime = juce::jmin (chunkCapacity, numSamples - processedSamples);

                for (int channel = 0; channel < modelInputChannelCount; ++channel)
                    inputChannelPointers[(size_t) channel] = modelInputBuffers[(size_t) channel].data();

                for (int sample = 0; sample < samplesThisTime; ++sample)
                {
                    auto monoSample = 0.0f;

                    for (int channel = 0; channel < numInputChannels; ++channel)
                        monoSample += inputChannels[channel][processedSamples + sample];

                    const auto gainedSample = (monoSample / static_cast<float> (numInputChannels))
                                                  * inputGain;
                    const auto modelSample = static_cast<NAM_SAMPLE> (
                        std::isfinite (gainedSample) ? gainedSample : 0.0f);

                    for (int channel = 0; channel < modelInputChannelCount; ++channel)
                        modelInputBuffers[(size_t) channel][(size_t) sample] = modelSample;
                }

                for (int channel = 0; channel < modelOutputChannelCount; ++channel)
                {
                    auto& channelBuffer = modelOutputBuffers[(size_t) channel];
                    std::fill (channelBuffer.begin(),
                               channelBuffer.begin() + samplesThisTime,
                               static_cast<NAM_SAMPLE> (0));
                    outputChannelPointers[(size_t) channel] = channelBuffer.data();
                }

                dspModel->process (inputChannelPointers.data(),
                                   outputChannelPointers.data(),
                                   samplesThisTime);

                for (int sampleIndex = 0; sampleIndex < samplesThisTime; ++sampleIndex)
                {
                    auto sample = 0.0f;

                    for (int channel = 0; channel < modelOutputChannelCount; ++channel)
                    {
                        sample += static_cast<float> (
                            modelOutputBuffers[(size_t) channel][(size_t) sampleIndex]);
                    }

                    sample = (sample / static_cast<float> (modelOutputChannelCount)) * modelOutputTrim;

                    if (! std::isfinite (sample))
                        sample = 0.0f;

                    output[processedSamples + sampleIndex] = sample;
                }

                processedSamples += samplesThisTime;
            }

            return true;
        }
        catch (...)
        {
            if (inputChannels[0] != output)
                std::copy_n (inputChannels[0], numSamples, output);

            return false;
        }
    }

    [[nodiscard]] juce::String getLastError() const
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        return lastError;
    }

private:
    bool failAndClear (juce::String error)
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        lastError = std::move (error);
        clearModelUnlocked();
        return false;
    }

    void clearModelUnlocked()
    {
        dspModel.reset();
        modelOutputTrim = 1.0f;
        modelInputChannels = 1;
        modelOutputChannels = 1;
    }

    void reserveProcessBuffers()
    {
        const auto inputChannels = juce::jmax (1, modelInputChannels);
        const auto outputChannels = juce::jmax (1, modelOutputChannels);
        const auto samples = juce::jmax (1, maxBlockSize);

        modelInputBuffers.resize ((size_t) inputChannels);
        modelOutputBuffers.resize ((size_t) outputChannels);
        inputChannelPointers.resize ((size_t) inputChannels);
        outputChannelPointers.resize ((size_t) outputChannels);

        for (auto& channel : modelInputBuffers)
            channel.resize ((size_t) samples);

        for (auto& channel : modelOutputBuffers)
            channel.resize ((size_t) samples);
    }

    static std::string canonicaliseArchitectureName (std::string name)
    {
        auto isSpaceLike = [] (unsigned char c) { return std::isspace (c) || c == '_' || c == '-'; };
        name.erase (std::remove_if (name.begin(), name.end(), isSpaceLike), name.end());

        std::string lowered;
        lowered.reserve (name.size());

        for (unsigned char c : name)
            lowered.push_back ((char) std::tolower (c));

        if (lowered == "slimmablecontainer") return "SlimmableContainer";
        if (lowered == "wavenet") return "WaveNet";
        if (lowered == "lstm") return "LSTM";
        if (lowered == "convnet") return "ConvNet";
        if (lowered == "linear") return "Linear";

        return name;
    }

    static void normalizeArchitectureNames (nlohmann::json& node)
    {
        if (node.is_object())
        {
            auto architectureIt = node.find ("architecture");

            if (architectureIt != node.end() && architectureIt->is_string())
                *architectureIt = canonicaliseArchitectureName (architectureIt->get<std::string>());

            for (auto& [key, value] : node.items())
            {
                juce::ignoreUnused (key);
                normalizeArchitectureNames (value);
            }
        }
        else if (node.is_array())
        {
            for (auto& element : node)
                normalizeArchitectureNames (element);
        }
    }

    static void validateModelDocument (const nlohmann::json& model)
    {
        if (! model.is_object() || ! model.contains ("architecture"))
            throw std::runtime_error ("NAM document has no model architecture");

        validateModelNode (model);
    }

    static void validateModelNode (const nlohmann::json& node)
    {
        if (node.is_array())
        {
            for (const auto& element : node)
                validateModelNode (element);

            return;
        }

        if (! node.is_object())
            return;

        if (const auto architecture = node.find ("architecture"); architecture != node.end())
        {
            if (! architecture->is_string())
                throw std::runtime_error ("NAM architecture must be a string");

            const auto name = architecture->get<std::string>();

            if (name == "ConvNet")
                throw std::runtime_error ("ConvNet models are not realtime-safe and are not supported");

            if (name != "SlimmableContainer" && name != "WaveNet"
                && name != "LSTM" && name != "Linear")
            {
                throw std::runtime_error ("Unsupported NAM architecture: " + name);
            }

            if (! node.contains ("version") || ! node["version"].is_string()
                || ! node.contains ("config") || ! node["config"].is_object()
                || ! node.contains ("weights") || ! node["weights"].is_array())
            {
                throw std::runtime_error ("NAM model entry is incomplete");
            }

            if (const auto rate = node.find ("sample_rate"); rate != node.end())
            {
                if (! rate->is_number())
                    throw std::runtime_error ("NAM model entry is not a 48 kHz model");

                const auto declaredRate = rate->get<double>();

                if (! std::isfinite (declaredRate)
                    || (declaredRate > 0.0 && ! isSupportedSampleRate (declaredRate)))
                {
                    throw std::runtime_error ("NAM model entry is not a 48 kHz model");
                }
            }

            for (const auto& weight : node["weights"])
            {
                if (! weight.is_number() || ! std::isfinite (weight.get<double>()))
                    throw std::runtime_error ("NAM weights must be finite numbers");
            }
        }

        for (const auto& [key, value] : node.items())
        {
            if (key == "groups" || key == "groups_input" || key == "groups_input_mixin")
            {
                if (! value.is_number_integer() || value.get<juce::int64>() <= 0)
                    throw std::runtime_error ("NAM group counts must be positive integers");
            }

            if (key != "weights")
                validateModelNode (value);
        }
    }

    static void forceNAMCoreParserRegistration()
    {
        using ParserFunction = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);

        // Diese Referenzen halten die NAMCore-Parser trotz LTO im Binary.
        static volatile ParserFunction keepAliveArray[] = {
            &nam::container::create_config,
            &nam::linear::create_config,
            &nam::lstm::create_config,
            &nam::wavenet::create_config
        };

        volatile auto dummy = keepAliveArray[0];
        (void) dummy;
    }

    static float calculateModelOutputTrim (const nam::DSP& model)
    {
        if (! model.HasLoudness())
            return 1.0f;

        const auto loudnessDb = model.GetLoudness();

        if (! std::isfinite (loudnessDb))
            return 1.0f;

        constexpr auto targetLoudnessDb = -18.0;
        const auto trimDb = targetLoudnessDb - loudnessDb;
        const auto trim = static_cast<float> (std::pow (10.0, trimDb / 20.0));
        return std::isfinite (trim) && trim > 0.0f ? trim : 1.0f;
    }

    static bool isSupportedSampleRate (double rate) noexcept
    {
        return std::isfinite (rate) && std::abs (rate - supportedSampleRate) < 0.5;
    }

    static constexpr double supportedSampleRate = 48000.0;
    static constexpr int maximumModelChannels = 8;
    static constexpr juce::int64 maximumNAMFileSizeBytes = 256 * 1024 * 1024;
    static constexpr auto unsupportedHostSampleRateError = "NAM supports 48 kHz host sessions only.";

    std::unique_ptr<nam::DSP> dspModel;
    mutable std::mutex modelMutex;
    std::vector<std::vector<NAM_SAMPLE>> modelInputBuffers;
    std::vector<std::vector<NAM_SAMPLE>> modelOutputBuffers;
    std::vector<NAM_SAMPLE*> inputChannelPointers;
    std::vector<NAM_SAMPLE*> outputChannelPointers;
    float modelOutputTrim = 1.0f;
    int modelInputChannels = 1;
    int modelOutputChannels = 1;
    juce::String lastError;
    double sampleRate = supportedSampleRate;
    int maxBlockSize = 512;
};
} // namespace better
