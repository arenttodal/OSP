// Headless checks of the processor, run by ctest (locally and in CI before anything is
// packed): it loads, makes sound (instrument) or passes sound (effect), stays finite, and
// its state comes back. Add a check here for every bug you fix.
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool ok, const char* what)
    {
        std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what);
        if (! ok)
            ++failures;
    }

    float peakOf (const juce::AudioBuffer<float>& b)
    {
        float peak = 0.0f;
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            peak = std::max (peak, b.getMagnitude (ch, 0, b.getNumSamples()));
        return peak;
    }

    bool finite (const juce::AudioBuffer<float>& b)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (ch, i)))
                    return false;
        return true;
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        PluginProcessor processor;
        const int block = 256;
        processor.setPlayConfigDetails (processor.getTotalNumInputChannels(), 2, rate, block);
        processor.prepareToPlay (rate, block);

        juce::AudioBuffer<float> buffer (std::max (2, processor.getTotalNumInputChannels()), block);
        float loudest = 0.0f;
        bool allFinite = true;
        for (int n = 0; n < 40; ++n)
        {
            juce::MidiBuffer midi;
            if (n == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            // An effect gets a test tone in; an instrument makes its own.
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < block; ++i)
                    buffer.setSample (ch, i, 0.25f * std::sin (0.05f * static_cast<float> (n * block + i)));
            processor.processBlock (buffer, midi);
            loudest = std::max (loudest, peakOf (buffer));
            allFinite = allFinite && finite (buffer);
        }
        std::printf ("-- %.0f Hz: peak %.3f\n", rate, loudest);
        check (loudest > 1.0e-3f, "makes sound");
        check (loudest < 2.0f, "stays below +6 dBFS");
        check (allFinite, "no NaN or infinity");
        processor.releaseResources();
    }

    // State: a changed parameter comes back in a fresh instance.
    {
        PluginProcessor a;
        a.parameters.getParameter ("gain")->setValueNotifyingHost (0.25f);
        juce::MemoryBlock state;
        a.getStateInformation (state);
        PluginProcessor b;
        b.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
        check (std::abs (b.parameters.getParameter ("gain")->getValue() - 0.25f) < 1.0e-4f, "state is recalled");
    }

    // The editor opens and closes (no host needed; skipped without a display, e.g. on a
    // headless Linux machine).
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
    {
        PluginProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
        check (editor != nullptr && editor->getWidth() > 0, "editor opens");
    }

    std::printf (failures == 0 ? "all checks passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
