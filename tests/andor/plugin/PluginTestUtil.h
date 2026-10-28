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

/** Counts heap allocations while `plugintest::counting` is set. Expand once per test program. */
#define ANDOR_DEFINE_ALLOCATION_COUNTER                                                     \
    namespace plugintest                                                                    \
    {                                                                                       \
        std::atomic<bool> counting { false };                                               \
        std::atomic<int> allocations { 0 };                                                 \
    }                                                                                       \
    void* operator new (std::size_t size)                                                   \
    {                                                                                       \
        if (plugintest::counting.load (std::memory_order_relaxed))                          \
            ++plugintest::allocations;                                                      \
        if (void* p = std::malloc (size == 0 ? 1 : size))                                   \
            return p;                                                                       \
        throw std::bad_alloc();                                                             \
    }                                                                                       \
    void* operator new[] (std::size_t size) { return operator new (size); }                 \
    void operator delete (void* p) noexcept { std::free (p); }                              \
    void operator delete (void* p, std::size_t) noexcept { std::free (p); }                 \
    void operator delete[] (void* p) noexcept { std::free (p); }                            \
    void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace plugintest
{
extern std::atomic<bool> counting;
extern std::atomic<int> allocations;

/** Runs `blocks` process calls with prepared MIDI and editor auditions; returns the allocations made. */
template <typename Processor>
int allocationsWhilePlaying (Processor& p, int blocks)
{
    const double rate = 48000.0;
    const int block = 256;
    p.setPlayConfigDetails (0, 2, rate, block);
    p.prepareToPlay (rate, block);
    juce::AudioBuffer<float> buffer (2, block);
    std::vector<juce::MidiBuffer> midi (static_cast<std::size_t> (blocks));
    for (int b = 0; b < blocks; ++b)
    {
        auto& m = midi[static_cast<std::size_t> (b)];
        m.ensureSize (256);
        if (b % 6 == 0)
            m.addEvent (juce::MidiMessage::noteOn (1, 48 + (b * 5) % 36, 0.8f), b % block);
        if (b % 9 == 0)
            m.addEvent (juce::MidiMessage::noteOff (1, 48 + (b * 3) % 36), 17);
        if (b == 40)
            m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 127), 3);
        if (b == 90)
            m.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), 3);
        m.addEvent (juce::MidiMessage::pitchWheel (1, 8192 + (b * 97) % 4000), 100);
    }
    for (int b = 0; b < 20; ++b)   // warm up (first notes, effect wake-up)
    {
        juce::MidiBuffer none;
        buffer.clear();
        p.processBlock (buffer, none);
    }
    allocations = 0;
    counting = true;
    for (int b = 0; b < blocks; ++b)
    {
        if (b % 13 == 0)
            p.audition (60 + b % 12, (b / 13) % 2 == 0);   // what the editor's keys do (message thread in real life)
        buffer.clear();
        p.processBlock (buffer, midi[static_cast<std::size_t> (b)]);
    }
    counting = false;
    return allocations.load();
}
} // namespace plugintest
