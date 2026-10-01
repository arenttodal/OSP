#pragma once

namespace osp
{

struct AdsrSettings
{
    double attackSeconds = 0.002;  ///< Linear rise 0 -> 1.
    double decaySeconds = 0.0;     ///< Exponential fall 1 -> sustain; time to fall 60 dB of the distance.
    double sustainLevel = 1.0;     ///< Linear gain 0..1.
    double releaseSeconds = 0.25;  ///< Exponential fall to silence; time to fall 60 dB.
};

/**
    Simple ADSR envelope.

    Attack is linear (predictable, click-free). Decay and release are exponential
    (one-pole) with "time" meaning the time to fall by 60 dB; the release stage ends
    at -90 dB. The envelope does not assume anything about the source's own
    envelope: with sustain = 1 it simply lets the recording speak.

    Real-time safe: no allocation.
*/
class Adsr
{
public:
    enum class Stage { idle, attack, decay, sustain, release };

    void prepare (double sampleRate, const AdsrSettings& settings) noexcept;

    void noteOn() noexcept;
    void noteOff() noexcept;
    void reset() noexcept;

    float next() noexcept;

    Stage stage() const noexcept { return currentStage; }
    bool isActive() const noexcept { return currentStage != Stage::idle; }
    float level() const noexcept { return static_cast<float> (value); }

private:
    static double coefficientFor60dB (double seconds, double sampleRate) noexcept;

    AdsrSettings settings;
    double sampleRate = 48000.0;
    Stage currentStage = Stage::idle;
    double value = 0.0;
    double attackIncrement = 1.0;
    double decayCoefficient = 0.0;
    double releaseCoefficient = 0.0;
};

} // namespace osp
