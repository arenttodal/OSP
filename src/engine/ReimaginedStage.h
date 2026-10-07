#pragma once

#include <array>
#include <cstdint>

namespace osp
{

struct InstrumentModel;

/**
    The bus part of Original <-> Reimagined (spec §12; shaping system v1.0 §47): a
    sympathetic resonator bank tuned to the source's partials and body peaks (plus a bank
    a fifth above towards the far end), re-excited by whatever is played, and towards the
    far end two formant peaks that wander slowly (spectral evolution: the instrument's
    vowel keeps changing). The per-voice part of Reimagined (shorter continuation,
    saturation, doubling, granular continuation, drift) lives in the voices.

    One implementation, several instances: the PostProcessor's (the shared stage every
    session made before per-layer routing uses, fed the layers' power-weighted amount) and
    one per layer (per-layer routing: each layer's own amount on its own signal, before
    the mix). The amount is smoothed, so moving it never clicks.

    prepare() is not real-time safe; everything else is (no allocation).
*/
class ReimaginedStage
{
public:
    void prepare (double sampleRate, std::uint64_t seed) noexcept;
    void reset() noexcept;

    /** Pick up the resonances of a newly published model. */
    void setModel (const InstrumentModel* model) noexcept;
    void setAmount (double amount) noexcept { target = amount < 0.0 ? 0.0 : (amount > 1.0 ? 1.0 : amount); }
    /** KALEIDOSCOPE FOCUS and SPREAD (0.5 / 0.5: the original stage, bit for bit). */
    void setShape (double newFocus, double newSpread) noexcept;
    double amount() const noexcept { return target; }

    /** Control-rate update (the owner calls it every controlInterval samples). */
    void update() noexcept;
    /** One stereo sample, in place. */
    void process (float& left, float& right) noexcept
    {
        if (resonanceMix > 0.0f)
        {
            const float excite = 0.5f * (left + right);
            float even = 0.0f, odd = 0.0f;
            for (int k = 0; k < numResonators; ++k)
            {
                const float y = bank[static_cast<std::size_t> (k)].process (excite);
                ((k & 1) == 0 ? even : odd) += y;
            }
            if (remapMix > 0.0f)
                for (int k = 0; k < numResonators; ++k)
                {
                    const float y = remapMix / resonanceMix * bank[static_cast<std::size_t> (k + resonators)].process (excite);
                    ((k & 1) == 0 ? odd : even) += y; // the other side: the halo spreads
                }
            left += resonanceMix * (nearSide * even + farSide * odd);
            right += resonanceMix * (farSide * even + nearSide * odd);
        }

        if (morphActive)
        {
            // Peaks only (no cut), so the level rises a little: compensate gently.
            const auto trim = static_cast<float> (1.0 / (1.0 + 0.9 * morph));
            left = formants[1].process (formants[0].process (left)) * trim;
            right = formants[3].process (formants[2].process (right)) * trim;
        }
        ++clock;
    }

    /** True while the stage changes the signal (otherwise a caller may skip it). */
    bool active() const noexcept { return resonanceMix > 0.0f || morphActive || target > 0.0 || smoothed > 1.0e-4; }
    /** How long the resonators ring after their input stops (seconds). */
    double ringSeconds() const noexcept { return resonanceMix > 0.0f ? 0.6 + 4.5 * smoothed : 0.0; }

    static constexpr int controlInterval = 32;
    static constexpr int resonators = 6;

private:
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float s1 = 0, s2 = 0;
        float process (float x) noexcept
        {
            const float y = b0 * x + s1;
            s1 = b1 * x - a1 * y + s2;
            s2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { s1 = s2 = 0; }
    };

    static void peaking (Biquad& f, double sampleRate, double hz, double q, double gainDb) noexcept;
    static void bandpass (Biquad& f, double sampleRate, double hz, double t60Seconds) noexcept;

    double sampleRate = 48000.0;
    double target = 0.0, smoothed = 0.0, applied = -1.0;
    double focus = 0.5;
    float nearSide = 0.75f, farSide = 0.25f;   ///< resonators' left / right split (SPREAD)

    std::array<double, resonators> resonatorHz {};
    int numResonators = 0;
    bool modelDirty = true;
    const InstrumentModel* model = nullptr;

    // First half: the source's partials and body; second half: a fifth above them
    // (harmonic remapping towards the Reimagined end).
    std::array<Biquad, 2 * resonators> bank;
    float resonanceMix = 0.0f, remapMix = 0.0f;

    // Two wandering formant peaks per channel.
    std::array<Biquad, 4> formants;
    double morph = 0.0;
    bool morphActive = false;
    std::uint64_t morphSeed = 1;
    std::int64_t clock = 0;
};

} // namespace osp
