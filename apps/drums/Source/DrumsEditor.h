#pragma once

#include "BreakView.h"
#include "ImportView.h"
#include "CeramicSkin.h"
#include "DrumsProcessor.h"
#include "KeyMapEditor.h"
#include "LayerUi.h"
#include "ModulationUi.h"
#include "OspLookAndFeel.h"
#include "PadGrid.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace osp::drumsplugin
{

using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

/** A knob with its label above and value below, in OSP's style (vector, or the rendered
    macro sprite with `rendered`). */
struct LabelledKnob
{
    juce::Slider slider;
    juce::Label label;
    std::unique_ptr<SliderAttachment> attachment;
    void setup (juce::Component& parent, const juce::String& text, juce::Colour arc, bool bipolar = false, bool rendered = false);
    void attach (juce::AudioProcessorValueTreeState& state, const juce::String& id);
    void setBounds (juce::Rectangle<int> r, int labelHeight = 18);
};

/** The selected pad's waveform (its selected layer): the sound in a dark display; drag
    the START and END markers. With one layer START is the pad's START parameter; with
    several, START and END belong to the layer shown. */
class WaveDisplay final : public juce::Component
{
public:
    explicit WaveDisplay (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> wave() const;
    float startX() const;
    float endX() const;
    const PadLayer* shownLayer() const;
    bool padStartMarker() const;
    DrumsProcessor& proc;
    enum class Drag { none, start, end } dragging = Drag::none;
    bool firstMove = true;
};

/** The selected pad: header (badge, name, < >, SAMPLE | KEY MAP tabs, menu), waveform,
    START / TUNE / REVERSE, its layer list, play mode, choke, one shot, VOLUME and PAN. */
class SamplePanel final : public juce::Component
{
public:
    explicit SamplePanel (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void bind();   ///< follow the selected pad
    std::function<void (int)> onSelect;
    std::function<void()> onLoad;
    std::function<void()> onOpenKeyMap;
    // Tests
    LayerList& layerList() noexcept { return layers; }
    VelocityRange& velocityRange() noexcept { return velocity; }
    juce::Slider& layerLevelSlider() noexcept { return layerLevel.slider; }

private:
    void showMenu();
    void openAddDialog();
    void bindLayer();   ///< the selected layer's LEVEL and VELOCITY
    juce::Rectangle<int> velocityCaption() const { return { 488, 270, 284, 22 }; }
    DrumsProcessor& proc;
    juce::Label name;
    void mouseDown (const juce::MouseEvent&) override;
    juce::Rectangle<int> keyMapTabArea() const { return { 488, 12, 84, 30 }; }
    ceramic::KeyButton previous { {}, 0.7f }, next { {}, 0.7f }, menu { {}, 0.7f }, addLayers { "Add Layers...", 0.8f };
    ceramic::KeyButton modeRobin { "ROBIN", 0.6f }, modeVelocity { "VELOCITY", 0.6f }, modeStack { "STACK", 0.6f };
    juce::ComboBox order;
    LayerList layers;
    LabelledKnob layerLevel;
    VelocityRange velocity;
    bool levelGesture = false;
    std::unique_ptr<juce::FileChooser> chooser;
    WaveDisplay wave;
    LabelledKnob start, tune, volume, pan;
    ceramic::PillSwitch reverse;
    std::unique_ptr<ButtonAttachment> reverseAttachment;
    juce::ComboBox voice, choke;
    ceramic::CheckKey oneShot { "One Shot" };
};

/** The always-visible KEYBOARD RANGE strip: every pad's note or zone on one keyboard. */
class KeyboardRangeStrip final : public juce::Component
{
public:
    explicit KeyboardRangeStrip (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();
    std::function<void (int)> onSelect;
    std::function<void()> onOpenKeyMap;
    std::function<void (int autoMapId)> onAutoMap;
    std::function<void()> onModeChanged;

private:
    juce::Rectangle<float> keys() const;
    /** A key's rectangle on the piano (white keys full height, black keys shorter on top). */
    juce::Rectangle<float> keyRect (int note) const;
    float noteX (double note) const;
    int noteAt (juce::Point<float>) const;
    DrumsProcessor& proc;
    ceramic::KeyButton padMode { "PAD", 0.6f }, expandedMode { "EXPANDED", 0.6f }, open { {}, 0.8f };
    juce::ComboBox root, presets;
    std::array<bool, 128> held {};
};

/** The selected pad's amplitude envelope (AMP), drawn from its own parameters. */
class EnvelopePanel final : public juce::Component
{
public:
    explicit EnvelopePanel (DrumsProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void bind();
    /** The processor changed (an undo, a preset): the modulation page follows. */
    void refresh();

    /** The card's tabs: AMP, then the pad's modulation sources. */
    enum Tab { amp, env, lfo, vel, ctrl, tabCount };
    void setTab (int tab);
    int currentTab() const noexcept { return tab; }
    juce::Rectangle<int> tabRect (int tab) const;
    PadModEditor& modEditor() noexcept { return mods; }
    void showPresetMenu();

private:
    DrumsProcessor& proc;
    LabelledKnob attack, decay, sustain, release;
    PadModEditor mods;
    ceramic::KeyButton presets { "PRESETS", 0.6f };
    int tab = amp;
};

/** BREAK in the top bar: click to choose a break, or drop one on it. */
class BreakButton final : public ceramic::KeyButton, public juce::FileDragAndDropTarget
{
public:
    using ceramic::KeyButton::KeyButton;
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (isAudioFileName (f))
                return true;
        return false;
    }
    /** One file: a break to slice. Several: an import into the kit. */
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        juce::Array<juce::File> list;
        for (const auto& f : files)
            if (isAudioFileName (f))
                list.add (juce::File (f));
        if (list.size() == 1 && onFile)
            onFile (list[0]);
        else if (list.size() > 1 && onFiles)
            onFiles (list);
    }
    std::function<void (const juce::File&)> onFile;
    std::function<void (const juce::Array<juce::File>&)> onFiles;
};

/** Everything inside the window, laid out once at the base size and scaled as a whole. */
class DrumsContent final : public juce::Component, public juce::DragAndDropContainer, private juce::Timer, private juce::ChangeListener
{
public:
    static constexpr int baseWidth = 1536;
    static constexpr int baseHeight = 1024;   ///< with one row of 8 pads; the grids add height
    /** Extra height the 2 x 4 and 3 x 3 grids need below the one-row layout. */
    static int extraHeight (DrumsProcessor::PadLayout layout) noexcept;
    int contentHeight() const noexcept { return baseHeight + extraHeight (proc.padLayout); }

    explicit DrumsContent (DrumsProcessor&);
    ~DrumsContent() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void selectPad (int pad);
    void setKeyMapOpen (bool open);
    bool isKeyMapOpen() const noexcept { return keyMap.isVisible(); }
    void setLayout (DrumsProcessor::PadLayout layout);
    PadGrid& padGrid() noexcept { return grid; }
    KeyMapEditor& keyMapEditor() noexcept { return keyMap; }
    SamplePanel& samplePanel() noexcept { return sample; }
    BreakView& breakView() noexcept { return breaks; }
    ImportView& importView() noexcept { return imports; }
    EnvelopePanel& envelopePanel() noexcept { return envelope; }
    void openBreakDialog();
    void syncBreakView();
    void openLoadDialog (int pad);
    /** Brings the mode switches, undo state and labels in line with the processor now. */
    void refreshModeBar();
    std::function<void()> onHeightChanged;

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    juce::Rectangle<int> padCard() const;
    juce::Rectangle<int> editorCard() const;
    juce::Rectangle<int> rangeCard() const;
    juce::Rectangle<int> bottomRow() const;

    DrumsProcessor& proc;
    PadGrid grid;
    ceramic::KeyButton undoButton { {}, 0.7f }, redoButton { {}, 0.7f }, kitMenu { {}, 0.7f };
    void showKitMenu();
    void saveKitAs();
    void tell (const juce::String& title, const juce::String& message);
    juce::String shownRelinkReport;
    ceramic::KeyButton layout1x8 { "1 x 8", 0.6f }, layout2x4 { "2 x 4", 0.6f }, layout3x3 { "3 x 3", 0.6f }, ninthPad { "PAD 9", 0.6f };
    juce::Label kitName;
    ceramic::Fader volume;
    ceramic::Meter meter;
    std::unique_ptr<SliderAttachment> volumeAttachment;
    SamplePanel sample;
    KeyboardRangeStrip range;
    EnvelopePanel envelope;
    std::array<LabelledKnob, 4> effects;   // DRIVE, MOVEMENT, ECHO, SPACE
    std::array<ceramic::KeyButton, 4> effectPower;
    std::array<float, 4> effectMemory { 0.3f, 0.3f, 0.3f, 0.3f };
    std::array<LabelledKnob, 3> macros;    // LIFE, DYNAMICS, CHARACTER
    ceramic::Fader machine;                // MACHINE (M9, experimental), in GLOBAL's header
    std::unique_ptr<SliderAttachment> machineAttachment;
    KeyMapEditor keyMap;
    BreakView breaks;
    ImportView imports;
    BreakButton breakButton { "BREAK", 0.7f };
    std::unique_ptr<juce::FileChooser> chooser;
    bool lastExpanded = false;
};

/**
    ANDOR Drums' editor (V1.2): the Warm Ceramic + Teak look of ANDOR/OSP and the mockup's
    layout - pads on top (one row of 8, or 2 x 4 / 3 x 3), the selected pad's sound, the
    keyboard range, then AMP, the kit's effects and its global macros. KEY MAP opens over
    the sound and the keyboard range. Resizable with a fixed aspect: the content is laid out
    once and scaled, so every proportion (and every pad) stays exactly equal at any size.
*/
class DrumsEditor final : public juce::AudioProcessorEditor
{
public:
    explicit DrumsEditor (DrumsProcessor&);
    ~DrumsEditor() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    DrumsContent& content() noexcept { return body; }
    /** Applies the content's height (the layout) at the current scale. */
    void updateSizeForContent();

    static constexpr double minScale = 0.6, defaultScale = 0.75, maxScale = 1.25;

private:
    plugin::OspLookAndFeel lookAndFeel;
    DrumsContent body;
    double scale = defaultScale;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumsEditor)
};

} // namespace osp::drumsplugin
