#include "andor/phrase/PhraseEngine.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>

namespace osp::phrase
{

int fillLimitSemitones (FillMode mode) noexcept
{
    switch (mode)
    {
        case FillMode::off: return 0;
        case FillMode::natural: return 2;
        case FillMode::balanced: return 5;
        case FillMode::wide: return 12;
        case FillMode::extended: return 24;
    }
    return 0;
}

int phrasePadding() noexcept
{
    return SincInterpolator::maxReachFor (16, 16.0) + 8;
}

namespace
{
    /** Picks loop points inside a sustain region: a whole number of pitch periods long, its
        ends aligned where the waveform before the end matches the waveform before the start
        best (normalised cross-correlation of the cross-fade windows). */
    void findLoop (PlayRegion& r, const PlaybackSource& src, double pitchHz)
    {
        const double sr = src.sampleRate();
        const double len = r.sustainEnd - r.sustainStart;
        r.loopable = false;
        if (len < 0.12 * sr)
            return;
        const double period = pitchHz > 20.0 ? sr / pitchHz : 0.01 * sr;
        const double fade = std::clamp (0.25 * len, 2.0 * period, 0.08 * sr);
        const double loopEnd = std::floor (r.sustainEnd - 0.05 * len);
        const double periods = std::max (2.0, std::floor (0.6 * len / period));
        const double loopLength = std::round (periods * period);
        const double nominalStart = loopEnd - loopLength;
        if (nominalStart - fade < r.sustainStart)
            return;

        const float* l = src.channelData (0);
        const float* rr = src.channelData (src.numChannels() > 1 ? 1 : 0);
        auto mono = [&] (std::int64_t i) { return 0.5f * (l[i] + rr[i]); };
        const auto w = static_cast<std::int64_t> (fade);
        const auto e = static_cast<std::int64_t> (loopEnd);
        double ea = 0.0;
        for (std::int64_t k = 0; k < w; ++k)
            ea += static_cast<double> (mono (e - w + k)) * mono (e - w + k);
        const auto search = static_cast<std::int64_t> (std::min (period, 400.0));
        double bestCorr = -2.0;
        std::int64_t bestStart = static_cast<std::int64_t> (nominalStart);
        for (std::int64_t d = -search; d <= search; ++d)
        {
            const auto s = static_cast<std::int64_t> (nominalStart) + d;
            if (s - w < static_cast<std::int64_t> (r.sustainStart) || s >= e - w)
                continue;
            double ab = 0.0, eb = 0.0;
            for (std::int64_t k = 0; k < w; ++k)
            {
                const double a = mono (e - w + k), b = mono (s - w + k);
                ab += a * b;
                eb += b * b;
            }
            const double corr = ea > 0.0 && eb > 0.0 ? ab / std::sqrt (ea * eb) : 0.0;
            if (corr > bestCorr)
            {
                bestCorr = corr;
                bestStart = s;
            }
        }
        r.loopStart = static_cast<double> (bestStart);
        r.loopEnd = loopEnd;
        r.loopCrossfade = fade;
        r.equalPowerLoop = bestCorr < 0.7;
        r.loopable = r.loopEnd - r.loopStart > 2.0 * fade;
    }
}

std::shared_ptr<PhraseModel> buildPhraseModel (std::shared_ptr<const PlaybackSource> source, const atlas::NoteAtlas& atlas,
                                               std::uint64_t generation)
{
    auto model = std::make_shared<PhraseModel>();
    model->generation = generation;
    model->source = source;
    model->atlas = atlas;
    if (source == nullptr || ! source->isValid())
        return model;

    const auto frames = static_cast<double> (source->numFrames());
    std::vector<int> regionOfNote (atlas.notes.size(), -1);
    double levelSum = 0.0;
    for (std::size_t i = 0; i < atlas.notes.size(); ++i)
    {
        const auto& n = atlas.notes[i];
        if (! n.included || n.sourceEnd - n.attackStart < 2)
            continue;
        PlayRegion r;
        r.noteIndex = static_cast<int> (i);
        r.key = std::clamp (n.playKey(), 0, 127);
        r.midiPitch = n.midiPitch > 0.0 ? n.midiPitch : static_cast<double> (n.rootMidi);
        // A key assigned by hand plays the note there at its own pitch relation: the region's
        // pitch moves with the key (the user said "this note belongs on that key").
        if (n.assignedKey >= 0)
            r.midiPitch += static_cast<double> (n.assignedKey - n.rootMidi);
        r.centsOffset = n.centsOffset;
        r.confidence = n.confidence;
        r.attackStart = std::clamp (static_cast<double> (n.attackStart), 0.0, frames - 2.0);
        r.end = std::clamp (static_cast<double> (n.sourceEnd), r.attackStart + 2.0, frames);
        r.sustainStart = std::clamp (static_cast<double> (n.sustain.start), r.attackStart, r.end);
        r.sustainEnd = std::clamp (static_cast<double> (n.sustain.end), r.sustainStart, r.end);
        r.releaseStart = std::clamp (static_cast<double> (n.releaseStart), r.sustainEnd, r.end);
        r.fadeIn = std::max (1.0, static_cast<double> (n.fadeInFrames));
        r.fadeOut = std::max (1.0, static_cast<double> (n.fadeOutFrames));
        r.levelDb = n.rmsDb > -119.0 ? n.rmsDb : n.peakDb - 6.0;
        if (n.articulation != atlas::Articulation::noisy)
            findLoop (r, *source, n.pitchHz);
        regionOfNote[i] = static_cast<int> (model->regions.size());
        levelSum += r.levelDb;
        model->regions.push_back (r);
    }
    if (! model->regions.empty())
        model->meanLevelDb = levelSum / static_cast<double> (model->regions.size());

    const auto byKey = atlas::notesByKey (atlas);
    for (int key = 0; key < 128; ++key)
        for (int note : byKey[static_cast<std::size_t> (key)])
            if (regionOfNote[static_cast<std::size_t> (note)] >= 0)
                model->keyRegions[static_cast<std::size_t> (key)].push_back (regionOfNote[static_cast<std::size_t> (note)]);
    return model;
}

namespace
{
    /** The recorded key nearest to `key` within `limit` semitones (deterministic ties: the one
        whose best take is more confident, then the lower key). -1 when none. */
    int nearestRecordedKey (const PhraseModel& model, int key, int limit) noexcept
    {
        auto bestConfidence = [&] (int k)
        {
            double best = 0.0;
            for (int r : model.keyRegions[static_cast<std::size_t> (k)])
                best = std::max (best, model.regions[static_cast<std::size_t> (r)].confidence);
            return best;
        };
        for (int d = 1; d <= limit; ++d)
        {
            const int below = key - d, above = key + d;
            const bool hasBelow = below >= 0 && ! model.keyRegions[static_cast<std::size_t> (below)].empty();
            const bool hasAbove = above < 128 && ! model.keyRegions[static_cast<std::size_t> (above)].empty();
            if (hasBelow && hasAbove)
                return bestConfidence (above) > bestConfidence (below) ? above : below;
            if (hasBelow)
                return below;
            if (hasAbove)
                return above;
        }
        return -1;
    }
}

KeyInfo describeKey (const PhraseModel& model, int key, FillMode fill) noexcept
{
    KeyInfo info;
    if (key < 0 || key > 127)
        return info;
    const auto& exact = model.keyRegions[static_cast<std::size_t> (key)];
    if (! exact.empty())
    {
        info.state = exact.size() > 1 ? KeyState::variations : KeyState::exact;
        info.sourceKey = key;
        info.variations = static_cast<int> (exact.size());
        return info;
    }
    const int source = nearestRecordedKey (model, key, fillLimitSemitones (fill));
    if (source >= 0)
    {
        info.state = KeyState::filled;
        info.sourceKey = source;
        info.variations = static_cast<int> (model.keyRegions[static_cast<std::size_t> (source)].size());
    }
    return info;
}

//==============================================================================
void PhraseEngine::prepare (double rate, int /*maxBlockSize*/)
{
    sampleRate = rate;
    interpolator.prepareStretchTables();
    wavetable.prepare (rate);
    fx.prepare (rate);
    for (auto& v : voices)
        v = Voice {};
    resetEvents();
}

void PhraseEngine::setModel (const PhraseModel* model) noexcept
{
    current = model;
}

void PhraseEngine::resetEvents() noexcept
{
    eventCounter = 0;
    rrCounter.fill (0);
    rrLast.fill (-1);
}

int PhraseEngine::chooseRegion (int key, int& transposeFrom) noexcept
{
    if (current == nullptr || ! current->hasNotes())
        return -1;
    int sourceKey = key;
    if (current->keyRegions[static_cast<std::size_t> (key)].empty())
    {
        sourceKey = nearestRecordedKey (*current, key, fillLimitSemitones (params.fill));
        if (sourceKey < 0)
            return -1;   // outside the Fill range: the key stays silent rather than stretched
    }
    transposeFrom = sourceKey;
    const auto& list = current->keyRegions[static_cast<std::size_t> (sourceKey)];
    const auto n = static_cast<int> (list.size());
    int index = 0;
    if (n > 1)
    {
        auto& last = rrLast[static_cast<std::size_t> (key)];
        Prng rng (Prng::deriveSeed (params.seed, eventCounter, static_cast<std::uint64_t> (key)));
        switch (params.roundRobin)
        {
            case RoundRobin::sequential:
                index = static_cast<int> (rrCounter[static_cast<std::size_t> (key)]++ % static_cast<std::uint32_t> (n));
                break;
            case RoundRobin::random:
                index = static_cast<int> (rng.nextBelow (static_cast<std::uint64_t> (n)));
                break;
            case RoundRobin::shuffle:
                if (last < 0 || last >= n)
                    index = static_cast<int> (rng.nextBelow (static_cast<std::uint64_t> (n)));
                else
                {
                    // Never the take that played last on this key.
                    index = static_cast<int> (rng.nextBelow (static_cast<std::uint64_t> (n - 1)));
                    if (index >= last)
                        ++index;
                }
                break;
        }
        last = index;
    }
    return list[static_cast<std::size_t> (index)];
}

PhraseEngine::Voice* PhraseEngine::allocateVoice() noexcept
{
    const int limit = std::clamp (params.polyphony, 1, maxVoices - 4);
    int playing = 0;
    for (const auto& v : voices)
        playing += v.active && ! v.stealing ? 1 : 0;

    if (playing >= limit)
    {
        // Steal: the oldest released voice, else the oldest playing one. It fades in 4 ms
        // while the new note starts in a free slot (the pool keeps spare slots for this).
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
    // No free slot (every spare is still fading): take the quietest fading voice.
    Voice* quietest = nullptr;
    for (auto& v : voices)
        if (v.stealing && (quietest == nullptr || v.stealGain < quietest->stealGain))
            quietest = &v;
    return quietest;
}

void PhraseEngine::noteOn (int key, float velocity) noexcept
{
    if (key < 0 || key > 127 || current == nullptr)
        return;
    if (velocity <= 0.0f)
    {
        noteOff (key);
        return;
    }
    // A repeated key: its previous voice is released (a new take starts; no pile-up).
    for (auto& v : voices)
        if (v.active && ! v.stealing && v.key == key && (v.keyDown || v.pedalHold))
        {
            v.keyDown = v.pedalHold = false;
            release (v);
        }
    if (auto* v = allocateVoice())
        startVoice (*v, key, velocity);
    ++eventCounter;
}

void PhraseEngine::startVoice (Voice& v, int key, float velocity) noexcept
{
    int sourceKey = key;
    const int regionIndex = chooseRegion (key, sourceKey);
    if (regionIndex < 0)
        return;
    const auto& region = current->regions[static_cast<std::size_t> (regionIndex)];
    const auto& src = *current->source;

    v = Voice {};
    v.active = true;
    v.keyDown = true;
    v.key = key;
    v.region = regionIndex;
    v.model = current;
    v.generation = current->generation;
    v.order = ++noteCounter;

    // Corrected: the region's measured pitch lands exactly on the key. Original: its rounded
    // root does, so the performance's own deviation (cents) is kept.
    const double from = params.tuning == Tuning::corrected ? region.midiPitch
                                                           : region.midiPitch - region.centsOffset / 100.0;
    v.transpose = static_cast<double> (key) - from;
    v.baseIncrement = src.sampleRate() / sampleRate * semitonesToRatio (v.transpose + params.pitchSemitones + params.fineCents / 100.0);

    const double attackSkip = (1.0 - std::clamp (params.attackPreservation, 0.0, 1.0)) * (region.sustainStart - region.attackStart);
    v.position = std::min (region.attackStart + attackSkip + params.startOffsetSeconds * src.sampleRate(), region.end - region.fadeOut - 2.0);
    v.position = std::max (v.position, region.attackStart);
    v.start = v.position;
    // Starting inside the note (attack preservation < 1, start offset) needs its own fade.
    v.fadeInLength = v.position > region.attackStart + 1.0 ? 0.008 * src.sampleRate() : region.fadeIn;

    const double vel = std::clamp (static_cast<double> (velocity), 0.01, 1.0);
    v.velocityGain = static_cast<float> (dbToGain (params.velocitySensitivity * 40.0 * std::log10 (vel)));
    const double matchDb = std::clamp ((current->meanLevelDb - region.levelDb) * params.levelMatch, -18.0, 18.0);
    v.levelGain = static_cast<float> (dbToGain (matchDb));

    AdsrSettings env;
    env.attackSeconds = params.attackSeconds;
    env.decaySeconds = params.decaySeconds;
    env.sustainLevel = params.sustainLevel;
    env.releaseSeconds = params.releaseSeconds;
    v.amp.prepare (sampleRate, env);
    v.amp.noteOn();
    v.tone.reset();
    v.osc.reset (0.0);
    v.rng.reseed (Prng::deriveSeed (params.seed ^ 0x9e3779b9ull, eventCounter, static_cast<std::uint64_t> (key)));
    v.lastLevel = 0.0f;
}

void PhraseEngine::release (Voice& v) noexcept
{
    if (v.releasing || ! v.active)
        return;
    if (params.playMode == PlayMode::oneShot)
        return;   // plays its region out
    v.releasing = true;
    v.amp.noteOff();
    if (params.playMode == PlayMode::sustain && v.looping && v.model != nullptr)
    {
        const auto& region = v.model->regions[static_cast<std::size_t> (v.region)];
        if (region.end - region.releaseStart > 0.02 * v.model->source->sampleRate())
        {
            // Into the recording's own release: a short cross-fade from the loop.
            v.inTail = true;
            v.tailFrom = v.position;
            v.position = region.releaseStart;
            v.tailCrossfade = 1.0;
            v.looping = false;
        }
    }
}

void PhraseEngine::noteOff (int key) noexcept
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

void PhraseEngine::setSustainPedal (bool down) noexcept
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

void PhraseEngine::allNotesOff() noexcept
{
    pedal = false;
    for (auto& v : voices)
        if (v.active)
        {
            v.keyDown = v.pedalHold = false;
            if (params.playMode == PlayMode::oneShot)
                v.stealing = true;   // one-shots ignore note-off; all-notes-off must still end them
            else
                release (v);
        }
}

void PhraseEngine::panic() noexcept
{
    pedal = false;
    for (auto& v : voices)
        v.active = false;
    fx.reset();
}

int PhraseEngine::activeVoices() const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        n += v.active ? 1 : 0;
    return n;
}

std::uint64_t PhraseEngine::oldestGenerationInUse() const noexcept
{
    std::uint64_t oldest = current != nullptr ? current->generation : ~0ull;
    for (const auto& v : voices)
        if (v.active && v.generation < oldest)
            oldest = v.generation;
    return oldest;
}

int PhraseEngine::collect (VoiceView* out, int max) const noexcept
{
    int n = 0;
    for (const auto& v : voices)
        if (v.active && n < max && v.model == current)
            out[n++] = { v.region, static_cast<float> (v.position), v.lastLevel, v.key };
    return n;
}

void PhraseEngine::readSource (Voice& v, double position, double increment, float& l, float& r) noexcept
{
    const auto& src = *v.model->source;
    interpolator.computeKernelFast (position, increment, v.kernel, v.levelCache);
    l = SincInterpolator::apply (v.kernel, src.channelData (0));
    r = src.numChannels() > 1 ? SincInterpolator::apply (v.kernel, src.channelData (1)) : l;
}

float PhraseEngine::grainSustain (Voice& v, const PlayRegion& region, double increment, float& right) noexcept
{
    // A soft cloud of 80 ms Hann grains from the sustain (or the body when it is short), at the
    // note's pitch: the "bloom" end of SUSTAIN CHARACTER, and the hold for regions too short
    // or unstable to loop.
    const auto& src = *v.model->source;
    const double srcRate = src.sampleRate();
    const double lo = region.sustainEnd - region.sustainStart > 0.04 * srcRate ? region.sustainStart : region.attackStart;
    const double hi = region.sustainEnd - region.sustainStart > 0.04 * srcRate ? region.sustainEnd : region.end;
    const int grainLength = static_cast<int> (0.08 * sampleRate);
    const double reach = grainLength * increment;
    if (--v.grainCountdown <= 0)
    {
        v.grainCountdown = static_cast<int> (0.02 * sampleRate);
        for (auto& g : v.grains)
            if (! g.active)
            {
                const double span = std::max (0.0, hi - lo - reach - 4.0);
                g.position = lo + 2.0 + v.rng.nextDouble() * span;
                g.age = 0;
                g.length = grainLength;
                g.pan = static_cast<float> (0.3 * v.rng.bipolar());
                g.active = true;
                break;
            }
    }
    float left = 0.0f;
    right = 0.0f;
    const float* l = src.channelData (0);
    const float* r = src.channelData (src.numChannels() > 1 ? 1 : 0);
    for (auto& g : v.grains)
    {
        if (! g.active)
            continue;
        const double x = static_cast<double> (g.age) / g.length;
        const auto w = static_cast<float> (0.5 - 0.5 * std::cos (andor::twoPi * x));
        const double pos = std::clamp (g.position, 2.0, static_cast<double> (src.numFrames()) - 3.0);
        float gl = 0.0f, gr = 0.0f;
        andor::panGains (g.pan, gl, gr);
        left += w * gl * andor::hermite (l, pos) * 1.41f;
        right += w * gr * andor::hermite (r, pos) * 1.41f;
        g.position += increment;
        if (++g.age >= g.length)
            g.active = false;
    }
    // Four grains overlap at 80 ms / 20 ms: Hann windows at 25 % hop sum to 2.
    left *= 0.5f;
    right *= 0.5f;
    return left;
}

void PhraseEngine::renderVoice (Voice& v, float* left, float* right, int numSamples) noexcept
{
    const auto& region = v.model->regions[static_cast<std::size_t> (v.region)];
    const auto& src = *v.model->source;
    const double increment = v.baseIncrement * semitonesToRatio (bend * params.bendRangeSemitones);
    const bool sustainMode = params.playMode == PlayMode::sustain;
    const double character = sustainMode ? std::clamp (params.sustainCharacter, 0.0, 1.0) : 0.0;
    const auto charLoop = static_cast<float> (std::cos (0.5 * andor::pi * character));
    const auto charGrain = static_cast<float> (std::sin (0.5 * andor::pi * character));
    const double synth = std::clamp (params.synthetic, 0.0, 1.0);
    const auto natGain = static_cast<float> (std::cos (0.5 * andor::pi * synth));
    const auto synGain = static_cast<float> (std::sin (0.5 * andor::pi * synth));
    const double synthHz = midiToHz (v.key + params.pitchSemitones + params.fineCents / 100.0 + bend * params.bendRangeSemitones);
    const bool filtered = params.tone < 0.999;
    if (filtered)
        v.tone.setCoefficients (300.0 * std::pow (20000.0 / 300.0, std::clamp (params.tone, 0.0, 1.0)), 0.707, sampleRate);
    const auto masterGain = static_cast<float> (dbToGain (params.gainDb)) * v.velocityGain * v.levelGain;
    const auto followAttack = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sampleRate)));
    const auto followRelease = static_cast<float> (1.0 - std::exp (-1.0 / (0.06 * sampleRate)));
    const float stealStep = static_cast<float> (1.0 / (0.004 * sampleRate));
    // Grain hold for SUSTAIN on regions that cannot loop (short or unstable sustain).
    const bool grainHold = sustainMode && ! region.loopable && region.end - region.attackStart > 0.05 * src.sampleRate();
    const double holdPoint = std::max (region.attackStart, (region.sustainEnd > region.sustainStart ? region.sustainEnd : region.end) - region.fadeOut - 2.0);

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float l = 0.0f, r = 0.0f;
        bool finished = false;

        if (sustainMode && region.loopable && ! v.inTail && ! v.releasing && v.position >= region.loopStart)
            v.looping = true;

        if (v.looping)
        {
            const double length = region.loopEnd - region.loopStart;
            const double fadeStart = region.loopEnd - region.loopCrossfade;
            readSource (v, v.position, increment, l, r);
            if (v.position >= fadeStart)
            {
                float l2 = 0.0f, r2 = 0.0f;
                readSource (v, v.position - length, increment, l2, r2);
                const double g = std::clamp ((v.position - fadeStart) / region.loopCrossfade, 0.0, 1.0);
                const float a = region.equalPowerLoop ? static_cast<float> (std::cos (0.5 * andor::pi * g)) : static_cast<float> (1.0 - g);
                const float b = region.equalPowerLoop ? static_cast<float> (std::sin (0.5 * andor::pi * g)) : static_cast<float> (g);
                l = a * l + b * l2;
                r = a * r + b * r2;
            }
            if (charGrain > 0.0f)
            {
                float gr = 0.0f;
                const float gl = grainSustain (v, region, increment, gr);
                l = charLoop * l + charGrain * gl;
                r = charLoop * r + charGrain * gr;
            }
            v.position += increment;
            if (v.position >= region.loopEnd)
                v.position -= length;
        }
        else if (grainHold && v.position >= holdPoint)
        {
            float gr = 0.0f;
            l = grainSustain (v, region, increment, gr);
            r = gr;
        }
        else
        {
            if (v.position >= region.end - 1.0)
                finished = true;
            else
            {
                readSource (v, v.position, increment, l, r);
                if (v.inTail && v.tailCrossfade > 0.0)
                {
                    float l2 = 0.0f, r2 = 0.0f;
                    readSource (v, v.tailFrom, increment, l2, r2);
                    const auto a = static_cast<float> (v.tailCrossfade);
                    l = a * l2 + (1.0f - a) * l;
                    r = a * r2 + (1.0f - a) * r;
                    v.tailFrom += increment;
                    v.tailCrossfade -= 1.0 / (0.02 * sampleRate);
                }
                // Non-destructive fades at the region's ends.
                double inGain = 1.0;
                if (! v.fadedIn)
                {
                    inGain = std::clamp ((v.position - v.start) / v.fadeInLength, 0.0, 1.0);
                    v.fadedIn = inGain >= 1.0;
                }
                const double outGain = std::clamp ((region.end - v.position) / region.fadeOut, 0.0, 1.0);
                const auto g = static_cast<float> (std::min (inGain, outGain));
                l *= g;
                r *= g;
                v.position += increment;
            }
        }

        // Natural <-> Synthetic: a band-limited tone at the played pitch that follows the
        // recording's own loudness.
        if (synGain > 0.0f)
        {
            const float level = 0.5f * (std::abs (l) + std::abs (r));
            v.follower += (level > v.follower ? followAttack : followRelease) * (level - v.follower);
            const float tone = v.osc.next (wavetable, 2.0, synthHz, sampleRate) * v.follower * 1.6f;
            l = natGain * l + synGain * tone;
            r = natGain * r + synGain * tone;
        }
        if (filtered)
        {
            l = v.tone.process (l, 0);
            r = v.tone.process (r, 1);
        }

        float env = v.amp.next();
        if (v.stealing)
        {
            v.stealGain -= stealStep;
            if (v.stealGain <= 0.0f)
                finished = true;
            env *= std::max (0.0f, v.stealGain);
        }
        if (v.releasing && ! v.amp.isActive())
            finished = true;
        if (finished)
        {
            v.active = false;
            break;
        }
        const float g = env * masterGain;
        left[i] += g * l;
        right[i] += g * r;
        peak = std::max (peak, std::abs (g * l));
    }
    v.lastLevel = peak;
}

void PhraseEngine::render (float* left, float* right, int numSamples) noexcept
{
    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);
    for (auto& v : voices)
        if (v.active && v.model != nullptr && v.region >= 0)
            renderVoice (v, left, right, numSamples);
    fx.process (left, right, numSamples, params.space, params.echo);
}

} // namespace osp::phrase
