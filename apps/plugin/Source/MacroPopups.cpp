// The five macro popups (redesign §2): each one explains its DSP with a picture drawn
// from the same formulas and parameters the sound uses, plus a few compact controls.
#include "EngineCard.h"
#include "ShapingPopups.h"

#include "core/Prng.h"
#include "engine/InstrumentEngine.h"
#include "engine/RhythmicShaper.h"
#include "engine/Shaping.h"
#include "engine/SpaceReverb.h"

#include <cmath>
#include <complex>
#include <numbers>

namespace osp::plugin
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    float value (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        if (auto* p = state.getParameter (id))
            return p->convertFrom0to1 (p->getValue());
        return 0.0f;
    }

    juce::String secondsLabel (double t)
    {
        auto text = juce::String (t, t < 1.0 && t > 0.0 ? 2 : 1);
        while (text.containsChar ('.') && (text.endsWithChar ('0') || text.endsWithChar ('.')))
            text = text.dropLastCharacters (1);
        return text + " s";
    }

    /**
        The graphite well every popup draws in. Repaints at 30 Hz while open when its
        picture moves or its parameters changed, and fades from the old picture (140 ms)
        when it changes kind (a mode or type).
    */
    class Visual : public juce::Component, private juce::Timer
    {
    public:
        Visual()
        {
            setInterceptsMouseClicks (false, false);
            startTimerHz (30);
        }
        ~Visual() override { stopTimer(); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.setColour (palette::graphite);
            g.fillRoundedRectangle (r, 7.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.2f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f,
                                                     r.getY() + 9.0f, false));
            g.fillRoundedRectangle (r.withHeight (10.0f), 7.0f);
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (getLocalBounds().reduced (1));
                paintVisual (g, r.reduced (12.0f, 9.0f));
            }
            if (fade > 0.0f && previous.isValid())
            {
                g.setOpacity (fade);
                g.drawImage (previous, r);
            }
        }

    protected:
        virtual void paintVisual (juce::Graphics&, juce::Rectangle<float> plot) = 0;
        /** A moving picture (drawn again every tick). */
        virtual bool animates() const { return false; }
        /** True when what the picture shows has changed (parameters checked every tick). */
        virtual bool changed() { return false; }
        /** The picture changes kind: fade out of the old one. */
        void transition()
        {
            if (getWidth() > 0 && getHeight() > 0 && isShowing())
            {
                previous = createComponentSnapshot (getLocalBounds(), true, 2.0f);
                fade = 1.0f;
            }
        }

    private:
        void timerCallback() override
        {
            const bool now = changed();
            if (fade > 0.0f)
            {
                fade = std::max (0.0f, fade - 1.0f / (0.14f * 30.0f));
                repaint();
            }
            else if (now || animates())
                repaint();
        }
        juce::Image previous;
        float fade = 0.0f;
    };

    /** Remembers a few values; `differs` is true when any of them moved since last time. */
    struct Watch
    {
        std::vector<double> last;
        bool differs (std::initializer_list<double> now)
        {
            const std::vector<double> v (now);
            if (v == last)
                return false;
            last = v;
            return true;
        }
    };

    //==========================================================================
    // SPACE: the sound going into the room - transient, early reflections, the dense tail
    // and its decay - drawn from the type's own design (SpaceReverb::portrait) and DECAY.
    class SpaceVisual final : public Visual
    {
    public:
        explicit SpaceVisual (juce::AudioProcessorValueTreeState& s) : state (s) {}

    private:
        bool changed() override
        {
            const int type = juce::roundToInt (value (state, "space.type"));
            if (type != shownType && shownType >= 0)
                transition();
            shownType = type;
            if (watch.differs ({ static_cast<double> (type), value (state, "space.decay"), value (state, "space"), static_cast<double> (getWidth()) }))
            {
                cacheValid = false;
                return true;
            }
            return false;
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> plot) override
        {
            const float scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
            const int w = juce::roundToInt (static_cast<float> (getWidth()) * scale), h = juce::roundToInt (static_cast<float> (getHeight()) * scale);
            if (! cacheValid || ! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
            {
                cache = juce::Image (juce::Image::ARGB, std::max (1, w), std::max (1, h), true);
                juce::Graphics cg (cache);
                cg.addTransform (juce::AffineTransform::scale (scale));
                draw (cg, plot);
                cacheValid = true;
            }
            g.drawImage (cache, getLocalBounds().toFloat());
        }

        void draw (juce::Graphics& g, juce::Rectangle<float> plot)
        {
            using namespace palette;
            const auto type = static_cast<SpaceType> (std::clamp (juce::roundToInt (value (state, "space.type")), 0, 3));
            double lo = 0.2, hi = 8.0;
            shaping::decayRange (type, lo, hi);
            const double decay = std::clamp (static_cast<double> (value (state, "space.decay")), lo, hi);
            const double amount = 0.5 + 0.5 * std::clamp (static_cast<double> (value (state, "space")) * 0.01, 0.0, 1.0);
            const auto portrait = SpaceReverb::portrait (type);
            const double span = std::max (0.6, decay * 1.15);
            auto plotArea = plot.withTrimmedBottom (12.0f);
            const float mid = plotArea.getCentreY(), half = plotArea.getHeight() * 0.46f;
            auto xAt = [&] (double t) { return plotArea.getX() + static_cast<float> (t / span) * plotArea.getWidth(); };

            // Time axis.
            {
                double step = 0.25;
                for (double s : { 0.1, 0.25, 0.5, 1.0, 2.0 })
                    if (span / s <= 6.0)
                    {
                        step = s;
                        break;
                    }
                g.setFont (fonts::make (9.0f));
                for (double t = 0.0; t <= span + 1.0e-6; t += step)
                {
                    const float x = xAt (t);
                    g.setColour (displayLine.withAlpha (0.45f));
                    g.drawVerticalLine (juce::roundToInt (x), plotArea.getY(), plotArea.getBottom());
                    g.setColour (displayText.withAlpha (0.75f));
                    g.drawText (secondsLabel (t), juce::Rectangle<float> (x + 2.0f, plot.getBottom() - 11.0f, 40.0f, 11.0f), juce::Justification::centredLeft, false);
                }
            }

            // The tail: -60 dB at DECAY, built up as the diffusers fill in; warmth fades into
            // cool mineral and loses saturation towards silence. Spring tails ripple.
            // Drawn in decibels (as the ear hears a tail): it falls in a straight line to
            // nothing at -60 dB, i.e. at DECAY.
            const double onset = 0.001 * portrait.preMs;
            const double build = std::max (0.004, 0.003 * portrait.diffusionMs * (type == SpaceType::chamber ? 4.0 : 2.0));
            auto tail = [&] (double t) {
                if (t <= onset)
                    return 0.0;
                double a = std::max (0.0, 1.0 - t / decay) * (1.0 - std::exp (-(t - onset) / build)) * amount;
                if (portrait.spring)
                    a *= 0.75 + 0.25 * std::sin (twoPi * t / 0.037);
                return a;
            };
            const int count = type == SpaceType::plate ? 4200 : (type == SpaceType::chamber ? 3600 : (type == SpaceType::room ? 2600 : 3000));
            const double spread = 0.55 + 0.45 * portrait.width;
            Prng rng (Prng::deriveSeed (0x7370616365ull, static_cast<std::uint64_t> (type), 0));
            for (int i = 0; i < count; ++i)
            {
                const double t = onset + (span - onset) * std::pow (rng.nextDouble(), 1.7);
                const double a = tail (t);
                if (a < 1.0e-3)
                    continue;
                // Plate: dense and smooth (close to its envelope); the others scatter more.
                const double scatter = type == SpaceType::plate ? 0.55 + 0.45 * rng.nextDouble() : std::clamp (std::abs (rng.gaussian()) * 0.6, 0.0, 1.0);
                const float y = mid + static_cast<float> ((rng.nextDouble() < 0.5 ? -1.0 : 1.0) * scatter * a * spread) * half;
                // Warmth dissipating: the colour moves on faster the darker the type damps.
                const float progress = static_cast<float> (std::pow (std::min (1.0, t / decay), 1.0 - 0.5 * portrait.damping));
                const float env = static_cast<float> (std::max (0.0, 1.0 - t / decay));
                auto colour = spectrum (0.04f + 0.96f * progress).withMultipliedSaturation (0.3f + 0.7f * env);
                const float size = 0.9f + 1.6f * static_cast<float> (a) * static_cast<float> (rng.nextDouble());
                g.setColour (colour.withAlpha (std::clamp (0.1f + 0.55f * static_cast<float> (a), 0.0f, 0.8f)));
                g.fillEllipse (xAt (t) - 0.5f * size, y - 0.5f * size, size, size);
            }
            // Its envelope, thinly.
            juce::Path outline;
            for (int i = 0; i <= 160; ++i)
            {
                const double t = span * i / 160.0;
                const float y = mid - static_cast<float> (tail (t) * spread) * half;
                if (i == 0)
                    outline.startNewSubPath (xAt (t), y);
                else
                    outline.lineTo (xAt (t), y);
            }
            g.setColour (raised.withAlpha (0.28f));
            g.strokePath (outline, juce::PathStrokeType (1.0f));

            // Early reflections at the type's own times (springs: dispersed echoes).
            for (std::size_t i = 0; i < portrait.erMs.size(); ++i)
            {
                const double t = onset + 0.001 * portrait.erMs[i];
                const float level = portrait.erLevel * (portrait.spring ? std::pow (0.72f, static_cast<float> (i)) : 1.0f / (1.0f + 0.35f * static_cast<float> (i)));
                if (level < 0.02f || t > span)
                    continue;
                const float x = xAt (t), extent = half * std::min (0.95f, 1.2f * level) * static_cast<float> (amount);
                const auto colour = gold.interpolatedWith (coral, static_cast<float> (i) / 8.0f);
                if (portrait.spring)
                {
                    juce::Path chirp;
                    for (int k = 0; k <= 24; ++k)
                    {
                        const float u = static_cast<float> (k) / 24.0f;
                        const float px = x + (u - 0.5f) * 10.0f;
                        const float py = mid - extent * std::sin (u * 9.0f + u * u * 14.0f) * (1.0f - std::abs (u - 0.5f) * 2.0f);
                        if (k == 0)
                            chirp.startNewSubPath (px, py);
                        else
                            chirp.lineTo (px, py);
                    }
                    g.setColour (colour.withAlpha (0.85f));
                    g.strokePath (chirp, juce::PathStrokeType (1.2f));
                }
                else
                {
                    g.setColour (colour.withAlpha (0.85f));
                    g.drawLine (x, mid - extent, x, mid + extent, 1.4f);
                }
            }

            // The sound itself: a bright transient at zero.
            const float x0 = xAt (0.0);
            g.setColour (amber.withAlpha (0.25f));
            g.fillRect (juce::Rectangle<float> (x0, mid - half * 0.95f, 5.0f, half * 1.9f));
            g.setColour (amber.brighter (0.4f));
            g.drawLine (x0 + 1.0f, mid - half * 0.95f, x0 + 1.0f, mid + half * 0.95f, 2.0f);

            // Where it has died away (-60 dB).
            const float xd = xAt (decay);
            juce::Path marker;
            marker.startNewSubPath (xd, plotArea.getY());
            marker.lineTo (xd, plotArea.getBottom());
            juce::Path dashed;
            const float dashes[] = { 2.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, marker, dashes, 2);
            g.setColour (displayText.withAlpha (0.6f));
            g.fillPath (dashed);
            g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.08f));
            g.drawText ("-60 dB", juce::Rectangle<float> (xd - 46.0f, plotArea.getY(), 42.0f, 11.0f), juce::Justification::centredRight, false);
            g.setColour (displayText.withAlpha (0.8f));
            g.drawText ("EARLY", juce::Rectangle<float> (xAt (onset + 0.004) - 2.0f, plotArea.getY(), 50.0f, 11.0f), juce::Justification::centredLeft, false);
        }

        juce::AudioProcessorValueTreeState& state;
        Watch watch;
        int shownType = -1;
        juce::Image cache;
        bool cacheValid = false;
    };

    //==========================================================================
    // CHARACTER: the filter's response over the sound's own spectrum, the cutoff where
    // CHARACTER puts it, the resonance, and the envelope's peak as a ghost.
    class CharacterVisual final : public Visual
    {
    public:
        explicit CharacterVisual (OspAudioProcessor& p) : processor (p) {}

        /** The response (dB) at f of `type` with cutoff fc (the CharacterFilter designs). */
        static double responseDb (FilterType type, double f, double fc, double resonance, double tiltDb)
        {
            const std::complex<double> s (0.0, f / std::max (1.0, fc));
            const double res = std::clamp (resonance, 0.0, 0.9);
            std::complex<double> h (1.0, 0.0);
            switch (type)
            {
                case FilterType::lp24:
                {
                    const double k = 3.8 * std::pow (res, 0.85);
                    h = (1.0 + 0.5 * k) / (std::pow (1.0 + s, 4.0) + k);
                    break;
                }
                case FilterType::lp12:
                case FilterType::hp12:
                case FilterType::bp12:
                {
                    const double qMax = type == FilterType::lp12 ? 8.0 : 3.5;
                    const double q = 0.55 + std::pow (res / 0.9, 1.6) * (qMax - 0.55);
                    const auto d = s * s + s / q + 1.0;
                    h = type == FilterType::lp12 ? 1.0 / d : (type == FilterType::hp12 ? s * s / d : 1.4 * (s / q) / d);
                    break;
                }
                case FilterType::tilt:
                {
                    const double low = 1.0 / (1.0 + (f / 350.0) * (f / 350.0));
                    const double high = (f / 1800.0) * (f / 1800.0) / (1.0 + (f / 1800.0) * (f / 1800.0));
                    return 0.5 * tiltDb * (high - low);
                }
                case FilterType::off: return 0.0;
            }
            return 20.0 * std::log10 (std::max (1.0e-9, std::abs (h)));
        }

    private:
        bool changed() override
        {
            auto& s = processor.parameters;
            const auto instrument = processor.currentInstrument (processor.editLayer());
            const int type = juce::roundToInt (value (s, "character.type"));
            if (type != shownType && shownType >= 0)
                transition();
            shownType = type;
            return watch.differs ({ static_cast<double> (type), value (s, "character"), value (s, "character.min"), value (s, "character.max"),
                                    value (s, "character.resonance"), value (s, "character.drive"), value (s, "character.envAmount"),
                                    static_cast<double> (instrument != nullptr ? instrument->generation : 0) });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> plot) override
        {
            using namespace palette;
            auto& s = processor.parameters;
            const auto type = static_cast<FilterType> (std::clamp (juce::roundToInt (value (s, "character.type")), 0, 4));
            Shaping shaping;
            shaping.filterMinHz = value (s, "character.min");
            shaping.filterMaxHz = value (s, "character.max");
            const double position = 0.01 * value (s, "character");
            const double fc = std::exp2 (shaping::cutoffOctaves (shaping, position));
            const double resonance = 0.01 * value (s, "character.resonance");
            const double env = 0.01 * value (s, "character.envAmount");
            const double tilt = (position - 0.5) * 24.0;
            auto area = plot.withTrimmedBottom (11.0f);
            constexpr double topDb = 15.0, bottomDb = -36.0;
            auto xAt = [&] (double f) { return area.getX() + static_cast<float> (std::log (f / 20.0) / std::log (1000.0)) * area.getWidth(); };
            auto yAt = [&] (double db) { return area.getY() + static_cast<float> ((topDb - std::clamp (db, bottomDb - 6.0, topDb)) / (topDb - bottomDb)) * area.getHeight(); };

            // Grid: decades, the 0 dB line.
            g.setFont (fonts::make (9.0f));
            for (const auto& [f, label] : { std::pair { 100.0, "100" }, std::pair { 1000.0, "1k" }, std::pair { 10000.0, "10k" } })
            {
                g.setColour (displayLine.withAlpha (0.55f));
                g.drawVerticalLine (juce::roundToInt (xAt (f)), area.getY(), area.getBottom());
                g.setColour (displayText.withAlpha (0.75f));
                g.drawText (label, juce::Rectangle<float> (xAt (f) + 2.0f, plot.getBottom() - 10.0f, 30.0f, 10.0f), juce::Justification::centredLeft, false);
            }
            g.setColour (displayLine);
            g.drawHorizontalLine (juce::roundToInt (yAt (0.0)), area.getX(), area.getRight());

            // The sound's own spectrum, faintly, in its spectral colours.
            if (const auto instrument = processor.currentInstrument (processor.editLayer()); instrument != nullptr && ! instrument->spectrumDb.empty())
            {
                juce::Path spectrum;
                spectrum.startNewSubPath (area.getX(), area.getBottom());
                const auto& bins = instrument->spectrumDb;
                for (std::size_t b = 0; b < bins.size(); ++b)
                {
                    const double f = 20.0 * std::pow (1000.0, (static_cast<double> (b) + 0.5) / static_cast<double> (bins.size()));
                    spectrum.lineTo (xAt (f), yAt (-4.0 + 0.62 * std::max (-60.0, static_cast<double> (bins[b]))));
                }
                spectrum.lineTo (area.getRight(), area.getBottom());
                spectrum.closeSubPath();
                juce::ColourGradient fill (amber.withAlpha (0.28f), area.getX(), 0.0f, mineral.withAlpha (0.24f), area.getRight(), 0.0f, false);
                fill.addColour (0.5, rose.withAlpha (0.26f));
                g.setGradientFill (fill);
                g.fillPath (spectrum);
            }

            auto curve = [&] (double cutoff) {
                juce::Path p;
                for (int i = 0; i <= 220; ++i)
                {
                    const double f = 20.0 * std::pow (1000.0, i / 220.0);
                    const float x = xAt (f), y = yAt (responseDb (type, f, cutoff, resonance, tilt));
                    if (i == 0)
                        p.startNewSubPath (x, y);
                    else
                        p.lineTo (x, y);
                }
                return p;
            };
            // The envelope's peak, as a ghost (where a note's filter sweeps from).
            if (std::abs (env) > 0.01 && type != FilterType::tilt)
            {
                const double peak = std::clamp (fc * std::exp2 (shaping::envelopeOctaves (env)), 20.0, 20000.0);
                juce::Path ghost = curve (peak), dashed;
                const float dashes[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, ghost, dashes, 2);
                g.setColour (raised.withAlpha (0.35f));
                g.fillPath (dashed);
            }
            const auto response = curve (fc);
            juce::Path under (response);
            under.lineTo (area.getRight(), area.getBottom());
            under.lineTo (area.getX(), area.getBottom());
            under.closeSubPath();
            g.setColour (amber.withAlpha (0.08f));
            g.fillPath (under);
            g.setColour (amber.brighter (0.2f));
            g.strokePath (response, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Cutoff (TILT: the pivot) and its value.
            const double markerHz = type == FilterType::tilt ? 800.0 : fc;
            const float mx = xAt (markerHz), my = yAt (responseDb (type, markerHz, fc, resonance, tilt));
            g.setColour (raised);
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
            g.setColour (accent);
            g.drawEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f, 1.5f);
            g.setColour (raised.withAlpha (0.9f));
            g.setFont (fonts::make (10.0f, fonts::Weight::medium));
            const auto text = type == FilterType::tilt ? juce::String (tilt, 1) + " dB" : format::hertz (fc);
            const bool leftOf = mx > area.getRight() - 60.0f;
            g.drawText (text, juce::Rectangle<float> (leftOf ? mx - 62.0f : mx + 8.0f, std::max (area.getY(), my - 18.0f), 54.0f, 12.0f),
                        leftOf ? juce::Justification::centredRight : juce::Justification::centredLeft, false);
        }

        OspAudioProcessor& processor;
        Watch watch;
        int shownType = -1;
    };

    //==========================================================================
    // MOVEMENT: what each mode does over time, from the bus' own formulas (SHAPER: the
    // pattern and the DSP's own playhead).
    class MovementVisual final : public Visual
    {
    public:
        explicit MovementVisual (OspAudioProcessor& p) : processor (p) {}

    private:
        bool animates() const override { return true; }
        bool changed() override
        {
            const int mode = juce::roundToInt (value (processor.parameters, "movement.mode"));
            if (mode != shownMode && shownMode >= 0)
                transition();
            shownMode = mode;
            return false;
        }

        void trace (juce::Graphics& g, juce::Rectangle<float> area, double window, const std::function<double (double)>& f,
                    juce::Colour colour, float thickness, float centre, float extent) const
        {
            // f(t) in -1..1 over the last `window` seconds, newest on the right.
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            juce::Path p;
            const int steps = std::max (40, juce::roundToInt (area.getWidth() / 2.0f));
            for (int i = 0; i <= steps; ++i)
            {
                const double t = now - window * (1.0 - static_cast<double> (i) / steps);
                const float x = area.getX() + area.getWidth() * static_cast<float> (i) / static_cast<float> (steps);
                const float y = centre - static_cast<float> (std::clamp (f (t), -1.2, 1.2)) * extent;
                if (i == 0)
                    p.startNewSubPath (x, y);
                else
                    p.lineTo (x, y);
            }
            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void caption (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text, juce::Colour colour, float y) const
        {
            g.setColour (colour);
            g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.08f));
            g.drawText (text, juce::Rectangle<float> (area.getX(), y, 120.0f, 11.0f), juce::Justification::centredLeft, false);
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            using namespace palette;
            auto& s = processor.parameters;
            const auto mode = static_cast<MovementMode> (std::clamp (juce::roundToInt (value (s, "movement.mode")), 0, 4));
            const double amount = std::clamp (0.01 * value (s, "motion"), 0.0, 1.0);
            const float depth = static_cast<float> (0.25 + 0.75 * amount);   // the knob's amount, still visible at 0
            const float mid = area.getCentreY(), half = area.getHeight() * 0.42f;
            g.setColour (displayLine);
            if (mode != MovementMode::shaper)
                g.drawHorizontalLine (juce::roundToInt (mid), area.getX(), area.getRight());
            switch (mode)
            {
                case MovementMode::drift:
                {
                    // Slow, organic wander (the voices' shared value noise): pitch, tone, level.
                    const double rate = shaping::driftSpeedHz (0.01 * value (s, "movement.drift.speed"));
                    // (Square roots: small settings still show.)
                    const double pitch = std::sqrt (std::clamp (0.01 * value (s, "movement.drift.pitch"), 0.02, 1.0));
                    const double tone = std::sqrt (std::clamp (0.01 * value (s, "movement.drift.tone"), 0.02, 1.0));
                    const double window = std::clamp (6.0 / rate, 6.0, 60.0);
                    trace (g, area, window, [rate] (double t) { return shaping::sharedWander (11, t, rate * 1.3) * 0.6; }, mineral.withAlpha (0.45f), 1.0f, mid, half * depth);
                    trace (g, area, window, [rate, tone] (double t) { return tone * shaping::sharedWander (7, t, rate * 0.8); }, lavender.withAlpha (0.75f), 1.4f, mid, half * depth);
                    trace (g, area, window, [rate, pitch] (double t) { return pitch * shaping::sharedWander (3, t, rate); }, amber, 2.0f, mid, half * depth);
                    caption (g, area, "PITCH", amber, area.getY());
                    caption (g, area, "TONE", lavender, area.getY() + 11.0f);
                    break;
                }
                case MovementMode::tape:
                {
                    // Wow (slow transport) above, flutter (fast capstan) below; WEAR roughens both.
                    const double a = 0.01 * value (s, "movement.tape.wow"), b = 0.01 * value (s, "movement.tape.flutter");
                    const double wear = 0.01 * value (s, "movement.tape.wear");
                    const double wowHz = shaping::tapeWowHz (a), flutterHz = shaping::tapeFlutterHz (b);
                    auto rough = [wear] (double t) { return wear * 0.25 * shaping::sharedWander (19, t, 9.0); };
                    trace (g, area, 4.0, [wowHz, a, rough] (double t) { return (0.7 * std::sin (twoPi * wowHz * t) + 0.5 * shaping::sharedWander (5, t, 0.3)) * (0.3 + 0.7 * a) * 0.8 + rough (t); },
                           coral, 2.0f, area.getY() + area.getHeight() * 0.3f, area.getHeight() * 0.24f * depth);
                    trace (g, area, 1.0, [flutterHz, b, rough] (double t) { return std::sin (twoPi * flutterHz * t) * (0.7 + 0.3 * shaping::sharedWander (9, t, 4.0)) * (0.2 + 0.8 * b) + rough (t); },
                           mineral, 1.2f, area.getY() + area.getHeight() * 0.76f, area.getHeight() * 0.18f * depth);
                    caption (g, area, "WOW  " + format::hertz (wowHz), coral, area.getY());
                    caption (g, area, "FLUTTER  " + format::hertz (flutterHz), mineral, area.getY() + area.getHeight() * 0.5f);
                    break;
                }
                case MovementMode::chorus:
                {
                    // Two delay taps moving in phase-offset sines: STEREO pulls them apart.
                    const double rate = shaping::chorusRateHz (0.01 * value (s, "movement.chorus.rate"));
                    const double width = std::log2 (shaping::chorusWidthMs (0.01 * value (s, "movement.chorus.width")) / 0.2) / std::log2 (90.0);
                    const double stereo = 0.01 * value (s, "movement.chorus.stereo");
                    const double window = 2.5 / rate;
                    const float extent = half * static_cast<float> (0.25 + 0.75 * width) * depth;
                    trace (g, area, window, [rate, stereo] (double t) { return std::sin (twoPi * rate * t + std::numbers::pi * stereo); }, lavender, 1.6f, mid, extent);
                    trace (g, area, window, [rate] (double t) { return std::sin (twoPi * rate * t); }, coral, 1.8f, mid, extent);
                    caption (g, area, "L", coral, area.getY());
                    caption (g, area, "R", lavender, area.getY() + 11.0f);
                    break;
                }
                case MovementMode::pulse:
                {
                    // The gain the bus applies: sine to rounded square by SHAPE, MOVEMENT's depth.
                    const double rate = shaping::pulseRateHz (0.01 * value (s, "movement.pulse.rate"));
                    const double k = 1.0 + 7.0 * 0.01 * value (s, "movement.pulse.shape");
                    const double stereo = 0.01 * value (s, "movement.pulse.stereo");
                    const double norm = std::tanh (k), window = 3.0 / rate;
                    auto gain = [k, norm, amount] (double phase) {
                        const double y = std::tanh (k * std::sin (phase)) / norm;
                        return 1.0 - std::max (0.35, amount) * 0.5 * (1.0 - y);
                    };
                    const float bottom = area.getBottom(), height = area.getHeight() * 0.9f;
                    auto toTrace = [&] (double offset) {
                        return [=] (double t) { return 2.0 * gain (twoPi * rate * t + offset) - 1.0; };
                    };
                    if (stereo > 0.02)
                        trace (g, area, window, toTrace (std::numbers::pi * stereo), lavender.withAlpha (0.7f), 1.3f, bottom - 0.5f * height, 0.5f * height);
                    trace (g, area, window, toTrace (0.0), coral, 2.0f, bottom - 0.5f * height, 0.5f * height);
                    caption (g, area, format::hertz (rate), coral, area.getY());
                    break;
                }
                case MovementMode::shaper:
                {
                    // The pattern's contour (with SMOOTH), the step grid, and the DSP's own
                    // playhead (host-synchronised) with the step it is in.
                    const int pattern = juce::roundToInt (value (s, "movement.shaper.pattern"));
                    const double smooth = 0.01 * value (s, "movement.shaper.smooth");
                    // DEPTH (the MOVEMENT macro): how far the level travels - the pattern's
                    // full range as a ghost, what DEPTH makes of it in front.
                    const float depth = static_cast<float> (std::clamp (0.01 * value (s, "motion"), 0.0, 1.0));
                    const auto span = RhythmicShaper::span (pattern, smooth);
                    const float phase = processor.shaperPhase();
                    const float cell = area.getWidth() / RhythmicShaper::steps;
                    const int current = phase >= 0.0f ? std::min (RhythmicShaper::steps - 1, static_cast<int> (phase * RhythmicShaper::steps)) : -1;
                    for (int i = 0; i < RhythmicShaper::steps; ++i)
                    {
                        const auto c = juce::Rectangle<float> (area.getX() + static_cast<float> (i) * cell, area.getY(), cell, area.getHeight());
                        if (i == current)
                        {
                            g.setColour (accent.withAlpha (0.12f));
                            g.fillRect (c);
                        }
                        if (i > 0)
                        {
                            g.setColour (displayLine.withAlpha (i % 4 == 0 ? 1.0f : 0.5f));
                            g.drawVerticalLine (juce::roundToInt (c.getX()), area.getY(), area.getBottom());
                        }
                    }
                    juce::Path contour, ghost;
                    contour.startNewSubPath (area.getX(), area.getBottom());
                    for (int i = 0; i <= 240; ++i)
                    {
                        const double x = static_cast<double> (i) / 240.0;
                        const float v = RhythmicShaper::evaluate (pattern, std::min (x, 0.99999), smooth, span);
                        const float px = area.getX() + static_cast<float> (x) * area.getWidth();
                        const float level = 1.0f - depth * (1.0f - v);
                        contour.lineTo (px, area.getBottom() - level * area.getHeight());
                        if (i == 0)
                            ghost.startNewSubPath (px, area.getBottom() - v * area.getHeight());
                        else
                            ghost.lineTo (px, area.getBottom() - v * area.getHeight());
                    }
                    contour.lineTo (area.getRight(), area.getBottom());
                    contour.closeSubPath();
                    g.setColour (coral.withAlpha (0.28f));
                    g.strokePath (ghost, juce::PathStrokeType (1.0f));
                    g.setGradientFill (juce::ColourGradient (coral.withAlpha (0.65f), 0.0f, area.getY(), amber.withAlpha (0.2f), 0.0f, area.getBottom(), false));
                    g.fillPath (contour);
                    g.setColour (coral.brighter (0.3f));
                    g.strokePath (contour, juce::PathStrokeType (1.2f));
                    if (phase >= 0.0f)
                    {
                        const float x = area.getX() + phase * area.getWidth();
                        g.setColour (accent);
                        g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY(), 1.5f, area.getHeight()));
                    }
                    break;
                }
            }
        }

        OspAudioProcessor& processor;
        int shownMode = -1;
    };

    //==========================================================================
    // LIFE: a cloud of possible performances - the same note's contour as it might be played
    // again. LIFE spreads them (pitch: height, tone: colour, attack: onset shape); the
    // newest note's draw is the strong one and changes with every note played.
    class LifeVisual final : public Visual
    {
    public:
        explicit LifeVisual (OspAudioProcessor& p) : processor (p) {}

    private:
        bool changed() override
        {
            auto& s = processor.parameters;
            const int mode = juce::roundToInt (value (s, "life.mode"));
            if (mode != shownMode && shownMode >= 0)
                transition();
            shownMode = mode;
            return watch.differs ({ static_cast<double> (mode), value (s, "life"), value (s, "life.pitch"), value (s, "life.tone"), value (s, "life.attack"),
                                    static_cast<double> (processor.velocityCount.load()) });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            using namespace palette;
            auto& s = processor.parameters;
            const double life = std::clamp (0.01 * value (s, "life"), 0.0, 1.0);
            const auto mode = static_cast<LifeMode> (std::clamp (juce::roundToInt (value (s, "life.mode")), 0, 2));
            const double modeScale = mode == LifeMode::loose ? 1.5 : (mode == LifeMode::fray ? 1.2 : 1.0);
            const double pitch = value (s, "life.pitch") / 15.0, tone = 0.01 * value (s, "life.tone"), attack = 0.01 * value (s, "life.attack");
            const auto notes = static_cast<std::uint64_t> (processor.velocityCount.load());
            const int contours = 7;
            for (int c = contours - 1; c >= 0; --c)
            {
                const bool current = c == 0;
                Prng rng (Prng::deriveSeed (0x6c696665ull, notes + static_cast<std::uint64_t> (c), static_cast<std::uint64_t> (c)));
                auto gauss = [&rng] { return std::clamp (rng.gaussian(), -2.2, 2.2); };
                const double spread = life * modeScale;
                // Pitch: height; attack: how the onset rises; tone: colour; and a little of
                // everything in the body, as real repeated notes do.
                double offset = spread * (0.1 + 0.6 * pitch) * 0.42 * gauss();
                if (mode == LifeMode::fray && c == 3 && rng.nextDouble() < 0.6 * life)
                    offset += (rng.nextDouble() < 0.5 ? -1.0 : 1.0) * 0.28 * life;   // a frayed take
                const double attackShare = std::clamp (0.07 * std::exp2 (spread * (0.3 + attack) * 1.6 * gauss()), 0.012, 0.45);
                const double body = std::clamp (0.6 + 0.16 * spread * gauss(), 0.25, 0.92);
                const double sag = 0.12 + 0.1 * spread * gauss();
                const double colourAt = std::clamp (0.3 + 0.45 * spread * (0.2 + tone) * gauss(), 0.0, 1.0);
                juce::Path contour;
                const float base = area.getBottom() - static_cast<float> (offset) * area.getHeight();
                const float height = area.getHeight() * 0.78f;
                for (int i = 0; i <= 120; ++i)
                {
                    const double x = i / 120.0;
                    double y;
                    if (x < attackShare)
                        y = std::pow (x / attackShare, 0.6);
                    else
                        y = (body + (1.0 - body) * std::exp (-(x - attackShare) * 9.0)) * (1.0 - sag * (x - attackShare));
                    y *= x > 0.82 ? std::max (0.0, 1.0 - (x - 0.82) / 0.18) : 1.0;
                    const float px = area.getX() + static_cast<float> (x) * area.getWidth();
                    const float py = base - static_cast<float> (y) * height;
                    if (i == 0)
                        contour.startNewSubPath (px, py);
                    else
                        contour.lineTo (px, py);
                }
                if (current)
                {
                    juce::Path fill (contour);
                    fill.lineTo (area.getRight(), base);
                    fill.lineTo (area.getX(), base);
                    fill.closeSubPath();
                    const auto colour = spectrum (static_cast<float> (colourAt) * 0.6f);
                    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0.0f, base - height, colour.withAlpha (0.02f), 0.0f, base, false));
                    g.fillPath (fill);
                    g.setColour (colour.brighter (0.2f));
                    g.strokePath (contour, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                {
                    const auto colour = lavender.interpolatedWith (mineral, static_cast<float> (colourAt)).withMultipliedSaturation (0.6f);
                    g.setColour (colour.withAlpha (0.22f + 0.06f * static_cast<float> (contours - c)));
                    g.strokePath (contour, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
            g.setColour (displayText.withAlpha (0.8f));
            g.setFont (fonts::make (9.0f, fonts::Weight::medium, 0.08f));
            g.drawText (life < 0.01 ? "EVERY NOTE THE SAME" : "LIFE " + juce::String (juce::roundToInt (100.0 * life)) + " %",
                        area.withHeight (11.0f), juce::Justification::centredRight, false);
        }

        OspAudioProcessor& processor;
        Watch watch;
        int shownMode = -1;
    };

    //==========================================================================
    // DYNAMICS: touch -> level, through the velocity curve and RANGE (scaled by the macro as
    // the engine does), a brightness halo where TONE opens the sound, and the notes just played.
    class DynamicsVisual final : public Visual
    {
    public:
        explicit DynamicsVisual (OspAudioProcessor& p) : processor (p) {}

    private:
        bool changed() override
        {
            auto& s = processor.parameters;
            return watch.differs ({ value (s, "dynamics.curve"), value (s, "velocityRange"), value (s, "dynamics.tone"), value (s, "dynamics"),
                                    static_cast<double> (processor.velocityCount.load()) });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> plot) override
        {
            using namespace palette;
            auto& s = processor.parameters;
            const auto curve = static_cast<VelocityCurve> (std::clamp (juce::roundToInt (value (s, "dynamics.curve")), 0, 2));
            const double macro = std::clamp (0.01 * value (s, "dynamics"), 0.0, 1.0);
            const double range = value (s, "velocityRange") * (0.15 + 1.31 * macro);   // as InstrumentEngine::levelRangeDb
            const double tone = 0.01 * value (s, "dynamics.tone") * macro;
            auto area = plot.withTrimmedBottom (11.0f).withTrimmedLeft (22.0f);
            constexpr double floorDb = -48.0;
            auto xAt = [&] (double v) { return area.getX() + static_cast<float> ((v - 1.0) / 126.0) * area.getWidth(); };
            auto yAt = [&] (double db) { return area.getY() + static_cast<float> (std::clamp (db, floorDb, 0.0) / floorDb) * area.getHeight(); };
            auto levelDb = [&] (int v) { return -range * (1.0 - shaping::curvedVelocity (curve, v) / 127.0); };

            g.setFont (fonts::make (9.0f));
            for (double db : { 0.0, -24.0, -48.0 })
            {
                g.setColour (displayLine.withAlpha (0.6f));
                g.drawHorizontalLine (juce::roundToInt (yAt (db)), area.getX(), area.getRight());
                g.setColour (displayText.withAlpha (0.75f));
                g.drawText (juce::String (juce::roundToInt (db)), juce::Rectangle<float> (plot.getX(), yAt (db) - 5.0f, 20.0f, 10.0f), juce::Justification::centredLeft, false);
            }
            g.setColour (displayText.withAlpha (0.8f));
            g.drawText ("SOFT", juce::Rectangle<float> (area.getX(), plot.getBottom() - 10.0f, 40.0f, 10.0f), juce::Justification::centredLeft, false);
            g.drawText ("HARD", juce::Rectangle<float> (area.getRight() - 40.0f, plot.getBottom() - 10.0f, 40.0f, 10.0f), juce::Justification::centredRight, false);

            // The neutral (linear) response, faintly.
            juce::Path neutral, response;
            for (int v = 1; v <= 127; ++v)
            {
                const auto x = xAt (v);
                const float yn = yAt (-range * (1.0 - v / 127.0)), yc = yAt (levelDb (v));
                if (v == 1)
                {
                    neutral.startNewSubPath (x, yn);
                    response.startNewSubPath (x, yc);
                }
                else
                {
                    neutral.lineTo (x, yn);
                    response.lineTo (x, yc);
                }
            }
            juce::Path dashed;
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, neutral, dashes, 2);
            g.setColour (displayText.withAlpha (0.5f));
            g.fillPath (dashed);

            // Brightness: TONE opens harder notes - a halo growing towards the hard end.
            if (tone > 0.01)
                for (int v = 64; v <= 127; v += 3)
                {
                    const float strength = static_cast<float> (tone * std::pow ((v - 64) / 63.0, 1.5));
                    const float r = 3.0f + 14.0f * strength;
                    g.setColour (amber.withAlpha (0.08f * strength + 0.02f));
                    g.fillEllipse (xAt (v) - r, yAt (levelDb (v)) - r, 2.0f * r, 2.0f * r);
                }
            juce::Path fill (response);
            fill.lineTo (area.getRight(), area.getBottom());
            fill.lineTo (area.getX(), area.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (coral.withAlpha (0.22f), 0.0f, area.getY(), mineral.withAlpha (0.04f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            g.setColour (coral.brighter (0.25f));
            g.strokePath (response, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // The notes just played, newest brightest.
            const int count = processor.velocityCount.load (std::memory_order_acquire);
            const int shown = std::min (count, OspAudioProcessor::velocityHistory);
            for (int i = 0; i < shown; ++i)
            {
                const int index = count - 1 - i;
                const int v = processor.recentVelocity[static_cast<std::size_t> (index % OspAudioProcessor::velocityHistory)].load (std::memory_order_relaxed);
                const float age = static_cast<float> (i) / static_cast<float> (OspAudioProcessor::velocityHistory);
                g.setColour (raised.withAlpha (0.85f * (1.0f - age)));
                g.fillEllipse (xAt (v) - 3.0f, yAt (levelDb (v)) - 3.0f, 6.0f, 6.0f);
            }
        }

        OspAudioProcessor& processor;
        Watch watch;
    };

    //==========================================================================
    /** A macro popup: title and question, mode tabs, the visualisation, compact controls. */
    class MacroPanel : public MiniPanel
    {
    public:
        MacroPanel (OspAudioProcessor& p, juce::String popupTitle, juce::String question, int width = 360)
            : MiniPanel (std::move (popupTitle), std::move (question)), processor (p), panelWidth (width)
        {
        }

        SegmentedControl& tabs (const char* id, juce::StringArray items, juce::String tooltip)
        {
            modeTabs = std::make_unique<SegmentedControl> (*processor.parameters.getParameter (id), std::move (items));
            modeTabs->setTooltip (tooltip);
            addAndMakeVisible (*modeTabs);
            return *modeTabs;
        }
        void setVisual (std::unique_ptr<Visual> v)
        {
            visual = std::move (v);
            addAndMakeVisible (*visual);
        }
        MiniKnob& knob (bool secondary, const char* id, const char* caption, MiniKnob::Formatter f, bool horizontal = false)
        {
            auto k = std::make_unique<MiniKnob> (processor.parameters, id, caption, std::move (f), horizontal);
            k->setSmall (secondary);
            addAndMakeVisible (*k);
            (secondary ? secondaryRow : primaryRow).push_back (k.get());
            knobs.push_back (std::move (k));
            return *knobs.back();
        }

        juce::Point<int> cardSize() const override
        {
            int h = 10 + headerHeight() + 6 + (modeTabs != nullptr ? 24 + 8 : 0) + visualHeight + 8 + primaryHeight() + 10;
            if (! secondaryRow.empty())
                h += 2 + 44;
            return { panelWidth, h };
        }

    protected:
        /** One control (SPACE's DECAY) sits on a single line; several are a row of knobs. */
        int primaryHeight() const { return primaryRow.size() == 1 ? 36 : 54; }

        void layoutContent (juce::Rectangle<int> area) override
        {
            if (modeTabs != nullptr)
            {
                modeTabs->setBounds (area.removeFromTop (24));
                area.removeFromTop (8);
            }
            if (visual != nullptr)
                visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (8);
            layoutRow (primaryRow, area.removeFromTop (primaryHeight()));
            if (! secondaryRow.empty())
            {
                area.removeFromTop (2);
                layoutRow (secondaryRow, area.removeFromTop (44));
            }
        }

        static void layoutRow (const std::vector<MiniKnob*>& row, juce::Rectangle<int> line)
        {
            if (row.empty())
                return;
            if (row.size() == 1)
            {
                row.front()->setBounds (line.withSizeKeepingCentre (std::min (line.getWidth(), 170), line.getHeight()));
                return;
            }
            const int cell = std::min (line.getWidth() / static_cast<int> (row.size()), 84);
            line = line.withSizeKeepingCentre (cell * static_cast<int> (row.size()), line.getHeight());
            for (auto* k : row)
                k->setBounds (line.removeFromLeft (cell));
        }

        OspAudioProcessor& processor;
        int panelWidth;
        int visualHeight = 116;
        std::unique_ptr<SegmentedControl> modeTabs;
        std::unique_ptr<Visual> visual;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::vector<MiniKnob*> primaryRow, secondaryRow;
    };

    //==========================================================================
    /** MOVEMENT: the mode, its picture, then that mode's own controls (SHAPER: PATTERN, RATE,
        TARGET, SMOOTH). Switching modes keeps every mode's values. */
    class MovementPanel final : public MacroPanel
    {
    public:
        explicit MovementPanel (OspAudioProcessor& p) : MacroPanel (p, "MOVEMENT", "HOW THE SOUND CHANGES THROUGH TIME", 370)
        {
            auto& t = tabs ("movement.mode", { "DRIFT", "TAPE", "CHORUS", "PULSE", "SHAPER" }, "Movement: drift, tape, chorus, pulse or rhythmic shaper");
            t.onChange = [this] (int mode) {
                build (mode);
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            };
            setVisual (std::make_unique<MovementVisual> (p));
            visualHeight = 104;
            build (t.selected());
        }

        juce::Point<int> cardSize() const override
        {
            const int controls = shaperMode ? 32 + 6 + 36 + 6 + 32 : 54;
            return { panelWidth, 10 + headerHeight() + 6 + 24 + 8 + visualHeight + 8 + controls + 10 };
        }

    private:
        void build (int mode)
        {
            knobs.clear();
            primaryRow.clear();
            patternSelector.reset();
            rateSelector.reset();
            target.reset();
            shaperMode = mode == static_cast<int> (MovementMode::shaper);
            auto& state = processor.parameters;
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            switch (static_cast<MovementMode> (mode))
            {
                case MovementMode::drift:
                    knob (false, "movement.drift.speed", "SPEED", [] (double v) { return format::hertz (shaping::driftSpeedHz (v * 0.01)); });
                    knob (false, "movement.drift.pitch", "PITCH", [] (double v) { return juce::String (shaping::driftPitchCents (v * 0.01), 1) + " c"; });
                    knob (false, "movement.drift.tone", "TONE", percent);
                    break;
                case MovementMode::tape:
                    knob (false, "movement.tape.wow", "WOW", [] (double v) { return format::hertz (shaping::tapeWowHz (v * 0.01)); });
                    knob (false, "movement.tape.flutter", "FLUTTER", [] (double v) { return format::hertz (shaping::tapeFlutterHz (v * 0.01)); });
                    knob (false, "movement.tape.wear", "WEAR", percent);
                    break;
                case MovementMode::chorus:
                    knob (false, "movement.chorus.rate", "RATE", [] (double v) { return format::hertz (shaping::chorusRateHz (v * 0.01)); });
                    knob (false, "movement.chorus.width", "WIDTH", [] (double v) { return format::milliseconds (shaping::chorusWidthMs (v * 0.01)); });
                    knob (false, "movement.chorus.stereo", "STEREO", percent);
                    break;
                case MovementMode::pulse:
                    knob (false, "movement.pulse.rate", "RATE", [] (double v) { return format::hertz (shaping::pulseRateHz (v * 0.01)); });
                    knob (false, "movement.pulse.shape", "SHAPE", percent);
                    knob (false, "movement.pulse.stereo", "STEREO", percent);
                    break;
                case MovementMode::shaper:
                    patternSelector = std::make_unique<ValueSelector> (*state.getParameter ("movement.shaper.pattern"), "PATTERN");
                    rateSelector = std::make_unique<ValueSelector> (*state.getParameter ("movement.shaper.rate"), "RATE");
                    addAndMakeVisible (*patternSelector);
                    addAndMakeVisible (*rateSelector);
                    target = std::make_unique<SegmentedControl> (*state.getParameter ("movement.shaper.target"), juce::StringArray { "VOL", "FILTER", "BOTH" });
                    target->setTooltip ("What the pattern shapes: the volume, a low-pass filter, or both");
                    addAndMakeVisible (*target);
                    // DEPTH is the MOVEMENT macro itself (one control, two places): 100 % takes
                    // the pattern's lowest point to closed - on VOL, silence - 10 % only breathes.
                    knob (false, "motion", "DEPTH", percent, true);
                    knob (false, "movement.shaper.smooth", "SMOOTH", percent, true);
                    break;
            }
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            modeTabs->setBounds (area.removeFromTop (24));
            area.removeFromTop (8);
            visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (8);
            if (! shaperMode)
            {
                layoutRow (primaryRow, area.removeFromTop (54));
                return;
            }
            auto row = area.removeFromTop (32);
            const int half = row.getWidth() / 2;
            if (patternSelector != nullptr)
                patternSelector->setBounds (row.removeFromLeft (half).reduced (2, 0));
            if (rateSelector != nullptr)
                rateSelector->setBounds (row.reduced (2, 0));
            area.removeFromTop (6);
            targetCaption = area.removeFromTop (12);
            if (target != nullptr)
                target->setBounds (area.removeFromTop (24).reduced (2, 0));
            area.removeFromTop (6);
            auto last = area.removeFromTop (32);
            const int each = last.getWidth() / std::max (1, static_cast<int> (knobs.size()));
            for (auto& k : knobs)
                k->setBounds (last.removeFromLeft (each).withSizeKeepingCentre (std::min (each, 170), 32));
        }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            if (shaperMode)
            {
                g.setColour (palette::textDim);
                g.setFont (fonts::label (9.5f));
                g.drawText ("TARGET", targetCaption, juce::Justification::centred, false);
            }
        }

        std::unique_ptr<SegmentedControl> target;
        std::unique_ptr<ValueSelector> patternSelector, rateSelector;
        juce::Rectangle<int> targetCaption;
        bool shaperMode = false;
    };
}

std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, OspAudioProcessor& processor)
{
    auto& state = processor.parameters;
    auto ms = [] (double v) { return format::milliseconds (v); };
    auto percent = [] (double v) { return format::percent (v) + " %"; };
    switch (macro)
    {
        case MacroPopup::life:
        {
            auto popup = std::make_unique<MacroPanel> (processor, "LIFE", "HOW DIFFERENTLY EACH NOTE IS PERFORMED");
            popup->tabs ("life.mode", { "NATURAL", "LOOSE", "FRAY" }, "Natural: subtle; Loose: wider; Fray: now and then a note strays");
            popup->setVisual (std::make_unique<LifeVisual> (processor));
            popup->knob (false, "life.pitch", "PITCH", [] (double v) { return juce::String (v, 1) + " c"; });
            popup->knob (false, "life.tone", "TONE", percent);
            popup->knob (false, "life.attack", "ATTACK", percent);
            return popup;
        }
        case MacroPopup::dynamics:
        {
            // ATTACK and RELEASE are the instrument's envelope (beside the macros).
            auto popup = std::make_unique<MacroPanel> (processor, "DYNAMICS", "HOW TOUCH CHANGES THE SOUND");
            popup->tabs ("dynamics.curve", { "SOFT", "LINEAR", "HARD" }, "Velocity curve: soft reaches loud easily, hard needs a firm touch");
            popup->setVisual (std::make_unique<DynamicsVisual> (processor));
            popup->knob (false, "velocityRange", "RANGE", [] (double v) { return juce::String (v, 0) + " dB"; });
            popup->knob (false, "dynamics.tone", "TONE", percent);
            return popup;
        }
        case MacroPopup::character:
        {
            auto popup = std::make_unique<MacroPanel> (processor, "CHARACTER", "THE TONAL SHAPE IMPOSED ON THE SOUND", 380);
            popup->tabs ("character.type", { "LP24", "LP12", "HP12", "BP12", "TILT" }, "Filter type");
            popup->setVisual (std::make_unique<CharacterVisual> (processor));
            popup->knob (false, "character.min", "MIN", [] (double v) { return format::hertz (v); });
            popup->knob (false, "character.max", "MAX", [] (double v) { return format::hertz (v); });
            popup->knob (false, "character.resonance", "RES", percent);
            popup->knob (true, "character.drive", "DRIVE", percent);
            popup->knob (true, "character.envAmount", "ENV", [] (double v) { return format::bipolar (v); });
            popup->knob (true, "character.envAttack", "ATTACK", ms);
            popup->knob (true, "character.envDecay", "DECAY", ms);
            return popup;
        }
        case MacroPopup::movement:
            return std::make_unique<MovementPanel> (processor);
        case MacroPopup::space:
            break;
    }

    auto popup = std::make_unique<MacroPanel> (processor, "SPACE", "THE ROOM THE SOUND PLAYS IN");
    auto& types = popup->tabs ("space.type", { "ROOM", "CHAMBER", "PLATE", "SPRING" }, "Room, chamber, plate or spring");
    popup->setVisual (std::make_unique<SpaceVisual> (state));
    auto* typeParam = state.getParameter ("space.type");
    // The readout shows the decay the chosen type actually uses (each type has its own range).
    auto& decay = popup->knob (false, "space.decay", "DECAY", [typeParam] (double v) {
        double lo = 0.2, hi = 8.0;
        if (typeParam != nullptr)
            shaping::decayRange (static_cast<SpaceType> (juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue()))), lo, hi);
        return juce::String (std::clamp (v, lo, hi), 1) + " s";
    }, true);
    types.onChange = [&decay] (int) { decay.repaint(); };
    return popup;
}

} // namespace osp::plugin
