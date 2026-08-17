#include "audio/graph/ModuleRegistry.h"
#include "audio/modules/DriveProcessor.h"
#include "audio/modules/IRCabProcessor.h"
#include "audio/modules/IRLoader.h"
#include "audio/modules/NAMModelLoader.h"
#include "audio/modules/NAMProcessor.h"
#include "core/GraphAudioProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <vector>

namespace better::tests
{
namespace
{
constexpr auto testSampleRate = 48000.0;
constexpr auto testSampleCount = 8192;
constexpr auto maximumModelBlockSize = 512;
constexpr auto convolutionBlockSize = 256;
constexpr auto convolutionFingerprintSamples = 4096;

struct AudioFingerprint
{
    double rms = 0.0;
    double meanAbsolute = 0.0;
    double deltaRms = 0.0;
    double peak = 0.0;
};

constexpr AudioFingerprint expectedNamFingerprint
{
    0.129293370894,
    0.117160082614,
    0.014214262734,
    0.268905937672
};

constexpr AudioFingerprint expectedIRFingerprint
{
    0.004313805248,
    0.000824231862,
    0.001522039248,
    0.121618575574
};

constexpr auto fingerprintRelativeTolerance = 0.005;

juce::File getTestAsset (const juce::String& fileName)
{
    return juce::File (BETTER_ARCHETYPE_TEST_ASSETS_DIR).getChildFile (fileName);
}

std::vector<float> loadMonoSamples (const juce::File& file, int maximumSamples)
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();
    const auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (file));

    if (reader == nullptr || reader->lengthInSamples <= 0 || reader->numChannels == 0)
        return {};

    const auto samplesToRead = static_cast<int> (juce::jmin (reader->lengthInSamples,
                                                             static_cast<juce::int64> (maximumSamples)));
    juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels), samplesToRead);

    if (! reader->read (&buffer, 0, samplesToRead, 0, true, true))
        return {};

    std::vector<float> result (static_cast<size_t> (samplesToRead));

    for (int sample = 0; sample < samplesToRead; ++sample)
    {
        auto mono = 0.0f;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            mono += buffer.getSample (channel, sample);

        result[static_cast<size_t> (sample)] = mono / static_cast<float> (buffer.getNumChannels());
    }

    return result;
}

bool containsOnlyFiniteSamples (const std::vector<float>& samples)
{
    return std::all_of (samples.begin(), samples.end(), [] (float sample)
    {
        return std::isfinite (sample);
    });
}

double calculateRms (const std::vector<float>& samples)
{
    if (samples.empty())
        return 0.0;

    auto sumOfSquares = 0.0;

    for (const auto sample : samples)
        sumOfSquares += static_cast<double> (sample) * static_cast<double> (sample);

    return std::sqrt (sumOfSquares / static_cast<double> (samples.size()));
}

double calculateDifferenceRms (const std::vector<float>& first, const std::vector<float>& second)
{
    if (first.size() != second.size() || first.empty())
        return std::numeric_limits<double>::infinity();

    auto sumOfSquares = 0.0;

    for (size_t index = 0; index < first.size(); ++index)
    {
        const auto difference = static_cast<double> (first[index]) - static_cast<double> (second[index]);
        sumOfSquares += difference * difference;
    }

    return std::sqrt (sumOfSquares / static_cast<double> (first.size()));
}

AudioFingerprint calculateFingerprint (const std::vector<float>& samples)
{
    if (samples.empty())
        return {};

    auto sumOfSquares = 0.0;
    auto sumOfAbsoluteValues = 0.0;
    auto sumOfDeltaSquares = 0.0;
    auto peak = 0.0;

    for (size_t index = 0; index < samples.size(); ++index)
    {
        const auto sample = static_cast<double> (samples[index]);
        sumOfSquares += sample * sample;
        sumOfAbsoluteValues += std::abs (sample);
        peak = std::max (peak, std::abs (sample));

        if (index > 0)
        {
            const auto delta = sample - static_cast<double> (samples[index - 1]);
            sumOfDeltaSquares += delta * delta;
        }
    }

    return {
        std::sqrt (sumOfSquares / static_cast<double> (samples.size())),
        sumOfAbsoluteValues / static_cast<double> (samples.size()),
        std::sqrt (sumOfDeltaSquares
                   / static_cast<double> (juce::jmax (static_cast<size_t> (1),
                                                      samples.size() - 1))),
        peak
    };
}

void expectFingerprint (juce::UnitTest& test,
                        const juce::String& name,
                        const AudioFingerprint& actual,
                        const AudioFingerprint& expected,
                        double relativeTolerance)
{
    const auto check = [&] (const juce::String& metric, double actualValue, double expectedValue)
    {
        const auto tolerance = juce::jmax (1.0e-8, std::abs (expectedValue) * relativeTolerance);
        test.expectWithinAbsoluteError (actualValue,
                                        expectedValue,
                                        tolerance,
                                        name + " " + metric + " fingerprint changed");
    };

    check ("RMS", actual.rms, expected.rms);
    check ("mean absolute", actual.meanAbsolute, expected.meanAbsolute);
    check ("delta RMS", actual.deltaRms, expected.deltaRms);
    check ("peak", actual.peak, expected.peak);
}

std::vector<float> renderModuleImpulse (AudioModuleProcessor& processor,
                                        int numSamples,
                                        int processingBlockSize = convolutionBlockSize)
{
    std::vector<float> output (static_cast<size_t> (numSamples));
    processingBlockSize = juce::jmax (1, processingBlockSize);
    juce::AudioBuffer<float> block (2, processingBlockSize);
    juce::MidiBuffer midi;

    for (int offset = 0; offset < numSamples; offset += processingBlockSize)
    {
        const auto samplesThisTime = juce::jmin (processingBlockSize, numSamples - offset);
        block.clear();

        if (offset == 0)
        {
            block.setSample (0, 0, 1.0f);
            block.setSample (1, 0, 1.0f);
        }

        processor.processBlock (block, midi);
        std::copy_n (block.getReadPointer (0),
                     samplesThisTime,
                     output.begin() + static_cast<size_t> (offset));
    }

    return output;
}

bool processInBlocks (NAMModelLoader& loader,
                      const std::vector<float>& input,
                      std::vector<float>& output,
                      int blockSize)
{
    output.resize (input.size());

    for (size_t offset = 0; offset < input.size(); offset += static_cast<size_t> (blockSize))
    {
        const auto remaining = input.size() - offset;
        const auto samplesThisTime = static_cast<int> (juce::jmin (remaining,
                                                                   static_cast<size_t> (blockSize)));

        if (! loader.processMonoBlock (input.data() + offset, output.data() + offset, samplesThisTime))
            return false;
    }

    return true;
}

bool containsConnection (const std::vector<GraphConnectionDescription>& connections,
                         GraphNodeUID source,
                         GraphNodeUID destination)
{
    return std::find (connections.begin(), connections.end(),
                      GraphConnectionDescription { source, destination }) != connections.end();
}

juce::ValueTree decodeProcessorState (const juce::MemoryBlock& data)
{
    if (const auto xml = juce::AudioProcessor::getXmlFromBinary (data.getData(),
                                                                 static_cast<int> (data.getSize())))
    {
        return juce::ValueTree::fromXml (*xml);
    }

    return {};
}

juce::MemoryBlock encodeProcessorState (const juce::ValueTree& state)
{
    juce::MemoryBlock data;

    if (const auto xml = state.createXml())
        juce::AudioProcessor::copyXmlToBinary (*xml, data);

    return data;
}

juce::AudioProcessor::BusesLayout makeMainBusLayout (juce::AudioChannelSet input,
                                                       juce::AudioChannelSet output)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (input);
    layout.outputBuses.add (output);
    return layout;
}

bool waitForModuleLoad (AudioModuleProcessor& processor, double timeoutMilliseconds = 10000.0)
{
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMilliseconds;

    while (juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        const auto status = processor.getStatusText();

        if (status.startsWith ("Loaded "))
            return true;

        if (status.startsWith ("Bypass:"))
            return false;

        juce::Thread::sleep (2);
    }

    return false;
}

class StateChangeRecorder final : public juce::AudioProcessorListener
{
public:
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}

    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details) override
    {
        if (details.nonParameterStateChanged)
            nonParameterStateChanges.fetch_add (1, std::memory_order_relaxed);
    }

    int getCount() const noexcept
    {
        return nonParameterStateChanges.load (std::memory_order_relaxed);
    }

private:
    std::atomic<int> nonParameterStateChanges { 0 };
};

class CoreStructureTests final : public juce::UnitTest
{
public:
    CoreStructureTests()
        : UnitTest ("Core structure", "BetterArchetype")
    {
    }

    void runTest() override
    {
        beginTest ("Test assets are readable");

        const auto namFile = getTestAsset ("JP2CCAP2.nam");
        const auto irFile = getTestAsset ("MLSL_BESTIR.wav");
        const auto namDIFile = getTestAsset ("nam_di.wav");

        expect (namFile.existsAsFile(), namFile.getFullPathName());
        expect (irFile.existsAsFile(), irFile.getFullPathName());
        expect (namDIFile.existsAsFile(), namDIFile.getFullPathName());

        const auto ir = IRLoader::loadIRFile (irFile);
        expect (ir != nullptr);

        if (ir != nullptr)
        {
            expectEquals (ir->sampleRate, 48000);
            expect (! ir->samples.empty());
            expect (containsOnlyFiniteSamples (ir->samples));

            beginTest ("IR module renders the Gateway-scaled impulse response");

            const auto gatewayGain48k = IRCabProcessor::calculateGatewayIRGain (ir->sampleRate);
            const auto gatewayGain96kIR = IRCabProcessor::calculateGatewayIRGain (96000.0);
            expectWithinAbsoluteError (static_cast<double> (juce::Decibels::gainToDecibels (gatewayGain48k)),
                                       -18.0,
                                       1.0e-5,
                                       "Gateway IR gain must be -18 dB at 48 kHz");
            expectWithinAbsoluteError (static_cast<double> (gatewayGain96kIR),
                                       static_cast<double> (gatewayGain48k * 0.5f),
                                       1.0e-7,
                                       "Gateway compensation must use the original IR sample rate");

            IRCabProcessor irProcessor;
            irProcessor.loadIRFileAsync (irFile);
            const auto irProcessorLoaded = waitForModuleLoad (irProcessor);
            expect (irProcessorLoaded, irProcessor.getStatusText());

            if (irProcessorLoaded)
            {
                // Load before prepare so JUCE publishes the IR for the first render.
                irProcessor.prepareToPlay (testSampleRate, convolutionBlockSize);
                const auto convolved = renderModuleImpulse (irProcessor,
                                                             convolutionFingerprintSamples);
                expect (containsOnlyFiniteSamples (convolved));
                expect (calculateRms (convolved) > 1.0e-6);

                const auto fingerprint = calculateFingerprint (convolved);
                expectFingerprint (*this,
                                   "Gateway-scaled IR",
                                   fingerprint,
                                   expectedIRFingerprint,
                                   fingerprintRelativeTolerance);

                const auto convolvedWithoutReset = renderModuleImpulse (irProcessor,
                                                                         convolutionFingerprintSamples);
                expect (calculateDifferenceRms (convolved, convolvedWithoutReset) > 1.0e-7,
                        "IR fixture did not leave observable convolution history");

                irProcessor.reset();
                const auto convolvedAfterReset = renderModuleImpulse (irProcessor,
                                                                       convolutionFingerprintSamples);
                expect (calculateDifferenceRms (convolved, convolvedAfterReset) < 1.0e-7,
                        "Reset did not clear the convolution history");
            }

            beginTest ("IR loads after prepare and chunks oversized host blocks");

            IRCabProcessor runtimeIRProcessor;
            runtimeIRProcessor.prepareToPlay (testSampleRate, 64);
            runtimeIRProcessor.loadIRFileAsync (irFile);

            const auto pendingIRState = runtimeIRProcessor.createModuleState();
            expectEquals (pendingIRState.getProperty ("irPath").toString(),
                          irFile.getFullPathName(),
                          "Saving during IR loading lost the selected file");

            const auto runtimeIRLoaded = waitForModuleLoad (runtimeIRProcessor);
            expect (runtimeIRLoaded, runtimeIRProcessor.getStatusText());

            if (runtimeIRLoaded)
            {
                juce::MidiBuffer irMidi;

                runtimeIRProcessor.reset();
                const auto chunkedIR = renderModuleImpulse (runtimeIRProcessor, 257, 64);
                runtimeIRProcessor.reset();
                const auto oversizedIR = renderModuleImpulse (runtimeIRProcessor, 257, 257);
                expect (containsOnlyFiniteSamples (oversizedIR));
                expect (calculateDifferenceRms (chunkedIR, oversizedIR) < 1.0e-7,
                        "IR output changed when a host block exceeded the prepared size");

                beginTest ("IR sanitises its main bus without touching extra channels");

                runtimeIRProcessor.reset();
                const auto cleanIR = renderModuleImpulse (runtimeIRProcessor, 512, 64);
                runtimeIRProcessor.reset();

                juce::AudioBuffer<float> invalidInput (2, 64);
                invalidInput.clear();
                invalidInput.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
                invalidInput.setSample (1, 0, std::numeric_limits<float>::infinity());
                runtimeIRProcessor.processBlock (invalidInput, irMidi);

                const auto afterInvalidInput = renderModuleImpulse (runtimeIRProcessor, 512, 64);
                expect (calculateDifferenceRms (cleanIR, afterInvalidInput) < 1.0e-7,
                        "Invalid input poisoned the convolution history");

                juce::AudioBuffer<float> extraChannelBuffer (3, 64);
                extraChannelBuffer.clear();
                juce::FloatVectorOperations::fill (extraChannelBuffer.getWritePointer (2), 0.375f, 64);
                extraChannelBuffer.setSample (0, 0, 1.0f);
                extraChannelBuffer.setSample (1, 0, 1.0f);
                runtimeIRProcessor.processBlock (extraChannelBuffer, irMidi);

                for (int sample = 0; sample < extraChannelBuffer.getNumSamples(); ++sample)
                    expectWithinAbsoluteError (extraChannelBuffer.getSample (2, sample), 0.375f, 0.0f,
                                               "IR processing modified a channel outside its main bus");

                if (auto* enabled = runtimeIRProcessor.getParameters().getParameter ("enabled"))
                    enabled->setValueNotifyingHost (0.0f);

                juce::AudioBuffer<float> bypassBuffer (3, 8);

                for (int channel = 0; channel < bypassBuffer.getNumChannels(); ++channel)
                    for (int sample = 0; sample < bypassBuffer.getNumSamples(); ++sample)
                        bypassBuffer.setSample (channel, sample,
                                                static_cast<float> ((channel + 1) * 10 + sample) / 100.0f);

                juce::AudioBuffer<float> bypassReference;
                bypassReference.makeCopyOf (bypassBuffer);
                runtimeIRProcessor.processBlock (bypassBuffer, irMidi);

                for (int channel = 0; channel < bypassBuffer.getNumChannels(); ++channel)
                    for (int sample = 0; sample < bypassBuffer.getNumSamples(); ++sample)
                        expectWithinAbsoluteError (bypassBuffer.getSample (channel, sample),
                                                   bypassReference.getSample (channel, sample),
                                                   0.0f,
                                                   "Disabled module did not bypass exactly");
            }
        }

        beginTest ("IR loader rejects non-audio and overlong cabinet files");
        expect (IRLoader::loadIRFile (namFile) == nullptr);

        IRCabProcessor rejectedIRProcessor;
        rejectedIRProcessor.prepareToPlay (testSampleRate, 64);
        rejectedIRProcessor.loadIRFileAsync (namFile);
        expect (! waitForModuleLoad (rejectedIRProcessor));
        expectEquals (rejectedIRProcessor.createModuleState().getProperty ("irPath").toString(),
                      namFile.getFullPathName(),
                      "A failed IR load discarded the selected state path");

        juce::TemporaryFile overlongIRFile (".wav");
        auto outputStream = std::unique_ptr<juce::OutputStream> (
            overlongIRFile.getFile().createOutputStream());
        juce::WavAudioFormat wavFormat;
        auto wavWriter = wavFormat.createWriterFor (
            outputStream,
            juce::AudioFormatWriterOptions {}
                .withSampleRate (testSampleRate)
                .withNumChannels (1)
                .withBitsPerSample (24));
        expect (wavWriter != nullptr);

        if (wavWriter != nullptr)
        {
            juce::AudioBuffer<float> overlongIR (1, 96001);
            overlongIR.clear();
            overlongIR.setSample (0, 0, 1.0f);
            expect (wavWriter->writeFromAudioSampleBuffer (overlongIR, 0, overlongIR.getNumSamples()));
            wavWriter.reset();
            expect (IRLoader::loadIRFile (overlongIRFile.getFile()) == nullptr,
                    "An IR longer than the advertised two-second tail was accepted");
        }

        expectEquals (static_cast<int> (loadMonoSamples (namDIFile, 64).size()), 64);

        beginTest ("Registry IDs are unique and factories work");

        std::set<juce::String> moduleIds;

        for (const auto& descriptor : ModuleRegistry::getModules())
        {
            expect (descriptor.moduleId.isNotEmpty());
            expect (moduleIds.insert (descriptor.moduleId).second,
                    "Duplicate module ID: " + descriptor.moduleId);

            std::set<juce::String> controlIds;

            for (const auto& control : descriptor.controls)
            {
                expect (control.parameterId.isNotEmpty(), descriptor.moduleId + " has an empty control ID");
                expect (controlIds.insert (control.parameterId).second,
                        descriptor.moduleId + " has a duplicate control ID: " + control.parameterId);

                if (control.kind == ModuleControlKind::slider)
                {
                    expect (std::isfinite (control.minimum)
                            && std::isfinite (control.maximum)
                            && std::isfinite (control.interval)
                            && std::isfinite (control.defaultValue));
                    expect (control.minimum < control.maximum);
                    expect (control.interval > 0.0f);
                    expect (control.defaultValue >= control.minimum
                            && control.defaultValue <= control.maximum);
                }
            }

            const auto processor = ModuleRegistry::createProcessor (descriptor.moduleId);
            expect (processor != nullptr, "Factory failed for: " + descriptor.moduleId);

            if (processor != nullptr)
                expectEquals (processor->getDescriptor().moduleId, descriptor.moduleId);
        }

        expect (ModuleRegistry::findModule ("does_not_exist") == nullptr);
        expect (ModuleRegistry::createProcessor ("does_not_exist") == nullptr);

        beginTest ("Module state requires the correct type and identity");

        DriveProcessor directDrive;
        auto* directDriveParameter = directDrive.getParameters().getParameter ("drive");
        expect (directDriveParameter != nullptr);

        if (directDriveParameter != nullptr)
        {
            constexpr auto storedValue = 0.81f;
            constexpr auto currentValue = 0.22f;
            directDriveParameter->setValueNotifyingHost (
                directDriveParameter->convertTo0to1 (storedValue));
            const auto validDriveState = directDrive.createModuleState();
            directDriveParameter->setValueNotifyingHost (
                directDriveParameter->convertTo0to1 (currentValue));

            auto missingIdState = validDriveState.createCopy();
            missingIdState.removeProperty ("moduleId", nullptr);
            directDrive.restoreModuleState (missingIdState);
            expectWithinAbsoluteError (directDriveParameter->convertFrom0to1 (
                                           directDriveParameter->getValue()),
                                       currentValue,
                                       1.0e-6f,
                                       "A module state without an ID was applied");

            auto wrongIdState = validDriveState.createCopy();
            wrongIdState.setProperty ("moduleId", "nam", nullptr);
            directDrive.restoreModuleState (wrongIdState);
            expectWithinAbsoluteError (directDriveParameter->convertFrom0to1 (
                                           directDriveParameter->getValue()),
                                       currentValue,
                                       1.0e-6f,
                                       "A foreign module state was applied");

            juce::ValueTree wrongTypeState ("NotModuleState");
            wrongTypeState.setProperty ("moduleId", "drive", nullptr);
            wrongTypeState.addChild (
                validDriveState.getChildWithName ("Parameters").createCopy(), -1, nullptr);
            directDrive.restoreModuleState (wrongTypeState);
            expectWithinAbsoluteError (directDriveParameter->convertFrom0to1 (
                                           directDriveParameter->getValue()),
                                       currentValue,
                                       1.0e-6f,
                                       "A state with the wrong root type was applied");

            directDrive.restoreModuleState (validDriveState);
            expectWithinAbsoluteError (directDriveParameter->convertFrom0to1 (
                                           directDriveParameter->getValue()),
                                       storedValue,
                                       1.0e-6f,
                                       "A valid module state was not applied");
        }

        beginTest ("Graph mutations and child parameters notify the host state");

        StateChangeRecorder hostStateChanges;
        GraphAudioProcessor notificationGraph;
        notificationGraph.addListener (&hostStateChanges);
        GraphNodeUID rejectedNode = 0;
        const auto countBeforeRejectedMutation = hostStateChanges.getCount();
        expect (! notificationGraph.addModuleNode ("does_not_exist", { 0.0f, 0.0f }, rejectedNode));
        expectEquals (hostStateChanges.getCount(), countBeforeRejectedMutation,
                      "A rejected graph mutation notified the host");

        GraphNodeUID notifiedDriveNode = 0;
        auto previousNotificationCount = hostStateChanges.getCount();
        expect (notificationGraph.addModuleNode ("drive", { 240.0f, 180.0f }, notifiedDriveNode));
        expect (hostStateChanges.getCount() > previousNotificationCount,
                "Adding a module did not mark the host state dirty");

        if (auto notifiedDrive = notificationGraph.getModuleProcessorForNode (notifiedDriveNode))
        {
            if (auto* parameter = notifiedDrive->getParameters().getParameter ("drive"))
            {
                previousNotificationCount = hostStateChanges.getCount();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.73f));
                expect (hostStateChanges.getCount() > previousNotificationCount,
                        "A child module parameter did not mark the top-level state dirty");
            }
        }

        previousNotificationCount = hostStateChanges.getCount();
        expect (notificationGraph.removeGraphConnection ({ inputNodeUID, outputNodeUID }));
        expect (hostStateChanges.getCount() > previousNotificationCount,
                "Removing a connection did not mark the host state dirty");

        previousNotificationCount = hostStateChanges.getCount();
        expect (notificationGraph.addGraphConnection (inputNodeUID, outputNodeUID));
        expect (hostStateChanges.getCount() > previousNotificationCount,
                "Adding a connection did not mark the host state dirty");

        GraphNodeUID notifiedIRNode = 0;
        expect (notificationGraph.addModuleNode ("ir", { 420.0f, 180.0f }, notifiedIRNode));
        previousNotificationCount = hostStateChanges.getCount();
        notificationGraph.loadIRFileForNode (notifiedIRNode, irFile);
        expect (hostStateChanges.getCount() > previousNotificationCount,
                "Selecting an IR did not mark the host state dirty");

        if (auto notifiedIR = notificationGraph.getModuleProcessorForNode (notifiedIRNode))
        {
            expectEquals (notifiedIR->createModuleState().getProperty ("irPath").toString(),
                          irFile.getFullPathName(),
                          "The graph lost an IR path while the load was pending");
        }

        notificationGraph.removeListener (&hostStateChanges);

        beginTest ("Graph accepts only matching mono and stereo layouts");

        const auto mono = juce::AudioChannelSet::mono();
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto disabled = juce::AudioChannelSet::disabled();
        const auto monoLayout = makeMainBusLayout (mono, mono);
        const auto stereoLayout = makeMainBusLayout (stereo, stereo);
        const auto monoToStereoLayout = makeMainBusLayout (mono, stereo);
        const auto stereoToMonoLayout = makeMainBusLayout (stereo, mono);

        GraphAudioProcessor layoutGraph;
        expect (layoutGraph.isBusesLayoutSupported (monoLayout));
        expect (layoutGraph.isBusesLayoutSupported (stereoLayout));
        expect (! layoutGraph.isBusesLayoutSupported (monoToStereoLayout));
        expect (! layoutGraph.isBusesLayoutSupported (stereoToMonoLayout));
        expect (! layoutGraph.isBusesLayoutSupported (makeMainBusLayout (disabled, stereo)));
        expect (! layoutGraph.isBusesLayoutSupported (makeMainBusLayout (stereo, disabled)));
        expect (! layoutGraph.setBusesLayout (monoToStereoLayout));
        expectEquals (layoutGraph.getTotalNumInputChannels(), 2);
        expectEquals (layoutGraph.getTotalNumOutputChannels(), 2);

        GraphAudioProcessor monoGraph;
        const auto monoLayoutApplied = monoGraph.setBusesLayout (monoLayout);
        expect (monoLayoutApplied);

        juce::MidiBuffer layoutMidi;

        if (monoLayoutApplied)
        {
            monoGraph.prepareToPlay (testSampleRate, 64);

            juce::AudioBuffer<float> monoBuffer (1, 5);
            const std::array<float, 5> monoSamples { 1.25f, -1.40f, 0.5f, -0.25f, 0.0f };

            for (size_t sample = 0; sample < monoSamples.size(); ++sample)
                monoBuffer.setSample (0, static_cast<int> (sample), monoSamples[sample]);

            monoGraph.processBlock (monoBuffer, layoutMidi);

            for (size_t sample = 0; sample < monoSamples.size(); ++sample)
                expectWithinAbsoluteError (monoBuffer.getSample (0, static_cast<int> (sample)),
                                           monoSamples[sample],
                                           1.0e-7f);

            monoGraph.releaseResources();
        }

        GraphAudioProcessor stereoGraph;
        stereoGraph.prepareToPlay (testSampleRate, 64);
        juce::AudioBuffer<float> stereoBuffer (2, 4);
        const std::array<float, 4> leftSamples { 0.1f, 0.2f, -0.3f, 0.4f };
        const std::array<float, 4> rightSamples { -0.5f, 0.6f, 0.7f, -0.8f };

        for (size_t sample = 0; sample < leftSamples.size(); ++sample)
        {
            stereoBuffer.setSample (0, static_cast<int> (sample), leftSamples[sample]);
            stereoBuffer.setSample (1, static_cast<int> (sample), rightSamples[sample]);
        }

        stereoGraph.processBlock (stereoBuffer, layoutMidi);

        for (size_t sample = 0; sample < leftSamples.size(); ++sample)
        {
            expectWithinAbsoluteError (stereoBuffer.getSample (0, static_cast<int> (sample)),
                                       leftSamples[sample],
                                       1.0e-7f);
            expectWithinAbsoluteError (stereoBuffer.getSample (1, static_cast<int> (sample)),
                                       rightSamples[sample],
                                       1.0e-7f);
        }

        stereoGraph.releaseResources();

        beginTest ("Graph reset clears module DSP history");

        GraphAudioProcessor resetGraph;
        GraphNodeUID resetDriveNode = 0;
        const auto driveNodeAdded = resetGraph.addModuleNode ("drive", { 300.0f, 200.0f }, resetDriveNode);
        expect (driveNodeAdded);

        const auto defaultConnectionRemoved = driveNodeAdded
                                           && resetGraph.removeGraphConnection ({ inputNodeUID, outputNodeUID });
        expect (defaultConnectionRemoved);

        const auto inputConnected = defaultConnectionRemoved
                                 && resetGraph.addGraphConnection (inputNodeUID, resetDriveNode);
        expect (inputConnected);

        const auto outputConnected = inputConnected
                                  && resetGraph.addGraphConnection (resetDriveNode, outputNodeUID);
        expect (outputConnected);

        if (outputConnected)
        {
            resetGraph.prepareToPlay (testSampleRate, 64);

            const auto renderDriveProbe = [&resetGraph]
            {
                juce::AudioBuffer<float> buffer (2, 64);
                buffer.clear();
                buffer.setSample (0, 0, 0.5f);
                buffer.setSample (1, 0, 0.5f);
                juce::MidiBuffer midi;
                resetGraph.processBlock (buffer, midi);

                std::vector<float> output (64);
                std::copy_n (buffer.getReadPointer (0), output.size(), output.begin());
                return output;
            };

            resetGraph.reset();
            const auto resetReference = renderDriveProbe();
            juce::AudioBuffer<float> conditioningBuffer (2, 64);
            juce::MidiBuffer resetMidi;

            for (int block = 0; block < 4; ++block)
            {
                for (int channel = 0; channel < conditioningBuffer.getNumChannels(); ++channel)
                    juce::FloatVectorOperations::fill (conditioningBuffer.getWritePointer (channel), 0.75f, 64);

                resetGraph.processBlock (conditioningBuffer, resetMidi);
            }

            resetGraph.reset();
            const auto resetOutput = renderDriveProbe();
            expect (calculateDifferenceRms (resetReference, resetOutput) < 1.0e-7,
                    "Graph reset did not reach the Drive module");
            resetGraph.releaseResources();
        }

        beginTest ("Module handles keep removed processors alive");

        StateChangeRecorder removedNodeStateChanges;
        GraphAudioProcessor handleGraph;
        handleGraph.addListener (&removedNodeStateChanges);
        GraphNodeUID handledNode = 0;
        expect (handleGraph.addModuleNode ("drive", { 200.0f, 200.0f }, handledNode));

        auto moduleHandle = handleGraph.getModuleProcessorForNode (handledNode);
        expect (static_cast<bool> (moduleHandle));
        expect (handleGraph.removeGraphNode (handledNode));
        expect (! handleGraph.getModuleProcessorForNode (handledNode));
        expect (static_cast<bool> (moduleHandle),
                "Removing a graph node invalidated a live processor handle");

        if (moduleHandle)
        {
            expectEquals (moduleHandle->getDescriptor().moduleId, juce::String { "drive" });
            expect (moduleHandle->getParameters().copyState().isValid());

            const auto countAfterRemoval = removedNodeStateChanges.getCount();

            if (auto* parameter = moduleHandle->getParameters().getParameter ("drive"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (0.91f));

            expectEquals (removedNodeStateChanges.getCount(), countAfterRemoval,
                          "A retained removed node still called its old graph listener");
        }

        moduleHandle.reset();
        expect (! moduleHandle);
        handleGraph.removeListener (&removedNodeStateChanges);

        beginTest ("Graph topology and state round-trip");

        GraphAudioProcessor graph;
        expectEquals (static_cast<int> (graph.getGraphNodes().size()), 2);
        expect (containsConnection (graph.getGraphConnections(), inputNodeUID, outputNodeUID));

        GraphNodeUID driveNode = 0;
        expect (graph.addModuleNode ("drive", { 300.0f, 200.0f }, driveNode));
        expect (driveNode >= firstModuleNodeUID);
        expect (graph.removeGraphConnection ({ inputNodeUID, outputNodeUID }));
        expect (graph.addGraphConnection (inputNodeUID, driveNode));
        expect (graph.addGraphConnection (driveNode, outputNodeUID));
        expect (! graph.addGraphConnection (inputNodeUID, driveNode));
        expect (! graph.addGraphConnection (driveNode, driveNode));
        expect (! graph.addGraphConnection (outputNodeUID, driveNode));
        expect (graph.addGraphConnection (inputNodeUID, outputNodeUID),
                "The runtime graph rejected a valid fan-in route");

        constexpr auto storedDriveValue = 0.82f;

        if (auto drive = graph.getModuleProcessorForNode (driveNode))
        {
            if (auto* parameter = drive->getParameters().getParameter ("drive"))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (storedDriveValue));
        }

        juce::MemoryBlock state;
        graph.getStateInformation (state);
        expect (state.getSize() > 0);

        GraphAudioProcessor restoredGraph;
        restoredGraph.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        expectEquals (static_cast<int> (restoredGraph.getGraphNodes().size()), 3);
        expectEquals (static_cast<int> (restoredGraph.getGraphConnections().size()), 3);
        expect (containsConnection (restoredGraph.getGraphConnections(), inputNodeUID, driveNode));
        expect (containsConnection (restoredGraph.getGraphConnections(), driveNode, outputNodeUID));
        expect (containsConnection (restoredGraph.getGraphConnections(), inputNodeUID, outputNodeUID));

        const auto restoredDriveNode = restoredGraph.getGraphNode (driveNode);
        expectWithinAbsoluteError (restoredDriveNode.x, 300.0f, 1.0e-7f);
        expectWithinAbsoluteError (restoredDriveNode.y, 200.0f, 1.0e-7f);

        if (auto restoredDrive = restoredGraph.getModuleProcessorForNode (driveNode))
        {
            if (const auto* driveValue = restoredDrive->getParameters().getRawParameterValue ("drive"))
                expectWithinAbsoluteError (driveValue->load(), storedDriveValue, 1.0e-6f,
                                           "The module parameter state was not restored");
            else
                expect (false, "The restored Drive parameter is missing");
        }
        else
        {
            expect (false, "The restored Drive processor is missing");
        }

        beginTest ("Malformed graph state leaves the current graph untouched");

        GraphAudioProcessor guardedGraph;
        GraphNodeUID guardedDriveNode = 0;
        expect (guardedGraph.addModuleNode ("drive", { 345.0f, 210.0f }, guardedDriveNode));
        expect (guardedGraph.removeGraphConnection ({ inputNodeUID, outputNodeUID }));
        expect (guardedGraph.addGraphConnection (inputNodeUID, guardedDriveNode));
        expect (guardedGraph.addGraphConnection (guardedDriveNode, outputNodeUID));

        const auto expectedGuardedNodes = guardedGraph.getGraphNodes();
        const auto expectedGuardedConnections = guardedGraph.getGraphConnections();
        const auto expectedGuardedNode = guardedGraph.getGraphNode (guardedDriveNode);
        auto guardedHandle = guardedGraph.getModuleProcessorForNode (guardedDriveNode);
        const auto* guardedProcessor = guardedHandle.get();

        juce::MemoryBlock guardedStateData;
        guardedGraph.getStateInformation (guardedStateData);
        const auto guardedState = decodeProcessorState (guardedStateData);
        expect (guardedState.isValid());

        const auto expectRejectedState = [&] (const juce::ValueTree& rejectedState,
                                               const juce::String& reason)
        {
            const auto rejectedData = encodeProcessorState (rejectedState);
            expect (rejectedData.getSize() > 0, reason + " could not be encoded");
            guardedGraph.setStateInformation (rejectedData.getData(),
                                              static_cast<int> (rejectedData.getSize()));

            expectEquals (static_cast<int> (guardedGraph.getGraphNodes().size()),
                          static_cast<int> (expectedGuardedNodes.size()),
                          reason + " changed the node count");
            expect (guardedGraph.getGraphConnections() == expectedGuardedConnections,
                    reason + " changed the connections");

            const auto currentNode = guardedGraph.getGraphNode (guardedDriveNode);
            expectEquals (currentNode.moduleId, expectedGuardedNode.moduleId,
                          reason + " changed the module");
            expectWithinAbsoluteError (currentNode.x, expectedGuardedNode.x, 1.0e-7f,
                                       reason + " changed the node position");
            expect (guardedGraph.getModuleProcessorForNode (guardedDriveNode).get() == guardedProcessor,
                    reason + " replaced the runtime processor");
        };

        auto futureVersionState = guardedState.createCopy();
        futureVersionState.setProperty ("version", 2, nullptr);
        expectRejectedState (futureVersionState, "A future state version");

        auto unknownModuleState = guardedState.createCopy();
        auto storedNodes = unknownModuleState.getChildWithName ("Nodes");

        for (int i = 0; i < storedNodes.getNumChildren(); ++i)
        {
            auto storedNode = storedNodes.getChild (i);

            if ((int) storedNode.getProperty ("uid", 0) == (int) guardedDriveNode)
                storedNode.setProperty ("moduleId", "does_not_exist", nullptr);
        }

        expectRejectedState (unknownModuleState, "An unknown module");

        auto mismatchedModuleState = guardedState.createCopy();
        auto mismatchedNodes = mismatchedModuleState.getChildWithName ("Nodes");

        for (int i = 0; i < mismatchedNodes.getNumChildren(); ++i)
        {
            auto storedNode = mismatchedNodes.getChild (i);

            if ((int) storedNode.getProperty ("uid", 0) == (int) guardedDriveNode)
                storedNode.getChildWithName ("ModuleState").setProperty ("moduleId", "nam", nullptr);
        }

        expectRejectedState (mismatchedModuleState, "A mismatched module state");

        auto missingModuleIdentityState = guardedState.createCopy();
        auto missingIdentityNodes = missingModuleIdentityState.getChildWithName ("Nodes");

        for (int i = 0; i < missingIdentityNodes.getNumChildren(); ++i)
        {
            auto storedNode = missingIdentityNodes.getChild (i);

            if ((int) storedNode.getProperty ("uid", 0) == (int) guardedDriveNode)
                storedNode.getChildWithName ("ModuleState").removeProperty ("moduleId", nullptr);
        }

        expectRejectedState (missingModuleIdentityState, "A module state without an identity");

        auto danglingConnectionState = guardedState.createCopy();
        juce::ValueTree danglingConnection ("Connection");
        danglingConnection.setProperty ("source", (int) inputNodeUID, nullptr);
        danglingConnection.setProperty ("destination", 999999, nullptr);
        danglingConnectionState.getChildWithName ("Connections")
                               .addChild (danglingConnection, -1, nullptr);
        expectRejectedState (danglingConnectionState, "A dangling connection");

        auto invalidAllocatorState = guardedState.createCopy();
        invalidAllocatorState.setProperty ("nextModuleNode", -1, nullptr);
        expectRejectedState (invalidAllocatorState, "A negative UID allocator");

        auto missingEndpointState = guardedState.createCopy();
        auto endpointNodes = missingEndpointState.getChildWithName ("Nodes");

        for (int i = endpointNodes.getNumChildren(); --i >= 0;)
        {
            if ((int) endpointNodes.getChild (i).getProperty ("uid", 0) == (int) inputNodeUID)
                endpointNodes.removeChild (i, nullptr);
        }

        expectRejectedState (missingEndpointState, "A state without its Input node");

        beginTest ("An intentionally empty graph survives a state round-trip");

        GraphAudioProcessor emptyGraph;
        expect (emptyGraph.removeGraphConnection ({ inputNodeUID, outputNodeUID }));
        expect (emptyGraph.getGraphConnections().empty());

        juce::MemoryBlock emptyState;
        emptyGraph.getStateInformation (emptyState);

        GraphAudioProcessor restoredEmptyGraph;
        restoredEmptyGraph.setStateInformation (emptyState.getData(), static_cast<int> (emptyState.getSize()));
        expect (restoredEmptyGraph.getGraphConnections().empty(),
                "State loading recreated Input -> Output for an intentionally empty graph");

        auto legacyState = decodeProcessorState (emptyState);
        const auto legacyConnections = legacyState.getChildWithName ("Connections");
        legacyState.removeChild (legacyConnections, nullptr);
        const auto legacyStateData = encodeProcessorState (legacyState);

        GraphAudioProcessor restoredLegacyGraph;
        restoredLegacyGraph.setStateInformation (legacyStateData.getData(),
                                                  static_cast<int> (legacyStateData.getSize()));
        expect (containsConnection (restoredLegacyGraph.getGraphConnections(),
                                    inputNodeUID,
                                    outputNodeUID),
                "A legacy state without Connections lost the safe default route");

        beginTest ("Graph state follows the host prepare and release lifecycle");

        GraphAudioProcessor lifecycleStateSource;
        GraphNodeUID lifecycleDriveNode = 0;
        expect (lifecycleStateSource.addModuleNode ("drive", { 300.0f, 200.0f }, lifecycleDriveNode));

        juce::MemoryBlock lifecycleState;
        lifecycleStateSource.getStateInformation (lifecycleState);

        GraphAudioProcessor lifecycleGraph;
        lifecycleGraph.setStateInformation (lifecycleState.getData(),
                                             static_cast<int> (lifecycleState.getSize()));

        auto lifecycleDrive = lifecycleGraph.getModuleProcessorForNode (lifecycleDriveNode);
        expect (static_cast<bool> (lifecycleDrive));

        if (lifecycleDrive)
        {
            expectWithinAbsoluteError (lifecycleDrive->getSampleRate(), 0.0, 1.0e-12,
                                       "State restore prepared a graph before the host did");
            expectEquals (lifecycleDrive->getBlockSize(), 0);
        }

        lifecycleGraph.suspendProcessing (true);
        lifecycleGraph.setStateInformation (lifecycleState.getData(),
                                             static_cast<int> (lifecycleState.getSize()));
        expect (lifecycleGraph.isSuspended(), "State restore changed the host's suspension state");
        auto replacementLifecycleDrive = lifecycleGraph.getModuleProcessorForNode (lifecycleDriveNode);
        expect (static_cast<bool> (lifecycleDrive),
                "Replacing the runtime graph invalidated an existing module handle");
        expect (static_cast<bool> (replacementLifecycleDrive));
        expect (replacementLifecycleDrive.get() != lifecycleDrive.get(),
                "State restore did not replace the runtime processor");
        lifecycleDrive = replacementLifecycleDrive;
        lifecycleGraph.suspendProcessing (false);

        lifecycleGraph.prepareToPlay (testSampleRate, 128);
        lifecycleGraph.setStateInformation (lifecycleState.getData(),
                                             static_cast<int> (lifecycleState.getSize()));
        lifecycleDrive = lifecycleGraph.getModuleProcessorForNode (lifecycleDriveNode);

        if (lifecycleDrive)
        {
            expectWithinAbsoluteError (lifecycleDrive->getSampleRate(), testSampleRate, 1.0e-12);
            expectEquals (lifecycleDrive->getBlockSize(), 128);
        }

        juce::AudioBuffer<float> passThroughBuffer (2, 4);
        passThroughBuffer.setSample (0, 0, 1.25f);
        passThroughBuffer.setSample (0, 1, -1.40f);
        passThroughBuffer.setSample (0, 2, 0.50f);
        passThroughBuffer.setSample (0, 3, -0.25f);
        passThroughBuffer.copyFrom (1, 0, passThroughBuffer, 0, 0, 4);
        juce::MidiBuffer graphMidi;
        lifecycleGraph.processBlock (passThroughBuffer, graphMidi);
        expectWithinAbsoluteError (passThroughBuffer.getSample (0, 0), 1.25f, 1.0e-6f,
                                   "The removed global limiter still changes valid samples");
        expectWithinAbsoluteError (passThroughBuffer.getSample (0, 1), -1.40f, 1.0e-6f,
                                   "The removed global limiter still changes valid samples");

        lifecycleGraph.releaseResources();
        lifecycleGraph.releaseResources();
        lifecycleGraph.setStateInformation (lifecycleState.getData(),
                                             static_cast<int> (lifecycleState.getSize()));
        lifecycleDrive = lifecycleGraph.getModuleProcessorForNode (lifecycleDriveNode);

        if (lifecycleDrive)
        {
            expectWithinAbsoluteError (lifecycleDrive->getSampleRate(), 0.0, 1.0e-12,
                                       "State restore re-prepared a graph after releaseResources");
            expectEquals (lifecycleDrive->getBlockSize(), 0);
        }

        lifecycleGraph.prepareToPlay (44100.0, 64);
        lifecycleGraph.releaseResources();
    }
};

class NAMAudioSmokeTests final : public juce::UnitTest
{
public:
    NAMAudioSmokeTests()
        : UnitTest ("NAM audio smoke", "BetterArchetype")
    {
    }

    void runTest() override
    {
        const auto modelFile = getTestAsset ("JP2CCAP2.nam");
        const auto input = loadMonoSamples (getTestAsset ("nam_di.wav"), testSampleCount);

        beginTest ("NAM rejects unsupported rates and unsafe model documents");

        NAMModelLoader unsupportedRateLoader;
        expect (! unsupportedRateLoader.prepare (44100.0, 64));
        expect (unsupportedRateLoader.getLastError().contains ("48 kHz"));
        expect (! unsupportedRateLoader.loadNAMFile (modelFile));

        NAMProcessor unsupportedRateProcessor;
        unsupportedRateProcessor.prepareToPlay (96000.0, 64);
        expect (unsupportedRateProcessor.getStatusText().contains ("48 kHz"));

        juce::TemporaryFile convNetFile (".nam");
        expect (convNetFile.getFile().replaceWithText (
            R"({"version":"0.7.0","architecture":"ConvNet","config":{},"weights":[],"sample_rate":48000})"));
        NAMModelLoader rejectedModelLoader;
        expect (rejectedModelLoader.prepare (testSampleRate, 64));
        expect (! rejectedModelLoader.loadNAMFile (convNetFile.getFile()));
        expect (rejectedModelLoader.getLastError().containsIgnoreCase ("realtime"));

        NAMProcessor rejectedStateProcessor;
        rejectedStateProcessor.prepareToPlay (testSampleRate, 64);
        rejectedStateProcessor.loadNAMFileAsync (convNetFile.getFile());
        expect (! waitForModuleLoad (rejectedStateProcessor));
        expectEquals (rejectedStateProcessor.createModuleState().getProperty ("modelPath").toString(),
                      convNetFile.getFile().getFullPathName(),
                      "A failed NAM load discarded the selected state path");

        juce::TemporaryFile invalidGroupsFile (".nam");
        expect (invalidGroupsFile.getFile().replaceWithText (
            R"({"version":"0.7.0","architecture":"WaveNet","config":{"groups":0},"weights":[],"sample_rate":48000})"));
        expect (! rejectedModelLoader.loadNAMFile (invalidGroupsFile.getFile()));
        expect (rejectedModelLoader.getLastError().containsIgnoreCase ("group"));

        juce::TemporaryFile wrongRateFile (".nam");
        expect (wrongRateFile.getFile().replaceWithText (
            R"({"version":"0.7.0","architecture":"Linear","config":{},"weights":[],"sample_rate":96000})"));
        expect (! rejectedModelLoader.loadNAMFile (wrongRateFile.getFile()));
        expect (rejectedModelLoader.getLastError().contains ("48 kHz"));

        juce::TemporaryFile malformedModelFile (".nam");
        expect (malformedModelFile.getFile().replaceWithText ("{}"));
        expect (! rejectedModelLoader.loadNAMFile (malformedModelFile.getFile()));

        beginTest ("Model changes audio and keeps samples finite");
        expectEquals (static_cast<int> (input.size()), testSampleCount);

        if (input.size() != testSampleCount)
            return;

        NAMModelLoader loader;
        expect (loader.prepare (testSampleRate, maximumModelBlockSize), loader.getLastError());
        expect (loader.loadNAMFile (modelFile), loader.getLastError());

        std::vector<float> output64;
        expect (processInBlocks (loader, input, output64, 64));
        expect (containsOnlyFiniteSamples (output64));
        expect (calculateRms (output64) > 1.0e-6);
        expect (calculateDifferenceRms (input, output64) > 1.0e-5);

        const auto namFingerprint = calculateFingerprint (output64);
        expectFingerprint (*this,
                           "NAM",
                           namFingerprint,
                           expectedNamFingerprint,
                           fingerprintRelativeTolerance);

        beginTest ("Output is independent of safe host block partitioning");

        expect (loader.prepare (testSampleRate, maximumModelBlockSize), loader.getLastError());

        std::vector<float> output257;
        expect (processInBlocks (loader, input, output257, 257));
        expect (containsOnlyFiniteSamples (output257));
        expect (calculateDifferenceRms (output64, output257) < 1.0e-5,
                "NAM output changed with block partitioning");

        beginTest ("A host block may exceed the prepared NAM block size");

        const std::vector<float> oversizedInput (input.begin(), input.begin() + 257);
        expect (loader.prepare (testSampleRate, 64), loader.getLastError());

        std::vector<float> chunkedReference;
        expect (processInBlocks (loader, oversizedInput, chunkedReference, 64));

        expect (loader.prepare (testSampleRate, 64), loader.getLastError());
        std::vector<float> oversizedOutput (oversizedInput.size());
        expect (loader.processMonoBlock (oversizedInput.data(),
                                         oversizedOutput.data(),
                                         static_cast<int> (oversizedInput.size())));
        expect (containsOnlyFiniteSamples (oversizedOutput));
        expect (calculateDifferenceRms (chunkedReference, oversizedOutput) < 1.0e-6,
                "Large NAM blocks were not processed like safe internal chunks");

        beginTest ("NAM processor chunks oversized blocks and averages stereo input");

        NAMProcessor processor;
        processor.prepareToPlay (testSampleRate, 64);
        processor.loadNAMFileAsync (modelFile);

        const auto pendingModelState = processor.createModuleState();
        expectEquals (pendingModelState.getProperty ("modelPath").toString(),
                      modelFile.getFullPathName(),
                      "Saving during NAM loading lost the selected file");

        const auto processorLoaded = waitForModuleLoad (processor);
        expect (processorLoaded, processor.getStatusText());

        if (processorLoaded)
        {
            auto hotInput = oversizedInput;
            const auto inputPeak = std::max_element (hotInput.begin(), hotInput.end(), [] (float a, float b)
            {
                return std::abs (a) < std::abs (b);
            });
            const auto peakMagnitude = inputPeak != hotInput.end() ? std::abs (*inputPeak) : 0.0f;
            const auto hotScale = peakMagnitude > 0.0f ? 2.5f / peakMagnitude : 1.0f;

            for (auto& sample : hotInput)
                sample *= hotScale;

            expect (loader.prepare (testSampleRate, 64), loader.getLastError());
            std::vector<float> hotReference (hotInput.size());
            expect (loader.processMonoBlock (hotInput.data(),
                                             hotReference.data(),
                                             static_cast<int> (hotInput.size())));

            const auto renderProcessor = [&] (const std::vector<float>& leftInput,
                                              const std::vector<float>& rightInput)
            {
                processor.prepareToPlay (testSampleRate, 64);
                juce::AudioBuffer<float> processorBuffer (2, static_cast<int> (leftInput.size()));
                processorBuffer.copyFrom (0, 0, leftInput.data(), static_cast<int> (leftInput.size()));
                processorBuffer.copyFrom (1, 0, rightInput.data(), static_cast<int> (rightInput.size()));

                juce::MidiBuffer midi;
                processor.processBlock (processorBuffer, midi);

                std::vector<float> left (hotInput.size());
                std::vector<float> right (hotInput.size());
                std::copy_n (processorBuffer.getReadPointer (0), left.size(), left.begin());
                std::copy_n (processorBuffer.getReadPointer (1), right.size(), right.begin());
                expect (containsOnlyFiniteSamples (left));
                expect (calculateDifferenceRms (left, right) < 1.0e-7,
                        "NAM mono output was not broadcast to both channels");
                return left;
            };

            const auto dualMonoOutput = renderProcessor (hotInput, hotInput);
            expect (calculateDifferenceRms (hotReference, dualMonoOutput) < 1.0e-6,
                    "Dual-mono input did not match the mono NAM reference");

            std::vector<float> silence (hotInput.size(), 0.0f);
            auto halfInput = hotInput;

            for (auto& sample : halfInput)
                sample *= 0.5f;

            expect (loader.prepare (testSampleRate, 64), loader.getLastError());
            std::vector<float> halfInputReference (halfInput.size());
            expect (loader.processMonoBlock (halfInput.data(),
                                             halfInputReference.data(),
                                             static_cast<int> (halfInput.size())));

            const auto leftOnlyOutput = renderProcessor (hotInput, silence);
            expect (calculateDifferenceRms (halfInputReference, leftOnlyOutput) < 1.0e-6,
                    "Stereo-to-mono input was not averaged like Gateway");

            const auto rightOnlyOutput = renderProcessor (silence, hotInput);
            expect (calculateDifferenceRms (leftOnlyOutput, rightOnlyOutput) < 1.0e-6,
                    "Left-only and right-only input used different mono policies");
        }
    }
};

CoreStructureTests coreStructureTests;
NAMAudioSmokeTests namAudioSmokeTests;
} // namespace
} // namespace better::tests
