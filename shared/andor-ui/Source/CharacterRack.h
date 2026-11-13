#pragma once

#include "EffectRack.h"
#include "InstrumentHost.h"

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

} // namespace osp::plugin
