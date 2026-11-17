// Before / after screenshots of the final polish: INFO's note, the preset card, the sound
// inspector's TYPE and ROOT NOTE. Only APIs that existed before the polish, so the same test
// shoots both: OSP_FINAL_POLISH_SHOTS=<folder> OSP_FINAL_POLISH_PREFIX=before|after
// osp_plugin_tests "[final-polish-compare-shots]".

namespace
{
    juce::Component* findTitled (juce::Component& root, const juce::String& title)
    {
        for (auto* child : root.getChildren())
        {
            if (child->getTitle() == title && child->isVisible())
                return child;
            if (auto* c = findTitled (*child, title))
                return c;
        }
        return nullptr;
    }

    void writeShot (const juce::File& out, const juce::String& name, const juce::Image& image)
    {
        const auto file = out.getChildFile (name + ".png");
        file.deleteFile();
        juce::FileOutputStream stream (file);
        juce::PNGImageFormat().writeImageToStream (image, stream);
    }
}

TEST_CASE ("final polish: before / after screenshots (OSP_FINAL_POLISH_SHOTS)", "[.][final-polish-compare-shots]")
{
    const char* dir = std::getenv ("OSP_FINAL_POLISH_SHOTS");
    if (dir == nullptr)
        return;
    const juce::File out (juce::String::fromUTF8 (dir));
    out.createDirectory();
    const juce::String prefix = std::getenv ("OSP_FINAL_POLISH_PREFIX") != nullptr ? juce::String (std::getenv ("OSP_FINAL_POLISH_PREFIX")) : juce::String ("after");
    {
        LearnRig rig;
        const juce::String target = "osp.macro.filter";
        rig.learn.setLearnMode (true);
        rig.learn.inspect (target);
        rig.learn.layoutNow();
        rig.ui.refreshNow();
        auto* note = rig.learn.note();
        REQUIRE (note != nullptr);
        const auto area = rig.editor->getLocalArea (note, note->getLocalBounds());
        writeShot (out, prefix + "-01-info-note", rig.editor->createComponentSnapshot (area, true, 2.0f));
    }
    {
        LibraryRig rig;
        OspAudioProcessor::presetFolder().createDirectory();
        REQUIRE (rig.p.savePreset (OspAudioProcessor::presetFolder().getChildFile ("Warm Hymn.osppreset")));
        REQUIRE (rig.p.libraryService().waitUntilIdle (10000));
        rig.panel->setView (osp::plugin::LibraryPanel::View::presets);
        rig.panel->refresh();
        REQUIRE (rig.panel->settle());
        for (int i = 0; i < rig.panel->rowCount(); ++i)
            if (rig.panel->row (i).name == "Warm Hymn")
                rig.panel->selectRow (i);
        REQUIRE (rig.panel->settle());
        rig.ui->refreshNow();
        auto* card = findTitled (*rig.panel, "Selected preset");
        REQUIRE (card != nullptr);
        writeShot (out, prefix + "-02-preset-card", rig.editor->createComponentSnapshot (rig.editor->getLocalArea (card, card->getLocalBounds()), true, 2.0f));
        rig.sounds();
        rig.panel->selectRow (rig.rowOf (rig.idDrone));
        REQUIRE (rig.panel->settle());
        rig.ui->refreshNow();
        auto* inspector = findTitled (*rig.panel, "Sound inspector");
        REQUIRE (inspector != nullptr);
        const auto top = inspector->getLocalBounds().withHeight (juce::jmin (inspector->getHeight(), 260));
        writeShot (out, prefix + "-03-type-and-root", rig.editor->createComponentSnapshot (rig.editor->getLocalArea (inspector, top), true, 2.0f));
    }
}
