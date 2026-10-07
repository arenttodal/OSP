#pragma once

#include <algorithm>
#include <array>

namespace osp
{

/**
    REIMAGINED modes: five ways of reinterpreting a layer's recording. The layer's
    REIMAGINED amount (LayerSettings::reimagined) says how far; the mode says which way.

      KALEIDOSCOPE  refraction: the original Reimagined algorithm (per-voice continuation,
                    saturation, doubling and grains + the layer's ReimaginedStage).
      TAPE FRAME    mechanical memory: the source laid onto a finite tape frame and
                    played by tape speed, with wow, flutter, age and ghost passes.
      TOYBOX        primitive digital mutation: a low-rate, low-resolution sampler that
                    turns and wanders through its memory, modulated by the source itself.
      MOSAIC        harmonic reconstruction: the source rebuilt from its harmonic frames
                    (partials + residual noise), played at any pitch.
      MIRAGE        early-digital weight: variable-clock playback whose fidelity follows
                    the note, into a driven resonant 4-pole low-pass.

    Every mode keeps its own settings (0..1 continuous, stepped choices), so switching
    away and back restores them. Plain data, real-time safe to copy. Defaults are the
    starting state of a new patch; KALEIDOSCOPE's neutral FOCUS / SPREAD (0.5) is exactly
    the algorithm every older session was made with.
*/
enum class ReimaginedMode { kaleidoscope, tapeFrame, toybox, mosaic, mirage };
inline constexpr int reimaginedModeCount = 5;

enum class TapeFrameLength { shortFrame, classic, longFrame };
enum class ToyboxPlay { forward, turn, chaos };
enum class MosaicModel { pure, textured };
enum class MirageTone { dark, open };

struct KaleidoscopeParams
{
    double focus = 0.5;    ///< recognizable (0) <-> abstract (1); 0.5 = the original balance
    double spread = 0.5;   ///< stereo / variation width; 0.5 = the original width
};

struct TapeFrameParams
{
    double age = 0.35;         ///< bandwidth loss, saturation, compression, wear
    double stability = 0.25;   ///< inverse mechanical consistency (wow, flutter, start variation)
    TapeFrameLength frame = TapeFrameLength::classic;
};

struct ToyboxParams
{
    double motion = 0.30;    ///< turning, region activity, source-derived modulation
    double digital = 0.40;   ///< rate, resolution, bandwidth, interpolation
    ToyboxPlay play = ToyboxPlay::turn;
};

struct MosaicParams
{
    double detail = 0.60;   ///< partial count and spectral detail
    double motion = 0.45;   ///< travel through the source's harmonic frames
    MosaicModel model = MosaicModel::textured;
};

struct MirageParams
{
    double clock = 0.45;    ///< variable-rate / old-digital intensity
    double filter = 0.50;   ///< the analog filter's character, resonance and drive
    MirageTone tone = MirageTone::dark;
};

/** One layer's REIMAGINED mode and every mode's settings (the amount lives in LayerSettings). */
struct ReimaginedSettings
{
    ReimaginedMode mode = ReimaginedMode::kaleidoscope;
    KaleidoscopeParams kaleidoscope;
    TapeFrameParams tapeFrame;
    ToyboxParams toybox;
    MosaicParams mosaic;
    MirageParams mirage;
};

/** Whether a mode's prepared source data exists for a model (ReimaginedAnalysis). */
enum class ReimaginedAnalysisStatus { notNeeded, pending, ready, failed };

namespace reimagined
{
    inline const char* modeName (ReimaginedMode mode) noexcept
    {
        switch (mode)
        {
            case ReimaginedMode::kaleidoscope: return "KALEIDOSCOPE";
            case ReimaginedMode::tapeFrame:    return "TAPE FRAME";
            case ReimaginedMode::toybox:       return "TOYBOX";
            case ReimaginedMode::mosaic:       return "MOSAIC";
            case ReimaginedMode::mirage:       return "MIRAGE";
        }
        return "KALEIDOSCOPE";
    }

    inline ReimaginedMode modeFromIndex (int index) noexcept
    {
        return static_cast<ReimaginedMode> (std::clamp (index, 0, reimaginedModeCount - 1));
    }

    /** 0 below lo, 1 above hi, a smoothstep between: the building block of every amount curve. */
    inline double ramp (double x, double lo, double hi) noexcept
    {
        if (hi <= lo)
            return x >= hi ? 1.0 : 0.0;
        const double t = std::clamp ((x - lo) / (hi - lo), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    /** TAPE FRAME frame lengths in seconds of tape at the root (lower notes play longer). */
    inline double frameSeconds (TapeFrameLength frame) noexcept
    {
        switch (frame)
        {
            case TapeFrameLength::shortFrame: return 3.6;
            case TapeFrameLength::classic:    return 7.6;
            case TapeFrameLength::longFrame:  return 11.4;
        }
        return 7.6;
    }
}

} // namespace osp
