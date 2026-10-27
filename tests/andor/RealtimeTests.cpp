#include "andor/dsp/OfflineRender.h"
#include "andor/grain/GrainEngine.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseEngine.h"
#include "andor/phrase/PhraseFixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdlib>
#include <new>

// Rule 1 checked, not just promised: allocations on the rendering thread are counted while
// `counting` is set. render() and every note / pedal / panic call must make none.
namespace
{
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
void* operator new[] (std::size_t size)
{
    if (counting.load (std::memory_order_relaxed))
        ++allocations;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace osp;

TEST_CASE ("Real time: the Phrase engine never allocates while playing", "[phrase][realtime]")
{
    const auto fx = phrase::makePhraseFixture ("vocal-phrase");
    const auto atlas = phrase::analysePhrase (fx.audio, {}).atlas;
    auto src = std::make_shared<const PlaybackSource> (fx.audio, 60.0, phrase::phrasePadding());
    const auto model = phrase::buildPhraseModel (src, atlas, 1);
    phrase::PhraseEngine e;
    e.prepare (48000.0, 256);
    e.setModel (model.get());
    std::vector<float> l (256), r (256);
    for (auto mode : { phrase::PlayMode::oneShot, phrase::PlayMode::gate, phrase::PlayMode::sustain })
    {
        phrase::PhraseParams p;
        p.playMode = mode;
        p.fill = phrase::FillMode::extended;
        p.roundRobin = phrase::RoundRobin::random;
        p.sustainCharacter = 0.6;
        p.synthetic = 0.4;
        p.tone = 0.5;
        p.space = 0.4;
        p.echo = 0.3;
        p.polyphony = 6;
        e.setParams (p);
        allocations = 0;
        counting = true;
        for (int b = 0; b < 400; ++b)
        {
            if (b % 7 == 0)
                e.noteOn (48 + (b * 5) % 36, 0.8f);
            if (b % 11 == 0)
                e.noteOff (48 + (b * 3) % 36);
            if (b == 100)
                e.setSustainPedal (true);
            if (b == 200)
                e.setSustainPedal (false);
            e.setPitchBend (std::sin (b * 0.05));
            e.render (l.data(), r.data(), 256);
        }
        e.allNotesOff();
        e.render (l.data(), r.data(), 256);
        e.panic();
        e.resetEvents();
        counting = false;
        INFO (static_cast<int> (mode));
        CHECK (allocations.load() == 0);
    }
}

TEST_CASE ("Real time: the Grain engine never allocates while playing", "[grain][realtime]")
{
    const auto fx = phrase::makePhraseFixture ("vocal-phrase");
    const auto atlas = std::make_shared<const atlas::NoteAtlas> (phrase::analysePhrase (fx.audio, {}).atlas);
    auto src = std::make_shared<const PlaybackSource> (fx.audio, 64.0, grain::grainPadding());
    const auto model = grain::buildGrainModel (src, atlas, 64.0, 1);
    grain::GrainEngine e;
    e.prepare (48000.0, 256);
    e.setModel (model.get());
    std::vector<float> l (256), r (256);
    for (auto quality : { grain::Quality::eco, grain::Quality::high })
    {
        grain::GrainParams p;
        p.quality = quality;
        p.sizeSeconds = 0.5;
        p.density = 150.0;
        p.pitchScatter = 3.0;
        p.reverseProbability = 0.3;
        p.oscMix = 0.4;
        p.space = 0.4;
        p.echo = 0.3;
        p.mod[0] = { grain::ModSource::lfo1, grain::ModDest::density, 0.5 };
        p.mod[1] = { grain::ModSource::velocity, grain::ModDest::size, 0.3 };
        p.dna.curves[0].addPoint (30.0f, 0.5f);
        p.dna.curves[0].addPoint (90.0f, -0.5f);
        e.setParams (p);
        allocations = 0;
        counting = true;
        for (int b = 0; b < 400; ++b)
        {
            if (b % 5 == 0)
                e.noteOn (40 + (b * 7) % 48, 0.9f);
            if (b % 9 == 0)
                e.noteOff (40 + (b * 3) % 48);
            if (b % 50 == 0)
            {
                // KEY DNA recomputed on the audio thread when its seed or a curve changes.
                p.dna.seed = static_cast<std::uint64_t> (b + 1);
                p.dna.curves[1].addPoint (static_cast<float> (b % 100), 0.3f);
                p.policy = static_cast<grain::SourcePolicy> ((b / 50) % 4);
                e.setParams (p);
            }
            e.setModWheel ((b % 100) / 100.0);
            e.render (l.data(), r.data(), 256);
        }
        e.allNotesOff();
        e.render (l.data(), r.data(), 256);
        e.panic();
        counting = false;
        CHECK (allocations.load() == 0);
    }
}
