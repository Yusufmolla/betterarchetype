#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "NAM/activations.h"
#include "NAM/container.h"
#include "NAM/convnet.h"
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

    void prepare (double newSampleRate, int newMaxBlockSize)
    {
        sampleRate = newSampleRate;
        maxBlockSize = juce::jmax (1, newMaxBlockSize);

        const std::lock_guard<std::mutex> lock (modelMutex);

        reserveProcessBuffers();

        if (dspModel)
            dspModel->Reset (sampleRate, maxBlockSize);
    }

    bool loadNAMFile (const juce::File& namFile)
    {
        lastError.clear();

        if (! namFile.existsAsFile())
            return failAndClear ("Datei existiert nicht.");

        try
        {
            forceNAMCoreParserRegistration();
            nam::activations::Activation::disable_fast_tanh();

            auto modelJson = nlohmann::json::parse (namFile.loadFileAsString().toStdString());
            normalizeArchitectureNames (modelJson);

            nam::DspLoadOptions options;
            options.prewarm = false;

            auto loadedModel = nam::get_dsp (modelJson, options);

            if (! loadedModel)
                return failAndClear ("NAMCore gab kein DSP-Objekt zurück.");

            loadedModel->Reset (sampleRate, maxBlockSize);
            const auto loadedModelOutputTrim = calculateModelOutputTrim (*loadedModel);

            {
                const std::lock_guard<std::mutex> lock (modelMutex);
                dspModel = std::move (loadedModel);
                modelOutputTrim = loadedModelOutputTrim;
                modelInputChannels = juce::jmax (1, dspModel->NumInputChannels());
                modelOutputChannels = juce::jmax (1, dspModel->NumOutputChannels());
                reserveProcessBuffers();
            }

            currentModelFile = namFile;
            modelName = namFile.getFileNameWithoutExtension();
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
        dspModel.reset();
        modelName.clear();
        currentModelFile = juce::File();
    }

    bool processMonoBlock (const float* input, float* output, int numSamples)
    {
        if (input == nullptr || output == nullptr || numSamples <= 0)
            return false;

        std::unique_lock<std::mutex> lock (modelMutex, std::try_to_lock);

        if (! lock.owns_lock() || ! dspModel)
        {
            std::copy (input, input + numSamples, output);
            return false;
        }

        const auto inputChannels = juce::jmax (1, modelInputChannels);
        const auto outputChannels = juce::jmax (1, modelOutputChannels);

        for (int channel = 0; channel < inputChannels; ++channel)
        {
            auto& channelBuffer = modelInputBuffers[(size_t) channel];
            inputChannelPointers[(size_t) channel] = channelBuffer.data();

            for (int sample = 0; sample < numSamples; ++sample)
                channelBuffer[(size_t) sample] = static_cast<NAM_SAMPLE> (input[sample]);
        }

        for (int channel = 0; channel < outputChannels; ++channel)
        {
            auto& channelBuffer = modelOutputBuffers[(size_t) channel];
            std::fill (channelBuffer.begin(), channelBuffer.begin() + numSamples, static_cast<NAM_SAMPLE> (0));
            outputChannelPointers[(size_t) channel] = channelBuffer.data();
        }

        try
        {
            dspModel->process (inputChannelPointers.data(), outputChannelPointers.data(), numSamples);

            for (int i = 0; i < numSamples; ++i)
            {
                auto sample = 0.0f;

                for (int channel = 0; channel < outputChannels; ++channel)
                    sample += static_cast<float> (modelOutputBuffers[(size_t) channel][(size_t) i]);

                sample = (sample / static_cast<float> (outputChannels)) * modelOutputTrim;

                if (! std::isfinite (sample))
                    sample = 0.0f;

                // Letzte Schutzstufe gegen kaputte oder extrem laute Modelle.
                output[i] = juce::jlimit (-2.0f, 2.0f, sample);
            }

            return true;
        }
        catch (...)
        {
            std::copy (input, input + numSamples, output);
            return false;
        }
    }

    [[nodiscard]] bool isModelLoaded() const
    {
        const std::lock_guard<std::mutex> lock (modelMutex);
        return dspModel != nullptr;
    }

    [[nodiscard]] juce::String getModelName() const { return modelName; }
    [[nodiscard]] juce::File getCurrentModelFile() const { return currentModelFile; }
    [[nodiscard]] juce::String getLastError() const { return lastError; }

private:
    bool failAndClear (juce::String error)
    {
        lastError = std::move (error);
        clear();
        return false;
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

    static void forceNAMCoreParserRegistration()
    {
        using ParserFunction = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);

        // Diese Referenzen halten die NAMCore-Parser trotz LTO im Binary.
        static volatile ParserFunction keepAliveArray[] = {
            &nam::container::create_config,
            &nam::convnet::create_config,
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
        constexpr auto maxAutoCutDb = -12.0;
        constexpr auto maxAutoBoostDb = 6.0;

        const auto trimDb = juce::jlimit (maxAutoCutDb, maxAutoBoostDb, targetLoudnessDb - loudnessDb);
        return static_cast<float> (std::pow (10.0, trimDb / 20.0));
    }

    std::unique_ptr<nam::DSP> dspModel;
    mutable std::mutex modelMutex;
    std::vector<std::vector<NAM_SAMPLE>> modelInputBuffers;
    std::vector<std::vector<NAM_SAMPLE>> modelOutputBuffers;
    std::vector<NAM_SAMPLE*> inputChannelPointers;
    std::vector<NAM_SAMPLE*> outputChannelPointers;
    float modelOutputTrim = 1.0f;
    int modelInputChannels = 1;
    int modelOutputChannels = 1;
    juce::File currentModelFile;
    juce::String modelName;
    juce::String lastError;
    double sampleRate = 44100.0;
    int maxBlockSize = 512;
};
} // namespace better
