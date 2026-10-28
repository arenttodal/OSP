#include "PhraseProcessor.h"

#include "PhraseEditor.h"

#include "andor/io/NoteAtlasJson.h"
#include "andor/phrase/PhraseEdits.h"
#include "andor/phrase/PhraseFixtures.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"

namespace osp::andorlab
{

namespace
{
    // Parameter IDs are stable: they are the session and automation identity.
    const char* const ids[] = { "playMode", "roundRobin", "fill", "tuning", "pitch", "fine", "bendRange", "gain", "velocity",
                                "attack", "decay", "sustain", "release", "attackPreserve", "startOffset", "tone", "synthetic",
                                "sustainCharacter", "levelMatch", "space", "echo", "polyphony", "seed", "detection", "minNote" };
    enum Id { playMode, roundRobin, fill, tuning, pitch, fine, bendRange, gain, velocity, attack, decay, sustain, release,
              attackPreserve, startOffset, tone, synthetic, sustainCharacter, levelMatch, space, echo, polyphony, seed,
              detection, minNote, numIds };
    static_assert (numIds <= 32, "raw[] holds 32 parameters");

    juce::NormalisableRange<float> timeRange (float lo, float hi, float centre)
    {
        juce::NormalisableRange<float> r (lo, hi);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    juce::String seconds (float v, int)
    {
        return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 2) + " s";
    }

    juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        auto choice = [&] (int id, const char* name, StringArray items, int def)
        { l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[id], 1 }, name, items, def)); };
        auto real = [&] (int id, const char* name, NormalisableRange<float> range, float def, std::function<String (float, int)> text = {})
        {
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, range, def,
                                                          AudioParameterFloatAttributes().withStringFromValueFunction (std::move (text))));
        };
        choice (playMode, "Play Mode", { "One Shot", "Gate", "Sustain" }, 2);
        choice (roundRobin, "Variation", { "Sequential", "Random", "Shuffle" }, 2);
        choice (fill, "Fill Keyboard", { "Off", "Natural", "Balanced", "Wide", "Extended" }, 2);
        choice (tuning, "Tuning", { "Corrected", "Original" }, 0);
        real (pitch, "Pitch", { -24.0f, 24.0f, 1.0f }, 0.0f, [] (float v, int) { return (v > 0 ? "+" : "") + String (juce::roundToInt (v)) + " st"; });
        real (fine, "Fine", { -100.0f, 100.0f, 0.1f }, 0.0f, [] (float v, int) { return String (v, 1) + " c"; });
        real (bendRange, "Bend Range", { 0.0f, 24.0f, 1.0f }, 2.0f, [] (float v, int) { return String (juce::roundToInt (v)) + " st"; });
        real (gain, "Gain", { -36.0f, 12.0f, 0.1f }, 0.0f, [] (float v, int) { return String (v, 1) + " dB"; });
        real (velocity, "Velocity", { 0.0f, 1.0f }, 0.6f, pct);
        real (attack, "Attack", timeRange (0.001f, 2.0f, 0.05f), 0.002f, seconds);
        real (decay, "Decay", timeRange (0.01f, 5.0f, 0.5f), 0.3f, seconds);
        real (sustain, "Sustain", { 0.0f, 1.0f }, 1.0f, pct);
        real (release, "Release", timeRange (0.01f, 8.0f, 0.6f), 0.25f, seconds);
        real (attackPreserve, "Attack Preservation", { 0.0f, 1.0f }, 1.0f, pct);
        real (startOffset, "Start Offset", timeRange (0.0f, 0.5f, 0.08f), 0.0f, seconds);
        real (tone, "Tone", { 0.0f, 1.0f }, 1.0f, pct);
        real (synthetic, "Natural / Synthetic", { 0.0f, 1.0f }, 0.0f, pct);
        real (sustainCharacter, "Sustain Character", { 0.0f, 1.0f }, 0.0f, pct);
        real (levelMatch, "Level Match", { 0.0f, 1.0f }, 0.5f, pct);
        real (space, "Space", { 0.0f, 1.0f }, 0.0f, pct);
        real (echo, "Echo", { 0.0f, 1.0f }, 0.0f, pct);
        l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[polyphony], 1 }, "Polyphony", 4, 28, 16));
        l.add (std::make_unique<AudioParameterInt> (ParameterID { ids[seed], 1 }, "Seed", 1, 9999, 1));
        // Analysis choices: saved with the session, never automated (each change re-analyses).
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[detection], 1 }, "Detection", StringArray { "Fewer Notes", "Normal", "More Notes" }, 1,
                                                       AudioParameterChoiceAttributes().withAutomatable (false)));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { ids[minNote], 1 }, "Min Note", NormalisableRange<float> (0.05f, 0.5f, 0.01f), 0.25f,
                                                      AudioParameterFloatAttributes().withAutomatable (false).withStringFromValueFunction (seconds)));
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
        return a;
    }

    std::string hexOf (const std::string& contentHash)
    {
        return contentHash.rfind ("sha256:", 0) == 0 ? contentHash.substr (7) : contentHash;
    }
}

//==============================================================================
struct PhraseProcessor::Job
{
    std::atomic<bool> cancel { false };
    std::atomic<float> progress { 0.0f };
    std::atomic<bool> done { false };
    std::mutex lock;
    juce::String stage;   ///< guarded by lock

    // Results (written by the worker before `done`, read by the message thread after it).
    std::optional<LoadedSource> source;
    std::optional<atlas::NoteAtlas> atlas;
    bool mergeWithEdits = false;
    std::optional<phrase::AnalysisSettings> settings;   ///< what the atlas was analysed with (when it was)
    juce::String undoName;
    bool missing = false;
    std::string missingName;
    std::string error;
    bool cancelled = false;
    std::vector<std::string> warnings;

    void setStage (const juce::String& s)
    {
        const std::lock_guard<std::mutex> guard (lock);
        stage = s;
    }
};

class AtlasEdit final : public juce::UndoableAction
{
public:
    AtlasEdit (PhraseProcessor& p, atlas::NoteAtlas b, atlas::NoteAtlas a) : processor (p), before (std::move (b)), after (std::move (a)) {}
    bool perform() override
    {
        processor.setAtlas (after);
        return true;
    }
    bool undo() override
    {
        processor.setAtlas (before);
        return true;
    }
    int getSizeInUnits() override { return static_cast<int> (64 * (before.notes.size() + after.notes.size()) + 1); }

private:
    PhraseProcessor& processor;
    atlas::NoteAtlas before, after;
};

/** A re-analysis: the notes and the settings they came from, as one undo step. Undo puts the
    DETECTION / MIN NOTE controls back too, without analysing again. */
class AnalysisEdit final : public juce::UndoableAction
{
public:
    AnalysisEdit (PhraseProcessor& p, atlas::NoteAtlas b, phrase::AnalysisSettings bs, atlas::NoteAtlas a, phrase::AnalysisSettings as)
        : processor (p), before (std::move (b)), after (std::move (a)), beforeSettings (bs), afterSettings (as)
    {
    }
    bool perform() override
    {
        // The first time, the controls already show these settings (and may have moved on since).
        processor.setAnalysis (after, afterSettings, ! firstPerform);
        firstPerform = false;
        return true;
    }
    bool undo() override
    {
        processor.setAnalysis (before, beforeSettings, true);
        return true;
    }
    int getSizeInUnits() override { return static_cast<int> (64 * (before.notes.size() + after.notes.size()) + 1); }

private:
    PhraseProcessor& processor;
    atlas::NoteAtlas before, after;
    phrase::AnalysisSettings beforeSettings, afterSettings;
    bool firstPerform = true;
};

//==============================================================================
PhraseProcessor::PhraseProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "PARAMS", layout()),
      pool (std::make_unique<juce::ThreadPool> (1))
{
    for (int i = 0; i < numIds; ++i)
        raw[static_cast<std::size_t> (i)] = state.getRawParameterValue (ids[i]);
    publishModel();
    startTimerHz (30);
}

PhraseProcessor::~PhraseProcessor()
{
    stopTimer();
    if (job != nullptr)
        job->cancel = true;
    pool->removeAllJobs (true, 10000);
}

bool PhraseProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PhraseProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    if (auto m = models.latestModel())
        engine.setModel (m.get());
}

phrase::PhraseParams PhraseProcessor::currentParams() const
{
    auto v = [this] (int id) { return raw[static_cast<std::size_t> (id)]->load (std::memory_order_relaxed); };
    phrase::PhraseParams p;
    p.playMode = static_cast<phrase::PlayMode> (juce::jlimit (0, 2, juce::roundToInt (v (playMode))));
    p.roundRobin = static_cast<phrase::RoundRobin> (juce::jlimit (0, 2, juce::roundToInt (v (roundRobin))));
    p.fill = static_cast<phrase::FillMode> (juce::jlimit (0, 4, juce::roundToInt (v (fill))));
    p.tuning = static_cast<phrase::Tuning> (juce::jlimit (0, 1, juce::roundToInt (v (tuning))));
    p.pitchSemitones = v (pitch);
    p.fineCents = v (fine);
    p.bendRangeSemitones = v (bendRange);
    p.gainDb = v (gain);
    p.velocitySensitivity = v (velocity);
    p.attackSeconds = v (attack);
    p.decaySeconds = v (decay);
    p.sustainLevel = v (sustain);
    p.releaseSeconds = v (release);
    p.attackPreservation = v (attackPreserve);
    p.startOffsetSeconds = v (startOffset);
    p.tone = v (tone);
    p.synthetic = v (synthetic);
    p.sustainCharacter = v (sustainCharacter);
    p.levelMatch = v (levelMatch);
    p.space = v (space);
    p.echo = v (echo);
    p.polyphony = juce::roundToInt (v (polyphony));
    p.seed = static_cast<std::uint64_t> (juce::roundToInt (v (seed))) * std::uint64_t { 0x9E3779B97F4A7C15ull };
    return p;
}

void PhraseProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (const auto* m = models.takePending())
        engine.setModel (m);
    engine.setParams (currentParams());

    // Deterministic bounces: round robins and random draws restart when the transport starts.
    if (auto* head = getPlayHead())
        if (const auto position = head->getPosition())
        {
            const bool playing = position->getIsPlaying();
            if (playing && ! wasPlaying)
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

    // Display: playing regions and read heads; sounding keys.
    phrase::PhraseEngine::VoiceView views[32];
    const int count = engine.collect (views, 32);
    std::uint32_t mask[4] = {};
    for (int i = 0; i < count; ++i)
    {
        display.a[static_cast<std::size_t> (i)].store (views[i].position, std::memory_order_relaxed);
        display.b[static_cast<std::size_t> (i)].store (views[i].level, std::memory_order_relaxed);
        display.c[static_cast<std::size_t> (i)].store (views[i].region, std::memory_order_relaxed);
        if (views[i].key >= 0)
            mask[views[i].key >> 5] |= 1u << (views[i].key & 31);
    }
    display.count.store (count, std::memory_order_release);
    display.voices.store (engine.activeVoices(), std::memory_order_relaxed);
    for (int i = 0; i < 4; ++i)
        display.heldKeys[i].store (mask[i], std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PhraseProcessor::createEditor()
{
    return new PhraseEditor (*this);
}

//==============================================================================
float PhraseProcessor::progress() const noexcept
{
    return job != nullptr ? job->progress.load() : 1.0f;
}

juce::String PhraseProcessor::status() const
{
    if (job != nullptr)
    {
        const std::lock_guard<std::mutex> guard (job->lock);
        return job->stage;
    }
    return lastStatus;
}

void PhraseProcessor::startJob (std::function<void (Job&)> work)
{
    if (job != nullptr)
        job->cancel = true;   // the older one finishes into nothing (its result is dropped)
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

void PhraseProcessor::loadFile (const juce::File& file)
{
    const auto settings = analysisSettings();
    auto* storePtr = &store;
    startJob ([file, settings, storePtr] (Job& j)
              {
                  j.setStage ("Reading " + file.getFileName());
                  std::string error;
                  auto source = loadSource (*storePtr, file, {}, {}, 300.0, error);
                  if (! source)
                  {
                      j.error = error;
                      return;
                  }
                  j.warnings = source->warnings;
                  // A cached analysis of these exact bytes, version and settings is reused.
                  const auto cache = storePtr->analysisCache (source->sha256Hex, phrase::analysisVersion, settings.digest());
                  if (cache.existsAsFile())
                  {
                      if (auto cached = io::readNoteAtlas (cache.getFullPathName().toStdString(), error);
                          cached && cached->analysis.version == phrase::analysisVersion && ! cached->sources.empty())
                      {
                          cached->sources[0] = assetOf (*source);
                          cached->sources[0].sampleRate = source->audio->sampleRate;
                          cached->sources[0].numChannels = std::min (source->audio->numChannels(), 2);
                          cached->sources[0].numFrames = source->audio->numFrames();
                          j.atlas = std::move (*cached);
                          j.settings = settings;
                          j.source = std::move (source);
                          return;
                      }
                  }
                  j.setStage ("Analysing " + file.getFileName());
                  auto result = phrase::analysePhrase (*source->audio, assetOf (*source), settings, &j.cancel, &j.progress);
                  if (result.cancelled)
                  {
                      j.cancelled = true;
                      return;
                  }
                  for (auto& w : result.warnings)
                      j.warnings.push_back (w);
                  io::writeNoteAtlas (cache.getFullPathName().toStdString(), result.atlas, error);
                  j.atlas = std::move (result.atlas);
                  j.settings = settings;
                  j.source = std::move (source);
              });
}

void PhraseProcessor::loadExample()
{
    auto* storePtr = &store;
    const auto settings = analysisSettings();
    startJob ([storePtr, settings] (Job& j)
              {
                  j.setStage ("Generating the example phrase");
                  const auto fx = phrase::makePhraseFixture ("vocal-phrase");
                  std::string hex, error;
                  const auto stored = storePtr->importAudio (fx.audio, "example-vocal-phrase", hex, error);
                  if (! stored)
                  {
                      j.error = error;
                      return;
                  }
                  auto source = loadSource (*storePtr, *stored, hex, {}, 300.0, error);
                  if (! source)
                  {
                      j.error = error;
                      return;
                  }
                  source->fileName = "Example phrase (generated)";
                  j.setStage ("Analysing the example");
                  auto result = phrase::analysePhrase (*source->audio, assetOf (*source), settings, &j.cancel, &j.progress);
                  j.atlas = std::move (result.atlas);
                  j.settings = settings;
                  j.source = std::move (source);
              });
}

void PhraseProcessor::cancelLoad()
{
    if (job != nullptr)
        job->cancel = true;
}

void PhraseProcessor::reanalyse()
{
    startReanalysis (analysisSettings(), "Re-analyse");
}

phrase::AnalysisSettings PhraseProcessor::analysisSettings() const
{
    phrase::AnalysisSettings settings;
    const auto d = juce::roundToInt (raw[static_cast<std::size_t> (detection)]->load (std::memory_order_relaxed));
    settings.detection = d == 0 ? phrase::Detection::fewer : (d == 2 ? phrase::Detection::more : phrase::Detection::normal);
    settings.minPieceSeconds = raw[static_cast<std::size_t> (minNote)]->load (std::memory_order_relaxed);
    return settings;
}

juce::String PhraseProcessor::describeChange (const phrase::AnalysisSettings& from, const phrase::AnalysisSettings& to) const
{
    if (from.detection != to.detection)
        return to.detection == phrase::Detection::fewer ? "Detection: fewer notes" : (to.detection == phrase::Detection::more ? "Detection: more notes" : "Detection: normal");
    if (std::abs (from.minPieceSeconds - to.minPieceSeconds) > 1.0e-6)
        return "Min note " + juce::String (juce::roundToInt (to.minPieceSeconds * 1000.0)) + " ms";
    return "Re-analyse";
}

void PhraseProcessor::startReanalysis (const phrase::AnalysisSettings& settings, const juce::String& undoName)
{
    if (doc.source == nullptr)
        return;
    pendingDigest.clear();
    auto source = doc.source;
    auto* storePtr = &store;
    startJob ([source, settings, undoName, storePtr] (Job& j)
              {
                  j.setStage ("Re-analysing (your corrections are kept)");
                  j.settings = settings;
                  j.undoName = undoName;
                  j.mergeWithEdits = true;
                  // Each setting's analysis is cached, so trying one again is instant.
                  std::string error;
                  const auto cache = storePtr->analysisCache (source->sha256Hex, phrase::analysisVersion, settings.digest());
                  if (cache.existsAsFile())
                      if (auto cached = io::readNoteAtlas (cache.getFullPathName().toStdString(), error);
                          cached && cached->analysis.version == phrase::analysisVersion && ! cached->sources.empty())
                      {
                          cached->sources[0] = assetOf (*source);
                          cached->sources[0].sampleRate = source->audio->sampleRate;
                          cached->sources[0].numChannels = std::min (source->audio->numChannels(), 2);
                          cached->sources[0].numFrames = source->audio->numFrames();
                          j.atlas = std::move (*cached);
                          return;
                      }
                  auto result = phrase::analysePhrase (*source->audio, assetOf (*source), settings, &j.cancel, &j.progress);
                  if (result.cancelled)
                  {
                      j.cancelled = true;
                      return;
                  }
                  io::writeNoteAtlas (cache.getFullPathName().toStdString(), result.atlas, error);
                  j.atlas = std::move (result.atlas);
              });
}

void PhraseProcessor::applyAnalysisSettingsNow()
{
    const auto wanted = analysisSettings();
    if (doc.source != nullptr && wanted.digest() != analysed.digest())
        startReanalysis (wanted, describeChange (analysed, wanted));
}

void PhraseProcessor::timerCallback()
{
    pollLoader();
    // DETECTION / MIN NOTE: re-analyse once the control has rested for a moment (a dragged
    // slider does not start an analysis per pixel).
    if (doc.source == nullptr || job != nullptr)
        return;
    const auto wanted = analysisSettings();
    const auto digest = wanted.digest();
    if (digest == analysed.digest())
    {
        pendingDigest.clear();
        return;
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (digest != pendingDigest)
    {
        pendingDigest = digest;
        pendingSince = now;
        return;
    }
    if (now - pendingSince >= 350)
        startReanalysis (wanted, describeChange (analysed, wanted));
}

void PhraseProcessor::setControls (const phrase::AnalysisSettings& settings)
{
    if (auto* p = state.getParameter (ids[detection]))
        p->setValueNotifyingHost (p->convertTo0to1 (settings.detection == phrase::Detection::fewer ? 0.0f : (settings.detection == phrase::Detection::more ? 2.0f : 1.0f)));
    if (auto* p = state.getParameter (ids[minNote]))
        p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (settings.minPieceSeconds)));
}

void PhraseProcessor::setAnalysis (atlas::NoteAtlas a, const phrase::AnalysisSettings& settings, bool restoreControls)
{
    analysed = settings;
    if (restoreControls)
    {
        setControls (settings);
        pendingDigest.clear();
    }
    setAtlas (std::move (a));
}

void PhraseProcessor::pollLoader()
{
    models.collectGarbage();
    if (job == nullptr || ! job->done)
        return;
    auto j = job;
    job.reset();
    if (j->cancelled)
    {
        lastStatus = "Cancelled";
        documentChanged();
        return;
    }
    if (! j->error.empty())
    {
        lastStatus = j->error;
        if (j->missing || (j->atlas && ! j->source))
        {
            doc.sourceMissing = true;
            doc.missingName = j->missingName;
            if (j->atlas)
                doc.atlas = *j->atlas;
        }
        documentChanged();
        return;
    }
    if (j->mergeWithEdits && j->atlas)
    {
        auto merged = phrase::edit::mergeReanalysis (doc.atlas, *j->atlas);
        const auto settings = j->settings.value_or (analysed);
        const auto name = j->undoName.isNotEmpty() ? j->undoName : juce::String ("Re-analyse");
        undo.beginNewTransaction (name);
        undo.perform (new AnalysisEdit (*this, doc.atlas, analysed, merged, settings), name);
        int included = 0;
        for (const auto& note : merged.notes)
            included += note.included ? 1 : 0;
        lastStatus = juce::String (included) + " notes  (" + name + ")";
        documentChanged();
        return;
    }
    if (j->source)
    {
        doc.source = std::make_shared<const LoadedSource> (std::move (*j->source));
        doc.playback = std::make_shared<const PlaybackSource> (*doc.source->audio, 60.0, phrase::phrasePadding());
        doc.sourceMissing = false;
        doc.missingName.clear();
    }
    doc.warnings = j->warnings;
    if (j->atlas)
        doc.atlas = std::move (*j->atlas);
    // A recalled session plays its saved notes as they were: whatever the controls say now is
    // what they were analysed with (no surprise re-analysis on reopening).
    analysed = j->settings.value_or (analysisSettings());
    pendingDigest.clear();
    undo.clearUndoHistory();
    publishModel();
    int included = 0;
    for (const auto& note : doc.atlas.notes)
        included += note.included ? 1 : 0;
    lastStatus = doc.atlas.notes.empty() ? juce::String ("No notes found") : juce::String (included) + " notes";
    documentChanged();
}

bool PhraseProcessor::waitForLoad (int timeoutMs)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (job != nullptr && juce::Time::getMillisecondCounter() < end)
    {
        juce::Thread::sleep (5);
        pollLoader();
    }
    return job == nullptr;
}

bool PhraseProcessor::edit (const juce::String& name, const std::function<bool (atlas::NoteAtlas&)>& change)
{
    auto after = doc.atlas;
    if (! change (after))
        return false;
    undo.beginNewTransaction (name);
    return undo.perform (new AtlasEdit (*this, doc.atlas, std::move (after)), name);
}

void PhraseProcessor::setAtlas (atlas::NoteAtlas a)
{
    doc.atlas = std::move (a);
    publishModel();
    documentChanged();
}

void PhraseProcessor::publishModel()
{
    auto previous = models.latestModel();
    models.publish (phrase::buildPhraseModel (doc.playback, doc.atlas, ++generation, previous.get()));
}

void PhraseProcessor::documentChanged()
{
    if (onDocumentChanged)
        onDocumentChanged();
}

bool PhraseProcessor::exportAtlas (const juce::File& file, std::string& error) const
{
    if (doc.atlas.sources.empty())
    {
        error = "nothing to export yet";
        return false;
    }
    auto a = doc.atlas;
    a.createdBy = "ANDOR Phrase " JucePlugin_VersionString;
    // The managed copy is the reliable path; the original stays as the human-readable one.
    if (doc.source != nullptr && a.sources[0].lastKnownPath.empty())
        a.sources[0].lastKnownPath = doc.source->storedPath;
    return io::writeNoteAtlas (file.getFullPathName().toStdString(), a, error);
}

bool PhraseProcessor::relink (const juce::File& file, std::string& error)
{
    if (doc.atlas.sources.empty())
    {
        error = "no session to relink";
        return false;
    }
    auto relinked = doc.atlas;
    atlas::RelinkResult match {};
    if (! io::relinkSource (relinked, relinked.sources[0].assetId, file.getFullPathName().toStdString(), error, &match))
        return false;
    const auto hash = hexOf (relinked.sources[0].contentHash);
    auto* storePtr = &store;
    startJob ([file, hash, relinked, storePtr] (Job& j)
              {
                  j.setStage ("Relinking " + file.getFileName());
                  std::string err;
                  auto source = loadSource (*storePtr, file, {}, file.getFullPathName().toStdString(), 300.0, err);
                  if (! source)
                  {
                      j.error = err;
                      j.atlas = relinked;
                      return;
                  }
                  j.source = std::move (source);
                  j.atlas = relinked;
              });
    return true;
}

//==============================================================================
juce::ValueTree PhraseProcessor::toTree()
{
    juce::ValueTree tree (stateTag);
    tree.setProperty ("version", stateVersion, nullptr);
    tree.setProperty ("product", "ANDOR Phrase", nullptr);
    tree.appendChild (state.copyState(), nullptr);
    juce::ValueTree source ("SOURCE");
    if (doc.source != nullptr)
    {
        source.setProperty ("hash", juce::String (doc.source->sha256Hex), nullptr);
        source.setProperty ("name", juce::String (doc.source->fileName), nullptr);
        source.setProperty ("originalPath", juce::String (doc.source->originalPath), nullptr);
    }
    else if (! doc.atlas.sources.empty())
    {
        // A missing source stays referenced so a later relink or a moved store finds it.
        source.setProperty ("hash", juce::String (hexOf (doc.atlas.sources[0].contentHash)), nullptr);
        source.setProperty ("name", juce::String (doc.atlas.sources[0].fileName), nullptr);
        source.setProperty ("originalPath", juce::String (doc.atlas.sources[0].lastKnownPath), nullptr);
    }
    tree.appendChild (source, nullptr);
    if (! doc.atlas.sources.empty())
        tree.setProperty ("atlas", juce::String (io::noteAtlasToString (doc.atlas)), nullptr);
    return tree;
}

bool PhraseProcessor::fromTree (const juce::ValueTree& tree, std::string& error)
{
    if (! tree.hasType (stateTag))
    {
        error = "not an ANDOR Phrase state";
        return false;
    }
    if (static_cast<int> (tree.getProperty ("version", 0)) > stateVersion)
        doc.warnings.push_back ("this session was saved by a newer ANDOR Phrase; some settings may be ignored");
    const auto params = tree.getChildWithName ("PARAMS");
    if (params.isValid())
        state.replaceState (params);

    std::optional<atlas::NoteAtlas> recalled;
    const auto text = tree.getProperty ("atlas").toString();
    if (text.isNotEmpty())
    {
        std::string parseError;
        recalled = io::noteAtlasFromString (text.toStdString(), parseError);
        if (! recalled)
            doc.warnings.push_back ("the saved analysis could not be read (" + parseError + ")");
    }
    const auto source = tree.getChildWithName ("SOURCE");
    const auto hash = source.getProperty ("hash").toString().toStdString();
    const auto originalPath = source.getProperty ("originalPath").toString().toStdString();
    const auto name = source.getProperty ("name").toString().toStdString();

    undo.clearUndoHistory();
    if (hash.empty())
    {
        doc = Document {};
        if (recalled)
            doc.atlas = *recalled;
        analysed = analysisSettings();
        pendingDigest.clear();
        publishModel();
        documentChanged();
        return true;
    }
    // Recall plays the saved atlas as it was: no re-analysis, edits intact.
    auto* storePtr = &store;
    const bool haveAtlas = recalled.has_value();
    const auto settings = analysisSettings();
    startJob ([storePtr, hash, originalPath, name, recalled, haveAtlas, settings] (Job& j)
              {
                  j.setStage ("Recalling " + juce::String (name));
                  std::string err;
                  auto loaded = loadSource (*storePtr, juce::File(), hash, originalPath, 300.0, err);
                  if (! loaded)
                  {
                      j.error = err;
                      j.missing = true;
                      j.missingName = name;
                      j.atlas = recalled;
                      return;
                  }
                  j.warnings = loaded->warnings;
                  loaded->fileName = name.empty() ? loaded->fileName : name;
                  if (haveAtlas)
                      j.atlas = recalled;
                  else
                  {
                      auto result = phrase::analysePhrase (*loaded->audio, assetOf (*loaded), settings, &j.cancel, &j.progress);
                      j.atlas = std::move (result.atlas);
                      j.settings = settings;
                  }
                  j.source = std::move (loaded);
              });
    return true;
}

void PhraseProcessor::getStateInformation (juce::MemoryBlock& data)
{
    if (auto xml = toTree().createXml())
        copyXmlToBinary (*xml, data);
}

void PhraseProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        std::string error;
        if (! fromTree (juce::ValueTree::fromXml (*xml), error))
            lastStatus = error;
    }
}

juce::File PhraseProcessor::presetFolder()
{
    // Phrase's own namespace: it never reads or writes Grain's (or OSP's) presets.
    const auto env = juce::SystemStats::getEnvironmentVariable ("ANDOR_DOCUMENTS_DIR", {});
    const auto root = env.isNotEmpty() ? juce::File (env) : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR Lab");
    return root.getChildFile ("Phrase Presets");
}

bool PhraseProcessor::savePreset (const juce::File& file, std::string& error)
{
    auto xml = toTree().createXml();
    if (xml == nullptr || ! file.getParentDirectory().createDirectory() || ! xml->writeTo (file))
    {
        error = "cannot write " + file.getFullPathName().toStdString();
        return false;
    }
    return true;
}

bool PhraseProcessor::loadPreset (const juce::File& file, std::string& error)
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
    return new osp::andorlab::PhraseProcessor();
}
