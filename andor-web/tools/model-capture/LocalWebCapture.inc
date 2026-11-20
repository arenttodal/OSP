// LOCAL capture scaffolding (not committed): the current editor's hero state for the product
// renders (with caps, and without caps + every knob's centre/radius/angle), at the default size.
namespace osp::plugin::design::draw { extern bool captureWithoutCaps; }

TEST_CASE ("plugin: local hero capture (OSP_LOCAL_CAPTURE)", "[.][local-capture]")
{
    const char* dir = std::getenv ("OSP_LOCAL_CAPTURE");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    const ScopedEnvironment library ("OSP_LIBRARY_DIR", tmp.dir.getChildFile ("library").getFullPathName());
    const ScopedEnvironment documents ("OSP_DOCUMENTS_DIR", tmp.dir.getChildFile ("documents").getFullPathName());
    const ScopedLook ceramic (osp::plugin::skin::Look::ceramic);
    const juce::File out (dir);
    out.createDirectory();
    auto save = [&out] (const juce::Image& image, const juce::String& name) {
        juce::FileOutputStream stream (out.getChildFile (name));
        stream.setPosition (0);
        stream.truncate();
        juce::PNGImageFormat().writeImageToStream (image, stream);
    };
    std::vector<juce::File> src;
    src.push_back (writeSource (tmp.dir, "A.wav", testsignals::vowel (midiToHz (60), 2.4, 48000.0, 3)));
    src.push_back (writeSource (tmp.dir, "B.wav", testsignals::pluck (220.0, 2.6, 48000.0, 4)));
    src.push_back (writeSource (tmp.dir, "C.wav", testsignals::vowel (midiToHz (48), 2.4, 48000.0, 5)));
    using Editor = osp::plugin::OspAudioProcessorEditor;
    using Host = osp::plugin::InstrumentHost;
    const std::vector<int> chord { 48, 55, 60, 64, 71 };
    auto rectVar = [] (juce::Rectangle<float> r) {
        auto* o = new juce::DynamicObject();
        o->setProperty ("x", r.getX()); o->setProperty ("y", r.getY());
        o->setProperty ("w", r.getWidth()); o->setProperty ("h", r.getHeight());
        return juce::var (o);
    };
    for (const bool caps : { true, false })
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<Editor*> (e.get());
        REQUIRE (ui != nullptr);
        juce::Array<juce::File> list;
        for (auto& f : src) list.add (f);
        p.addLayers (list);
        REQUIRE (p.waitForLoads (30000));
        p.pollLoads();
        e->setSize (1448, juce::roundToInt (ui->instrumentHeight()));
        auto set = [&p] (const juce::String& id, float v) { p.setParameterValue (id, v); };
        set ("life", 62.0f); set ("character", 74.0f); set ("motion", 28.0f); set ("echo", 18.0f); set ("space", 46.0f);
        set ("reimagined", 24.0f);
        set (Host::layerParameterId (1, "sourceMode"), 1.0f);
        set (Host::synthesizeId (2), 45.0f);
        set (Host::synthParameterId (2, "mode"), 1.0f);
        set (Host::synthParameterId (2, "imprint"), 80.0f);
        p.prepareToPlay (48000.0, 480);
        juce::AudioBuffer<float> buffer (2, 480);
        for (int block = 0; block < 60; ++block)
        {
            juce::MidiBuffer midi;
            if (block == 0)
                for (const int n : chord)
                    midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (96)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
        }
        for (const int n : chord)
            p.keyboardState.noteOn (1, n, 0.75f);
        ui->refreshNow();
        ui->refreshNow();
        const juce::String name = caps ? "cur-hero-3L" : "cur-hero-3L-nocaps";
        if (caps)
        {
            juce::DynamicObject::Ptr marks (new juce::DynamicObject());
            marks->setProperty ("editor", rectVar (e->getLocalBounds().toFloat()));
            marks->setProperty ("components", designcapture::bounds (*ui));
            out.getChildFile ("cur-hero-3L-bounds.json").replaceWithText (juce::JSON::toString (juce::var (marks.get()), false));
        }
        if (! caps)
        {
            juce::Array<juce::var> knobs;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& parent) {
                for (auto* child : parent.getChildren())
                {
                    auto visibleInEditor = [ui] (juce::Component* c) {
                        for (; c != nullptr && c != ui; c = c->getParentComponent())
                            if (! c->isVisible())
                                return false;
                        return c == ui;
                    };
                    if (auto* s = dynamic_cast<juce::Slider*> (child); s != nullptr && visibleInEditor (s) && s->isRotary())
                    {
                        const auto& props = s->getProperties();
                        const bool mini = static_cast<bool> (props["mini"]);
                        const bool ticks = ! mini && ! static_cast<bool> (props["noTicks"]);
                        const bool popup = static_cast<bool> (props["popup"]);
                        const auto b = ui->getLocalArea (s, s->getLocalBounds()).toFloat();
                        const float side = std::min (b.getWidth(), b.getHeight());
                        const float r = popup ? 0.5f * side / 1.36f : 0.5f * side / (ticks ? 1.42f : 1.2f);
                        const auto rp = s->getRotaryParameters();
                        const auto pos = static_cast<float> (s->valueToProportionOfLength (s->getValue()));
                        auto* k = new juce::DynamicObject();
                        k->setProperty ("x", b.getCentreX()); k->setProperty ("y", b.getCentreY()); k->setProperty ("r", r);
                        k->setProperty ("angle", rp.startAngleRadians + pos * (rp.endAngleRadians - rp.startAngleRadians));
                        k->setProperty ("ticks", ticks); k->setProperty ("mini", mini); k->setProperty ("w", b.getWidth());
                        k->setProperty ("param", props["paramId"].toString());
                        knobs.add (juce::var (k));
                    }
                    walk (*child);
                }
            };
            walk (*ui);
            juce::DynamicObject::Ptr marks (new juce::DynamicObject());
            marks->setProperty ("editor", rectVar (e->getLocalBounds().toFloat()));
            marks->setProperty ("knobs", knobs);
            out.getChildFile (name + ".json").replaceWithText (juce::JSON::toString (juce::var (marks.get()), false));
            osp::plugin::design::draw::captureWithoutCaps = true;
        }
        for (const float scale : caps ? std::vector<float> { 1.0f, 2.0f, 4.0f } : std::vector<float> { 1.0f, 3.0f, 4.0f })
            save (e->createComponentSnapshot (e->getLocalBounds(), true, scale), name + "@" + juce::String (juce::roundToInt (scale)) + "x.png");
        osp::plugin::design::draw::captureWithoutCaps = false;
        p.releaseResources();
    }
}
