#include "audio/envelopes/Adsr.h"

#include <algorithm>
#include <cmath>

namespace osp
{

namespace
{
    constexpr double releaseEndLevel = 3.1622776601683795e-5; // -90 dB
    constexpr double decaySettleDistance = 1.0e-5;
}

double Adsr::coefficientFor60dB (double seconds, double rate) noexcept
{
    if (seconds <= 0.0)
        return 0.0;
    return std::exp (std::log (1.0e-3) / (seconds * rate));
}

void Adsr::prepare (double newSampleRate, const AdsrSettings& newSettings) noexcept
{
    sampleRate = newSampleRate;
    settings = newSettings;
    settings.sustainLevel = std::clamp (settings.sustainLevel, 0.0, 1.0);

    const double attackSamples = settings.attackSeconds * sampleRate;
    attackIncrement = attackSamples >= 1.0 ? 1.0 / attackSamples : 1.0;
    decayCoefficient = coefficientFor60dB (settings.decaySeconds, sampleRate);
    releaseCoefficient = coefficientFor60dB (std::max (settings.releaseSeconds, 0.001), sampleRate);
    sustainGlide = 1.0 - std::exp (-1.0 / (0.01 * sampleRate));
}

void Adsr::noteOn() noexcept
{
    // Restart from the current level so retriggering a sounding voice does not click.
    currentStage = Stage::attack;
}

void Adsr::noteOff() noexcept
{
    if (currentStage != Stage::idle)
        currentStage = Stage::release;
}

void Adsr::reset() noexcept
{
    currentStage = Stage::idle;
    value = 0.0;
}

float Adsr::next() noexcept
{
    switch (currentStage)
    {
        case Stage::idle:
            return 0.0f;

        case Stage::attack:
            value += attackIncrement;
            if (value >= 1.0)
            {
                value = 1.0;
                currentStage = Stage::decay;
            }
            break;

        case Stage::decay:
            value = settings.sustainLevel + (value - settings.sustainLevel) * decayCoefficient;
            if (std::abs (value - settings.sustainLevel) < decaySettleDistance)
            {
                value = settings.sustainLevel;
                currentStage = Stage::sustain;
            }
            break;

        case Stage::sustain:
            // Held: the sustain level, gliding there when it is changed during the note
            // (unchanged, the value stays exactly the level).
            value += (settings.sustainLevel - value) * sustainGlide;
            if (std::abs (value - settings.sustainLevel) < decaySettleDistance)
                value = settings.sustainLevel;
            if (value <= 0.0)
            {
                value = 0.0;
                currentStage = Stage::idle;
            }
            break;

        case Stage::release:
            value *= releaseCoefficient;
            if (value < releaseEndLevel)
            {
                value = 0.0;
                currentStage = Stage::idle;
            }
            break;
    }

    return static_cast<float> (value);
}

} // namespace osp
