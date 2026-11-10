#pragma once

#include "core/AudioData.h"

#include <filesystem>
#include <string>
#include <vector>

namespace osp::research
{

/** One source for the Pedal Loop lab: a recording, or a synthetic stand-in. */
struct PedalLabSource
{
    std::string name;
    AudioData audio;
    bool standIn = false;     ///< synthetic (listening still needs a real recording)
    int note = -1;            ///< the key to play (-1: the source's root)
};

/**
    Synthetic stand-ins for the spec's listening sources (Pedal Loop 1.0, section 16) when the
    owned recordings are not available here: a piano note (inharmonic decay and hammer), a
    nyckelharpa-like bowed note (vibrato, bow noise, sympathetic strings), a bowed cello note, a
    sung vowel, a short sung phrase, a plucked string, an organ chord note and a percussive hit.
    Deterministic (fixed seeds).
*/
std::vector<PedalLabSource> pedalLabStandIns (double sampleRate);

struct PedalLabResult
{
    std::string markdown;   ///< the report (sources, regions, every render's measurements)
    int renders = 0;
    bool allFinite = true;
};

/**
    Renders the Pedal Loop matrix for every source (docs/research/pedal-loop): Normal, Forward
    and Ping-Pong through the spec's performances (pedal before the note, halfway, late, released
    with the key held, after the key, a chord), plus Ping-Pong's turnaround alternatives, and
    measures each: peak, NaN/Inf, the click energy at every loop turn and jump (high-passed
    energy around it against the audio near it), the level step across entering and leaving the
    loop, DC, and the voices left at the end. Writes 24-bit WAVs under `outDir` when `writeAudio`.
*/
PedalLabResult runPedalLoopLab (const std::vector<PedalLabSource>& sources, const std::filesystem::path& outDir, bool writeAudio);

/**
    CPU of the Pedal Loop against Normal (docs/research/pedal-loop): the same notes held under
    the pedal over the same window (every voice sounding in every mode), at 44.1, 48 and 96 kHz,
    1 to 32 voices, one or three layers, with SYNTH Imprint, and with the pedal pumped. Reports
    the share of one core each takes (median of `repeats`).
*/
std::string runPedalLoopBenchmark (const PedalLabSource& source, int repeats = 3);

} // namespace osp::research
