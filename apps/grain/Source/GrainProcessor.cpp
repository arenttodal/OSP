#include "GrainProcessor.h"

#include "GrainEditor.h"

#include "analysis/Analyzer.h"
#include "core/PitchMath.h"
#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseAnalysis.h"
#include "andor/phrase/PhraseFixtures.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"
#include "model/RootChoice.h"

namespace osp::andorlab
{

namespace
{
    // Stable parameter IDs (session and automation identity). The order below is `raw`'s order.
    const std::vector<juce::String>& parameterIds()
    {
        static const std::vector<juce::String> ids = [] {
            std::vector<juce::String> v { "size", "density", "position", "scatter", "pitch", "pitchScatter", "spread", "reverse", "jitter",
                                          "window", "grainLevel", "triggerVar", "oscMix", "oscShape", "oscTune", "tone", "resonance",
                                          "attack", "decay", "sustain", "release", "auxAttack", "auxDecay", "auxSustain", "auxRelease",
                                          "lfo1Rate", "lfo1Shape", "lfo2Rate", "lfo2Shape", "velocity", "bendRange", "gain", "macro1",
                                          "macro2", "dnaCurve", "dnaVariation", "dnaIndividual", "dnaSeed", "policy", "fixedNote", "root",
                                          "quality", "deterministic", "seed", "space", "echo" };
            for (int i = 1; i <= grain::numModSlots; ++i)
                for (const char* part : { "Src", "Dst", "Amt" })
                    v.push_back ("mod" + juce::String (i) + part);
            return v;
        }();
        return ids;
    }

    enum P { size, density, position, scatter, pitch, pitchScatter, spread, reverse, jitter, window, grainLevel, triggerVar, oscMix,
             oscShape, oscTune, tone, resonance, attack, decay, sustain, release, auxAttack, auxDecay, auxSustain, auxRelease, lfo1Rate,
             lfo1Shape, lfo2Rate, lfo2Shape, velocity, bendRange, gain, macro1, macro2, dnaCurve, dnaVariation, dnaIndividual, dnaSeed,
             policy, fixedNote, root, quality, deterministic, seed, space, echo, modBase };

    juce::NormalisableRange<float> skewed (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }
    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String secs (float v, int) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s"; }
    juce::String semis (float v, int) { return (v > 0.0f ? "+" : "") + juce::String (v, std::abs (v) < 10.0f ? 1 : 0) + " st"; }

    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        const auto& ids = parameterIds();
        auto real = [&] (int id, const char* name, NormalisableRange<float> range, float def, std::function<String (float, int)> text = {})
        {
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[static_cast<std::size_t> (id)], 1 }, name, range, def,
                                                          AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
        };
        auto choice = [&] (int id, const char* name, StringArray items, int def)
        { l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[static_cast<std::size_t> (id)], 1 }, name, items, def)); };
        auto integer = [&] (int id, const char* name, int lo, int hi, int def)
        { l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[static_cast<std::size_t> (id)], 1 }, name, lo, hi, def)); };

        real (size, "Size", skewed (0.005f, 1.0f, 0.12f), 0.12f, secs);
        real (density, "Density", skewed (1.0f, 200.0f, 20.0f), 18.0f, [] (float v, int) { return String (v, v < 10.0f ? 1 : 0) + " /s"; });
        real (position, "Position", { 0.0f, 1.0f }, 0.35f, pct);
        real (scatter, "Scatter", { 0.0f, 1.0f }, 0.15f, pct);
        real (pitch, "Pitch", { -24.0f, 24.0f, 0.01f }, 0.0f, semis);
        real (pitchScatter, "Pitch Scatter", skewed (0.0f, 12.0f, 1.0f), 0.0f, semis);
        real (spread, "Spread", { 0.0f, 1.0f }, 0.35f, pct);
        real (reverse, "Reverse", { 0.0f, 1.0f }, 0.0f, pct);
        real (jitter, "Jitter", { 0.0f, 1.0f }, 0.25f, pct);
        choice (window, "Window", { "Hann", "Soft", "Percussive" }, 0);
        real (grainLevel, "Grain Level", { 0.0f, 1.0f }, 1.0f, pct);
        real (triggerVar, "Trigger Variation", { 0.0f, 1.0f }, 0.1f, pct);
        real (oscMix, "Osc / Grain", { 0.0f, 1.0f }, 0.0f, pct);
        real (oscShape, "Osc Shape", { 0.0f, 3.0f }, 1.0f, [] (float v, int) {
            const char* names[] = { "Sine", "Triangle", "Saw", "Square" };
            return String (names[jlimit (0, 3, roundToInt (v))]) + (std::abs (v - std::round (v)) > 0.05f ? "~" : "");
        });
        real (oscTune, "Osc Tune", { -24.0f, 24.0f, 0.01f }, 0.0f, semis);
        real (tone, "Tone", skewed (200.0f, 20000.0f, 3000.0f), 12000.0f, [] (float v, int) { return v < 1000.0f ? String (roundToInt (v)) + " Hz" : String (v / 1000.0f, 1) + " kHz"; });
        real (resonance, "Resonance", { 0.0f, 1.0f }, 0.2f, pct);
        real (attack, "Attack", skewed (0.001f, 5.0f, 0.1f), 0.02f, secs);
        real (decay, "Decay", skewed (0.01f, 8.0f, 0.6f), 0.3f, secs);
        real (sustain, "Sustain", { 0.0f, 1.0f }, 0.85f, pct);
        real (release, "Release", skewed (0.01f, 10.0f, 0.8f), 0.6f, secs);
        real (auxAttack, "Aux Attack", skewed (0.001f, 8.0f, 0.4f), 0.5f, secs);
        real (auxDecay, "Aux Decay", skewed (0.01f, 8.0f, 0.8f), 1.0f, secs);
        real (auxSustain, "Aux Sustain", { 0.0f, 1.0f }, 0.0f, pct);
        real (auxRelease, "Aux Release", skewed (0.01f, 10.0f, 0.8f), 1.0f, secs);
        real (lfo1Rate, "LFO 1 Rate", skewed (0.01f, 20.0f, 1.0f), 0.3f, [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; });
        choice (lfo1Shape, "LFO 1 Shape", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 0);
        real (lfo2Rate, "LFO 2 Rate", skewed (0.01f, 20.0f, 1.0f), 4.0f, [] (float v, int) { return String (v, v < 1.0f ? 2 : 1) + " Hz"; });
        choice (lfo2Shape, "LFO 2 Shape", { "Sine", "Triangle", "Saw", "Square", "S&H" }, 1);
        real (velocity, "Velocity", { 0.0f, 1.0f }, 0.5f, pct);
        real (bendRange, "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f, [] (float v, int) { return String (roundToInt (v)) + " st"; });
        real (gain, "Gain", { -36.0f, 12.0f, 0.1f }, 0.0f, [] (float v, int) { return String (v, 1) + " dB"; });
        real (macro1, "Macro 1", { 0.0f, 1.0f }, 0.0f, pct);
        real (macro2, "Macro 2", { 0.0f, 1.0f }, 0.0f, pct);
        real (dnaCurve, "DNA Curve Amount", { 0.0f, 1.0f }, 1.0f, pct);
        real (dnaVariation, "DNA Variation", { 0.0f, 1.0f }, 0.35f, pct);
        real (dnaIndividual, "Smooth / Individual", { 0.0f, 1.0f }, 0.3f, pct);
        integer (dnaSeed, "DNA Seed", 1, 9999, 1);
        choice (policy, "Source Policy", { "Mapped", "Closest", "Fixed Note", "Whole Source" }, 0);
        l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[fixedNote], 1 }, "Fixed Note", 0, 255, 0,
                                                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return "note " + String (v + 1); })));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[root], 1 }, "Root", 0, 127, 60,
                                                    AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (midiNoteName (v)); })));
        choice (quality, "Quality", { "Eco", "Standard", "High" }, 1);
        l.add (std::make_unique<AudioParameterBool> (ParameterID { ids[deterministic], 1 }, "Deterministic", true));
        integer (seed, "Seed", 1, 9999, 1);
        real (space, "Space", { 0.0f, 1.0f }, 0.2f, pct);
        real (echo, "Echo", { 0.0f, 1.0f }, 0.0f, pct);

        StringArray sources, dests;
        for (int i = 0; i < grain::numModSources; ++i)
            sources.add (grain::modSourceName (static_cast<grain::ModSource> (i)));
        for (int i = 0; i < grain::numModDests; ++i)
            dests.add (grain::modDestName (static_cast<grain::ModDest> (i)));
        for (int s = 0; s < grain::numModSlots; ++s)
        {
            const auto base = modBase + 3 * s;
            const auto label = "Mod " + String (s + 1);
            l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[static_cast<std::size_t> (base)], 1 }, label + " Source", sources, 0));
            l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[static_cast<std::size_t> (base + 1)], 1 }, label + " Destination", dests, 0));
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[static_cast<std::size_t> (base + 2)], 1 }, label + " Amount",
                                                          NormalisableRange<float> (-1.0f, 1.0f), 0.0f,
                                                          AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)) + " %"; })));
        }
        return l;
    }

    atlas::SourceAsset assetOf (const LoadedSource& s)
    {
        atlas::SourceAsset a;
        a.assetId = "src0";
        a.contentHash = io::contentId (s.sha256Hex);
        a.fileName = s.fileName;
        a.lastKnownPath = s.originalPath;
        a.managedCopy = s.storedPath;
        a.sampleRate = s.audio->sampleRate;
        a.numChannels = std::min (s.audio->numChannels(), 2);
        a.numFrames = s.audio->numFrames();
        return a;
    }

    std::string hexOf (const std::string& contentHash) { return contentHash.rfind ("sha256:", 0) == 0 ? contentHash.substr (7) : contentHash; }

    juce::ValueTree curvesToTree (const grain::KeyDnaSettings& dna)
    {
        juce::ValueTree t ("DNA");
        for (int p = 0; p < grain::numDnaParams; ++p)
        {
            juce::ValueTree c ("CURVE");
            c.setProperty ("param", grain::dnaParamId (static_cast<grain::DnaParam> (p)), nullptr);
            const auto& curve = dna.curves[static_cast<std::size_t> (p)];
            for (int i = 0; i < curve.count; ++i)
            {
                juce::ValueTree pt ("P");
                pt.setProperty ("k", curve.points[static_cast<std::size_t> (i)].key, nullptr);
                pt.setProperty ("v", curve.points[static_cast<std::size_t> (i)].value, nullptr);
                c.appendChild (pt, nullptr);
            }
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void curvesFromTree (const juce::ValueTree& t, grain::KeyDnaSettings& dna)
    {
        for (auto& c : dna.curves)
            c.clear();
        for (const auto& c : t)
            for (int p = 0; p < grain::numDnaParams; ++p)
                if (c.getProperty ("param").toString() == grain::dnaParamId (static_cast<grain::DnaParam> (p)))
                    for (const auto& pt : c)
                        dna.curves[static_cast<std::size_t> (p)].addPoint (static_cast<float> (pt.getProperty ("k")), static_cast<float> (pt.getProperty ("v")));
    }
}

//==============================================================================
struct GrainProcessor::Job
{
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> done { false };
    std::mutex lock;
    juce::String stage;

    std::optional<LoadedSource> source;
    std::shared_ptr<const atlas::NoteAtlas> atlas;
    double detectedRoot = 60.0;
    bool setRoot = false;
    bool recall = false;   ///< a session: its saved ROOT stays
    bool missing = false;
    std::string missingName;
    std::string error;
    std::vector<std::string> warnings;

    void setStage (const juce::String& s)
    {
        const std::lock_guard<std::mutex> guard (lock);
        stage = s;
    }
};

GrainProcessor::GrainProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PARAMS", layout()),
      pool (std::make_unique<juce::ThreadPool> (1))
{
    for (const auto& id : parameterIds())
        raw.push_back (state.getRawParameterValue (id));
    publishModel();
    publishCurves();
    startTimerHz (30);
}

GrainProcessor::~GrainProcessor()
{
    stopTimer();
    if (job != nullptr)
        job->cancel = true;
    pool->removeAllJobs (true, 10000);
}

bool GrainProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GrainProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    if (auto m = models.latestModel())
        engine.setModel (m.get());
}

grain::GrainParams GrainProcessor::currentParams() const
{
    auto v = [this] (int id) { return raw[static_cast<std::size_t> (id)]->load (std::memory_order_relaxed); };
    auto choice = [&] (int id, int count) { return juce::jlimit (0, count - 1, juce::roundToInt (v (id))); };
    grain::GrainParams p;
    p.sizeSeconds = v (size);
    p.density = v (density);
    p.position = v (position);
    p.positionScatter = v (scatter);
    p.pitchSemitones = v (pitch);
    p.pitchScatter = v (pitchScatter);
    p.spread = v (spread);
    p.reverseProbability = v (reverse);
    p.timingJitter = v (jitter);
    p.window = static_cast<grain::GrainWindow> (choice (window, 3));
    p.grainLevel = v (grainLevel);
    p.triggerVariation = v (triggerVar);
    p.oscMix = v (oscMix);
    p.oscShape = v (oscShape);
    p.oscTuneSemitones = v (oscTune);
    p.toneHz = v (tone);
    p.resonance = v (resonance);
    p.attackSeconds = v (attack);
    p.decaySeconds = v (decay);
    p.sustainLevel = v (sustain);
    p.releaseSeconds = v (release);
    p.auxAttack = v (auxAttack);
    p.auxDecay = v (auxDecay);
    p.auxSustain = v (auxSustain);
    p.auxRelease = v (auxRelease);
    p.lfo1Hz = v (lfo1Rate);
    p.lfo1Shape = static_cast<andor::Lfo::Shape> (choice (lfo1Shape, 5));
    p.lfo2Hz = v (lfo2Rate);
    p.lfo2Shape = static_cast<andor::Lfo::Shape> (choice (lfo2Shape, 5));
    p.velocitySensitivity = v (velocity);
    p.bendRangeSemitones = v (bendRange);
    p.gainDb = v (gain);
    p.macro1 = v (macro1);
    p.macro2 = v (macro2);
    p.dna.curveAmount = v (dnaCurve);
    p.dna.variationAmount = v (dnaVariation);
    p.dna.individuality = v (dnaIndividual);
    p.dna.seed = static_cast<std::uint64_t> (juce::roundToInt (v (dnaSeed)));
    p.policy = static_cast<grain::SourcePolicy> (choice (policy, 4));
    p.fixedNoteIndex = juce::roundToInt (v (fixedNote));
    p.wholeRootMidi = juce::roundToInt (v (root));
    p.quality = static_cast<grain::Quality> (choice (quality, 3));
    p.deterministic = v (deterministic) > 0.5f;
    p.seed = static_cast<std::uint64_t> (juce::roundToInt (v (seed))) * std::uint64_t { 0x9E3779B97F4A7C15ull };
    p.space = v (space);
    p.echo = v (echo);
    for (int s = 0; s < grain::numModSlots; ++s)
    {
        auto& slot = p.mod[static_cast<std::size_t> (s)];
        slot.source = static_cast<grain::ModSource> (choice (modBase + 3 * s, grain::numModSources));
        slot.dest = static_cast<grain::ModDest> (choice (modBase + 3 * s + 1, grain::numModDests));
        slot.amount = v (modBase + 3 * s + 2);
    }
    return p;
}

void GrainProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (const auto* m = models.takePending())
        engine.setModel (m);
    if (const auto* c = curves.takePending())
    {
        audioCurves = c->curves;   // a plain copy: the snapshot is released at once
        audioCurveGeneration = c->generation;
        curves.publishOldestInUse (c->generation);
    }
    auto params = currentParams();
    params.dna.curves = audioCurves;
    engine.setParams (params);

    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            const bool playing = position->getIsPlaying();
            if (playing && ! wasPlaying && params.deterministic)
                engine.resetEvents();
            wasPlaying = playing;
        }

    auditions.drain ([this] (const AuditionQueue::Event& e)
                     {
                         if (e.down)
                             engine.noteOn (e.key, e.velocity);
                         else
                             engine.noteOff (e.key);
                     });
    float* left = buffer.getWritePointer (0);
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    int done = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > done)
        {
            engine.render (left + done, right + done, at - done);
            done = at;
        }
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
            engine.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff())
            engine.noteOff (msg.getNoteNumber());
        else if (msg.isSustainPedalOn())
            engine.setSustainPedal (true);
        else if (msg.isSustainPedalOff())
            engine.setSustainPedal (false);
        else if (msg.isPitchWheel())
            engine.setPitchBend ((msg.getPitchWheelValue() - 8192) / 8192.0);
        else if (msg.isControllerOfType (1))
            engine.setModWheel (msg.getControllerValue() / 127.0);
        else if (msg.isAllSoundOff())
            engine.panic();
        else if (msg.isAllNotesOff())
            engine.allNotesOff();
    }
    if (done < n)
        engine.render (left + done, right + done, n - done);
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);
    models.publishOldestInUse (engine.oldestGenerationInUse());

    // Display.
    grain::GrainEngine::GrainView grains[256];
    const int count = engine.collectGrains (grains, 256);
    for (int i = 0; i < count; ++i)
    {
        grainsDisplay.a[static_cast<std::size_t> (i)].store (grains[i].position, std::memory_order_relaxed);
        grainsDisplay.b[static_cast<std::size_t> (i)].store (grains[i].level, std::memory_order_relaxed);
        grainsDisplay.c[static_cast<std::size_t> (i)].store (grains[i].voice, std::memory_order_relaxed);
    }
    grainsDisplay.count.store (count, std::memory_order_release);
    grainsDisplay.grains.store (engine.activeGrains(), std::memory_order_relaxed);
    grainsDisplay.voices.store (engine.activeVoices(), std::memory_order_relaxed);
    grain::GrainEngine::VoiceView voices[16];
    const int vc = engine.collectVoices (voices, 16);
    std::uint32_t mask[4] = {};
    for (int i = 0; i < vc; ++i)
    {
        voicesDisplay.keys[static_cast<std::size_t> (i)].store (voices[i].key, std::memory_order_relaxed);
        for (int p = 0; p < grain::numDnaParams; ++p)
            voicesDisplay.effective[static_cast<std::size_t> (i)][static_cast<std::size_t> (p)].store (voices[i].effective[static_cast<std::size_t> (p)], std::memory_order_relaxed);
        voicesDisplay.regionStart[static_cast<std::size_t> (i)].store (voices[i].regionStart, std::memory_order_relaxed);
        voicesDisplay.regionEnd[static_cast<std::size_t> (i)].store (voices[i].regionEnd, std::memory_order_relaxed);
        if (voices[i].key >= 0)
            mask[voices[i].key >> 5] |= 1u << (voices[i].key & 31);
    }
    voicesDisplay.count.store (vc, std::memory_order_release);
    for (int i = 0; i < 4; ++i)
        grainsDisplay.heldKeys[i].store (mask[i], std::memory_order_relaxed);
}

juce::AudioProcessorEditor* GrainProcessor::createEditor()
{
    return new GrainEditor (*this);
}

//==============================================================================
float GrainProcessor::progress() const noexcept { return job != nullptr ? job->progress.load() : 1.0f; }

juce::String GrainProcessor::status() const
{
    if (job != nullptr)
    {
        const std::lock_guard<std::mutex> guard (job->lock);
        return job->stage;
    }
    return lastStatus;
}

void GrainProcessor::startJob (std::function<void (Job&)> work)
{
    if (job != nullptr)
        job->cancel = true;
    auto j = std::make_shared<Job>();
    job = j;
    pool->addJob ([j, work = std::move (work)]
                  {
                      try
                      {
                          work (*j);
                      }
                      catch (const std::exception& e)
                      {
                          j->error = e.what();
                      }
                      j->done = true;
                  });
}

namespace
{
    double estimateRoot (const AudioData& audio)
    {
        // OSP's own root analysis of the whole file (its first ten seconds are plenty).
        AudioData head = audio;
        const auto limit = static_cast<std::size_t> (std::min<double> (10.0 * audio.sampleRate, static_cast<double> (audio.numFrames())));
        for (auto& ch : head.channels)
            ch.resize (limit);
        const auto analysis = Analyzer::analyse (head);
        return chooseRoot (&analysis, std::nullopt).rootMidi;
    }
}

void GrainProcessor::loadSample (const juce::File& file)
{
    auto* storePtr = &store;
    startJob ([file, storePtr] (Job& j)
              {
                  j.setStage ("Reading " + file.getFileName());
                  std::string error;
                  auto source = loadSource (*storePtr, file, {}, {}, 600.0, error);
                  if (! source)
                  {
                      j.error = error;
                      return;
                  }
                  j.setStage ("Finding the root of " + file.getFileName());
                  j.detectedRoot = estimateRoot (*source->audio);
                  j.setRoot = true;
                  j.warnings = source->warnings;
                  j.source = std::move (source);
              });
}

void GrainProcessor::loadAtlas (const juce::File& file)
{
    std::string error;
    const auto atlas = io::readNoteAtlas (file.getFullPathName().toStdString(), error);
    if (! atlas)
    {
        lastStatus = "Cannot read the Note Atlas: " + juce::String (error);
        documentChanged();
        return;
    }
    loadAtlas (*atlas, file.getParentDirectory());
}

void GrainProcessor::loadAtlas (const atlas::NoteAtlas& atlasIn, const juce::File& folder)
{
    auto* storePtr = &store;
    auto atlas = std::make_shared<const atlas::NoteAtlas> (atlasIn);
    startJob ([atlas, folder, storePtr] (Job& j)
              {
                  j.setStage ("Finding the Note Atlas' recording");
                  if (atlas->sources.empty())
                  {
                      j.error = "the Note Atlas has no source";
                      return;
                  }
                  const auto& src = atlas->sources[0];
                  std::string error;
                  std::optional<LoadedSource> loaded;
                  // The shared store first (by fingerprint), then the atlas' own paths.
                  loaded = loadSource (*storePtr, juce::File(), hexOf (src.contentHash), {}, 600.0, error);
                  if (! loaded)
                  {
                      if (const auto resolved = io::resolveSource (src, folder.getFullPathName().toStdString(), { storePtr->directory().getFullPathName().toStdString() }))
                          loaded = loadSource (*storePtr, juce::File (juce::String (resolved->path.string())), {}, {}, 600.0, error);
                  }
                  if (! loaded)
                  {
                      j.error = "the Note Atlas' recording was not found (" + src.fileName + ")";
                      j.missing = true;
                      j.missingName = src.fileName;
                      j.atlas = atlas;
                      return;
                  }
                  if (loaded->audio->numFrames() != src.numFrames || std::abs (loaded->audio->sampleRate - src.sampleRate) > 1.0e-6)
                  {
                      j.error = "the recording found does not match the Note Atlas (length or rate)";
                      return;
                  }
                  loaded->fileName = src.fileName.empty() ? loaded->fileName : src.fileName;
                  j.atlas = atlas;
                  j.source = std::move (loaded);
              });
}

void GrainProcessor::loadExample (Example which)
{
    auto* storePtr = &store;
    startJob ([which, storePtr] (Job& j)
              {
                  j.setStage (which == Example::pad ? "Generating the example tone" : "Analysing the example phrase into a Note Atlas");
                  const auto fx = phrase::makePhraseFixture (which == Example::pad ? "long-sustain" : "vocal-phrase");
                  std::string hex, error;
                  const auto stored = storePtr->importAudio (fx.audio, which == Example::pad ? "example-tone" : "example-vocal-phrase", hex, error);
                  if (! stored)
                  {
                      j.error = error;
                      return;
                  }
                  auto source = loadSource (*storePtr, *stored, hex, {}, 600.0, error);
                  if (! source)
                  {
                      j.error = error;
                      return;
                  }
                  source->fileName = which == Example::pad ? "Example tone (generated)" : "Example phrase (generated)";
                  if (which == Example::phraseAtlas)
                  {
                      auto result = phrase::analysePhrase (*source->audio, assetOf (*source), {}, &j.cancel, &j.progress);
                      result.atlas.createdBy = "ANDOR Grain example (Phrase analysis)";
                      j.atlas = std::make_shared<const atlas::NoteAtlas> (std::move (result.atlas));
                  }
                  else
                  {
                      j.detectedRoot = estimateRoot (*source->audio);
                      j.setRoot = true;
                  }
                  j.source = std::move (source);
              });
}

void GrainProcessor::clearSource()
{
    doc = Document {};
    publishModel();
    lastStatus = "No source: the oscillator plays alone";
    documentChanged();
}

void GrainProcessor::pollLoader()
{
    models.collectGarbage();
    curves.collectGarbage();
    if (job == nullptr || ! job->done)
        return;
    auto j = job;
    job.reset();
    if (! j->error.empty())
    {
        lastStatus = j->error;
        if (j->missing)
        {
            doc.sourceMissing = true;
            doc.missingName = j->missingName;
            doc.atlas = j->atlas;
        }
        documentChanged();
        return;
    }
    doc.source = std::make_shared<const LoadedSource> (std::move (*j->source));
    doc.atlas = j->atlas;
    doc.sourceMissing = false;
    doc.missingName.clear();
    doc.warnings = j->warnings;
    if (doc.atlas != nullptr && ! j->setRoot && ! j->recall)
    {
        // An atlas' "whole source" root: its most confident note.
        double best = -1.0;
        for (const auto& note : doc.atlas->notes)
            if (note.included && note.confidence > best)
            {
                best = note.confidence;
                j->detectedRoot = note.midiPitch;
                j->setRoot = true;
            }
    }
    if (j->setRoot)
    {
        doc.detectedRoot = j->detectedRoot;
        auto* rootParam = state.getParameter ("root");
        rootParam->setValueNotifyingHost (rootParam->convertTo0to1 (static_cast<float> (juce::roundToInt (j->detectedRoot))));
    }
    publishModel();
    lastStatus = doc.atlas != nullptr ? juce::String (static_cast<int> (doc.atlas->notes.size())) + " atlas notes" : "Root " + juce::String (midiNoteName (juce::roundToInt (doc.detectedRoot)));
    documentChanged();
}

bool GrainProcessor::waitForLoad (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (job != nullptr && juce::Time::getMillisecondCounter() < end)
    {
        juce::Thread::sleep (5);
        pollLoader();
    }
    return job == nullptr;
}

void GrainProcessor::publishModel()
{
    std::shared_ptr<const PlaybackSource> playback;
    if (doc.source != nullptr)
        playback = std::make_shared<const PlaybackSource> (*doc.source->audio, 60.0, grain::grainPadding());
    // The whole-source root follows the ROOT parameter (set from the analysis on load).
    const double rootMidi = state.getParameter ("root") != nullptr ? juce::roundToInt (raw[root]->load()) : 60;
    if (doc.atlas != nullptr)
        models.publish (grain::buildGrainModel (playback, doc.atlas, rootMidi, ++generation));
    else
        models.publish (grain::buildGrainModel (playback, rootMidi, ++generation));
}

void GrainProcessor::publishCurves()
{
    auto snapshot = std::make_shared<CurveSnapshot>();
    snapshot->generation = ++curveGeneration;
    snapshot->curves = dna.curves;
    curves.publish (snapshot);
}

void GrainProcessor::documentChanged()
{
    if (onDocumentChanged)
        onDocumentChanged();
}

void GrainProcessor::setCurve (grain::DnaParam p, const grain::DnaCurve& curve)
{
    dna.curves[static_cast<std::size_t> (p)] = curve;
    publishCurves();
}

void GrainProcessor::resetCurve (grain::DnaParam p)
{
    dna.curves[static_cast<std::size_t> (p)].clear();
    publishCurves();
}

void GrainProcessor::randomiseCurve (grain::DnaParam p)
{
    // Deterministic from the seed parameter and a running count, so "randomise" twice differs
    // but a session replays what was saved (the curve itself is saved).
    static std::uint64_t counter = 0;
    Prng rng (Prng::deriveSeed (static_cast<std::uint64_t> (juce::roundToInt (raw[dnaSeed]->load())), static_cast<std::uint64_t> (p) + 50, ++counter));
    grain::DnaCurve c;
    const int points = 3 + static_cast<int> (rng.nextBelow (4));
    for (int i = 0; i < points; ++i)
        c.addPoint (static_cast<float> (21.0 + (108.0 - 21.0) * i / (points - 1)), static_cast<float> (0.8 * rng.bipolar()));
    setCurve (p, c);
}

void GrainProcessor::regenerateSeed()
{
    auto* seedParam = state.getParameter ("dnaSeed");
    const int next = juce::roundToInt (raw[dnaSeed]->load()) % 9999 + 1;
    seedParam->setValueNotifyingHost (seedParam->convertTo0to1 (static_cast<float> (next)));
}

grain::KeyDnaTable GrainProcessor::dnaTable() const
{
    auto settings = currentParams().dna;
    settings.curves = dna.curves;
    grain::KeyDnaTable table;
    table.compute (settings);
    return table;
}

//==============================================================================
juce::ValueTree GrainProcessor::toTree()
{
    juce::ValueTree tree (stateTag);
    tree.setProperty ("version", stateVersion, nullptr);
    tree.setProperty ("product", "ANDOR Grain", nullptr);
    tree.appendChild (state.copyState(), nullptr);
    tree.appendChild (curvesToTree (dna), nullptr);
    juce::ValueTree source ("SOURCE");
    if (doc.source != nullptr)
    {
        source.setProperty ("hash", juce::String (doc.source->sha256Hex), nullptr);
        source.setProperty ("name", juce::String (doc.source->fileName), nullptr);
        source.setProperty ("originalPath", juce::String (doc.source->originalPath), nullptr);
    }
    else if (doc.atlas != nullptr && ! doc.atlas->sources.empty())
    {
        source.setProperty ("hash", juce::String (hexOf (doc.atlas->sources[0].contentHash)), nullptr);
        source.setProperty ("name", juce::String (doc.atlas->sources[0].fileName), nullptr);
        source.setProperty ("originalPath", juce::String (doc.atlas->sources[0].lastKnownPath), nullptr);
    }
    tree.appendChild (source, nullptr);
    if (doc.atlas != nullptr)
        tree.setProperty ("atlas", juce::String (io::noteAtlasToString (*doc.atlas)), nullptr);
    return tree;
}

bool GrainProcessor::fromTree (const juce::ValueTree& tree, std::string& error)
{
    if (! tree.hasType (stateTag))
    {
        error = "not an ANDOR Grain state";
        return false;
    }
    if (static_cast<int> (tree.getProperty ("version", 0)) > stateVersion)
        doc.warnings.push_back ("this session was saved by a newer ANDOR Grain; some settings may be ignored");
    const auto params = tree.getChildWithName ("PARAMS");
    if (params.isValid())
        state.replaceState (params);
    curvesFromTree (tree.getChildWithName ("DNA"), dna);
    publishCurves();

    std::shared_ptr<const atlas::NoteAtlas> atlas;
    if (const auto text = tree.getProperty ("atlas").toString(); text.isNotEmpty())
    {
        std::string parseError;
        if (auto parsed = io::noteAtlasFromString (text.toStdString(), parseError))
            atlas = std::make_shared<const atlas::NoteAtlas> (std::move (*parsed));
    }
    const auto source = tree.getChildWithName ("SOURCE");
    const auto hash = source.getProperty ("hash").toString().toStdString();
    const auto originalPath = source.getProperty ("originalPath").toString().toStdString();
    const auto name = source.getProperty ("name").toString().toStdString();
    if (hash.empty())
    {
        doc = Document {};
        publishModel();
        documentChanged();
        return true;
    }
    auto* storePtr = &store;
    startJob ([storePtr, hash, originalPath, name, atlas] (Job& j)
              {
                  j.setStage ("Recalling " + juce::String (name));
                  j.recall = true;
                  std::string err;
                  auto loaded = loadSource (*storePtr, juce::File(), hash, originalPath, 600.0, err);
                  if (! loaded)
                  {
                      j.error = err;
                      j.missing = true;
                      j.missingName = name;
                      j.atlas = atlas;
                      return;
                  }
                  loaded->fileName = name.empty() ? loaded->fileName : name;
                  j.warnings = loaded->warnings;
                  j.atlas = atlas;
                  j.source = std::move (loaded);
                  // The ROOT parameter is recalled as saved (no re-analysis).
              });
    return true;
}

void GrainProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = toTree().createXml())
        copyXmlToBinary (*xml, data);
}

void GrainProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        std::string error;
        if (! fromTree (juce::ValueTree::fromXml (*xml), error))
            lastStatus = error;
    }
}

juce::File GrainProcessor::presetFolder()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ANDOR_DOCUMENTS_DIR", {});
    const auto rootDir = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR Lab");
    return rootDir.getChildFile ("Grain Presets");
}

bool GrainProcessor::savePreset (const juce::File& file, std::string& error)
{
    auto xml = toTree().createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
    {
        error = "cannot write " + file.getFullPathName().toStdString();
        return false;
    }
    return true;
}

bool GrainProcessor::loadPreset (const juce::File& file, std::string& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = "cannot read " + file.getFileName().toStdString();
        return false;
    }
    return fromTree (juce::ValueTree::fromXml (*xml), error);
}

} // namespace osp::andorlab

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new osp::andorlab::GrainProcessor();
}
