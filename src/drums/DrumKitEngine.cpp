#include "drums/DrumKitEngine.h"

#include "core/PitchMath.h"
#include "core/Prng.h"

#include <cmath>
#include <cstring>

namespace osp::drums
{

namespace
{
    constexpr double chokeFadeSeconds = 0.008;     // a closed hat cuts an open one: quick, no click
    constexpr double retriggerFadeSeconds = 0.004;
    constexpr std::uint64_t padSeedSalt = 0x44524d50414453ull;   // "DRMPADS"
}

const char* toString (DrumCategory category) noexcept
{
    switch (category)
    {
        case DrumCategory::kick: return "kick";
        case DrumCategory::snare: return "snare";
        case DrumCategory::clap: return "clap";
        case DrumCategory::closedHat: return "closedHat";
        case DrumCategory::openHat: return "openHat";
        case DrumCategory::tom: return "tom";
        case DrumCategory::rim: return "rim";
        case DrumCategory::cymbal: return "cymbal";
        case DrumCategory::percussion: return "percussion";
        case DrumCategory::unknown: break;
    }
    return "unknown";
}

DrumCategory categoryFromString (const std::string& text) noexcept
{
    for (auto c : { DrumCategory::kick, DrumCategory::snare, DrumCategory::clap, DrumCategory::closedHat, DrumCategory::openHat,
                    DrumCategory::tom, DrumCategory::rim, DrumCategory::cymbal, DrumCategory::percussion })
        if (text == toString (c))
            return c;
    return DrumCategory::unknown;
}

const char* toString (VoiceMode mode) noexcept { return mode == VoiceMode::monoRetrigger ? "mono" : "poly"; }
const char* toString (TriggerMode mode) noexcept { return mode == TriggerMode::gate ? "gate" : "oneShot"; }

PadSettings PadSettings::defaultsFor (DrumCategory category)
{
    PadSettings s;
    s.category = category;
    switch (category)
    {
        case DrumCategory::kick:
            s.voiceMode = VoiceMode::monoRetrigger;   // a new kick replaces the boom (no low-end pile-up)
            break;
        case DrumCategory::closedHat:
        case DrumCategory::openHat:
            s.chokeGroup = 1;                         // the hats share a choke group
            break;
        default:
            break;
    }
    return s;
}

DrumKitEngine::DrumKitEngine()
{
    // A pad plays up to its polyphony (12) on one layer; 48 slots leave room for stacked
    // layers and the fade-outs of fast retriggers (OSP's 208 would cost 36 MB per kit).
    for (auto& e : engines)
        e = std::make_unique<InstrumentEngine> (voiceSlotsPerPad);
    for (auto& row : started)
        row.fill (-1);
}

void DrumKitEngine::prepare (double rate, int maximumBlockSize, const KitSettings& kitSettings, const std::array<PadSettings, maxPads>& padSettings)
{
    sampleRate = rate;
    blockSize = std::max (16, maximumBlockSize);
    kit = kitSettings;
    pads = padSettings;
    for (auto* group : { &scratch, &mix })
        for (auto& b : *group)
            b.assign (static_cast<std::size_t> (blockSize), 0.0f);

    for (int p = 0; p < maxPads; ++p)
    {
        const auto& ps = pads[index (p)];
        EngineSettings s;
        s.postStage = false;                                 // ONE shared post stage for the kit
        s.reimaginedRouting = ReimaginedRouting::perLayer;   // each pad's REIMAGINED is its own
        s.outputGainDb = 0.0;                                // pad gain and kit volume are applied here
        s.polyphony = std::clamp (ps.polyphony, 1, voiceSlotsPerPad / 2);   // half the slots: the rest are fading tails
        s.adsr = ps.adsr;
        s.macros = padMacros (p);
        s.shaping = effectiveShaping();
        s.releaseGraft = false;                              // a gate's release is the envelope's, predictably
        s.seed = Prng::deriveSeed (kit.seed, padSeedSalt, static_cast<std::uint64_t> (p));
        s.pitchCharacter = PitchCharacter::tape;             // drums transpose like a sampler: speed and pitch together
        auto& engine = *engines[index (p)];
        engine.prepare (rate, blockSize, s);
        applyPad (p);
    }

    Macros postMacros = effectiveMacros();
    postMacros.reimagined = 0.0;
    post.setMacros (postMacros);
    post.setShaping (effectiveShaping());
    post.prepare (rate, blockSize, kit.seed);
    post.setReimagined (0.0);
    gainL.fill (0.0f);
    gainR.fill (0.0f);
    primed = false;
    for (auto& row : started)
        row.fill (-1);
    for (auto& picker : pickers)
        picker.reset();
    layerHits.fill (0);
    for (auto& v : padVoices)
        v.store (0, std::memory_order_relaxed);
    for (auto& h : padHits)
        h.store (0, std::memory_order_relaxed);
}

Macros DrumKitEngine::effectiveMacros() const noexcept
{
    Macros m = kit.macros;
    const double a = std::clamp (kit.machine, 0.0, 1.0);
    if (a <= 0.0)
        return m;
    // A little saturation, a little wear and motion, a little more per-hit variation:
    // never less than the musician set.
    m.drive = std::max (m.drive, 0.35 * a);
    m.motion = std::max (m.motion, 0.22 * a);
    m.life = std::max (m.life, 0.25 + 0.2 * a);
    return m;
}

Shaping DrumKitEngine::effectiveShaping() const noexcept
{
    Shaping s = kit.shaping;
    const double a = std::clamp (kit.machine, 0.0, 1.0);
    if (a <= 0.0)
        return s;
    // Bandwidth: CHARACTER's low-pass closes from the kit's top towards ~6 kHz (12 dB/oct,
    // the gentler slope of older converters' filters).
    s.filterType = FilterType::lp12;
    s.filterMaxHz = shaping::logLerp (std::max (s.filterMaxHz, 6000.0), 6000.0, a);
    s.resonance = std::min (s.resonance, 0.1);
    // Saturation: tape, rounder (transients rounded a little, punch kept by the attack).
    s.driveMode = DriveMode::tape;
    s.driveBody = 0.5 + 0.25 * a;
    s.driveTone = 0.5 - 0.2 * a;
    // Instability: tape wow and flutter, kept small; wear adds noise and colour.
    s.movementMode = MovementMode::tape;
    s.tapeWow = 0.15;
    s.tapeFlutter = 0.25 + 0.2 * a;
    s.tapeWear = 0.25 + 0.5 * a;
    // Per-hit pitch: at most 1.5-5 cents (tuned percussion keeps its pitch, spec 9.3).
    s.lifePitchCents = std::min (s.lifePitchCents, 1.5 + 3.5 * a);
    s.lifeTone = std::max (s.lifeTone, 0.3 + 0.25 * a);
    return s;
}

void DrumKitEngine::setMachine (double amount) noexcept
{
    kit.machine = std::clamp (amount, 0.0, 1.0);
    const auto shaping = effectiveShaping();
    post.setShaping (shaping);
    for (auto& e : engines)
        e->setShaping (shaping);
    Macros postMacros = effectiveMacros();
    postMacros.reimagined = 0.0;
    post.setMacros (postMacros);
    for (int p = 0; p < maxPads; ++p)
        applyPad (p);
}

Macros DrumKitEngine::padMacros (int pad) const noexcept
{
    const auto& ps = pads[index (pad)];
    Macros m = effectiveMacros();
    if (ps.life >= 0.0)
        m.life = ps.life;
    if (ps.dynamics >= 0.0)
        m.dynamics = ps.dynamics;
    if (ps.character >= 0.0)
        m.character = ps.character;
    // The shared stages (SPACE, ECHO, DRIVE, MOVEMENT's bus) run once for the kit, not here.
    m.space = m.echo = m.drive = 0.0;
    m.reimagined = 0.0;
    return m;
}

void DrumKitEngine::applyPad (int pad) noexcept
{
    const auto& ps = pads[index (pad)];
    auto& engine = *engines[index (pad)];
    engine.setEnvelope (ps.adsr);
    engine.setMacros (padMacros (pad));
    LayerSettings layer;
    layer.start = std::clamp (ps.start, 0.0, 1.0);
    layer.tuneSemitones = std::clamp (ps.tuneSemitones, -24.0, 24.0);
    layer.reverse = ps.reverse;
    layer.loop = false;   // one-shots play their recording once; no sustain loops
    layer.reimagined = ps.reimagined >= 0.0 ? ps.reimagined : kit.macros.reimagined;
    if (const double a = std::clamp (kit.machine, 0.0, 1.0); a > 0.0)
    {
        // The sample memory of a small, older machine: TOYBOX at its digital character only
        // (lower rate, fewer bits, a simple DAC); below 25 % it never loops or turns.
        layer.reimaginedSettings.mode = ReimaginedMode::toybox;
        layer.reimaginedSettings.toybox.play = ToyboxPlay::forward;
        layer.reimaginedSettings.toybox.motion = 0.0;
        layer.reimaginedSettings.toybox.digital = 0.25 + 0.6 * a;
        layer.reimagined = std::max (layer.reimagined, 0.22 * a);
    }
    engine.setLayerSettings (0, layer);
}

void DrumKitEngine::setPadModel (int pad, const InstrumentModel* model) noexcept
{
    if (layerModels[index (pad)][0] != model)
    {
        // A single sound set directly (not through setPadLayers): one layer, its defaults.
        plans[index (pad)] = {};
        plans[index (pad)].count = model != nullptr ? 1 : 0;
        layerModels[index (pad)] = {};
        layerModels[index (pad)][0] = model;
        pickers[index (pad)].reset();
    }
    auto& engine = *engines[index (pad)];
    engine.setModel (model, 0);
    const double root = model != nullptr ? model->rootMidi : 60.0;
    baseNote[index (pad)] = std::clamp (static_cast<int> (std::lround (root)), 0, 127);
    rootShift[index (pad)] = root - baseNote[index (pad)];
    engine.setLayerPitchOffsetSemitones (0, rootShift[index (pad)]);
}

void DrumKitEngine::setPadLayers (int pad, const std::array<const InstrumentModel*, maxLayers>& models, const LayerPlan& plan) noexcept
{
    const auto p = index (pad);
    const int count = std::clamp (plan.count, 0, maxLayers);
    if (count != std::clamp (plans[p].count, 0, maxLayers))
        pickers[p].reset();
    plans[p] = plan;
    plans[p].count = count;
    layerModels[p] = models;
    for (int i = count; i < maxLayers; ++i)
        layerModels[p][static_cast<std::size_t> (i)] = nullptr;
    setPadModel (pad, count > 0 ? models[0] : nullptr);
}

void DrumKitEngine::setPadSet (int pad, const InstrumentSet* set) noexcept
{
    plans[index (pad)] = {};
    layerModels[index (pad)] = {};
    pickers[index (pad)].reset();
    auto& engine = *engines[index (pad)];
    engine.setInstrumentSet (set, 0);
    const auto* primary = engine.model (0);
    const double root = primary != nullptr ? primary->rootMidi : 60.0;
    baseNote[index (pad)] = std::clamp (static_cast<int> (std::lround (root)), 0, 127);
    rootShift[index (pad)] = root - baseNote[index (pad)];
    engine.setLayerPitchOffsetSemitones (0, rootShift[index (pad)]);
}

bool DrumKitEngine::hasSound (int pad) const noexcept { return engines[index (pad)]->isLayerOccupied (0); }

bool DrumKitEngine::isModelInUse (const InstrumentModel* model) const noexcept
{
    for (const auto& e : engines)
        if (e->isModelInUse (model))
            return true;
    return false;
}

bool DrumKitEngine::isSetInUse (const InstrumentSet* set) const noexcept
{
    for (const auto& e : engines)
        if (e->isSetInUse (set))
            return true;
    return false;
}

void DrumKitEngine::killVoicesUsing (const InstrumentModel* model) noexcept
{
    for (auto& e : engines)
        e->killVoicesUsing (model);
}

void DrumKitEngine::killVoicesUsing (const InstrumentSet* set) noexcept
{
    for (auto& e : engines)
        e->killVoicesUsing (set);
}

void DrumKitEngine::setPadSettings (int pad, const PadSettings& settings) noexcept
{
    pads[index (pad)] = settings;
    applyPad (pad);
}

void DrumKitEngine::setKitMacros (const Macros& macros) noexcept
{
    kit.macros = macros;
    Macros postMacros = effectiveMacros();
    postMacros.reimagined = 0.0;
    post.setMacros (postMacros);
    post.setReimagined (0.0);
    for (int p = 0; p < maxPads; ++p)
        applyPad (p);
}

void DrumKitEngine::setShaping (const Shaping& shaping) noexcept
{
    kit.shaping = shaping;
    const auto effective = effectiveShaping();
    post.setShaping (effective);
    for (auto& e : engines)
        e->setShaping (effective);
}

void DrumKitEngine::setSeed (std::uint64_t seed) noexcept
{
    kit.seed = seed;
    for (int p = 0; p < maxPads; ++p)
        engines[index (p)]->setSeed (Prng::deriveSeed (seed, padSeedSalt, static_cast<std::uint64_t> (p)));
}

void DrumKitEngine::setHostTiming (const HostTiming& timing) noexcept
{
    post.setTiming (timing);
    for (auto& e : engines)
        e->setHostTiming (timing);
}

void DrumKitEngine::startHit (int pad, int semitones, int velocity, int channel, int sourceNote, int onlyLayer) noexcept
{
    const auto p = index (pad);
    auto& engine = *engines[p];
    if (! engine.isLayerOccupied (0))
        return;
    const auto& ps = pads[p];
    // Chokes first, so the choking hit itself is never cut.
    if (ps.chokeGroup > 0)
        for (int q = 0; q < maxPads; ++q)
            if (q != pad && pads[index (q)].chokeGroup == ps.chokeGroup)
                engines[index (q)]->fadeOutAll (chokeFadeSeconds);
    if (ps.voiceMode == VoiceMode::monoRetrigger)
        engine.fadeOutAll (retriggerFadeSeconds);

    const int engineNote = std::clamp (baseNote[p] + semitones, 0, 127);
    // The same source note still holding this pad (no note-off came): let it go first.
    auto& slot = started[static_cast<std::size_t> (sourceNote)][p];
    if (slot >= 0 && ps.trigger == TriggerMode::gate)
        engine.noteOff (slot, 0);
    slot = static_cast<std::int16_t> (engineNote);
    const auto& plan = plans[p];
    if (plan.count > 1 || (plan.count == 1 && ! (plan.layer[0] == LayerParams {})))
    {
        // Sample layers: the chosen ones start together, each at its own natural pitch,
        // level and START / END (the pad's START moves every layer's start further in).
        std::array<int, maxLayers> chosen {};
        int n = 1;
        if (onlyLayer >= 0 && onlyLayer < plan.count)
            chosen[0] = onlyLayer;
        else
            n = pickers[p].pick (plan, velocity, Prng::deriveSeed (kit.seed, padSeedSalt, static_cast<std::uint64_t> (pad)),
                                 layerHits[p]++, chosen);
        const double padRoot = baseNote[p] + rootShift[p];
        for (int i = 0; i < n; ++i)
        {
            const auto l = static_cast<std::size_t> (chosen[static_cast<std::size_t> (i)]);
            const auto* model = layerModels[p][l];
            if (model == nullptr || ! model->isValid())
                continue;
            const auto& lp = plan.layer[l];
            InstrumentEngine::NoteOverride o;
            o.model = model;
            o.gainDb = lp.gainDb > -60.0 ? std::min (lp.gainDb, 12.0) : -200.0;
            const double padStart = std::clamp (ps.start, 0.0, 1.0);
            const double layerStart = std::clamp (lp.start, 0.0, 1.0);
            o.start = layerStart + (1.0 - layerStart) * padStart;
            o.end = std::max (std::clamp (lp.end, 0.0, 1.0), o.start);
            o.pitchSemitones = model->rootMidi - padRoot;
            engine.setNextNoteOverride (o);
            engine.noteOn (engineNote, velocity, channel);
        }
    }
    else
        engine.noteOn (engineNote, velocity, channel);
    padHits[p].fetch_add (1, std::memory_order_relaxed);
    post.noteStarted();
}

void DrumKitEngine::noteOn (int note, int velocity, int channel) noexcept
{
    if (note < 0 || note > 127)
        return;
    if (velocity <= 0)
    {
        noteOff (note, channel);
        return;
    }
    std::array<Hit, maxPads> hits {};
    const int count = resolve (kit.keyMap, kit.keyboardMode, note, hits);
    for (int i = 0; i < count; ++i)
        startHit (hits[static_cast<std::size_t> (i)].pad, hits[static_cast<std::size_t> (i)].semitones, velocity, 1, note);
}

void DrumKitEngine::noteOff (int note, int) noexcept
{
    if (note < 0 || note >= sourceNotes)
        return;
    auto& row = started[static_cast<std::size_t> (note)];
    for (int p = 0; p < maxPads; ++p)
    {
        auto& slot = row[static_cast<std::size_t> (p)];
        if (slot < 0)
            continue;
        if (pads[index (p)].trigger == TriggerMode::gate)
            engines[index (p)]->noteOff (slot, 0);
        slot = -1;
    }
}

void DrumKitEngine::triggerPad (int pad, int velocity) noexcept
{
    if (pad < 0 || pad >= maxPads)
        return;
    startHit (pad, 0, std::clamp (velocity, 1, 127), 1, 128 + pad);
}

void DrumKitEngine::triggerPadLayer (int pad, int layer, int velocity) noexcept
{
    if (pad < 0 || pad >= maxPads)
        return;
    startHit (pad, 0, std::clamp (velocity, 1, 127), 1, 128 + pad, std::max (0, layer));
}

void DrumKitEngine::releasePad (int pad) noexcept
{
    if (pad >= 0 && pad < maxPads)
        noteOff (128 + pad);
}

void DrumKitEngine::setSustainPedal (bool down) noexcept
{
    for (auto& e : engines)
        e->setSustainPedal (down);
}

void DrumKitEngine::allNotesOff() noexcept
{
    for (auto& e : engines)
        e->allNotesOff();
    for (auto& row : started)
        row.fill (-1);
}

void DrumKitEngine::reset() noexcept
{
    for (auto& e : engines)
        e->reset();
    post.reset();
    primed = false;
    for (auto& row : started)
        row.fill (-1);
    for (auto& picker : pickers)
        picker.reset();
    layerHits.fill (0);
}

void DrumKitEngine::resetPerformance() noexcept
{
    for (auto& e : engines)
        e->resetPerformance();
}

bool DrumKitEngine::padAudible (int pad) const noexcept
{
    bool anySolo = false;
    for (const auto& ps : pads)
        anySolo = anySolo || ps.solo;
    const auto& ps = pads[index (pad)];
    return ! ps.mute && (! anySolo || ps.solo);
}

int DrumKitEngine::activeVoiceCount (int pad) const noexcept { return engines[index (pad)]->activeVoiceCount(); }

int DrumKitEngine::activeVoiceCount() const noexcept
{
    int n = 0;
    for (const auto& e : engines)
        n += e->activeVoiceCount();
    return n;
}

void DrumKitEngine::render (float* const* output, int numChannels, int numSamples) noexcept
{
    if (numChannels <= 0 || numSamples <= 0)
        return;
    const float volume = static_cast<float> (dbToGain (kit.volumeDb));
    const auto maxGainStep = static_cast<float> (1.0 / (0.01 * sampleRate));   // no jump faster than 10 ms
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (blockSize, numSamples - done);
        auto* ml = mix[0].data();
        auto* mr = mix[1].data();
        std::fill_n (ml, n, 0.0f);
        std::fill_n (mr, n, 0.0f);
        bool anyActive = false;
        for (int p = 0; p < maxPads; ++p)
        {
            const auto i = index (p);
            // Every pad renders every block (its clocks move evenly), so the output never
            // depends on how a host splits its blocks.
            float* ch[2] = { scratch[0].data(), scratch[1].data() };
            engines[i]->render (ch, 2, n);
            const auto& ps = pads[i];
            const double gain = padAudible (p) && ps.gainDb > -60.0 ? dbToGain (std::min (ps.gainDb, 12.0)) : 0.0;
            const double pan = std::clamp (ps.pan, -1.0, 1.0);
            const auto targetL = static_cast<float> (gain * (pan > 0.0 ? 1.0 - pan : 1.0));
            const auto targetR = static_cast<float> (gain * (pan < 0.0 ? 1.0 + pan : 1.0));
            const bool sounding = engines[i]->activeVoiceCount() > 0;
            anyActive = anyActive || sounding;
            if (! primed)
            {
                gainL[i] = targetL;
                gainR[i] = targetR;
            }
            const float l0 = gainL[i], r0 = gainR[i];
            const float limit = maxGainStep * static_cast<float> (n);
            const float l1 = std::clamp (targetL, l0 - limit, l0 + limit);
            const float r1 = std::clamp (targetR, r0 - limit, r0 + limit);
            gainL[i] = l1;
            gainR[i] = r1;
            if (l0 == 0.0f && l1 == 0.0f && r0 == 0.0f && r1 == 0.0f)
                continue;
            for (int s = 0; s < n; ++s)
            {
                const float t = static_cast<float> (s + 1) / static_cast<float> (n);
                ml[s] += scratch[0][static_cast<std::size_t> (s)] * (l0 + (l1 - l0) * t);
                mr[s] += scratch[1][static_cast<std::size_t> (s)] * (r0 + (r1 - r0) * t);
            }
        }
        primed = true;
        post.setVoicesActive (anyActive);
        post.process (ml, mr, n);
        for (int c = 0; c < numChannels; ++c)
        {
            const float* src = c % 2 == 0 ? ml : mr;
            for (int s = 0; s < n; ++s)
                output[c][done + s] = src[s] * volume;
        }
        done += n;
    }
    for (int p = 0; p < maxPads; ++p)
        padVoices[index (p)].store (engines[index (p)]->activeVoiceCount(), std::memory_order_relaxed);
}

} // namespace osp::drums
