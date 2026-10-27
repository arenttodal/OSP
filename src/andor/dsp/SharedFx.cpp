#include "andor/dsp/SharedFx.h"

#include <cmath>

namespace osp::andor
{

void SharedFx::prepare (double sampleRate)
{
    rate = sampleRate;
    reverb.prepare (sampleRate);
    SpaceReverb::Settings rs;
    rs.type = SpaceType::hall;
    rs.decaySeconds = 2.4;
    rs.preDelayMs = 12.0;
    rs.size = 0.6;
    rs.damping = 0.45;
    rs.modulation = 0.35;
    reverb.configure (rs);
    delay.prepare (sampleRate);
    EchoDelay::Settings es;
    es.type = EchoType::tape;
    es.sync = false;
    es.timeMs = 380.0;
    es.feedback = 0.38;
    es.tone = 0.45;
    es.age = 0.3;
    es.stereo = EchoStereo::pingPong;
    delay.setSettings (es);
    prepared = true;
    reset();
}

void SharedFx::reset() noexcept
{
    if (! prepared)
        return;
    reverb.reset();
    delay.reset();
    reverbQuiet = echoQuiet = 1 << 30;   // asleep until something is sent
}

void SharedFx::process (float* left, float* right, int numSamples, double space, double echo) noexcept
{
    if (! prepared)
        return;
    const int sleepAfter = static_cast<int> (8.0 * rate);   // longer than any tail at these settings
    const auto spaceGain = static_cast<float> (0.6 * space);
    const auto echoGain = static_cast<float> (0.5 * echo);
    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i], inR = right[i];
        float outL = inL, outR = inR;
        const bool reverbFed = spaceGain > 0.0f;
        if (reverbFed || reverbQuiet < sleepAfter)
        {
            float wl = 0.0f, wr = 0.0f;
            reverb.process (reverbFed ? inL : 0.0f, reverbFed ? inR : 0.0f, wl, wr);
            outL += spaceGain * wl;
            outR += spaceGain * wr;
            reverbQuiet = reverbFed ? 0 : reverbQuiet + 1;
        }
        else
            reverb.skip();
        const bool echoFed = echoGain > 0.0f;
        if (echoFed || echoQuiet < sleepAfter)
        {
            float wl = 0.0f, wr = 0.0f;
            delay.process (echoFed ? inL : 0.0f, echoFed ? inR : 0.0f, wl, wr);
            outL += echoGain * wl;
            outR += echoGain * wr;
            echoQuiet = echoFed ? 0 : echoQuiet + 1;
        }
        else
            delay.skip();
        left[i] = outL;
        right[i] = outR;
    }
}

} // namespace osp::andor
