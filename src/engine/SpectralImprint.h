#pragma once

#include "core/AudioData.h"
#include "model/ImprintProfile.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace osp::imprint
{

/**
    SYNTHESIS S2, the spectral imprint (docs/research/synthesize/s2-imprint-*.md): the recording's
    own spectral envelope (its resonant body, formants, bow and breath colour) laid onto the SYNTH
    oscillator, so the synthetic voice keeps the recording's identity. Not a vocoder laboratory:
    one descriptor per recording (an Atlas member by its own), a band shaper per voice.

    Profile (the descriptor) is built offline from a model's audio: immutable, a few kB.
    Shaper colours one oscillator at control rate from a Profile (experiments A and B).
    Vocoder is experiment C (a real-time channel vocoder), kept for the research comparison.
*/

/** Builds a profile from a recording (not the audio thread). `f0Hz` <= 0: no pitch. */
Profile analyse (const AudioData& audio, double f0Hz);

/** Where the bands sit. Log spacing, or perceptual (ERB-rate) spacing. */
struct BandLayout
{
    static constexpr int maxBands = 32;
    int count = 24;
    std::array<double, maxBands> hz {};
    std::array<double, maxBands> q {};
    std::array<double, maxBands> octaves {};   ///< each band's width (to its neighbours)

    static BandLayout make (int count, bool perceptual, double lowHz = 80.0, double highHz = 12000.0);
};

/**
    Experiments A and B: a cascade of band (bell) filters on the oscillator. The bands' gains are
    solved so the cascade meets the target at every band's centre (each band's spill into its
    neighbours compensated), and the output is levelled to the oscillator's own loudness for the
    harmonics it actually has. Bypassed entirely while the imprint is 0.

    prepare() allocates; everything else is real-time safe.
*/
class Shaper
{
public:
    void prepare (double sampleRate, int bands = standardBands, bool perceptual = true) noexcept;
    /** A new note: the filters silent, the gains straight at their first target. */
    void reset() noexcept;

    /**
        Control rate. The target: `amount` (0..1) of the profile at `position` (frames from
        the start; < 0: the static imprint) blended with the static imprint by `motion`
        (0: static only), for an oscillator at `pitchHz`. `smoothSeconds` eases the change.
    */
    void setTarget (const Profile& profile, double amount, double position, double motion, double pitchHz,
                    double formantFollow, int samplesToNext) noexcept;

    /** True while any band or the level differs from unity (else process() is not needed). */
    bool active() const noexcept { return engaged; }

    float process (float x) noexcept
    {
        energyIn += x * x;
        float v = x;
        for (int b = 0; b < bands; ++b)
        {
            auto& s = state[static_cast<std::size_t> (b)];
            const auto& c = coef[static_cast<std::size_t> (b)];
            // TPT state-variable bell (Zavalishin): y = x + (A^2 - 1) k bp, stable under modulation.
            const float v3 = v - s.ic2;
            const float v1 = c.a1 * s.ic1 + c.a2 * v3;
            const float v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
            s.ic1 = 2.0f * v1 - s.ic1;
            s.ic2 = 2.0f * v2 - s.ic2;
            v += c.boost * v1;
        }
        energyOut += v * v;
        level += levelStep;
        return v * level;
    }

    /** The cascade's response (dB) at `hz` with the current gains (tests, research). */
    double responseDb (double hz) const noexcept;
    const BandLayout& layout() const noexcept { return bandLayout; }

private:
    void updateCoefficients() noexcept;

    struct Coef { float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, boost = 0.0f; };
    struct State { float ic1 = 0.0f, ic2 = 0.0f; };
    double sampleRate = 48000.0;
    int bands = 0;
    BandLayout bandLayout;
    std::array<double, BandLayout::maxBands> g {};           ///< tan (pi f / fs) per band
    std::array<double, BandLayout::maxBands * BandLayout::maxBands> solve {};   ///< target dB -> band gain dB
    std::array<double, BandLayout::maxBands> gainDb {}, targetDb {};
    bool standard = false;   ///< the standard layout: the descriptor's band values apply
    std::array<Coef, BandLayout::maxBands> coef {};
    std::array<State, BandLayout::maxBands> state {};
    float level = 1.0f, levelTarget = 1.0f, levelStep = 0.0f;
    /** The loudness: the oscillator's power in and the cascade's out, measured over about
        120 ms (exact for any wave, detune, tone or envelope); the model's estimate until then. */
    float energyIn = 0.0f, energyOut = 0.0f;
    double powerIn = 0.0, powerOut = 0.0, measured = 0.0;
    bool engaged = false, fresh = true;
};

/**
    Experiment C: a real-time channel vocoder. The carrier (the oscillator) and the contour (the
    voice's own sample output) are split into the same bands; each carrier band takes the
    contour band's envelope; the result is levelled to the carrier. `amount` blends it in.
*/
class Vocoder
{
public:
    void prepare (double sampleRate, int bands = 24, bool perceptual = true);
    void reset() noexcept;
    float process (float carrier, float contour, float amount) noexcept;

private:
    struct Band
    {
        double g = 0.0, k = 1.0, a1 = 1.0, a2 = 0.0, a3 = 0.0;
        float c1 = 0.0f, c2 = 0.0f, m1 = 0.0f, m2 = 0.0f;   ///< carrier and contour filter states
        float carrierEnv = 0.0f, contourEnv = 0.0f;
    };
    float bandpass (const Band& b, float x, float& s1, float& s2) const noexcept;
    std::array<Band, BandLayout::maxBands> bank {};
    int bands = 0;
    float up = 0.0f, down = 0.0f, levelUp = 0.0f;
    float inLevel = 0.0f, outLevel = 0.0f;
};

} // namespace osp::imprint
