// Live capture in ANDOR OSP: the audio input (a sidechain in a DAW, the standalone app's input)
// kept by the audio thread; RECORD / STOP and GRAB turn what was played into the layer's
// instrument, its notes mapped (Phase 4's analysis). The input is never heard, the take lives
// in the sample store only, and the audio thread never allocates. Included after
// UnifiedPhase4Tests.inc (its helpers).

namespace
{
    constexpr double captureRate = 48000.0;

    /** A sung-like phrase (Phase 4's vowels, 0.8 s each with 0.2 s gaps) after `leadSilence`. */
    AudioData capturePhrase (std::vector<int> keys, double leadSilence = 0.0, int channels = 2)
    {
        const auto noteFrames = static_cast<std::int64_t> (0.8 * captureRate), gap = static_cast<std::int64_t> (0.2 * captureRate);
        const auto lead = static_cast<std::int64_t> (leadSilence * captureRate);
        auto audio = AudioData::allocate (channels, lead + static_cast<std::int64_t> (keys.size()) * (noteFrames + gap), captureRate);
        for (std::size_t k = 0; k < keys.size(); ++k)
        {
            const auto note = testsignals::vowel (midiToHz (keys[k]), 0.8, captureRate, 30 + k, 0.5, channels);
            const auto start = lead + static_cast<std::int64_t> (k) * (noteFrames + gap);
            for (int c = 0; c < channels; ++c)
                std::copy (note.channels[static_cast<std::size_t> (c)].begin(), note.channels[static_cast<std::size_t> (c)].begin() + noteFrames,
                           audio.channels[static_cast<std::size_t> (c)].begin() + start);
        }
        return audio;
    }

    AudioData silenceFor (double seconds, int channels = 2)
    {
        return AudioData::allocate (channels, static_cast<std::int64_t> (seconds * captureRate), captureRate);
    }

    /** Connects the input (as a host routing a sidechain does) and prepares. */
    void connectInput (OspAudioProcessor& p, const juce::AudioChannelSet& set = juce::AudioChannelSet::stereo(), int block = 256)
    {
        auto layout = p.getBusesLayout();
        REQUIRE (layout.inputBuses.size() == 1);
        layout.inputBuses.getReference (0) = set;
        REQUIRE (p.setBusesLayout (layout));
        p.prepareToPlay (captureRate, block);
    }

    /** Plays `input` into the processor's input; returns the loudest output sample. `eachBlock`
        runs after every block (an editor's timer). */
    float feedInput (OspAudioProcessor& p, const AudioData& input, int block = 256, const std::function<void()>& eachBlock = {})
    {
        const int inChannels = p.getTotalNumInputChannels();
        juce::AudioBuffer<float> buffer (std::max (2, inChannels), block);
        juce::MidiBuffer midi;
        float loudest = 0.0f;
        for (std::int64_t pos = 0; pos < input.numFrames(); pos += block)
        {
            const int n = static_cast<int> (std::min<std::int64_t> (block, input.numFrames() - pos));
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), n);
            view.clear();
            for (int ch = 0; ch < inChannels; ++ch)
                std::copy_n (input.channels[static_cast<std::size_t> (std::min (ch, input.numChannels() - 1))].begin() + pos, n, view.getWritePointer (ch));
            p.processBlock (view, midi);
            if (eachBlock)
                eachBlock();
            for (int ch = 0; ch < view.getNumChannels(); ++ch)
                loudest = std::max (loudest, view.getMagnitude (ch, 0, n));
        }
        return loudest;
    }

    std::set<int> includedKeys (const OspAudioProcessor& p, int layer)
    {
        std::set<int> keys;
        if (const auto instrument = p.currentInstrument (layer); instrument != nullptr && instrument->atlas != nullptr)
            for (const auto& n : instrument->atlas->notes)
                if (n.included)
                    keys.insert (n.rootMidi);
        return keys;
    }
}

TEST_CASE ("plugin capture: the input is a sidechain, off until a host connects it", "[plugin][capture]")
{
    OspAudioProcessor p;
    REQUIRE (p.getBusCount (true) == 1);
    CHECK (p.getBus (true, 0)->getName() == "Sidechain");
    CHECK_FALSE (p.getBus (true, 0)->isEnabled());   // (a plugin; the standalone app turns it on)
    CHECK (p.getTotalNumInputChannels() == 0);
    // Disabled, mono or stereo in; mono or stereo out.
    for (const auto& in : { juce::AudioChannelSet::disabled(), juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        for (const auto& out : { juce::AudioChannelSet::mono(), juce::AudioChannelSet::stereo() })
        {
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add (in);
            layout.outputBuses.add (out);
            CHECK (p.checkBusesLayoutSupported (layout));
        }
    juce::AudioProcessor::BusesLayout surround;
    surround.inputBuses.add (juce::AudioChannelSet::create5point1());
    surround.outputBuses.add (juce::AudioChannelSet::stereo());
    CHECK_FALSE (p.checkBusesLayoutSupported (surround));
    // VST3: an aux (sidechain) bus, not a main input.
    REQUIRE (p.getVST3ClientExtensions() != nullptr);
    CHECK_FALSE (p.getVST3ClientExtensions()->getPluginHasMainInput());

    // Not connected: nothing to record, and the page says how to connect.
    p.prepareToPlay (captureRate, 256);
    const auto view = p.captureView();
    CHECK_FALSE (view.connected);
    CHECK (view.hint.contains ("sidechain"));
    CHECK_FALSE (p.startTake (0));
    CHECK_FALSE (p.grabLast (10.0, 0));
    CHECK (p.liveCapture());
}

TEST_CASE ("plugin capture: the input is kept, never heard", "[plugin][capture]")
{
    OspAudioProcessor p;
    connectInput (p);
    auto noise = silenceFor (2.0);
    Prng prng (7);
    for (auto& channel : noise.channels)
        for (auto& v : channel)
            v = static_cast<float> (prng.nextDouble() * 1.6 - 0.8);
    CHECK (feedInput (p, noise) == 0.0f);   // no sound loaded, no note: silence out
    const auto view = p.captureView();
    CHECK (view.connected);
    CHECK (view.level > 0.5f);
    CHECK (view.available > 1.9);
}

TEST_CASE ("plugin capture: RECORD / STOP maps the take's notes onto the layer", "[plugin][capture]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    connectInput (p);
    feedInput (p, silenceFor (1.5));            // before RECORD: not in the take
    REQUIRE (p.startTake (1));
    feedInput (p, capturePhrase ({ 69, 74, 77, 81 }, 0.3));
    auto view = p.captureView();
    CHECK (view.recording);
    CHECK (view.layer == 1);
    CHECK (view.seconds > 4.2);
    REQUIRE (p.stopTake());
    CHECK_FALSE (p.captureView().recording);
    settleAnalysis (p);
    REQUIRE (p.loadState (1) == OspAudioProcessor::LoadState::ready);
    const auto instrument = p.currentInstrument (1);
    REQUIRE (instrument != nullptr);
    REQUIRE (instrument->atlas != nullptr);
    CHECK (includedKeys (p, 1) == std::set<int> { 69, 74, 77, 81 });
    const auto& source = instrument->atlas->sources.front();
    CHECK (source.origin == atlas::AssetOrigin::liveCapture);
    CHECK (juce::String (source.captureId).startsWith ("take-"));
    CHECK (source.fileName == source.captureId);
    // Trimmed: the silence before the phrase is gone (a short lead-in kept), the tail kept.
    CHECK (source.durationSeconds() < 4.0 + 0.05 + 0.25 + 0.05);
    CHECK (source.durationSeconds() > 3.8);
    // The take is in the store by its hash, and nowhere else.
    const auto storeFolder = tmp.dir.getChildFile ("store");
    CHECK (juce::File (juce::String (source.lastKnownPath)).getParentDirectory() == storeFolder);
    CHECK (storeFolder.getChildFile ("takes").getNumberOfChildFiles (juce::File::findFiles) == 0);
    for (int key : { 69, 77 })
        CHECK (heldLevelDb (p, key) > -50.0);   // (only B holds a sound)
}

TEST_CASE ("plugin capture: GRAB keeps what was just played", "[plugin][capture]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    connectInput (p, juce::AudioChannelSet::mono());
    feedInput (p, capturePhrase ({ 62, 67 }, 0.0, 1));          // older than the grab: left out
    feedInput (p, silenceFor (6.0, 1));
    feedInput (p, capturePhrase ({ 72, 76, 79 }, 0.5, 1));
    feedInput (p, silenceFor (1.0, 1));
    REQUIRE (p.grabLast (5.0, 0));
    settleAnalysis (p);
    REQUIRE (p.loadState (0) == OspAudioProcessor::LoadState::ready);
    CHECK (includedKeys (p, 0) == std::set<int> { 72, 76, 79 });
    const auto instrument = p.currentInstrument (0);
    REQUIRE (instrument->atlas != nullptr);
    const auto& source = instrument->atlas->sources.front();
    CHECK (source.origin == atlas::AssetOrigin::bufferSnapshot);
    CHECK (source.numChannels == 1);   // a mono input stays mono

    // Another grab replaces it: one undo step (a first sound on an empty layer is none, as
    // for any load), and undo brings the first take back.
    feedInput (p, silenceFor (5.0, 1));
    feedInput (p, capturePhrase ({ 64, 67 }, 0.2, 1));
    feedInput (p, silenceFor (0.5, 1));
    REQUIRE (p.grabLast (5.0, 0));
    settleAnalysis (p);
    CHECK (includedKeys (p, 0) == std::set<int> { 64, 67 });
    CHECK (p.undo().getUndoDescription() == "Grab into A");
    REQUIRE (p.undo().undo());
    settleAnalysis (p);
    CHECK (includedKeys (p, 0) == std::set<int> { 72, 76, 79 });
}

TEST_CASE ("plugin capture: silence, an unfinished take and a minute's limit", "[plugin][capture]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    SECTION ("nothing heard: nothing loads, the reason is shown")
    {
        OspAudioProcessor p;
        connectInput (p);
        feedInput (p, silenceFor (6.0));
        CHECK_FALSE (p.grabLast (5.0, 0));
        CHECK (p.statusMessage().contains ("nothing was heard"));
        REQUIRE (p.startTake (0));
        feedInput (p, silenceFor (2.0));
        CHECK_FALSE (p.stopTake());
        CHECK (p.currentInstrument (0) == nullptr);
    }
    SECTION ("cancel: the take is thrown away")
    {
        OspAudioProcessor p;
        connectInput (p);
        REQUIRE (p.startTake (0));
        feedInput (p, capturePhrase ({ 69 }));
        p.cancelTake();
        CHECK_FALSE (p.captureView().recording);
        CHECK_FALSE (p.stopTake());
        CHECK (p.currentInstrument (0) == nullptr);
    }
    SECTION ("a take stops itself at a minute")
    {
        OspAudioProcessor p;
        connectInput (p, juce::AudioChannelSet::stereo(), 1024);
        REQUIRE (p.startTake (0));
        const auto phrase = capturePhrase ({ 69, 72, 76, 79, 81 });   // 5 s, played 13 times
        for (int k = 0; k < 13 && p.captureView().recording; ++k)
            feedInput (p, phrase, 1024, [&p] { p.pollTake(); });   // polled every block (~21 ms)
        CHECK_FALSE (p.captureView().recording);
        settleAnalysis (p);
        const auto instrument = p.currentInstrument (0);
        REQUIRE (instrument != nullptr);
        REQUIRE (instrument->atlas != nullptr);
        CHECK (instrument->atlas->sources.front().durationSeconds() <= capture::maxTakeSeconds + 0.3);
        CHECK (instrument->atlas->sources.front().durationSeconds() > 55.0);
    }
    SECTION ("a new sample rate throws a running take away")
    {
        OspAudioProcessor p;
        connectInput (p);
        REQUIRE (p.startTake (0));
        p.prepareToPlay (44100.0, 256);
        CHECK_FALSE (p.captureView().recording);
    }
}

TEST_CASE ("plugin capture: the audio thread never allocates while the input is kept", "[plugin][capture]")
{
    OspAudioProcessor p;
    connectInput (p);
    const auto phrase = capturePhrase ({ 69, 72 });
    feedInput (p, phrase);   // warm up every path
    REQUIRE (p.startTake (0));
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    test::AllocationScope scope;
    for (int i = 0; i < 400; ++i)
    {
        for (int ch = 0; ch < 2; ++ch)
            std::copy_n (phrase.channels[static_cast<std::size_t> (ch)].begin() + (i % 150) * 256, 256, buffer.getWritePointer (ch));
        p.processBlock (buffer, midi);
    }
    CHECK (scope.count() == 0);
    p.cancelTake();
}

namespace
{
    juce::MouseEvent captureClick (juce::Component& c, juce::Point<float> at)
    {
        auto source = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (source, at, {}, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, at, now, 1, false);
    }

    /** Clicks a part of the page; true when the page asked to close (it does so after a kept
        take; its card then closes it on the message loop, which these tests do not run). */
    bool clickPart (osp::plugin::CaptureEditor& page, juce::Rectangle<float> part)
    {
        bool closed = false;
        auto previous = page.onClose;
        page.onClose = [&closed] { closed = true; };
        page.mouseDown (captureClick (page, part.getCentre()));
        page.onClose = previous;
        return closed;
    }
}

TEST_CASE ("plugin capture: each card's REC key opens its CAPTURE page; RECORD, STOP and GRAB from the page", "[plugin][capture][ui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    loadLayers (p, tmp.dir, 2);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, 1086);
    ui->refreshNow();
    auto& card = ui->engineCard (1);
    REQUIRE (card.recToggle() != nullptr);
    CHECK (card.recToggle()->isVisible());
    CHECK (card.recToggle()->getRight() <= card.eqToggle().getX());

    // Not connected: the page says how; RECORD does nothing.
    card.setEqOpen (true);
    card.recToggle()->onClick();
    REQUIRE (card.isCaptureOpen());
    CHECK (card.recToggle()->getToggleState());
    CHECK_FALSE (card.isEqOpen());   // one page in the well at a time
    auto* page = card.captureEditorComponent();
    REQUIRE (page != nullptr);
    CHECK (page->getWidth() > 300);   // (the well)
    CHECK_FALSE (page->shownView().connected);
    CHECK_FALSE (clickPart (*page, page->recordArea()));
    CHECK (p.recordingLayer() == -1);

    // Connected: RECORD from the page, the key's dot follows, STOP keeps the take into B.
    connectInput (p);
    page->refresh();
    CHECK (page->shownView().connected);
    clickPart (*page, page->recordArea());
    CHECK (p.recordingLayer() == 1);
    feedInput (p, capturePhrase ({ 69, 74, 77 }, 0.2));
    ui->refreshNow();
    CHECK (card.recToggle()->isRecording());
    CHECK_FALSE (ui->engineCard (0).recToggle()->isRecording());
    page->refresh();
    CHECK (page->shownView().recording);
    CHECK (clickPart (*page, page->recordArea()));   // STOP: kept, the page closes
    CHECK (p.recordingLayer() == -1);
    card.setCaptureOpen (false);
    settleAnalysis (p);
    ui->refreshNow();
    CHECK (includedKeys (p, 1) == std::set<int> { 69, 74, 77 });
    CHECK_FALSE (card.recToggle()->isRecording());

    // GRAB LAST 10 s into A, from A's page.
    auto& first = ui->engineCard (0);
    feedInput (p, silenceFor (10.0));
    feedInput (p, capturePhrase ({ 72, 79 }, 0.2));
    first.setCaptureOpen (true);
    REQUIRE (first.captureEditorComponent() != nullptr);
    first.captureEditorComponent()->refresh();
    CHECK (clickPart (*first.captureEditorComponent(), first.captureEditorComponent()->grabArea (0)));
    first.setCaptureOpen (false);
    settleAnalysis (p);
    CHECK (includedKeys (p, 0) == std::set<int> { 72, 79 });

    // DISCARD throws a running take away; the close cross leaves the page.
    first.setCaptureOpen (true);
    auto* again = first.captureEditorComponent();
    clickPart (*again, again->recordArea());
    CHECK (p.recordingLayer() == 0);
    again->refresh();
    clickPart (*again, again->discardArea());
    CHECK (p.recordingLayer() == -1);
}

TEST_CASE ("plugin capture: an empty instrument records from its Record... button", "[plugin][capture][ui]")
{
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    OspAudioProcessor p;
    connectInput (p);
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
    auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
    REQUIRE (ui != nullptr);
    e->setSize (1448, 1086);
    ui->refreshNow();
    REQUIRE (ui->newLayerCaptureComponent() == nullptr);
    ui->openNewLayerCapture();
    auto* page = ui->newLayerCaptureComponent();
    REQUIRE (page != nullptr);
    CHECK (page->isVisible());
    CHECK (page->getWidth() > 1000);   // over the whole sources area
    feedInput (p, capturePhrase ({ 69, 76 }, 0.2));
    page->refresh();
    CHECK (clickPart (*page, page->grabArea (0)));
    settleAnalysis (p);
    ui->refreshNow();
    CHECK (ui->newLayerCaptureComponent() == nullptr);   // the take arrived: the page gives way to its card
    CHECK (includedKeys (p, 0) == std::set<int> { 69, 76 });
}

TEST_CASE ("plugin capture: the CAPTURE page's screenshots (OSP_CAPTURE_SHOTS)", "[.][plugin][capture-shots]")
{
    const char* dir = std::getenv ("OSP_CAPTURE_SHOTS");
    if (dir == nullptr)
        return;
    TempDir tmp;
    const ScopedEnvironment storeDir ("OSP_SAMPLE_STORE", tmp.dir.getChildFile ("store").getFullPathName());
    auto shoot = [dir] (juce::AudioProcessorEditor& e, const juce::String& name) {
        juce::FileOutputStream out (juce::File (juce::String::fromUTF8 (dir)).getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (e.createComponentSnapshot (e.getLocalBounds(), true, 1.0f), out);
    };
    for (int layers : { 1, 3 })
    {
        OspAudioProcessor p;
        loadLayers (p, tmp.dir, layers);
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        REQUIRE (ui != nullptr);
        e->setSize (1448, 1086);
        ui->refreshNow();
        auto& card = ui->engineCard (0);
        card.setCaptureOpen (true);
        auto* page = card.captureEditorComponent();
        page->refresh();
        shoot (*e, "capture-" + juce::String (layers) + "-no-input");
        connectInput (p);
        // A phrase played in, at the page's pace (the level picture scrolls with its timer).
        const auto phrase = capturePhrase ({ 69, 74, 77, 81 }, 0.3);
        const int perTick = static_cast<int> (captureRate / 30.0);
        for (std::int64_t at = 0; at + perTick <= phrase.numFrames(); at += perTick)
        {
            AudioData slice = AudioData::allocate (2, perTick, captureRate);
            for (int ch = 0; ch < 2; ++ch)
                std::copy_n (phrase.channels[static_cast<std::size_t> (ch)].begin() + at, perTick, slice.channels[static_cast<std::size_t> (ch)].begin());
            feedInput (p, slice, perTick);
            page->refresh();
        }
        shoot (*e, "capture-" + juce::String (layers) + "-listening");
        p.startTake (0);
        for (std::int64_t at = 0; at + perTick <= phrase.numFrames() / 2; at += perTick)
        {
            AudioData slice = AudioData::allocate (2, perTick, captureRate);
            for (int ch = 0; ch < 2; ++ch)
                std::copy_n (phrase.channels[static_cast<std::size_t> (ch)].begin() + at, perTick, slice.channels[static_cast<std::size_t> (ch)].begin());
            feedInput (p, slice, perTick);
            page->refresh();
        }
        ui->refreshNow();
        shoot (*e, "capture-" + juce::String (layers) + "-recording");
        p.cancelTake();
    }
    {
        OspAudioProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditorIfNeeded());
        auto* ui = dynamic_cast<osp::plugin::OspAudioProcessorEditor*> (e.get());
        e->setSize (1448, 1086);
        ui->refreshNow();
        ui->openNewLayerCapture();
        ui->refreshNow();
        shoot (*e, "capture-empty-instrument");
    }
}
