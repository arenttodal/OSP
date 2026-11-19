// STRUM's interface (ST4): the key beside SATELLITE (SATELLITE's design: the light switches, the
// name opens; neither does the other), its light's three states (off, on, on but unavailable
// without CHORD), EDIT A | B | C deciding whose STRUM it acts on, and the panel.

#include "Binding.h"
#include "StrumPanel.h"

TEST_CASE ("strum ui: the key beside SATELLITE switches the edited card's STRUM and opens its panel; never both", "[plugin][strum][strum-ui]")
{
    using Editor = osp::plugin::OspAudioProcessorEditor;
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<Editor*> (e.get());
    REQUIRE (ui != nullptr);
    ui->arpControl().onToggleEditor();
    REQUIRE (ui->isArpExpanded());
    auto& arp = ui->arpInlinePanel();
    auto* key = arp.strumKey();
    auto* satellite = arp.satelliteKey();
    REQUIRE (key != nullptr);
    REQUIRE (satellite != nullptr);
    CHECK (key->isVisible());
    // Right of SATELLITE on the title row, inside the panel, not overlapping it.
    CHECK (key->getX() > satellite->getRight());
    CHECK (arp.getLocalBounds().contains (key->getBounds()));
    CHECK (osp::plugin::ArpInlinePanel::strumKeyArea().contains (key->getBounds().toFloat()));
    CHECK (key->getY() == satellite->getY());
    CHECK (key->getHeight() == satellite->getHeight());

    // Off by default; the pattern is UP: not available (but off, so nothing to suppress).
    CHECK_FALSE (key->anyOn());
    CHECK_FALSE (key->isAvailable());
    // The light: card A's STRUM on (and the pattern still UP: on, unavailable); never opens.
    key->toggle();
    CHECK (arpValue (p, "arp.strum.on") == 1.0f);
    CHECK (key->anyOn());
    CHECK_FALSE (key->isAvailable());
    CHECK (ui->openPopupIndex() < 0);
    // CHORD: available.
    p.setParameterValue ("arp.pattern", 5.0f);
    key->refresh();
    CHECK (key->isAvailable());
    // Back to UP: the setting is kept (on), only unavailable; CHORD again restores it.
    p.setParameterValue ("arp.pattern", 0.0f);
    key->refresh();
    CHECK (arpValue (p, "arp.strum.on") == 1.0f);
    CHECK_FALSE (key->isAvailable());
    p.setParameterValue ("arp.pattern", 5.0f);
    key->refresh();

    // The name: the panel; opening never switches.
    key->toggle();   // off again
    CHECK (arpValue (p, "arp.strum.on") == 0.0f);
    key->openPanel();
    ui->mouseDownAnywhere (key);
    CHECK (ui->openPopupIndex() == Editor::anchoredPopup);
    CHECK (key->isOpen());
    auto* panel = dynamic_cast<osp::plugin::StrumPanel*> (ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    CHECK (panel->shownCard() == 0);
    CHECK (arpValue (p, "arp.strum.on") == 0.0f);
    CHECK (panel->showsCards());
    // Contextual: FREE shows the milliseconds (knob and typed field), SYNC the note value.
    panel->refreshContent();
    CHECK (panel->spreadKnob().isVisible());
    CHECK (panel->spreadField().isVisible());
    CHECK_FALSE (panel->divisionKnob().isVisible());
    p.setParameterValue ("arp.strum.sync", 1.0f);
    panel->refreshContent();
    CHECK_FALSE (panel->spreadKnob().isVisible());
    CHECK (panel->divisionKnob().isVisible());
    // Typed: 35 ms.
    p.setParameterValue ("arp.strum.sync", 0.0f);
    panel->refreshContent();
    panel->spreadField().commitTyping ("35");
    CHECK (arpValue (p, "arp.strum.ms") == Approx (35.0f).margin (0.1));
    // The shown card's controls; A / B / C show, only the light switches.
    CHECK (osp::plugin::binding::tagsOf (panel->directionControl()).contains ("arp.strum.direction"));
    panel->showCard (1);
    CHECK (osp::plugin::binding::tagsOf (panel->directionControl()).contains ("arp.cardB.strum.direction"));
    CHECK (arpValue (p, "arp.cardB.strum.on") == 0.0f);
    panel->lamp().toggle();
    CHECK (arpValue (p, "arp.cardB.strum.on") == 1.0f);
    CHECK (arpValue (p, "arp.strum.on") == 0.0f);
    key->openPanel();   // (the key again closes it)
    CHECK_FALSE (key->isOpen());
}

TEST_CASE ("strum ui: unlinked, the light and the panel act on the card being edited, not A", "[plugin][strum][strum-ui]")
{
    OspAudioProcessor p;
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    ui->arpControl().onToggleEditor();
    auto& arp = ui->arpInlinePanel();
    auto* key = arp.strumKey();
    REQUIRE (key != nullptr);
    p.setParameterValue ("arp.link", 0.0f);
    arp.setEditCard (2);
    p.setParameterValue ("arp.cardC.pattern", 5.0f);   // C plays CHORD
    CHECK (arp.strumCard() == 2);
    key->toggle();
    CHECK (arpValue (p, "arp.cardC.strum.on") == 1.0f);
    CHECK (arpValue (p, "arp.strum.on") == 0.0f);
    key->refresh();
    CHECK (key->isAvailable());   // C's own pattern
    key->openPanel();
    ui->mouseDownAnywhere (key);
    auto* panel = dynamic_cast<osp::plugin::StrumPanel*> (ui->openPopupPanel());
    REQUIRE (panel != nullptr);
    CHECK (panel->shownCard() == 2);
    key->openPanel();
    // Linked again: A's (every lane strums as A).
    p.setParameterValue ("arp.link", 1.0f);
    arp.refresh();
    CHECK (arp.strumCard() == 0);
}

TEST_CASE ("strum ui: the picture shows the held chord, else a preview, and what the step lets play", "[plugin][strum][strum-ui]")
{
    OspAudioProcessor p;
    p.setParameterValue ("arp.pattern", 5.0f);
    p.setParameterValue ("arp.rate", 2.0f);   // 1/16: 125 ms at 120 BPM
    p.setParameterValue ("arp.strum.ms", 30.0f);
    osp::plugin::StrumDisplay display (p, 0, [] (int note) { return note == 48 || note == 55 || note == 64; });
    auto m = display.model();
    CHECK_FALSE (m.preview);
    CHECK (m.chord == std::vector<int> { 48, 55, 64 });
    CHECK (m.applies);
    CHECK (m.requestedMs == Approx (30.0));
    CHECK (m.playedMs == Approx (30.0));
    CHECK (m.order == std::vector<int> { 0, 1, 2 });
    p.setParameterValue ("arp.strum.direction", 1.0f);
    CHECK (display.model().order == std::vector<int> { 2, 1, 0 });
    p.setParameterValue ("arp.strum.ms", 250.0f);
    m = display.model();
    CHECK (m.requestedMs == Approx (250.0));
    CHECK (m.playedMs < 125.0);   // the step is shorter: what plays
    osp::plugin::StrumDisplay idle (p, 0, [] (int) { return false; });
    CHECK (idle.model().preview);
    CHECK (idle.model().chord == std::vector<int> { 60, 64, 67, 71 });
    p.setParameterValue ("arp.pattern", 0.0f);
    CHECK_FALSE (idle.model().applies);
}

TEST_CASE ("strum ui: screenshots of the key's three states and the panel (OSP_STRUM_SHOTS)", "[.][strum-shots]")
{
    const char* dir = std::getenv ("OSP_STRUM_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedLook ceramic (skin::Look::ceramic);
    OspAudioProcessor p;
    loadAndWait (p, writeSource (tmp.dir, "vowel.wav", testsignals::vowel (midiToHz (57), 2.0, 48000.0, 4)));
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    auto save = [dir] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream out (juce::File (dir).getChildFile (name));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    };
    ui->arpControl().onToggleEditor();
    if (! ui->isArpExpanded())
        ui->arpControl().onToggleEditor();
    e->setSize (1448, juce::roundToInt (1448.0f * ui->instrumentHeight() / 1448.0f));
    auto& arp = ui->arpInlinePanel();
    auto row = [&] { return ui->getLocalArea (&arp, juce::Rectangle<int> (180, 4, 640, 48)); };
    auto shot = [&] (const juce::String& name) {
        ui->refreshNow();
        save (e->createComponentSnapshot (row(), true, 2.0f), name);
    };
    shot ("strum-1-key-off.png");
    p.setParameterValue ("arp.strum.on", 1.0f);
    arp.strumKey()->refresh();
    shot ("strum-2-key-on-unavailable.png");   // pattern UP
    p.setParameterValue ("arp.pattern", 5.0f);
    arp.strumKey()->refresh();
    shot ("strum-3-key-on.png");
    p.setParameterValue ("arp.strum.ms", 60.0f);
    p.setParameterValue ("arp.strum.direction", 3.0f);
    arp.strumKey()->openPanel();
    if (auto* panel = ui->openPopupPanel())
    {
        panel->refreshContent();
        panel->repaint();
    }
    ui->refreshNow();
    save (e->createComponentSnapshot (e->getLocalBounds(), true, 1.0f), "strum-4-panel.png");
    arp.strumKey()->openPanel();
}
