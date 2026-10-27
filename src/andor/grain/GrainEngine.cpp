#include "andor/grain/GrainEngine.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>

namespace osp::grain
{

int grainPadding() noexcept { return 8; }

QualityLimits limitsFor (Quality q) noexcept
{
    switch (q)
    {
        case Quality::eco: return { 8, 96 };
        case Quality::standard: return { 12, 192 };
        case Quality::high: return { 16, 384 };
    }
    return { 12, 192 };
}

const char* modSourceName (ModSource s) noexcept
{
    static const char* names[] = { "None", "Velocity", "Note", "Mod Wheel", "Amp Env", "Aux Env", "LFO 1", "LFO 2", "Macro 1", "Macro 2" };
    return names[static_cast<int> (s)];
}

const char* modDestName (ModDest d) noexcept
{
    static const char* names[] = { "None", "Size", "Density", "Position", "Scatter", "Pitch Scatter", "Grain Level", "Osc Mix", "Cutoff", "Spread" };
    return names[static_cast<int> (d)];
}

double toNormalised (DnaParam p, double v) noexcept
{
    switch (p)
    {
        case DnaParam::size: return std::clamp (std::log (std::max (v, 0.005) / 0.005) / std::log (200.0), 0.0, 1.0);
        case DnaParam::density: return std::clamp (std::log (std::max (v, 1.0)) / std::log (200.0), 0.0, 1.0);
        case DnaParam::position:
        case DnaParam::positionScatter:
        case DnaParam::spread: return std::clamp (v, 0.0, 1.0);
        case DnaParam::pitchScatter: return std::sqrt (std::clamp (v / 12.0, 0.0, 1.0));
        case DnaParam::tone: return std::clamp (std::log (std::max (v, 200.0) / 200.0) / std::log (100.0), 0.0, 1.0);
    }
    return 0.0;
}

double fromNormalised (DnaParam p, double n) noexcept
{
    n = std::clamp (n, 0.0, 1.0);
    switch (p)
    {
        case DnaParam::size: return 0.005 * std::pow (200.0, n);
        case DnaParam::density: return std::pow (200.0, n);
        case DnaParam::position:
        case DnaParam::positionScatter:
        case DnaParam::spread: return n;
        case DnaParam::pitchScatter: return 12.0 * n * n;
        case DnaParam::tone: return 200.0 * std::pow (100.0, n);
    }
    return 0.0;
}

//==============================================================================
std::shared_ptr<GrainModel> buildGrainModel (std::shared_ptr<const PlaybackSource> source, double rootMidi, std::uint64_t generation)
{
    auto model = std::make_shared<GrainModel>();
    model->generation = generation;
    model->source = source;
    if (source != nullptr && source->isValid())
        model->whole = { 0.0, static_cast<double> (source->numFrames()), rootMidi, 1.0, static_cast<int> (std::lround (rootMidi)), -1 };
    return model;
}

std::shared_ptr<GrainModel> buildGrainModel (std::shared_ptr<const PlaybackSource> source, std::shared_ptr<const atlas::NoteAtlas> atlas,
                                             double fallbackRootMidi, std::uint64_t generation)
{
    auto model = buildGrainModel (source, fallbackRootMidi, generation);
    model->atlas = atlas;
    if (atlas == nullptr || ! model->hasSource())
        return model;
    const double frames = static_cast<double> (source->numFrames());
    std::vector<int> regionOf (atlas->notes.size(), -1);
    for (std::size_t i = 0; i < atlas->notes.size(); ++i)
    {
        const auto& n = atlas->notes[i];
        if (! n.included)
            continue;
        const auto range = n.grainRange();
        GrainRegion r;
        r.start = std::clamp (static_cast<double> (range.start), 0.0, frames);
        r.end = std::clamp (static_cast<double> (range.end), r.start, frames);
        if (r.end - r.start < 64.0)
        {
            // A sustain too short for grains: use the whole play range instead.
            r.start = std::clamp (static_cast<double> (n.attackStart), 0.0, frames);
            r.end = std::clamp (static_cast<double> (n.sourceEnd), r.start, frames);
        }
        if (r.end - r.start < 64.0)
            continue;
        r.rootMidi = n.midiPitch > 0.0 ? n.midiPitch : static_cast<double> (n.rootMidi);
        if (n.assignedKey >= 0)
            r.rootMidi += static_cast<double> (n.assignedKey - n.rootMidi);
        r.confidence = n.confidence;
        r.key = n.playKey();
        r.noteIndex = static_cast<int> (i);
        regionOf[i] = static_cast<int> (model->regions.size());
        model->regions.push_back (r);
    }
    const auto byKey = atlas::notesByKey (*atlas);
    for (int k = 0; k < 128; ++k)
        for (int note : byKey[static_cast<std::size_t> (k)])
            if (regionOf[static_cast<std::size_t> (note)] >= 0)
                model->keyRegions[static_cast<std::size_t> (k)].push_back (regionOf[static_cast<std::size_t> (note)]);
    // The whole-source root for an atlas: its most confident note's pitch.
    double bestConfidence = -1.0;
    for (const auto& r : model->regions)
        if (r.confidence > bestConfidence)
        {
            bestConfidence = r.confidence;
            model->whole.rootMidi = r.rootMidi;
        }
    return model;
}

//==============================================================================
void GrainEngine::prepare (double rate, int /*maxBlockSize*/)
{
    sampleRate = rate;
    wavetable.prepare (rate);
    fx.prepare (rate);
    for (int i = 0; i <= windowSize; ++i)
    {
        const double x = static_cast<double> (i) / windowSize;
        windows[0][static_cast<std::size_t> (i)] = static_cast<float> (0.5 - 0.5 * std::cos (andor::twoPi * x));
        // Soft: a Tukey window, flat in the middle half with cosine shoulders.
        const double t = x < 0.25 ? 0.5 - 0.5 * std::cos (andor::pi * x / 0.25) : x > 0.75 ? 0.5 - 0.5 * std::cos (andor::pi * (1.0 - x) / 0.25) : 1.0;
        windows[1][static_cast<std::size_t> (i)] = static_cast<float> (t);
        // Percussive: a quick rise, an exponential fall, and a short taper so it ends at zero.
        const double rise = std::min (1.0, x / 0.04);
        const double fall = std::exp (-5.0 * std::max (0.0, x - 0.04) / 0.96);
        const double taper = std::min (1.0, (1.0 - x) / 0.04);
        windows[2][static_cast<std::size_t> (i)] = static_cast<float> (rise * fall * taper);
    }
    for (auto& v : voices)
        v = Voice {};
    for (auto& g : pool)
        g = Grain {};
    freeCount = poolSize;
    for (int i = 0; i < poolSize; ++i)
        freeList[static_cast<std::size_t> (i)] = static_cast<std::int16_t> (poolSize - 1 - i);
    liveGrains = peakLive = 0;
    denied = 0;
    limits = limitsFor (params.quality);
    dna.compute (params.dna);
    dnaSettings = params.dna;
    resetEvents();
}

void GrainEngine::setParams (const GrainParams& p) noexcept
{
    params = p;
    limits = limitsFor (p.quality);
    const auto& d = p.dna;
    bool changed = d.curveAmount != dnaSettings.curveAmount || d.variationAmount != dnaSettings.variationAmount
                   || d.individuality != dnaSettings.individuality || d.seed != dnaSettings.seed;
    for (int i = 0; i < numDnaParams && ! changed; ++i)
        changed = ! (d.curves[static_cast<std::size_t> (i)] == dnaSettings.curves[static_cast<std::size_t> (i)]);
    if (changed)
    {
        dnaSettings = d;
        dna.compute (d);
    }
}

void GrainEngine::resetEvents() noexcept
{
    eventCounter = 0;
    rrCounter.fill (0);
}

int GrainEngine::chooseRegion (int key, GrainRegion& out) noexcept
{
    if (current == nullptr || ! current->hasSource())
        return -1;
    if (! current->hasAtlas() || params.policy == SourcePolicy::wholeSource)
    {
        out = current->whole;
        return 0;
    }
    const auto& regions = current->regions;
    auto closest = [&]
    {
        int best = 0;
        for (int i = 1; i < static_cast<int> (regions.size()); ++i)
        {
            const double d = std::abs (regions[static_cast<std::size_t> (i)].rootMidi - key);
            const double bd = std::abs (regions[static_cast<std::size_t> (best)].rootMidi - key);
            if (d < bd - 1.0e-9 || (std::abs (d - bd) <= 1.0e-9 && regions[static_cast<std::size_t> (i)].confidence > regions[static_cast<std::size_t> (best)].confidence))
                best = i;
        }
        return best;
    };
    int index = 0;
    switch (params.policy)
    {
        case SourcePolicy::fixedNote:
            index = std::clamp (params.fixedNoteIndex, 0, static_cast<int> (regions.size()) - 1);
            break;
        case SourcePolicy::closest:
            index = closest();
            break;
        case SourcePolicy::mapped:
        {
            int sourceKey = -1;
            for (int d = 0; d <= 12 && sourceKey < 0; ++d)
                for (int k : { key - d, key + d })
                    if (sourceKey < 0 && k >= 0 && k < 128 && ! current->keyRegions[static_cast<std::size_t> (k)].empty())
                        sourceKey = k;
            if (sourceKey < 0)
                index = closest();
            else
            {
                const auto& list = current->keyRegions[static_cast<std::size_t> (sourceKey)];
                index = list[rrCounter[static_cast<std::size_t> (key)]++ % list.size()];
            }
            break;
        }
        case SourcePolicy::wholeSource:
            break;
    }
    out = regions[static_cast<std::size_t> (index)];
    return index;
}

GrainEngine::Voice* GrainEngine::allocateVoice() noexcept
{
    int playing = 0;
    for (const auto& v : voices)
        playing += v.active && ! v.stealing ? 1 : 0;
    if (playing >= limits.voices)
    {
        Voice* victim = nullptr;
        for (auto& v : voices)
            if (v.active && ! v.stealing && v.releasing && (victim == nullptr || v.order < victim->order))
                victim = &v;
        if (victim == nullptr)
            for (auto& v : voices)
                if (v.active && ! v.stealing && (victim == nullptr || v.order < victim->order))
                    victim = &v;
        if (victim != nullptr)
            victim->stealing = true;
    }
    for (auto& v : voices)
        if (! v.active)
            return &v;
    Voice* quietest = nullptr;
    for (auto& v : voices)
        if (v.stealing && (quietest == nullptr || v.stealGain < quietest->stealGain))
            quietest = &v;
    if (quietest != nullptr)
        freeGrains (*quietest);
    return quietest;
}

void GrainEngine::noteOn (int key, float velocity) noexcept
{
    if (key < 0 || key > 127)
        return;
    if (velocity <= 0.0f)
    {
        noteOff (key);
        return;
    }
    for (auto& v : voices)
        if (v.active && ! v.stealing && v.key == key && (v.keyDown || v.pedalHold))
        {
            v.keyDown = v.pedalHold = false;
            release (v);
        }
    auto* vp = allocateVoice();
    if (vp == nullptr)
        return;
    auto& v = *vp;
    v = Voice {};
    v.active = true;
    v.keyDown = true;
    v.key = key;
    v.velocity = std::clamp (velocity, 0.0f, 1.0f);
    v.order = ++noteCounter;
    v.model = current;
    v.generation = current != nullptr ? current->generation : 0;

    if (chooseRegion (key, v.region) >= 0)
        v.baseRatio = current->source->sampleRate() / sampleRate * semitonesToRatio (static_cast<double> (key) - v.region.rootMidi);

    // Deterministic mode restarts this sequence on transport start (resetEvents); otherwise the
    // counter keeps running, so each performance differs while staying seed-derived.
    v.rng.reseed (Prng::deriveSeed (params.seed, eventCounter, static_cast<std::uint64_t> (key)));
    ++eventCounter;

    for (int p = 0; p < numDnaParams; ++p)
    {
        v.dnaOffset[static_cast<std::size_t> (p)] = dna.offset (key, static_cast<DnaParam> (p));
        v.triggerOffset[static_cast<std::size_t> (p)] = static_cast<float> (std::clamp (params.triggerVariation, 0.0, 1.0) * 0.5 * v.rng.bipolar());
    }

    AdsrSettings amp { params.attackSeconds, params.decaySeconds, params.sustainLevel, params.releaseSeconds };
    AdsrSettings aux { params.auxAttack, params.auxDecay, params.auxSustain, params.auxRelease };
    v.amp.prepare (sampleRate, amp);
    v.aux.prepare (sampleRate, aux);
    v.amp.noteOn();
    v.aux.noteOn();
    v.lfo1.setShape (params.lfo1Shape);
    v.lfo2.setShape (params.lfo2Shape);
    v.lfo1.reset (0.0, Prng::deriveSeed (params.seed, 7, v.order));
    v.lfo2.reset (0.25, Prng::deriveSeed (params.seed, 8, v.order));
    v.filter.reset();
    v.osc.reset (0.0);
    for (auto* s : { &v.sizeS, &v.densityS, &v.positionS, &v.scatterS, &v.pitchScatterS, &v.spreadS, &v.cutoffS, &v.levelS, &v.oscMixS })
        s->prepare (sampleRate, 0.02);
    control (v, -1);   // initial values without smoothing
    v.countdown = 0.0;
}

void GrainEngine::release (Voice& v) noexcept
{
    if (v.releasing || ! v.active)
        return;
    v.releasing = true;
    v.amp.noteOff();
    v.aux.noteOff();
}

void GrainEngine::noteOff (int key) noexcept
{
    for (auto& v : voices)
        if (v.active && v.key == key && v.keyDown)
        {
            v.keyDown = false;
            if (pedal)
                v.pedalHold = true;
            else
                release (v);
        }
}

void GrainEngine::setSustainPedal (bool down) noexcept
{
    pedal = down;
    if (! down)
        for (auto& v : voices)
            if (v.active && v.pedalHold)
            {
                v.pedalHold = false;
                release (v);
            }
}

void GrainEngine::allNotesOff() noexcept
{
    pedal = false;
    for (auto& v : voices)
        if (v.active)
        {
            v.keyDown = v.pedalHold = false;
            release (v);
        }
}

void GrainEngine::panic() noexcept
{
    pedal = false;
    for (auto& v : voices)
    {
        freeGrains (v);
        v.active = false;
    }
    fx.reset();
}

void GrainEngine::freeGrains (Voice& v) noexcept
{
    for (int i = 0; i < v.grainCount; ++i)
    {
        const auto id = v.grains[static_cast<std::size_t> (i)];
        pool[static_cast<std::size_t> (id)].active = false;
        freeList[static_cast<std::size_t> (freeCount++)] = id;
        --liveGrains;
    }
    v.grainCount = 0;
}

int GrainEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

int GrainEngine::playingVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active && ! v.stealing ? 1 : 0;
    return n;
}

std::uint64_t GrainEngine::oldestGenerationInUse() const noexcept
{
    std::uint64_t oldest = current != nullptr ? current->generation : ~0ull;
    for (const auto& v : voices)
        if (v.active && v.model != nullptr && v.generation < oldest)
            oldest = v.generation;
    return oldest;
}

double GrainEngine::modulation (const Voice& v, ModDest dest) const noexcept
{
    double sum = 0.0;
    for (const auto& slot : params.mod)
    {
        if (slot.dest != dest || slot.source == ModSource::none || slot.amount == 0.0)
            continue;
        double value = 0.0;
        switch (slot.source)
        {
            case ModSource::none: break;
            case ModSource::velocity: value = v.velocity; break;
            case ModSource::note: value = (v.key - 60) / 60.0; break;
            case ModSource::modWheel: value = modWheel; break;
            case ModSource::ampEnv: value = v.amp.level(); break;
            case ModSource::auxEnv: value = v.aux.level(); break;
            case ModSource::lfo1: value = v.lfo1.value(); break;
            case ModSource::lfo2: value = v.lfo2.value(); break;
            case ModSource::macro1: value = params.macro1; break;
            case ModSource::macro2: value = params.macro2; break;
        }
        sum += slot.amount * value;
    }
    return sum;
}

double GrainEngine::profileValue (int key, DnaParam p) const noexcept
{
    double base = 0.0;
    switch (p)
    {
        case DnaParam::size: base = params.sizeSeconds; break;
        case DnaParam::density: base = params.density; break;
        case DnaParam::position: base = params.position; break;
        case DnaParam::positionScatter: base = params.positionScatter; break;
        case DnaParam::pitchScatter: base = params.pitchScatter; break;
        case DnaParam::tone: base = params.toneHz; break;
        case DnaParam::spread: base = params.spread; break;
    }
    return fromNormalised (p, toNormalised (p, base) + 0.5 * dna.offset (key, p));
}

void GrainEngine::control (Voice& v, int samples) noexcept
{
    if (samples > 0)
    {
        const double seconds = samples / sampleRate;
        v.lfo1.advance (params.lfo1Hz, seconds);
        v.lfo2.advance (params.lfo2Hz, seconds);
    }
    // The precedence chain (spec 18): base -> DNA -> trigger -> modulation -> clamp/convert.
    auto chain = [&] (DnaParam p, double base, ModDest dest)
    {
        const auto i = static_cast<std::size_t> (p);
        const double n = std::clamp (toNormalised (p, base) + 0.5 * v.dnaOffset[i] + 0.5 * v.triggerOffset[i] + modulation (v, dest), 0.0, 1.0);
        v.effective[i] = n;
        return fromNormalised (p, n);
    };
    const double size = chain (DnaParam::size, params.sizeSeconds, ModDest::size);
    const double density = chain (DnaParam::density, params.density, ModDest::density);
    const double position = chain (DnaParam::position, params.position, ModDest::position);
    const double scatter = chain (DnaParam::positionScatter, params.positionScatter, ModDest::positionScatter);
    const double pitchScatter = chain (DnaParam::pitchScatter, params.pitchScatter, ModDest::pitchScatter);
    const double cutoff = chain (DnaParam::tone, params.toneHz, ModDest::cutoff);
    const double spread = chain (DnaParam::spread, params.spread, ModDest::spread);
    const double level = std::clamp (params.grainLevel + modulation (v, ModDest::grainLevel), 0.0, 1.0);
    const double mix = std::clamp (params.oscMix + modulation (v, ModDest::oscMix), 0.0, 1.0);

    // Smoothing, then the DSP reads the smoothed values.
    const std::pair<andor::Smoother*, double> targets[] = { { &v.sizeS, size }, { &v.densityS, density }, { &v.positionS, position },
                                                            { &v.scatterS, scatter }, { &v.pitchScatterS, pitchScatter }, { &v.spreadS, spread },
                                                            { &v.cutoffS, cutoff }, { &v.levelS, level }, { &v.oscMixS, mix } };
    for (const auto& [s, value] : targets)
    {
        if (samples < 0)
            s->reset (value);
        else
        {
            s->setTarget (value);
            s->advance (samples);
        }
    }
    v.filter.setCoefficients (v.cutoffS.value(), 0.707 + 4.0 * std::clamp (params.resonance, 0.0, 1.0), sampleRate);
}

void GrainEngine::spawn (Voice& v, int voiceIndex) noexcept
{
    if (v.model == nullptr || ! v.model->hasSource() || v.region.end - v.region.start < 8.0)
        return;
    // The global budget (spec 19.1/19.2), deterministic: never above the budget, and no voice
    // above its fair share of it, so one dense note cannot starve the others.
    int sounding = 0;
    for (const auto& other : voices)
        sounding += other.active && ! other.stealing ? 1 : 0;
    const int fairShare = std::max (1, limits.grainBudget / std::max (1, sounding));
    if (liveGrains >= limits.grainBudget || v.grainCount >= std::min (fairShare, maxGrainsPerVoice) || freeCount == 0)
    {
        ++denied;
        return;
    }

    const auto& src = *v.model->source;
    const double regionLength = v.region.end - v.region.start;
    int length = std::max (32, static_cast<int> (std::lround (v.sizeS.value() * sampleRate)));
    const double semis = params.pitchSemitones + v.pitchScatterS.value() * v.rng.bipolar();
    const double rate = std::max (1.0e-4, v.baseRatio * semitonesToRatio (semis) * bendRatio);
    // Short regions shorten the grain rather than read past the region (spec 15.6).
    if (length * rate > regionLength - 4.0)
        length = std::max (32, static_cast<int> ((regionLength - 4.0) / rate));
    const double extent = length * rate;
    if (extent > regionLength - 2.0)
        return;   // the region is too short for even a minimal grain at this pitch

    const double centre = std::clamp (v.positionS.value() + 0.5 * v.scatterS.value() * v.rng.bipolar(), 0.0, 1.0);
    const double begin = v.region.start + 1.0 + centre * (regionLength - extent - 2.0);
    const bool reverse = v.rng.nextDouble() < params.reverseProbability;

    const auto id = freeList[static_cast<std::size_t> (--freeCount)];
    auto& g = pool[static_cast<std::size_t> (id)];
    g.active = true;
    g.length = length;
    g.age = 0;
    g.increment = reverse ? -rate : rate;
    g.position = std::clamp (reverse ? begin + extent : begin, 1.0, static_cast<double> (src.numFrames()) - 3.0);
    g.windowPhase = 0.0f;
    g.windowStep = static_cast<float> (windowSize) / static_cast<float> (length);
    // Overlapping grains add in power: keep the level steady as size x density changes.
    const double overlap = std::max (1.0, v.densityS.value() * length / sampleRate);
    const auto gain = static_cast<float> (v.levelS.value() / std::sqrt (overlap));
    float pl = 0.0f, pr = 0.0f;
    andor::panGains (v.spreadS.value() * v.rng.bipolar(), pl, pr);
    g.gainL = gain * pl * 1.41421356f;
    g.gainR = gain * pr * 1.41421356f;
    v.grains[static_cast<std::size_t> (v.grainCount++)] = id;
    ++liveGrains;
    peakLive = std::max (peakLive, liveGrains);
    (void) voiceIndex;
}

void GrainEngine::renderVoice (Voice& v, int voiceIndex, float* left, float* right, int numSamples) noexcept
{
    const bool hasSource = v.model != nullptr && v.model->hasSource();
    const float* srcL = hasSource ? v.model->source->channelData (0) : nullptr;
    const float* srcR = hasSource ? v.model->source->channelData (v.model->source->numChannels() > 1 ? 1 : 0) : nullptr;
    const auto& window = windows[static_cast<std::size_t> (params.window)];
    const double velGain = dbToGain (params.velocitySensitivity * 40.0 * std::log10 (std::max (0.01f, v.velocity)));
    const auto voiceGain = static_cast<float> (dbToGain (params.gainDb) * velGain);
    const double oscHz = midiToHz (v.key + params.oscTuneSemitones + params.pitchSemitones + bend * params.bendRangeSemitones);
    const float stealStep = static_cast<float> (1.0 / (0.005 * sampleRate));
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        if (--v.controlCountdown <= 0)
        {
            control (v, controlInterval);
            v.controlCountdown = controlInterval;
        }
        const float env = v.amp.next();
        v.aux.next();

        v.countdown -= 1.0;
        if (v.countdown <= 0.0)
        {
            spawn (v, voiceIndex);
            const double interval = sampleRate / std::max (1.0, v.densityS.value());
            v.countdown += std::max (1.0, interval * (1.0 + 0.9 * std::clamp (params.timingJitter, 0.0, 1.0) * v.rng.bipolar()));
        }

        float gl = 0.0f, gr = 0.0f;
        for (int k = 0; k < v.grainCount;)
        {
            const auto id = v.grains[static_cast<std::size_t> (k)];
            auto& g = pool[static_cast<std::size_t> (id)];
            const auto wi = static_cast<int> (g.windowPhase);
            const float wf = g.windowPhase - static_cast<float> (wi);
            const float w = window[static_cast<std::size_t> (wi)] + wf * (window[static_cast<std::size_t> (std::min (wi + 1, windowSize))] - window[static_cast<std::size_t> (wi)]);
            gl += w * g.gainL * andor::hermite (srcL, g.position);
            gr += w * g.gainR * andor::hermite (srcR, g.position);
            g.position += g.increment;
            g.windowPhase = std::min (g.windowPhase + g.windowStep, static_cast<float> (windowSize));
            if (++g.age >= g.length)
            {
                g.active = false;
                freeList[static_cast<std::size_t> (freeCount++)] = id;
                --liveGrains;
                v.grains[static_cast<std::size_t> (k)] = v.grains[static_cast<std::size_t> (--v.grainCount)];
                continue;
            }
            ++k;
        }

        const double mix = v.oscMixS.value();
        float l = gl, r = gr;
        if (mix > 0.0)
        {
            const float o = 0.5f * v.osc.next (wavetable, params.oscShape, oscHz, sampleRate);
            const auto a = static_cast<float> (std::cos (0.5 * andor::pi * mix));
            const auto b = static_cast<float> (std::sin (0.5 * andor::pi * mix));
            l = a * l + b * o;
            r = a * r + b * o;
        }
        l = v.filter.process (l, 0);
        r = v.filter.process (r, 1);

        float g = env * voiceGain;
        if (v.stealing)
        {
            v.stealGain -= stealStep;
            g *= std::max (0.0f, v.stealGain);
        }
        left[i] += g * l;
        right[i] += g * r;
        peak = std::max (peak, std::abs (g * l));

        if ((v.stealing && v.stealGain <= 0.0f) || (v.releasing && ! v.amp.isActive()))
        {
            freeGrains (v);
            v.active = false;
            break;
        }
    }
    v.lastLevel = peak;
}

void GrainEngine::render (float* left, float* right, int numSamples) noexcept
{
    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);
    bendRatio = semitonesToRatio (bend * params.bendRangeSemitones);
    for (std::size_t i = 0; i < voices.size(); ++i)
        if (voices[i].active)
            renderVoice (voices[i], static_cast<int> (i), left, right, numSamples);
    fx.process (left, right, numSamples, params.space, params.echo);
}

int GrainEngine::collectGrains (GrainView* out, int max) const noexcept
{
    int n = 0;
    for (std::size_t vi = 0; vi < voices.size(); ++vi)
    {
        const auto& v = voices[vi];
        if (! v.active || v.model == nullptr || ! v.model->hasSource())
            continue;
        const double frames = static_cast<double> (v.model->source->numFrames());
        for (int k = 0; k < v.grainCount && n < max; ++k)
        {
            const auto& g = pool[static_cast<std::size_t> (v.grains[static_cast<std::size_t> (k)])];
            const auto wi = std::min (static_cast<int> (g.windowPhase), windowSize);
            out[n++] = { static_cast<float> (g.position / frames), windows[static_cast<std::size_t> (params.window)][static_cast<std::size_t> (wi)],
                         static_cast<int> (vi) };
        }
    }
    return n;
}

int GrainEngine::collectVoices (VoiceView* out, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
    {
        if (! v.active || n >= max)
            continue;
        VoiceView view;
        view.key = v.key;
        view.level = v.lastLevel;
        for (int p = 0; p < numDnaParams; ++p)
            view.effective[static_cast<std::size_t> (p)] = static_cast<float> (v.effective[static_cast<std::size_t> (p)]);
        if (v.model != nullptr && v.model->hasSource())
        {
            const double frames = static_cast<double> (v.model->source->numFrames());
            view.regionStart = static_cast<float> (v.region.start / frames);
            view.regionEnd = static_cast<float> (v.region.end / frames);
        }
        out[n++] = view;
    }
    return n;
}

} // namespace osp::grain
