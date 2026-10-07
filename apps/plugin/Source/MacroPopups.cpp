// The five macro popups (redesign §2): each one explains its DSP with a picture drawn
// from the same formulas and parameters the sound uses, plus a few compact controls.
#include "EngineCard.h"
#include "Design.h"
#include "ShapingPopups.h"

#include "core/Prng.h"
#include "engine/InstrumentEngine.h"
#include "engine/RhythmicShaper.h"
#include "engine/Shaping.h"
#include "engine/EchoDelay.h"
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
            // The reference's display (SPACE: 558 x 261): a deep slate well, rounded 9 px,
            // shaded under its top edge, a faint light line along its foot.
            const auto r = getLocalBounds().toFloat();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2626), 0.0f, r.getY(), juce::Colour (0xff1a1e1f), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, radius);
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f,
                                                     r.getY() + 6.0f, false));
            g.fillRoundedRectangle (r.withHeight (8.0f), radius);
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (getLocalBounds().reduced (1));
                if (native())
                    paintVisual (g, r);
                else
                {
                    // Pictures designed for the smaller display draw at their own scale.
                    const float k = visualScale;
                    g.addTransform (juce::AffineTransform::scale (k));
                    const auto v = juce::Rectangle<float> (r.getWidth() / k, r.getHeight() / k);
                    paintVisual (g, v.reduced (8.0f, 6.0f));
                }
            }
            g.setColour (juce::Colour (0xff0b0e10));
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            if (fade > 0.0f && previous.isValid())
            {
                g.setOpacity (fade);
                g.drawImage (previous, r);
            }
        }

        // Pictures designed for a larger display draw at this scale in the popovers.
        static constexpr float visualScale = 0.9f, radius = 6.0f;

        /** The macro's identity: its picture's main line, node and highlight (lifted for the
            graphite well); everything else stays as it was. */
        void setIdentity (design::colour::Macro m)
        {
            identity = design::colour::macroOnDark (m);
            repaint();
        }
        void setIdentityColour (juce::Colour c)
        {
            identity = c;
            repaint();
        }

    protected:
        juce::Colour identity { 0xffe8692a };
        virtual void paintVisual (juce::Graphics&, juce::Rectangle<float> plot) = 0;
        /** Draws at the display's full size (otherwise scaled by visualScale). */
        virtual bool native() const { return false; }
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
    class SpaceVisual final : public Visual, public juce::SettableTooltipClient
    {
    public:
        explicit SpaceVisual (juce::AudioProcessorValueTreeState& s) : state (s)
        {
            // The EQ's two corners are handles (drag them; double-click resets).
            setInterceptsMouseClicks (true, false);
            setTooltip ("The room's EQ: drag LOW CUT and HIGH CUT along the curve (double-click resets)");
        }

        void mouseMove (const juce::MouseEvent& e) override { setHover (handleAt (e.position)); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = handleAt (e.position);
            if (auto* p = handleParameter (dragging))
                p->beginChangeGesture();
            mouseDrag (e);
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (auto* p = handleParameter (dragging))
            {
                const double hz = std::clamp (hzAt (e.position.x), dragging == 0 ? 20.0 : 1000.0, dragging == 0 ? 2000.0 : 20000.0);
                p->setValueNotifyingHost (p->convertTo0to1 (static_cast<float> (hz)));
                repaint();
            }
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (auto* p = handleParameter (dragging))
                p->endChangeGesture();
            dragging = -1;
        }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (auto* p = handleParameter (handleAt (e.position)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->getDefaultValue());
                p->endChangeGesture();
                repaint();
            }
        }

    private:
        juce::RangedAudioParameter* handleParameter (int handle) const
        {
            return handle == 0 ? state.getParameter ("space.lowCut") : (handle == 1 ? state.getParameter ("space.highCut") : nullptr);
        }
        void setHover (int h)
        {
            if (h != hover)
            {
                hover = h;
                repaint();
            }
        }
        // The EQ's frequency axis: 20 Hz .. 20 kHz across the display, log.
        juce::Rectangle<float> eqArea() const { return getLocalBounds().toFloat().reduced (10.0f, 6.0f).withTrimmedBottom (10.0f); }
        float xAtHz (double hz) const
        {
            const auto a = eqArea();
            return a.getX() + a.getWidth() * static_cast<float> (std::log (std::clamp (hz, 20.0, 20000.0) / 20.0) / std::log (1000.0));
        }
        double hzAt (float x) const
        {
            const auto a = eqArea();
            return 20.0 * std::pow (1000.0, std::clamp ((x - a.getX()) / a.getWidth(), 0.0f, 1.0f));
        }
        /** The input EQ's response in dB (two 12 dB/oct Butterworths), as SpaceReverb runs it. */
        static double responseDb (double hz, double low, double high)
        {
            const double hp = std::pow (hz / low, 4.0), lp = std::pow (high / hz, 4.0);
            return 10.0 * std::log10 ((hp / (1.0 + hp)) * (lp / (1.0 + lp)));
        }
        float yAtDb (double db) const
        {
            const auto a = eqArea();
            return a.getY() + 0.1f * a.getHeight() + static_cast<float> (std::clamp (-db / 30.0, 0.0, 1.0)) * 0.9f * a.getHeight();
        }
        int handleAt (juce::Point<float> p) const
        {
            const double low = value (state, "space.lowCut"), high = value (state, "space.highCut");
            const juce::Point<float> handles[] = { { xAtHz (low), yAtDb (-3.0) }, { xAtHz (high), yAtDb (-3.0) } };
            for (int i = 0; i < 2; ++i)
                if (p.getDistanceFrom (handles[i]) < 11.0f)
                    return i;
            // Elsewhere: the nearer corner along the axis (the whole display is the EQ).
            return std::abs (p.x - handles[0].x) < std::abs (p.x - handles[1].x) ? 0 : 1;
        }

        bool changed() override
        {
            const int type = juce::roundToInt (value (state, "space.type"));
            if (type != shownType && shownType >= 0)
                transition();
            shownType = type;
            const bool eqMoved = eqWatch.differs ({ value (state, "space.lowCut"), value (state, "space.highCut") });
            if (watch.differs ({ static_cast<double> (type), value (state, "space.decay"), value (state, "space"), value (state, "space.preDelay"),
                                 value (state, "space.size"), value (state, "space.damping"), value (state, "space.width"), static_cast<double> (getWidth()) }))
            {
                cacheValid = false;
                return true;
            }
            return eqMoved;
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
                draw (cg, plot);   // native: the display's full bounds
                cacheValid = true;
            }
            g.drawImage (cache, getLocalBounds().toFloat());
            drawEq (g);
        }

        /** The input EQ over the tail (as on a hardware return's display): the response as a
            line, what it removes veiled above it, the two corners as handles. */
        void drawEq (juce::Graphics& g)
        {
            const double low = value (state, "space.lowCut"), high = value (state, "space.highCut");
            const auto a = eqArea();
            juce::Path curve, veil;
            const int points = 96;
            for (int i = 0; i <= points; ++i)
            {
                const float x = a.getX() + a.getWidth() * static_cast<float> (i) / points;
                const float y = yAtDb (responseDb (hzAt (x), low, high));
                if (i == 0)
                {
                    curve.startNewSubPath (x, y);
                    veil.startNewSubPath (x, yAtDb (0.0));
                }
                curve.lineTo (x, y);
                veil.lineTo (x, y);
            }
            // What the EQ takes away: veiled between the curve and 0 dB (nothing where it passes).
            veil.lineTo (a.getRight(), yAtDb (0.0));
            veil.closeSubPath();
            g.setColour (juce::Colour (0xff12161a).withAlpha (0.45f));
            g.fillPath (veil);
            g.setColour (identity.withAlpha (0.9f));
            g.strokePath (curve, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont (fonts::make (8.5f, fonts::Weight::medium, 0.04f));
            const double cuts[] = { low, high };
            for (int i = 0; i < 2; ++i)
            {
                const juce::Point<float> c { xAtHz (cuts[i]), yAtDb (-3.0) };
                const bool lit = hover == i || dragging == i;
                g.setColour (juce::Colour (0xff1a1e1f));
                g.fillEllipse (juce::Rectangle<float> (lit ? 9.0f : 7.0f, lit ? 9.0f : 7.0f).withCentre (c));
                g.setColour (identity.withAlpha (lit ? 1.0f : 0.85f));
                g.drawEllipse (juce::Rectangle<float> (lit ? 9.0f : 7.0f, lit ? 9.0f : 7.0f).withCentre (c), 1.4f);
                const auto text = (i == 0 ? "LO " : "HI ") + format::hertz (cuts[i]);
                const auto box = juce::Rectangle<float> (64.0f, 11.0f).withCentre ({ juce::jlimit (a.getX() + 32.0f, a.getRight() - 32.0f, c.x), a.getY() + 5.0f });
                g.setColour (juce::Colour (0xffd9dcd8).withAlpha (lit ? 0.95f : 0.6f));
                g.drawText (lit ? juce::String (i == 0 ? "LOW CUT " : "HIGH CUT ") + format::hertz (cuts[i]) : text, box, juce::Justification::centred, false);
            }
        }

        bool native() const override { return true; }

        /** The reference's picture (SPACE, ROOM, 3.2 s): seconds along a faint grid; the
            sound's early reflections as glowing amber strokes; the dense tail as a cloud of
            particles narrowing to nothing at the right edge (-75 dB, 1.25 x DECAY) while its
            colour cools from amber through white to mineral blue. Times, counts and spread
            come from the type's design (SpaceReverb::portrait) and DECAY. */
        void draw (juce::Graphics& g, juce::Rectangle<float> bounds)
        {
            const auto type = static_cast<SpaceType> (std::clamp (juce::roundToInt (value (state, "space.type")), 0, 3));
            double lo = 0.2, hi = 8.0;
            shaping::decayRange (type, lo, hi);
            const double decay = std::clamp (static_cast<double> (value (state, "space.decay")), lo, hi);
            const double amount = 0.85 + 0.15 * std::clamp (static_cast<double> (value (state, "space")) * 0.01, 0.0, 1.0);
            SpaceReverb::Settings settings;
            settings.type = type;
            settings.decaySeconds = decay;
            settings.preDelayMs = value (state, "space.preDelay");
            settings.size = 0.01 * value (state, "space.size");
            settings.damping = 0.01 * value (state, "space.damping");
            settings.width = 0.01 * value (state, "space.width");
            const auto portrait = SpaceReverb::portrait (settings);
            // A fixed time axis for the type (its longest DECAY fills it); DECAY sets how far
            // the tail reaches along it, so turning the knob lengthens or shortens the tail.
            const double span = hi * 1.25, tailEnd = decay * 1.25;   // drawn to -75 dB
            const float W = bounds.getWidth(), H = bounds.getHeight();
            // Sizes follow the display (the popover's is small): strokes, dots and counts.
            const float k = juce::jlimit (0.3f, 1.0f, H / 261.0f), dot = std::sqrt (k);
            const auto plot = juce::Rectangle<float>::leftTopRightBottom (bounds.getX() + 14.0f, bounds.getY() + 5.0f,
                                                                          bounds.getRight() - 12.0f, bounds.getBottom() - 14.0f);
            const float mid = plot.getCentreY(), half = 0.5f * plot.getHeight();
            auto xAt = [&] (double t) { return plot.getX() + static_cast<float> (t / span) * plot.getWidth(); };

            // Grid: five rows, a line every second (or the step that fits) and four between.
            double step = 2.0;
            for (double c : { 0.1, 0.25, 0.5, 1.0, 2.0 })
                if (span / c <= 6.0)
                {
                    step = c;
                    break;
                }
            for (int i = 0; i <= 4; ++i)
            {
                g.setColour (juce::Colours::white.withAlpha (0.045f));
                g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY() + plot.getHeight() * static_cast<float> (i) / 4.0f - 0.5f, plot.getWidth(), 1.0f));
            }
            for (int i = 0; static_cast<double> (i) * step * 0.25 <= span + 1.0e-6; ++i)
            {
                const float x = xAt (static_cast<double> (i) * step * 0.25);
                g.setColour (juce::Colours::white.withAlpha (i % 4 == 0 ? 0.075f : 0.04f));
                g.fillRect (juce::Rectangle<float> (x - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            }
            g.setFont (fonts::make (9.0f, fonts::Weight::regular, 0.02f));
            g.setColour (juce::Colour (0xff9aa09e).withAlpha (0.8f));
            for (double t = 0.0; t <= span + 1.0e-6; t += step)
                g.drawText (secondsLabel (t), juce::Rectangle<float> (xAt (t) - 20.0f, bounds.getBottom() - 13.0f, 40.0f, 12.0f), juce::Justification::centred, false);

            // The tail's envelope (linear in dB: a straight fall to the floor at the edge),
            // building up as the diffusers fill in; springs ripple.
            const double onset = 0.001 * portrait.preMs;
            const double build = std::min (0.15 * tailEnd, std::max (0.01, 0.006 * portrait.diffusionMs * (type == SpaceType::hall ? 4.0 : 2.0)) * span / 4.0);
            auto tail = [&] (double t) {
                if (t <= onset)
                    return 0.0;
                // Drawn compressed (as the ear hears a tail): a fast early fall slowing towards the floor.
                double a = std::pow (std::max (0.0, 1.0 - t / tailEnd), 1.4) * (1.0 - std::exp (-(t - onset) / build)) * amount;
                if (portrait.spring)
                    a *= 0.8 + 0.2 * std::sin (twoPi * t / (0.03 * std::max (0.3, tailEnd)));
                return a;
            };
            // Warmth dissipating: amber, peach, white, then mineral blue; damped types cool sooner.
            auto tint = [&] (double t) {
                const float p = static_cast<float> (std::pow (std::clamp (t / tailEnd, 0.0, 1.0), 1.0 - 0.45 * portrait.damping));
                // Warm onset, rose, SPACE's mauve, then a cool blue-grey.
                static const juce::Colour stops[] = { juce::Colour (0xffff8a2c), juce::Colour (0xffffa24c), juce::Colour (0xfff5bfa0),
                                                      juce::Colour (0xffe9c8cf), juce::Colour (0xffcdbbd2), juce::Colour (0xffb2aac4), juce::Colour (0xff8fa4b8) };
                static const float at[] = { 0.0f, 0.1f, 0.25f, 0.45f, 0.65f, 0.82f, 1.0f };
                for (int i = 1; i < 7; ++i)
                    if (p <= at[i])
                        return stops[i - 1].interpolatedWith (stops[i], (p - at[i - 1]) / (at[i] - at[i - 1]));
                return stops[6];
            };
            const double spread = 0.8 + 0.2 * portrait.width;

            // The centre's glow: the tail's energy along its axis.
            for (int pass = 0; pass < 3; ++pass)
            {
                const float width = (pass == 0 ? 9.0f : (pass == 1 ? 3.5f : 1.3f)) * dot;
                const float alpha = pass == 0 ? 0.05f : (pass == 1 ? 0.16f : 0.55f);
                const int segments = 96;
                for (int i = 0; i < segments; ++i)
                {
                    const double t0 = tailEnd * i / segments, t1 = tailEnd * (i + 1) / segments;
                    const float a = static_cast<float> (std::min (1.0, 1.4 * tail (0.5 * (t0 + t1)) + (i < 3 ? 0.6 : 0.0)));
                    g.setColour (tint (t0).withAlpha (alpha * a));
                    g.fillRect (juce::Rectangle<float> (xAt (t0), mid - 0.5f * width, xAt (t1) - xAt (t0) + 0.3f, width));
                }
            }

            // The cloud.
            // A warm haze where the tail is young and dense.
            {
                const float xh = xAt (onset + 0.16 * std::min (tailEnd, 0.6 * span));
                juce::ColourGradient haze (juce::Colour (0xffffa45a).withAlpha (0.06f), xh, mid, juce::Colour (0xffffa45a).withAlpha (0.0f), xh + 0.22f * W, mid, true);
                g.setGradientFill (haze);
                g.fillEllipse (juce::Rectangle<float> (0.44f * W, 0.6f * H).withCentre ({ xh, mid }));
            }
            const int count = juce::roundToInt ((type == SpaceType::plate ? 17000 : (type == SpaceType::hall ? 15000 : (type == SpaceType::room ? 13000 : 13500)))
                                                * juce::jlimit (0.15f, 1.0f, (W * H) / (558.0f * 261.0f)) * 1.6f
                                                * static_cast<float> (0.2 + 0.8 * tailEnd / span));   // the same density, short or long
            Prng rng (Prng::deriveSeed (0x7370616365ull, static_cast<std::uint64_t> (type), 0));
            for (int i = 0; i < count; ++i)
            {
                const double t = onset + (tailEnd - onset) * std::pow (rng.nextDouble(), 1.12);
                const double a = tail (t);
                // Thinner where the tail is quiet (no bright bead along the axis at its end).
                if (a < 2.0e-3 || rng.nextDouble() > 0.25 + 1.5 * a)
                    continue;
                // Plates are smooth (close to their envelope), the others scatter around the axis.
                // Plates fill their envelope evenly; the others gather around the axis.
                double r = 2.0;
                for (int tries = 0; tries < 4 && r > 1.0; ++tries)   // no hard edge: draw again past it
                    r = std::abs (rng.gaussian()) * (type == SpaceType::plate ? 0.6 : 0.45);
                if (r > 1.0)
                    continue;
                const float y = mid + static_cast<float> ((rng.nextDouble() < 0.5 ? -1.0 : 1.0) * r * a * spread) * half * 0.98f;
                const float near = 1.0f - static_cast<float> (r);
                const float size = dot * (1.0f + (0.9f + 1.2f * near) * static_cast<float> (rng.nextDouble()) * (0.65f + 0.6f * static_cast<float> (a)));
                const float alpha = std::clamp ((0.3f + 0.65f * static_cast<float> (rng.nextDouble())) * (0.5f + 0.5f * near) * (0.6f + 0.6f * static_cast<float> (a)), 0.0f, 0.95f);
                g.setColour (tint (t).withAlpha (alpha));
                g.fillEllipse (xAt (t) - 0.5f * size, y - 0.5f * size, size, size);
            }

            // Early reflections: discrete amber strokes over the first tenth of the picture,
            // as many as the type has (springs: dispersed, chirping echoes; plates: few).
            // The room's real reflections (their pattern, stretched over the picture's early
            // part so it reads); a plate has none, only a few dense first arrivals.
            const double early = std::min (0.45 * tailEnd, span * (type == SpaceType::plate ? 0.05 : (type == SpaceType::hall ? 0.12 : 0.095)));
            const bool real = portrait.erCount > 1;
            const int strokes = real ? portrait.erCount : 6;
            const double lastMs = real ? std::max (1.0, portrait.erMs[static_cast<std::size_t> (portrait.erCount - 1)]) : 1.0;
            Prng er (Prng::deriveSeed (0x6561726c79ull, static_cast<std::uint64_t> (type), 0));
            for (int i = 0; i < strokes; ++i)
            {
                const double u = static_cast<double> (i) / static_cast<double> (strokes - 1);
                const double t = real ? onset + early * (0.04 + 0.96 * portrait.erMs[static_cast<std::size_t> (i)] / lastMs)
                                      : onset + early * (0.02 + 0.98 * u) * (0.8 + 0.4 * er.nextDouble());
                const double level = real ? std::clamp (1.5 * static_cast<double> (portrait.erGain[static_cast<std::size_t> (i)]) * (0.9 + 0.3 * portrait.erLevel), 0.15, 1.0)
                                          : std::clamp ((1.0 - 0.45 * u) * (0.45 + 0.55 * er.nextDouble()) * (0.9 + 0.3 * portrait.erLevel), 0.15, 1.0);
                const float x = xAt (t), extent = half * static_cast<float> (level) * 0.97f;
                const auto colour = juce::Colour (0xffffa851).interpolatedWith (juce::Colour (0xfff07224), static_cast<float> (u));
                if (portrait.spring)
                {
                    juce::Path chirp;
                    for (int n = 0; n <= 30; ++n)
                    {
                        const float v = static_cast<float> (n) / 30.0f;
                        const float py = mid - extent + 2.0f * extent * v;
                        const float px = x + 3.5f * dot * std::sin (v * 18.0f + v * v * 20.0f) * (1.0f - std::abs (v - 0.5f) * 2.0f);
                        if (n == 0)
                            chirp.startNewSubPath (px, py);
                        else
                            chirp.lineTo (px, py);
                    }
                    g.setColour (colour.withAlpha (0.25f));
                    g.strokePath (chirp, juce::PathStrokeType (3.5f * dot));
                    g.setColour (colour.withAlpha (0.9f));
                    g.strokePath (chirp, juce::PathStrokeType (1.2f * dot));
                    continue;
                }
                juce::ColourGradient body (colour.withAlpha (0.0f), x, mid - extent, colour.withAlpha (0.0f), x, mid + extent, false);
                body.addColour (0.12, colour.withAlpha (0.8f));
                body.addColour (0.5, colour.brighter (0.3f));
                body.addColour (0.88, colour.withAlpha (0.8f));
                g.setGradientFill (body);
                g.fillRect (juce::Rectangle<float> (x - 1.2f * dot, mid - extent, 2.4f * dot, 2.0f * extent));
                g.setColour (colour.withAlpha (0.08f));
                g.fillRect (juce::Rectangle<float> (x - 3.5f * dot, mid - 0.8f * extent, 7.0f * dot, 1.6f * extent));
            }
            // The sound itself, arriving: a warm bloom at zero.
            const float x0 = xAt (onset);
            juce::ColourGradient bloom (juce::Colour (0xffff9a3c).withAlpha (0.35f), x0, mid, juce::Colour (0xffff9a3c).withAlpha (0.0f), x0 + 0.06f * W, mid, true);
            g.setGradientFill (bloom);
            g.fillEllipse (juce::Rectangle<float> (0.12f * W, 0.5f * H).withCentre ({ x0, mid }));
        }

        juce::AudioProcessorValueTreeState& state;
        Watch watch, eqWatch;
        int shownType = -1, hover = -1, dragging = -1;
        juce::Image cache;
        bool cacheValid = false;
    };

    //==========================================================================
    // ECHO: the sound and its repeats along time - each repeat a stroke as loud as the
    // feedback leaves it, darker for every pass through the machine; PING-PONG alternates
    // left (up) and right (down), WIDE splits two heads. TAPE's repeats smear (wow and
    // flutter), BBD's speckle (the clock's grain).
    class EchoVisual final : public Visual
    {
    public:
        explicit EchoVisual (OspAudioProcessor& p) : processor (p), state (p.parameters) {}

    private:
        bool native() const override { return true; }
        bool changed() override
        {
            return watch.differs ({ value (state, "echo.type"), value (state, "echo.sync"), value (state, "echo.division"), value (state, "echo.time"),
                                    value (state, "echo.feedback"), value (state, "echo.tone"), value (state, "echo.age"), value (state, "echo.stereo"),
                                    value (state, "echo"), static_cast<double> (processor.hostTempo()) });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> bounds) override
        {
            EchoDelay::Settings e;
            e.type = static_cast<EchoType> (juce::roundToInt (value (state, "echo.type")));
            e.sync = value (state, "echo.sync") >= 0.5f;
            e.division = juce::roundToInt (value (state, "echo.division"));
            e.timeMs = value (state, "echo.time");
            e.feedback = 0.01 * value (state, "echo.feedback");
            e.tone = 0.01 * value (state, "echo.tone");
            e.age = 0.01 * value (state, "echo.age");
            e.stereo = static_cast<EchoStereo> (juce::roundToInt (value (state, "echo.stereo")));
            const double seconds = EchoDelay::timeSeconds (e, processor.hostTempo());
            const bool tape = e.type == EchoType::tape;

            const auto plot = juce::Rectangle<float>::leftTopRightBottom (bounds.getX() + 14.0f, bounds.getY() + 14.0f, bounds.getRight() - 10.0f, bounds.getBottom() - 5.0f);
            const float mid = plot.getCentreY(), half = 0.5f * plot.getHeight();
            const double span = std::clamp (seconds * 7.5, 0.5, 6.0);
            auto xAt = [&] (double t) { return plot.getX() + static_cast<float> (t / span) * plot.getWidth(); };

            // Grid: the beat when synced (quarter notes), else tenths and seconds.
            const double beat = 60.0 / std::max (20.0, static_cast<double> (processor.hostTempo()));
            const double step = e.sync ? beat : (span > 2.0 ? 0.5 : 0.1);
            for (double t = 0.0; t <= span + 1.0e-6; t += step)
            {
                g.setColour (juce::Colours::white.withAlpha (0.05f));
                g.fillRect (juce::Rectangle<float> (xAt (t) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
            }
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), mid - 0.5f, plot.getWidth(), 1.0f));

            g.setFont (fonts::make (9.0f, fonts::Weight::regular, 0.02f));
            g.setColour (juce::Colour (0xff9aa09e).withAlpha (0.85f));
            const auto ms = juce::String (juce::roundToInt (1000.0 * seconds)) + " ms";
            g.drawText (e.sync ? juce::String (shaping::echoDivisionName (e.division)) + "  " + ms : ms,
                        juce::Rectangle<float> (bounds.getX() + 10.0f, bounds.getY() + 2.0f, 160.0f, 11.0f), juce::Justification::centredLeft, false);
            g.drawText ("FEEDBACK " + juce::String (juce::roundToInt (100.0 * e.feedback)) + " %", juce::Rectangle<float> (bounds.getRight() - 110.0f, bounds.getY() + 2.0f, 100.0f, 11.0f),
                        juce::Justification::centredRight, false);

            // The sound itself at zero.
            auto stroke = [&] (float x, float from, float to, juce::Colour c, float width) {
                const float top = std::min (from, to), bottom = std::max (from, to);
                juce::ColourGradient body (c.withAlpha (0.0f), x, top, c.withAlpha (0.0f), x, bottom, false);
                body.addColour (0.15, c.withAlpha (0.85f));
                body.addColour (0.5, c);
                body.addColour (0.85, c.withAlpha (0.85f));
                g.setGradientFill (body);
                g.fillRect (juce::Rectangle<float> (x - 0.5f * width, top, width, bottom - top));
                g.setColour (c.withAlpha (0.1f));
                g.fillRect (juce::Rectangle<float> (x - 2.5f * width, top + 0.15f * (bottom - top), 5.0f * width, 0.7f * (bottom - top)));
            };
            stroke (xAt (0.0), mid - 0.92f * half, mid + 0.92f * half, juce::Colour (0xfff3eee6), 1.8f);

            // The repeats: level after n passes (the loop saturates near 100 %), darker each time.
            Prng grain (Prng::deriveSeed (0x6563686full, static_cast<std::uint64_t> (e.type), 0));
            const double fb = std::min (0.98, e.feedback * (tape ? 1.04 : 1.03));
            double level = 1.0;
            const auto warm = tape ? juce::Colour (0xffffb15c) : juce::Colour (0xffd9c38a);
            for (int n = 1; n <= 40; ++n)
            {
                level = n == 1 ? 0.92 : level * fb;
                const double t = seconds * n;
                if (t > span || level < 0.02)
                    break;
                const float dark = std::clamp (static_cast<float> (n) * (0.07f + 0.08f * (1.0f - static_cast<float> (e.tone))) * (tape ? 0.8f : 1.1f), 0.0f, 0.85f);
                const auto c = warm.interpolatedWith (identity, std::min (1.0f, 0.35f + 0.6f * dark)).withMultipliedBrightness (1.0f - 0.45f * dark);
                const float h = half * 0.92f * static_cast<float> (std::sqrt (level));
                const float width = (tape ? 2.2f + 0.3f * static_cast<float> (n) * static_cast<float> (e.age) : 2.0f);
                auto at = [&] (double time, float sign, bool both) {
                    const float x = xAt (time);
                    if (both)
                        stroke (x, mid - h, mid + h, c, width);
                    else
                        stroke (x, mid, mid - sign * h, c, width);
                    if (tape && e.age > 0.05)
                    {
                        // Wow and flutter: faint ghosts either side.
                        const float wob = static_cast<float> (e.age) * (1.0f + 0.4f * static_cast<float> (n));
                        g.setColour (c.withAlpha (0.18f));
                        g.fillRect (juce::Rectangle<float> (x - wob - 0.5f, both ? mid - 0.8f * h : std::min (mid, mid - sign * 0.8f * h), 1.0f, both ? 1.6f * h : 0.8f * h));
                        g.fillRect (juce::Rectangle<float> (x + wob - 0.5f, both ? mid - 0.8f * h : std::min (mid, mid - sign * 0.8f * h), 1.0f, both ? 1.6f * h : 0.8f * h));
                    }
                    else if (! tape)
                    {
                        // The bucket brigade's grain: a few specks around the repeat.
                        const int specks = 4 + juce::roundToInt (14.0 * e.age);
                        for (int k = 0; k < specks; ++k)
                        {
                            const float px = x + static_cast<float> (grain.bipolar()) * (2.0f + 3.0f * static_cast<float> (e.age));
                            const float py = both ? mid + static_cast<float> (grain.bipolar()) * h : mid - sign * static_cast<float> (grain.nextDouble()) * h;
                            g.setColour (c.withAlpha (0.35f * static_cast<float> (grain.nextDouble()) + 0.15f));
                            g.fillEllipse (px - 0.7f, py - 0.7f, 1.4f, 1.4f);
                        }
                    }
                };
                switch (e.stereo)
                {
                    case EchoStereo::mono: at (t, 1.0f, true); break;
                    case EchoStereo::pingPong: at (t, n % 2 == 1 ? 1.0f : -1.0f, false); break;
                    case EchoStereo::wide:
                        at (t, 1.0f, false);
                        at (seconds * 1.06 * n + 0.004, -1.0f, false);
                        break;
                }
            }
            if (e.stereo != EchoStereo::mono)
            {
                g.setFont (fonts::make (8.0f, fonts::Weight::medium, 0.06f));
                g.setColour (juce::Colour (0xff9aa09e).withAlpha (0.55f));
                g.drawText ("L", juce::Rectangle<float> (plot.getX() - 10.0f, plot.getY(), 8.0f, 10.0f), juce::Justification::centred, false);
                g.drawText ("R", juce::Rectangle<float> (plot.getX() - 10.0f, plot.getBottom() - 10.0f, 8.0f, 10.0f), juce::Justification::centred, false);
            }
        }

        OspAudioProcessor& processor;
        juce::AudioProcessorValueTreeState& state;
        Watch watch;
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
            // The processing curve in CHARACTER's moss (the source's spectrum stays its own).
            g.setColour (identity.withAlpha (0.1f));
            g.fillPath (under);
            g.setColour (identity);
            g.strokePath (response, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Cutoff (TILT: the pivot) and its value.
            const double markerHz = type == FilterType::tilt ? 800.0 : fc;
            const float mx = xAt (markerHz), my = yAt (responseDb (type, markerHz, fc, resonance, tilt));
            g.setColour (raised);
            g.fillEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f);
            g.setColour (identity);
            g.drawEllipse (mx - 4.0f, my - 4.0f, 8.0f, 8.0f, 1.5f);
            g.setColour (raised.withAlpha (0.9f));
            g.setFont (type::annotation (10.0f));
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
            g.setFont (type::annotation (9.0f));
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
            // MOVEMENT's mineral blue for the movement itself; secondary lines a quiet stone.
            const auto stone = juce::Colour (0xffaaa49a);
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
                    trace (g, area, window, [rate] (double t) { return shaping::sharedWander (11, t, rate * 1.3) * 0.6; }, stone.withAlpha (0.35f), 1.0f, mid, half * depth);
                    trace (g, area, window, [rate, tone] (double t) { return tone * shaping::sharedWander (7, t, rate * 0.8); }, stone.withAlpha (0.7f), 1.4f, mid, half * depth);
                    trace (g, area, window, [rate, pitch] (double t) { return pitch * shaping::sharedWander (3, t, rate); }, identity, 2.0f, mid, half * depth);
                    caption (g, area, "PITCH", identity, area.getY());
                    caption (g, area, "TONE", stone, area.getY() + 11.0f);
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
                           identity, 2.0f, area.getY() + area.getHeight() * 0.3f, area.getHeight() * 0.24f * depth);
                    trace (g, area, 1.0, [flutterHz, b, rough] (double t) { return std::sin (twoPi * flutterHz * t) * (0.7 + 0.3 * shaping::sharedWander (9, t, 4.0)) * (0.2 + 0.8 * b) + rough (t); },
                           stone, 1.2f, area.getY() + area.getHeight() * 0.76f, area.getHeight() * 0.18f * depth);
                    caption (g, area, "WOW  " + format::hertz (wowHz), identity, area.getY());
                    caption (g, area, "FLUTTER  " + format::hertz (flutterHz), stone, area.getY() + area.getHeight() * 0.5f);
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
                    trace (g, area, window, [rate, stereo] (double t) { return std::sin (twoPi * rate * t + std::numbers::pi * stereo); }, stone, 1.6f, mid, extent);
                    trace (g, area, window, [rate] (double t) { return std::sin (twoPi * rate * t); }, identity, 1.8f, mid, extent);
                    caption (g, area, "L", identity, area.getY());
                    caption (g, area, "R", stone, area.getY() + 11.0f);
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
                        trace (g, area, window, toTrace (std::numbers::pi * stereo), stone.withAlpha (0.7f), 1.3f, bottom - 0.5f * height, 0.5f * height);
                    trace (g, area, window, toTrace (0.0), identity, 2.0f, bottom - 0.5f * height, 0.5f * height);
                    caption (g, area, format::hertz (rate), identity, area.getY());
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
                            g.setColour (identity.withAlpha (0.14f));
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
                    g.setColour (identity.withAlpha (0.3f));
                    g.strokePath (ghost, juce::PathStrokeType (1.0f));
                    g.setGradientFill (juce::ColourGradient (identity.withAlpha (0.6f), 0.0f, area.getY(), identity.withAlpha (0.12f), 0.0f, area.getBottom(), false));
                    g.fillPath (contour);
                    g.setColour (identity.brighter (0.15f));
                    g.strokePath (contour, juce::PathStrokeType (1.2f));
                    if (phase >= 0.0f)
                    {
                        const float x = area.getX() + phase * area.getWidth();
                        g.setColour (accent);   // the live playhead keeps the instrument's coral
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
    // SHAPER's pattern as an instrument of its own: the steps (CUSTOM: drawn by the musician)
    // under the contour the shaper plays (SMOOTH and DEPTH applied), the grid, the playhead.
    // Editing a library pattern makes it CUSTOM, starting from that pattern.
    struct ShaperTool
    {
        bool draw = false;                     ///< STEP: one level per step; DRAW: a free line
        StepShape brush = StepShape::hold;     ///< what a STEP click makes of a step
    };

    ShaperStep stepFromBrush (StepShape brush, float level, float previousEnd)
    {
        level = std::clamp (level, 0.0f, 1.0f);
        switch (brush)
        {
            case StepShape::hold: return { level, level, StepShape::hold };
            case StepShape::down: return { level, 0.15f * level, StepShape::down };
            case StepShape::up: return { 0.15f * level, level, StepShape::up };
            case StepShape::pulse: return { 0.25f * level, level, StepShape::pulse };
            case StepShape::dip: return { level, 0.2f * level, StepShape::dip };
            case StepShape::soft: return { previousEnd, level, StepShape::soft };
        }
        return { level, level, StepShape::hold };
    }

    class ShaperEditor final : public Visual, public juce::SettableTooltipClient
    {
    public:
        ShaperEditor (OspAudioProcessor& p, ShaperTool& t, bool isLarge) : processor (p), tool (t), large (isLarge)
        {
            setInterceptsMouseClicks (true, false);
            setTooltip (large ? "Draw the pattern: STEP sets each step's level (its shape from the brush), DRAW paints a free line"
                              : "Click or drag to set the steps (the magnifier opens the large editor)");
        }

        bool custom() const { return value (processor.parameters, "movement.shaper.custom") >= 0.5f; }
        ShaperPattern steps() const
        {
            return custom() ? processor.shaperCustomPattern() : RhythmicShaper::patternSteps (juce::roundToInt (value (processor.parameters, "movement.shaper.pattern")));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            processor.undoManager.beginNewTransaction ("Shaper pattern");
            working = steps();
            if (! custom())
            {
                // Editing a library pattern: it becomes CUSTOM, starting from it.
                processor.setShaperCustomPattern (working);
                if (auto* c = processor.parameters.getParameter ("movement.shaper.custom"))
                    c->setValueNotifyingHost (1.0f);
            }
            last = e.position;
            apply (e.position, e.position, e.mods.isPopupMenu());
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            apply (last, e.position, false);
            last = e.position;
        }

    private:
        bool native() const override { return true; }
        bool animates() const override { return true; }   // the playhead

        juce::Rectangle<float> plotArea() const
        {
            const auto r = getLocalBounds().toFloat();
            return large ? r.reduced (12.0f, 10.0f).withTrimmedBottom (12.0f) : r.reduced (8.0f, 6.0f);
        }
        int stepAt (float x) const
        {
            const auto a = plotArea();
            return std::clamp (static_cast<int> ((x - a.getX()) / a.getWidth() * RhythmicShaper::steps), 0, RhythmicShaper::steps - 1);
        }
        float levelAt (float y) const
        {
            const auto a = plotArea();
            return std::clamp ((a.getBottom() - y) / a.getHeight(), 0.0f, 1.0f);
        }

        /** One gesture segment from `from` to `to`: every step (STEP) or position (DRAW) it crosses. */
        void apply (juce::Point<float> from, juce::Point<float> to, bool cycleShape)
        {
            const auto a = plotArea();
            auto pattern = working;
            if (cycleShape)
            {
                // Right-click: the step's shape goes round (HOLD, FALL, RISE, DIP, PULSE, SOFT).
                auto& st = pattern[static_cast<std::size_t> (stepAt (to.x))];
                const float level = std::max (st.start, st.end);
                const auto next = static_cast<StepShape> ((static_cast<int> (st.shape) + 1) % 6);
                st = stepFromBrush (next, level, st.start);
            }
            else if (! tool.draw)
            {
                const int s0 = stepAt (from.x), s1 = stepAt (to.x);
                for (int i = std::min (s0, s1); i <= std::max (s0, s1); ++i)
                {
                    const float t = s1 == s0 ? 1.0f : static_cast<float> (i - s0) / static_cast<float> (s1 - s0);
                    const float level = levelAt (from.y + (to.y - from.y) * t);
                    const float previousEnd = pattern[static_cast<std::size_t> ((i + RhythmicShaper::steps - 1) % RhythmicShaper::steps)].end;
                    pattern[static_cast<std::size_t> (i)] = stepFromBrush (tool.brush, level, previousEnd);
                }
            }
            else
            {
                // DRAW: the line sets each step's start (first half) or end (second half), rounded.
                const int samples = std::max (1, juce::roundToInt (std::abs (to.x - from.x) / a.getWidth() * 64.0f));
                for (int k = 0; k <= samples; ++k)
                {
                    const float t = static_cast<float> (k) / static_cast<float> (samples);
                    const float x = from.x + (to.x - from.x) * t, y = from.y + (to.y - from.y) * t;
                    const float position = std::clamp ((x - a.getX()) / a.getWidth(), 0.0f, 0.99999f) * RhythmicShaper::steps;
                    auto& st = pattern[static_cast<std::size_t> (static_cast<int> (position))];
                    st.shape = StepShape::soft;
                    ((position - std::floor (position)) < 0.5f ? st.start : st.end) = levelAt (y);
                }
            }
            if (! (pattern == working))
            {
                working = pattern;
                processor.setShaperCustomPattern (pattern);
                repaint();
            }
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float>) override
        {
            using namespace palette;
            auto& s = processor.parameters;
            const auto area = plotArea();
            const auto pattern = steps();
            const double smooth = 0.01 * value (s, "movement.shaper.smooth");
            const float depth = static_cast<float> (std::clamp (0.01 * value (s, "motion"), 0.0, 1.0));
            const auto span = RhythmicShaper::span (pattern, smooth);
            const float phase = processor.shaperPhase();
            const float cell = area.getWidth() / RhythmicShaper::steps;
            const int current = phase >= 0.0f ? std::min (RhythmicShaper::steps - 1, static_cast<int> (phase * RhythmicShaper::steps)) : -1;
            const bool editable = custom() || large;
            for (int i = 0; i < RhythmicShaper::steps; ++i)
            {
                const auto c = juce::Rectangle<float> (area.getX() + static_cast<float> (i) * cell, area.getY(), cell, area.getHeight());
                if (i == current)
                {
                    g.setColour (identity.withAlpha (0.14f));
                    g.fillRect (c);
                }
                if (i > 0)
                {
                    g.setColour (displayLine.withAlpha (i % 4 == 0 ? 1.0f : 0.5f));
                    g.drawVerticalLine (juce::roundToInt (c.getX()), area.getY(), area.getBottom());
                }
                if (editable)
                {
                    // The step as drawn: its start and end level, a bar under it.
                    const auto& st = pattern[static_cast<std::size_t> (i)];
                    const float top = std::max (st.start, st.end);
                    g.setColour (juce::Colour (0xffd9dcd8).withAlpha (custom() ? 0.1f : 0.05f));
                    g.fillRect (c.reduced (1.5f, 0.0f).withTop (area.getBottom() - top * area.getHeight()));
                    g.setColour (juce::Colour (0xffd9dcd8).withAlpha (custom() ? 0.55f : 0.25f));
                    g.drawLine (c.getX() + 2.0f, area.getBottom() - st.start * area.getHeight(), c.getRight() - 2.0f, area.getBottom() - st.end * area.getHeight(), 1.2f);
                }
                if (large)
                {
                    g.setFont (fonts::make (8.5f, fonts::Weight::regular, 0.02f));
                    g.setColour (juce::Colour (0xff9aa09e).withAlpha (i % 4 == 0 ? 0.85f : 0.45f));
                    g.drawText (i % 4 == 0 ? juce::String (i / 4 + 1) : juce::String ("."), juce::Rectangle<float> (c.getX(), area.getBottom() + 1.0f, cell, 11.0f),
                                juce::Justification::centred, false);
                }
            }
            juce::Path contour, ghost;
            contour.startNewSubPath (area.getX(), area.getBottom());
            const int points = large ? 480 : 240;
            for (int i = 0; i <= points; ++i)
            {
                const double x = static_cast<double> (i) / points;
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
            g.setColour (identity.withAlpha (0.3f));
            g.strokePath (ghost, juce::PathStrokeType (1.0f));
            g.setGradientFill (juce::ColourGradient (identity.withAlpha (0.55f), 0.0f, area.getY(), identity.withAlpha (0.1f), 0.0f, area.getBottom(), false));
            g.fillPath (contour);
            g.setColour (identity.brighter (0.15f));
            g.strokePath (contour, juce::PathStrokeType (1.2f));
            if (phase >= 0.0f)
            {
                const float x = area.getX() + phase * area.getWidth();
                g.setColour (accent);   // the live playhead keeps the instrument's coral
                g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY(), 1.5f, area.getHeight()));
            }
        }

        OspAudioProcessor& processor;
        ShaperTool& tool;
        bool large;
        ShaperPattern working {};
        juce::Point<float> last;
    };

    /** The large editor's tools: STEP / DRAW, the brush, then COPY FROM, CLEAR, SAVE and LOAD. */
    class ShaperToolbar final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ShaperToolbar (ShaperTool& t, juce::Colour c) : tool (t), accent (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void()> onToolChanged, onCopyFrom, onClear, onSave, onLoad;

        void paint (juce::Graphics& g) override
        {
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const auto r = bounds (static_cast<int> (i));
                const bool on = active (static_cast<int> (i));
                design::draw::button (g, r, 4.0f, on, hover == static_cast<int> (i), accent);
                g.setColour (on ? accent.darker (0.35f) : design::colour::text.withAlpha (0.8f));
                g.setFont (fonts::make (9.5f, fonts::Weight::medium, 0.05f));
                g.drawText (items[i], r, juce::Justification::centred, false);
            }
        }
        void mouseMove (const juce::MouseEvent& e) override { setHover (at (e.position)); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int i = at (e.position);
            if (i < 0 || ! getLocalBounds().contains (e.getPosition()))
                return;
            if (i <= 1)
                tool.draw = i == 1;
            else if (i <= 7)
            {
                tool.draw = false;
                tool.brush = brushes[static_cast<std::size_t> (i - 2)];
            }
            else if (i == 8 && onCopyFrom != nullptr)
                onCopyFrom();
            else if (i == 9 && onClear != nullptr)
                onClear();
            else if (i == 10 && onSave != nullptr)
                onSave();
            else if (i == 11 && onLoad != nullptr)
                onLoad();
            if (i <= 7 && onToolChanged != nullptr)
                onToolChanged();
            repaint();
        }

    private:
        static constexpr std::array<StepShape, 6> brushes { StepShape::hold, StepShape::down, StepShape::up, StepShape::pulse, StepShape::dip, StepShape::soft };
        const std::array<juce::String, 12> items { "STEP", "DRAW", "HOLD", "FALL", "RISE", "PULSE", "DIP", "SOFT",
                                                   juce::String::fromUTF8 ("COPY \xe2\x96\xbe"), "CLEAR", juce::String::fromUTF8 ("SAVE\xe2\x80\xa6"),
                                                   juce::String::fromUTF8 ("LOAD \xe2\x96\xbe") };
        // Groups: tools, brushes, actions (a gap between groups).
        juce::Rectangle<float> bounds (int i) const
        {
            const float w = static_cast<float> (getWidth()), h = static_cast<float> (getHeight());
            const float gapBetween = 10.0f, unitWidth = (w - 2.0f * gapBetween) / 12.0f;
            const float x = static_cast<float> (i) * unitWidth + (i >= 2 ? gapBetween : 0.0f) + (i >= 8 ? gapBetween : 0.0f);
            return { x + 1.5f, 1.0f, unitWidth - 3.0f, h - 2.0f };
        }
        bool active (int i) const
        {
            if (i <= 1)
                return tool.draw == (i == 1);
            if (i <= 7)
                return ! tool.draw && tool.brush == brushes[static_cast<std::size_t> (i - 2)];
            return false;
        }
        int at (juce::Point<float> p) const
        {
            for (int i = 0; i < 12; ++i)
                if (bounds (i).contains (p))
                    return i;
            return -1;
        }
        void setHover (int h)
        {
            if (h != hover)
            {
                hover = h;
                repaint();
            }
        }
        ShaperTool& tool;
        juce::Colour accent;
        int hover = -1;
    };

    /** A small magnifier on the SHAPER display: opens (and closes) the large editor. */
    class MagnifierButton final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MagnifierButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        std::function<void()> onClick;
        void setExpanded (bool e)
        {
            expanded = e;
            setTooltip (expanded ? "Back to the small view" : "Open the large pattern editor");
            repaint();
        }
        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f);
            g.setColour (juce::Colour (0xff1a1e1f).withAlpha (0.8f));
            g.fillRoundedRectangle (r.expanded (1.0f), 4.0f);
            const auto colour = juce::Colour (0xffd9dcd8).withAlpha (isMouseOver() ? 1.0f : 0.7f);
            const float d = 0.58f * r.getWidth();
            const auto lens = juce::Rectangle<float> (d, d).withPosition (r.getX() + 0.08f * r.getWidth(), r.getY() + 0.08f * r.getHeight());
            g.setColour (colour);
            g.drawEllipse (lens, 1.3f);
            g.drawLine (lens.getRight() - 0.12f * d, lens.getBottom() - 0.12f * d, r.getRight() - 0.1f * r.getWidth(), r.getBottom() - 0.1f * r.getHeight(), 1.6f);
            const auto c = lens.getCentre();
            g.drawLine (c.x - 0.25f * d, c.y, c.x + 0.25f * d, c.y, 1.2f);
            if (! expanded)
                g.drawLine (c.x, c.y - 0.25f * d, c.x, c.y + 0.25f * d, 1.2f);
        }
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (getLocalBounds().contains (e.getPosition()) && onClick != nullptr)
                onClick();
        }

    private:
        bool expanded = false;
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
                                    value (s, "life.takes"), value (s, "life.takesSeed"), value (s, "life.character"),
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
            // TAKES: the note's fixed set of takes, all drawn, the one playing now strong;
            // endless: the last few performances, a new one with every note.
            const int takeIndex = juce::roundToInt (value (s, "life.takes"));
            const int takes = takeIndex > 0 ? takeIndex + 1 : 0;
            const auto reroll = static_cast<std::uint64_t> (std::max (0, juce::roundToInt (value (s, "life.takesSeed"))));
            const int contours = takes > 0 ? takes : 7;
            const int playing = takes > 0 ? static_cast<int> (notes % static_cast<std::uint64_t> (takes)) : 0;
            for (int order = contours - 1; order >= 0; --order)
            {
                // Draw the playing take last (on top).
                const int c = takes > 0 ? (playing + 1 + order) % takes : order;
                const bool current = takes > 0 ? c == playing : c == 0;
                Prng rng (takes > 0 ? Prng::deriveSeed (0x74616b65ull + reroll, static_cast<std::uint64_t> (takes), static_cast<std::uint64_t> (c))
                                    : Prng::deriveSeed (0x6c696665ull, notes + static_cast<std::uint64_t> (c), static_cast<std::uint64_t> (c)));
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
                    // The performance now in LIFE's rust (TONE still moves its shade a little).
                    const auto colour = identity.withMultipliedBrightness (0.92f + 0.22f * static_cast<float> (colourAt));
                    g.setGradientFill (juce::ColourGradient (colour.withAlpha (0.3f), 0.0f, base - height, colour.withAlpha (0.02f), 0.0f, base, false));
                    g.fillPath (fill);
                    g.setColour (colour.brighter (0.2f));
                    g.strokePath (contour, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                else
                {
                    const auto colour = lavender.interpolatedWith (mineral, static_cast<float> (colourAt)).withMultipliedSaturation (0.35f);
                    const float alpha = takes > 0 ? 0.42f : 0.22f + 0.06f * static_cast<float> (contours - c);
                    g.setColour (colour.withAlpha (alpha));
                    g.strokePath (contour, juce::PathStrokeType (1.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }
            g.setColour (displayText.withAlpha (0.8f));
            g.setFont (type::annotation (9.0f));
            const auto lifeText = life < 0.01 ? juce::String ("EVERY NOTE THE SAME") : "LIFE " + juce::String (juce::roundToInt (100.0 * life)) + " %";
            g.drawText (takes > 0 && life >= 0.01 ? juce::String (takes) + " TAKES " + juce::String::fromUTF8 ("\xc2\xb7 ") + lifeText : lifeText,
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
            g.setGradientFill (juce::ColourGradient (identity.withAlpha (0.2f), 0.0f, area.getY(), identity.withAlpha (0.02f), 0.0f, area.getBottom(), false));
            g.fillPath (fill);
            g.setColour (identity);
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
        /** A compact popover (230-270 x 170-210 reference px) that unfolds above its macro:
            a small title with the mode as a quiet selector beside it, the visualisation as
            the largest element, then small knobs. */
        MacroPanel (OspAudioProcessor& p, juce::String popupTitle, int width, int displayHeight, design::colour::Macro macro)
            : MiniPanel (std::move (popupTitle)), processor (p), panelWidth (width), visualHeight (displayHeight),
              identity (macro), accent (design::colour::macro (macro))
        {
        }
        /** A popover without a macro identity of its own (REIMAGINED: the aurora at its amount). */
        MacroPanel (OspAudioProcessor& p, juce::String popupTitle, int width, int displayHeight, juce::Colour colour)
            : MiniPanel (std::move (popupTitle)), processor (p), panelWidth (width), visualHeight (displayHeight),
              identity (design::colour::Macro::life), accent (colour), ownColour (true)
        {
        }

        bool compact() const override { return true; }

        ValueSelector& mode (const char* id, juce::String tooltip)
        {
            modeSelector = std::make_unique<ValueSelector> (*processor.parameters.getParameter (id), juce::String());
            modeSelector->setPlain (true);
            modeSelector->setAccent (accent);
            modeSelector->setTooltip (tooltip);
            addAndMakeVisible (*modeSelector);
            return *modeSelector;
        }
        /** SPACE also shows its types as a row of tabs under the display. */
        SegmentedControl& tabs (const char* id, juce::StringArray items, juce::String tooltip)
        {
            modeTabs = std::make_unique<SegmentedControl> (*processor.parameters.getParameter (id), std::move (items));
            modeTabs->setAccent (accent);
            modeTabs->setTooltip (tooltip);
            addAndMakeVisible (*modeTabs);
            return *modeTabs;
        }
        void setVisual (std::unique_ptr<Visual> v)
        {
            visual = std::move (v);
            if (ownColour)
                visual->setIdentityColour (accent.brighter (0.2f));
            else
                visual->setIdentity (identity);
            addAndMakeVisible (*visual);
        }
        MiniKnob& knob (bool secondary, const char* id, const char* caption, MiniKnob::Formatter f)
        {
            auto k = std::make_unique<MiniKnob> (processor.parameters, id, caption, std::move (f));
            k->setSmall (secondary);
            k->setArcColour (accent);
            addAndMakeVisible (*k);
            (secondary ? secondaryRow : primaryRow).push_back (k.get());
            knobs.push_back (std::move (k));
            return *knobs.back();
        }

        static constexpr int headerTop = 32, gap = 8, tabsHeight = 21, cellHeight = 58, secondaryCellHeight = 46, bottom = 10;

        juce::Point<int> cardSize() const override
        {
            int h = headerTop + visualHeight + gap + (modeTabs != nullptr ? tabsHeight + 6 : 0) + cellHeight + bottom;
            if (! secondaryRow.empty())
                h += 2 + secondaryCellHeight;
            return { panelWidth, h };
        }

    protected:
        void layoutHeader()
        {
            if (modeSelector != nullptr)
            {
                const auto c = card();
                modeSelector->setBounds (c.getRight() - 12 - 120, c.getY() + 7, 120, 20);
            }
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            layoutHeader();
            if (visual != nullptr)
                visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (gap);
            if (modeTabs != nullptr)
            {
                modeTabs->setBounds (area.removeFromTop (tabsHeight));
                area.removeFromTop (6);
            }
            layoutRow (primaryRow, area.removeFromTop (cellHeight));
            if (! secondaryRow.empty())
            {
                area.removeFromTop (2);
                layoutRow (secondaryRow, area.removeFromTop (secondaryCellHeight));
            }
        }

        static void layoutRow (const std::vector<MiniKnob*>& row, juce::Rectangle<int> line)
        {
            if (row.empty())
                return;
            const int cell = std::min (line.getWidth() / static_cast<int> (row.size()), 80);
            line = line.withSizeKeepingCentre (cell * static_cast<int> (row.size()), line.getHeight());
            for (auto* k : row)
                k->setBounds (line.removeFromLeft (cell));
        }

        OspAudioProcessor& processor;
        int panelWidth;
        int visualHeight;
        design::colour::Macro identity;
        juce::Colour accent;   ///< the macro's identity (its knobs' arcs, the chosen mode)
        bool ownColour = false;
        std::unique_ptr<ValueSelector> modeSelector;
        std::unique_ptr<SegmentedControl> modeTabs;
        std::unique_ptr<Visual> visual;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::vector<MiniKnob*> primaryRow, secondaryRow;
    };

    //==========================================================================
    /** MOVEMENT: the mode, its picture, then that mode's own controls (SHAPER: PATTERN, RATE,
        TARGET on one line, then DEPTH and SMOOTH). Switching modes keeps every mode's values. */
    class MovementPanel final : public MacroPanel
    {
    public:
        explicit MovementPanel (OspAudioProcessor& p)
            : MacroPanel (p, "MOVEMENT", 270, 62, design::colour::Macro::movement),
              customAttachment (*p.parameters.getParameter ("movement.shaper.custom"), [this] (float) {
                  for (auto& sel : selectors)
                      sel->repaint();
              })
        {
            auto& m = mode ("movement.mode", "Movement: drift, tape, chorus, pulse or rhythmic shaper");
            m.onChange = [this] (int newMode) {
                build (newMode);
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            };
            expanded = p.shaperEditorLarge();
            magnifier.onClick = [this] {
                expanded = ! expanded;
                processor.setShaperEditorLarge (expanded);
                build (static_cast<int> (MovementMode::shaper));
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            };
            addChildComponent (magnifier);
            build (m.selected());
        }

        static constexpr int selectorHeight = 22, largeHeight = 214, toolbarHeight = 24, largeWidth = 580;

        juce::Point<int> cardSize() const override
        {
            if (shaperMode && expanded)
                return { largeWidth, headerTop + largeHeight + gap + toolbarHeight + 6 + selectorHeight + 6 + cellHeight + bottom };
            const int controls = shaperMode ? selectorHeight + 6 + cellHeight : cellHeight;
            return { panelWidth, headerTop + visualHeight + gap + controls + bottom };
        }

    private:
        void build (int newMode)
        {
            knobs.clear();
            primaryRow.clear();
            selectors.clear();
            toolbar.reset();
            shaperMode = newMode == static_cast<int> (MovementMode::shaper);
            if (! shaperMode)
                expanded = false;
            // SHAPER draws its own pattern editor; the other modes their picture.
            if (shaperMode)
                setVisual (std::make_unique<ShaperEditor> (processor, tool, expanded));
            else if (dynamic_cast<MovementVisual*> (visual.get()) == nullptr)
                setVisual (std::make_unique<MovementVisual> (processor));
            magnifier.setVisible (shaperMode);
            magnifier.setExpanded (expanded);
            magnifier.toFront (false);
            auto& state = processor.parameters;
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            switch (static_cast<MovementMode> (newMode))
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
                {
                    // PATTERN, RATE and TARGET as three quiet value keys (their names in tooltips).
                    const std::pair<const char*, const char*> keys[] = {
                        { "movement.shaper.pattern", "Pattern: a curated one, or CUSTOM (your own steps; draw on the display)" },
                        { "movement.shaper.rate", "Rate (synced to the host tempo)" },
                        { "movement.shaper.target", "What the pattern shapes: the volume, a low-pass filter, or both" } };
                    for (const auto& [id, tip] : keys)
                    {
                        selectors.push_back (std::make_unique<ValueSelector> (*state.getParameter (id), juce::String()));
                        selectors.back()->setTooltip (tip);
                        addAndMakeVisible (*selectors.back());
                    }
                    setUpPatternSelector (*selectors.front());
                    if (expanded)
                    {
                        toolbar = std::make_unique<ShaperToolbar> (tool, accent);
                        toolbar->onCopyFrom = [this] { copyFromMenu(); };
                        toolbar->onClear = [this] { setCustom ([] { ShaperPattern p; for (auto& st : p) st = { 1.0f, 1.0f, StepShape::hold }; return p; }()); };
                        toolbar->onSave = [this] { savePattern(); };
                        toolbar->onLoad = [this] { loadMenu(); };
                        addAndMakeVisible (*toolbar);
                    }
                    // DEPTH is the MOVEMENT macro itself (one control, two places): 100 % takes
                    // the pattern's lowest point to closed - on VOL, silence - 10 % only breathes.
                    knob (false, "motion", "DEPTH", percent);
                    knob (false, "movement.shaper.smooth", "SMOOTH", percent);
                    break;
                }
            }
            resized();
        }

        /** PATTERN also offers CUSTOM, a copy of any pattern to start from, and the saved ones. */
        void setUpPatternSelector (ValueSelector& selector)
        {
            auto* customParam = processor.parameters.getParameter ("movement.shaper.custom");
            selector.textOverride = [customParam] { return customParam != nullptr && customParam->getValue() >= 0.5f ? juce::String ("CUSTOM") : juce::String(); };
            selector.onPick = [customParam] (int) {
                if (customParam != nullptr && customParam->getValue() >= 0.5f)
                {
                    customParam->beginChangeGesture();
                    customParam->setValueNotifyingHost (0.0f);
                    customParam->endChangeGesture();
                }
            };
            juce::Component::SafePointer<MovementPanel> safe (this);
            selector.extraItems = [safe, customParam] (juce::PopupMenu& menu) {
                menu.addSeparator();
                menu.addItem ("CUSTOM", true, customParam != nullptr && customParam->getValue() >= 0.5f, [safe] { if (safe != nullptr) safe->setCustomOn(); });
                const auto files = OspAudioProcessor::savedShaperPatterns();
                if (! files.isEmpty())
                {
                    juce::PopupMenu saved;
                    for (const auto& f : files)
                        saved.addItem (f.getFileNameWithoutExtension(), [safe, f] { if (safe != nullptr) safe->loadFile (f); });
                    menu.addSubMenu ("SAVED", saved);
                }
            };
        }

        void setCustomOn()
        {
            if (auto* c = processor.parameters.getParameter ("movement.shaper.custom"))
            {
                c->beginChangeGesture();
                c->setValueNotifyingHost (1.0f);
                c->endChangeGesture();
            }
            repaintEditor();
        }
        void setCustom (const ShaperPattern& pattern)
        {
            processor.undoManager.beginNewTransaction ("Shaper pattern");
            processor.setShaperCustomPattern (pattern);
            setCustomOn();
        }
        void repaintEditor()
        {
            if (visual != nullptr)
                visual->repaint();
            for (auto& sel : selectors)
                sel->repaint();
        }
        void copyFromMenu()
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<MovementPanel> safe (this);
            for (int i = 0; i < RhythmicShaper::patternCount; ++i)
                menu.addItem (RhythmicShaper::patternName (i), [safe, i] { if (safe != nullptr) safe->setCustom (RhythmicShaper::patternSteps (i)); });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolbar.get()).withDeletionCheck (*this));
        }
        void loadMenu()
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<MovementPanel> safe (this);
            const auto files = OspAudioProcessor::savedShaperPatterns();
            for (const auto& f : files)
                menu.addItem (f.getFileNameWithoutExtension(), [safe, f] { if (safe != nullptr) safe->loadFile (f); });
            if (files.isEmpty())
                menu.addItem ("No saved patterns yet", false, false, [] {});
            menu.addSeparator();
            menu.addItem ("Show folder", [] {
                OspAudioProcessor::shaperPatternFolder().createDirectory();
                OspAudioProcessor::shaperPatternFolder().revealToUser();
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolbar.get()).withDeletionCheck (*this));
        }
        void loadFile (const juce::File& file)
        {
            if (const auto pattern = OspAudioProcessor::readShaperPatternFile (file))
                setCustom (*pattern);
        }
        void savePattern()
        {
            auto* window = new juce::AlertWindow ("Save pattern", "A name for this SHAPER pattern:", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", "My pattern");
            window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<MovementPanel> safe (this);
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
                const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());
                if (result == 1 && name.isNotEmpty() && safe != nullptr)
                {
                    const auto pattern = safe->processor.shaperCustomPattern();
                    OspAudioProcessor::writeShaperPatternFile (OspAudioProcessor::shaperPatternFolder().getChildFile (name + OspAudioProcessor::shaperPatternExtension), pattern);
                }
            }), true);
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            layoutHeader();
            const int displayHeight = shaperMode && expanded ? largeHeight : visualHeight;
            visual->setBounds (area.removeFromTop (displayHeight));
            magnifier.setBounds (juce::Rectangle<int> (18, 18).withPosition (visual->getRight() - 22, visual->getY() + 4));
            area.removeFromTop (gap);
            if (toolbar != nullptr)
            {
                toolbar->setBounds (area.removeFromTop (toolbarHeight));
                area.removeFromTop (6);
            }
            if (shaperMode)
            {
                auto row = area.removeFromTop (selectorHeight);
                const int third = row.getWidth() / 3;
                for (auto& selector : selectors)
                    selector->setBounds (row.removeFromLeft (third).reduced (3, 0));
                area.removeFromTop (6);
            }
            layoutRow (primaryRow, area.removeFromTop (cellHeight));
        }

        std::vector<std::unique_ptr<ValueSelector>> selectors;
        bool shaperMode = false, expanded = false;
        ShaperTool tool;
        MagnifierButton magnifier;
        std::unique_ptr<ShaperToolbar> toolbar;
        juce::ParameterAttachment customAttachment;
    };

    //==========================================================================
    /** ECHO: the machine beside the title, its repeats, then FREE / SYNC and the stereo
        picture, then TIME (a note value when synced, ms when free), FEEDBACK, TONE, AGE. */
    class EchoPanel final : public MacroPanel
    {
    public:
        explicit EchoPanel (OspAudioProcessor& p) : MacroPanel (p, "ECHO", 286, 86, design::colour::Macro::echo)
        {
            mode ("echo.type", "Tape echo or bucket-brigade (BBD) delay");
            setVisual (std::make_unique<EchoVisual> (p));
            sync = std::make_unique<SegmentedControl> (*p.parameters.getParameter ("echo.sync"), juce::StringArray { "FREE", "SYNC" });
            sync->setAccent (accent);
            sync->setTooltip ("Free time in ms, or a note value synced to the host tempo");
            sync->onChange = [this] (int) { build(); };
            addAndMakeVisible (*sync);
            stereo = std::make_unique<SegmentedControl> (*p.parameters.getParameter ("echo.stereo"), juce::StringArray { "MONO", "PING-PONG", "WIDE" });
            stereo->setAccent (accent);
            stereo->setTooltip ("In the centre, alternating left and right, or two heads a little apart");
            addAndMakeVisible (*stereo);
            build();
        }

        juce::Point<int> cardSize() const override
        {
            return { panelWidth, headerTop + visualHeight + gap + tabsHeight + 6 + cellHeight + bottom };
        }

    private:
        void build()
        {
            knobs.clear();
            primaryRow.clear();
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            if (sync->selected() == 1)
                knob (false, "echo.division", "TIME", [] (double v) { return juce::String (shaping::echoDivisionName (juce::roundToInt (v))); });
            else
                knob (false, "echo.time", "TIME", [] (double v) { return format::milliseconds (v); });
            knob (false, "echo.feedback", "FEEDBACK", percent);
            knob (false, "echo.tone", "TONE", percent);
            knob (false, "echo.age", "AGE", percent);
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            layoutHeader();
            visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (gap);
            auto row = area.removeFromTop (tabsHeight);
            sync->setBounds (row.removeFromLeft (row.getWidth() * 2 / 5).withTrimmedRight (4));
            stereo->setBounds (row.withTrimmedLeft (4));
            area.removeFromTop (6);
            layoutRow (primaryRow, area.removeFromTop (cellHeight));
        }

        std::unique_ptr<SegmentedControl> sync, stereo;
    };

    //==========================================================================
    /** NEW TAKES: re-rolls every note's takes (a new, equally spread set). */
    class NewTakesButton final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit NewTakesButton (juce::RangedAudioParameter& p) : parameter (p)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setWantsKeyboardFocus (true);
            setTitle ("New takes");
            setTooltip ("New takes: roll a new set of takes for every note");
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const float h = r.getHeight();
            const float alpha = isEnabled() ? 1.0f : 0.4f;
            g.setColour (design::colour::textSecondary.withMultipliedAlpha (alpha));
            g.setFont (fonts::make (0.27f * h, fonts::Weight::regular, 0.05f));
            g.drawText ("NEW", r.removeFromTop (0.36f * h), juce::Justification::centred, false);
            const auto box = r.reduced (1.0f, 1.0f);
            const bool hot = isEnabled() && (isMouseOver() || hasKeyboardFocus (false));
            design::draw::button (g, box, 0.2f * box.getHeight(), pressed, hot, design::colour::macro (design::colour::Macro::life));
            icons::draw (g, icons::Kind::loop, box.withSizeKeepingCentre (0.6f * box.getHeight(), 0.6f * box.getHeight()),
                         design::colour::text.withAlpha (0.75f * alpha), 1.2f);
        }

        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override { repaint(); }
        void mouseDown (const juce::MouseEvent&) override { pressed = true; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            pressed = false;
            repaint();
            if (getLocalBounds().contains (e.getPosition()))
                reroll();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key.isKeyCode (juce::KeyPress::returnKey) || key.isKeyCode (juce::KeyPress::spaceKey))
                return reroll(), true;
            return false;
        }

    private:
        void reroll()
        {
            const auto& range = parameter.getNormalisableRange();
            const float now = parameter.convertFrom0to1 (parameter.getValue());
            const float next = now + 1.0f > range.end ? range.start : now + 1.0f;
            parameter.beginChangeGesture();
            parameter.setValueNotifyingHost (parameter.convertTo0to1 (next));
            parameter.endChangeGesture();
        }

        juce::RangedAudioParameter& parameter;
        bool pressed = false;
    };

    /** LIFE: the mode beside the title, the cloud of performances, then how it varies
        (CHARACTER) and whether each note keeps a fixed set of TAKES (round robins: how
        many, in which ORDER, NEW for another set), then PITCH, TONE and ATTACK. */
    class LifePanel final : public MacroPanel
    {
    public:
        explicit LifePanel (OspAudioProcessor& p) : MacroPanel (p, "LIFE", 268, 66, design::colour::Macro::life)
        {
            mode ("life.mode", "Natural: subtle; Loose: wider; Fray: now and then a note strays");
            setVisual (std::make_unique<LifeVisual> (p));
            auto& state = p.parameters;
            const std::pair<const char*, const char*> keys[] = {
                { "life.character", "Character: how performances vary. Auto reads the sample; Pluck, Synth and Drum use round-robin models" },
                { "life.takes", "Takes: endless (every note new) or a fixed set of round robins per note" },
                { "life.takeOrder", "Order: cycle through the takes, or pick at random (never the same twice in a row)" } };
            const char* captions[] = { "CHARACTER", "TAKES", "ORDER" };
            for (int i = 0; i < 3; ++i)
            {
                selectors.push_back (std::make_unique<ValueSelector> (*state.getParameter (keys[i].first), captions[i]));
                selectors.back()->setTooltip (keys[i].second);
                addAndMakeVisible (*selectors.back());
            }
            newTakes = std::make_unique<NewTakesButton> (*state.getParameter ("life.takesSeed"));
            addAndMakeVisible (*newTakes);
            // ORDER and NEW only matter with a fixed set of takes.
            selectors[1]->onChange = [this] (int) { updateTakes(); };
            updateTakes();
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            knob (false, "life.pitch", "PITCH", [] (double v) { return juce::String (v, 1) + " c"; });
            knob (false, "life.tone", "TONE", percent);
            knob (false, "life.attack", "ATTACK", percent);
        }

        static constexpr int selectorHeight = 34;

        juce::Point<int> cardSize() const override
        {
            return { panelWidth, headerTop + visualHeight + gap + selectorHeight + 6 + cellHeight + bottom };
        }

    private:
        void updateTakes()
        {
            const bool fixed = selectors[1]->selected() > 0;
            selectors[2]->setEnabled (fixed);
            selectors[2]->setAlpha (fixed ? 1.0f : 0.45f);
            newTakes->setEnabled (fixed);
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            layoutHeader();
            visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (gap);
            auto row = area.removeFromTop (selectorHeight);
            // CHARACTER gets the widest key; NEW is a small square-ish key.
            const int newWidth = 34;
            newTakes->setBounds (row.removeFromRight (newWidth).reduced (2, 0));
            const int unit = row.getWidth() / 10;
            selectors[0]->setBounds (row.removeFromLeft (4 * unit).reduced (2, 0));
            selectors[1]->setBounds (row.removeFromLeft (3 * unit).reduced (2, 0));
            selectors[2]->setBounds (row.reduced (2, 0));
            area.removeFromTop (6);
            layoutRow (primaryRow, area.removeFromTop (cellHeight));
        }

        std::vector<std::unique_ptr<ValueSelector>> selectors;
        std::unique_ptr<NewTakesButton> newTakes;
    };

    //==========================================================================
    // REIMAGINED: each mode's own picture of what it does to this layer's recording, drawn
    // from the recording's analysis (tape, harmonic frames), its waveform overview and the
    // live settings, in the aurora palette biased per mode.
    class ReimaginedVisual final : public Visual
    {
    public:
        ReimaginedVisual (OspAudioProcessor& p, int l) : processor (p), layer (l) {}

        void modeChanged() { transition(); }

    private:
        bool native() const override { return true; }
        // Stability, travel and the heads move; MIRAGE's picture is still, so it is drawn
        // again only when what it shows changes (the popover otherwise repaints at 30 fps).
        bool animates() const override { return reimagined::modeFromIndex (choice ("mode")) != ReimaginedMode::mirage; }
        bool changed() override
        {
            const auto inst = processor.currentInstrument (layer);
            const auto* analysis = inst != nullptr && inst->model != nullptr ? inst->model->reimagined.get() : nullptr;
            return watch.differs ({ static_cast<double> (choice ("mode")), amount(), number ("mirage.clock"), number ("mirage.filter"),
                                    static_cast<double> (choice ("mirage.tone")), static_cast<double> (reinterpret_cast<std::uintptr_t> (analysis)) });
        }

        float number (const char* name) const
        {
            if (auto* p = processor.parameters.getParameter (OspAudioProcessor::reimaginedModeParameterId (layer, name)))
                return p->getValue();   // 0..100 % as 0..1
            return 0.5f;
        }
        int choice (const char* name) const
        {
            if (auto* p = processor.parameters.getParameter (OspAudioProcessor::reimaginedModeParameterId (layer, name)))
                return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
            return 0;
        }
        float amount() const
        {
            if (auto* p = processor.parameters.getParameter (OspAudioProcessor::reimaginedParameterId (layer)))
                return p->getValue();
            return 0.0f;
        }
        static double now() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
        static juce::Colour aurora (float t) { return design::colour::reimagined (juce::jlimit (0.0f, 1.0f, t)); }

        /** The recording's loudness over its length (0..1), sampled at `n` points. */
        std::vector<float> envelope (const LoadedInstrument* inst, int n) const
        {
            std::vector<float> out (static_cast<std::size_t> (n), 0.0f);
            if (inst == nullptr || inst->peakRms.empty())
            {
                for (int i = 0; i < n; ++i)
                {
                    const float x = static_cast<float> (i) / static_cast<float> (n - 1);
                    out[static_cast<std::size_t> (i)] = std::min (1.0f, 12.0f * x) * (0.75f + 0.25f * std::cos (6.0f * x));
                }
                return out;
            }
            const auto& rms = inst->peakRms;
            float peak = 1.0e-6f;
            for (float v : rms)
                peak = std::max (peak, v);
            for (int i = 0; i < n; ++i)
            {
                const auto at = static_cast<std::size_t> (std::min<double> (rms.size() - 1, static_cast<double> (i) / (n - 1) * (rms.size() - 1)));
                out[static_cast<std::size_t> (i)] = rms[at] / peak;
            }
            return out;
        }

        void status (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text) const
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.setFont (fonts::make (8.5f, fonts::Weight::regular, 0.08f));
            g.drawText (text, r.reduced (8.0f, 5.0f), juce::Justification::topLeft, false);
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            const auto inst = processor.currentInstrument (layer);
            const auto* model = inst != nullptr ? inst->model.get() : nullptr;
            const auto* analysis = model != nullptr ? model->reimagined.get() : nullptr;
            const auto plot = r.reduced (10.0f, 8.0f);
            switch (reimagined::modeFromIndex (choice ("mode")))
            {
                case ReimaginedMode::kaleidoscope: kaleidoscope (g, plot, inst.get()); break;
                case ReimaginedMode::tapeFrame:
                    tapeFrame (g, plot, inst.get(), analysis);
                    if (model != nullptr && analysis == nullptr)
                        status (g, r, juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"));
                    else if (analysis != nullptr && ! analysis->tape.ready)
                        status (g, r, "PLAYS THE RECORDING");
                    break;
                case ReimaginedMode::toybox: toybox (g, plot, inst.get()); break;
                case ReimaginedMode::mosaic:
                    mosaic (g, plot, inst.get(), analysis);
                    if (model != nullptr && analysis == nullptr)
                        status (g, r, juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"));
                    else if (analysis != nullptr && ! analysis->mosaic.ready)
                        status (g, r, "NO STABLE PITCH: PLAYS THE RECORDING");
                    break;
                case ReimaginedMode::mirage: mirage (g, plot, analysis); break;
            }
        }

        // KALEIDOSCOPE: one source form refracting into related traces. They overlap at low
        // amounts and part at high ones; FOCUS bends their shape away from the source, SPREAD
        // sets how many there are and how far apart.
        void kaleidoscope (juce::Graphics& g, juce::Rectangle<float> r, const LoadedInstrument* inst) const
        {
            const float a = amount(), focus = number ("kaleidoscope.focus"), spread = number ("kaleidoscope.spread");
            constexpr int n = 120;
            // The source's form, smoothed (a refraction of its shape, not of every grain).
            const auto raw = envelope (inst, n);
            std::vector<float> env (raw.size());
            for (int i = 0; i < n; ++i)
            {
                float sum = 0.0f;
                int count = 0;
                for (int j = std::max (0, i - 4); j <= std::min (n - 1, i + 4); ++j, ++count)
                    sum += raw[static_cast<std::size_t> (j)];
                env[static_cast<std::size_t> (i)] = sum / static_cast<float> (count);
            }
            const int traces = 3 + juce::roundToInt (2.0f * spread);
            const float h = 0.34f * r.getHeight();
            const float centreY = r.getCentreY() + 0.16f * r.getHeight();   // the form sits low, refracting upwards
            const float abstract = a * (0.25f + 0.75f * focus);
            const auto t = static_cast<float> (now());
            for (int k = traces - 1; k >= 0; --k)
            {
                const float o = static_cast<float> (k) - 0.5f * static_cast<float> (traces - 1);
                const float rel = traces > 1 ? o / (0.5f * static_cast<float> (traces - 1)) : 0.0f;   // -1..1
                const float shift = rel * a * (0.3f + 0.7f * spread) * 0.3f * r.getHeight();
                juce::Path line;
                for (int i = 0; i < n; ++i)
                {
                    const float x = static_cast<float> (i) / (n - 1);
                    const float ripple = std::sin (x * (7.0f + 3.0f * static_cast<float> (k)) + 0.5f * t + static_cast<float> (k)) * 0.5f + 0.5f;
                    const float v = env[static_cast<std::size_t> (i)] * (1.0f - 0.45f * abstract) + 0.45f * abstract * ripple * env[static_cast<std::size_t> (i)];
                    const juce::Point<float> pt (r.getX() + x * r.getWidth(), centreY + shift - v * h);
                    if (i == 0)
                        line.startNewSubPath (pt);
                    else
                        line.lineTo (pt);
                }
                const bool centre = std::abs (rel) < 0.01f;
                const float hue = 0.5f + 0.5f * rel * (0.12f + 0.88f * a);
                g.setColour (aurora (hue).withAlpha (centre ? 0.95f : 0.3f + 0.45f * a));
                g.strokePath (line, juce::PathStrokeType (centre ? 1.6f : 1.2f, juce::PathStrokeType::curved));
            }
            // The source itself, faintly mirrored below: what every trace refracts.
            juce::Path base;
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getX() + static_cast<float> (i) / (n - 1) * r.getWidth();
                const float y = centreY + 0.4f * h * env[static_cast<std::size_t> (i)];
                i == 0 ? base.startNewSubPath (x, y) : base.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.strokePath (base, juce::PathStrokeType (1.0f));
        }

        // TAPE FRAME: the finite frame of tape. FRAME sets its length, AGE darkens and softens
        // what comes late, STABILITY moves the trace, the amount brings in faint ghost passes.
        void tapeFrame (juce::Graphics& g, juce::Rectangle<float> r, const LoadedInstrument* inst, const ReimaginedAnalysis* analysis) const
        {
            const float a = amount(), age = number ("tapeFrame.age"), stab = number ("tapeFrame.stability");
            const auto frame = static_cast<TapeFrameLength> (juce::jlimit (0, 2, choice ("tapeFrame.frame")));
            const double frameSec = reimagined::frameSeconds (frame);
            const double mech = 0.3 * reimagined::ramp (a, 0.0, 0.3) + 0.7 * reimagined::ramp (a, 0.3, 0.85);
            const auto ghost = static_cast<float> (reimagined::ramp (a, 0.55, 0.95));
            const float runout = static_cast<float> (reimagined::ramp (a, 0.25, 0.6));
            const float x0 = r.getX() + 4.0f;
            const float width = (r.getWidth() - 8.0f) * static_cast<float> (frameSec / 11.4);
            const float h = 0.4f * r.getHeight();
            const auto t = static_cast<float> (now());
            // The tape's loudness along its length (the analysis), else the recording's.
            constexpr int n = 110;
            std::vector<float> e (static_cast<std::size_t> (n));
            if (analysis != nullptr && analysis->tape.ready && analysis->tape.lengthFrames > 0.0)
            {
                const double seconds = analysis->tape.lengthFrames / std::max (1.0, analysis->tape.sampleRate);
                for (int i = 0; i < n; ++i)
                {
                    const double at = frameSec * i / (n - 1) / seconds * ReimaginedAnalysis::TapeFrame::overviewSize;
                    e[static_cast<std::size_t> (i)] = at < ReimaginedAnalysis::TapeFrame::overviewSize ? analysis->tape.energy[static_cast<std::size_t> (at)] : 0.0f;
                }
            }
            else
                e = envelope (inst, n);
            auto band = [&] (float dx, float dy, float wobble) {
                juce::Path top;
                for (int i = 0; i < n; ++i)
                {
                    const float u = static_cast<float> (i) / (n - 1);
                    const float fade = 1.0f - runout * juce::jlimit (0.0f, 1.0f, (u - 0.92f) / 0.08f);
                    const float y = r.getCentreY() + dy + wobble * std::sin (6.2832f * (u * 3.0f + 0.6f * t)) - e[static_cast<std::size_t> (i)] * h * fade;
                    const float x = x0 + dx + u * width;
                    i == 0 ? top.startNewSubPath (x, y) : top.lineTo (x, y);
                }
                return top;
            };
            const float wobble = static_cast<float> (2.5 * mech * (0.1 + 0.9 * stab));
            for (int k = 1; k >= 0; --k)
                if (ghost > 0.01f)
                {
                    g.setColour (aurora (0.82f).withAlpha (0.55f * ghost));
                    g.strokePath (band (6.0f + 5.0f * static_cast<float> (k), k == 0 ? -2.0f : 2.0f, 1.4f * wobble), juce::PathStrokeType (1.0f));
                }
            auto main = band (0.0f, 0.0f, wobble);
            juce::Path fill (main);
            fill.lineTo (x0 + width, r.getCentreY());
            fill.lineTo (x0, r.getCentreY());
            fill.closeSubPath();
            // Warm at the start, darker and softer late on as AGE grows.
            juce::ColourGradient warm (aurora (0.06f).withAlpha (0.6f), x0, 0.0f, aurora (0.2f).withAlpha (0.6f * (1.0f - 0.7f * age)), x0 + width, 0.0f, false);
            g.setGradientFill (warm);
            g.fillPath (fill);
            juce::ColourGradient edge (aurora (0.1f), x0, 0.0f, aurora (0.18f).withAlpha (1.0f - 0.65f * age), x0 + width, 0.0f, false);
            g.setGradientFill (edge);
            g.strokePath (main, juce::PathStrokeType (1.0f + 0.8f * age));
            // The tape: its head (the recording's start) and where it ends.
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRect (x0 - 1.0f, r.getCentreY() - h - 2.0f, 1.2f, h + 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            g.drawHorizontalLine (juce::roundToInt (r.getCentreY()), x0, x0 + width);
            const float end = x0 + width;
            for (float y = r.getCentreY() - h; y < r.getCentreY() + 4.0f; y += 4.0f)
                g.drawVerticalLine (juce::roundToInt (end), y, y + 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.setFont (fonts::make (8.5f, fonts::Weight::regular, 0.06f));
            g.drawText (juce::String (frameSec, 1) + " s", juce::Rectangle<float> (end - 40.0f, r.getBottom() - 12.0f, 40.0f, 12.0f), juce::Justification::centredRight, false);
        }

        // TOYBOX: the recording as a low-resolution memory (DIGITAL: fewer, coarser steps) and
        // the head's path through it (PLAY: forward, turning, irregular; MOTION: how often).
        void toybox (juce::Graphics& g, juce::Rectangle<float> r, const LoadedInstrument* inst) const
        {
            const float a = amount(), motionSetting = number ("toybox.motion"), digital = number ("toybox.digital");
            const int play = choice ("toybox.play");
            const auto early = static_cast<float> (reimagined::ramp (a, 0.0, 0.3));
            const float dig = juce::jlimit (0.0f, 1.0f, (0.25f + 0.75f * digital) * (0.6f * early + 0.4f * static_cast<float> (reimagined::ramp (a, 0.3, 1.0))));
            const int blocks = juce::roundToInt (72.0f - 56.0f * dig);
            const float levels = 28.0f - 22.0f * dig;
            const auto env = envelope (inst, blocks);
            const float h = 0.36f * r.getHeight();
            const float bw = r.getWidth() / static_cast<float> (blocks);
            const float mid = r.getCentreY() - 6.0f;
            for (int i = 0; i < blocks; ++i)
            {
                const float v = std::round (env[static_cast<std::size_t> (i)] * levels) / levels;
                const float x = r.getX() + static_cast<float> (i) * bw;
                const float u = static_cast<float> (i) / static_cast<float> (blocks);
                g.setColour (aurora (0.05f + 0.45f * u).withAlpha (0.75f));
                g.fillRect (x + 0.5f, mid - v * h, std::max (1.0f, bw - 1.0f), 2.0f * v * h);
            }
            // The head's path, under the memory.
            const auto motion = static_cast<float> (motionSetting * reimagined::ramp (a, 0.25, 0.75));
            const float y0 = r.getBottom() - 9.0f;
            const float leg = r.getWidth() * (0.4f - 0.33f * motion);
            juce::Path path;
            std::vector<float> turns;
            float x = r.getX(), dir = 1.0f;
            path.startNewSubPath (x, y0);
            Prng rng (0x746f79ull + static_cast<std::uint64_t> (layer));
            int row = 0;
            while (x < r.getRight() && path.getLength() < 6.0f * r.getWidth())
            {
                if (play == 0 || motion < 1.0e-3f)
                {
                    x = r.getRight();
                    path.lineTo (x, y0);
                    break;
                }
                float len = leg * (dir > 0.0f ? 1.3f : 1.0f);
                if (play == 2)
                    len = leg * static_cast<float> (rng.uniform (0.4, 1.6));
                x = juce::jlimit (r.getX(), r.getRight(), x + dir * len);
                const float y = y0 - 3.0f * static_cast<float> (row & 1);
                path.lineTo (x, y);
                turns.push_back (x);
                ++row;
                if (play == 2 && rng.nextDouble() < 0.25)
                    x = juce::jlimit (r.getX(), r.getRight(), x + leg * static_cast<float> (rng.uniform (0.3, 0.8)));   // skip ahead
                else
                    dir = -dir;
                path.lineTo (x, y0 - 3.0f * static_cast<float> (row & 1));
            }
            g.setColour (aurora (0.5f).withAlpha (0.85f));
            g.strokePath (path, juce::PathStrokeType (1.1f));
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            for (float tx : turns)
                g.fillRect (tx - 0.5f, y0 - 6.0f, 1.0f, 7.0f);
            // A small light running along the path.
            const auto length = path.getLength();
            if (length > 1.0f)
            {
                const auto at = path.getPointAlongPath (std::fmod (static_cast<float> (now()) * 45.0f, length));
                g.setColour (aurora (0.35f));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (at));
            }
        }

        // MOSAIC: the partials of the harmonic frame being played (DETAIL: how many), moving
        // through the recording's frames (MOTION), the residual noise as a cloud (MODEL), the
        // source's waveform behind it at low amounts.
        void mosaic (juce::Graphics& g, juce::Rectangle<float> r, const LoadedInstrument* inst, const ReimaginedAnalysis* analysis) const
        {
            const float a = amount(), detail = number ("mosaic.detail"), motion = number ("mosaic.motion");
            const bool textured = choice ("mosaic.model") == 1;
            const int count = juce::roundToInt (8.0f + 40.0f * detail);
            const auto* m = analysis != nullptr && analysis->mosaic.ready ? &analysis->mosaic : nullptr;
            // The source behind, fading as the reconstruction takes over.
            if (a < 0.99f)
            {
                const auto env = envelope (inst, 100);
                juce::Path wave;
                for (int i = 0; i < 100; ++i)
                {
                    const float x = r.getX() + static_cast<float> (i) / 99.0f * r.getWidth();
                    const float y = r.getCentreY() - 0.35f * r.getHeight() * env[static_cast<std::size_t> (i)];
                    i == 0 ? wave.startNewSubPath (x, y) : wave.lineTo (x, y);
                }
                g.setColour (juce::Colours::white.withAlpha (0.35f * (1.0f - a)));
                g.strokePath (wave, juce::PathStrokeType (1.0f));
            }
            // Travel through the frames, as the engine does (a ping-pong over the body).
            double pos = 0.0;
            int frames = 1;
            if (m != nullptr)
            {
                frames = static_cast<int> (m->frames.size());
                const double travel = motion * (0.4 + 0.6 * reimagined::ramp (a, 0.55, 0.85));
                const double span = std::max (1.0, static_cast<double> (frames - 1 - m->bodyFrame));
                const double phase = std::fmod (now() * 3.0, 2.0 * span);
                const double wander = m->bodyFrame + (phase <= span ? phase : 2.0 * span - phase);
                pos = m->stableFrame + (wander - m->stableFrame) * travel;
            }
            auto partial = [&] (int h) -> float {
                if (m == nullptr)
                    return 0.6f / static_cast<float> (h);
                const int last = frames - 1;
                const int i = juce::jlimit (0, std::max (0, last - 1), static_cast<int> (pos));
                const auto f = static_cast<float> (pos - i);
                const auto& a0 = m->frames[static_cast<std::size_t> (i)].partial;
                const auto& a1 = m->frames[static_cast<std::size_t> (std::min (i + 1, last))].partial;
                return a0[static_cast<std::size_t> (h - 1)] * (1.0f - f) + a1[static_cast<std::size_t> (h - 1)] * f;
            };
            float peak = 1.0e-9f;
            for (int hh = 1; hh <= count; ++hh)
                peak = std::max (peak, partial (hh));
            // The residual: a quiet cloud above the partials (TEXTURED keeps it).
            const float cloud = (textured ? 1.0f : 0.2f) * (m != nullptr ? std::min (1.0f, 0.3f + 3.0f * static_cast<float> (m->residualShare)) : 0.5f);
            Prng dots (0x6d6f73ull);
            const float drift = static_cast<float> (std::fmod (now() * 4.0, 1000.0));
            g.setColour (aurora (0.9f).withAlpha (0.28f * cloud * (0.4f + 0.6f * a)));
            for (int i = 0; i < juce::roundToInt (140.0f * cloud); ++i)
            {
                const float x = r.getX() + std::fmod (static_cast<float> (dots.nextDouble()) * r.getWidth() + drift * static_cast<float> (dots.uniform (0.2, 1.0)), r.getWidth());
                const float y = r.getY() + static_cast<float> (dots.nextDouble()) * 0.55f * r.getHeight();
                g.fillRect (x, y, 1.2f, 1.2f);
            }
            const float slot = r.getWidth() / static_cast<float> (count);
            for (int hh = 1; hh <= count; ++hh)
            {
                const float db = 20.0f * std::log10 (std::max (1.0e-9f, partial (hh) / peak));
                const float v = juce::jlimit (0.0f, 1.0f, 1.0f + db / 60.0f);
                const float x = r.getX() + (static_cast<float> (hh) - 0.5f) * slot;
                const float top = r.getBottom() - 6.0f - v * 0.82f * (r.getHeight() - 6.0f);
                g.setColour (aurora (0.62f + 0.38f * static_cast<float> (hh) / static_cast<float> (count)).withAlpha (0.35f + 0.6f * a));
                g.fillRect (x - 0.3f * slot, top, std::max (1.0f, 0.6f * slot), r.getBottom() - 6.0f - top);
            }
            // The frames, with where the spectrum is now.
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            for (int k = 0; k < frames; ++k)
                g.fillRect (r.getX() + r.getWidth() * static_cast<float> (k) / std::max (1, frames - 1) - 0.5f, r.getBottom() - 3.0f, 1.0f, 3.0f);
            if (m != nullptr)
            {
                g.setColour (aurora (0.75f));
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ r.getX() + r.getWidth() * static_cast<float> (pos / std::max (1, frames - 1)), r.getBottom() - 1.5f }));
            }
        }

        // MIRAGE: a cycle of the sound held at the sample clock (CLOCK: coarser; the clock is
        // slower for low notes, at the left, than for high ones), under the low-pass's response
        // (FILTER: lower and more resonant, TONE: darker or more open).
        void mirage (juce::Graphics& g, juce::Rectangle<float> r, const ReimaginedAnalysis* analysis) const
        {
            const float a = amount(), clockSetting = number ("mirage.clock"), filter = number ("mirage.filter");
            const bool open = choice ("mirage.tone") == 1;
            const float clock = (0.3f + 0.7f * clockSetting) * (0.65f * static_cast<float> (reimagined::ramp (a, 0.0, 0.35)) + 0.35f * static_cast<float> (reimagined::ramp (a, 0.35, 1.0)));
            std::array<float, 8> harmonics { 1.0f, 0.5f, 0.33f, 0.25f, 0.2f, 0.16f, 0.14f, 0.12f };
            if (analysis != nullptr && analysis->mosaic.ready)
            {
                const auto& f = analysis->mosaic.frames[static_cast<std::size_t> (analysis->mosaic.stableFrame)];
                const float p0 = std::max (1.0e-9f, f.partial[0]);
                for (std::size_t h = 0; h < harmonics.size(); ++h)
                    harmonics[h] = f.partial[h] / p0;
            }
            auto wave = [&] (float u) {
                float v = 0.0f, norm = 0.0f;
                for (std::size_t h = 0; h < harmonics.size(); ++h)
                {
                    v += harmonics[h] * std::sin (6.2832f * static_cast<float> (h + 1) * u * 2.5f);
                    norm += std::abs (harmonics[h]);
                }
                return v / std::max (1.0e-6f, norm);
            };
            const float h = 0.36f * r.getHeight();
            const float mid = r.getCentreY() - 4.0f;
            juce::Path smooth, held;
            for (int i = 0; i <= 160; ++i)
            {
                const float u = static_cast<float> (i) / 160.0f;
                const float x = r.getX() + u * r.getWidth(), y = mid - h * wave (u);
                i == 0 ? smooth.startNewSubPath (x, y) : smooth.lineTo (x, y);
            }
            g.setColour (juce::Colours::white.withAlpha (0.14f));
            g.strokePath (smooth, juce::PathStrokeType (1.0f));
            // Zero-order hold: wide steps at the left (a low note's slow clock), finer at the right.
            float x = r.getX();
            held.startNewSubPath (x, mid - h * wave (0.0f));
            while (x < r.getRight())
            {
                const float u = (x - r.getX()) / r.getWidth();
                const float stepWidth = (1.5f + 13.0f * clock) * (1.7f - 1.3f * u);
                const float v = mid - h * wave (u);
                const float next = std::min (r.getRight(), x + stepWidth);
                held.lineTo (x, v);
                held.lineTo (next, v);
                x = next;
            }
            g.setColour (aurora (0.8f).brighter (0.3f).withAlpha (0.9f));
            g.strokePath (held, juce::PathStrokeType (1.1f));
            // The filter's response: a dark silhouette with its resonance in rust and amber.
            const float body = static_cast<float> (reimagined::ramp (a, 0.15, 0.7));
            const float cutoff = juce::jlimit (0.12f, 0.95f, 0.95f - body * (0.55f + 0.25f * filter - (open ? 0.22f : 0.0f)));
            const float res = body * (0.1f + 0.55f * filter);
            juce::Path response;
            const float base = r.getBottom();
            for (int i = 0; i <= 100; ++i)
            {
                const float u = static_cast<float> (i) / 100.0f;
                const float ratio = std::pow (2.0f, 8.0f * (u - cutoff));   // octaves around the cutoff
                const float mag = 1.0f / std::sqrt (1.0f + std::pow (ratio, 8.0f)) * (1.0f + 2.5f * res * std::exp (-std::pow ((u - cutoff) * 14.0f, 2.0f)));
                const float y = base - 0.8f * r.getHeight() * juce::jlimit (0.0f, 1.25f, mag) * 0.75f;
                const float px = r.getX() + u * r.getWidth();
                i == 0 ? response.startNewSubPath (px, y) : response.lineTo (px, y);
            }
            juce::Path area (response);
            area.lineTo (r.getRight(), base);
            area.lineTo (r.getX(), base);
            area.closeSubPath();
            g.setColour (juce::Colour (0xff10161c).withAlpha (open ? 0.25f : 0.4f));
            g.fillPath (area);
            juce::ColourGradient rust (aurora (0.0f), r.getX(), 0.0f, aurora (0.18f), r.getRight(), 0.0f, false);
            g.setGradientFill (rust);
            g.strokePath (response, juce::PathStrokeType (1.4f));
        }

        OspAudioProcessor& processor;
        int layer;
        Watch watch;
    };

    /** REIMAGINED: the mode beside the title, its picture, then that mode's own two or three
        settings (continuous ones as small knobs in the aurora at the layer's amount, stepped
        ones as text selectors). Switching modes keeps every mode's values. */
    class ReimaginedPanel final : public MacroPanel, private juce::Timer
    {
    public:
        ReimaginedPanel (OspAudioProcessor& p, int l)
            : MacroPanel (p, "REIMAGINED", 270, 80, design::colour::reimagined (amountOf (p, l))), layer (l)
        {
            const auto modeId = OspAudioProcessor::reimaginedModeParameterId (l, "mode");
            auto& m = mode (modeId.toRawUTF8(), "Reimagined mode: the way this source is reinterpreted");
            m.onChange = [this] (int newMode) {
                build (newMode);
                if (picture != nullptr)
                    picture->modeChanged();
            };
            auto v = std::make_unique<ReimaginedVisual> (p, l);
            picture = v.get();
            setVisual (std::move (v));
            build (m.selected());
            startTimerHz (10);
        }
        ~ReimaginedPanel() override { stopTimer(); }

        juce::Point<int> cardSize() const override { return { panelWidth, headerTop + visualHeight + gap + cellHeight + bottom }; }

    private:
        static float amountOf (OspAudioProcessor& p, int l)
        {
            auto* param = p.parameters.getParameter (OspAudioProcessor::reimaginedParameterId (l));
            return param != nullptr ? param->getValue() : 0.0f;
        }

        void timerCallback() override
        {
            // The sub-settings' arcs follow the aurora at the layer's REIMAGINED amount.
            const auto colour = design::colour::reimagined (amountOf (processor, layer));
            if (colour == accent)
                return;
            accent = colour;
            for (auto& k : knobs)
                k->setArcColour (accent);
            if (modeSelector != nullptr)
                modeSelector->setAccent (accent);
        }

        void build (int newMode)
        {
            knobs.clear();
            primaryRow.clear();
            selectors.clear();
            cells.clear();
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            auto addKnob = [&] (const char* name, const char* caption) {
                const auto pid = OspAudioProcessor::reimaginedModeParameterId (layer, name);
                cells.push_back (&knob (false, pid.toRawUTF8(), caption, percent));
            };
            auto addChoice = [&] (const char* name, const char* caption, const char* tip) {
                auto* param = processor.parameters.getParameter (OspAudioProcessor::reimaginedModeParameterId (layer, name));
                if (param == nullptr)
                    return;
                selectors.push_back (std::make_unique<ValueSelector> (*param, caption));
                selectors.back()->setTooltip (tip);
                addAndMakeVisible (*selectors.back());
                cells.push_back (selectors.back().get());
            };
            switch (reimagined::modeFromIndex (newMode))
            {
                case ReimaginedMode::kaleidoscope:
                    addKnob ("kaleidoscope.focus", "FOCUS");
                    addKnob ("kaleidoscope.spread", "SPREAD");
                    break;
                case ReimaginedMode::tapeFrame:
                    addKnob ("tapeFrame.age", "AGE");
                    addKnob ("tapeFrame.stability", "STABILITY");
                    addChoice ("tapeFrame.frame", "FRAME", "How much tape: short, classic or long");
                    break;
                case ReimaginedMode::toybox:
                    addKnob ("toybox.motion", "MOTION");
                    addKnob ("toybox.digital", "DIGITAL");
                    addChoice ("toybox.play", "PLAY", "Forward, turning back and forth, or irregular");
                    break;
                case ReimaginedMode::mosaic:
                    addKnob ("mosaic.detail", "DETAIL");
                    addKnob ("mosaic.motion", "MOTION");
                    addChoice ("mosaic.model", "MODEL", "Pure: mostly harmonic; Textured: keeps the breath and noise");
                    break;
                case ReimaginedMode::mirage:
                    addKnob ("mirage.clock", "CLOCK");
                    addKnob ("mirage.filter", "FILTER");
                    addChoice ("mirage.tone", "TONE", "Dark or open filter");
                    break;
            }
            for (auto& k : knobs)
                k->setArcColour (accent);
            resized();
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            layoutHeader();
            visual->setBounds (area.removeFromTop (visualHeight));
            area.removeFromTop (gap);
            auto row = area.removeFromTop (cellHeight);
            if (cells.empty())
                return;
            const int cell = std::min (row.getWidth() / static_cast<int> (cells.size()), 84);
            row = row.withSizeKeepingCentre (cell * static_cast<int> (cells.size()), row.getHeight());
            for (auto* c : cells)
            {
                auto slot = row.removeFromLeft (cell);
                if (dynamic_cast<ValueSelector*> (c) != nullptr)
                    c->setBounds (slot.withSizeKeepingCentre (cell - 6, 36));
                else
                    c->setBounds (slot);
            }
        }

        int layer;
        ReimaginedVisual* picture = nullptr;
        std::vector<std::unique_ptr<ValueSelector>> selectors;
        std::vector<juce::Component*> cells;
    };
}

std::unique_ptr<MiniPanel> createReimaginedPopup (OspAudioProcessor& processor, int layer)
{
    return std::make_unique<ReimaginedPanel> (processor, layer);
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
            return std::make_unique<LifePanel> (processor);
        }
        case MacroPopup::dynamics:
        {
            // ATTACK and RELEASE are the instrument's envelope (beside the macros).
            auto popup = std::make_unique<MacroPanel> (processor, "DYNAMICS", 232, 70, design::colour::Macro::dynamics);
            popup->mode ("dynamics.curve", "Velocity curve: soft reaches loud easily, hard needs a firm touch");
            popup->setVisual (std::make_unique<DynamicsVisual> (processor));
            popup->knob (false, "velocityRange", "RANGE", [] (double v) { return juce::String (v, 0) + " dB"; });
            popup->knob (false, "dynamics.tone", "TONE", percent);
            return popup;
        }
        case MacroPopup::character:
        {
            auto popup = std::make_unique<MacroPanel> (processor, "CHARACTER", 262, 58, design::colour::Macro::character);
            popup->mode ("character.type", "Filter type");
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
        case MacroPopup::echo:
            return std::make_unique<EchoPanel> (processor);
        case MacroPopup::space:
            break;
    }

    // SPACE v2: the room, its EQ on the display (drag the corners), then its shape:
    // PRE-DELAY, SIZE, DECAY, DAMP on the first row; MOD, WIDTH and the EQ's values below.
    auto popup = std::make_unique<MacroPanel> (processor, "SPACE", 300, 92, design::colour::Macro::space);
    popup->mode ("space.type", "Room, hall, plate or spring");
    auto& types = popup->tabs ("space.type", { "ROOM", "HALL", "PLATE", "SPRING" }, "Room, hall (after the Berlin concert halls), plate or spring");
    popup->setVisual (std::make_unique<SpaceVisual> (state));
    auto* typeParam = state.getParameter ("space.type");
    popup->knob (false, "space.preDelay", "PRE-DELAY", ms);
    popup->knob (false, "space.size", "SIZE", percent);
    // The readout shows the decay the chosen type actually uses (each type has its own range).
    auto& decay = popup->knob (false, "space.decay", "DECAY", [typeParam] (double v) {
        double lo = 0.2, hi = 8.0;
        if (typeParam != nullptr)
            shaping::decayRange (static_cast<SpaceType> (juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue()))), lo, hi);
        return juce::String (std::clamp (v, lo, hi), 1) + " s";
    });
    popup->knob (false, "space.damping", "DAMP", percent);
    popup->knob (true, "space.modulation", "MOD", percent);
    popup->knob (true, "space.width", "WIDTH", percent);
    popup->knob (true, "space.lowCut", "LOW CUT", [] (double v) { return format::hertz (v); });
    popup->knob (true, "space.highCut", "HIGH CUT", [] (double v) { return format::hertz (v); });
    types.onChange = [&decay] (int) { decay.repaint(); };
    return popup;
}

} // namespace osp::plugin
