#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>

namespace osp::plugin
{

/**
    MIDI LEARN: an external controller (a MIDI CC, any channel) turns one host parameter, as if
    the knob were turned: the knob moves, the host sees the change (automation writes in touch
    mode), and the value is saved like any other.

    Right-click a control, Learn MIDI CC, move a controller: that controller now turns that
    control. One controller turns one control; learning a controller again moves it.

    Threads: the audio thread only posts each mapped controller's latest value (one atomic per
    controller number: no lock, no allocation). A message-thread timer (60 Hz) applies it with
    setValueNotifyingHost inside a change gesture, and ends the gesture after a pause.

    Mappings belong to the session (the DAW project), not to presets: the processors save them
    in getStateInformation only, so changing sounds never changes a controller setup.
*/
class MidiLearn final : private juce::Timer
{
public:
    MidiLearn (juce::AudioProcessor& processor, juce::AudioProcessorValueTreeState& state);
    ~MidiLearn() override;

    static constexpr int controllers = 120;   ///< CC 0-119 (120-127 are channel-mode messages)

    // -- audio thread --------------------------------------------------------------------------
    /** A controller message. True when MIDI LEARN takes it (learning, or a mapped controller):
        the processor then skips that number's built-in shortcut (CC 20-27 macros), never the
        others (mod wheel, sustain, the CC sources). */
    bool handleController (int number, int value) noexcept;

    // -- message thread ------------------------------------------------------------------------
    /** The next controller moved turns this parameter. */
    void learn (const juce::String& parameterId);
    void cancelLearn() noexcept { learning.store (-1, std::memory_order_release); }
    /** The parameter being learned ("" when none). */
    juce::String learningParameter() const;
    /** The controller that turns this parameter, or -1. */
    int controllerFor (const juce::String& parameterId) const;
    void assign (int number, const juce::String& parameterId);
    void forget (const juce::String& parameterId);
    void clear();
    int mappingCount() const;

    /** `<MidiLearn schema="1"><Map cc="21" param="life"/>...</MidiLearn>`, or nullptr with no
        mapping (an older session reads as none). */
    std::unique_ptr<juce::XmlElement> toXml() const;
    /** Replaces every mapping; nullptr clears. Unknown parameter IDs are skipped. */
    void fromXml (const juce::XmlElement* xml);

    /** What the timer does (tests call it directly). */
    void applyPending();

private:
    void timerCallback() override { applyPending(); }
    int indexOf (const juce::String& parameterId) const;
    juce::RangedAudioParameter* parameterAt (int index) const;

    juce::AudioProcessor& processor;
    juce::AudioProcessorValueTreeState& state;
    std::array<std::atomic<int>, controllers> target {};    ///< parameter index, -1: none
    std::array<std::atomic<int>, controllers> pending {};   ///< latest value 0-127, -1: none
    std::atomic<int> learning { -1 };                       ///< parameter index being learned
    std::atomic<int> heard { -1 };                          ///< the controller learning heard
    // Message thread only: open change gestures, and when each controller last moved.
    std::array<int, controllers> gestureParam {};   ///< the parameter whose gesture is open, -1: none
    std::array<juce::uint32, controllers> lastMoved {};
};

} // namespace osp::plugin
