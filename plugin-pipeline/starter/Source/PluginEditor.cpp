#include "PluginEditor.h"

PluginEditor::PluginEditor (PluginProcessor& p)
    : AudioProcessorEditor (p),
      gainAttachment (p.parameters, "gain", gain)
{
    gain.setTextValueSuffix (" dB");
    addAndMakeVisible (gain);
    setSize (320, 240);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1d1f24));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (20.0f));
    g.drawText (JucePlugin_Name, getLocalBounds().removeFromTop (48), juce::Justification::centred);
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("GAIN", getLocalBounds().removeFromBottom (28), juce::Justification::centred);
}

void PluginEditor::resized()
{
    gain.setBounds (getLocalBounds().reduced (60, 48));
}
