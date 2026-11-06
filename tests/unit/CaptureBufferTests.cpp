// Live capture's ring (audio/capture): what the audio thread writes comes out exactly, by
// absolute position, across the wrap; a stretch that may be overwritten is refused, never torn;
// mono stays mono; the writer never allocates; trimTake cuts the silence around a take.

#include "audio/capture/CaptureBuffer.h"
#include "support/AllocationCounter.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using namespace osp;

namespace
{
    /** A frame's value encodes its position (exact in float up to 2^24). */
    float valueAt (std::int64_t frame, int channel) { return static_cast<float> (frame % 8000000) * (channel == 0 ? 1.0f : -1.0f); }

    void writeFrames (capture::CaptureBuffer& ring, std::int64_t from, int n, bool stereo = true)
    {
        std::vector<float> l (static_cast<std::size_t> (n)), r (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
        {
            l[static_cast<std::size_t> (i)] = valueAt (from + i, 0);
            r[static_cast<std::size_t> (i)] = valueAt (from + i, 1);
        }
        const float* channels[2] = { l.data(), r.data() };
        ring.write (channels, stereo ? 2 : 1, n);
    }

    bool matches (const AudioData& take, std::int64_t from)
    {
        for (int ch = 0; ch < take.numChannels(); ++ch)
            for (std::int64_t i = 0; i < take.numFrames(); ++i)
                if (take.channels[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] != valueAt (from + i, ch))
                    return false;
        return true;
    }
}

TEST_CASE ("capture: a stretch comes out exactly as it went in, across the ring's wrap", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    ring.prepare (1000.0, 10.0);   // 10 s + the margin at 1 kHz: 14000 frames
    std::int64_t pos = 0;
    for (int block : { 7, 64, 513, 1, 999, 256 })
        for (int k = 0; k < 20; ++k)
        {
            writeFrames (ring, pos, block);
            pos += block;
        }
    REQUIRE (ring.position() == pos);
    REQUIRE (pos > 14000);   // wrapped
    AudioData take;
    const auto from = pos - 9000;
    REQUIRE (ring.copy (from, pos - 100, take));
    CHECK (take.numChannels() == 2);
    CHECK (take.numFrames() == 8900);
    CHECK (take.sampleRate == 1000.0);
    CHECK (matches (take, from));
}

TEST_CASE ("capture: what may be overwritten or has not arrived is refused", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    AudioData take;
    CHECK_FALSE (ring.copy (0, 10, take));   // not prepared
    ring.prepare (1000.0, 10.0);
    writeFrames (ring, 0, 30000);
    CHECK_FALSE (ring.copy (0, 100, take));                       // long gone
    CHECK_FALSE (ring.copy (ring.oldestSafe() - 1, 29000, take));   // one frame too old
    CHECK_FALSE (ring.copy (29000, 30001, take));                  // not written yet
    CHECK_FALSE (ring.copy (29000, 29000, take));                  // empty
    CHECK (take.isEmpty());
    REQUIRE (ring.copy (ring.oldestSafe(), 30000, take));
    CHECK (take.numFrames() == 30000 - ring.oldestSafe());
    CHECK (30000 - ring.oldestSafe() == 10000);   // the take length, the margin held back
}

TEST_CASE ("capture: a mono input gives a mono take, a stereo one stereo", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    ring.prepare (1000.0, 10.0);
    writeFrames (ring, 0, 500, false);
    AudioData take;
    REQUIRE (ring.copy (0, 500, take));
    CHECK (take.numChannels() == 1);
    CHECK (matches (take, 0));
    writeFrames (ring, 500, 500, true);
    REQUIRE (ring.copy (500, 1000, take));
    CHECK (take.numChannels() == 2);
    CHECK (matches (take, 500));
}

TEST_CASE ("capture: the meter holds the loudest sample until read", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    ring.prepare (1000.0, 1.0);
    std::vector<float> block (64, 0.1f);
    block[10] = -0.7f;
    const float* channels[1] = { block.data() };
    ring.write (channels, 1, 64);
    block[10] = 0.2f;
    ring.write (channels, 1, 64);
    CHECK (ring.takePeak() == 0.7f);
    CHECK (ring.takePeak() == 0.0f);
}

TEST_CASE ("capture: copied while the input keeps arriving, a take is whole or refused", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    ring.prepare (48000.0, 2.0);
    std::atomic<bool> stop { false };
    std::thread writer ([&] {
        std::int64_t pos = 0;
        std::vector<float> l (128), r (128);
        while (! stop.load())
        {
            for (int i = 0; i < 128; ++i)
            {
                l[static_cast<std::size_t> (i)] = valueAt (pos + i, 0);
                r[static_cast<std::size_t> (i)] = valueAt (pos + i, 1);
            }
            const float* channels[2] = { l.data(), r.data() };
            ring.write (channels, 2, 128);
            pos += 128;
            std::this_thread::sleep_for (std::chrono::microseconds (100));   // ~30x real time
        }
    });
    int whole = 0, refused = 0;
    while (ring.position() < 48000)
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    for (int k = 0; k < 400; ++k)
    {
        const auto end = ring.position();
        // Alternately a safe stretch and one right at the edge of the overwrite.
        const auto from = (k % 2 == 0) ? end - 48000 : ring.oldestSafe();
        AudioData take;
        if (ring.copy (from, end, take))
        {
            CHECK (matches (take, from));
            ++whole;
        }
        else
            ++refused;
    }
    stop = true;
    writer.join();
    CHECK (whole > 0);
    INFO ("refused " << refused);
}

TEST_CASE ("capture: the audio thread's write never allocates", "[unit][capture]")
{
    capture::CaptureBuffer ring;
    ring.prepare (48000.0, 60.0);
    std::vector<float> block (512, 0.25f);
    const float* channels[2] = { block.data(), block.data() };
    test::AllocationScope scope;
    for (int i = 0; i < 2000; ++i)   // 21 s, through the wrap of nothing yet; then more
        ring.write (channels, 2, 512);
    for (int i = 0; i < 6000; ++i)
        ring.write (channels, 1, 512);
    CHECK (scope.count() == 0);
}

TEST_CASE ("capture: trimTake keeps the sound with a short lead-in and a tail", "[unit][capture]")
{
    AudioData take;
    take.sampleRate = 1000.0;
    take.channels.assign (1, std::vector<float> (3000, 0.0f));
    for (int i = 1000; i < 2000; ++i)
        take.channels[0][static_cast<std::size_t> (i)] = 0.5f * std::sin (0.3f * static_cast<float> (i));
    take.channels[0][100] = 0.001f;   // -60 dBFS: below the floor
    const auto trimmed = capture::trimTake (take);
    REQUIRE_FALSE (trimmed.isEmpty());
    CHECK (trimmed.numFrames() == 1000 + 50 + 250);   // the sound, 50 ms before, 250 ms after
    CHECK (trimmed.channels[0][50] == take.channels[0][1000]);

    AudioData silence;
    silence.sampleRate = 1000.0;
    silence.channels.assign (2, std::vector<float> (3000, 0.0001f));
    CHECK (capture::trimTake (silence).isEmpty());
}
