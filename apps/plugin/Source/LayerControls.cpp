#include "LayerControls.h"

namespace osp::plugin
{

//==============================================================================
juce::Rectangle<float> LayerTabs::tab (int layer) const
{
    return { static_cast<float> (layer) * 22.0f, 0.0f, 20.0f, static_cast<float> (getHeight()) };
}

void LayerTabs::setSelected (int layer)
{
    if (current != layer)
    {
        current = layer;
        repaint();
    }
}

void LayerTabs::setLoaded (int layer, bool isLoaded)
{
    if (loaded[static_cast<std::size_t> (layer & 1)] != isLoaded)
    {
        loaded[static_cast<std::size_t> (layer & 1)] = isLoaded;
        repaint();
    }
}

void LayerTabs::mouseUp (const juce::MouseEvent& e)
{
    for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
        if (tab (layer).contains (e.position))
        {
            setSelected (layer);
            if (onSelect != nullptr)
                onSelect (layer);
        }
}

void LayerTabs::paint (juce::Graphics& g)
{
    const auto mouse = getMouseXYRelative().toFloat();
    g.setFont (fonts::make (11.0f, fonts::Weight::semibold));
    for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
    {
        const auto r = tab (layer);
        const bool active = layer == current;
        const bool hover = isMouseOver() && r.contains (mouse);
        if (active)
        {
            g.setColour (palette::housing.withAlpha (0.92f));
            g.fillRoundedRectangle (r, 3.0f);
        }
        else
        {
            g.setColour (palette::displayLine.withAlpha (hover ? 1.0f : 0.6f));
            g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
        }
        g.setColour (active ? palette::display : palette::displayText.withAlpha (hover ? 1.0f : 0.8f));
        g.drawText (OspAudioProcessor::layerName (layer), r.withTrimmedBottom (1.0f), juce::Justification::centred, false);
        if (loaded[static_cast<std::size_t> (layer)])
        {
            g.setColour (active ? palette::accent : palette::displayText);
            g.fillEllipse (r.getCentreX() - 1.0f, r.getBottom() - 3.5f, 2.0f, 2.0f);
        }
    }
}

//==============================================================================
BlendControl::BlendControl (juce::AudioProcessorValueTreeState& state)
{
    slider.getProperties().set ("blend", true);
    slider.setTitle ("A/B blend");
    slider.setTooltip ("Blend between layer A and layer B");
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "ab.blend", slider);
    if (auto* p = state.getParameter ("ab.blend"))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    addAndMakeVisible (slider);
}

void BlendControl::resized()
{
    slider.setBounds (getLocalBounds().reduced (16, 0));
}

void BlendControl::paint (juce::Graphics& g)
{
    g.setFont (fonts::make (11.0f, fonts::Weight::semibold));
    g.setColour (palette::displayText);
    const auto r = getLocalBounds();
    g.drawText ("A", r.withWidth (14), juce::Justification::centredLeft, false);
    g.drawText ("B", r.withTrimmedLeft (r.getWidth() - 14), juce::Justification::centredRight, false);
}

//==============================================================================
SourceModeSwitch::SourceModeSwitch (juce::RangedAudioParameter& parameter)
    : attachment (parameter, [this] (float value) {
          current = value >= 0.5f ? 1 : 0;
          repaint();
          if (onChange != nullptr)
              onChange (current);
      })
{
    attachment.sendInitialUpdate();
    setTitle ("Source mode");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

juce::Rectangle<float> SourceModeSwitch::option (int index) const
{
    const auto half = static_cast<float> (getWidth()) * 0.5f;
    return { index == 0 ? 0.0f : half, 0.0f, half, static_cast<float> (getHeight()) };
}

void SourceModeSwitch::mouseUp (const juce::MouseEvent& e)
{
    for (int i = 0; i < 2; ++i)
        if (option (i).contains (e.position) && i != current)
            attachment.setValueAsCompleteGesture (static_cast<float> (i));
}

void SourceModeSwitch::paint (juce::Graphics& g)
{
    g.setFont (fonts::label (9.5f));
    const char* names[] = { "ONE SHOT", "GRANULAR" };
    for (int i = 0; i < 2; ++i)
    {
        auto r = option (i).reduced (2.0f, 0.0f);
        const bool active = i == current;
        const auto dot = r.removeFromLeft (10.0f).withSizeKeepingCentre (6.0f, 6.0f);
        if (active)
        {
            g.setColour (palette::accent);
            g.fillEllipse (dot);
        }
        else
        {
            g.setColour (palette::displayText.withAlpha (0.8f));
            g.drawEllipse (dot.reduced (0.5f), 1.0f);
        }
        g.setColour (active ? palette::housing : palette::displayText);
        g.drawText (names[i], r.withTrimmedLeft (3.0f), juce::Justification::centredLeft, false);
    }
}

//==============================================================================
GranularOverlay::GranularOverlay (juce::AudioProcessorValueTreeState& state, int layer)
{
    const auto id = [layer] (const char* name) { return OspAudioProcessor::layerParameterId (layer, name); };
    const std::array<std::pair<const char*, const char*>, 5> controls { {
        { "granular.position", "POS" }, { "granular.size", "SIZE" }, { "granular.density", "DENS" },
        { "granular.tune", "TUNE" }, { "granular.spread", "SPREAD" } } };
    const std::array<MiniKnob::Formatter, 5> formats {
        [] (double v) { return format::percent (v); },
        [] (double v) { return format::milliseconds (v); },
        [] (double v) { return juce::String (v, v < 10.0 ? 1 : 0) + "/s"; },
        [] (double v) {
            const bool whole = std::abs (v - std::round (v)) < 0.05;
            return (v > 0.0 ? "+" : "") + juce::String (v, whole ? 0 : 1) + " st";
        },
        [] (double v) { return format::percent (v); },
    };
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i] = std::make_unique<MiniKnob> (state, id (controls[i].first), controls[i].second, formats[i]);
        knobs[i]->setOnDark (true);
        addAndMakeVisible (*knobs[i]);
    }
    setTitle ("Granular");
}

void GranularOverlay::resized()
{
    auto r = getLocalBounds().reduced (6, 3);
    const int cell = r.getWidth() / static_cast<int> (knobs.size());
    for (auto& k : knobs)
        k->setBounds (r.removeFromLeft (cell));
}

void GranularOverlay::paint (juce::Graphics& g)
{
    // A quiet backdrop: the waveform shows through.
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (palette::display.withAlpha (0.62f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (palette::displayLine.withAlpha (0.9f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

} // namespace osp::plugin
