#pragma once

#include "AndorUi.h"
#include "GrainProcessor.h"

namespace osp::andorlab
{

/** The source display: the recording, the atlas regions, POSITION and SCATTER, the live grain cloud. */
class GrainWaveform final : public WaveformView
{
public:
    explicit GrainWaveform (GrainProcessor&);
    void refresh();
    struct Dot
    {
        float position, level;
        int voice;
    };
    void setGrains (std::vector<Dot> dots, std::vector<std::pair<float, float>> regions);

private:
    void paintOverlay (juce::Graphics&, juce::Rectangle<float> area) override;
    GrainProcessor& processor;
    std::vector<Dot> grains;
    std::vector<std::pair<float, float>> activeRegions;
};

/**
    KEY DNA: the selected parameter's curve over the keyboard (click adds a point, drag moves it,
    double-click removes it, drag on empty space draws), the per-key individuality as bars, the
    played keys' effective values as dots, and the keys themselves for audition.
*/
class KeyDnaEditor final : public juce::Component
{
public:
    explicit KeyDnaEditor (GrainProcessor&);
    void setParam (grain::DnaParam p);
    grain::DnaParam param() const noexcept { return current; }
    void setVoices (std::vector<std::pair<int, std::array<float, grain::numDnaParams>>> v);
    void setRange (int lowKey, int highKey);
    void refresh() { repaint(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    KeyboardMap keys;

private:
    juce::Rectangle<float> plot() const;
    float xOfKey (float key) const;
    float keyOfX (float x) const;
    float yOf (float value) const;
    float valueOfY (float y) const;
    int pointAt (juce::Point<float>) const;

    GrainProcessor& processor;
    grain::DnaParam current = grain::DnaParam::size;
    std::vector<std::pair<int, std::array<float, grain::numDnaParams>>> voices;
    int low = 21, high = 108;
    int dragging = -1;
    bool drawing = false;
    int hoverKey = -1;
};

class GrainEditor final : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    explicit GrainEditor (GrainProcessor&);
    ~GrainEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    static constexpr int designWidth = 1240;
    static constexpr int designHeight = 840;
    enum class Tab { dna, modulation, voice };
    void showTab (Tab t) { content.showTab (t); }
    KeyDnaEditor& dnaEditor() noexcept { return content.dna; }
    void refreshNow() { timerCallback(); }

private:
    struct Content final : public juce::Component
    {
        explicit Content (GrainProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void refresh();
        void showTab (Tab);
        void showSourceMenu();
        void showPresetMenu();

        GrainProcessor& processor;
        GrainWaveform wave;
        KeyDnaEditor dna;
        Key sourceKey, presetKey;
        Segmented quality, window;
        Knob size, density, position, scatter, pitch, spread, pitchScatter, reverse, jitter, level, master;
        // KEY DNA tab
        std::vector<std::unique_ptr<Key>> paramKeys;
        Knob curveAmount, variation, individuality;
        Key newSeed { "NEW SEED" }, randomise { "RANDOMISE CURVE" }, resetCurve { "RESET CURVE" }, zoom { "ZOOM" };
        // Modulation tab
        std::vector<std::unique_ptr<Choice>> modSources, modDests;
        std::vector<std::unique_ptr<Knob>> modAmounts;
        Knob lfo1Rate, lfo2Rate, auxAttack, auxDecay, auxSustain, auxRelease, macro1, macro2, triggerVar;
        Choice lfo1Shape, lfo2Shape;
        // Oscillator and voice tab
        Knob oscMix, oscShape, oscTune, tone, resonance, attack, decay, sustain, release, velocity, space, echo;
        Choice policy;
        Knob fixedNote, root, seed;
        Key deterministic { "DETERMINISTIC" };
        Key tabDna { "KEY DNA" }, tabMod { "MODULATION" }, tabVoice { "OSC + VOICE" };
        Tab tab = Tab::dna;
        juce::String statusText;
        std::unique_ptr<juce::FileChooser> chooser;
        bool zoomed = false;
        std::unique_ptr<juce::ParameterAttachment> deterministicAttachment;
    };

    void timerCallback() override;

    GrainProcessor& processor;
    plugin::OspLookAndFeel look;
    Content content;
    ScaledContent scaler;
    juce::ComponentBoundsConstrainer constrainer;
};

} // namespace osp::andorlab
