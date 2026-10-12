// Design lab (design-lab/): the real editor exported as layers, for material mockups.
// Hidden ([.][design-lab]); writes only when OSP_DESIGNLAB_DIR is set. Production code is
// not touched: this uses the public Component API only (each component's own paint is taken
// with its children hidden for the moment, then shown again).
//
// Output: <dir>/layers/NNNN.png (each component's own paint, transparent, at 2x of the
// 1448 x 1086 design), <dir>/layers.json (tree, z-order, bounds, class, slider and button
// state), <dir>/full@2x.png (the editor as the plugin draws it, for checking a recomposition).

#include <typeinfo>
#include <cxxabi.h>

namespace
{
    juce::String demangled (const std::type_info& t)
    {
        int status = 0;
        char* name = abi::__cxa_demangle (t.name(), nullptr, nullptr, &status);
        juce::String s (status == 0 && name != nullptr ? name : t.name());
        std::free (name);
        return s;
    }

    struct LayerExporter
    {
        juce::Component& root;
        juce::File dir;
        float scale = 2.0f;
        juce::Array<juce::var> entries;
        int next = 0;

        void save (const juce::Image& image, const juce::File& file)
        {
            file.deleteFile();
            juce::FileOutputStream out (file);
            juce::PNGImageFormat().writeImageToStream (image, out);
        }

        void visit (juce::Component& c, int parent, int depth)
        {
            if ((depth > 0 && ! c.isVisible()) || c.getWidth() <= 0 || c.getHeight() <= 0)
                return;
            const int index = next++;
            // Its own paint: the visible children hidden for the moment.
            juce::Array<juce::Component*> shown;
            for (auto* child : c.getChildren())
                if (child->isVisible())
                    shown.add (child);
            for (auto* child : shown)
                child->setVisible (false);
            const auto own = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
            for (auto* child : shown)
                child->setVisible (true);
            const auto file = dir.getChildFile ("layers").getChildFile (juce::String (index).paddedLeft ('0', 4) + ".png");
            save (own, file);

            auto* o = new juce::DynamicObject();
            o->setProperty ("index", index);
            o->setProperty ("parent", parent);
            o->setProperty ("depth", depth);
            o->setProperty ("class", demangled (typeid (c)));
            o->setProperty ("name", c.getName());
            o->setProperty ("title", c.getTitle());
            const auto area = root.getLocalArea (&c, c.getLocalBounds());
            o->setProperty ("x", area.getX());
            o->setProperty ("y", area.getY());
            o->setProperty ("w", area.getWidth());
            o->setProperty ("h", area.getHeight());
            o->setProperty ("file", file.getFileName());
            if (auto* s = dynamic_cast<juce::Slider*> (&c))
            {
                o->setProperty ("sliderPos", s->valueToProportionOfLength (s->getValue()));
                o->setProperty ("sliderStyle", static_cast<int> (s->getSliderStyle()));
                o->setProperty ("paramId", s->getProperties()["paramId"].toString());
                o->setProperty ("sprite", s->getProperties()["sprite"].toString());
            }
            if (auto* b = dynamic_cast<juce::Button*> (&c))
            {
                o->setProperty ("toggled", b->getToggleState());
                o->setProperty ("buttonText", b->getButtonText());
            }
            entries.add (juce::var (o));
            for (auto* child : shown)
                visit (*child, index, depth + 1);
        }
    };
} // namespace

TEST_CASE ("design lab: the editor exported as layers (own paint per component), for material mockups", "[.][design-lab]")
{
    const char* path = std::getenv ("OSP_DESIGNLAB_DIR");
    if (path == nullptr)
        return;
    const juce::File dir (path);
    REQUIRE (dir.getChildFile ("layers").createDirectory());
    TempDir tmp;
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    osp::plugin::setKnobSkin (osp::plugin::KnobSkin::macros);   // the approved knob on all six
    const auto a = writeSource (tmp.dir, "MMZT_one_shot_sailboat_C.wav", testsignals::vowel (midiToHz (69), 2.1, 48000.0, 3));
    const auto b = writeSource (tmp.dir, "MMZT_one_shot_reverb_kalimba_C.wav", testsignals::pluck (midiToHz (69), 3.4, 48000.0, 5));
    const auto c = writeSource (tmp.dir, "Warm Harmonics.wav", testsignals::pluck (midiToHz (64), 2.5, 48000.0, 9));
    OspAudioProcessor p;
    p.addLayers ({ a, b, c });
    REQUIRE (p.waitForLoads (30000));
    p.pollLoads();
    auto set = [&] (const char* id, float value) { p.setParameterValue (id, value); };
    // The macros as in the reference screenshot; one route (LFO 1 on CHARACTER) for a halo.
    set ("life", 72.0f);
    set ("drive", 46.0f);
    set ("character", 42.0f);
    set ("motion", 28.0f);
    set ("echo", 64.0f);
    set ("space", 18.0f);
    set ("layerA.level", 6.0f);
    set ("layerA.reimagined", 85.0f);
    set ("layerB.sourceMode", 1.0f);
    set ("layerB.level", -0.5f);
    set ("layerC.reimagined", 100.0f);
    set ("life.pitch", 65.0f);
    p.assignModulation (osp::mod::Source::lfo1, osp::mod::Dest::character, 40.0f);
    std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (editor.get());
    REQUIRE (ui != nullptr);
    editor->setSize (1448, 1086);
    ui->refreshNow();

    LayerExporter exporter { *editor, dir };
    if (const char* scale = std::getenv ("OSP_DESIGNLAB_SCALE"))
        exporter.scale = static_cast<float> (std::atof (scale));   // 4 for the lab's close-ups
    const auto full = editor->createComponentSnapshot (editor->getLocalBounds(), true, exporter.scale);
    exporter.save (full, dir.getChildFile ("full@" + juce::String (juce::roundToInt (exporter.scale)) + "x.png"));
    exporter.save (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), dir.getChildFile ("full@1x.png"));
    exporter.visit (*editor, -1, 0);
    auto* doc = new juce::DynamicObject();
    doc->setProperty ("schemaVersion", 1);
    doc->setProperty ("scale", exporter.scale);
    doc->setProperty ("width", editor->getWidth());
    doc->setProperty ("height", editor->getHeight());
    doc->setProperty ("layers", exporter.entries);
    dir.getChildFile ("layers.json").replaceWithText (juce::JSON::toString (juce::var (doc)));
    CHECK (exporter.entries.size() > 20);
    osp::plugin::setKnobSkin (osp::plugin::KnobSkin::pilot);
}
