#include "audio/envelopes/Adsr.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using osp::Adsr;
using osp::AdsrSettings;

TEST_CASE ("ADSR: linear attack reaches 1 after the attack time", "[unit][adsr]")
{
    Adsr env;
    env.prepare (1000.0, { 0.1, 0.0, 1.0, 0.1 });
    env.noteOn();
    float v = 0.0f;
    for (int i = 0; i < 50; ++i)
        v = env.next();
    CHECK (v == Approx (0.5).margin (0.011));
    for (int i = 0; i < 50; ++i)
        v = env.next();
    CHECK (v == Approx (1.0));
    CHECK (env.isActive());
}

TEST_CASE ("ADSR: decay settles at the sustain level", "[unit][adsr]")
{
    Adsr env;
    env.prepare (1000.0, { 0.0, 0.2, 0.5, 0.1 });
    env.noteOn();
    float v = 0.0f;
    // 60 dB of the distance after 0.2 s
    for (int i = 0; i < 201; ++i)
        v = env.next();
    CHECK (v == Approx (0.5).margin (0.001));
    for (int i = 0; i < 1000; ++i)
        v = env.next();
    CHECK (v == Approx (0.5));
    CHECK (env.stage() == Adsr::Stage::sustain);
}

TEST_CASE ("ADSR: release falls 60 dB in the release time and then ends", "[unit][adsr]")
{
    Adsr env;
    env.prepare (1000.0, { 0.0, 0.0, 1.0, 0.1 });
    env.noteOn();
    for (int i = 0; i < 10; ++i)
        env.next();
    env.noteOff();
    float v = 1.0f;
    for (int i = 0; i < 100; ++i)
        v = env.next();
    CHECK (v == Approx (0.001).margin (0.0002));
    int extra = 0;
    while (env.isActive() && extra < 1000)
    {
        env.next();
        ++extra;
    }
    CHECK_FALSE (env.isActive());
    CHECK (extra < 60); // -90 dB reached at ~1.5x the release time
}

TEST_CASE ("ADSR: retrigger continues from the current level (no click)", "[unit][adsr]")
{
    Adsr env;
    env.prepare (1000.0, { 0.1, 0.0, 1.0, 0.5 });
    env.noteOn();
    for (int i = 0; i < 200; ++i)
        env.next();
    env.noteOff();
    for (int i = 0; i < 100; ++i)
        env.next();
    const float before = env.level();
    env.noteOn();
    const float after = env.next();
    CHECK (after > before);
    CHECK (after - before < 0.011f);
}

TEST_CASE ("ADSR: changing the sustain level while a note is held glides there (no step)", "[unit][adsr]")
{
    const double rate = 48000.0;
    AdsrSettings s;
    s.attackSeconds = 0.001;
    s.decaySeconds = 0.05;
    s.sustainLevel = 0.8;
    s.releaseSeconds = 0.2;
    Adsr env;
    env.prepare (rate, s);
    env.noteOn();
    for (int i = 0; i < 24000; ++i)
        env.next();
    REQUIRE (env.stage() == Adsr::Stage::sustain);
    CHECK (env.level() == Approx (0.8f));
    // Unchanged it stays exactly at the level.
    CHECK (env.next() == 0.8f);
    s.sustainLevel = 0.2;
    env.prepare (rate, s);
    float previous = env.level(), largestStep = 0.0f;
    for (int i = 0; i < 4800; ++i)
    {
        const float v = env.next();
        largestStep = std::max (largestStep, std::abs (v - previous));
        previous = v;
    }
    CHECK (largestStep < 0.01f);                 // 0.6 spread over ~10 ms, never a jump
    CHECK (env.level() == Approx (0.2f).margin (1.0e-4));
    // Down to zero while held: the note ends.
    s.sustainLevel = 0.0;
    env.prepare (rate, s);
    for (int i = 0; i < 4800; ++i)
        env.next();
    CHECK_FALSE (env.isActive());
}
