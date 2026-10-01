#pragma once

#include "core/AudioData.h"

#include <cstdint>

namespace osp::testsignals
{

/**
    Programmatic signals with known ground truth (pitch, envelope, stereo).
    Used by unit tests, golden regressions, benchmarks and `research-renderer --generate-test-signals`.
    All generators are deterministic.
*/

AudioData sine (double frequencyHz, double seconds, double sampleRate, double amplitude = 0.5, int channels = 1);

/** Additive band-limited sawtooth (harmonics up to 0.45 * sampleRate). */
AudioData saw (double frequencyHz, double seconds, double sampleRate, double amplitude = 0.5, int channels = 1);

AudioData whiteNoise (double seconds, double sampleRate, double amplitude, std::uint64_t seed, int channels = 1);

AudioData silence (double seconds, double sampleRate, int channels = 1);

/** Single sample of `amplitude` at `atSeconds`. */
AudioData impulse (double seconds, double sampleRate, double atSeconds = 0.1, double amplitude = 0.9, int channels = 1);

/** Sine with sinusoidal frequency modulation (depth = peak deviation in cents). */
AudioData vibratoSine (double frequencyHz, double rateHz, double depthCents, double seconds, double sampleRate,
                       double amplitude = 0.5, int channels = 1);

/** Sine with sinusoidal amplitude modulation (depth = peak-to-peak in dB). */
AudioData tremoloSine (double frequencyHz, double rateHz, double depthDb, double seconds, double sampleRate,
                       double amplitude = 0.5, int channels = 1);

/** Karplus-Strong plucked string (decaying, slightly inharmonic, transient-rich). */
AudioData pluck (double frequencyHz, double seconds, double sampleRate, std::uint64_t seed, double amplitude = 0.7,
                 int channels = 1);

/**
    Vowel-like harmonic tone: additive harmonics shaped by three formant peaks, gentle
    vibrato and a soft attack. Stereo output is lightly decorrelated.
*/
AudioData vowel (double frequencyHz, double seconds, double sampleRate, std::uint64_t seed, double amplitude = 0.5,
                 int channels = 2);

/** Linear fade-in/out (applied in place) to avoid edge clicks in synthetic material. */
void applyFades (AudioData& audio, double fadeInSeconds, double fadeOutSeconds);

} // namespace osp::testsignals
