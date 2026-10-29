#pragma once

// The ANDOR visual regression's states: one scenario that ANDOR OSP's tests and every
// product built from the ANDOR Instrument Shell run the same way (the processor and editor
// types differ; the calls are the same), so the two renders can be compared pixel by pixel.
// The approved dynamic regions (the masks) are the only places the two may differ.

#include "CeramicSkin.h"
#include "Design.h"
#include "EngineCard.h"

#include "audio/utility/TestSignals.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

namespace visualstates
{
    /** The stand-in sounds (deterministic test signals: no licensed audio). */
    inline juce::Array<juce::File> writeSounds (const juce::File& dir)
    {
        using namespace osp;
        juce::Array<juce::File> files;
        const std::pair<const char*, AudioData> sounds[] = {
            { "A A4.wav", testsignals::vowel (midiToHz (69), 2.1, 48000.0, 3) },
            { "B A4.wav", testsignals::pluck (440.0, 3.2, 48000.0, 4) },
            { "C E4.wav", testsignals::pluck (329.63, 2.4, 48000.0, 5) },
        };
        for (const auto& [name, audio] : sounds)
        {
            const auto file = dir.getChildFile (name);
            std::string error;
            io::writeAudioFile (std::filesystem::path (file.getFullPathName().toStdString()), audio, io::SampleFormat::pcm24, error);
            files.add (file);
        }
        return files;
    }

    inline void savePng (const juce::Image& image, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    /** The states, in reference pixels unless the name says otherwise. */
    inline juce::StringArray names()
    {
        return { "00-empty", "01-one-layer", "02-two-layers", "03-three-layers", "03-three-layers@2x",
                 "04-window-869", "04-window-1810", "05-mod-tab-amp", "05-mod-tab-env", "05-mod-tab-lfo1",
                 "05-mod-tab-lfo2", "05-mod-tab-vel", "05-mod-tab-note", "07-card-eq", "07-card-dampen",
                 "07-card-granular", "08-keys-held", "09-arp-open" };
    }

    /**
        Renders every state into `out` (one PNG each). `Processor` and `Editor` are ANDOR OSP's
        or the shell's; `makeProcessor` gives a fresh processor.
    */
    template <typename Processor, typename Editor>
    void render (const juce::File& out, const juce::File& scratch, const std::function<std::unique_ptr<Processor>()>& makeProcessor)
    {
        using namespace osp::plugin;
        SourceDisplay::softwareCaches = true;   // bit-repeatable cached pictures on every platform (see there)
        const auto sounds = writeSounds (scratch);
        auto shoot = [&] (juce::AudioProcessorEditor& e, Editor& u, const juce::String& name, float scale = 1.0f) {
            u.refreshNow();
            // JUCE's own software renderer, not the platform's: by default the snapshot is a native
            // image, CoreGraphics on macOS, where two renders of the same state differed by a channel
            // level or two now and then (runs 151 and 164). On Linux the native image is this
            // renderer already, so the pictures there are unchanged.
            savePng (e.createComponentSnapshot (e.getLocalBounds(), true, scale, juce::SoftwareImageType()), out.getChildFile (name + ".png"));
        };
        auto open = [] (Processor& p) {
            std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
            e->setSize (1448, 1086);
            return e;
        };
        auto load = [&sounds] (Processor& p, int count) {
            juce::Array<juce::File> files;
            for (int i = 0; i < count; ++i)
                files.add (sounds[i]);
            p.addLayers (files);
            p.waitForLoads (30000);
            p.pollLoads();
        };

        {
            auto p = makeProcessor();
            auto e = open (*p);
            shoot (*e, dynamic_cast<Editor&> (*e), "00-empty");
        }
        for (const int count : { 1, 2 })
        {
            auto p = makeProcessor();
            auto e = open (*p);
            load (*p, count);
            shoot (*e, dynamic_cast<Editor&> (*e), count == 1 ? "01-one-layer" : "02-two-layers");
        }

        auto p = makeProcessor();
        auto editor = open (*p);
        auto& ui = dynamic_cast<Editor&> (*editor);
        load (*p, 3);
        shoot (*editor, ui, "03-three-layers");
        shoot (*editor, ui, "03-three-layers@2x", 2.0f);
        for (const int width : { 869, 1810 })
        {
            editor->setSize (width, juce::roundToInt (static_cast<float> (width) * 1086.0f / 1448.0f));
            shoot (*editor, ui, "04-window-" + juce::String (width));
        }
        editor->setSize (1448, 1086);
        const char* tabs[] = { "amp", "env", "lfo1", "lfo2", "vel", "note" };
        for (int t = 0; t < 6; ++t)
        {
            ui.modulationPanel().showTab (t);
            shoot (*editor, ui, "05-mod-tab-" + juce::String (tabs[t]));
        }
        ui.modulationPanel().showTab (0);
        ui.engineCard (0).setEqOpen (true);
        shoot (*editor, ui, "07-card-eq");
        if (auto* eq = ui.engineCard (0).eqEditorComponent())
        {
            eq->showDampen (true);
            shoot (*editor, ui, "07-card-dampen");
        }
        ui.engineCard (0).setEqOpen (false);
        p->setParameterValue ("layerA.sourceMode", 1.0f);
        ui.refreshNow();
        // The same state in both: ANDOR OSP places POS from its analysis when a layer first
        // turns Granular (its engine's choice, not the interface's), so POS is set here.
        p->setParameterValue ("layerA.granular.position", 50.0f);
        ui.refreshNow();
        ui.engineCard (0).showGranularControls (true, true);
        shoot (*editor, ui, "07-card-granular");
        ui.engineCard (0).showGranularControls (false, true);
        p->setParameterValue ("layerA.sourceMode", 0.0f);
        for (int n : { 48, 52, 55, 60, 64 })
            p->keyboardState.noteOn (1, n, 0.8f);
        shoot (*editor, ui, "08-keys-held");
        p->keyboardState.allNotesOff (1);
        ui.setArpExpanded (true);
        editor->setSize (1448, juce::roundToInt (ui.instrumentHeight()));
        shoot (*editor, ui, "09-arp-open");
        ui.setArpExpanded (false);
    }

    /** The approved dynamic regions, in reference pixels: where a product may differ from
        ANDOR OSP. Nothing else may. */
    struct Mask
    {
        juce::String name, why;
        juce::Rectangle<float> area;
    };
    inline std::vector<Mask> masks (float arpShift = 0.0f)
    {
        using namespace osp::plugin::design::layout;
        // The identity: "ANDOR" + the product word, and the tagline under it (from the
        // housing's left edge to just before the preset bar).
        const auto identity = juce::Rectangle<float>::leftTopRightBottom (logo.x - 6.0f, logo.y - 58.0f, presetBar.getX() - 6.0f, logoSubtitle.getBottom() + 6.0f);
        // The preset's name inside the bar (each product's presets are its own).
        const auto presetName = presetBar.reduced (46.0f, 4.0f);
        // The footer's status line (it names the engine; the sounds' stages differ).
        const auto statusLine = status.translated (0.0f, arpShift).expanded (2.0f, 2.0f);
        return { { "identity", "the product's name and tagline", identity },
                 { "preset-name", "the product's own presets", presetName },
                 { "status-line", "the engine's and the sounds' state", statusLine } };
    }
}
