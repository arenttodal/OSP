// The CHARACTER rack's graph follows what the filter does, as ANDOR ANALOG's FILTER graph does:
// LIVE (the most recent sounding voice's cutoff from the engine's CharacterTaps), PREVIEW (a
// silent C4 through the envelope after an edit with nothing playing), IDLE (the knobs).
// Included at the end of PluginProcessorTests.cpp.

#include "CharacterRack.h"

namespace
{
    void setPlain (OspAudioProcessor& p, const char* id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    void runBlocks (OspAudioProcessor& p, juce::AudioBuffer<float>& buffer, int blocks, juce::MidiBuffer first = {})
    {
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer midi;
            if (b == 0)
                midi = first;
            p.processBlock (buffer, midi);
        }
    }
}

TEST_CASE ("character graph: idle shows the knobs; an edit with nothing playing previews the envelope; a note is followed live", "[plugin][character-graph]")
{
    using Mode = osp::plugin::CharacterGraphState::Mode;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    setPlain (p, "character", 30.0f);
    setPlain (p, "character.envAmount", 0.0f);
    setPlain (p, "character.envAttack", 200.0f);
    setPlain (p, "character.envDecay", 400.0f);
    ui->toggleRackFromLabel (osp::plugin::rack::Kind::filter);
    auto* module = ui->effectsRack().module (osp::plugin::rack::Kind::filter);
    REQUIRE (module != nullptr);
    auto state = [&] {
        const auto s = osp::plugin::characterGraphState (*module);
        REQUIRE (s.has_value());
        return *s;
    };

    // IDLE: the knobs' cutoff; nothing moves.
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    auto s = state();
    CHECK (s.mode == Mode::idle);
    CHECK (s.nowHz == Approx (s.baseHz));
    const double rest = s.baseHz;

    // PREVIEW: ENV up with nothing playing - the cutoff rises over ATTACK and falls back over DECAY.
    setPlain (p, "character.envAmount", 60.0f);
    osp::plugin::advanceCharacterGraph (*module, 0.1);
    s = state();
    CHECK (s.mode == Mode::preview);
    CHECK (s.stage == 1);
    CHECK (s.nowHz > rest * 1.5);
    CHECK (s.nowHz < s.reachHz);
    CHECK (s.reachHz > s.nowHz);
    osp::plugin::advanceCharacterGraph (*module, 0.1);   // ATTACK's end: the top
    s = state();
    CHECK (s.nowHz == Approx (s.reachHz).epsilon (0.05));
    const double top = s.nowHz;
    osp::plugin::advanceCharacterGraph (*module, 0.2);   // DECAY: back towards the knob
    s = state();
    CHECK (s.stage == 2);
    CHECK (s.nowHz < top * 0.8);
    for (int i = 0; i < 20; ++i)
        osp::plugin::advanceCharacterGraph (*module, 0.1);
    s = state();
    CHECK (s.mode == Mode::idle);
    CHECK (s.nowHz == Approx (rest));
    CHECK (s.reachHz > rest);   // (the envelope's reach stays drawn, dashed)

    // LIVE: a note through the engine - its own cutoff, rising with its envelope, then its decay.
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 0);
    runBlocks (p, buffer, 10, on);   // ~53 ms into ATTACK
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    s = state();
    CHECK (s.mode == Mode::live);
    CHECK (s.note == 64);
    CHECK (s.velocity == 100);
    CHECK (s.stage == 1);
    CHECK (s.nowHz > s.baseHz * 1.2);
    CHECK (s.nowHz < s.reachHz);
    const double early = s.nowHz;
    runBlocks (p, buffer, 40);       // past ATTACK, into DECAY
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    s = state();
    CHECK (s.mode == Mode::live);
    CHECK (s.stage == 2);
    CHECK (s.peakHz > early);
    CHECK (s.peakHz == Approx (s.reachHz).epsilon (0.05));
    // CHARACTER turned while the note plays: the live base follows (no new preview).
    setPlain (p, "character", 70.0f);
    runBlocks (p, buffer, 20);
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    CHECK (state().mode == Mode::live);
    CHECK (state().baseHz > s.baseHz * 1.5);
    // The drawn cutoff eases towards the truth (drawing only) and arrives.
    for (int i = 0; i < 10; ++i)
        osp::plugin::advanceCharacterGraph (*module, 0.03);
    CHECK (state().drawnHz == Approx (state().nowHz).epsilon (0.01));
    // Released and ended: back to IDLE.
    juce::MidiBuffer off;
    off.addEvent (juce::MidiMessage::allNotesOff (1), 0);
    runBlocks (p, buffer, 1500, off);   // ~8 s: the note's release and tail end
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    CHECK (state().mode == Mode::idle);
    p.releaseResources();
}

TEST_CASE ("character graph: screenshots (OSP_CHARACTER_SHOTS)", "[.][character-graph-shots]")
{
    // On demand: OSP_CHARACTER_SHOTS=<folder> osp_plugin_tests "[character-graph-shots]".
    const char* dir = std::getenv ("OSP_CHARACTER_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (60), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    setPlain (p, "character", 25.0f);
    setPlain (p, "character.resonance", 35.0f);
    setPlain (p, "character.envAmount", 57.0f);
    setPlain (p, "character.envAttack", 120.0f);
    setPlain (p, "character.envDecay", 700.0f);
    ui->toggleRackFromLabel (osp::plugin::rack::Kind::filter);
    auto* module = ui->effectsRack().module (osp::plugin::rack::Kind::filter);
    REQUIRE (module != nullptr);
    auto shoot = [&] (const juce::String& name) {
        ui->refreshNow();
        module->repaint();
        const auto area = ui->getLocalArea (module, module->getLocalBounds());
        auto image = e->createComponentSnapshot (area, true, 2.0f);
        juce::FileOutputStream out (juce::File (dir).getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    osp::plugin::advanceCharacterGraph (*module, 0.03);
    shoot ("character-1-idle");
    setPlain (p, "character.envAmount", 60.0f);
    osp::plugin::advanceCharacterGraph (*module, 0.06);
    for (int i = 0; i < 6; ++i)
        osp::plugin::advanceCharacterGraph (*module, 0.01);
    shoot ("character-2-preview-attack");
    p.prepareToPlay (48000.0, 256);
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer on;
    on.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 0);
    for (int i = 0; i < 40; ++i)
        osp::plugin::advanceCharacterGraph (*module, 0.1);   // (the preview ends first)
    runBlocks (p, buffer, 60, on);
    for (int i = 0; i < 10; ++i)
        osp::plugin::advanceCharacterGraph (*module, 0.03);
    shoot ("character-3-live-decay");
    p.releaseResources();
}
