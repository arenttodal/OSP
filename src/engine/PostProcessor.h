#pragma once

#include "engine/ShelfFilter.h"

#include <array>
#include <cstdint>
#include <vector>

namespace osp
{

struct InstrumentModel;
struct Macros;

/**
    Global stage after the voices (spec §11, §12, §46):

      CHARACTER  spectral-envelope transformation: the source's own body resonances
                 move (cut at the original frequency, boost at the shifted one) and
                 the spectrum tilts: smaller/brighter above 0.5, larger/darker below.
      REIMAGINED a sympathetic resonator bank tuned to the source's partials and body
                 peaks, re-excited by whatever is played (more and longer towards
                 Reimagined).
      SPACE      width (M/S), decorrelation for narrow sources, and a small diffuse
                 ambience (feedback delay network with damping).

    Macro values are smoothed and coefficients recomputed at control rate, so moving a
    macro never clicks. prepare() allocates; everything else is real-time safe.
*/
class PostProcessor
{
public:
    void prepare (double sampleRate, int maximumBlockSize);
    void reset() noexcept;

    /** Pick up the resonances of a newly published model (real-time safe). */
    void setModel (const InstrumentModel* model) noexcept;
    void setMacros (const Macros& macros) noexcept;

    void process (float* left, float* right, int numSamples) noexcept;

    static constexpr int maxPeaks = 3;
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

    void updateCoefficients() noexcept;
    static void peaking (Biquad& f, double sampleRate, double hz, double q, double gainDb) noexcept;
    static void bandpass (Biquad& f, double sampleRate, double hz, double t60Seconds) noexcept;

    double sampleRate = 48000.0;
    static constexpr int controlInterval = 32;
    int countdown = 0;

    // Targets and smoothed values
    double characterTarget = 0.5, character = 0.5;
    double spaceTarget = 0.0, space = 0.0;
    double reimaginedTarget = 0.0, reimagined = 0.0;
    double appliedCharacter = -1.0, appliedSpace = -1.0, appliedReimagined = -1.0;

    // Model-derived
    int numPeaks = 0;
    std::array<double, maxPeaks> peakHz {};
    std::array<double, resonators> resonatorHz {};
    int numResonators = 0;
    double sourceWidth = 0.5;
    bool modelDirty = true;
    const InstrumentModel* model = nullptr;

    // CHARACTER
    std::array<Biquad, maxPeaks * 2> characterL, characterR;
    ShelfFilter tiltHighL, tiltHighR, tiltLowL, tiltLowR;
    bool characterActive = false;

    // Resonators (mono-summed excitation, stereo spread output)
    // First half: the source's partials and body; second half: a fifth above them
    // (harmonic remapping towards the Reimagined end, spec §12).
    std::array<Biquad, 2 * resonators> resonatorBank;
    float resonanceMix = 0.0f, remapMix = 0.0f;

    // SPACE
    float sideGain = 1.0f;
    float decorrelation = 0.0f;
    std::array<float, 4> allpassState {};
    std::array<int, 4> allpassDelay {};
    std::vector<float> allpassBuffer;
    std::array<int, 4> allpassOffset {};
    std::array<int, 4> allpassWrite {};
    static constexpr int fdnLines = 4;
    std::vector<float> fdnBuffer;
    std::array<int, fdnLines> fdnLength {};
    std::array<int, fdnLines> fdnOffset {};
    std::array<int, fdnLines> fdnWrite {};
    std::array<float, fdnLines> fdnLowpass {};
    float fdnFeedback = 0.0f;
    float fdnDamping = 0.3f;
    float reverbMix = 0.0f;
    float spaceTrim = 1.0f;         // keeps loudness roughly constant as SPACE adds width and ambience
};

} // namespace osp
