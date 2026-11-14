#pragma once

#include "EffectRack.h"
#include "InstrumentHost.h"

#include <optional>

namespace osp::plugin
{

/**
    CHARACTER's full rack (rack::Kind::filter; ANDOR OSP's macro row and the FX routing view):
    the character filter in the standard 214 px module, built as ANDOR ANALOG's FILTER rack is
    (a ParameterRack wearing the voice filter's own enamel, synthIdentity (voiceFilter)) - the
    filter's TYPE in the selector row, one row of controls (CHARACTER, MIN, MAX, RESO, DRIVE,
    and its envelope ENV, ATTACK, DECAY) and the response its popover draws. The same
    parameters as the popover (`character`, `character.*`): a view, never a new sound.
*/
std::unique_ptr<rack::Module> createCharacterRack (InstrumentHost& host);

/** What the CHARACTER rack's graph shows (tests): LIVE follows the most recent sounding voice's
    filter (InstrumentHost::telemetryEngine), PREVIEW a silent C4 after an edit with nothing
    playing, IDLE the knobs. Cutoffs in Hz; `drawnHz` is the eased cutoff on screen. */
struct CharacterGraphState
{
    enum class Mode { idle, live, preview };
    Mode mode = Mode::idle;
    int note = 60, velocity = 100, stage = 0;   ///< stage: 0 at rest, 1 attack, 2 decay
    double baseHz = 0.0, nowHz = 0.0, drawnHz = 0.0, reachHz = 0.0, peakHz = 0.0;
};
/** The CHARACTER rack's graph state, or nullopt when `module` is not that rack. */
std::optional<CharacterGraphState> characterGraphState (rack::Module& module);
/** Tests: one graph update as if `seconds` had passed. */
void advanceCharacterGraph (rack::Module& module, double seconds);

} // namespace osp::plugin
