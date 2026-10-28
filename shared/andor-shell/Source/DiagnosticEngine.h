#pragma once

#include "ShellEngine.h"

#include "audio/sampler/BaselineSampler.h"

#include <array>
#include <atomic>
#include <vector>

namespace andor::shell
{

/**
    The shell's labelled stand-in engine, never a product: it proves the integration boundary
    (sources, parameters, MIDI, wheels) with the simplest honest sound.
      - A layer with a sound plays it with baseline A, OSP's plain chromatic resampler
        (src/audio/sampler), at the layer's LEVEL, PAN and TUNE, MUTE and SOLO, under the
        AMP envelope and the master volume.
      - With no sound loaded, a held key plays a quiet sine TEST TONE at its pitch.
    It reads nothing else, so the shell shows every other control disabled, and the footer
    says what it is.
*/
class DiagnosticEngine final : public Engine
{
public:
    DiagnosticEngine();

    Description describe() const override;
    juce::StringArray boundParameters() const override;
    void prepare (double sampleRate, int maximumBlockSize, juce::AudioProcessorValueTreeState& parameters) override;
    void setSource (int layer, const osp::plugin::LoadedInstrument* source) noexcept override;
    void process (juce::AudioBuffer<float>& buffer, const juce::MidiBuffer& midi, const Performance& performance) noexcept override;
    int activeNotes() const noexcept override { return sounding.load (std::memory_order_relaxed); }

    static constexpr int layers = 3;

private:
    void handle (const juce::MidiMessage& m) noexcept;
    void renderSegment (juce::AudioBuffer<float>& out, int start, int count) noexcept;
    void updateSettings (const Performance& performance) noexcept;

    struct LayerParams
    {
        std::atomic<float>* level = nullptr;
        std::atomic<float>* pan = nullptr;
        std::atomic<float>* tune = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
    };
    std::array<LayerParams, layers> layerParams;
    std::atomic<float>* attack = nullptr;
    std::atomic<float>* decay = nullptr;
    std::atomic<float>* sustainLevel = nullptr;
    std::atomic<float>* release = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* velocityRange = nullptr;
    std::atomic<float>* fineTune = nullptr;
    std::atomic<float>* bendRange = nullptr;

    std::array<osp::BaselineSampler, layers> samplers;
    std::array<const osp::PlaybackSource*, layers> sources {};
    std::array<float, layers> gainL {}, gainR {};
    juce::AudioBuffer<float> scratch;

    // The test tone: one sine per held key (no sound loaded).
    struct Tone
    {
        int note = -1;
        double phase = 0.0, increment = 0.0;
        float level = 0.0f, target = 0.0f, velocity = 0.0f;
    };
    std::array<Tone, 16> tones {};
    float toneAttackStep = 0.0f, toneReleaseStep = 0.0f, outputGain = 1.0f;
    double sampleRate = 48000.0;
    bool pedalDown = false;
    std::atomic<int> sounding { 0 };
};

} // namespace andor::shell
