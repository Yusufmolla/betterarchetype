#include "gui/ModuleBrowserComponent.h"

#include "audio/graph/ModuleRegistry.h"

namespace better
{
namespace
{
const auto panelColour = juce::Colour (0xff171a1f);
const auto borderColour = juce::Colour (0xff303640);
const auto textColour = juce::Colour (0xffeceff4);
const auto buttonColour = juce::Colour (0xff252b33);
const auto accentColour = juce::Colour (0xffd95840);
} // namespace

ModuleBrowserComponent::ModuleBrowserComponent()
{
    titleLabel.setText ("Modules", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, textColour);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    rebuildButtons();
}

void ModuleBrowserComponent::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);
    g.setColour (borderColour);
    g.drawRect (getLocalBounds());
}

void ModuleBrowserComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleLabel.setBounds (area.removeFromTop (30));
    area.removeFromTop (10);

    for (auto* button : moduleButtons)
    {
        button->setBounds (area.removeFromTop (42));
        area.removeFromTop (8);
    }
}

void ModuleBrowserComponent::rebuildButtons()
{
    moduleButtons.clear();

    for (const auto& module : ModuleRegistry::getModules())
    {
        auto* button = moduleButtons.add (new juce::TextButton (module.name));
        button->setButtonText (module.category + "  " + module.name);
        button->setColour (juce::TextButton::buttonColourId, buttonColour);
        button->setColour (juce::TextButton::buttonOnColourId, accentColour);
        button->setColour (juce::TextButton::textColourOffId, textColour);
        button->onClick = [this, moduleId = module.moduleId]
        {
            if (onModuleRequested)
                onModuleRequested (moduleId);
        };
        addAndMakeVisible (button);
    }
}
} // namespace better
