#pragma once

// Four float lanes for the engine's hottest inner loops (the sinc reads, the stereo ladder,
// the grain pool). SSE2 on x86-64 and NEON on arm64 are part of those platforms' baselines,
// so no runtime dispatch is needed; anything else gets a plain-array fallback with the same
// results. JUCE's SIMDRegister would do the same, but osp_dsp stays free of JUCE.
//
// Sums over lanes are taken in one fixed order ((0 + 1) + (2 + 3)) on every platform, so a
// render is repeatable from its seed wherever it runs on the same compiler settings.

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #include <emmintrin.h>
    #define OSP_SIMD_SSE 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
    #include <arm_neon.h>
    #define OSP_SIMD_NEON 1
#endif

namespace osp::simd
{

struct Float4
{
#if OSP_SIMD_SSE
    __m128 v;
    static Float4 load (const float* p) noexcept { return { _mm_loadu_ps (p) }; }
    static Float4 broadcast (float x) noexcept { return { _mm_set1_ps (x) }; }
    static Float4 set (float a, float b, float c, float d) noexcept { return { _mm_setr_ps (a, b, c, d) }; }
    void store (float* p) const noexcept { _mm_storeu_ps (p, v); }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return { _mm_add_ps (a.v, b.v) }; }
    friend Float4 operator- (Float4 a, Float4 b) noexcept { return { _mm_sub_ps (a.v, b.v) }; }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return { _mm_mul_ps (a.v, b.v) }; }
    friend Float4 operator/ (Float4 a, Float4 b) noexcept { return { _mm_div_ps (a.v, b.v) }; }
    static Float4 min (Float4 a, Float4 b) noexcept { return { _mm_min_ps (a.v, b.v) }; }
    static Float4 max (Float4 a, Float4 b) noexcept { return { _mm_max_ps (a.v, b.v) }; }
#elif OSP_SIMD_NEON
    float32x4_t v;
    static Float4 load (const float* p) noexcept { return { vld1q_f32 (p) }; }
    static Float4 broadcast (float x) noexcept { return { vdupq_n_f32 (x) }; }
    static Float4 set (float a, float b, float c, float d) noexcept
    {
        const float x[4] = { a, b, c, d };
        return { vld1q_f32 (x) };
    }
    void store (float* p) const noexcept { vst1q_f32 (p, v); }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return { vaddq_f32 (a.v, b.v) }; }
    friend Float4 operator- (Float4 a, Float4 b) noexcept { return { vsubq_f32 (a.v, b.v) }; }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return { vmulq_f32 (a.v, b.v) }; }
    friend Float4 operator/ (Float4 a, Float4 b) noexcept { return { vdivq_f32 (a.v, b.v) }; }
    static Float4 min (Float4 a, Float4 b) noexcept { return { vminq_f32 (a.v, b.v) }; }
    static Float4 max (Float4 a, Float4 b) noexcept { return { vmaxq_f32 (a.v, b.v) }; }
#else
    float v[4];
    static Float4 load (const float* p) noexcept { return { { p[0], p[1], p[2], p[3] } }; }
    static Float4 broadcast (float x) noexcept { return { { x, x, x, x } }; }
    static Float4 set (float a, float b, float c, float d) noexcept { return { { a, b, c, d } }; }
    void store (float* p) const noexcept
    {
        for (int i = 0; i < 4; ++i)
            p[i] = v[i];
    }
    template <typename Op>
    static Float4 each (Float4 a, Float4 b, Op op) noexcept
    {
        return { { op (a.v[0], b.v[0]), op (a.v[1], b.v[1]), op (a.v[2], b.v[2]), op (a.v[3], b.v[3]) } };
    }
    friend Float4 operator+ (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return x + y; }); }
    friend Float4 operator- (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return x - y; }); }
    friend Float4 operator* (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return x * y; }); }
    friend Float4 operator/ (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return x / y; }); }
    static Float4 min (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return y < x ? y : x; }); }
    static Float4 max (Float4 a, Float4 b) noexcept { return each (a, b, [] (float x, float y) { return x < y ? y : x; }); }
#endif

    static Float4 zero() noexcept { return broadcast (0.0f); }

    /** The four lanes added in a fixed order: (0 + 1) + (2 + 3). */
    float sum() const noexcept
    {
        float x[4];
        store (x);
        return (x[0] + x[1]) + (x[2] + x[3]);
    }
};

} // namespace osp::simd
