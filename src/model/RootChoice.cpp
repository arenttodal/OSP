#include "model/RootChoice.h"

#include "core/PitchMath.h"

namespace osp
{

RootChoice chooseRoot (const AnalysisData* analysis, std::optional<double> overrideMidi)
{
    RootChoice choice;
    const bool hasEstimate = analysis != nullptr && analysis->pitch.midiNote >= 0 && analysis->pitch.fundamentalHz > 0.0;

    if (hasEstimate && analysis->pitch.detected)
        choice.sourceF0Hz = analysis->pitch.fundamentalHz;

    if (overrideMidi)
    {
        choice.rootMidi = *overrideMidi;
        choice.origin = "override";
    }
    else if (hasEstimate)
    {
        choice.rootMidi = hzToMidi (analysis->pitch.fundamentalHz);
        choice.origin = analysis->pitch.detected ? "analysis" : "analysis-low-confidence";
    }
    else
    {
        choice.rootMidi = 60.0;
        choice.origin = "fallback";
    }
    return choice;
}

std::string describeCharacter (const AnalysisData& a)
{
    if (a.envelope.maxRmsDbfs < -100.0)
        return "SILENT";

    const bool sustained = a.envelope.endsWhileSounding || a.envelope.decaySlopeDbPerSecond > -6.0;
    std::string tone;
    if (a.spectral.harmonicEnergyRatio >= 0.6 && a.pitch.detected)
        tone = "TONAL";
    else if (a.spectral.meanFlatness >= 0.3)
        tone = "NOISY";
    else
        tone = "TEXTURE";

    std::string text = std::string (sustained ? "SUSTAINED" : "DECAYING") + " \xC2\xB7 " + tone;
    if (a.pitch.vibrato)
        text += " \xC2\xB7 VIBRATO";
    else if (a.envelope.tremolo)
        text += " \xC2\xB7 TREMOLO";
    return text;
}

} // namespace osp
