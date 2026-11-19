#pragma once

namespace osp
{

/** PEDAL LOOP (Pedal Loop 1.0, docs/research/pedal-loop): what the sustain pedal does to a
    One Shot note. Saved as a choice index: append only. */
enum class PedalLoopMode
{
    normal,    ///< the sustain pedal as it always was
    forward,   ///< the pedal holds the note in a forward loop
    pingPong   ///< the pedal holds the note in a forward/reverse loop
};

} // namespace osp
