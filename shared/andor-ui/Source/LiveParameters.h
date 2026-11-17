#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace osp::plugin
{

/**
    USER MACROS: the parameters as the engine reads them. Each is its own value plus, while a
    USER macro moves it, that macro's offset, in the parameter's normalised (0..1) travel, added
    once and clamped once. The knob and its automation are never written.

    The offsets are written by the audio thread (once per block, before anything reads the
    parameters) and read by whoever reads a LiveParam. A parameter no macro moves has an offset
    of exactly 0, and its value is returned untouched, bit for bit.
*/
class LiveOffsets
{
public:
    /** Message thread, before any LiveParam is made: indexes every parameter of `state`. */
    void attach (juce::AudioProcessorValueTreeState& state)
    {
        parameters.clear();
        indices.clear();
        for (auto* p : state.processor.getParameters())
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            {
                indices[ranged->paramID] = static_cast<int> (parameters.size());
                parameters.push_back (ranged);
            }
        offsets = std::make_unique<std::atomic<float>[]> (parameters.size());
        for (std::size_t i = 0; i < parameters.size(); ++i)
            offsets[i].store (0.0f, std::memory_order_relaxed);
        read.assign (parameters.size(), false);
    }
    int size() const noexcept { return static_cast<int> (parameters.size()); }
    int indexOf (const juce::String& id) const
    {
        const auto it = indices.find (id);
        return it != indices.end() ? it->second : -1;
    }
    /** The engine reads this parameter every block (a LiveParam was made for it). */
    bool isRead (int index) const noexcept { return index >= 0 && index < size() && read[static_cast<std::size_t> (index)]; }
    void markRead (int index)
    {
        if (index >= 0 && index < size())
            read[static_cast<std::size_t> (index)] = true;
    }
    juce::RangedAudioParameter* parameter (int index) const noexcept
    {
        return index >= 0 && index < size() ? parameters[static_cast<std::size_t> (index)] : nullptr;
    }

    /** Audio thread: the macros' smoothed sum on a parameter (normalised; 0: none). */
    void setOffset (int index, float normalised) noexcept { offsets[static_cast<std::size_t> (index)].store (normalised, std::memory_order_relaxed); }
    float offset (int index) const noexcept
    {
        return index >= 0 && index < size() ? offsets[static_cast<std::size_t> (index)].load (std::memory_order_relaxed) : 0.0f;
    }
    /** The value read: untouched without an offset; else moved along the parameter's own travel. */
    float apply (int index, float value) const noexcept
    {
        const float off = offsets[static_cast<std::size_t> (index)].load (std::memory_order_relaxed);
        if (off == 0.0f)
            return value;
        const auto* p = parameters[static_cast<std::size_t> (index)];
        return p->convertFrom0to1 (std::clamp (p->convertTo0to1 (value) + off, 0.0f, 1.0f));
    }

private:
    std::vector<juce::RangedAudioParameter*> parameters;
    std::map<juce::String, int> indices;
    std::unique_ptr<std::atomic<float>[]> offsets;
    std::vector<bool> read;
};

/** A parameter's value as the engine reads it (LiveOffsets): used where a raw
    std::atomic<float>* was (`p->load()` reads the same, plus any macro offset). */
struct LiveParam
{
    std::atomic<float>* raw = nullptr;
    const LiveOffsets* live = nullptr;
    int index = -1;

    float load (std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
        const float v = raw->load (order);
        return live != nullptr && index >= 0 ? live->apply (index, v) : v;
    }
    const LiveParam* operator->() const noexcept { return this; }
    explicit operator bool() const noexcept { return raw != nullptr; }
    bool operator== (std::nullptr_t) const noexcept { return raw == nullptr; }
    bool operator!= (std::nullptr_t) const noexcept { return raw != nullptr; }
};

} // namespace osp::plugin
