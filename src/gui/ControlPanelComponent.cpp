#include "gui/ControlPanelComponent.h"

namespace better
{
namespace
{
const auto panelColour = juce::Colour (0xff171a1f);
const auto borderColour = juce::Colour (0xff303640);
const auto textColour = juce::Colour (0xffeceff4);
const auto mutedTextColour = juce::Colour (0xffaeb6c1);
const auto buttonColour = juce::Colour (0xff252b33);
const auto accentColour = juce::Colour (0xffd95840);
} // namespace

ControlPanelComponent::ControlPanelComponent()
{
    titleLabel.setText ("Selection", juce::dontSendNotification);
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setColour (juce::Label::textColourId, textColour);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
    addAndMakeVisible (titleLabel);

    statusLabel.setJustificationType (juce::Justification::centredLeft);
    statusLabel.setColour (juce::Label::textColourId, mutedTextColour);
    statusLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    addAndMakeVisible (statusLabel);

    for (auto* button : { &deleteButton, &loadNAMButton, &loadIRButton })
    {
        button->setColour (juce::TextButton::buttonColourId, buttonColour);
        button->setColour (juce::TextButton::buttonOnColourId, accentColour);
        button->setColour (juce::TextButton::textColourOffId, textColour);
        addAndMakeVisible (*button);
    }

    deleteButton.onClick = [this]
    {
        if (onDeleteRequested)
            onDeleteRequested();
    };

    loadNAMButton.onClick = [this]
    {
        if (onLoadNAMRequested)
            onLoadNAMRequested();
    };

    loadIRButton.onClick = [this]
    {
        if (onLoadIRRequested)
            onLoadIRRequested();
    };

    setSelection ({}, nullptr);
}

void ControlPanelComponent::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);
    g.setColour (borderColour);
    g.drawRect (getLocalBounds());
}

void ControlPanelComponent::resized()
{
    auto area = getLocalBounds().reduced (16);
    titleLabel.setBounds (area.removeFromTop (30));
    statusLabel.setBounds (area.removeFromTop (42));
    area.removeFromTop (8);

    auto buttonRow = area.removeFromTop (34);
    const auto buttonWidth = juce::jmax (74, buttonRow.getWidth() / 3 - 6);
    deleteButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
    buttonRow.removeFromLeft (7);
    loadNAMButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
    buttonRow.removeFromLeft (7);
    loadIRButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
    area.removeFromTop (18);

    for (auto* toggle : toggles)
    {
        toggle->setBounds (area.removeFromTop (30));
        area.removeFromTop (8);
    }

    for (int i = 0; i < sliders.size(); ++i)
    {
        labels[i]->setBounds (area.removeFromTop (20));
        sliders[i]->setBounds (area.removeFromTop (38));
        area.removeFromTop (10);
    }
}

void ControlPanelComponent::setSelection (GraphNodeDescription node, AudioModuleProcessor* processor)
{
    selectedNode = std::move (node);
    clearControls();

    const auto title = selectedNode.uid == 0 ? "Selection" : selectedNode.name;
    titleLabel.setText (title, juce::dontSendNotification);

    deleteButton.setVisible (selectedNode.role == GraphNodeRole::module);
    loadNAMButton.setVisible (processor != nullptr && processor->getDescriptor().canLoadNAM);
    loadIRButton.setVisible (processor != nullptr && processor->getDescriptor().canLoadIR);

    if (processor != nullptr)
    {
        addControlsFor (*processor);
        setStatusText (processor->getStatusText());
    }
    else
    {
        setStatusText (selectedNode.uid == 0 ? "" : "Graph endpoint");
    }

    resized();
    repaint();
}

void ControlPanelComponent::setStatusText (const juce::String& text)
{
    statusLabel.setText (text, juce::dontSendNotification);
}

void ControlPanelComponent::clearControls()
{
    sliderAttachments.clear();
    buttonAttachments.clear();
    labels.clear();
    sliders.clear();
    toggles.clear();
}

void ControlPanelComponent::addControlsFor (AudioModuleProcessor& processor)
{
    for (const auto& control : processor.getDescriptor().controls)
    {
        if (control.kind == ModuleControlKind::toggle)
        {
            auto* toggle = toggles.add (new juce::ToggleButton (control.name));
            toggle->setColour (juce::ToggleButton::textColourId, textColour);
            addAndMakeVisible (toggle);
            buttonAttachments.push_back (std::make_unique<ButtonAttachment> (processor.getParameters(),
                                                                             control.parameterId,
                                                                             *toggle));
            continue;
        }

        auto* label = labels.add (new juce::Label());
        label->setText (control.name, juce::dontSendNotification);
        label->setColour (juce::Label::textColourId, mutedTextColour);
        label->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (label);

        auto* slider = sliders.add (new juce::Slider());
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 24);
        slider->setNumDecimalPlacesToDisplay (control.decimalPlaces);
        slider->setColour (juce::Slider::trackColourId, accentColour);
        slider->setColour (juce::Slider::thumbColourId, textColour);
        slider->setColour (juce::Slider::textBoxTextColourId, textColour);
        slider->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff101216));
        addAndMakeVisible (slider);

        sliderAttachments.push_back (std::make_unique<SliderAttachment> (processor.getParameters(),
                                                                         control.parameterId,
                                                                         *slider));
    }
}
} // namespace better
