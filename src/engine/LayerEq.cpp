#include "engine/LayerEq.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace osp::eq
{

namespace
{
    constexpr int chunk = 16;                 ///< samples between redesigns
    constexpr double glideSeconds = 0.02;     ///< frequency / gain / Q
    constexpr double fadeSeconds = 0.01;      ///< a band or the EQ switched
    constexpr double butterworth4[2] { 0.54119610014619698, 1.3065629648763766 };

    double clampHz (double hz, double sampleRate) noexcept
    {
        return std::clamp (hz, 10.0, 0.45 * sampleRate);
    }

    Section passSection (bool high, double hz, double q, double sampleRate) noexcept
    {
        Section s;
        s.g = std::tan (std::numbers::pi * clampHz (hz, sampleRate) / sampleRate);
        s.k = 1.0 / q;
        if (high)
            s = { s.g, s.k, 1.0, -s.k, -1.0 };
        else
            s = { s.g, s.k, 0.0, 0.0, 1.0 };
        return s;
    }
}

const char* bandName (Band band) noexcept
{
    switch (band)
    {
        case Band::highPass: return "HP";
        case Band::lowShelf: return "LOW SHELF";
        case Band::bell: return "BELL";
        case Band::highShelf: return "HIGH SHELF";
        case Band::lowPass: return "LP";
    }
    return "";
}

Range frequencyRange (Band band) noexcept
{
    switch (band)
    {
        case Band::highPass: return { 20.0, 2000.0, 80.0 };
        case Band::lowShelf: return { 20.0, 1000.0, 200.0 };
        case Band::bell: return { 20.0, 20000.0, 1000.0 };
        case Band::highShelf: return { 1000.0, 20000.0, 5000.0 };
        case Band::lowPass: return { 500.0, 20000.0, 12000.0 };
    }
    return { 20.0, 20000.0, 1000.0 };
}

bool hasGain (Band band) noexcept { return band == Band::lowShelf || band == Band::bell || band == Band::highShelf; }
bool hasQ (Band band) noexcept { return hasGain (band); }
bool hasSlope (Band band) noexcept { return band == Band::highPass || band == Band::lowPass; }

Settings::Settings()
{
    for (int b = 0; b < bandCount; ++b)
    {
        auto& band = bands[static_cast<std::size_t> (b)];
        band.frequencyHz = frequencyRange (static_cast<Band> (b)).defaultHz;
        band.q = static_cast<Band> (b) == Band::bell ? 1.0 : 0.707;
    }
}

bool Settings::audible() const noexcept
{
    if (! enabled)
        return false;
    for (const auto& b : bands)
        if (b.enabled)
            return true;
    return false;
}

int design (Band band, const BandSettings& s, double sampleRate, std::array<Section, 2>& sections) noexcept
{
    // Simper's forms ("Linear trapezoidal integrated SVF"): the RBJ cookbook's responses.
    const double hz = clampHz (s.frequencyHz, sampleRate);
    const double g = std::tan (std::numbers::pi * hz / sampleRate);
    switch (band)
    {
        case Band::highPass:
        case Band::lowPass:
        {
            const bool high = band == Band::highPass;
            if (! s.steep)
            {
                sections[0] = passSection (high, hz, std::numbers::sqrt2 / 2.0, sampleRate);
                return 1;
            }
            sections[0] = passSection (high, hz, butterworth4[0], sampleRate);
            sections[1] = passSection (high, hz, butterworth4[1], sampleRate);
            return 2;
        }
        case Band::bell:
        {
            const double a = std::pow (10.0, std::clamp (s.gainDb, -maxGainDb, maxGainDb) / 40.0);
            const double k = 1.0 / (std::clamp (s.q, minBellQ, maxBellQ) * a);
            sections[0] = { g, k, 1.0, k * (a * a - 1.0), 0.0 };
            return 1;
        }
        case Band::lowShelf:
        {
            const double a = std::pow (10.0, std::clamp (s.gainDb, -maxGainDb, maxGainDb) / 40.0);
            const double k = 1.0 / std::clamp (s.q, minShelfQ, maxShelfQ);
            sections[0] = { g / std::sqrt (a), k, 1.0, k * (a - 1.0), a * a - 1.0 };
            return 1;
        }
        case Band::highShelf:
        {
            const double a = std::pow (10.0, std::clamp (s.gainDb, -maxGainDb, maxGainDb) / 40.0);
            const double k = 1.0 / std::clamp (s.q, minShelfQ, maxShelfQ);
            sections[0] = { g * std::sqrt (a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a };
            return 1;
        }
    }
    return 0;
}

double sectionDb (const Section& s, double hz, double sampleRate) noexcept
{
    // The trapezoidal SVF is the bilinear transform of H(s) = m0 + (m1 s + m2) / (s^2 + k s + 1),
    // s normalised by the prewarped cutoff: at digital frequency w, s = j tan (w / 2) / g.
    const double w = 2.0 * std::numbers::pi * std::clamp (hz, 1.0, 0.4999 * sampleRate) / sampleRate;
    const std::complex<double> sj (0.0, std::tan (0.5 * w) / s.g);
    const auto h = s.m0 + (s.m1 * sj + s.m2) / (sj * sj + s.k * sj + 1.0);
    return 20.0 * std::log10 (std::max (1.0e-12, std::abs (h)));
}

double bandResponseDb (Band band, const BandSettings& settings, double hz, double sampleRate) noexcept
{
    std::array<Section, 2> sections {};
    const int count = design (band, settings, sampleRate, sections);
    double db = 0.0;
    for (int i = 0; i < count; ++i)
        db += sectionDb (sections[static_cast<std::size_t> (i)], hz, sampleRate);
    return db;
}

double responseDb (const Settings& settings, double hz, double sampleRate) noexcept
{
    if (! settings.enabled)
        return 0.0;
    double db = 0.0;
    for (int b = 0; b < bandCount; ++b)
        if (settings.bands[static_cast<std::size_t> (b)].enabled)
            db += bandResponseDb (static_cast<Band> (b), settings.bands[static_cast<std::size_t> (b)], hz, sampleRate);
    return db;
}

//==============================================================================
void Processor::prepare (double rate) noexcept
{
    sampleRate = rate > 0.0 ? rate : 48000.0;
    reset();
}

void Processor::reset() noexcept
{
    for (auto& b : bands)
    {
        b.state = {};
        b.designed = false;
        b.mix = 0.0;
    }
    masterMix = 0.0;
    started = false;
}

void Processor::setTarget (const Settings& settings) noexcept
{
    target = settings;
    if (! started)
    {
        // The first settings: no glide from nothing (the switches still fade in).
        for (int i = 0; i < bandCount; ++i)
        {
            auto& b = bands[static_cast<std::size_t> (i)];
            const auto& t = target.bands[static_cast<std::size_t> (i)];
            b.logHz = std::log2 (t.frequencyHz);
            b.gainDb = t.gainDb;
            b.q = t.q;
            b.steep = t.steep;
        }
        started = true;
    }
}

bool Processor::active() const noexcept
{
    return masterMix > 0.0 || target.audible();
}

bool Processor::ringing() const noexcept
{
    for (const auto& b : bands)
        if (b.mix > 0.0)
            for (const auto& z : b.state)
                if (std::abs (z[0]) > 1.0e-9 || std::abs (z[1]) > 1.0e-9)
                    return true;
    return false;
}

void Processor::glide (int samples) noexcept
{
    const double k = 1.0 - std::exp (-static_cast<double> (samples) / (glideSeconds * sampleRate));
    for (int i = 0; i < bandCount; ++i)
    {
        auto& b = bands[static_cast<std::size_t> (i)];
        const auto& t = target.bands[static_cast<std::size_t> (i)];
        const double logHz = std::log2 (std::max (1.0, t.frequencyHz));
        const bool wasOff = b.mix <= 0.0;
        if (wasOff)
        {
            // Switched on from silence: start at the target (nothing to glide from).
            b.logHz = logHz;
            b.gainDb = t.gainDb;
            b.q = t.q;
        }
        const double before[3] { b.logHz, b.gainDb, b.q };
        b.logHz += (logHz - b.logHz) * k;
        b.gainDb += (t.gainDb - b.gainDb) * k;
        b.q += (t.q - b.q) * k;
        if (std::abs (b.logHz - logHz) < 1.0e-5)
            b.logHz = logHz;
        if (std::abs (b.gainDb - t.gainDb) < 1.0e-4)
            b.gainDb = t.gainDb;
        if (std::abs (b.q - t.q) < 1.0e-5)
            b.q = t.q;
        if (t.steep != b.steep)
        {
            // A slope change: a new structure (the second section starts from rest).
            b.steep = t.steep;
            b.state[2] = b.state[3] = {};
            b.designed = false;
        }
        if (! b.designed || before[0] != b.logHz || before[1] != b.gainDb || before[2] != b.q)
        {
            BandSettings now;
            now.frequencyHz = std::exp2 (b.logHz);
            now.gainDb = b.gainDb;
            now.q = b.q;
            now.steep = b.steep;
            b.sectionCount = design (static_cast<Band> (i), now, sampleRate, b.sections);
            b.designed = true;
        }
    }
}

void Processor::processBand (BandState& b, int band, float* left, float* right, int n, bool mono) noexcept
{
    const auto& t = target.bands[static_cast<std::size_t> (band)];
    const double goal = target.enabled && t.enabled ? 1.0 : 0.0;
    const double step = static_cast<double> (n) / (fadeSeconds * sampleRate);
    const double m0 = b.mix;
    const double m1 = goal > m0 ? std::min (goal, m0 + step) : std::max (goal, m0 - step);
    b.mix = m1;
    if (m0 <= 0.0 && m1 <= 0.0)
    {
        b.state = {};
        return;
    }
    for (int ch = 0; ch < (mono ? 1 : 2); ++ch)
    {
        float* x = ch == 0 ? left : right;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            double y = in;
            for (int s = 0; s < b.sectionCount; ++s)
            {
                const auto& c = b.sections[static_cast<std::size_t> (s)];
                auto& z = b.state[static_cast<std::size_t> (s * 2 + ch)];
                const double a1 = 1.0 / (1.0 + c.g * (c.g + c.k));
                const double a2 = c.g * a1, a3 = c.g * a2;
                const double v3 = y - z[1];
                const double v1 = a1 * z[0] + a2 * v3;
                const double v2 = z[1] + a2 * z[0] + a3 * v3;
                z[0] = 2.0 * v1 - z[0];
                z[1] = 2.0 * v2 - z[1];
                y = c.m0 * y + c.m1 * v1 + c.m2 * v2;
            }
            const double m = m0 + (m1 - m0) * static_cast<double> (i + 1) / static_cast<double> (n);
            x[i] = static_cast<float> (in + m * (y - in));
        }
    }
    if (m1 <= 0.0)
        b.state = {};   // off: the next switch-on starts from rest
    for (auto& z : b.state)
        for (auto& v : z)
            if (std::abs (v) < 1.0e-25)
                v = 0.0;   // no denormals in a decaying tail
}

void Processor::process (float* left, float* right, int numSamples, bool mono) noexcept
{
    // The EQ's own switch is the bands' common switch: each band fades with it, so switching
    // the EQ off fades every band out (and on, in) without a separate dry path.
    for (int done = 0; done < numSamples;)
    {
        const int n = std::min (chunk, numSamples - done);
        glide (n);
        for (int band = 0; band < bandCount; ++band)
            processBand (bands[static_cast<std::size_t> (band)], band, left + done, mono ? nullptr : right + done, n, mono);
        done += n;
    }
    masterMix = 0.0;
    for (const auto& b : bands)
        masterMix = std::max (masterMix, b.mix);
}

} // namespace osp::eq
