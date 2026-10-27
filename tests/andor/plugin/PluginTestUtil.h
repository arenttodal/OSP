#pragma once

#include "midi/MidiEvent.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace plugintest
{

/** Plays `seq` through a processor exactly as a host would (blocks, sample-accurate MIDI). */
inline juce::AudioBuffer<float> renderThrough (juce::AudioProcessor& p, const osp::MidiSequence& seq, double sampleRate, int block, double seconds)
{
    p.setPlayConfigDetails (0, 2, sampleRate, block);
    p.prepareToPlay (sampleRate, block);
    const int total = static_cast<int> (seconds * sampleRate);
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    juce::AudioBuffer<float> buffer (2, block);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buffer.setSize (2, n, false, false, true);
        buffer.clear();
        juce::MidiBuffer midi;
        for (const auto& e : seq.events)
        {
            const int at = static_cast<int> (std::llround (e.timeSeconds * sampleRate)) - pos;
            if (at < 0 || at >= n)
                continue;
            switch (e.type)
            {
                case osp::MidiEvent::Type::noteOn: midi.addEvent (juce::MidiMessage::noteOn (1, e.note, static_cast<juce::uint8> (e.value)), at); break;
                case osp::MidiEvent::Type::noteOff: midi.addEvent (juce::MidiMessage::noteOff (1, e.note), at); break;
                case osp::MidiEvent::Type::sustainPedal: midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, e.value), at); break;
                case osp::MidiEvent::Type::allNotesOff: midi.addEvent (juce::MidiMessage::allNotesOff (1), at); break;
            }
        }
        p.processBlock (buffer, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, buffer, ch, 0, n);
    }
    return out;
}

inline double rms (const juce::AudioBuffer<float>& b, double sr, double from, double to)
{
    double sum = 0.0;
    int n = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = static_cast<int> (from * sr); i < std::min (b.getNumSamples(), static_cast<int> (to * sr)); ++i)
        {
            sum += static_cast<double> (b.getSample (ch, i)) * b.getSample (ch, i);
            ++n;
        }
    return n > 0 ? std::sqrt (sum / n) : 0.0;
}

inline bool finite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;
    return true;
}

inline bool identical (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumSamples() != b.getNumSamples() || a.getNumChannels() != b.getNumChannels())
        return false;
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        if (std::memcmp (a.getReadPointer (ch), b.getReadPointer (ch), sizeof (float) * static_cast<std::size_t> (a.getNumSamples())) != 0)
            return false;
    return true;
}

/** Writes a component's image to $ANDOR_SCREENSHOT_DIR/<name> when that variable is set. */
inline void screenshot (juce::Component& c, const juce::String& name, float scale = 1.0f)
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("ANDOR_SCREENSHOT_DIR", {});
    if (dir.isEmpty())
        return;
    const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    juce::File (dir).createDirectory();
    const auto file = juce::File (dir).getChildFile (name);
    file.deleteFile();
    juce::FileOutputStream out (file);
    juce::PNGImageFormat().writeImageToStream (image, out);
}

/** Isolated store and documents folders for a test run (never the user's). */
struct IsolatedFolders
{
    juce::File root;
    IsolatedFolders()
    {
        root = juce::File::getSpecialLocation (juce::File::tempDirectory)
                   .getChildFile ("andor-plugin-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        root.createDirectory();
#if JUCE_WINDOWS
        _putenv_s ("ANDOR_SAMPLE_STORE", root.getChildFile ("store").getFullPathName().toRawUTF8());
        _putenv_s ("ANDOR_DOCUMENTS_DIR", root.getChildFile ("documents").getFullPathName().toRawUTF8());
#else
        setenv ("ANDOR_SAMPLE_STORE", root.getChildFile ("store").getFullPathName().toRawUTF8(), 1);
        setenv ("ANDOR_DOCUMENTS_DIR", root.getChildFile ("documents").getFullPathName().toRawUTF8(), 1);
#endif
    }
    ~IsolatedFolders() { root.deleteRecursively(); }
};

} // namespace plugintest
