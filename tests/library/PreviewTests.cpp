// Library Stage 4: the preview engine. Hearing sounds never touches the instrument; the audio
// thread never allocates; slots started together are not louder than -1 dBFS; the keyboard
// plays a sound against its root at any host rate; polyphony is bounded; sounds replaced while
// they play are freed only once nothing plays them.

#include "audio/utility/TestSignals.h"
#include "library/PreviewEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>

using namespace osp;
using namespace osp::library;

namespace
{
    // Counts allocations made while `counting` is set (render() must make none).
    std::atomic<bool> counting { false };
    std::atomic<int> allocations { 0 };
}

void* operator new (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    struct Output
    {
        std::vector<float> left, right;
        float peak() const
        {
            float p = 0.0f;
            for (std::size_t i = 0; i < left.size(); ++i)
                p = std::max ({ p, std::abs (left[i]), std::abs (right[i]) });
            return p;
        }
        double rms (std::size_t from = 0, std::size_t to = SIZE_MAX) const
        {
            to = std::min (to, left.size());
            double sum = 0.0;
            for (auto i = from; i < to; ++i)
                sum += static_cast<double> (left[i]) * left[i];
            return to > from ? std::sqrt (sum / static_cast<double> (to - from)) : 0.0;
        }
        /** Last frame above -60 dBFS. */
        std::size_t lastSound() const
        {
            for (auto i = left.size(); i-- > 0;)
                if (std::abs (left[i]) > 0.001f || std::abs (right[i]) > 0.001f)
                    return i;
            return 0;
        }
    };

    Output render (PreviewEngine& engine, int frames, int block = 512)
    {
        Output o;
        o.left.assign (static_cast<std::size_t> (frames), 0.0f);
        o.right.assign (static_cast<std::size_t> (frames), 0.0f);
        for (int start = 0; start < frames; start += block)
        {
            const int n = std::min (block, frames - start);
            float* channels[2] = { o.left.data() + start, o.right.data() + start };
            engine.render (channels, 2, n);
        }
        return o;
    }

    std::shared_ptr<const PreviewSound> sine (double hz, double seconds, double rate = 48000.0, double amplitude = 0.5, std::optional<double> root = 60.0)
    {
        return makePreviewSound (testsignals::sine (hz, seconds, rate, amplitude, 2), root);
    }

    /** Frequency from zero crossings of `left` between two frames. */
    double frequency (const Output& o, std::size_t from, std::size_t to, double rate)
    {
        int crossings = 0;
        for (auto i = from + 1; i < to; ++i)
            if ((o.left[i - 1] < 0.0f) != (o.left[i] < 0.0f))
                ++crossings;
        return crossings / 2.0 / (static_cast<double> (to - from) / rate);
    }
}

TEST_CASE ("preview: a slot plays its sound once, dry, at the preview gain", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (48000.0);
    engine.setSound (0, sine (440.0, 0.5));
    engine.play (0b0001u);
    const auto o = render (engine, 48000);
    // -6 dB of a 0.5 sine: RMS 0.5 / sqrt 2 * 0.501
    CHECK (o.rms (1000, 20000) == Catch::Approx (0.5 / std::sqrt (2.0) * std::pow (10.0, -6.0 / 20.0)).epsilon (0.02));
    CHECK (o.lastSound() == Catch::Approx (24000.0).margin (10.0));   // once, not looped
    CHECK (frequency (o, 1000, 20000, 48000.0) == Catch::Approx (440.0).epsilon (0.01));
    CHECK_FALSE (engine.isSounding());
    CHECK (engine.playheadSeconds (0) < 0.0);
}

TEST_CASE ("preview: the host's rate is respected (a 44.1 kHz sound at 96 kHz keeps its pitch and length)", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (96000.0);
    engine.setSound (1, sine (220.0, 1.0, 44100.0));
    engine.play (0b0010u);
    const auto o = render (engine, 2 * 96000, 333);
    CHECK (frequency (o, 2000, 90000, 96000.0) == Catch::Approx (220.0).epsilon (0.01));
    CHECK (static_cast<double> (o.lastSound()) == Catch::Approx (96000.0).margin (40.0));
}

TEST_CASE ("preview: slots together are an equal-power sum, and the preview never exceeds -1 dBFS", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (48000.0);
    engine.setGainDb (0.0f);
    for (int s = 0; s < 3; ++s)
        engine.setSound (s, sine (220.0, 0.5, 48000.0, 0.3));
    engine.play (0b0001u);
    const double one = render (engine, 24000).rms (1000, 20000);
    engine.play (PreviewEngine::trayMask);
    const double three = render (engine, 24000).rms (1000, 20000);
    // Identical (correlated) sounds: 3 / sqrt 3 = 1.73 times, never 3 times.
    CHECK (three / one == Catch::Approx (std::sqrt (3.0)).epsilon (0.02));

    // Loud sounds at +6 dB: the limiter holds the ceiling.
    engine.setGainDb (6.0f);
    for (int s = 0; s < 3; ++s)
        engine.setSound (s, sine (110.0, 0.5, 48000.0, 0.95));
    engine.play (PreviewEngine::trayMask);
    const auto loud = render (engine, 24000);
    CHECK (loud.peak() <= 0.892f);
    CHECK (loud.rms (2000, 20000) > 0.4);
}

TEST_CASE ("preview: the keyboard plays a sound against its root, bounded to eight voices", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (48000.0);
    // No root in the catalog: estimated from the audio (a 220 Hz tone is A3, MIDI 57).
    const auto tone = makePreviewSound (testsignals::sine (220.0, 1.0, 48000.0, 0.5, 1), std::nullopt);
    REQUIRE (tone->rootKnown);
    CHECK (tone->rootMidi == Catch::Approx (57.0).margin (0.1));
    engine.setSound (PreviewEngine::browserSlot, tone);
    engine.noteOn (69, 1.0f, 1u << PreviewEngine::browserSlot);   // A4: an octave up
    auto o = render (engine, 12000);
    CHECK (frequency (o, 500, 11000, 48000.0) == Catch::Approx (440.0).epsilon (0.01));
    engine.noteOff (69);
    o = render (engine, 9600);
    CHECK (o.lastSound() < 4800);   // released within 100 ms

    // Twenty notes: never more than eight sound (the oldest fade out), output stays finite.
    for (int n = 0; n < 20; ++n)
        engine.noteOn (40 + n, 0.8f, 1u << PreviewEngine::browserSlot);
    o = render (engine, 4800);
    for (auto v : o.left)
        REQUIRE (std::isfinite (v));
    CHECK (o.peak() <= 0.892f);
    engine.allNotesOff();
    o = render (engine, 4800);
    CHECK (o.lastSound() < 2400);
}

TEST_CASE ("preview: noise and silence have no invented root; empty and tiny sounds are ignored", "[unit][library][preview]")
{
    const auto noise = makePreviewSound (testsignals::whiteNoise (1.0, 48000.0, 0.3, 7), std::nullopt);
    CHECK_FALSE (noise->rootKnown);
    CHECK (noise->rootMidi == 60.0);
    const auto silence = makePreviewSound (testsignals::silence (0.5, 48000.0), std::nullopt);
    CHECK_FALSE (silence->rootKnown);
    CHECK (silence->peakMax.size() == PreviewSound::overviewBins);

    PreviewEngine engine;
    engine.prepare (44100.0);
    AudioData tiny;
    tiny.sampleRate = 48000.0;
    tiny.channels = { { 0.5f } };
    engine.setSound (0, makePreviewSound (tiny, 60.0));
    engine.setSound (1, makePreviewSound (AudioData {}, 60.0));
    engine.play (0b0011u);
    engine.noteOn (60, 1.0f, 0b1111u);
    const auto o = render (engine, 4410);
    CHECK (o.peak() == 0.0f);
    CHECK_FALSE (engine.isSounding());
}

TEST_CASE ("preview: stop fades out; a replaced sound keeps playing and is freed only afterwards", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (48000.0);
    engine.setSound (0, sine (330.0, 2.0));
    engine.play (0b0001u);
    render (engine, 4800);
    CHECK (engine.isSounding());
    CHECK (engine.playheadSeconds (0) == Catch::Approx (0.1).margin (0.011));
    // Replaced while it plays: the old one finishes, nothing is freed under it.
    for (int i = 0; i < 5; ++i)
        engine.setSound (0, sine (200.0 + i, 0.2));
    engine.collectGarbage();
    CHECK (engine.ownedCount() == 6);
    auto o = render (engine, 4800);
    CHECK (o.rms (0, 4800) > 0.1);
    engine.collectGarbage();
    // Freed by age: nothing published after the sound still playing goes before it does.
    CHECK (engine.ownedCount() == 6);
    engine.stop();
    o = render (engine, 4800);
    CHECK (o.lastSound() < 1250);   // 20 ms fade (+ a block)
    engine.collectGarbage();
    CHECK (engine.ownedCount() == 1);
    engine.setSound (0, nullptr);
    render (engine, 512);
    engine.collectGarbage();
    CHECK (engine.ownedCount() == 0);
    CHECK (engine.sound (0) == nullptr);
}

TEST_CASE ("preview: render, notes and commands never allocate (audio thread)", "[unit][library][preview]")
{
    PreviewEngine engine;
    engine.prepare (48000.0);
    for (int s = 0; s < PreviewEngine::numSlots; ++s)
        engine.setSound (s, sine (100.0 * (s + 1), 1.0));
    engine.play (0b1111u);   // queued by the message thread before counting
    std::vector<float> l (512), r (512);
    float* channels[2] = { l.data(), r.data() };
    allocations = 0;
    counting = true;
    for (int b = 0; b < 50; ++b)
    {
        if (b % 7 == 0)
            engine.noteOn (48 + b % 24, 0.7f, 0b1111u);
        if (b % 11 == 0)
            engine.noteOff (48 + (b - 7) % 24);
        engine.render (channels, 2, 512);
        float* mono[1] = { l.data() };
        engine.render (mono, 1, 64);
    }
    engine.allNotesOff();
    engine.render (channels, 2, 512);
    counting = false;
    CHECK (allocations.load() == 0);
}
