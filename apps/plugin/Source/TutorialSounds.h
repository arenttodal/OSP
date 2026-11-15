#pragma once

#include <juce_core/juce_core.h>

#include <optional>

/**
    The tutorials' example sounds (ANDOR Learn in ANDOR OSP). They are synthesised here, from
    fixed seeds, so they are ours to ship, need no download and sound the same everywhere:

      bowed     a sustained bowed-string-like note (C3): chromatic playing, GRANULAR, REIMAGINED
      pluck     a short plucked string (A3): LIFE and repeated notes
      voice     a sung vowel (A3): texture and register
      layered   the bowed note and the voice on two layers
      pluckSet  plucks at G3, C4, G4 and D5, two takes of each: a multi-sample instrument

    Written once as 24-bit WAV files into a folder of the musician's (Tutorial Sounds, beside
    OSP's other user files), so they load, save and recall through the normal sample path like
    any recording. They are not recordings of a real instrument and the tutorials never say so.
*/
namespace osp::plugin::tutorial
{

enum class Example
{
    bowed,
    pluck,
    voice,
    layered,
    pluckSet
};

juce::String idOf (Example);
std::optional<Example> fromId (const juce::String& id);
/** "Bowed note": what the tutorials call it. */
juce::String nameOf (Example);

/** Where they are written: $OSP_TUTORIAL_DIR, else <user application data>/ANDOR/OSP/Tutorial Sounds. */
juce::File folder();

/** The example's files (one per layer; pluckSet's are one set), written first if missing.
    Empty if a file could not be written (`error` says why). */
juce::Array<juce::File> files (Example, juce::String& error);

} // namespace osp::plugin::tutorial
