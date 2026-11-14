#pragma once

#include <atomic>
#include <cstdint>

namespace osp
{

/**
    CHARACTER's telemetry (read only: the CHARACTER rack's live graph, as ANDOR ANALOG's FILTER
    rack reads its Synth::FilterTap). One tap per voice slot, written by the audio thread once a
    block from what the voice's filter used at its last control update; a sequence count around
    each write lets any thread read a consistent snapshot. Writing it changes nothing a voice
    plays. No allocation, no lock.
*/
struct CharacterTap
{
    struct Snapshot
    {
        bool active = false, released = false;
        int note = -1, layer = 0;
        int stage = 0;               ///< the filter envelope: 0 at rest, 1 attack, 2 decay
        std::uint64_t order = 0;     ///< note-on order (newer is larger)
        float velocity = 0.0f;       ///< 0..1
        float baseOctaves = 0.0f;    ///< the cutoff before the envelope (CHARACTER, its routes, velocity, drift), log2 Hz
        float depthOctaves = 0.0f;   ///< the envelope's reach at its top (ENV x the note's scale), octaves
        float env = 0.0f, envPeak = 0.0f;   ///< the filter envelope now (0..1); its highest since the note began
        float cutoffOctaves = 0.0f;  ///< what the filter uses: base + env x depth, log2 Hz
    };

    /** False if no consistent snapshot could be read (a write in progress each try). */
    bool read (Snapshot& out) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto before = sequence.load (std::memory_order_acquire);
            if ((before & 1u) != 0u)
                continue;
            Snapshot s;
            s.active = active.load (std::memory_order_relaxed);
            s.released = released.load (std::memory_order_relaxed);
            s.note = note.load (std::memory_order_relaxed);
            s.layer = layer.load (std::memory_order_relaxed);
            s.stage = stage.load (std::memory_order_relaxed);
            s.order = order.load (std::memory_order_relaxed);
            s.velocity = velocity.load (std::memory_order_relaxed);
            s.baseOctaves = baseOctaves.load (std::memory_order_relaxed);
            s.depthOctaves = depthOctaves.load (std::memory_order_relaxed);
            s.env = env.load (std::memory_order_relaxed);
            s.envPeak = envPeak.load (std::memory_order_relaxed);
            s.cutoffOctaves = cutoffOctaves.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (sequence.load (std::memory_order_relaxed) == before)
            {
                out = s;
                return true;
            }
        }
        return false;
    }

    /** Audio thread only (one writer). */
    void write (const Snapshot& s) noexcept
    {
        const auto seq = sequence.load (std::memory_order_relaxed);
        sequence.store (seq + 1u, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        active.store (s.active, std::memory_order_relaxed);
        released.store (s.released, std::memory_order_relaxed);
        note.store (s.note, std::memory_order_relaxed);
        layer.store (s.layer, std::memory_order_relaxed);
        stage.store (s.stage, std::memory_order_relaxed);
        order.store (s.order, std::memory_order_relaxed);
        velocity.store (s.velocity, std::memory_order_relaxed);
        baseOctaves.store (s.baseOctaves, std::memory_order_relaxed);
        depthOctaves.store (s.depthOctaves, std::memory_order_relaxed);
        env.store (s.env, std::memory_order_relaxed);
        envPeak.store (s.envPeak, std::memory_order_relaxed);
        cutoffOctaves.store (s.cutoffOctaves, std::memory_order_relaxed);
        sequence.store (seq + 2u, std::memory_order_release);
    }

    bool isActive() const noexcept { return active.load (std::memory_order_relaxed); }

private:
    std::atomic<std::uint32_t> sequence { 0 };
    std::atomic<bool> active { false }, released { false };
    std::atomic<int> note { -1 }, layer { 0 }, stage { 0 };
    std::atomic<std::uint64_t> order { 0 };
    std::atomic<float> velocity { 0.0f }, baseOctaves { 0.0f }, depthOctaves { 0.0f }, env { 0.0f }, envPeak { 0.0f }, cutoffOctaves { 0.0f };
};

} // namespace osp
