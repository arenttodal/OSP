#include "library/PreviewEngine.h"

#include "analysis/AnalysisFrames.h"
#include "analysis/pitch/PitchAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace osp::library
{

std::shared_ptr<const PreviewSound> makePreviewSound (AudioData audio, std::optional<double> knownRoot, std::string assetId, std::string name)
{
    auto sound = std::make_shared<PreviewSound>();
    if (audio.numChannels() > 2)
        audio.channels.resize (2);
    sound->assetId = std::move (assetId);
    sound->name = std::move (name);

    // The overview: min and max of every channel per bin (what a waveform thumbnail draws).
    const auto frames = audio.numFrames();
    sound->peakMin.assign (PreviewSound::overviewBins, 0.0f);
    sound->peakMax.assign (PreviewSound::overviewBins, 0.0f);
    if (frames > 0)
        for (int bin = 0; bin < PreviewSound::overviewBins; ++bin)
        {
            const auto from = frames * bin / PreviewSound::overviewBins;
            const auto to = std::max (from + 1, frames * (bin + 1) / PreviewSound::overviewBins);
            float lo = 0.0f, hi = 0.0f;
            for (const auto& channel : audio.channels)
                for (auto i = from; i < std::min (to, frames); ++i)
                {
                    lo = std::min (lo, channel[static_cast<std::size_t> (i)]);
                    hi = std::max (hi, channel[static_cast<std::size_t> (i)]);
                }
            sound->peakMin[static_cast<std::size_t> (bin)] = std::clamp (lo, -1.0f, 1.0f);
            sound->peakMax[static_cast<std::size_t> (bin)] = std::clamp (hi, -1.0f, 1.0f);
        }

    if (knownRoot)
    {
        sound->rootMidi = *knownRoot;
        sound->rootKnown = true;
    }
    else if (frames > 0 && audio.sampleRate > 0.0)
    {
        // The keyboard plays the sound against its root: the analysis' pitch estimate over the
        // first seconds (a one-shot's pitch is there). Unpitched or unsure material plays as C4
        // and says so, rather than inventing a root.
        AnalysisOptions options;
        options.maxAnalysisSeconds = 8.0;
        PitchFrames pitchFrames;
        const auto analysisFrames = AnalysisFrames::build (audio, options);
        const auto pitch = PitchAnalyzer::analyse (analysisFrames, options, pitchFrames);
        if (pitch.detected && pitch.confidence >= PitchAnalyzer::moderateConfidence && pitch.fundamentalHz > 0.0)
        {
            sound->rootMidi = 69.0 + 12.0 * std::log2 (pitch.fundamentalHz / 440.0);
            sound->rootKnown = true;
        }
    }
    sound->audio = std::move (audio);
    return sound;
}

PreviewEngine::PreviewEngine()
{
    for (auto& p : playheads)
        p.store (-1.0f);
}

PreviewEngine::~PreviewEngine() = default;

void PreviewEngine::prepare (double sampleRate)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (auto& v : voices)
        v = {};
    limiterGain = 1.0f;
    limiterRelease = 1.0f - std::exp (-1.0f / static_cast<float> (0.15 * rate));   // 150 ms recovery
    commandRead.store (commandWrite.load());
    for (auto& p : playheads)
        p.store (-1.0f);
    sounding.store (false);
}

void PreviewEngine::setSound (int slot, std::shared_ptr<const PreviewSound> sound)
{
    if (slot < 0 || slot >= numSlots)
        return;
    const auto index = static_cast<std::size_t> (slot);
    latest[index] = sound;
    if (sound == nullptr)
    {
        latestEntry[index] = nullptr;
        slots[index].store (nullptr, std::memory_order_release);
        return;
    }
    // A new entry per publish (even for a sound another slot already holds), so an entry
    // never returns to a slot and its generation says when it was published.
    auto entry = std::make_unique<Entry>();
    entry->sound = std::move (sound);
    entry->generation = publishedGeneration.load() + 1;
    latestEntry[index] = entry.get();
    const auto* raw = entry.get();
    owned.push_back (std::move (entry));
    publishedGeneration.store (raw->generation, std::memory_order_release);
    slots[index].store (raw, std::memory_order_release);
}

std::shared_ptr<const PreviewSound> PreviewEngine::sound (int slot) const
{
    return slot >= 0 && slot < numSlots ? latest[static_cast<std::size_t> (slot)] : nullptr;
}

void PreviewEngine::play (unsigned slotMask)
{
    const int w = commandWrite.load (std::memory_order_relaxed);
    const int next = (w + 1) % commandCapacity;
    if (next == commandRead.load (std::memory_order_acquire))
        return;   // the audio thread is not running (or far behind): nothing to hear anyway
    commands[static_cast<std::size_t> (w)] = { Command::Kind::play, slotMask };
    commandWrite.store (next, std::memory_order_release);
}

void PreviewEngine::stop()
{
    const int w = commandWrite.load (std::memory_order_relaxed);
    const int next = (w + 1) % commandCapacity;
    if (next == commandRead.load (std::memory_order_acquire))
        return;
    commands[static_cast<std::size_t> (w)] = { Command::Kind::stop, 0u };
    commandWrite.store (next, std::memory_order_release);
}

void PreviewEngine::setGainDb (float db) noexcept
{
    gainDecibels.store (std::clamp (db, -60.0f, 6.0f), std::memory_order_relaxed);
}

void PreviewEngine::collectGarbage()
{
    const auto oldest = oldestInUse.load (std::memory_order_acquire);
    owned.erase (std::remove_if (owned.begin(), owned.end(),
                                 [&] (const auto& e) {
                                     return std::find (latestEntry.begin(), latestEntry.end(), e.get()) == latestEntry.end()
                                         && e->generation < oldest;
                                 }),
                 owned.end());
}

double PreviewEngine::playheadSeconds (int slot) const noexcept
{
    return slot >= 0 && slot < numSlots ? static_cast<double> (playheads[static_cast<std::size_t> (slot)].load (std::memory_order_relaxed)) : -1.0;
}

void PreviewEngine::release (Voice& voice, float seconds) noexcept
{
    if (voice.entry == nullptr)
        return;
    voice.releasing = true;
    const float frames = std::max (1.0f, seconds * static_cast<float> (rate));
    voice.fadeStep = -std::max (voice.fade, 1.0e-3f) / frames;
}

void PreviewEngine::startVoice (int slot, const Entry* entry, int note, float gain) noexcept
{
    if (entry == nullptr || entry->sound == nullptr || entry->sound->audio.numFrames() < 2)
        return;
    // Bounded polyphony: past maxVoices sounding, the oldest fades out (5 ms) to make room.
    int active = 0;
    Voice* oldest = nullptr;
    for (auto& v : voices)
        if (v.entry != nullptr && ! v.releasing)
        {
            ++active;
            if (oldest == nullptr || v.age < oldest->age)
                oldest = &v;
        }
    if (active >= maxVoices && oldest != nullptr)
        release (*oldest, 0.005f);

    Voice* free = nullptr;
    for (auto& v : voices)
        if (v.entry == nullptr)
        {
            free = &v;
            break;
        }
    if (free == nullptr)   // every voice busy, even the fading ones: the oldest fading one goes
        for (auto& v : voices)
            if (free == nullptr || v.age < free->age)
                free = &v;

    const auto& sound = *entry->sound;
    const double pitch = note >= 0 ? std::pow (2.0, (static_cast<double> (note) - sound.rootMidi) / 12.0) : 1.0;
    *free = {};
    free->entry = entry;
    free->slot = slot;
    free->note = note;
    free->increment = std::clamp (sound.audio.sampleRate / rate * pitch, 1.0 / 16.0, 8.0);
    free->gain = gain;
    free->fade = 0.0f;
    free->fadeStep = 1.0f / std::max (1.0f, 0.0005f * static_cast<float> (rate));   // 0.5 ms: no click, the attack kept
    free->age = ++voiceCounter;
}

void PreviewEngine::handleCommands() noexcept
{
    int r = commandRead.load (std::memory_order_relaxed);
    const int w = commandWrite.load (std::memory_order_acquire);
    while (r != w)
    {
        const auto command = commands[static_cast<std::size_t> (r)];
        r = (r + 1) % commandCapacity;
        if (command.kind == Command::Kind::stop)
        {
            for (auto& v : voices)
                release (v, 0.02f);
            continue;
        }
        for (auto& v : voices)
            if (v.note < 0)
                release (v, 0.005f);
        int count = 0;
        for (int s = 0; s < numSlots; ++s)
            if ((command.mask & (1u << s)) != 0 && blockSlots[static_cast<std::size_t> (s)] != nullptr)
                ++count;
        const float gain = count > 0 ? 1.0f / std::sqrt (static_cast<float> (count)) : 0.0f;   // equal power
        for (int s = 0; s < numSlots; ++s)
            if ((command.mask & (1u << s)) != 0)
                startVoice (s, blockSlots[static_cast<std::size_t> (s)], -1, gain);
    }
    commandRead.store (r, std::memory_order_release);
}

void PreviewEngine::noteOn (int note, float velocity, unsigned slotMask) noexcept
{
    // Called before this block's render(): the slots are read here (an entry read now is in a
    // voice when the block reports, and anything newer than the last report is never freed).
    std::array<const Entry*, numSlots> now {};
    int count = 0;
    for (int s = 0; s < numSlots; ++s)
        if ((slotMask & (1u << s)) != 0)
        {
            now[static_cast<std::size_t> (s)] = slots[static_cast<std::size_t> (s)].load (std::memory_order_acquire);
            count += now[static_cast<std::size_t> (s)] != nullptr ? 1 : 0;
        }
    if (count == 0)
        return;
    const float gain = (0.25f + 0.75f * std::clamp (velocity, 0.0f, 1.0f)) / std::sqrt (static_cast<float> (count));
    for (int s = 0; s < numSlots; ++s)
        startVoice (s, now[static_cast<std::size_t> (s)], note, gain);
}

void PreviewEngine::noteOff (int note) noexcept
{
    for (auto& v : voices)
        if (v.note == note && ! v.releasing)
            release (v, 0.08f);
}

void PreviewEngine::allNotesOff() noexcept
{
    for (auto& v : voices)
        if (v.note >= 0)
            release (v, 0.02f);
}

void PreviewEngine::render (float* const* out, int numChannels, int numFrames) noexcept
{
    // What this block may touch: anything published after this point is newer than
    // `generationAtStart`; anything read below is reported before the block ends.
    const auto generationAtStart = publishedGeneration.load (std::memory_order_acquire);
    for (int s = 0; s < numSlots; ++s)
        blockSlots[static_cast<std::size_t> (s)] = slots[static_cast<std::size_t> (s)].load (std::memory_order_acquire);
    handleCommands();

    // Silent (nothing playing): nothing to add, and no work per sample for the instrument's block.
    const bool active = std::any_of (voices.begin(), voices.end(), [] (const Voice& v) { return v.entry != nullptr; });
    const float gain = active ? std::pow (10.0f, gainDecibels.load (std::memory_order_relaxed) / 20.0f) : 0.0f;
    constexpr float ceiling = 0.891f;   // -1 dBFS
    constexpr int chunk = 256;
    std::array<float, chunk> left {}, right {};

    bool any = false;
    if (! active)
        limiterGain = 1.0f;
    for (int start = 0; active && start < numFrames; start += chunk)
    {
        const int n = std::min (chunk, numFrames - start);
        std::fill (left.begin(), left.begin() + n, 0.0f);
        std::fill (right.begin(), right.begin() + n, 0.0f);
        for (auto& v : voices)
        {
            if (v.entry == nullptr)
                continue;
            any = true;
            const auto& audio = v.entry->sound->audio;
            const auto& l = audio.channels[0];
            const auto& r = audio.channels[audio.numChannels() > 1 ? 1 : 0];
            const auto last = static_cast<double> (audio.numFrames() - 1);
            for (int i = 0; i < n; ++i)
            {
                if (v.position >= last || (v.releasing && v.fade <= 0.0f))
                {
                    v.entry = nullptr;
                    break;
                }
                // 4-point Hermite: clean enough for audition at any pitch and host rate.
                const auto i1 = static_cast<std::int64_t> (v.position);
                const auto t = static_cast<float> (v.position - static_cast<double> (i1));
                const auto at = [&audio] (const std::vector<float>& c, std::int64_t k) {
                    return c[static_cast<std::size_t> (std::clamp<std::int64_t> (k, 0, audio.numFrames() - 1))];
                };
                const auto hermite = [&] (const std::vector<float>& c) {
                    const float y0 = at (c, i1 - 1), y1 = at (c, i1), y2 = at (c, i1 + 1), y3 = at (c, i1 + 2);
                    const float c1 = 0.5f * (y2 - y0);
                    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
                    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                    return ((c3 * t + c2) * t + c1) * t + y1;
                };
                v.fade = std::clamp (v.fade + v.fadeStep, 0.0f, 1.0f);
                const float g = v.gain * v.fade;
                left[static_cast<std::size_t> (i)] += hermite (l) * g;
                right[static_cast<std::size_t> (i)] += hermite (r) * g;
                v.position += v.increment;
            }
        }
        for (int i = 0; i < n; ++i)
        {
            float a = left[static_cast<std::size_t> (i)] * gain, b = right[static_cast<std::size_t> (i)] * gain;
            const float peak = std::max (std::abs (a), std::abs (b)) * limiterGain;
            if (peak > ceiling)
                limiterGain *= ceiling / peak;   // instant attack: the preview never clips by itself
            else
                limiterGain += (1.0f - limiterGain) * limiterRelease;
            a *= limiterGain;
            b *= limiterGain;
            if (numChannels >= 2)
            {
                out[0][start + i] += a;
                out[1][start + i] += b;
            }
            else if (numChannels == 1)
                out[0][start + i] += 0.5f * (a + b);
        }
    }

    // Report the oldest entry this block could touch (slots read above, voices), and the
    // playheads for the display.
    auto oldest = generationAtStart + 1;
    for (const auto* e : blockSlots)
        if (e != nullptr)
            oldest = std::min (oldest, e->generation);
    std::array<std::uint64_t, numSlots> newest {};
    std::array<float, numSlots> heads { -1.0f, -1.0f, -1.0f, -1.0f };
    for (const auto& v : voices)
        if (v.entry != nullptr)
        {
            oldest = std::min (oldest, v.entry->generation);
            const auto s = static_cast<std::size_t> (v.slot);
            if (v.note < 0 && ! v.releasing && v.age > newest[s])
            {
                newest[s] = v.age;
                heads[s] = static_cast<float> (v.position / std::max (1.0, v.entry->sound->audio.sampleRate));
            }
        }
    oldestInUse.store (oldest, std::memory_order_release);
    for (std::size_t s = 0; s < numSlots; ++s)
        playheads[s].store (heads[s], std::memory_order_relaxed);
    sounding.store (any, std::memory_order_relaxed);
}

} // namespace osp::library
