#include "ShellProcessor.h"
#include "ShellEditor.h"

#include <algorithm>
#include <cstdlib>

namespace andor::shell
{

using osp::plugin::InstrumentHost;
using osp::plugin::LoadedInstrument;

namespace
{
    /** CC 1-4's controllers at first, as in ANDOR OSP: 2 (breath), 11 (expression), 16, 17. */
    constexpr std::array<int, osp::mod::ccSlots> defaultControllers { 2, 11, 16, 17 };

    juce::File folderFromEnvironment (const char* variable)
    {
        if (const char* path = std::getenv (variable); path != nullptr && *path != 0)
            return juce::File (juce::String::fromUTF8 (path));
        return {};
    }

    struct CurveAction final : juce::UndoableAction
    {
        CurveAction (ShellProcessor& p, int i, osp::mod::Curve a, osp::mod::Curve b) : processor (p), index (i), from (a), to (b) {}
        bool perform() override { processor.setModulationCurve (index, to, false); return true; }
        bool undo() override { processor.setModulationCurve (index, from, false); return true; }
        ShellProcessor& processor;
        int index;
        osp::mod::Curve from, to;
    };

    const juce::Identifier sourcesType ("Sources"), layerType ("Layer"), viewType ("View"), curvesType ("Curves");

    /** ANDOR OSP's schema, then the product's own parameters (ProductInfo::addParameters). */
    juce::AudioProcessorValueTreeState::ParameterLayout productParameterLayout()
    {
        auto layout = InstrumentHost::createParameterLayout();
        if (productInfo().addParameters != nullptr)
            productInfo().addParameters (layout);
        return layout;
    }

    /** A small, stable fingerprint of a picture (the card repaints when it changes). */
    std::uint64_t fingerprint (const std::vector<float>& values)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const float v : values)
        {
            h ^= static_cast<std::uint64_t> (juce::roundToInt (v * 4096.0f) + 65536);
            h *= 1099511628211ull;
        }
        return h;
    }
}

//==============================================================================
ShellProcessor::ShellProcessor (std::unique_ptr<Engine> productEngine)
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, &undoManager, productInfo().stateType, productParameterLayout()),
      engine (std::move (productEngine))
{
    jassert (engine != nullptr);
    description = engine->describe();
    bound = engine->boundParameters();
    generatorName = engine->generatorName();
    macros = productInfo().macros;
    for (std::size_t i = 0; i < ccNumbers.size(); ++i)
        ccNumbers[i].store (defaultControllers[i]);
    store = std::make_unique<osp::plugin::SampleStore> (sampleStoreFolder());
    if (productInfo().installFactoryPresets != nullptr)
        productInfo().installFactoryPresets (*this, presetFolder().getChildFile ("Factory"));
    startTimerHz (20);
}

ShellProcessor::~ShellProcessor()
{
    closing = true;
    stopTimer();
    loaderPool.removeAllJobs (true, 30000);
}

void ShellProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine->prepare (sampleRate, samplesPerBlock, parameters);
    setLatencySamples (engine->latencySamples());
    // A prepared engine plays the sources it is given from now on: hand it the current ones.
    for (int l = 0; l < numLayers; ++l)
    {
        auto& layer = layers[static_cast<std::size_t> (l)];
        if (layer.instrument != nullptr)
            layer.exchange.publish (layer.instrument);
    }
}

void ShellProcessor::releaseResources()
{
    engine->release();
}

bool ShellProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void ShellProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    // The sources' hand-over: a newly published one replaces the layer's (the engine stops
    // reading the old one before setSource returns), so older ones may be freed.
    for (int l = 0; l < numLayers; ++l)
    {
        auto& layer = layers[static_cast<std::size_t> (l)];
        if (layer.clearRequested.exchange (false, std::memory_order_acq_rel))
        {
            engine->setSource (l, nullptr);
            layer.playing = nullptr;
        }
        if (const auto* next = layer.exchange.takePending())
        {
            engine->setSource (l, next);
            layer.playing = next;
            layer.exchange.publishOldestInUse (next->generation);
        }
    }

    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        if (m.isNoteOn())
        {
            const int count = velocityCount.load (std::memory_order_relaxed);
            recentVelocity[static_cast<std::size_t> (count % velocityHistory)].store (m.getVelocity(), std::memory_order_relaxed);
            velocityCount.store (count + 1, std::memory_order_release);
            lastNote.store (m.getNoteNumber(), std::memory_order_relaxed);
        }
        else if (m.isPitchWheel())
            performance.pitchWheel = static_cast<float> (m.getPitchWheelValue() - 8192) / 8192.0f;
        else if (m.isControllerOfType (1))
            performance.modWheel = static_cast<float> (m.getControllerValue()) / 127.0f;
    }
    // The on-screen wheels win when they move.
    if (const float p = screenPitch.load (std::memory_order_relaxed); ! juce::exactlyEqual (p, lastScreenPitch))
        performance.pitchWheel = lastScreenPitch = p;
    if (const float m = screenMod.load (std::memory_order_relaxed); ! juce::exactlyEqual (m, lastScreenMod))
        performance.modWheel = lastScreenMod = m;
    wheelShown.store (performance.modWheel, std::memory_order_relaxed);

    // The host's transport (tempo-synced movement and echoes): once per block.
    performance.hasPosition = false;
    performance.playing = false;
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            performance.playing = position->getIsPlaying();
            if (const auto bpm = position->getBpm(); bpm.hasValue() && *bpm > 1.0)
                performance.bpm = *bpm;
            if (const auto ppq = position->getPpqPosition(); ppq.hasValue())
            {
                performance.ppq = *ppq;
                performance.hasPosition = true;
            }
            if (const auto signature = position->getTimeSignature(); signature.hasValue())
            {
                performance.numerator = signature->numerator;
                performance.denominator = signature->denominator;
            }
        }

    buffer.clear();
    engine->process (buffer, midi, performance);
}

juce::AudioProcessorEditor* ShellProcessor::createEditor()
{
    return new ShellEditor (*this);
}

//==============================================================================
// Sources

InstrumentHost::LoadState ShellProcessor::loadState (int layer) const noexcept
{
    if (isGenerator())
        return resolve (layer) == 0 ? LoadState::ready : LoadState::empty;
    return layers[static_cast<std::size_t> (resolve (layer))].state.load();
}

std::shared_ptr<const LoadedInstrument> ShellProcessor::currentInstrument (int layer) const
{
    return layers[static_cast<std::size_t> (resolve (layer))].instrument;
}

bool ShellProcessor::isLayerOccupied (int layer) const
{
    if (isGenerator())
        return layer == 0;
    const auto& slot = layers[static_cast<std::size_t> (std::clamp (layer, 0, numLayers - 1))];
    return slot.state.load() == LoadState::loading || slot.instrument != nullptr;
}

const osp::InstrumentEngine::GrainSnapshot& ShellProcessor::grainSnapshot (int layer) const noexcept
{
    return noGrains[static_cast<std::size_t> (std::clamp (layer, 0, numLayers - 1))];
}

bool ShellProcessor::generatedSource (int layer, GeneratedSource& out) const
{
    if (! isGenerator() || layer != 0)
        return false;
    out.name = generatorName;
    out.cycle.clear();
    engine->sourcePicture (layer, parameters, out.cycle);
    if (out.cycle.empty())
        out.cycle.assign (2, 0.0f);   // a flat line: the card still shows the generator, not a drop target
    out.revision = fingerprint (out.cycle);
    return true;
}

int ShellProcessor::slotCount() const
{
    if (isGenerator())
        return 1;
    int count = 0;
    for (int l = 0; l < numLayers; ++l)
        if (isLayerOccupied (l))
            count = l + 1;
    return count;
}

int ShellProcessor::addLayers (const juce::Array<juce::File>& files)
{
    if (isGenerator())
    {
        // Nothing to load: the instrument makes its own sound.
        if (! files.isEmpty())
        {
            const std::lock_guard<std::mutex> lock (messageMutex);
            lastMessage = productInfo().name + " makes its own sound: it plays no recordings";
        }
        return 0;
    }
    int loaded = 0;
    const bool pairBefore = occupiedLayerCount() >= 2;   // an A/B mix the musician has already set
    for (const auto& file : files)
    {
        int free = -1;
        for (int l = 0; l < numLayers && free < 0; ++l)
            if (! isLayerOccupied (l))
                free = l;
        if (free < 0)
        {
            const std::lock_guard<std::mutex> lock (messageMutex);
            lastMessage = "Three layers are the most: " + file.getFileName() + " was not loaded";
            break;
        }
        // A new layer is heard in the mix at once (ANDOR OSP's rule; a free slot's settings
        // are at their defaults: removeLayer and INIT reset them).
        loadFile (file, free);
        makeLayerAudible (free, pairBefore);
        ++loaded;
    }
    return loaded;
}

void ShellProcessor::loadFile (const juce::File& file, int layer)
{
    if (isGenerator())
    {
        addLayers ({ file });
        return;
    }
    osp::plugin::LoadRequest request;
    request.file = file;
    enqueueLoad (std::move (request), resolve (layer));
}

void ShellProcessor::enqueueLoad (osp::plugin::LoadRequest request, int layer)
{
    auto& target = layers[static_cast<std::size_t> (layer)];
    ++pendingLoads;
    target.state = LoadState::loading;
    const auto generation = nextGeneration.fetch_add (1);
    target.latestLoadId = generation;
    // Import, hashing and analysis on the loader thread, never the audio thread.
    loaderPool.addJob ([this, request = std::move (request), generation, layer] {
        auto result = osp::plugin::loadInstrument (request, *store, generation);
        // The next model stage is queued before this one reports, so pendingLoads never reads 0 between them.
        if (result.instrument != nullptr)
            enqueueRefine (result.instrument, result.audio, layer);
        pushResult (std::move (result), layer);
        --pendingLoads;
    });
}

void ShellProcessor::enqueueRefine (std::shared_ptr<const LoadedInstrument> base, std::shared_ptr<const osp::AudioData> audio, int layer)
{
    // The same stages as ANDOR OSP (continuation, then register anchors): the cards show what
    // they add (the sustain loop), and an engine may use them.
    if (closing.load() || base == nullptr || base->model == nullptr || base->model->stage == osp::InstrumentModel::Stage::complete)
        return;
    ++pendingLoads;
    loaderPool.addJob ([this, base = std::move (base), audio = std::move (audio), layer] {
        // A newer sound was dropped into this layer meanwhile: do not spend time on this one.
        if (! closing.load() && base->loadId == layers[static_cast<std::size_t> (layer)].latestLoadId.load())
        {
            auto result = osp::plugin::refineInstrument (*base, audio, nextGeneration.fetch_add (1));
            if (result.instrument != nullptr)
                enqueueRefine (result.instrument, result.audio, layer);
            result.audio.reset();
            pushResult (std::move (result), layer);
        }
        --pendingLoads;
    });
}

void ShellProcessor::pushResult (osp::plugin::LoadResult result, int layer)
{
    const std::lock_guard<std::mutex> lock (resultsMutex);
    finished.push_back ({ layer, std::move (result) });
}

void ShellProcessor::clearLayer (int layer)
{
    auto& slot = layers[static_cast<std::size_t> (layer)];
    slot.instrument = nullptr;
    slot.latestLoadId = nextGeneration.fetch_add (1);   // a load still running for it is dropped
    slot.state = LoadState::empty;
    slot.clearRequested.store (true, std::memory_order_release);
}

void ShellProcessor::removeLayer (int layer)
{
    if (isGenerator())
        return;
    if (layer < 0 || layer >= numLayers || pendingLoads.load() > 0)
        return;
    // The layers above move down with their sounds and settings (every "layerX." parameter;
    // A's REIMAGINED is the instrument's `reimagined`).
    auto idFor = [] (int l, const juce::String& rest) { return rest == "reimagined" ? reimaginedParameterId (l) : layerParameterId (l, rest); };
    juce::StringArray names;
    for (auto* p : getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p); withId != nullptr && withId->paramID.startsWith ("layerA."))
            names.add (withId->paramID.fromFirstOccurrenceOf (".", false, false));
    names.add ("reimagined");
    const int slots = slotCount();
    for (int l = layer; l + 1 < slots; ++l)
    {
        for (const auto& name : names)
            setParameterValue (idFor (l, name), parameterValue (idFor (l + 1, name)));
        auto& to = layers[static_cast<std::size_t> (l)];
        const auto& from = layers[static_cast<std::size_t> (l + 1)];
        to.instrument = from.instrument;
        to.state = from.state.load();
        if (to.instrument != nullptr)
            to.exchange.publish (to.instrument);
        else
            to.clearRequested.store (true, std::memory_order_release);
    }
    clearLayer (std::max (layer, slots - 1));
    for (const auto& name : names)
        if (auto* p = parameters.getParameter (idFor (std::max (layer, slots - 1), name)))
            p->setValueNotifyingHost (p->getDefaultValue());
}

bool ShellProcessor::waitForLoads (int timeoutMs)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (pendingLoads.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep (5);
    }
    return true;
}

void ShellProcessor::timerCallback()
{
    std::deque<Finished> done;
    {
        const std::lock_guard<std::mutex> lock (resultsMutex);
        done.swap (finished);
    }
    for (auto& item : done)
    {
        auto& slot = layers[static_cast<std::size_t> (item.layer)];
        if (item.result.instrument == nullptr || item.result.instrument->loadId != slot.latestLoadId.load())
        {
            if (item.result.instrument == nullptr && slot.state.load() == LoadState::loading)
            {
                // Fail beautifully: odd input is reported, nothing else changes.
                slot.state = slot.instrument != nullptr ? LoadState::ready : LoadState::failed;
                const std::lock_guard<std::mutex> lock (messageMutex);
                lastMessage = juce::String (item.result.error);
            }
            continue;
        }
        slot.instrument = item.result.instrument;
        slot.state = LoadState::ready;
        slot.exchange.publish (slot.instrument);
    }
    for (auto& slot : layers)
        slot.exchange.collectGarbage();
}

juce::String ShellProcessor::statusMessage() const
{
    const std::lock_guard<std::mutex> lock (messageMutex);
    return lastMessage;
}

//==============================================================================
// State: the parameters, the sources (by content hash and path), the curves and the view.

std::unique_ptr<juce::XmlElement> ShellProcessor::stateXml() const
{
    auto xml = std::make_unique<juce::XmlElement> (productInfo().stateType);
    xml->setAttribute ("schemaVersion", stateVersion);
    xml->setAttribute ("product", productInfo().slug);
    // copyState() flushes the parameters; the shell's tree is only read here.
    if (auto params = const_cast<juce::AudioProcessorValueTreeState&> (parameters).copyState().createXml())
        xml->addChildElement (params.release());
    auto* sources = xml->createNewChildElement (sourcesType.toString());
    for (int l = 0; l < numLayers; ++l)
        if (const auto instrument = layers[static_cast<std::size_t> (l)].instrument)
        {
            auto* e = sources->createNewChildElement (layerType.toString());
            e->setAttribute ("index", l);
            e->setAttribute ("hash", juce::String (instrument->contentHash));
            e->setAttribute ("filename", juce::String (instrument->filename));
            e->setAttribute ("path", juce::String (instrument->originalPath));
        }
    {
        const juce::ScopedLock lock (curveLock);
        xml->createNewChildElement (curvesType.toString())->setAttribute ("value", encodeModulationCurves (curves));
    }
    auto* view = xml->createNewChildElement (viewType.toString());
    view->setAttribute ("arpOpen", arpOpen.load() ? 1 : 0);
    view->setAttribute ("modTab", modTab.load());
    view->setAttribute ("editLayer", editLayer());
    view->setAttribute ("velToAmp", velToAmp.load() ? 1 : 0);
    view->setAttribute ("arpRateStepped", arpStepped.load() ? 1 : 0);
    juce::StringArray controllers;
    for (const auto& c : ccNumbers)
        controllers.add (juce::String (c.load()));
    view->setAttribute ("controllers", controllers.joinIntoString (","));
    return xml;
}

bool ShellProcessor::applyStateXml (const juce::XmlElement& xml)
{
    // Another product's (or ANDOR OSP's) state is never read as this one's.
    if (! xml.hasTagName (productInfo().stateType) || xml.getStringAttribute ("product") != productInfo().slug)
        return false;
    if (auto* params = xml.getChildByName (parameters.state.getType()))
        parameters.replaceState (juce::ValueTree::fromXml (*params));
    for (int l = 0; l < numLayers; ++l)
        clearLayer (l);
    if (auto* sources = xml.getChildByName (sourcesType))
        for (auto* e : sources->getChildWithTagNameIterator (layerType.toString()))
        {
            const int index = e->getIntAttribute ("index", -1);
            if (index < 0 || index >= numLayers)
                continue;
            if (isGenerator())
                continue;   // (never saved by a generator; ignored if a file says otherwise)
            osp::plugin::LoadRequest request;
            request.expectedHash = e->getStringAttribute ("hash").toStdString();
            request.originalPath = e->getStringAttribute ("path").toStdString();
            request.filename = e->getStringAttribute ("filename").toStdString();
            request.file = juce::File (e->getStringAttribute ("path"));
            enqueueLoad (std::move (request), index);
        }
    if (auto* c = xml.getChildByName (curvesType))
        if (const auto decoded = decodeModulationCurves (c->getStringAttribute ("value")))
        {
            const juce::ScopedLock lock (curveLock);
            curves = *decoded;
        }
    if (auto* view = xml.getChildByName (viewType))
    {
        arpOpen = view->getIntAttribute ("arpOpen") != 0;
        setModPanelTab (view->getIntAttribute ("modTab"));
        setEditLayer (view->getIntAttribute ("editLayer"));
        velToAmp = view->getIntAttribute ("velToAmp", 1) != 0;
        arpStepped = view->getIntAttribute ("arpRateStepped") != 0;
        const auto controllers = juce::StringArray::fromTokens (view->getStringAttribute ("controllers"), ",", "");
        for (int i = 0; i < std::min (controllers.size(), osp::mod::ccSlots); ++i)
            setModControllerNumber (i, controllers[i].getIntValue());
    }
    return true;
}

void ShellProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = stateXml())
        copyXmlToBinary (*xml, destData);
}

void ShellProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        applyStateXml (*xml);
}

//==============================================================================
// Presets

juce::File ShellProcessor::documentsFolder() const
{
    if (const auto overridden = folderFromEnvironment ("ANDOR_DOCUMENTS_DIR"); overridden != juce::File())
        return overridden.getChildFile (productInfo().presetFolder);
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (productInfo().presetFolder);
}

juce::File ShellProcessor::sampleStoreFolder() const
{
    if (const auto overridden = folderFromEnvironment ("ANDOR_SAMPLE_STORE"); overridden != juce::File())
        return overridden;
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (productInfo().presetFolder).getChildFile ("Samples");
}

bool ShellProcessor::savePreset (const juce::File& file)
{
    auto xml = stateXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory())
        return false;
    // Never over a file that is not ours: a preset keeps the product's extension.
    const auto target = file.hasFileExtension (productInfo().presetExtension) ? file : file.withFileExtension (productInfo().presetExtension);
    if (! xml->writeTo (target))
        return false;
    lastPresetFile = target;
    return true;
}

bool ShellProcessor::loadPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
        return false;
    rememberPreviousState (file.getFileNameWithoutExtension());
    if (! applyStateXml (*xml))
        return false;
    lastPresetFile = file;
    undoManager.clearUndoHistory();
    return true;
}

void ShellProcessor::rememberPreviousState (const juce::String& label)
{
    previousState = stateXml();
    previousStateLabel = label;
}

bool ShellProcessor::restorePreviousState()
{
    if (previousState == nullptr)
        return false;
    const auto state = std::move (previousState);
    previousStateLabel.clear();
    return applyStateXml (*state);
}

std::vector<InstrumentHost::PresetEntry> ShellProcessor::presetList() const
{
    std::vector<PresetEntry> list;
    auto files = presetFolder().findChildFiles (juce::File::findFiles, true, "*" + productInfo().presetExtension);
    files.sort();
    for (const auto& f : files)
    {
        PresetEntry e;
        e.name = f.getFileNameWithoutExtension();
        e.file = f;
        list.push_back (e);
    }
    return list;
}

juce::String ShellProcessor::presetDisplayName() const
{
    return lastPresetFile == juce::File() ? juce::String ("Init") : lastPresetFile.getFileNameWithoutExtension();
}

void ShellProcessor::stepPresetList (int delta)
{
    const auto list = presetList();
    if (list.empty())
        return;
    int index = -1;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].file == lastPresetFile)
            index = static_cast<int> (i);
    const int n = static_cast<int> (list.size());
    const int next = index < 0 ? (delta > 0 ? 0 : n - 1) : ((index + delta) % n + n) % n;
    loadPreset (list[static_cast<std::size_t> (next)].file);
}

void ShellProcessor::openPresetEntry (const PresetEntry& entry)
{
    if (entry.file.existsAsFile())
        loadPreset (entry.file);
}

const juce::StringArray& ShellProcessor::favourites() const
{
    if (! favouritesLoaded)
    {
        favouriteNames = juce::StringArray::fromLines (favouritesFile().loadFileAsString());
        favouriteNames.removeEmptyStrings();
        favouritesLoaded = true;
    }
    return favouriteNames;
}

bool ShellProcessor::isFavourite() const
{
    return favourites().contains (presetDisplayName());
}

void ShellProcessor::toggleFavourite()
{
    favourites();
    const auto name = presetDisplayName();
    if (favouriteNames.contains (name))
        favouriteNames.removeString (name);
    else
        favouriteNames.add (name);
    favouritesFile().getParentDirectory().createDirectory();
    favouritesFile().replaceWithText (favouriteNames.joinIntoString ("\n"));
}

void ShellProcessor::resetSettings()
{
    rememberPreviousState ("Reset");
    for (auto* p : getParameters())
        p->setValueNotifyingHost (p->getDefaultValue());
    {
        const juce::ScopedLock lock (curveLock);
        curves = defaultModulationCurves();
    }
    if (auto links = parameters.state.getChildWithName (modLinksTreeType()); links.isValid())
        parameters.state.removeChild (links, nullptr);
}

void ShellProcessor::initPatch()
{
    resetSettings();
    previousStateLabel = "Init";
    for (int l = 0; l < numLayers; ++l)
        clearLayer (l);
    lastPresetFile = juce::File();
    undoManager.clearUndoHistory();
}

//==============================================================================
// Modulation curves and controllers

osp::mod::Curve ShellProcessor::modulationCurve (int index) const
{
    const juce::ScopedLock lock (curveLock);
    return curves[static_cast<std::size_t> (std::clamp (index, 0, modCurveCount - 1))];
}

void ShellProcessor::setModulationCurve (int index, const osp::mod::Curve& curve, bool undoable)
{
    index = std::clamp (index, 0, modCurveCount - 1);
    auto next = curve;
    next.normalise();
    const auto before = modulationCurve (index);
    if (before == next)
        return;
    if (undoable)
    {
        undoManager.perform (new CurveAction (*this, index, before, next));
        return;
    }
    const juce::ScopedLock lock (curveLock);
    curves[static_cast<std::size_t> (index)] = next;
}

int ShellProcessor::modControllerNumber (int slot) const noexcept
{
    return slot >= 0 && slot < osp::mod::ccSlots ? ccNumbers[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed) : -1;
}

void ShellProcessor::setModControllerNumber (int slot, int controller) noexcept
{
    if (slot >= 0 && slot < osp::mod::ccSlots)
        ccNumbers[static_cast<std::size_t> (slot)].store (std::clamp (controller, 0, 119), std::memory_order_relaxed);
}

int ShellProcessor::recentVelocityAt (int index) const noexcept
{
    return recentVelocity[static_cast<std::size_t> (((index % velocityHistory) + velocityHistory) % velocityHistory)].load (std::memory_order_relaxed);
}

} // namespace andor::shell

// The plugin's entry point: every product built from the shell is a ShellProcessor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new andor::shell::ShellProcessor();
}
