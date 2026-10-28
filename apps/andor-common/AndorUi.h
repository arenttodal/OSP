#pragma once

#include "Design.h"
#include "OspLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace osp::andorlab
{

namespace design = osp::plugin::design;

/**
    The ANDOR Lab instruments' controls, drawn by ANDOR/OSP's own design system (Design.h,
    OspLookAndFeel): the ivory housing, raised panels, graphite displays, Inter type and the
    approved rendered ivory knob. No new visual language: the same family, recomposed.
*/
namespace ui
{
    using design::colour::text;
    /** A UTF-8 literal ("·", "▾") as a juce::String (the const char* constructor reads ASCII only). */
    inline juce::String u8 (const char* utf8) { return juce::String::fromUTF8 (utf8); }
    juce::Font label (float height = 12.5f);
    juce::Font value (float height = 13.5f);
    juce::Font title (float height = 15.0f);
    juce::Font logo (float height = 30.0f);

    /** A raised panel with an optional small title along its top. */
    void panel (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title = {});
    /** A graphite display well. */
    void well (juce::Graphics&, juce::Rectangle<float> r);
    /** "ANDOR" in the display face and the product name beside it. */
    void logoBlock (juce::Graphics&, juce::Rectangle<float> r, const juce::String& product, const juce::String& subtitle);
}

/** A labelled knob bound to a parameter: name above, value below, the instrument's knob. */
class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& name, bool rendered = false,
          juce::Colour arc = design::colour::accent);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider& slider() noexcept { return dial; }

private:
    juce::Slider dial;
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

/** A row of labelled keys choosing one value of a choice parameter. */
class Segmented : public juce::Component
{
public:
    Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, juce::StringArray labels);
    ~Segmented() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int selected() const noexcept { return index; }

private:
    juce::StringArray labels;
    int index = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* parameter = nullptr;
};

/** A choice parameter as a compact drop-down. */
class Choice : public juce::Component
{
public:
    Choice (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& name);
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::ComboBox& box() noexcept { return combo; }

private:
    juce::ComboBox combo;
    juce::String name;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
};

/** A small raised text key with a callback. */
class Key : public juce::Component
{
public:
    explicit Key (juce::String text, std::function<void()> onClick = {});
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void setText (const juce::String& t) { text = t; repaint(); }
    void setActive (bool a) { active = a; repaint(); }
    std::function<void()> onClick;

private:
    juce::String text;
    bool active = false;
};

//==============================================================================
/**
    The graphite waveform display: an overview (min/max per column) on a time grid, with frame
    <-> x mapping over a visible range (zoomable). Subclasses draw their overlays.
*/
class WaveformView : public juce::Component
{
public:
    void setOverview (std::vector<float> mins, std::vector<float> maxs, std::int64_t frames, double sampleRate);
    void setVisibleRange (double startFrame, double endFrame);
    void resetZoom();
    double frameAt (float x) const noexcept;
    float xOf (double frame) const noexcept;
    std::int64_t totalFrames() const noexcept { return frames; }
    double rate() const noexcept { return sampleRate; }
    bool hasAudio() const noexcept { return frames > 0; }

    void paint (juce::Graphics&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    /** Shown in the empty display. */
    juce::String emptyText { "Drop a recording here" };

protected:
    virtual void paintOverlay (juce::Graphics&, juce::Rectangle<float> /*area*/) {}
    juce::Rectangle<float> plotArea() const noexcept { return getLocalBounds().toFloat().reduced (10.0f, 12.0f); }

private:
    std::vector<float> mins, maxs;
    std::int64_t frames = 0;
    double sampleRate = 48000.0;
    double viewStart = 0.0, viewEnd = 1.0;
};

//==============================================================================
/**
    A keyboard strip showing each key's state (colour + small label from a callback) and the
    keys sounding now. Clicking a key auditions it through `onNote` (true = down).
*/
class KeyboardMap : public juce::Component
{
public:
    struct KeyLook
    {
        juce::Colour fill = juce::Colours::transparentBlack;   ///< transparent: the key's own colour
        juce::String badge;                                    ///< e.g. "3" variations
        bool hatched = false;                                  ///< unplayable
        bool strong = false;                                   ///< a recorded (exact) key
    };
    std::function<KeyLook (int key)> lookOf;
    std::function<void (int key, bool down)> onNote;
    std::function<void (int key)> onSelect;

    void setRange (int lowKey, int highKey);
    int lowKey() const noexcept { return low; }
    int highKey() const noexcept { return high; }
    void setSounding (const std::array<std::uint32_t, 4>& mask);
    void setSelected (int key) { selected = key; repaint(); }
    juce::Rectangle<float> keyBounds (int key) const;
    int keyAt (juce::Point<float> p) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    static bool isBlack (int key) noexcept { const int k = key % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
    int whiteIndex (int key) const noexcept;
    int numWhite() const noexcept;
    int low = 36, high = 96;
    int pressed = -1, selected = -1;
    std::array<std::uint32_t, 4> sounding {};
};

/** The editor's uniform scaling: lay out once at the design size, scale to the window. */
class ScaledContent : public juce::Component
{
public:
    ScaledContent (juce::Component& content, int designWidth, int designHeight);
    void resized() override;

private:
    juce::Component& inner;
    int w, h;
};

} // namespace osp::andorlab
