#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace osp::plugin
{

/**
    The Library inspector's ROOT NOTE: a compact musical selector in place of a 73-row list.

    The field (ANDOR's dropdown look) shows the current root ("A2", or "Detected"). A click opens
    a small picker below it:
    - Detected (the analysis decides; its note shown when known);
    - the 12 pitch classes, laid out as an octave of keys;
    - a compact octave stepper (octave -1 to 9, C4 = MIDI 60, as the Library writes names);
    - the root it now gives.

    Every MIDI root 0-127 can be given. A key or an octave step that would leave that range is
    disabled, never clamped silently. Each choice reports the new root once through onChange; the
    owner records it as before (the catalog's root, its undo and persistence are unchanged).
    Escape or a press outside closes the picker.
*/
class RootNoteSelector final : public juce::Component
{
public:
    RootNoteSelector();
    ~RootNoteSelector() override;

    /** The root given (nullopt: Detected) and the analysis's own root, when known. */
    void setRoot (std::optional<int> root, std::optional<int> detected);
    std::optional<int> root() const noexcept { return given; }
    std::optional<int> detected() const noexcept { return detectedRoot; }
    /** A new root chosen (nullopt: Detected). */
    std::function<void (std::optional<int>)> onChange;

    /** The field's text ("A2", "Detected", "Detected  A2"). */
    juce::String fieldText() const;

    // The picker (also the tests' entry points: each acts as its click does).
    void showPicker (bool shouldShow);
    bool pickerShown() const noexcept { return picker != nullptr; }
    juce::Component* pickerComponent() const noexcept;
    /** The octave the keys write into (the root's, else the detected root's, else 3). */
    int octave() const noexcept { return shownOctave; }
    static int midiFor (int pitchClass, int octave) noexcept { return (octave + 1) * 12 + pitchClass; }
    static bool valid (int midi) noexcept { return midi >= 0 && midi <= 127; }
    bool keyEnabled (int pitchClass) const noexcept { return valid (midiFor (pitchClass, shownOctave)); }
    bool canStepOctave (int delta) const noexcept;
    void choosePitchClass (int pitchClass);
    void chooseDetected();
    void stepOctave (int delta);

    static juce::String nameOf (int midi);

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override { repaint(); }
    void focusLost (FocusChangeType) override { repaint(); }

    class Picker;

private:
    void commit (std::optional<int> root);
    int pitchClassShown() const noexcept;
    std::optional<int> given, detectedRoot;
    int shownOctave = 3;
    std::unique_ptr<Picker> picker;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RootNoteSelector)
};

} // namespace osp::plugin
