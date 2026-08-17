#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

namespace
{
class ConsoleLogger final : public juce::Logger
{
    void logMessage (const juce::String& message) override
    {
        std::cout << message << std::endl;
    }
};
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    ConsoleLogger logger;
    juce::Logger::setCurrentLogger (&logger);

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);
    runner.runTestsInCategory ("BetterArchetype", 0xBA2026);

    const auto resultCount = runner.getNumResults();

    if (resultCount == 0)
    {
        juce::Logger::writeToLog ("No BetterArchetype tests were registered.");
        juce::Logger::setCurrentLogger (nullptr);
        return 1;
    }

    auto failures = 0;

    for (int index = 0; index < resultCount; ++index)
        failures += runner.getResult (index)->failures;

    juce::Logger::writeToLog (failures == 0
                                  ? "BetterArchetype tests passed."
                                  : juce::String (failures) + " BetterArchetype test assertion(s) failed.");
    juce::Logger::setCurrentLogger (nullptr);
    return failures == 0 ? 0 : 1;
}
