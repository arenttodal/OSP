// The five macro popups (redesign §2): each one explains its DSP with a picture drawn
// from the same formulas and parameters the sound uses, plus a few compact controls.
#include "Binding.h"
#include "EngineCard.h"
#include "Design.h"
#include "EffectRack.h"
#include "MacroPopups.h"
#include "RackControls.h"
#include "ShapingPopups.h"
#include "SpacePresets.h"

#include "core/Prng.h"
#include "engine/InstrumentEngine.h"
#include "engine/RhythmicShaper.h"
#include "engine/Shaping.h"
#include "engine/DriveProcessor.h"
#include "engine/EchoDelay.h"
#include "engine/AbyssReverb.h"
#include "engine/BloomReverb.h"
#include "engine/SpaceReverb.h"

#include <cmath>
#include <map>
#include <complex>
#include <numbers>

namespace osp::plugin
{

namespace
{
    constexpr double twoPi = 2.0 * std::numbers::pi;

    float value (juce::AudioProcessorValueTreeState& state, const juce::String& id)
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
            // The effects rack's graph (approved): #1c2528 to #192124, radius 5, edge #343f43.
            const float corner = detailed ? 5.0f : radius;
            if (detailed)
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1c2528), 0.0f, r.getY(), juce::Colour (0xff192124), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, corner);
            }
            else
            {
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1f2626), 0.0f, r.getY(), juce::Colour (0xff1a1e1f), 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, radius);
                g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.3f), 0.0f, r.getY(), juce::Colours::transparentBlack, 0.0f,
                                                         r.getY() + 6.0f, false));
                g.fillRoundedRectangle (r.withHeight (8.0f), radius);
            }
            {
                juce::Graphics::ScopedSaveState state (g);
                g.reduceClipRegion (getLocalBounds().reduced (1));
                if (native())
                    paintVisual (g, r);
                else
                {
                    // Pictures designed for the smaller display draw at their own scale.
                    const float k = pictureScale;
                    g.addTransform (juce::AffineTransform::scale (k));
                    const auto v = juce::Rectangle<float> (r.getWidth() / k, r.getHeight() / k);
                    paintVisual (g, v.reduced (8.0f, 6.0f));
                }
            }
            g.setColour (detailed ? juce::Colour (0xff343f43) : juce::Colour (0xff0b0e10));
            g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);
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
        /** The scale a picture designed for the popovers' display draws at (the effects
            rack's larger display draws it larger: its lines and captions in proportion). */
        void setPictureScale (float k)
        {
            pictureScale = k;
            repaint();
        }
        /** The effects rack's picture: captions with their legends, values beside them, the
            hierarchy of lines made plainer (opt-in; the popovers' pictures as ever). */
        void setDetailed (bool shouldBeDetailed)
        {
            detailed = shouldBeDetailed;
            repaint();
        }
        /** Up to date at once (no fade): what a snapshot without a message loop sees. */
        void settle()
        {
            changed();
            fade = 0.0f;
            repaint();
        }

    protected:
        juce::Colour identity { 0xffe8692a };
        bool detailed = false;
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
        float pictureScale = visualScale;
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
        explicit SpaceVisual (juce::AudioProcessorValueTreeState& s, std::function<double()> hostTempo = {}) : state (s), tempo (std::move (hostTempo))
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
            // (The other engines show as further "types": a change of engine, or of VINTAGE's
            // variant, transitions like a change of room.)
            const int engine = juce::roundToInt (value (state, "space.engine"));
            const bool abyss = engine == 1;
            const int type = engine == 1 ? 4 : (engine == 2 ? 5 + juce::roundToInt (value (state, "space.vintage.variant")) : (engine == 3 ? 9 : juce::roundToInt (value (state, "space.type"))));
            if (type != shownType && shownType >= 0)
                transition();
            shownType = type;
            const bool eqMoved = eqWatch.differs ({ value (state, "space.lowCut"), value (state, "space.highCut") });
            if (watch.differs ({ static_cast<double> (type), value (state, "space.decay"), value (state, "space"), value (state, "space.preDelay"),
                                 value (state, "space.size"), value (state, "space.damping"), value (state, "space.width"), static_cast<double> (getWidth()),
                                 abyss ? value (state, "space.abyss.gravity") : 0.0, abyss ? value (state, "space.abyss.freeze") : 0.0,
                                 abyss ? value (state, "space.abyss.variant") : 0.0,
                                 engine == 3 ? value (state, "space.bloom.sync") : 0.0, engine == 3 ? value (state, "space.bloom.division") : 0.0,
                                 engine == 3 ? value (state, "space.bloom.time") : 0.0, engine == 3 ? value (state, "space.bloom.diffusion") : 0.0,
                                 engine == 3 && tempo != nullptr ? tempo() : 0.0 }))
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
            // ABYSS (SPACE's other engine): no reflections, a tail as long as GRAVITY makes it,
            // swelling when GRAVITY is below 0, held to the edge while FROZEN.
            const int engine = juce::roundToInt (value (state, "space.engine"));
            const bool abyss = engine == 1, vintage = engine == 2, bloom = engine == 3;
            const double gravity = abyss ? 0.01 * value (state, "space.abyss.gravity") : 0.0;
            const auto abyssVariant = static_cast<AbyssVariant> (std::clamp (juce::roundToInt (value (state, "space.abyss.variant")), 0, 3));
            const bool frozen = abyss && value (state, "space.abyss.freeze") >= 0.5;
            // VINTAGE draws as the room its variant is voiced after; BLOOM as a hall's cloud
            // with its repeats over it.
            const int vintageVariant = juce::roundToInt (value (state, "space.vintage.variant"));
            const auto type = abyss || bloom ? SpaceType::hall
                              : (vintage ? (vintageVariant == 1 ? SpaceType::plate : (vintageVariant == 3 ? SpaceType::room : SpaceType::hall))
                                         : static_cast<SpaceType> (std::clamp (juce::roundToInt (value (state, "space.type")), 0, 3)));
            double lo = 0.2, hi = 8.0;
            if (engine == 0)
                shaping::decayRange (type, lo, hi);
            const double bloomDiffusion = bloom ? 0.01 * value (state, "space.bloom.diffusion") : 0.0;
            const double decay = std::clamp (static_cast<double> (value (state, "space.decay")), lo, hi);
            const double amount = 0.85 + 0.15 * std::clamp (static_cast<double> (value (state, "space")) * 0.01, 0.0, 1.0);
            SpaceReverb::Settings settings;
            settings.type = type;
            settings.decaySeconds = decay;
            settings.preDelayMs = value (state, "space.preDelay");
            settings.size = 0.01 * value (state, "space.size");
            settings.damping = 0.01 * value (state, "space.damping");
            settings.width = 0.01 * value (state, "space.width");
            auto portrait = SpaceReverb::portrait (settings);
            if (abyss || bloom)
            {
                portrait.erCount = 0;
                portrait.erLevel = 0.0f;
                portrait.diffusionMs = 12.0;
                portrait.damping = static_cast<float> (std::clamp (0.1 + 0.6 * settings.damping + 0.25 * std::max (0.0, gravity)
                                                                       + (abyss && abyssVariant == AbyssVariant::dark ? 0.35 : 0.0), 0.0, 1.0));
                portrait.width = static_cast<float> (std::clamp (settings.width, 0.0, 1.0));
            }
            // A fixed time axis for the type (its longest DECAY fills it); DECAY sets how far
            // the tail reaches along it, so turning the knob lengthens or shortens the tail.
            // ABYSS: an axis that fits the tail GRAVITY makes (10 s at least), the whole of it
            // while FROZEN.
            const double abyssTail = AbyssReverb::effectiveDecay (decay, gravity, abyssVariant) * 1.25;
            const double span = abyss ? std::max (10.0, 5.0 * std::ceil (abyssTail * 1.1 / 5.0)) : hi * 1.25;
            const double tailEnd = abyss ? (frozen ? span : abyssTail) : decay * 1.25;   // drawn to -75 dB
            const double swell = abyss ? AbyssReverb::windowLength (abyssVariant) * std::sqrt (AbyssReverb::sizeFactor (settings.size)) : 0.0;   // GRAVITY's window
            const float W = bounds.getWidth(), H = bounds.getHeight();
            // Sizes follow the display (the popover's is small): strokes, dots and counts.
            const float k = juce::jlimit (0.3f, 1.0f, H / 261.0f), dot = std::sqrt (k);
            const auto plot = juce::Rectangle<float>::leftTopRightBottom (bounds.getX() + 14.0f, bounds.getY() + 5.0f,
                                                                          bounds.getRight() - 12.0f, bounds.getBottom() - 14.0f);
            const float mid = plot.getCentreY(), half = 0.5f * plot.getHeight();
            auto xAt = [&] (double t) { return plot.getX() + static_cast<float> (t / span) * plot.getWidth(); };

            // Grid: five rows, a line every second (or the step that fits) and four between.
            double step = 2.0;
            for (double c : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 20.0, 30.0, 60.0 })   // (ENDLESS reaches minutes)
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
                if (abyss)
                {
                    // Natural (or held), and below GRAVITY 0 the swell through its window.
                    const double fill = 1.0 - std::exp (-(t - onset) / build);
                    const double natural = frozen ? 0.85 * fill : std::pow (std::max (0.0, 1.0 - t / tailEnd), 1.4) * fill;
                    const double u = t - onset, inverse = AbyssReverb::windowShare (gravity, abyssVariant);
                    const double rise = u < swell ? (u / swell) * (u / swell) : (frozen ? 0.85 : std::pow (std::max (0.0, 1.0 - (u - swell) / tailEnd), 1.4));
                    return ((1.0 - inverse) * natural + inverse * rise) * amount;
                }
                if (bloom)   // (a diffused BLOOM fills in; a clean one is mostly its repeats)
                    return std::pow (std::max (0.0, 1.0 - t / tailEnd), 1.4) * (1.0 - std::exp (-(t - onset) / build)) * amount * (0.25 + 0.75 * bloomDiffusion);
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
                static const juce::Colour approved[] = { juce::Colour (0xffff8a2c), juce::Colour (0xffffa24c), juce::Colour (0xfff5bfa0),
                                                         juce::Colour (0xffe9c8cf), juce::Colour (0xffcdbbd2), juce::Colour (0xffb2aac4), juce::Colour (0xff8fa4b8) };
                // A product with its own SPACE identity (ANDOR ANALOG's spore): the cool half
                // through aged stone and spore instead of rose and lavender.
                static const juce::Colour spore[] = { juce::Colour (0xffff8a2c), juce::Colour (0xffffa24c), juce::Colour (0xfff2c49e),
                                                      juce::Colour (0xffe1d9c3), juce::Colour (0xffcfd0c1), juce::Colour (0xffb0b6a3), juce::Colour (0xff8fa4b8) };
                const auto* stops = design::detail::materials.macros[static_cast<std::size_t> (design::colour::Macro::space)].getAlpha() != 0 ? spore : approved;
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
            Prng rng (Prng::deriveSeed (0x7370616365ull, static_cast<std::uint64_t> (abyss ? 4 : (bloom ? 9 : static_cast<int> (type))), 0));
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
            const int strokes = abyss || bloom ? 0 : (real ? portrait.erCount : 6);   // (ABYSS and BLOOM have no reflections)
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
            if (bloom)
            {
                // BLOOM's repeats where they land (the tempo's, or free), each as loud as the
                // feedback leaves it, softer the more DIFFUSION smears them.
                BloomReverb::Settings b;
                b.sync = value (state, "space.bloom.sync") >= 0.5;
                b.division = juce::roundToInt (value (state, "space.bloom.division"));
                b.timeMs = value (state, "space.bloom.time");
                const double period = BloomReverb::timeSeconds (b, tempo != nullptr ? tempo() : 120.0);
                const double fb = std::min (0.97, std::pow (10.0, -3.0 * period / std::max (0.2, decay)));
                double level = 1.0;
                for (int k = 1; static_cast<double> (k) * period <= span && level > 0.02 && k < 64; ++k)
                {
                    const double t = onset + static_cast<double> (k) * period;
                    const float x = xAt (t), extent = half * static_cast<float> (std::clamp (level, 0.1, 1.0)) * 0.95f;
                    const auto colour = tint (t).withAlpha (static_cast<float> ((1.0 - 0.75 * bloomDiffusion) * std::sqrt (level)));
                    juce::ColourGradient body (colour.withAlpha (0.0f), x, mid - extent, colour.withAlpha (0.0f), x, mid + extent, false);
                    body.addColour (0.15, colour);
                    body.addColour (0.5, colour.brighter (0.25f));
                    body.addColour (0.85, colour);
                    g.setGradientFill (body);
                    g.fillRect (juce::Rectangle<float> (x - 1.1f * dot, mid - extent, 2.2f * dot, 2.0f * extent));
                    level *= fb;
                }
            }
            // The sound itself, arriving: a warm bloom at zero.
            const float x0 = xAt (onset);
            juce::ColourGradient arrival (juce::Colour (0xffff9a3c).withAlpha (0.35f), x0, mid, juce::Colour (0xffff9a3c).withAlpha (0.0f), x0 + 0.06f * W, mid, true);
            g.setGradientFill (arrival);
            g.fillEllipse (juce::Rectangle<float> (0.12f * W, 0.5f * H).withCentre ({ x0, mid }));
        }

        juce::AudioProcessorValueTreeState& state;
        std::function<double()> tempo;   ///< (BLOOM's synced repeats; 120 without it)
        Watch watch, eqWatch;
        int shownType = -1, hover = -1, dragging = -1;
        juce::Image cache;
        bool cacheValid = false;
    };

    //==========================================================================
    // ECHO: the sound and its repeats along time - each repeat a stroke as loud as the
    // feedback leaves it, darker for every pass through the machine; PING-PONG alternates
    // left (up) and right (down), WIDE splits two heads. TAPE's repeats smear (wow and
    // flutter), BBD's speckle (the clock's grain). ASCEND's repeats are paler and carry their
    // pitch (+12, +24 ..), speckled by its CLOCK.
    class EchoVisual final : public Visual
    {
    public:
        explicit EchoVisual (InstrumentHost& p) : processor (p), state (p.apvts()) {}

    private:
        bool native() const override { return true; }
        bool changed() override
        {
            return watch.differs ({ value (state, "echo.type"), value (state, "echo.sync"), value (state, "echo.division"), value (state, "echo.time"),
                                    value (state, "echo.feedback"), value (state, "echo.tone"), value (state, "echo.age"), value (state, "echo.stereo"),
                                    value (state, "echo"), static_cast<double> (processor.hostTempo()),
                                    value (state, "echo.ascend.interval"), value (state, "echo.ascend.motion") });
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
            e.interval = static_cast<AscendInterval> (std::clamp (juce::roundToInt (value (state, "echo.ascend.interval")), 0, 3));
            e.motion = static_cast<AscendMotion> (std::clamp (juce::roundToInt (value (state, "echo.ascend.motion")), 0, 2));
            const double seconds = EchoDelay::timeSeconds (e, processor.hostTempo());
            const bool tape = e.type == EchoType::tape, ascend = e.type == EchoType::ascend;

            // (The rack's picture: room for its captions above and below, L / R at the left.)
            const auto plot = detailed ? juce::Rectangle<float>::leftTopRightBottom (bounds.getX() + 22.0f, bounds.getY() + 22.0f, bounds.getRight() - 12.0f, bounds.getBottom() - 18.0f)
                                       : juce::Rectangle<float>::leftTopRightBottom (bounds.getX() + 14.0f, bounds.getY() + 14.0f, bounds.getRight() - 10.0f, bounds.getBottom() - 5.0f);
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

            g.setFont (fonts::make (detailed ? 10.0f : 9.0f, fonts::Weight::regular, 0.02f));
            g.setColour (juce::Colour (detailed ? 0xffc2c7c5 : 0xff9aa09e).withAlpha (0.85f));
            const auto ms = juce::String (juce::roundToInt (1000.0 * seconds)) + " ms";
            const float captionY = bounds.getY() + (detailed ? 5.0f : 2.0f);
            g.drawText (e.sync ? juce::String (shaping::echoDivisionName (e.division)) + "  " + ms : ms,
                        juce::Rectangle<float> (bounds.getX() + 10.0f, captionY, 160.0f, 12.0f), juce::Justification::centredLeft, false);
            g.drawText ("FEEDBACK " + juce::String (juce::roundToInt (100.0 * e.feedback)) + " %", juce::Rectangle<float> (bounds.getRight() - 130.0f, captionY, 120.0f, 12.0f),
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
            const double fb = std::min (0.98, e.feedback * (ascend ? 0.85 : tape ? 1.04 : 1.03));
            double level = 1.0;
            const auto warm = ascend ? juce::Colour (0xffd6e4f0) : tape ? juce::Colour (0xffffb15c) : juce::Colour (0xffd9c38a);
            const int interval = ascend ? juce::roundToInt (12.0 * std::log2 (EchoDelay::intervalRatio (e.interval))) : 0;
            // The rack's picture: each repeat's place and height, for the decay line through them.
            std::vector<juce::Point<float>> peaks { { xAt (0.0), 0.92f * half } };
            int repeats = 0;
            for (int n = 1; n <= 40; ++n)
            {
                level = n == 1 ? 0.92 : level * fb;
                const double t = seconds * n;
                if (t > span || level < 0.02)
                    break;
                const float dark = std::clamp (static_cast<float> (n) * (0.07f + 0.08f * (1.0f - static_cast<float> (e.tone))) * (tape ? 0.8f : 1.1f), 0.0f, 0.85f);
                const auto c = warm.interpolatedWith (identity, std::min (1.0f, 0.35f + 0.6f * dark)).withMultipliedBrightness (1.0f - 0.45f * dark);
                // (The rack: the quiet repeats keep a readable height - still in order, still
                // falling with the feedback - and are drawn a little wider.)
                const float h = detailed ? half * std::min (0.92f, 0.22f + 0.7f * static_cast<float> (std::pow (level, 0.6)))
                                         : half * 0.92f * static_cast<float> (std::sqrt (level));
                const float width = (tape ? 2.2f + 0.3f * static_cast<float> (n) * static_cast<float> (e.age) : 2.0f) + (detailed ? 1.0f : 0.0f);
                peaks.push_back ({ xAt (t), h });
                ++repeats;
                auto at = [&] (double time, float sign, bool both) {
                    const float x = xAt (time);
                    // (The rack: the right channel's repeats a shade cooler than the left's.)
                    const auto cc = detailed && ! both && sign < 0.0f ? c.interpolatedWith (juce::Colour (0xffbcd0e0), 0.3f) : c;
                    if (both)
                        stroke (x, mid - h, mid + h, cc, width);
                    else
                        stroke (x, mid, mid - sign * h, cc, width);
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
                if (ascend && n <= 5)
                {
                    // The repeat's pitch against the played note.
                    const int passes = e.motion == AscendMotion::steady ? 1 : e.motion == AscendMotion::late ? n - 1 : n;
                    const int semis = passes * interval;
                    if (const float x = xAt (t); semis != 0 && x + 14.0f < plot.getRight())
                    {
                        g.setFont (fonts::make (detailed ? 9.5f : 8.0f, fonts::Weight::medium, 0.04f));
                        g.setColour (c.withAlpha (0.8f));
                        g.drawText ((semis > 0 ? "+" : "") + juce::String (semis), juce::Rectangle<float> (x + 3.0f, plot.getY() - 2.0f, 24.0f, 10.0f),
                                    juce::Justification::centredLeft, false);
                    }
                }
            }
            if (detailed && peaks.size() > 1)
            {
                // The decay: a quiet line through the repeats' tops (and, mirrored, their feet).
                juce::Path upper, lower;
                for (std::size_t i = 0; i < peaks.size(); ++i)
                {
                    const auto& pk = peaks[i];
                    if (i == 0)
                    {
                        upper.startNewSubPath (pk.x, mid - pk.y);
                        lower.startNewSubPath (pk.x, mid + pk.y);
                    }
                    else
                    {
                        upper.lineTo (pk.x, mid - pk.y);
                        lower.lineTo (pk.x, mid + pk.y);
                    }
                }
                juce::Path dashedUpper, dashedLower;
                const float dashes[] { 3.0f, 3.0f };
                juce::PathStrokeType line (1.0f);
                line.createDashedStroke (dashedUpper, upper, dashes, 2);
                line.createDashedStroke (dashedLower, lower, dashes, 2);
                g.setColour (identity.withAlpha (0.45f));
                g.fillPath (dashedUpper);
                g.fillPath (dashedLower);
                // What the decay is: the repeats heard and how much each falls.
                const double fallDb = 20.0 * std::log10 (std::max (1.0e-3, fb));
                g.setFont (fonts::make (10.0f, fonts::Weight::regular, 0.02f));
                g.setColour (juce::Colour (0xffc2c7c5).withAlpha (0.8f));
                g.drawText (juce::String (repeats) + (repeats == 1 ? " repeat" : " repeats") + juce::String::fromUTF8 ("  \xc2\xb7  ") + juce::String (fallDb, 1) + " dB each",
                            juce::Rectangle<float> (bounds.getX() + 10.0f, bounds.getBottom() - 16.0f, 260.0f, 12.0f), juce::Justification::centredLeft, false);
                if (ascend)
                    g.drawText (e.motion == AscendMotion::steady ? "PITCH: each at the interval" : e.motion == AscendMotion::late ? "PITCH: rising from the 2nd" : "PITCH: rising",
                                juce::Rectangle<float> (bounds.getRight() - 230.0f, bounds.getBottom() - 16.0f, 220.0f, 12.0f), juce::Justification::centredRight, false);
            }
            if (e.stereo != EchoStereo::mono)
            {
                g.setFont (fonts::make (detailed ? 9.5f : 8.0f, fonts::Weight::medium, 0.06f));
                g.setColour (juce::Colour (0xff9aa09e).withAlpha (detailed ? 0.8f : 0.55f));
                const float lx = detailed ? plot.getX() - 16.0f : plot.getX() - 10.0f;
                g.drawText ("L", juce::Rectangle<float> (lx, plot.getY(), 10.0f, 10.0f), juce::Justification::centred, false);
                g.drawText ("R", juce::Rectangle<float> (lx, plot.getBottom() - 10.0f, 10.0f, 10.0f), juce::Justification::centred, false);
            }
        }

        InstrumentHost& processor;
        juce::AudioProcessorValueTreeState& state;
        Watch watch;
    };

    //==========================================================================
    // CHARACTER: the filter's response over the sound's own spectrum, the cutoff where
    // CHARACTER puts it, the resonance, and the envelope's peak as a ghost.
    class CharacterVisual final : public Visual
    {
    public:
        explicit CharacterVisual (InstrumentHost& p) : processor (p) {}

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
            auto& s = processor.apvts();
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
            auto& s = processor.apvts();
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

        InstrumentHost& processor;
        Watch watch;
        int shownType = -1;
    };

    //==========================================================================
    // MOVEMENT: what each mode does over time, from the bus' own formulas (SHAPER: the
    // pattern and the DSP's own playhead).
    /** MOVEMENT's lane `index` (InstrumentHost::movementLane): its parameters' names and the
        depth it plays at. Lane 0 is MOVEMENT itself ("movement.*", the MOVEMENT macro). */
    struct Lane
    {
        Lane (InstrumentHost& host, int laneIndex) : index (laneIndex), ids (host.movementLane (laneIndex)) {}
        int index;
        InstrumentHost::MovementLane ids;
        juce::String operator() (const char* suffix) const { return ids.prefix + suffix; }
        /** 0..1: the MOVEMENT macro, times the lane's own INTENSITY when it has one. */
        double depth (juce::AudioProcessorValueTreeState& s) const
        {
            double d = std::clamp (0.01 * value (s, "motion"), 0.0, 1.0);
            if (ids.levelId.isNotEmpty())
                d *= std::clamp (0.01 * value (s, ids.levelId), 0.0, 1.0);
            return d;
        }
        /** PULSE's rate in Hz: free, or the synced note value at the host's tempo. */
        double pulseHz (InstrumentHost& host) const
        {
            auto& s = host.apvts();
            if (ids.syncId.isNotEmpty() && value (s, ids.syncId) >= 0.5f && ids.pulseDivisionId.isNotEmpty())
                return shaping::pulseSyncedHz (juce::roundToInt (value (s, ids.pulseDivisionId)), host.hostTempo());
            return shaping::pulseRateHz (0.01 * value (s, (*this) ("pulse.rate")));
        }
    };

    class MovementVisual final : public Visual
    {
    public:
        explicit MovementVisual (InstrumentHost& p, int laneIndex = 0) : processor (p), lane (p, laneIndex) {}

    private:
        bool animates() const override { return true; }
        bool changed() override
        {
            const int mode = juce::roundToInt (value (processor.apvts(), lane ("mode")));
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
            if (detailed)
            {
                // A legend: a short stroke of the line's own colour, then its name (and value).
                g.setColour (colour);
                g.fillRoundedRectangle (juce::Rectangle<float> (area.getX(), y + 5.0f, 10.0f, 2.0f), 1.0f);
                g.setFont (type::annotation (9.5f));
                g.setColour (colour.interpolatedWith (juce::Colours::white, 0.2f));
                g.drawText (text, juce::Rectangle<float> (area.getX() + 14.0f, y, 180.0f, 12.0f), juce::Justification::centredLeft, false);
                return;
            }
            g.setColour (colour);
            g.setFont (type::annotation (9.0f));
            g.drawText (text, juce::Rectangle<float> (area.getX(), y, 120.0f, 11.0f), juce::Justification::centredLeft, false);
        }
        /** The rack's picture: what the lines move at, at the top right. */
        void note (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text) const
        {
            if (! detailed)
                return;
            g.setFont (type::annotation (9.5f));
            g.setColour (juce::Colour (0xffb8bdbb));
            g.drawText (text, juce::Rectangle<float> (area.getRight() - 200.0f, area.getY(), 200.0f, 12.0f), juce::Justification::centredRight, false);
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            using namespace palette;
            auto& s = processor.apvts();
            const auto mode = static_cast<MovementMode> (std::clamp (juce::roundToInt (value (s, lane ("mode"))), 0, 4));
            const double amount = lane.depth (s);
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
                    const double rate = shaping::driftSpeedHz (0.01 * value (s, lane ("drift.speed")));
                    // (Square roots: small settings still show.)
                    const double pitch = std::sqrt (std::clamp (0.01 * value (s, lane ("drift.pitch")), 0.02, 1.0));
                    const double tone = std::sqrt (std::clamp (0.01 * value (s, lane ("drift.tone")), 0.02, 1.0));
                    const double window = std::clamp (6.0 / rate, 6.0, 60.0);
                    const float swing = detailed ? 1.7f : 1.0f;   // (the rack: the wander over more of the height)
                    if (! detailed)
                        trace (g, area, window, [rate] (double t) { return shaping::sharedWander (11, t, rate * 1.3) * 0.6; }, stone.withAlpha (0.35f), 1.0f, mid, half * depth);
                    // (The rack: TONE a quieter, thinner line under PITCH, the third, unnamed one left out.)
                    trace (g, area, window, [rate, tone] (double t) { return tone * shaping::sharedWander (7, t, rate * 0.8); }, stone.withAlpha (detailed ? 0.8f : 0.7f),
                           detailed ? 1.3f : 1.4f, mid, half * depth * swing);
                    trace (g, area, window, [rate, pitch] (double t) { return pitch * shaping::sharedWander (3, t, rate); }, identity, detailed ? 2.4f : 2.0f, mid, half * depth * swing);
                    if (detailed)
                    {
                        caption (g, area, "PITCH  " + juce::String (shaping::driftPitchCents (0.01 * value (s, lane ("drift.pitch"))), 1) + " c", identity, area.getY());
                        caption (g, area, "TONE  " + juce::String (juce::roundToInt (value (s, lane ("drift.tone")))) + " %", stone, area.getY() + 14.0f);
                        note (g, area, format::hertz (shaping::driftSpeedHz (0.01 * value (s, lane ("drift.speed")))));
                    }
                    else
                    {
                        caption (g, area, "PITCH", identity, area.getY());
                        caption (g, area, "TONE", stone, area.getY() + 11.0f);
                    }
                    break;
                }
                case MovementMode::tape:
                {
                    // Wow (slow transport) above, flutter (fast capstan) below; WEAR roughens both.
                    const double a = 0.01 * value (s, lane ("tape.wow")), b = 0.01 * value (s, lane ("tape.flutter"));
                    const double wear = 0.01 * value (s, lane ("tape.wear"));
                    const double wowHz = shaping::tapeWowHz (a), flutterHz = shaping::tapeFlutterHz (b);
                    auto rough = [wear] (double t) { return wear * 0.25 * shaping::sharedWander (19, t, 9.0); };
                    trace (g, area, 4.0, [wowHz, a, rough] (double t) { return (0.7 * std::sin (twoPi * wowHz * t) + 0.5 * shaping::sharedWander (5, t, 0.3)) * (0.3 + 0.7 * a) * 0.8 + rough (t); },
                           identity, 2.0f, area.getY() + area.getHeight() * 0.3f, area.getHeight() * 0.24f * depth);
                    trace (g, area, 1.0, [flutterHz, b, rough] (double t) { return std::sin (twoPi * flutterHz * t) * (0.7 + 0.3 * shaping::sharedWander (9, t, 4.0)) * (0.2 + 0.8 * b) + rough (t); },
                           stone, 1.2f, area.getY() + area.getHeight() * 0.76f, area.getHeight() * 0.18f * depth);
                    caption (g, area, "WOW  " + format::hertz (wowHz), identity, area.getY());
                    caption (g, area, "FLUTTER  " + format::hertz (flutterHz), stone, area.getY() + area.getHeight() * 0.5f);
                    if (detailed)
                    {
                        // The two machines apart: a faint rule between them, WEAR at the top right.
                        g.setColour (displayLine.withAlpha (0.6f));
                        g.fillRect (juce::Rectangle<float> (area.getX(), area.getY() + area.getHeight() * 0.5f - 3.0f, area.getWidth(), 1.0f));
                        note (g, area, "WEAR  " + juce::String (juce::roundToInt (100.0 * wear)) + " %");
                    }
                    break;
                }
                case MovementMode::chorus:
                {
                    // Two delay taps moving in phase-offset sines: STEREO pulls them apart.
                    const double rate = shaping::chorusRateHz (0.01 * value (s, lane ("chorus.rate")));
                    const double width = std::log2 (shaping::chorusWidthMs (0.01 * value (s, lane ("chorus.width"))) / 0.2) / std::log2 (90.0);
                    const double stereo = 0.01 * value (s, lane ("chorus.stereo"));
                    const double window = 2.5 / rate;
                    const float extent = half * static_cast<float> (0.25 + 0.75 * width) * depth;
                    trace (g, area, window, [rate, stereo] (double t) { return std::sin (twoPi * rate * t + std::numbers::pi * stereo); }, stone, 1.6f, mid, extent);
                    trace (g, area, window, [rate] (double t) { return std::sin (twoPi * rate * t); }, identity, 1.8f, mid, extent);
                    if (detailed)
                    {
                        caption (g, area, "LEFT", identity, area.getY());
                        caption (g, area, "RIGHT", stone, area.getY() + 14.0f);
                        note (g, area, format::hertz (rate) + juce::String::fromUTF8 ("  \xc2\xb7  ") + format::milliseconds (shaping::chorusWidthMs (0.01 * value (s, lane ("chorus.width")))));
                    }
                    else
                    {
                        caption (g, area, "L", identity, area.getY());
                        caption (g, area, "R", stone, area.getY() + 11.0f);
                    }
                    break;
                }
                case MovementMode::pulse:
                {
                    // The gain the bus applies: sine to rounded square by SHAPE, MOVEMENT's depth.
                    const double rate = lane.pulseHz (processor);
                    const double k = 1.0 + 7.0 * 0.01 * value (s, lane ("pulse.shape"));
                    const double stereo = 0.01 * value (s, lane ("pulse.stereo"));
                    const double norm = std::tanh (k), window = 3.0 / rate;
                    auto gain = [k, norm, amount] (double phase) {
                        const double y = std::tanh (k * std::sin (phase)) / norm;
                        return 1.0 - std::max (0.35, amount) * 0.5 * (1.0 - y);
                    };
                    const float bottom = area.getBottom(), height = area.getHeight() * (detailed ? 0.72f : 0.9f);   // (the rack: under its legends)
                    auto toTrace = [&] (double offset) {
                        return [=] (double t) { return 2.0 * gain (twoPi * rate * t + offset) - 1.0; };
                    };
                    if (stereo > 0.02)
                        trace (g, area, window, toTrace (std::numbers::pi * stereo), stone.withAlpha (0.7f), 1.3f, bottom - 0.5f * height, 0.5f * height);
                    trace (g, area, window, toTrace (0.0), identity, 2.0f, bottom - 0.5f * height, 0.5f * height);
                    if (detailed)
                    {
                        caption (g, area, stereo > 0.02 ? "LEFT" : "LEVEL", identity, area.getY());
                        if (stereo > 0.02)
                            caption (g, area, "RIGHT", stone.withAlpha (0.85f), area.getY() + 14.0f);
                        note (g, area, format::hertz (rate));
                    }
                    else
                        caption (g, area, format::hertz (rate), identity, area.getY());
                    break;
                }
                case MovementMode::shaper:
                {
                    // The pattern's contour (with SMOOTH), the step grid, and the DSP's own
                    // playhead (host-synchronised) with the step it is in.
                    const int pattern = juce::roundToInt (value (s, lane ("shaper.pattern")));
                    const double smooth = 0.01 * value (s, lane ("shaper.smooth"));
                    // DEPTH (the MOVEMENT macro): how far the level travels - the pattern's
                    // full range as a ghost, what DEPTH makes of it in front.
                    const float depth = static_cast<float> (lane.depth (s));
                    const auto span = RhythmicShaper::span (pattern, smooth);
                    const float phase = processor.laneShaperPhase (lane.index);
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

        InstrumentHost& processor;
        Lane lane;
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
        ShaperEditor (InstrumentHost& p, ShaperTool& t, bool isLarge, int laneIndex = 0) : processor (p), lane (p, laneIndex), tool (t), large (isLarge)
        {
            setInterceptsMouseClicks (true, false);
            setTooltip (large ? "Draw the pattern: STEP sets each step's level (its shape from the brush), DRAW paints a free line"
                              : "Click or drag to set the steps (the magnifier opens the large editor)");
        }

        bool custom() const { return value (processor.apvts(), lane ("shaper.custom")) >= 0.5f; }
        ShaperPattern steps() const
        {
            return custom() ? processor.laneShaperPattern (lane.index) : RhythmicShaper::patternSteps (juce::roundToInt (value (processor.apvts(), lane ("shaper.pattern"))));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            processor.undo().beginNewTransaction ("Shaper pattern");
            working = steps();
            if (! custom())
            {
                // Editing a library pattern: it becomes CUSTOM, starting from it.
                processor.setLaneShaperPattern (lane.index, working);
                if (auto* c = processor.apvts().getParameter (lane ("shaper.custom")))
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
                processor.setLaneShaperPattern (lane.index, pattern);
                repaint();
            }
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float>) override
        {
            using namespace palette;
            auto& s = processor.apvts();
            const auto area = plotArea();
            const auto pattern = steps();
            const double smooth = 0.01 * value (s, lane ("shaper.smooth"));
            const float depth = static_cast<float> (lane.depth (s));
            const auto span = RhythmicShaper::span (pattern, smooth);
            const float phase = processor.laneShaperPhase (lane.index);
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

        InstrumentHost& processor;
        Lane lane;
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
        explicit LifeVisual (InstrumentHost& p) : processor (p) {}

    private:
        bool changed() override
        {
            auto& s = processor.apvts();
            const int mode = juce::roundToInt (value (s, "life.mode"));
            if (mode != shownMode && shownMode >= 0)
                transition();
            shownMode = mode;
            return watch.differs ({ static_cast<double> (mode), value (s, "life"), value (s, "life.pitch"), value (s, "life.tone"), value (s, "life.attack"),
                                    value (s, "life.takes"), value (s, "life.takesSeed"), value (s, "life.character"),
                                    static_cast<double> (processor.playedVelocityCount()) });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> area) override
        {
            using namespace palette;
            auto& s = processor.apvts();
            const double life = std::clamp (0.01 * value (s, "life"), 0.0, 1.0);
            const auto mode = static_cast<LifeMode> (std::clamp (juce::roundToInt (value (s, "life.mode")), 0, 2));
            const double modeScale = mode == LifeMode::loose ? 1.5 : (mode == LifeMode::fray ? 1.2 : 1.0);
            const double pitch = value (s, "life.pitch") / 15.0, tone = 0.01 * value (s, "life.tone"), attack = 0.01 * value (s, "life.attack");
            const auto notes = static_cast<std::uint64_t> (processor.playedVelocityCount());
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

        InstrumentHost& processor;
        Watch watch;
        int shownMode = -1;
    };

    //==========================================================================
    // DYNAMICS: touch -> level, through the velocity curve and RANGE (scaled by the macro as
    // the engine does), a brightness halo where TONE opens the sound, and the notes just played.
    /** DRIVE: the circuit's transfer curve at its settings (DriveProcessor::transfer, the
        DSP's own shaping functions): input across, output up, a faint clean diagonal behind.
        As DRIVE rises the curve bends and its peaks flatten; TUBE leans (its asymmetry),
        CRUNCH folds harder, TAPE rounds - and, as a stage with memory has no single curve,
        TAPE also shows (faintly, dashed) how a transient is driven harder. */
    class DriveVisual final : public Visual, public juce::SettableTooltipClient
    {
    public:
        explicit DriveVisual (juce::AudioProcessorValueTreeState& s) : state (s)
        {
            setTooltip ("Input across, output up: the straight line is the clean sound, the curve the circuit at these settings");
        }

    private:
        bool native() const override { return true; }
        bool changed() override
        {
            const int mode = juce::roundToInt (value (state, "drive.mode"));
            if (mode != lastMode)
            {
                if (lastMode >= 0)
                    transition();   // a new circuit: fade from the old picture
                lastMode = mode;
            }
            return watch.differs ({ value (state, "drive"), static_cast<double> (mode), value (state, "drive.tone"), value (state, "drive.body") });
        }

        void paintVisual (juce::Graphics& g, juce::Rectangle<float> r) override
        {
            using namespace palette;
            DriveProcessor::Settings settings;
            settings.amount = std::clamp (0.01 * value (state, "drive"), 0.0, 1.0);
            settings.mode = static_cast<DriveMode> (std::clamp (juce::roundToInt (value (state, "drive.mode")), 0, 2));
            settings.tone = 0.01 * value (state, "drive.tone");
            settings.body = 0.01 * value (state, "drive.body");

            // The whole well: input +-1.8 x the reference level (program peaks), output +-1.3
            // (a saturated output lives in the smaller range: its bend reads better).
            constexpr double range = 1.8, outRange = 1.3;
            const auto plot = r.reduced (18.0f, 9.0f);
            auto xAt = [&] (double v) { return plot.getCentreX() + static_cast<float> (v / range) * 0.5f * plot.getWidth(); };
            auto yAt = [&] (double v) { return plot.getCentreY() - static_cast<float> (std::clamp (v, -outRange, outRange) / outRange) * 0.5f * plot.getHeight(); };

            g.setColour (displayLine.withAlpha (0.45f));
            g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
            g.drawVerticalLine (juce::roundToInt (plot.getCentreX()), plot.getY(), plot.getBottom());
            for (double level : { -1.0, 1.0 })   // the reference level (program peaks)
            {
                g.setColour (displayLine.withAlpha (0.22f));
                g.drawVerticalLine (juce::roundToInt (xAt (level)), plot.getY(), plot.getBottom());
                g.drawHorizontalLine (juce::roundToInt (yAt (level)), plot.getX(), plot.getRight());
            }
            g.setFont (fonts::make (8.5f));
            g.setColour (displayText.withAlpha (0.7f));
            g.drawText ("IN", juce::Rectangle<float> (plot.getRight() + 2.0f, plot.getCentreY() - 6.0f, 20.0f, 12.0f), juce::Justification::centredLeft, false);
            g.drawText ("OUT", juce::Rectangle<float> (plot.getCentreX() + 3.0f, plot.getY() - 1.0f, 24.0f, 10.0f), juce::Justification::centredLeft, false);

            // The clean response, faint and dashed.
            juce::Path clean;
            clean.startNewSubPath (xAt (-range), yAt (-range));
            clean.lineTo (xAt (range), yAt (range));
            juce::Path dashed;
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, clean, dashes, 2);
            g.setColour (displayText.withAlpha (0.4f));
            g.fillPath (dashed);

            auto curve = [&] (double push) {
                juce::Path p;
                constexpr int steps = 90;
                for (int i = 0; i <= steps; ++i)
                {
                    const double x = -range + 2.0 * range * i / steps;
                    const auto px = xAt (x), py = yAt (DriveProcessor::transfer (settings, x, push));
                    if (i == 0)
                        p.startNewSubPath (px, py);
                    else
                        p.lineTo (px, py);
                }
                return p;
            };
            const auto main = curve (0.0);
            {
                juce::Graphics::ScopedSaveState clip (g);
                g.reduceClipRegion (plot.expanded (1.0f).toNearestInt());
                if (settings.mode == DriveMode::tape && settings.amount > 0.0)
                {
                    juce::Path transient;
                    juce::PathStrokeType (1.0f).createDashedStroke (transient, curve (1.5), dashes, 2);
                    g.setColour (identity.withAlpha (0.45f));
                    g.fillPath (transient);
                }
                g.setColour (identity.withAlpha (0.18f));
                g.strokePath (main, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (identity);
                g.strokePath (main, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (settings.amount <= 0.0)
            {
                g.setColour (displayText.withAlpha (0.75f));
                g.drawText ("CLEAN", plot.withTrimmedTop (plot.getHeight() * 0.62f).withTrimmedLeft (plot.getWidth() * 0.5f),
                            juce::Justification::centred, false);
            }
        }

        juce::AudioProcessorValueTreeState& state;
        Watch watch;
        int lastMode = -1;
    };

    //==========================================================================
    /** A macro popup: title and question, mode tabs, the visualisation, compact controls. */
    class MacroPanel : public MiniPanel
    {
    public:
        /** A compact popover (230-270 x 170-210 reference px) that unfolds above its macro:
            a small title with the mode as a quiet selector beside it, the visualisation as
            the largest element, then small knobs. */
        MacroPanel (InstrumentHost& p, juce::String popupTitle, int width, int displayHeight, design::colour::Macro macro)
            : MiniPanel (std::move (popupTitle)), processor (p), panelWidth (width), visualHeight (displayHeight),
              identity (macro), accent (design::colour::macro (macro))
        {
        }
        /** A popover without a macro identity of its own (REIMAGINED: the aurora at its amount). */
        MacroPanel (InstrumentHost& p, juce::String popupTitle, int width, int displayHeight, juce::Colour colour)
            : MiniPanel (std::move (popupTitle)), processor (p), panelWidth (width), visualHeight (displayHeight),
              identity (design::colour::Macro::life), accent (colour), ownColour (true)
        {
        }

        bool compact() const override { return true; }
        void settleNow() override
        {
            if (visual != nullptr)
                visual->settle();
        }

        ValueSelector& mode (const char* id, juce::String tooltip)
        {
            modeSelector = std::make_unique<ValueSelector> (*processor.apvts().getParameter (id), juce::String());
            modeSelector->setPlain (true);
            modeSelector->setAccent (accent);
            modeSelector->setTooltip (tooltip);
            addAndMakeVisible (*modeSelector);
            return *modeSelector;
        }
        /** SPACE also shows its types as a row of tabs under the display. */
        SegmentedControl& tabs (const char* id, juce::StringArray items, juce::String tooltip)
        {
            modeTabs = std::make_unique<SegmentedControl> (*processor.apvts().getParameter (id), std::move (items));
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
            auto k = std::make_unique<MiniKnob> (processor.apvts(), id, caption, std::move (f));
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
            int h = headerTop + visualHeight + gap + (modeTabs != nullptr || modeRow != nullptr ? tabsHeight + 6 : 0) + cellHeight + bottom;
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
            if (auto* row = modeTabs != nullptr ? static_cast<juce::Component*> (modeTabs.get()) : modeRow.get())
            {
                row->setBounds (area.removeFromTop (tabsHeight));
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

        InstrumentHost& processor;
        int panelWidth;
        int visualHeight;
        design::colour::Macro identity;
        juce::Colour accent;   ///< the macro's identity (its knobs' arcs, the chosen mode)
        bool ownColour = false;
        std::unique_ptr<ValueSelector> modeSelector;
        std::unique_ptr<SegmentedControl> modeTabs;
        std::unique_ptr<juce::Component> modeRow;   ///< in the tabs' place (SPACE: its presets)
        std::unique_ptr<Visual> visual;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        std::vector<MiniKnob*> primaryRow, secondaryRow;
    };

    //==========================================================================
    /** SPACE's presets in the tabs' place: < NAME > (the arrows step through them, the name
        opens them by category). The name is worked out from the values (spacepresets): the
        preset they match, the last one chosen with " *" once edited, else what is set. */
    class SpacePresetBar final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        SpacePresetBar (InstrumentHost& p, juce::Colour accentColour) : host (p), accent (accentColour)
        {
            setTitle ("SPACE presets");
            setTooltip ("SPACE's presets: click for the list by category, the arrows for the next or previous");
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            refreshText();
            startTimerHz (8);
        }

        /** What the bar shows now (tests). */
        juce::String text() const { return shown; }
        void choose (int index)
        {
            const auto& all = spacepresets::all();
            if (index < 0 || index >= static_cast<int> (all.size()))
                return;
            spacepresets::apply (host.apvts(), all[static_cast<std::size_t> (index)], &host.undo());
            last = index;
            refreshText();
        }
        void step (int delta)
        {
            const int count = static_cast<int> (spacepresets::all().size());
            const int from = current >= 0 ? current : last;
            choose (from < 0 ? (delta > 0 ? 0 : count - 1) : (from + delta + count) % count);
        }
        void showMenu()
        {
            const auto& all = spacepresets::all();
            juce::PopupMenu menu;
            juce::Component::SafePointer<SpacePresetBar> safe (this);
            for (const auto& category : spacepresets::categories())
            {
                juce::PopupMenu sub;
                bool ticked = false;
                for (int i = 0; i < static_cast<int> (all.size()); ++i)
                    if (category == all[static_cast<std::size_t> (i)].category)
                    {
                        sub.addItem (all[static_cast<std::size_t> (i)].name, true, i == current, [safe, i] { if (safe != nullptr) safe->choose (i); });
                        ticked = ticked || i == current;
                    }
                menu.addSubMenu (category, sub, true, nullptr, ticked);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
        }

        void paint (juce::Graphics& g) override
        {
            using namespace design;
            // (The values may have moved since the timer last looked: what is drawn is now.)
            current = spacepresets::matching (host.apvts());
            if (current >= 0)
                last = current;
            shown = spacepresets::barText (host.apvts(), last);
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (colour::text.withAlpha (hover == 2 ? 0.07f : 0.04f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (colour::hairline.darker (0.1f));
            g.drawRoundedRectangle (r, 5.0f, 1.0f);
            auto arrow = [&g, this] (juce::Rectangle<float> a, bool left, bool lit) {
                juce::Path path;
                const auto c = a.getCentre();
                path.startNewSubPath (c.x + (left ? 2.5f : -2.5f), c.y - 4.0f);
                path.lineTo (c.x + (left ? -2.0f : 2.0f), c.y);
                path.lineTo (c.x + (left ? 2.5f : -2.5f), c.y + 4.0f);
                g.setColour (colour::text.withAlpha (lit ? 0.9f : 0.55f));
                g.strokePath (path, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            };
            arrow (leftArea(), true, hover == 1);
            arrow (rightArea(), false, hover == 3);
            g.setColour (colour::hairline.darker (0.1f));
            g.fillRect (juce::Rectangle<float> (leftArea().getRight(), r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f));
            g.fillRect (juce::Rectangle<float> (rightArea().getX() - 1.0f, r.getY() + 4.0f, 1.0f, r.getHeight() - 8.0f));
            g.setFont (type::popupLabel (12.5f));
            g.setColour (current >= 0 ? accent.darker (0.35f) : colour::text.withAlpha (0.85f));
            g.drawText (shown, getLocalBounds().toFloat().withTrimmedLeft (leftArea().getWidth() + 4.0f).withTrimmedRight (rightArea().getWidth() + 4.0f),
                        juce::Justification::centred, true);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (leftArea().contains (e.position))
                step (-1);
            else if (rightArea().contains (e.position))
                step (1);
            else
                showMenu();
        }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int now = leftArea().contains (e.position) ? 1 : (rightArea().contains (e.position) ? 3 : 2);
            if (now != hover)
            {
                hover = now;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hover = 0;
            repaint();
        }

    private:
        juce::Rectangle<float> leftArea() const { return getLocalBounds().toFloat().removeFromLeft (26.0f); }
        juce::Rectangle<float> rightArea() const { return getLocalBounds().toFloat().removeFromRight (26.0f); }
        void timerCallback() override { refreshText(); }
        void refreshText()
        {
            current = spacepresets::matching (host.apvts());
            if (current >= 0)
                last = current;
            if (const auto next = spacepresets::barText (host.apvts(), last); next != shown)
            {
                shown = next;
                repaint();
            }
        }

        InstrumentHost& host;
        juce::Colour accent;
        juce::String shown;
        int current = -1, last = -1, hover = 0;
    };

    //==========================================================================
    /** SPACE: the room, its EQ on the display (drag the corners), then its shape. The header's
        list picks a room or one of SPACE's other engines and its variant (ABYSS DEEP / DARK /
        INVERSE / ENDLESS, VINTAGE HALL / PLATE / CHAMBER / ROOM, BLOOM CLEAN / VINTAGE / TAPE /
        WASH); under the display the presets bar (SpacePresetBar); the popover keeps its size.
          ROOMS    PRE-DELAY, SIZE, DECAY, DAMP; MOD, WIDTH, LOW / HIGH CUT.
          ABYSS    PRE-DELAY, SIZE, DECAY, DAMP; GRAVITY, RESONANCE, MOD, WIDTH, FREEZE.
          VINTAGE  PRE-DELAY, SIZE, DECAY, DAMP; DIFFUSION, MOD, WIDTH, ERA.
          BLOOM    TIME (a division synced, ms free), DIFFUSION, DECAY, DAMP; SYNC, SIZE, MOD, WIDTH.
        Off ROOMS, LOW / HIGH CUT stay on the display's corners. */
    class SpacePanel final : public MacroPanel
    {
    public:
        explicit SpacePanel (InstrumentHost& p)
            : MacroPanel (p, "SPACE", 300, 92, design::colour::Macro::space),
              // (On the message thread; the switch comes from the header's selector or a
              // knob the rebuild replaces only after the change: only tabs and knobs are remade.)
              engineAttachment (*p.apvts().getParameter ("space.engine"), [this] (float) { build(); }),
              syncAttachment (*p.apvts().getParameter ("space.bloom.sync"), [this] (float) {
                  juce::Component::SafePointer<SpacePanel> safe (this);   // (SYNC is itself a knob here: rebuilt after its gesture)
                  juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->build(); });
              }),
              typeAttachment (*p.apvts().getParameter ("space.type"), [this] (float) {
                  for (auto& k : knobs)
                      k->repaint();   // (ROOMS' DECAY readout follows the type's range)
              }),
              variantAttachments { juce::ParameterAttachment (*p.apvts().getParameter ("space.abyss.variant"), [this] (float) { repaintMode(); }),
                                   juce::ParameterAttachment (*p.apvts().getParameter ("space.vintage.variant"), [this] (float) { repaintMode(); }),
                                   juce::ParameterAttachment (*p.apvts().getParameter ("space.bloom.variant"), [this] (float) { repaintMode(); }) }
        {
            auto& m = mode ("space.type", "Room, hall, plate or spring - or ABYSS, VINTAGE or BLOOM, SPACE's other engines, and their variants");
            m.textOverride = [this] {
                if (engine() == 0)
                    return juce::String();
                return spacepresets::describe (processor.apvts()).replace (" ", juce::String::fromUTF8 (" \xc2\xb7 "));
            };
            m.extraItems = [this] (juce::PopupMenu& menu) {
                juce::Component::SafePointer<SpacePanel> safe (this);
                menu.addSeparator();
                static const std::array<juce::StringArray, 3> variants { juce::StringArray { "Deep", "Dark", "Inverse", "Endless" },
                                                                         juce::StringArray { "Hall", "Plate", "Chamber", "Room" },
                                                                         juce::StringArray { "Clean", "Vintage", "Tape", "Wash" } };
                static const char* variantIds[] { "space.abyss.variant", "space.vintage.variant", "space.bloom.variant" };
                int index = 1;
                for (const char* name : { "Abyss", "Vintage", "Bloom" })
                {
                    juce::PopupMenu sub;
                    const int now = juce::roundToInt (value (processor.apvts(), variantIds[index - 1]));
                    for (int v = 0; v < 4; ++v)
                        sub.addItem (variants[static_cast<std::size_t> (index - 1)][v], true, engine() == index && now == v,
                                     [safe, index, v] { if (safe != nullptr) safe->setEngine (index, v); });
                    menu.addSubMenu (name, sub, true, nullptr, engine() == index);
                    ++index;
                }
            };
            m.onPick = [this] (int) { setEngine (0); };   // a room leaves the other engines
            setVisual (std::make_unique<SpaceVisual> (p.apvts(), [&p] { return static_cast<double> (p.hostTempo()); }));
            modeRow = std::make_unique<SpacePresetBar> (p, accent);
            addAndMakeVisible (*modeRow);
            build();
        }

        /** Tests: the presets bar. */
        SpacePresetBar& presetBar() { return *static_cast<SpacePresetBar*> (modeRow.get()); }

        /** Tests: what the popover is built for now (engine * 2 + BLOOM's SYNC). */
        int builtFor() const noexcept { return built; }

    private:
        int engine() const { return juce::roundToInt (value (processor.apvts(), "space.engine")); }
        void setEngine (int e, int variant = -1)
        {
            auto set = [this] (const char* id, int v) {
                if (auto* param = processor.apvts().getParameter (id); param != nullptr && juce::roundToInt (param->convertFrom0to1 (param->getValue())) != v)
                {
                    param->beginChangeGesture();
                    param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (v)));
                    param->endChangeGesture();
                }
            };
            static const char* variantIds[] { "space.abyss.variant", "space.vintage.variant", "space.bloom.variant" };
            if (variant >= 0 && e >= 1 && e <= 3)
                set (variantIds[e - 1], variant);
            set ("space.engine", e);
        }
        void repaintMode()
        {
            if (modeSelector != nullptr)
                modeSelector->repaint();
        }

        void build()
        {
            const int which = engine();
            const bool sync = value (processor.apvts(), "space.bloom.sync") >= 0.5f;
            const int key = which * 2 + (which == 3 && sync ? 1 : 0);
            if (built == key)
                return;
            built = key;
            primaryRow.clear();
            secondaryRow.clear();
            knobs.clear();
            modeTabs.reset();
            const auto ms = [] (double v) { return format::milliseconds (v); };
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            const auto seconds = [] (double v) { return juce::String (std::clamp (v, 0.2, 8.0), 1) + " s"; };
            switch (which)
            {
                case 1:   // ABYSS
                {
                    knob (false, "space.preDelay", "PRE-DELAY", ms);
                    knob (false, "space.size", "SIZE", percent);
                    knob (false, "space.decay", "DECAY", seconds);
                    knob (false, "space.damping", "DAMP", percent);
                    knob (true, "space.abyss.gravity", "GRAVITY", [] (double v) {
                        const int r = juce::roundToInt (v);
                        return (r > 0 ? juce::String ("+") : juce::String()) + juce::String (r) + " %";
                    });
                    knob (true, "space.abyss.resonance", "RESONANCE", percent);
                    knob (true, "space.modulation", "MOD", percent);
                    knob (true, "space.width", "WIDTH", percent);
                    knob (true, "space.abyss.freeze", "FREEZE", [] (double v) { return juce::String (v >= 0.5 ? "ON" : "OFF"); })
                        .slider.setTooltip ("FREEZE holds the space as it is (still moving); off, it decays again");
                    break;
                }
                case 2:   // VINTAGE
                {
                    knob (false, "space.preDelay", "PRE-DELAY", ms);
                    knob (false, "space.size", "SIZE", percent);
                    knob (false, "space.decay", "DECAY", seconds);
                    knob (false, "space.damping", "DAMP", percent);
                    knob (true, "space.vintage.diffusion", "DIFFUSION", percent);
                    knob (true, "space.modulation", "MOD", percent);
                    knob (true, "space.width", "WIDTH", percent);
                    knob (true, "space.vintage.era", "ERA", [] (double v) { return juce::String (v >= 0.5 ? "LATER" : "EARLY"); })
                        .slider.setTooltip ("EARLY: the first machines' lower rate (no air, more grain). LATER: a higher rate, a little more top");
                    break;
                }
                case 3:   // BLOOM
                {
                    if (sync)
                        knob (false, "space.bloom.division", "TIME", [] (double v) { return juce::String (shaping::echoDivisionName (juce::roundToInt (v))); });
                    else
                        knob (false, "space.bloom.time", "TIME", ms);
                    knob (false, "space.bloom.diffusion", "DIFFUSION", percent);
                    knob (false, "space.decay", "DECAY", seconds);
                    knob (false, "space.damping", "DAMP", percent);
                    knob (true, "space.bloom.sync", "SYNC", [] (double v) { return juce::String (v >= 0.5 ? "TEMPO" : "FREE"); });
                    knob (true, "space.size", "SIZE", percent);
                    knob (true, "space.modulation", "MOD", percent);
                    knob (true, "space.width", "WIDTH", percent);
                    break;
                }
                default:   // ROOMS
                {
                    auto* typeParam = processor.apvts().getParameter ("space.type");
                    knob (false, "space.preDelay", "PRE-DELAY", ms);
                    knob (false, "space.size", "SIZE", percent);
                    // The readout shows the decay the chosen type actually uses (each type has its own range).
                    knob (false, "space.decay", "DECAY", [typeParam] (double v) {
                        double lo = 0.2, hi = 8.0;
                        if (typeParam != nullptr)
                            shaping::decayRange (static_cast<SpaceType> (juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue()))), lo, hi);
                        return juce::String (std::clamp (v, lo, hi), 1) + " s";
                    });
                    knob (false, "space.damping", "DAMP", percent);
                    knob (true, "space.modulation", "MOD", percent);
                    knob (true, "space.width", "WIDTH", percent);
                    knob (true, "space.lowCut", "LOW CUT", [] (double v) { return format::hertz (v); });
                    knob (true, "space.highCut", "HIGH CUT", [] (double v) { return format::hertz (v); });
                    break;
                }
            }
            resized();
            repaint();
        }

        juce::ParameterAttachment engineAttachment, syncAttachment, typeAttachment;
        std::array<juce::ParameterAttachment, 3> variantAttachments;
        int built = -1;
    };

    //==========================================================================
    /** MOVEMENT: the mode, its picture, then that mode's own controls (SHAPER: PATTERN, RATE,
        TARGET on one line, then DEPTH and SMOOTH). Switching modes keeps every mode's values. */
    class MovementPanel final : public MacroPanel
    {
    public:
        explicit MovementPanel (InstrumentHost& p)
            : MacroPanel (p, "MOVEMENT", 270, 62, design::colour::Macro::movement),
              customAttachment (*p.apvts().getParameter ("movement.shaper.custom"), [this] (float) {
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
            auto& state = processor.apvts();
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
            auto* customParam = processor.apvts().getParameter ("movement.shaper.custom");
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
                const auto files = InstrumentHost::savedShaperPatterns();
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
            if (auto* c = processor.apvts().getParameter ("movement.shaper.custom"))
            {
                c->beginChangeGesture();
                c->setValueNotifyingHost (1.0f);
                c->endChangeGesture();
            }
            repaintEditor();
        }
        void setCustom (const ShaperPattern& pattern)
        {
            processor.undo().beginNewTransaction ("Shaper pattern");
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
            const auto files = InstrumentHost::savedShaperPatterns();
            for (const auto& f : files)
                menu.addItem (f.getFileNameWithoutExtension(), [safe, f] { if (safe != nullptr) safe->loadFile (f); });
            if (files.isEmpty())
                menu.addItem ("No saved patterns yet", false, false, [] {});
            menu.addSeparator();
            menu.addItem ("Show folder", [] {
                InstrumentHost::shaperPatternFolder().createDirectory();
                InstrumentHost::shaperPatternFolder().revealToUser();
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolbar.get()).withDeletionCheck (*this));
        }
        void loadFile (const juce::File& file)
        {
            if (const auto pattern = InstrumentHost::readShaperPatternFile (file))
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
                    InstrumentHost::writeShaperPatternFile (InstrumentHost::shaperPatternFolder().getChildFile (name + InstrumentHost::shaperPatternExtension), pattern);
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
    // MOVEMENT's lanes as data: everything a lane plays (its mode, every mode's settings, its
    // INTENSITY, SYNC, CYCLE and PHASE, its CUSTOM steps), by name, so a lane can be copied to
    // the other, swapped with it, or saved and loaded as a MOVEMENT preset that touches
    // nothing else of the instrument.
    const juce::StringArray& laneSettingNames()
    {
        static const juce::StringArray names { "mode", "drift.speed", "drift.pitch", "drift.tone", "tape.wow", "tape.flutter", "tape.wear",
                                               "chorus.rate", "chorus.width", "chorus.stereo", "pulse.rate", "pulse.shape", "pulse.stereo",
                                               "shaper.pattern", "shaper.rate", "shaper.target", "shaper.smooth", "shaper.custom" };
        return names;
    }

    struct LaneSnapshot
    {
        std::map<juce::String, float> values;   ///< plain values by name ("mode", "level", ...)
        ShaperPattern pattern {};
        bool on = true;
    };

    /** The lane's own names besides MOVEMENT's settings: "level", "sync", "pulse.division", "phase". */
    std::vector<std::pair<juce::String, juce::String>> laneExtras (const InstrumentHost::MovementLane& ids)
    {
        return { { "level", ids.levelId }, { "sync", ids.syncId }, { "pulse.division", ids.pulseDivisionId }, { "phase", ids.phaseId } };
    }

    LaneSnapshot readLane (InstrumentHost& host, int laneIndex)
    {
        const Lane lane (host, laneIndex);
        auto& s = host.apvts();
        LaneSnapshot snap;
        for (const auto& name : laneSettingNames())
            if (s.getParameter (lane.ids.prefix + name) != nullptr)
                snap.values[name] = value (s, lane.ids.prefix + name);
        for (const auto& [name, id] : laneExtras (lane.ids))
            if (id.isNotEmpty() && s.getParameter (id) != nullptr)
                snap.values[name] = value (s, id);
        snap.pattern = host.laneShaperPattern (laneIndex);
        snap.on = lane.ids.onId.isEmpty() || value (s, lane.ids.onId) >= 0.5f;
        return snap;
    }

    void setPlain (juce::AudioProcessorValueTreeState& s, const juce::String& id, float plain)
    {
        if (auto* p = s.getParameter (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
            p->endChangeGesture();
        }
    }

    /** Writes `snap` into the lane (the SHAPER steps an undoable step of their own); its ON
        too when `withOn`. Names the lane lacks are skipped (a preset from a richer lane). */
    void writeLane (InstrumentHost& host, int laneIndex, const LaneSnapshot& snap, bool withOn)
    {
        const Lane lane (host, laneIndex);
        auto& s = host.apvts();
        for (const auto& name : laneSettingNames())
            if (const auto it = snap.values.find (name); it != snap.values.end())
                setPlain (s, lane.ids.prefix + name, it->second);
        for (const auto& [name, id] : laneExtras (lane.ids))
            if (const auto it = snap.values.find (name); id.isNotEmpty() && it != snap.values.end())
                setPlain (s, id, it->second);
        host.setLaneShaperPattern (laneIndex, snap.pattern);
        if (withOn && lane.ids.onId.isNotEmpty())
            setPlain (s, lane.ids.onId, snap.on ? 1.0f : 0.0f);
    }

    constexpr const char* movementPresetExtension = ".andormovement";
}   // namespace

juce::File movementPresetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR/Movement Presets");
}

juce::Array<juce::File> savedMovementPresets()
{
    auto files = movementPresetFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + movementPresetExtension);
    files.sort();
    return files;
}

bool writeMovementPreset (InstrumentHost& host, int lane, const juce::File& file)
{
    const auto snap = readLane (host, lane);
    auto* root = new juce::DynamicObject();
    root->setProperty ("schemaVersion", 1);
    root->setProperty ("kind", "andor.movement");
    auto* settings = new juce::DynamicObject();
    for (const auto& [name, plain] : snap.values)
        settings->setProperty (juce::Identifier (name), plain);
    root->setProperty ("settings", juce::var (settings));
    root->setProperty ("pattern", InstrumentHost::encodeShaperPattern (snap.pattern));
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (juce::JSON::toString (juce::var (root)));
}

bool readMovementPreset (InstrumentHost& host, int lane, const juce::File& file)
{
    const auto parsed = juce::JSON::parse (file.loadFileAsString());
    if (! parsed.isObject() || parsed["kind"].toString() != "andor.movement")
        return false;
    LaneSnapshot snap;
    if (auto* settings = parsed["settings"].getDynamicObject())
        for (const auto& property : settings->getProperties())
            snap.values[property.name.toString()] = static_cast<float> (static_cast<double> (property.value));
    snap.pattern = InstrumentHost::decodeShaperPattern (parsed["pattern"].toString()).value_or (RhythmicShaper::patternSteps (ShaperParams().pattern));
    snap.on = true;   // (loading into a lane that is off: it plays)
    host.undo().beginNewTransaction ("Load MOVEMENT preset");
    writeLane (host, lane, snap, true);
    return true;
}

void copyMovementLane (InstrumentHost& host, int from, int to)
{
    if (from == to || std::max (from, to) >= host.movementLaneCount())
        return;
    host.undo().beginNewTransaction ("Copy MOVEMENT lane");
    writeLane (host, to, readLane (host, from), false);
}

void swapMovementLanes (InstrumentHost& host)
{
    if (host.movementLaneCount() < 2)
        return;
    const auto a = readLane (host, 0), b = readLane (host, 1);
    host.undo().beginNewTransaction ("Swap MOVEMENT lanes");
    writeLane (host, 0, b, true);
    writeLane (host, 1, a, true);
}

namespace
{
    //==========================================================================
    /** The effects rack's columns (module coordinates, the module as wide as the rack): the
        picture exactly under the modulation window (its display's width, the same divider
        before it), the controls on the left. */
    namespace rackcolumns
    {
        /** The approved rack (ANDOR_*_RACK_APPROVAL.html, 1392 px reference): its content
            inset 20 px (19 padding + the 1 px border) on both sides; the controls' column a
            fixed share of that width per module (MOVEMENT 57 %, ECHO 61 %, SPACE 68 %); a 17 px
            gap, then the divider (1 px), 17 px, the graph to the right inset. The share is the
            module's, never a mode's or a value's: the graph never moves while editing. */
        constexpr int inset = rackui::Cell::contentInset, columnGap = rackui::Cell::columnGap, graphPad = rackui::Cell::columnGap, bottom = 12;
        inline int dividerFor (float share, int width)
        {
            const int x0 = inset, x1 = width - inset;
            return x0 + juce::roundToInt (share * static_cast<float> (x1 - x0)) + columnGap;
        }
        inline int pictureLeftFor (float share, int width) { return dividerFor (share, width) + 1 + graphPad; }
        /** The divider before the graph (approved: 1 px #cbc4b9, the content's height). */
        inline void paintDivider (juce::Graphics& g, int x, float top, float bottomY)
        {
            g.setColour (juce::Colour (0xffcbc4b9));
            g.fillRect (juce::Rectangle<float> (static_cast<float> (x), top, 1.0f, bottomY - top));
        }
        using Slot = rackui::Slot;
        using rackui::slotWidth;
        using rackui::slotGap;
        using rackui::denseFrom;
    }

    //==========================================================================
    // The effects rack's modules - MOVEMENT, ECHO and SPACE - built on one structure, SPACE's:
    //   1 the heading (the host draws it) and the module's own compact actions beside it,
    //   2 MODE: the algorithm, a selector at the left,
    //   3 PRESET: the presets of that mode, ‹ NAME ›, beside it,
    //   4 the controls of that mode: one row of equal cells (knobs, or keys under a caption),
    //   5 the picture, in the modulation window's column behind the same divider.
    // Their presets follow one set of rules (RackPresetBar).

    /** One preset of a rack module: values by the module's own names, SHAPER steps maybe. */
    struct RackPreset
    {
        juce::String category, name;
        std::map<juce::String, float> values;
        std::optional<ShaperPattern> pattern;
        juce::File file;   ///< a user preset's file (none: a factory one)
        bool isUser() const { return file != juce::File(); }
        bool sameAs (const RackPreset& other) const { return name == other.name && file == other.file; }
    };

    /** Tests: where user presets are saved instead of Documents/ANDOR (empty: there). */
    juce::File& rackPresetFolderOverride()
    {
        static juce::File folder;
        return folder;
    }

    /** What a module's presets are made of, and how they meet its parameters. */
    class RackPresetModel
    {
    public:
        /** `memory`: where the host keeps which preset was chosen last (saved with the session,
            so "NAME *" outlives the rack being closed and the instrument being reopened). */
        RackPresetModel (InstrumentHost& h, juce::String memory) : host (h), memoryKey (std::move (memory)) {}
        virtual ~RackPresetModel() = default;
        virtual juce::String kind() const = 0;        ///< in a user preset's file ("andor.echo")
        virtual juce::String extension() const = 0;
        virtual juce::String folderName() const = 0;  ///< under Documents/ANDOR
        virtual int mode() const = 0;
        virtual int modeOf (const RackPreset&) const = 0;
        virtual juce::String modeName (int mode) const = 0;
        virtual const std::vector<RackPreset>& factory() const = 0;
        /** The settings now, as a user preset stores them. */
        virtual RackPreset capture() const = 0;
        /** Sets the preset's settings (this effect, this lane only). */
        virtual void apply (const RackPreset&) = 0;
        virtual bool matches (const RackPreset&) const = 0;
        /** More of the module's actions at the end of the presets' menu (MOVEMENT: its lanes'). */
        virtual void addMenuItems (juce::PopupMenu& menu) { juce::ignoreUnused (menu); }

        juce::File folder() const
        {
            if (rackPresetFolderOverride() != juce::File())
                return rackPresetFolderOverride().getChildFile (folderName());
            return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR").getChildFile (folderName());
        }
        std::vector<RackPreset> user() const
        {
            std::vector<RackPreset> list;
            auto files = folder().findChildFiles (juce::File::findFiles, false, "*" + extension());
            files.sort();
            for (const auto& f : files)
                if (auto p = read (f))
                    list.push_back (std::move (*p));
            return list;
        }
        std::optional<RackPreset> read (const juce::File& file) const
        {
            const auto parsed = juce::JSON::parse (file.loadFileAsString());
            if (! parsed.isObject() || parsed["kind"].toString() != kind())
                return std::nullopt;
            RackPreset p;
            p.category = "YOURS";
            p.name = file.getFileNameWithoutExtension();
            p.file = file;
            if (auto* settings = parsed["settings"].getDynamicObject())
                for (const auto& property : settings->getProperties())
                    p.values[property.name.toString()] = static_cast<float> (static_cast<double> (property.value));
            if (parsed.hasProperty ("pattern"))
                p.pattern = InstrumentHost::decodeShaperPattern (parsed["pattern"].toString());
            return p;
        }
        std::optional<RackPreset> save (const juce::String& name) const
        {
            auto p = capture();
            p.name = name;
            p.category = "YOURS";
            p.file = folder().getChildFile (juce::File::createLegalFileName (name) + extension());
            auto* root = new juce::DynamicObject();
            root->setProperty ("schemaVersion", 1);
            root->setProperty ("kind", kind());
            auto* settings = new juce::DynamicObject();
            for (const auto& [key, plain] : p.values)
                settings->setProperty (juce::Identifier (key), plain);
            root->setProperty ("settings", juce::var (settings));
            if (p.pattern.has_value())
                root->setProperty ("pattern", InstrumentHost::encodeShaperPattern (*p.pattern));
            p.file.getParentDirectory().createDirectory();
            if (! p.file.replaceWithText (juce::JSON::toString (juce::var (root))))
                return std::nullopt;
            return p;
        }

        /** The preset last chosen or saved (the bar's "NAME *" once edited). */
        std::optional<RackPreset> lastChosen() const
        {
            const auto code = host.rackPresetMemory (memoryKey);
            if (code != cachedCode)
            {
                cachedCode = code;
                cached.reset();
                if (code.startsWith ("factory:"))
                {
                    for (const auto& p : factory())
                        if (p.name == code.fromFirstOccurrenceOf ("factory:", false, false))
                            cached = p;
                }
                else if (code.startsWith ("user:"))
                    cached = read (juce::File (code.fromFirstOccurrenceOf ("user:", false, false)));
            }
            return cached;
        }
        void setLastChosen (const std::optional<RackPreset>& p)
        {
            host.setRackPresetMemory (memoryKey, ! p.has_value() ? juce::String() : (p->isUser() ? "user:" + p->file.getFullPathName() : "factory:" + p->name));
        }

    protected:
        InstrumentHost& host;
        juce::String memoryKey;

    private:
        mutable juce::String cachedCode { "-" };
        mutable std::optional<RackPreset> cached;
    };

    /** Values of host parameters by their IDs (SPACE's, ECHO's). */
    class ParameterPresetModel : public RackPresetModel
    {
    public:
        ParameterPresetModel (InstrumentHost& h, juce::String memory, juce::StringArray parameterIds) : RackPresetModel (h, std::move (memory)), ids (std::move (parameterIds)) {}
        RackPreset capture() const override
        {
            RackPreset p;
            for (const auto& id : ids)
                if (host.apvts().getParameter (id) != nullptr)
                    p.values[id] = value (host.apvts(), id);
            return p;
        }
        void apply (const RackPreset& p) override
        {
            host.undo().beginNewTransaction ("Preset " + p.name);
            for (const auto& [id, plain] : p.values)
                if (ids.contains (id))
                    setPlain (host.apvts(), id, plain);
        }
        bool matches (const RackPreset& p) const override
        {
            for (const auto& [id, plain] : p.values)
                if (auto* param = host.apvts().getParameter (id); param != nullptr && std::abs (param->convertTo0to1 (plain) - param->getValue()) > 1.5e-3f)
                    return false;
            return ! p.values.empty();
        }

    protected:
        float defaultOf (const juce::String& id) const
        {
            auto* param = host.apvts().getParameter (id);
            return param != nullptr ? param->convertFrom0to1 (param->getDefaultValue()) : 0.0f;
        }
        juce::StringArray ids;
    };

    /** SPACE: its presets (spacepresets, each with every other SPACE parameter at its default)
        and the musician's own; the mode is the engine (ROOMS, ABYSS, VINTAGE, BLOOM). */
    class SpacePresetModel final : public ParameterPresetModel
    {
    public:
        explicit SpacePresetModel (InstrumentHost& h) : ParameterPresetModel (h, "space", spacepresets::parameterIds())
        {
            for (const auto& preset : spacepresets::all())
            {
                RackPreset p;
                p.category = preset.category;
                p.name = preset.name;
                for (const auto& id : ids)
                    p.values[id] = defaultOf (id);
                for (const auto& [id, plain] : preset.values)
                    p.values[id] = plain;
                presets.push_back (std::move (p));
            }
        }
        juce::String kind() const override { return "andor.space"; }
        juce::String extension() const override { return ".andorspace"; }
        juce::String folderName() const override { return "Space Presets"; }
        int mode() const override { return juce::roundToInt (value (host.apvts(), "space.engine")); }
        int modeOf (const RackPreset& p) const override
        {
            const auto it = p.values.find ("space.engine");
            return it != p.values.end() ? juce::roundToInt (it->second) : 0;
        }
        juce::String modeName (int m) const override
        {
            static const char* names[] { "ROOMS", "ABYSS", "VINTAGE", "BLOOM" };
            return names[std::clamp (m, 0, 3)];
        }
        const std::vector<RackPreset>& factory() const override { return presets; }

    private:
        std::vector<RackPreset> presets;
    };

    /** ECHO: a few starting points for each machine (TAPE, BBD, ASCEND) and the musician's own. */
    class EchoPresetModel final : public ParameterPresetModel
    {
    public:
        explicit EchoPresetModel (InstrumentHost& h)
            : ParameterPresetModel (h, "echo", { "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback", "echo.tone", "echo.age", "echo.stereo",
                                         "echo.ascend.interval", "echo.ascend.motion" })
        {
            // (type, sync, division index, free ms, feedback %, tone %, age %, stereo, interval, motion)
            struct Row
            {
                const char* name;
                float type, sync, division, ms, feedback, tone, age, stereo, interval, motion;
            };
            static const Row rows[] {
                { "Dotted Eighth", 0, 1, 5, 375, 45, 50, 35, 1, 0, 0 },   // (the instrument's own default)
                { "Slapback", 0, 0, 5, 110, 12, 55, 30, 0, 0, 0 },
                { "Worn Loop", 0, 1, 6, 375, 68, 32, 78, 2, 0, 0 },
                { "Dub Wash", 0, 1, 8, 375, 82, 30, 55, 1, 0, 0 },
                { "Bucket Eighths", 1, 1, 3, 375, 40, 45, 35, 1, 0, 0 },
                { "Short Doubler", 1, 0, 3, 32, 10, 60, 50, 2, 0, 0 },
                { "Dark Repeats", 1, 1, 6, 375, 58, 22, 70, 2, 0, 0 },
                { "Bucket Slap", 1, 0, 3, 85, 18, 55, 25, 0, 0, 0 },
                { "Octave Rise", 2, 1, 5, 375, 50, 55, 35, 1, 0, 0 },
                { "Fifth Steps", 2, 1, 3, 375, 45, 50, 30, 1, 1, 1 },
                { "Shimmer Late", 2, 1, 6, 375, 62, 60, 30, 2, 0, 2 },
                { "Falling Octave", 2, 1, 5, 375, 48, 40, 40, 1, 3, 0 },
            };
            for (const auto& r : rows)
            {
                RackPreset p;
                p.name = r.name;
                p.values = { { "echo.type", r.type }, { "echo.sync", r.sync }, { "echo.division", r.division }, { "echo.time", r.ms },
                             { "echo.feedback", r.feedback }, { "echo.tone", r.tone }, { "echo.age", r.age }, { "echo.stereo", r.stereo },
                             { "echo.ascend.interval", r.interval }, { "echo.ascend.motion", r.motion } };
                if (r.sync >= 0.5f)
                    p.values.erase ("echo.time");   // (synced: the free time is not part of it)
                else
                    p.values.erase ("echo.division");
                if (r.type < 1.5f)
                {
                    p.values.erase ("echo.ascend.interval");   // (only ASCEND has them)
                    p.values.erase ("echo.ascend.motion");
                }
                p.category = modeName (juce::roundToInt (r.type));
                presets.push_back (std::move (p));
            }
        }
        juce::String kind() const override { return "andor.echo"; }
        juce::String extension() const override { return ".andorecho"; }
        juce::String folderName() const override { return "Echo Presets"; }
        int mode() const override { return juce::roundToInt (value (host.apvts(), "echo.type")); }
        int modeOf (const RackPreset& p) const override
        {
            const auto it = p.values.find ("echo.type");
            return it != p.values.end() ? juce::roundToInt (it->second) : 0;
        }
        juce::String modeName (int m) const override
        {
            static const char* names[] { "TAPE", "BBD", "ASCEND" };
            return names[std::clamp (m, 0, 2)];
        }
        const std::vector<RackPreset>& factory() const override { return presets; }

    private:
        std::vector<RackPreset> presets;
    };

    /** A MOVEMENT lane: its mode, every mode's settings, INTENSITY, SYNC, CYCLE, PHASE and its
        SHAPER steps, by name - so a preset loads into whichever lane it is chosen in, and only
        there. The musician's own share the format of writeMovementPreset. */
    /** MOVEMENT's lane actions, from the A / B selector's menu and the presets' menu. */
    void addLaneActions (juce::PopupMenu& menu, InstrumentHost& host)
    {
        if (host.movementLaneCount() < 2)
            return;
        auto* h = &host;
        menu.addItem (juce::String::fromUTF8 ("Copy lane A \xe2\x96\xb8 lane B"), [h] { copyMovementLane (*h, 0, 1); });
        menu.addItem (juce::String::fromUTF8 ("Copy lane B \xe2\x96\xb8 lane A"), [h] { copyMovementLane (*h, 1, 0); });
        menu.addItem ("Swap lanes A and B", [h] { swapMovementLanes (*h); });
    }

    class MovementPresetModel final : public RackPresetModel
    {
    public:
        MovementPresetModel (InstrumentHost& h, int laneIndex) : RackPresetModel (h, laneIndex == 0 ? "movementA" : "movementB"), laneNumber (laneIndex)
        {
            struct Row
            {
                int mode;
                const char* name;
                std::vector<std::pair<const char*, float>> values;
            };
            // Each mode's starting points; the first is the instrument's own default.
            const std::vector<Row> rows {
                { 0, "Gentle Drift", { { "drift.speed", 70 }, { "drift.pitch", 25 }, { "drift.tone", 40 } } },
                { 0, "Wander", { { "drift.speed", 45 }, { "drift.pitch", 55 }, { "drift.tone", 50 } } },
                { 0, "Seasick", { { "drift.speed", 30 }, { "drift.pitch", 90 }, { "drift.tone", 70 } } },
                { 0, "Shiver", { { "drift.speed", 92 }, { "drift.pitch", 15 }, { "drift.tone", 20 } } },
                { 1, "Cassette", { { "tape.wow", 50 }, { "tape.flutter", 35 }, { "tape.wear", 35 } } },
                { 1, "Worn Reel", { { "tape.wow", 60 }, { "tape.flutter", 50 }, { "tape.wear", 75 } } },
                { 1, "Warped", { { "tape.wow", 85 }, { "tape.flutter", 30 }, { "tape.wear", 55 } } },
                { 1, "Clean Deck", { { "tape.wow", 25 }, { "tape.flutter", 20 }, { "tape.wear", 10 } } },
                { 2, "Ensemble", { { "chorus.rate", 45 }, { "chorus.width", 60 }, { "chorus.stereo", 60 } } },
                { 2, "Slow Sweep", { { "chorus.rate", 20 }, { "chorus.width", 75 }, { "chorus.stereo", 80 } } },
                { 2, "Fast Shimmer", { { "chorus.rate", 75 }, { "chorus.width", 35 }, { "chorus.stereo", 50 } } },
                { 2, "Wide Doubler", { { "chorus.rate", 35 }, { "chorus.width", 50 }, { "chorus.stereo", 100 } } },
                { 3, "Soft Tremolo", { { "pulse.rate", 55 }, { "pulse.shape", 30 }, { "pulse.stereo", 30 }, { "sync", 0 } } },
                { 3, "Choppy", { { "pulse.rate", 75 }, { "pulse.shape", 90 }, { "pulse.stereo", 0 }, { "sync", 0 } } },
                { 3, "Stereo Swirl", { { "pulse.rate", 40 }, { "pulse.shape", 20 }, { "pulse.stereo", 100 }, { "sync", 0 } } },
                { 3, "Quarter Pulse", { { "pulse.shape", 60 }, { "pulse.stereo", 0 }, { "sync", 1 }, { "pulse.division", 4 }, { "phase", 0 } } },
            };
            for (const auto& r : rows)
            {
                RackPreset p;
                p.name = r.name;
                p.category = modeName (r.mode);
                p.values["mode"] = static_cast<float> (r.mode);
                for (const auto& [n, v] : r.values)
                    p.values[n] = v;
                presets.push_back (std::move (p));
            }
            // SHAPER: every library pattern (the popover's PATTERN list) at 1/16, both volume and
            // filter; THREE, the instrument's own, first.
            std::vector<int> patterns { 3 };
            for (int i = 0; i < RhythmicShaper::patternCount; ++i)
                if (i != 3)
                    patterns.push_back (i);
            for (const int pattern : patterns)
            {
                RackPreset p;
                p.name = juce::String (RhythmicShaper::patternName (pattern)).toLowerCase().substring (0, 1).toUpperCase()
                         + juce::String (RhythmicShaper::patternName (pattern)).toLowerCase().substring (1);
                p.category = modeName (4);
                p.values = { { "mode", 4.0f }, { "shaper.pattern", static_cast<float> (pattern) }, { "shaper.rate", 3.0f }, { "shaper.target", 2.0f },
                             { "shaper.smooth", 30.0f }, { "shaper.custom", 0.0f } };
                presets.push_back (std::move (p));
            }
        }
        juce::String kind() const override { return "andor.movement"; }
        void addMenuItems (juce::PopupMenu& menu) override
        {
            if (host.movementLaneCount() < 2)
                return;
            menu.addSeparator();
            addLaneActions (menu, host);
        }
        juce::String extension() const override { return movementPresetExtension; }
        juce::String folderName() const override { return "Movement Presets"; }
        int mode() const override { return juce::roundToInt (value (host.apvts(), Lane (host, laneNumber) ("mode"))); }
        int modeOf (const RackPreset& p) const override
        {
            const auto it = p.values.find ("mode");
            return it != p.values.end() ? juce::roundToInt (it->second) : 0;
        }
        juce::String modeName (int m) const override
        {
            static const char* names[] { "DRIFT", "TAPE", "CHORUS", "PULSE", "SHAPER" };
            return names[std::clamp (m, 0, 4)];
        }
        const std::vector<RackPreset>& factory() const override { return presets; }
        RackPreset capture() const override
        {
            const auto snap = readLane (host, laneNumber);
            RackPreset p;
            p.values = snap.values;
            p.pattern = snap.pattern;
            return p;
        }
        void apply (const RackPreset& p) override
        {
            // Only what the preset names (a factory one: its mode's own settings); its lane plays.
            host.undo().beginNewTransaction ("MOVEMENT preset " + p.name);
            const Lane lane (host, laneNumber);
            auto& s = host.apvts();
            for (const auto& [n, plain] : p.values)
                if (const auto id = idOf (lane, n); id.isNotEmpty())
                    setPlain (s, id, plain);
            if (p.pattern.has_value())
                host.setLaneShaperPattern (laneNumber, *p.pattern);
            if (lane.ids.onId.isNotEmpty())
                setPlain (s, lane.ids.onId, 1.0f);
        }
        bool matches (const RackPreset& p) const override
        {
            const Lane lane (host, laneNumber);
            for (const auto& [n, plain] : p.values)
            {
                const auto id = idOf (lane, n);
                auto* param = id.isNotEmpty() ? host.apvts().getParameter (id) : nullptr;
                if (param != nullptr && std::abs (param->convertTo0to1 (plain) - param->getValue()) > 1.5e-3f)
                    return false;
            }
            const auto custom = p.values.find ("shaper.custom");
            if (p.pattern.has_value() && custom != p.values.end() && custom->second >= 0.5f && ! (host.laneShaperPattern (laneNumber) == *p.pattern))
                return false;
            return ! p.values.empty();
        }

    private:
        static juce::String idOf (const Lane& lane, const juce::String& n)
        {
            if (laneSettingNames().contains (n))
                return lane (n.toRawUTF8());
            for (const auto& [name, id] : laneExtras (lane.ids))
                if (name == n)
                    return id;
            return {};
        }
        int laneNumber;
        std::vector<RackPreset> presets;
    };

    //==========================================================================
    /**
        PRESET: ‹ NAME › - the presets of the mode the module is in (its factory ones, then the
        musician's own); the arrows step through them, the name opens them (with SAVE, RENAME,
        DELETE and the folder, and the other modes' presets). It shows the preset the settings
        match, the last one chosen with " *" once edited (in its mode), else CUSTOM. Choosing one
        sets that effect's (that lane's) settings only. SPACE's popover keeps its own bar.
    */
    class RackPresetBar final : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        RackPresetBar (juce::Colour accentColour) : accent (accentColour)
        {
            setTitle ("Presets");
            setTooltip ("Presets of this mode: click for the list, the arrows for the next or previous (save and manage: the key beside it)");
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            startTimerHz (8);
        }
        void setModel (RackPresetModel* m)
        {
            model = m;
            refreshText();
            repaint();
        }
        RackPresetModel* presetModel() const noexcept { return model; }

        /** What the bar shows now (worked out from the values now). */
        juce::String text()
        {
            refreshText();
            return shown;
        }
        std::vector<RackPreset> relevant() const
        {
            std::vector<RackPreset> list;
            if (model == nullptr)
                return list;
            const int m = model->mode();
            for (const auto& p : model->factory())
                if (model->modeOf (p) == m)
                    list.push_back (p);
            for (auto& p : model->user())
                if (model->modeOf (p) == m)
                    list.push_back (std::move (p));
            return list;
        }
        void choose (const RackPreset& p)
        {
            if (model == nullptr)
                return;
            model->apply (p);
            model->setLastChosen (p);
            refreshText();
        }
        bool chooseNamed (const juce::String& name)
        {
            if (model == nullptr)
                return false;
            for (const auto& p : model->factory())
                if (p.name == name)
                {
                    choose (p);
                    return true;
                }
            for (const auto& p : model->user())
                if (p.name == name)
                {
                    choose (p);
                    return true;
                }
            return false;
        }
        void step (int delta)
        {
            const auto list = relevant();
            if (list.empty())
                return;
            const int count = static_cast<int> (list.size());
            int from = -1;
            const auto last = model->lastChosen();
            for (int i = 0; i < count; ++i)
                if (model->matches (list[static_cast<std::size_t> (i)]) || (from < 0 && last.has_value() && list[static_cast<std::size_t> (i)].sameAs (*last)))
                    from = i;
            choose (list[static_cast<std::size_t> (from < 0 ? (delta > 0 ? 0 : count - 1) : (from + delta + count) % count)]);
        }
        bool saveAs (const juce::String& name)
        {
            if (model == nullptr || name.trim().isEmpty())
                return false;
            if (auto p = model->save (name.trim()))
            {
                model->setLastChosen (*p);
                refreshText();
                return true;
            }
            return false;
        }

        void paint (juce::Graphics& g) override
        {
            // The approved PRESET field (294 x 27): ‹ NAME ›, the arrows in 32 px wells with
            // engraved dividers, the name 11 px semibold, an edited one with a quiet asterisk.
            refreshText();
            const auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xffd4ccbf));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (juce::Colour (0xffeae4db));
            g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.69f));
            g.fillRect (juce::Rectangle<float> (r.getX() + 4.0f, r.getY() + 1.0f, r.getWidth() - 8.0f, 1.0f));
            g.setColour (juce::Colour (0xffd4ccc0));
            g.fillRect (juce::Rectangle<float> (leftArea().getRight(), r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f));
            g.fillRect (juce::Rectangle<float> (rightArea().getX() - 1.0f, r.getY() + 1.0f, 1.0f, r.getHeight() - 2.0f));
            auto arrow = [&g, this] (juce::Rectangle<float> a, bool left, bool lit) {
                juce::Path path;
                const auto c = a.getCentre().translated (0.0f, 0.5f);
                path.startNewSubPath (c.x + (left ? 2.5f : -2.5f), c.y - 5.0f);
                path.lineTo (c.x + (left ? -2.5f : 2.5f), c.y);
                path.lineTo (c.x + (left ? 2.5f : -2.5f), c.y + 5.0f);
                g.setColour (juce::Colour (0xff78868e).withMultipliedBrightness (lit ? 0.8f : 1.0f));
                g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::ignoreUnused (this);
            };
            arrow (leftArea(), true, hover == 1);
            arrow (rightArea(), false, hover == 3);
            const auto font = fonts::make (11.0f, fonts::Weight::semibold, 0.015f);
            g.setFont (font);
            const auto area = r.withTrimmedLeft (leftArea().getWidth() + 7.0f).withTrimmedRight (rightArea().getWidth() + 7.0f);
            const auto ink = juce::Colour (0xff607b8b).withMultipliedBrightness (hover == 2 ? 0.85f : 1.0f);
            if (edited)
            {
                // The name as chosen, then a quiet asterisk: still that preset, edited.
                auto name = shown.dropLastCharacters (2);
                const float star = juce::GlyphArrangement::getStringWidth (font, " *");
                const float w = std::min (juce::GlyphArrangement::getStringWidth (font, name), area.getWidth() - star - 2.0f);
                const float x = area.getCentreX() - 0.5f * (w + star);
                g.setColour (ink);
                g.drawText (name, juce::Rectangle<float> (x, area.getY(), w + 1.0f, area.getHeight()), juce::Justification::centredLeft, true);
                g.setColour (accent.darker (0.2f));
                g.drawText (" *", juce::Rectangle<float> (x + w, area.getY(), star + 2.0f, area.getHeight()), juce::Justification::centredLeft, false);
            }
            else
            {
                g.setColour (matched ? ink : ink.withAlpha (0.8f));
                g.drawText (shown, area, juce::Justification::centred, true);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (rackui::palette::focus);
                g.drawRoundedRectangle (r.expanded (2.0f), 7.0f, 2.0f);
            }
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (leftArea().contains (e.position))
                step (-1);
            else if (rightArea().contains (e.position))
                step (1);
            else
                showMenu();
        }
        bool keyPressed (const juce::KeyPress& key) override
        {
            if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
            {
                step (key == juce::KeyPress::leftKey ? -1 : 1);
                return true;
            }
            if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
            {
                showMenu();
                return true;
            }
            return false;
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int now = leftArea().contains (e.position) ? 1 : (rightArea().contains (e.position) ? 3 : 2);
            if (now != hover)
            {
                hover = now;
                repaint();
            }
        }
        void mouseExit (const juce::MouseEvent&) override
        {
            hover = 0;
            repaint();
        }

    private:
        void showMenu()
        {
            if (model == nullptr)
                return;
            juce::PopupMenu menu;
            juce::Component::SafePointer<RackPresetBar> safe (this);
            const int m = model->mode();
            auto item = [safe, this] (juce::PopupMenu& into, const RackPreset& p) {
                into.addItem (p.name, true, model->matches (p), [safe, p] { if (safe != nullptr) safe->choose (p); });
            };
            // This mode's: by category when it has several (SPACE's ROOMS), else in a list.
            juce::StringArray categories;
            for (const auto& p : model->factory())
                if (model->modeOf (p) == m)
                    categories.addIfNotAlreadyThere (p.category);
            for (const auto& category : categories)
            {
                juce::PopupMenu sub;
                for (const auto& p : model->factory())
                    if (model->modeOf (p) == m && p.category == category)
                        item (categories.size() > 1 ? sub : menu, p);
                if (categories.size() > 1)
                    menu.addSubMenu (category, sub);
            }
            const auto mine = model->user();
            menu.addSeparator();
            menu.addSectionHeader ("YOUR " + model->modeName (m) + " PRESETS");
            bool any = false;
            for (const auto& p : mine)
                if (model->modeOf (p) == m)
                {
                    item (menu, p);
                    any = true;
                }
            if (! any)
                menu.addItem ("(none saved yet)", false, false, [] {});
            // The other modes' presets (choosing one changes the mode).
            juce::PopupMenu others;
            for (int other = 0; other < 8; ++other)
            {
                if (other == m)
                    continue;
                juce::PopupMenu sub;
                bool has = false;
                for (const auto& p : model->factory())
                    if (model->modeOf (p) == other)
                    {
                        item (sub, p);
                        has = true;
                    }
                if (has)
                    others.addSubMenu (model->modeName (other), sub);
            }
            if (others.getNumItems() > 0)
            {
                menu.addSeparator();
                menu.addSubMenu ("Other modes", others);
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withDeletionCheck (*this));
        }

    public:
        /** The preset actions ("···"): save, rename, delete, the folder, the module's own. */
        juce::PopupMenu actionsMenu()
        {
            juce::PopupMenu menu;
            if (model == nullptr)
                return menu;
            juce::Component::SafePointer<RackPresetBar> safe (this);
            menu.addItem (juce::String::fromUTF8 ("Save preset\xe2\x80\xa6"), [safe] { if (safe != nullptr) safe->askName ("Save preset", "A name for these settings:", {}, false); });
            if (const auto last = model->lastChosen(); last.has_value() && last->isUser())
            {
                const auto name = last->name;
                menu.addItem (juce::String::fromUTF8 ("Rename \xe2\x80\x9c") + name + juce::String::fromUTF8 ("\xe2\x80\x9d\xe2\x80\xa6"),
                              [safe, name] { if (safe != nullptr) safe->askName ("Rename preset", "A new name:", name, true); });
                menu.addItem (juce::String::fromUTF8 ("Delete \xe2\x80\x9c") + name + juce::String::fromUTF8 ("\xe2\x80\x9d"), [safe] {
                    if (safe != nullptr && safe->model != nullptr)
                        if (const auto chosen = safe->model->lastChosen(); chosen.has_value() && chosen->isUser())
                        {
                            chosen->file.moveToTrash();
                            safe->model->setLastChosen (std::nullopt);
                            safe->refreshText();
                        }
                });
            }
            menu.addItem ("Show folder", [safe] {
                if (safe != nullptr && safe->model != nullptr)
                {
                    safe->model->folder().createDirectory();
                    safe->model->folder().revealToUser();
                }
            });
            model->addMenuItems (menu);
            return menu;
        }
        void showActions (juce::Component& from)
        {
            actionsMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&from).withDeletionCheck (*this));
        }

    private:
        void askName (const juce::String& title, const juce::String& prompt, const juce::String& current, bool rename)
        {
            auto* window = new juce::AlertWindow (title, prompt, juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", current.isNotEmpty() ? current : juce::String ("My preset"));
            window->addButton (rename ? "Rename" : "Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<RackPresetBar> safe (this);
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window, rename] (int result) {
                const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());
                if (result != 1 || name.isEmpty() || safe == nullptr || safe->model == nullptr)
                    return;
                auto& m = *safe->model;
                if (auto chosen = m.lastChosen(); rename && chosen.has_value() && chosen->isUser())
                {
                    const auto to = chosen->file.getSiblingFile (name + m.extension());
                    if (chosen->file.moveFileTo (to))
                    {
                        chosen->file = to;
                        chosen->name = name;
                        m.setLastChosen (chosen);
                    }
                    safe->refreshText();
                }
                else
                    safe->saveAs (name);
            }), true);
        }
        juce::Rectangle<float> leftArea() const { return getLocalBounds().toFloat().removeFromLeft (32.0f); }
        juce::Rectangle<float> rightArea() const { return getLocalBounds().toFloat().removeFromRight (32.0f); }
        void timerCallback() override
        {
            const auto before = shown;
            refreshText();
            if (shown != before)
                repaint();
        }
        void refreshText()
        {
            if (model == nullptr)
            {
                shown = {};
                return;
            }
            const int m = model->mode();
            matched = edited = false;
            // The last one chosen first (two presets may hold the same values), then the rest.
            const auto last = model->lastChosen();
            if (last.has_value() && model->modeOf (*last) == m && model->matches (*last))
            {
                shown = last->name;
                matched = true;
                return;
            }
            for (const auto& p : relevant())
                if (model->matches (p))
                {
                    shown = p.name;
                    matched = true;
                    return;
                }
            // Edited: the chosen one keeps its name, with a quiet asterisk; nothing chosen: CUSTOM.
            edited = last.has_value() && model->modeOf (*last) == m;
            shown = edited ? last->name + " *" : juce::String ("CUSTOM");
        }

        RackPresetModel* model = nullptr;
        juce::Colour accent;
        juce::String shown;
        bool matched = false, edited = false;
        int hover = 0;
    };

    /** A control cell that is not a knob (a choice, FREE / SYNC): the knobs' caption over it,
        the key where a knob's dial is, so a row of cells reads as one. */
    using RackCell = rackui::Cell;

    //==========================================================================
    /** The modules' common part: the five slots at SPACE's geometry (rackcolumns). */
    //==========================================================================
    /**
        V4's TAPE deck (opt-in: rack::Faceplate::tapeDeck): two light reels, the tape around two
        guides and the head, a readout between the reels. One drawing for ECHO's tape echo and
        MOVEMENT's wow and flutter; what it reads and how the reels turn is each module's own
        (Reading). The reels turn only while the module plays and the instrument sounds (the
        host's output peak: tails included); they ease to rest in silence. Nothing here is a
        transport: no position, no elapsed time. Animated only while showing.
    */
    class TapeDeck final : public juce::Component, private juce::Timer
    {
    public:
        struct Reading
        {
            juce::String value, unit, note;   ///< "1/8D" / "375 ms" / "SYNC"
            bool noteLit = false;
            int ticks = 0, tick = -1;         ///< the sync divisions row (tick: the chosen one)
            double radiansPerSecond = 0.0;    ///< the take-up reel's speed while playing
            double wowHz = 0.0, wowDepth = 0.0, flutterHz = 0.0, flutterDepth = 0.0;   ///< MOVEMENT: the speed's own wobble
            bool active = false;              ///< the module plays (its macro, its power)
        };
        using Reader = std::function<Reading()>;

        TapeDeck (InstrumentHost& h, Reader r, juce::Colour accentColour) : host (h), reader (std::move (r)), accent (accentColour)
        {
            setInterceptsMouseClicks (false, false);
            startTimerHz (4);
        }
        ~TapeDeck() override { stopTimer(); }

        /** Tests and stills: the reels' angle and speed (a snapshot draws them at rest). */
        double reelAngle() const noexcept { return angle; }
        double reelSpeed() const noexcept { return speed; }
        /** Advances the reels by `seconds` (the timer's step; tests drive it directly). */
        void advance (double seconds)
        {
            const auto r = reader();
            const float peak = host.outputPeak();
            const bool sounding = peak < 0.0f || peak > 3.0e-4f;   // (unknown, or above -70 dBFS)
            double target = r.active && sounding ? r.radiansPerSecond : 0.0;
            clock += seconds;
            if (target > 0.0 && (r.wowDepth > 0.0 || r.flutterDepth > 0.0))
                target *= 1.0 + r.wowDepth * std::sin (juce::MathConstants<double>::twoPi * r.wowHz * clock)
                          + r.flutterDepth * std::sin (juce::MathConstants<double>::twoPi * r.flutterHz * clock);
            speed += (target - speed) * (1.0 - std::exp (-seconds / 0.35));   // (eases to rest)
            if (std::abs (speed) < 1.0e-3 && target == 0.0)
                speed = 0.0;
            angle = std::fmod (angle + speed * seconds, juce::MathConstants<double>::twoPi);
            deviation = r.radiansPerSecond > 0.0 && target > 0.0 ? target / r.radiansPerSecond - 1.0 : 0.0;
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = reader();
            const auto b = getLocalBounds().toFloat();
            // The reels: as large as the deck allows (84 - 106 px at the canonical size).
            const float d = juce::jlimit (60.0f, 106.0f, std::min (b.getHeight() - 30.0f, 0.36f * b.getWidth()));
            const float cy = b.getY() + 10.0f + 0.5f * d;
            const float lx = b.getX() + 0.27f * b.getWidth(), rx = b.getRight() - 0.27f * b.getWidth();
            const float supply = 0.43f, takeUp = 0.31f;   // (the tape packs' radii, of d)

            // The tape: from the supply pack, under a guide, across the head, a guide, up to the
            // take-up pack (drawn first: the reels' flanges sit over it).
            const float guideY = cy + 0.5f * d + 10.0f, headY = guideY + 2.0f;
            const auto guideL = juce::Point<float> (lx + 0.36f * d, guideY), guideR = juce::Point<float> (rx - 0.36f * d, guideY);
            const auto headC = juce::Point<float> (b.getCentreX(), headY);
            juce::Path tape;
            tape.startNewSubPath (lx - supply * d * 0.25f, cy + supply * d * 0.97f);
            tape.quadraticTo (lx + 0.1f * d, guideY + 5.0f, guideL.x, guideL.y + 5.0f);
            tape.lineTo (headC.x, headC.y + 7.0f);
            tape.lineTo (guideR.x, guideR.y + 5.0f);
            tape.quadraticTo (rx - 0.1f * d, guideY + 5.0f, rx + takeUp * d * 0.25f, cy + takeUp * d * 0.97f);
            g.setColour (juce::Colour (0xff6e6450));
            g.strokePath (tape, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            paintReel (g, { lx, cy }, d, supply, angle * takeUp / supply);
            paintReel (g, { rx, cy }, d, takeUp, angle);

            for (const auto& gp : { guideL, guideR })
            {
                const auto post = juce::Rectangle<float> (11.0f, 11.0f).withCentre (gp.translated (0.0f, 5.0f));
                g.setColour (juce::Colour (0x1f2a1d12));
                g.fillEllipse (post.translated (0.0f, 1.2f));
                g.setColour (juce::Colour (0xfff4eee4));
                g.fillEllipse (post);
                g.setColour (juce::Colour (0xffb9ab95));
                g.drawEllipse (post.reduced (0.5f), 1.0f);
                g.setColour (juce::Colour (0xff6c6458));
                g.fillEllipse (post.withSizeKeepingCentre (3.0f, 3.0f));
            }
            // The head: a champagne block, its gap lit amber while the tape runs.
            const auto head = juce::Rectangle<float> (30.0f, 15.0f).withCentre (headC.translated (0.0f, 5.0f));
            g.setColour (juce::Colour (0x1f2a1d12));
            g.fillRoundedRectangle (head.translated (0.0f, 1.5f), 3.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffefe6d6), 0.0f, head.getY(), juce::Colour (0xffd8ccb8), 0.0f, head.getBottom(), false));
            g.fillRoundedRectangle (head, 3.0f);
            g.setColour (juce::Colour (0xffb3a48d));
            g.drawRoundedRectangle (head.reduced (0.5f), 3.0f, 1.0f);
            const bool running = speed > 0.05;
            g.setColour (running ? juce::Colour (0xffe0883a) : juce::Colour (0xffa1968a));
            g.fillRect (juce::Rectangle<float> (head.getCentreX() - 0.75f, head.getY() + 3.0f, 1.5f, head.getHeight() - 6.0f));

            // The readout between the reels: the value large, its unit, the divisions under it.
            const auto mid = juce::Rectangle<float> (lx + 0.5f * d + 6.0f, cy - 0.5f * d, rx - lx - d - 12.0f, d);
            const auto ink = rackui::inkFor (*this);   // (V5: on an enamel face)
            g.setColour (ink ? ink->value : juce::Colour (0xff34383a));
            g.setFont (fonts::make (19.0f, fonts::Weight::semibold, 0.02f));
            g.drawFittedText (r.value, mid.withHeight (24.0f).translated (0.0f, 6.0f).getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.8f);
            g.setColour (ink ? ink->caption : juce::Colour (0xff7a7268));
            g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.03f));
            g.drawFittedText (r.unit, mid.withTrimmedTop (31.0f).withHeight (14.0f).getSmallestIntegerContainer(), juce::Justification::centred, 1, 0.85f);
            if (r.note.isNotEmpty())
            {
                const auto f = fonts::make (9.5f, fonts::Weight::semibold, 0.08f);
                const float tw = juce::GlyphArrangement::getStringWidth (f, r.note);
                const auto noteRow = juce::Rectangle<float> (tw + 12.0f, 12.0f).withCentre ({ mid.getCentreX(), mid.getY() + 53.0f });
                g.setColour (r.noteLit ? juce::Colour (0xffe0883a) : juce::Colour (0xffb7ada0));
                g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ noteRow.getX() + 2.5f, noteRow.getCentreY() }));
                g.setColour (ink ? ink->caption : juce::Colour (0xff7a7268));
                g.setFont (f);
                g.drawText (r.note, noteRow.withTrimmedLeft (9.0f), juce::Justification::centredLeft, false);
            }
            if (r.ticks > 1)
            {
                // The sync divisions (ECHO), or the speed's deviation (MOVEMENT, -1 when none).
                const float w = std::min (mid.getWidth() - 8.0f, 9.0f * static_cast<float> (r.ticks));
                const float x0 = mid.getCentreX() - 0.5f * w, step = w / static_cast<float> (r.ticks - 1), ty = mid.getY() + 70.0f;
                int lit = r.tick;
                if (lit < 0)
                    lit = juce::jlimit (0, r.ticks - 1, juce::roundToInt (0.5f * static_cast<float> (r.ticks - 1) * (1.0f + juce::jlimit (-1.0f, 1.0f, static_cast<float> (deviation) * 6.0f))));
                for (int i = 0; i < r.ticks; ++i)
                {
                    const bool on = i == lit;
                    g.setColour (on ? juce::Colour (0xffe0883a) : (ink ? ink->quiet : juce::Colour (0xffc4b9aa)));
                    g.fillRoundedRectangle (juce::Rectangle<float> (on ? 2.4f : 1.4f, on ? 11.0f : 7.0f).withCentre ({ x0 + step * static_cast<float> (i), ty }), 0.7f);
                }
            }
        }

    private:
        void paintReel (juce::Graphics& g, juce::Point<float> c, float d, float pack, double turn) const
        {
            const auto disc = juce::Rectangle<float> (d, d).withCentre (c);
            // Shadow, the tape pack under the flange, the flange (ivory) with three windows.
            g.setColour (juce::Colour (0x1c2a1d12));
            g.fillEllipse (disc.translated (0.0f, 2.5f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f3ea), disc.getX(), disc.getY(), juce::Colour (0xffe6dccd), disc.getRight(), disc.getBottom(), false));
            g.fillEllipse (disc);
            g.setColour (juce::Colour (0xffc6b8a2));
            g.drawEllipse (disc.reduced (0.6f), 1.2f);
            g.setColour (juce::Colour (0xffd9cdbb));
            g.drawEllipse (disc.reduced (3.5f), 0.8f);
            const float inner = 0.17f * d, outer = 0.43f * d;
            for (int w = 0; w < 3; ++w)
            {
                const float a0 = static_cast<float> (turn) + juce::MathConstants<float>::twoPi * static_cast<float> (w) / 3.0f;
                juce::Path window;
                window.addCentredArc (c.x, c.y, outer, outer, 0.0f, a0 - 0.55f, a0 + 0.55f, true);
                window.addCentredArc (c.x, c.y, inner, inner, 0.0f, a0 + 0.42f, a0 - 0.42f, false);
                window.closeSubPath();
                g.setColour (juce::Colour (0xffa09787));   // (the opening: earthy grey)
                g.fillPath (window);
                {
                    // The tape pack seen through it (warm olive), out to its radius.
                    juce::Graphics::ScopedSaveState clip (g);
                    g.reduceClipRegion (window);
                    g.setColour (juce::Colour (0xff786d58));
                    g.fillEllipse (juce::Rectangle<float> (2.0f * pack * d, 2.0f * pack * d).withCentre (c));
                    g.setColour (juce::Colour (0x22ffffff));
                    g.drawEllipse (juce::Rectangle<float> (2.0f * pack * d, 2.0f * pack * d).withCentre (c).reduced (0.5f), 1.0f);
                }
                g.setColour (juce::Colour (0x40000000));
                g.strokePath (window, juce::PathStrokeType (0.8f));
            }
            // The hub: warm metal, the spindle, three keys.
            const auto hub = juce::Rectangle<float> (0.27f * d, 0.27f * d).withCentre (c);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe6d9c1), hub.getX(), hub.getY(), juce::Colour (0xffb09f80), hub.getRight(), hub.getBottom(), false));
            g.fillEllipse (hub);
            g.setColour (juce::Colour (0xff9d8d70));
            g.drawEllipse (hub.reduced (0.5f), 1.0f);
            g.setColour (juce::Colour (0xff5a5348));
            g.fillEllipse (juce::Rectangle<float> (0.08f * d, 0.08f * d).withCentre (c));
            for (int k = 0; k < 3; ++k)
            {
                const float a = static_cast<float> (turn) + juce::MathConstants<float>::twoPi * (static_cast<float> (k) / 3.0f + 1.0f / 6.0f);
                const auto from = c.getPointOnCircumference (0.055f * d, a), to = c.getPointOnCircumference (0.115f * d, a);
                g.drawLine ({ from, to }, 1.3f);
            }
        }

        void timerCallback() override
        {
            // 30 fps while showing (and moving or about to); a still frame otherwise.
            const bool showing = isShowing();
            if (showing != wasShowing)
            {
                wasShowing = showing;
                startTimerHz (showing ? 30 : 4);
            }
            if (! showing)
                return;
            const double before = speed;
            advance (1.0 / 30.0);
            if (speed != 0.0 || before != 0.0)
                repaint();
            else if (const auto r = reader(); r.value != shownValue || r.unit != shownUnit || r.tick != shownTick)
            {
                shownValue = r.value;
                shownUnit = r.unit;
                shownTick = r.tick;
                repaint();
            }
        }

        InstrumentHost& host;
        Reader reader;
        juce::Colour accent;
        double angle = 0.0, speed = 0.0, clock = 0.0, deviation = 0.0;
        bool wasShowing = false;
        juce::String shownValue, shownUnit;
        int shownTick = -2;
    };

    class UnifiedRack : public rack::Module
    {
    public:
        UnifiedRack (InstrumentHost& p, juce::String name, design::colour::Macro macro) : processor (p), heading (std::move (name)), identity (macro)
        {
            presetBar = std::make_unique<RackPresetBar> (design::colour::macro (macro));
            addAndMakeVisible (*presetBar);
            presetActions.onClick = [this] { presetBar->showActions (presetActions); };
            addAndMakeVisible (presetActions);
        }

        juce::String title() const override { return heading; }
        juce::Colour accent() const override
        {
            // (V5: on an enamel face, the knobs' arcs in the face's own ink)
            if (plate != nullptr && plate->enamelBody && ! plate->arc.isTransparent())
                return plate->arc;
            return design::colour::macro (identity);
        }
        int bodyHeight() const override { return standardBody; }
        void settle() override
        {
            refresh();
            if (visual != nullptr)
                visual->settle();
            presetBar->repaint();
        }
        void resized() override { layout(); }
        void paint (juce::Graphics& g) override
        {
            if (plate != nullptr)
            {
                // The hardware faceplate (opt-in): its subtitle kept clear of the rail's parts.
                float limit = static_cast<float> (getWidth());
                for (auto* c : getChildren())
                    if (c->isVisible() && c->getBottom() <= titleOffset() + 1 && c->getX() > 200)
                        limit = std::min (limit, static_cast<float> (c->getX()));
                rack::hardware::paint (g, getLocalBounds(), titleOffset(), *plate, heading, processingActive(),
                                       visual != nullptr && ! expanded() ? visual->getBounds() : juce::Rectangle<int>(), limit - 14.0f);
            }
            if (deck != nullptr && deck->isVisible() && ! expanded())
            {
                // The deck's three regions: two quiet rules; the display's caption over it
                // (what it shows is predicted from the settings, not metered).
                const auto ink = rackui::inkFor (*this);
                for (const int x : deckDividers)
                {
                    if (ink)
                    {
                        g.setColour (ink->rule);
                        g.fillRect (juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (titleOffset() + 12), 1.0f, static_cast<float> (getHeight() - 22 - titleOffset() - 12)));
                    }
                    else
                        rackcolumns::paintDivider (g, x, static_cast<float> (titleOffset() + 12), static_cast<float> (getHeight() - 22));
                }
                if (visual != nullptr)
                {
                    const auto v = visual->getBounds();
                    g.setColour (ink ? ink->caption : rackui::palette::caption);
                    g.setFont (fonts::make (10.5f, fonts::Weight::medium, 0.07f));
                    g.drawText (deckCaption(), juce::Rectangle<int> (v.getX(), v.getY() - 20, v.getWidth(), 16), juce::Justification::centredLeft, false);
                    g.setColour (ink ? ink->quiet : rackui::palette::quietCaption);
                    g.setFont (fonts::make (9.5f, fonts::Weight::regular, 0.04f));
                    g.drawText ("PREDICTED", juce::Rectangle<int> (v.getX(), v.getY() - 20, v.getWidth(), 16), juce::Justification::centredRight, false);
                }
                return;
            }
            if (! expanded())
            {
                if (const auto ink = rackui::inkFor (*this))
                {
                    g.setColour (ink->rule);   // (V5: the divider in the face's ink)
                    g.fillRect (juce::Rectangle<float> (static_cast<float> (dividerX()), static_cast<float> (titleOffset() + 3), 1.0f, static_cast<float> (getHeight() - titleOffset() - 5)));
                }
                else
                    rackcolumns::paintDivider (g, dividerX(), static_cast<float> (titleOffset() + 3), static_cast<float> (getHeight() - 2));
            }
        }
        /** Tests: the TAPE deck (V4, opt-in), or null. */
        TapeDeck* tapeDeck() const noexcept { return deck.get(); }
        /** The product's hardware faceplate (opt-in; ANDOR ANALOG's): the preset browser and its
            actions go into the rail, the controls start after the stripes. Null: as approved. */
        void wearFaceplate (const rack::Faceplate* f)
        {
            plate = f;
            if (plate != nullptr)
            {
                presetActions.onRail = true;
                lightWatch.rack = this;
                lightWatch.startTimerHz (8);
                if (plate->enamelBody)
                    rackui::setInk (*this, { plate->ink, plate->ink.withAlpha (1.0f), plate->inkQuiet, plate->ink.withAlpha (0.2f) });
            }
            faceplateChanged();
            // (the cells already built take the face's arc)
            for (auto& cell : cells)
                if (auto* k = dynamic_cast<MiniKnob*> (cell.get()))
                    k->setArcColour (accent());
            if (modeSelector != nullptr)
                modeSelector->setAccent (accent());
            resized();
        }
        bool hasFaceplate() const noexcept { return plate != nullptr; }
        /** A module whose own parts depend on the faceplate (MOVEMENT's lane power). */
        virtual void faceplateChanged() {}
        void layoutTitleRow (juce::Rectangle<int> row) override { layoutRail (row); }

        /** Tests: the cells' captions in order; the presets bar; the slots' places. */
        juce::String captions() const
        {
            juce::StringArray c;
            for (const auto& cell : cells)
            {
                if (auto* k = dynamic_cast<MiniKnob*> (cell.get()))
                    c.add (k->captionText());
                else if (auto* key = dynamic_cast<RackCell*> (cell.get()))
                    c.add (key->captionText());
            }
            for (const auto& k : deckKeys)
                c.add (k.caption);
            return c.joinIntoString (" ");
        }
        RackPresetBar& presets() noexcept { return *presetBar; }
        juce::Rectangle<int> slot (int index) const
        {
            switch (index)
            {
                case 2: return modeSelector != nullptr ? modeSelector->getBounds() : juce::Rectangle<int>();
                case 3: return presetBar->getBounds();
                case 4: return cells.empty() ? juce::Rectangle<int>() : cells.front()->getBounds();
                case 5: return visual != nullptr ? visual->getBounds() : juce::Rectangle<int>();
                case 6: return presetActions.getBounds();
                default: return {};
            }
        }
        /** Tests: every cell's bounds, in order. */
        std::vector<juce::Rectangle<int>> cellBounds() const
        {
            std::vector<juce::Rectangle<int>> b;
            for (const auto& cell : cells)
                b.push_back (cell->getBounds());
            return b;
        }
        /** Tests: the cell captioned `caption` (its control for a non-knob cell), or null. */
        juce::Component* cellNamed (const juce::String& caption) const
        {
            for (const auto& cell : cells)
            {
                if (auto* k = dynamic_cast<MiniKnob*> (cell.get()); k != nullptr && k->captionText() == caption)
                    return k;
                if (auto* c = dynamic_cast<RackCell*> (cell.get()); c != nullptr && c->captionText() == caption)
                    return &c->key();
            }
            for (const auto& k : deckKeys)
                if (k.caption == caption)
                    return k.control.get();
            return nullptr;
        }
        /** The layout family (0 standard, 1 dense: eight controls or more). */
        bool dense() const noexcept { return static_cast<int> (cells.size()) >= rackcolumns::denseFrom; }

        // The approved rack's rows (reference pixels): the selector row 39 under the title
        // (its 27 px fields centred), 8 px, then the controls (103 px cells); the graph 160 px.
        static constexpr int standardBody = 172, modeWidth = 165, presetWidth = 294, fieldHeight = 27, selectRow = 39, rowGap = 8, cellHeight = 112,
                             graphHeight = 160, actionsWidth = 30;
        static_assert (cellHeight == rackui::cellHeight);

    protected:
        /** The controls' share of the content width (the approved column: per module). */
        virtual float leftShare() const = 0;
        /** What the faceplate's LED shows: the module plays (its macro above 0). */
        virtual bool processingActive() const { return true; }
        /** Faceplate: the preset browser and its actions at the rail's right; returns what is
            left of the row (a module's own title-row parts go there). */
        juce::Rectangle<int> layoutRail (juce::Rectangle<int> row)
        {
            if (plate == nullptr)
                return row;
            row = row.withSizeKeepingCentre (row.getWidth(), 26);
            presetActions.setBounds (row.removeFromRight (actionsWidth).withSizeKeepingCentre (actionsWidth, 22));
            row.removeFromRight (8);
            presetBar->setBounds (row.removeFromRight (railPresetWidth).withSizeKeepingCentre (railPresetWidth, 24));
            row.removeFromRight (16);
            return row;
        }
        static constexpr int railPresetWidth = 236;
        int contentX() const noexcept { return plate != nullptr ? rack::hardware::measures (*plate).contentLeft() : rackcolumns::inset; }
        int contentRightMargin() const noexcept { return plate != nullptr ? rack::hardware::measures (*plate).contentRightMargin() : rackcolumns::inset; }
        int dividerX() const
        {
            const int x0 = contentX(), x1 = getWidth() - contentRightMargin();
            return x0 + juce::roundToInt (leftShare() * static_cast<float> (x1 - x0)) + rackcolumns::columnGap;
        }
        const rack::Faceplate* plate = nullptr;

        ValueSelector& setModeSelector (const juce::String& id, const juce::String& tip)
        {
            if (modeSelector != nullptr)
                removeChildComponent (modeSelector.get());
            modeSelector = std::make_unique<ValueSelector> (*processor.apvts().getParameter (id), juce::String());
            modeSelector->setAccent (accent());
            modeSelector->setRackStyle (ValueSelector::RackStyle::mode);
            modeSelector->setTooltip (tip);
            addAndMakeVisible (*modeSelector);
            return *modeSelector;
        }
        void setVisual (std::unique_ptr<Visual> v, bool scaled = true)
        {
            if (visual != nullptr)
                removeChildComponent (visual.get());
            visual = std::move (v);
            visual->setIdentity (identity);
            visual->setDetailed (true);
            if (scaled)
                visual->setPictureScale (1.1f);
            addAndMakeVisible (*visual);
        }
        /** A continuous ceramic knob (the approved cell: caption, 59 px knob, value). */
        MiniKnob& knob (const juce::String& id, const char* caption, MiniKnob::Formatter f)
        {
            auto k = std::make_unique<MiniKnob> (processor.apvts(), id, caption, std::move (f));
            k->setArcColour (accent());
            k->setRackMetrics (true);
            auto& ref = *k;
            addAndMakeVisible (ref);
            cells.push_back (std::move (k));
            return ref;
        }
        /** A stepped rate (a choice parameter): the same knob with its detents marked. */
        MiniKnob& steppedKnob (const juce::String& id, const char* caption, MiniKnob::Formatter f)
        {
            auto& k = knob (id, caption, std::move (f));
            k.setDetents (true, accent());
            return k;
        }
        /** A compact list (PATTERN, INTERVAL, MOTION): more than three, or long, choices. */
        ValueSelector& listCell (const juce::String& id, const juce::String& caption, const juce::String& tip)
        {
            auto selector = std::make_unique<ValueSelector> (*processor.apvts().getParameter (id), juce::String());
            selector->setTooltip (tip);
            selector->setRackStyle (ValueSelector::RackStyle::option);
            auto& ref = *selector;
            addCell (caption, std::move (selector), RackCell::Kind::list);
            return ref;
        }
        /** A few short exclusive choices: the ceramic segmented switch. */
        rackui::SegmentedSwitch& segmentCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip)
        {
            auto segments = std::make_unique<rackui::SegmentedSwitch> (*processor.apvts().getParameter (id), std::move (positions), &processor.undo(), accent());
            segments->setTooltip (tip);
            auto& ref = *segments;
            addCell (caption, std::move (segments), RackCell::Kind::segments);
            return ref;
        }
        /** A real two- or three-state setting: the ceramic vertical switch. */
        rackui::VerticalSwitch& switchCell (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip)
        {
            const bool three = positions.size() == 3;
            auto key = std::make_unique<rackui::VerticalSwitch> (*processor.apvts().getParameter (id), std::move (positions), &processor.undo());
            key->setTooltip (tip);
            key->setTitle (caption);
            auto& ref = *key;
            addCell (caption, std::move (key), three ? RackCell::Kind::switch3 : RackCell::Kind::switch2);
            return ref;
        }
        void addCell (const juce::String& caption, std::unique_ptr<juce::Component> control, RackCell::Kind kind)
        {
            auto cell = std::make_unique<RackCell> (caption, std::move (control), kind);
            addAndMakeVisible (*cell);
            cells.push_back (std::move (cell));
        }
        void clearCells()
        {
            cells.clear();
            deckKeys.clear();
        }

        /** The TAPE deck (V4, opt-in by the faceplate): shown with its reader, or removed. */
        bool deckWanted() const noexcept { return plate != nullptr && plate->tapeDeck; }
        void setDeck (TapeDeck::Reader reader)
        {
            if (reader == nullptr)
            {
                if (deck != nullptr)
                    removeChildComponent (deck.get());
                deck.reset();
                return;
            }
            if (deck != nullptr)
                removeChildComponent (deck.get());
            deck = std::make_unique<TapeDeck> (processor, std::move (reader), accent());
            addAndMakeVisible (*deck);
            deck->toBack();
        }
        /** The deck's own small choices (ECHO's FREE / SYNC, MONO / PING / WIDE): a ceramic
            segmented switch in the deck's top row, on the same parameter as the cell it replaces. */
        rackui::SegmentedSwitch& deckSegments (const juce::String& id, const juce::String& caption, std::vector<rackui::Position> positions, const juce::String& tip)
        {
            auto segments = std::make_unique<rackui::SegmentedSwitch> (*processor.apvts().getParameter (id), std::move (positions), &processor.undo(), accent());
            segments->setTooltip (tip);
            segments->setTitle (caption);
            auto& ref = *segments;
            addAndMakeVisible (ref);
            deckKeys.push_back ({ caption, std::move (segments) });
            return ref;
        }
        /** The deck display's caption ("REPEATS", "SPEED"). */
        virtual juce::String deckCaption() const { return {}; }

        /** An expanded precision editor (SHAPER's) is open: the module lays itself out. */
        virtual bool expanded() const { return false; }
        virtual void layoutExpanded (juce::Rectangle<int>) {}
        /** The picture's place (the column; a module may put something over it). */
        virtual void placeVisual (juce::Rectangle<int> column) { visual->setBounds (column); }

        void layout()
        {
            if (visual == nullptr)
                return;
            const int width = getWidth(), x0 = contentX(), top = titleOffset();
            // MODE, PRESET (fixed widths: never stretched to the divider) and the preset actions
            // (with a faceplate, PRESET and its actions are in the rail: layoutRail).
            const int fieldY = top + (selectRow - fieldHeight) / 2;
            if (modeSelector != nullptr)
                modeSelector->setBounds (x0, fieldY, modeWidth, fieldHeight);
            if (plate == nullptr)
            {
                presetBar->setBounds (x0 + modeWidth + 12, fieldY, presetWidth, fieldHeight);
                presetActions.setBounds (presetBar->getRight() + 12, fieldY, actionsWidth, fieldHeight);
            }
            const int controlsY = top + selectRow + rowGap;
            if (expanded())
            {
                for (auto& cell : cells)
                    cell->setVisible (false);
                layoutExpanded (juce::Rectangle<int> (x0, controlsY, width - x0 - contentRightMargin(), getHeight() - rackcolumns::bottom - controlsY));
                repaint();
                return;
            }
            if (deck != nullptr)
            {
                // The TAPE deck: the knobs left (27 - 32 %, the dense slots), the reels in the
                // middle (33 - 39 %), the small display right; one substrate, two quiet rules.
                const int x1 = width - contentRightMargin(), w = x1 - x0;
                const int leftEnd = x0 + juce::roundToInt (0.29f * static_cast<float> (w));
                const int deckEnd = x0 + juce::roundToInt (0.66f * static_cast<float> (w));
                deckDividers = { leftEnd + rackcolumns::columnGap, deckEnd + rackcolumns::columnGap };
                int x = x0;
                for (auto& cell : cells)
                {
                    auto slotKind = rackcolumns::Slot::knob;
                    if (auto* c = dynamic_cast<RackCell*> (cell.get()))
                        slotKind = c->slot();
                    const int cw = rackcolumns::slotWidth (slotKind, true);
                    cell->setVisible (true);
                    cell->setBounds (x, controlsY, cw, cellHeight);
                    x += cw + rackcolumns::slotGap (true);
                }
                const int deckX = deckDividers[0] + 1 + rackcolumns::graphPad, deckRight = deckDividers[1] - rackcolumns::graphPad;
                const int bottomY = getHeight() - 16;
                // Its own small choices in its top row (the selector row's centre line).
                const int keyY = top + (selectRow - 24) / 2;
                if (! deckKeys.empty())
                    deckKeys.front().control->setBounds (deckX, keyY, 108, 24);
                if (deckKeys.size() > 1)
                    deckKeys[1].control->setBounds (deckRight - 156, keyY, 156, 24);
                const int deckTop = deckKeys.empty() ? top + 8 : top + selectRow - 2;
                deck->setBounds (deckX, deckTop, deckRight - deckX, bottomY - deckTop);
                // The display: about 320 x 120, centred in its column under its caption.
                const int rx = deckDividers[1] + 1 + rackcolumns::graphPad, rw = x1 - rx;
                const int dw = std::min (rw, 336), dh = std::min (122, bottomY - (top + 32));
                visual->setBounds (rx + (rw - dw) / 2, top + 32, dw, dh);
                repaint();
                return;
            }
            const int pictureX = dividerX() + 1 + rackcolumns::graphPad;
            if (plate != nullptr && plate->fullBleed)
                placeVisual ({ pictureX, top + 7, width - contentRightMargin() - pictureX, std::max (graphHeight - 4, getHeight() - top - 7 - 16) });   // (the face's own bottom margin)
            else if (plate != nullptr)
                placeVisual ({ pictureX, top + 7, width - contentRightMargin() - pictureX, graphHeight - 4 });   // (room for its bezel)
            else
                placeVisual ({ pictureX, top + 4, width - rackcolumns::inset - pictureX, graphHeight });
            // The controls: left-aligned from one origin on the approved slot widths; a mode with
            // fewer leaves the rest of the column empty. (The family is the mode's: its count.)
            const bool isDense = dense();
            int x = x0;
            for (auto& cell : cells)
            {
                auto slot = rackcolumns::Slot::knob;
                if (auto* c = dynamic_cast<RackCell*> (cell.get()))
                    slot = c->slot();
                const int w = rackcolumns::slotWidth (slot, isDense);
                cell->setVisible (true);
                cell->setBounds (x, controlsY, w, cellHeight);
                x += w + rackcolumns::slotGap (isDense);
            }
            jassert (x - rackcolumns::slotGap (isDense) <= dividerX() - rackcolumns::columnGap);
            repaint();
        }

        /** The preset actions (approved "···" key beside PRESET): save, rename, delete, the folder,
            and the module's own (MOVEMENT's lanes). */
        class ActionsKey final : public juce::Button
        {
        public:
            ActionsKey() : juce::Button ("Preset actions")
            {
                setTooltip ("Preset actions: save, rename, delete");
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
                setWantsKeyboardFocus (true);
                setMouseClickGrabsKeyboardFocus (false);
            }
            bool onRail = false;   // (the faceplate's rail: light dots on the enamel)
            void paintButton (juce::Graphics& g, bool highlighted, bool down) override
            {
                const auto r = getLocalBounds().toFloat();
                if (onRail)
                {
                    g.setColour (juce::Colour (0xfff3eadb).withAlpha (highlighted || down || hasKeyboardFocus (false) ? 1.0f : 0.78f));
                    for (int i = -1; i <= 1; ++i)
                        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre ({ r.getCentreX() + 5.5f * static_cast<float> (i), r.getCentreY() }));
                    return;
                }
                g.setColour (juce::Colour (0xffd2cbbf));
                g.fillRoundedRectangle (r, 5.0f);
                g.setColour (highlighted || down ? juce::Colour (0xfff2ece2) : juce::Colour (0xfff7f2ea));
                g.fillRoundedRectangle (r.reduced (1.0f), 4.0f);
                g.setColour (juce::Colour (0xff746e65));
                for (int i = -1; i <= 1; ++i)
                    g.fillEllipse (juce::Rectangle<float> (2.0f, 2.0f).withCentre ({ r.getCentreX() + 4.5f * static_cast<float> (i), r.getCentreY() }));
                if (hasKeyboardFocus (false))
                {
                    g.setColour (rackui::palette::focus);
                    g.drawRoundedRectangle (r.expanded (2.0f), 7.0f, 2.0f);
                }
            }
        };

        /** Faceplate: the LED follows what plays (repainted when it changes). */
        struct LightWatch final : juce::Timer
        {
            UnifiedRack* rack = nullptr;
            bool shown = true;
            void timerCallback() override
            {
                if (rack != nullptr && rack->processingActive() != shown)
                {
                    shown = ! shown;
                    rack->repaint (0, 0, 120, rack->titleOffset());
                }
            }
        } lightWatch;

        InstrumentHost& processor;
        juce::String heading;
        design::colour::Macro identity;
        std::unique_ptr<ValueSelector> modeSelector;
        std::unique_ptr<RackPresetBar> presetBar;
        ActionsKey presetActions;
        std::unique_ptr<Visual> visual;
        std::vector<std::unique_ptr<juce::Component>> cells;
        std::unique_ptr<TapeDeck> deck;
        struct DeckKey
        {
            juce::String caption;
            std::unique_ptr<juce::Component> control;
        };
        std::vector<DeckKey> deckKeys;
        std::array<int, 2> deckDividers {};
    };

    //==========================================================================
    /**
        MOVEMENT in the rack: a lane's MODE (DRIFT, TAPE, CHORUS, PULSE, SHAPER) and its PRESET,
        then that mode's controls only, the picture on the right. The title row has only the
        A / B selector at its right (each letter with its lane's light; its menu copies or swaps
        the lanes, as the presets' menu does), then the shown lane's power. SHAPER's pattern is drawn on in place; its magnifier opens the precision
        editor across the rack, with the brushes.
    */
    class MovementRack final : public UnifiedRack
    {
    public:
        explicit MovementRack (InstrumentHost& p) : UnifiedRack (p, "MOVEMENT", design::colour::Macro::movement), lane (p, 0)
        {
            for (int l = 0; l < p.movementLaneCount(); ++l)
                models.push_back (std::make_unique<MovementPresetModel> (p, l));
            magnifier.onClick = [this] {
                wide = ! wide;
                buildMode();
                if (onHeightChanged != nullptr)
                    onHeightChanged();
            };
            addChildComponent (magnifier);
            if (p.movementLaneCount() > 1)
                for (int l = 0; l < 2; ++l)
                {
                    auto tab = std::make_unique<rackui::LaneKey> (l == 0 ? "A" : "B");
                    tab->setTitle (l == 0 ? "Lane A" : "Lane B");
                    tab->lit = [this, l] { return laneOn (l); };
                    tab->onClick = [this, l] { showLane (l); };
                    tab->onMenu = [this, l] { laneMenu (l); };
                    addAndMakeVisible (*tab);
                    laneTabs.push_back (std::move (tab));
                }
            buildLane();
        }

        int bodyHeight() const override { return wide ? expandedBody : standardBody; }
        float leftShare() const override { return 0.57f; }   // approved MOVEMENT column
        bool processingActive() const override { return processor.parameterValue ("motion") > 0.0f; }
        static constexpr int expandedBody = 268;

        void layoutTitleRow (juce::Rectangle<int> row) override
        {
            // Approved: MOVEMENT ... [A][B]  ON (switch)  (collapse) - the lane keys 40 x 23, 2 px
            // apart; 7 px; the word ON; 7 px; the shown lane's 16 x 26 switch; 8 px to collapse.
            // (With a faceplate the preset browser takes the rail's right first.)
            row = layoutRail (row);
            row = row.withSizeKeepingCentre (row.getWidth(), 30);
            if (powerSlide != nullptr)
            {
                // Full bleed: the horizontal slide (its word inside) on the rail's centre line.
                powerSlide->setBounds (row.removeFromRight (rackui::PowerKey::slideWidth).withSizeKeepingCentre (rackui::PowerKey::slideWidth, rackui::PowerKey::slideHeight));
                row.removeFromRight (12);
            }
            if (power != nullptr)
            {
                power->setBounds (row.removeFromRight (16).withSizeKeepingCentre (16, 26));
                row.removeFromRight (7);
                onWord = row.removeFromRight (onWordWidth);
                row.removeFromRight (7);
            }
            for (auto it = laneTabs.rbegin(); it != laneTabs.rend(); ++it)
            {
                (*it)->setBounds (row.removeFromRight (rackui::LaneKey::width).withSizeKeepingCentre (rackui::LaneKey::width, rackui::LaneKey::height));
                row.removeFromRight (rackui::LaneKey::gap);
            }
        }
        static constexpr int onWordWidth = 12;
        void paint (juce::Graphics& g) override
        {
            UnifiedRack::paint (g);
            if (power != nullptr && power->isVisible())
            {
                // The power's word (approved: 9 px, #777069), beside the switch (cream on a rail).
                g.setColour (hasFaceplate() ? juce::Colour (0xfff3eadb) : juce::Colour (0xff777069));
                g.setFont (fonts::make (9.0f, fonts::Weight::regular, 0.03f));
                g.drawText ("ON", onWord, juce::Justification::centred, false);
            }
        }
        void refresh() override
        {
            if (builtMode != mode() || (builtMode == static_cast<int> (MovementMode::pulse) && builtSync != laneSynced()))
                buildMode();
            const juce::String letter = lane.index == 0 ? "A" : "B";
            for (std::size_t l = 0; l < laneTabs.size(); ++l)
            {
                const int index = static_cast<int> (l);
                const juce::String name = index == 0 ? "Lane A" : "Lane B";
                laneTabs[l]->setToggleState (index == lane.index, juce::dontSendNotification);
                laneTabs[l]->setTooltip (name + (laneOn (index) ? " (on)" : " (off)")
                                         + (index == 0 ? ": MOVEMENT itself" : ": a second MOVEMENT after A")
                                         + juce::String::fromUTF8 (" \xc2\xb7 Right-click to copy or swap lanes"));
                laneTabs[l]->repaint();
            }
            if (power != nullptr || powerSlide != nullptr)
            {
                const bool on = laneOn (lane.index);
                const auto tip = on ? "Lane " + letter + " plays (click: off; the other lane stays as it is)" : "Lane " + letter + " is off (click: on)";
                if (power != nullptr)
                    power->setTooltip (tip);
                if (powerSlide != nullptr)
                    powerSlide->setTooltip (tip);
            }
        }

        int laneShown() const noexcept { return lane.index; }
        /** Tests: what the title row shows (the components in it, by name), the ON, the menu. */
        juce::StringArray titleRowItems() const
        {
            juce::StringArray items;
            for (auto* child : getChildren())
                if (child->isVisible() && child->getY() < titleOffset())
                    items.add (child->getTitle().isNotEmpty() ? child->getTitle() : child->getName());
            return items;
        }
        juce::Rectangle<int> laneSwitchBounds() const
        {
            if (powerSlide != nullptr && powerSlide->isVisible())
                return powerSlide->getBounds();
            return power != nullptr && power->isVisible() ? power->getBounds() : juce::Rectangle<int>();
        }
        void clickLaneSwitch()
        {
            if (powerSlide != nullptr)
                powerSlide->toggle();
            if (power != nullptr)
                power->choose (power->positionIndex() == 0 ? 1 : 0);
        }
        juce::StringArray laneActionNames()
        {
            juce::StringArray names;
            const auto menu = laneActions();
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.isNotEmpty())
                    names.add (it.getItem().text);
            return names;
        }
        void runLaneAction (int index)
        {
            int i = 0;
            const auto menu = laneActions();
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.isNotEmpty() && i++ == index && it.getItem().action != nullptr)
                    it.getItem().action();
        }
        juce::StringArray presetMenuExtras() const
        {
            juce::PopupMenu menu;
            juce::StringArray names;
            if (auto* m = presetBar->presetModel())
                m->addMenuItems (menu);
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
                if (it.getItem().text.isNotEmpty())
                    names.add (it.getItem().text);
            return names;
        }
        void showLane (int index)
        {
            index = std::clamp (index, 0, processor.movementLaneCount() - 1);
            if (index == lane.index)
                return;
            lane = Lane (processor, index);
            buildLane();
        }
        bool isWide() const noexcept { return wide; }
        void setWide (bool shouldBeWide)
        {
            if (wide != shouldBeWide)
                magnifier.onClick();
        }

    private:
        int mode() const { return std::clamp (juce::roundToInt (value (processor.apvts(), lane ("mode"))), 0, 4); }
        bool laneSynced() const { return lane.ids.syncId.isNotEmpty() && value (processor.apvts(), lane.ids.syncId) >= 0.5f; }
        bool expanded() const override { return wide; }

        void faceplateChanged() override
        {
            if (plate != nullptr && plate->fullBleed && powerSlide == nullptr)
                buildLane();   // (the lane's power as the rail's slide)
        }
        void buildLane()
        {
            auto& m = setModeSelector (lane ("mode"), "MOVEMENT's kind: DRIFT, TAPE, CHORUS, PULSE or the rhythmic SHAPER (each keeps its own settings)");
            m.onChange = [this] (int) {
                juce::Component::SafePointer<MovementRack> safe (this);
                juce::MessageManager::callAsync ([safe] {
                    if (safe != nullptr && safe->builtMode != safe->mode())
                        safe->buildMode();
                });
            };
            presetBar->setModel (models[static_cast<std::size_t> (std::clamp (lane.index, 0, static_cast<int> (models.size()) - 1))].get());
            // The shown lane's power: the approved mini switch, bound to that lane's ON (ON up).
            if (power != nullptr)
                removeChildComponent (power.get());
            power.reset();
            if (powerSlide != nullptr)
                removeChildComponent (powerSlide.get());
            powerSlide.reset();
            if (lane.ids.onId.isNotEmpty() && plate != nullptr && plate->fullBleed)
            {
                // Full bleed: the lane's ON as the rail's horizontal slide (the same parameter).
                if (auto* on = processor.apvts().getParameter (lane.ids.onId))
                {
                    powerSlide = std::make_unique<rackui::PowerKey> (*on, &processor.undo(), plate->glow);
                    powerSlide->setStyle (rackui::PowerKey::Style::slide);
                    powerSlide->setTitle ("Lane power");
                    powerSlide->onChange = [this] (bool) {
                        for (auto& t : laneTabs)
                            t->repaint();
                        refresh();
                    };
                    addAndMakeVisible (*powerSlide);
                }
            }
            else if (lane.ids.onId.isNotEmpty())
                if (auto* on = processor.apvts().getParameter (lane.ids.onId))
                {
                    power = std::make_unique<rackui::VerticalSwitch> (*on, std::vector<rackui::Position> { { "ON", "On", 1.0f }, { "OFF", "Off", 0.0f } }, &processor.undo(),
                                                                      rackui::VerticalSwitch::Style::mini);
                    power->setTitle ("Lane power");
                    power->onChange = [this] (int) {
                        for (auto& t : laneTabs)
                            t->repaint();
                        refresh();
                    };
                    addAndMakeVisible (*power);
                }
            customAttachment.reset();
            if (auto* custom = processor.apvts().getParameter (lane ("shaper.custom")))
                customAttachment = std::make_unique<juce::ParameterAttachment> (*custom, [this] (float) { repaint(); });
            builtMode = -1;
            buildMode();
            if (getParentComponent() != nullptr)
                getParentComponent()->resized();   // (the title row: the lane shown, its power)
            refresh();
        }

        void buildMode()
        {
            clearCells();
            toolbar.reset();
            builtMode = mode();
            builtSync = laneSynced();
            const auto m = static_cast<MovementMode> (builtMode);
            const bool shaper = m == MovementMode::shaper;
            if (! shaper)
                wide = false;
            if (shaper)
                setVisual (std::make_unique<ShaperEditor> (processor, tool, true, lane.index), false);
            else
                setVisual (std::make_unique<MovementVisual> (processor, lane.index));
            magnifier.setExpanded (wide);
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            const juce::String level = lane.ids.levelId.isNotEmpty() ? lane.ids.levelId : juce::String ("motion");
            switch (m)
            {
                case MovementMode::drift:
                    knob (level, "INTENSITY", percent);
                    knob (lane ("drift.speed"), "SPEED", [] (double v) { return format::hertz (shaping::driftSpeedHz (v * 0.01)); });
                    knob (lane ("drift.pitch"), "PITCH", [] (double v) { return juce::String (shaping::driftPitchCents (v * 0.01), 1) + " c"; });
                    knob (lane ("drift.tone"), "TONE", percent);
                    break;
                case MovementMode::tape:
                    knob (level, "INTENSITY", percent);
                    knob (lane ("tape.wow"), "WOW", [] (double v) { return format::hertz (shaping::tapeWowHz (v * 0.01)); });
                    knob (lane ("tape.flutter"), "FLUTTER", [] (double v) { return format::hertz (shaping::tapeFlutterHz (v * 0.01)); });
                    knob (lane ("tape.wear"), "WEAR", percent);
                    break;
                case MovementMode::chorus:
                    knob (level, "INTENSITY", percent);
                    knob (lane ("chorus.rate"), "RATE", [] (double v) { return format::hertz (shaping::chorusRateHz (v * 0.01)); });
                    knob (lane ("chorus.width"), "WIDTH", [] (double v) { return format::milliseconds (shaping::chorusWidthMs (v * 0.01)); });
                    knob (lane ("chorus.stereo"), "STEREO", percent);
                    break;
                case MovementMode::pulse:
                {
                    // INTENSITY, TIMING (FREE up, SYNC down), one RATE knob in the same place for
                    // both - free in Hz, synced on the cycle's note values with detents - SHAPE,
                    // STEREO, PHASE (acting on the tempo's grid, so live only while synced).
                    knob (level, "INTENSITY", percent);
                    if (lane.ids.syncId.isNotEmpty())
                        switchCell (lane.ids.syncId, "TIMING", { { "FREE", "Free", 0.0f }, { "SYNC", "Sync", 1.0f } },
                                    "PULSE's rate: free (Hz), or a note value at the host's tempo");
                    if (builtSync && lane.ids.pulseDivisionId.isNotEmpty())
                    {
                        auto* division = processor.apvts().getParameter (lane.ids.pulseDivisionId);
                        steppedKnob (lane.ids.pulseDivisionId, "RATE", [division] (double v) {
                            return division != nullptr ? division->getText (division->convertTo0to1 (static_cast<float> (v)), 16) : juce::String (v);
                        });
                    }
                    else
                        knob (lane ("pulse.rate"), "RATE", [] (double v) { return format::hertz (shaping::pulseRateHz (v * 0.01)); });
                    knob (lane ("pulse.shape"), "SHAPE", percent);
                    knob (lane ("pulse.stereo"), "STEREO", percent);
                    if (lane.ids.phaseId.isNotEmpty())
                    {
                        auto& phase = knob (lane.ids.phaseId, "PHASE", [] (double v) { return juce::String (juce::roundToInt (v * 3.6)) + juce::String::fromUTF8 ("\xc2\xb0"); });
                        phase.setEnabled (builtSync);
                        phase.setAlpha (builtSync ? 1.0f : 0.45f);
                        phase.slider.setTooltip (builtSync ? "PHASE: where the cycle starts on the tempo's grid" : "PHASE acts on the tempo's grid: set TIMING to SYNC");
                    }
                    break;
                }
                case MovementMode::shaper:
                {
                    // PATTERN (a compact list), RATE (stepped: the engine's divisions), TARGET
                    // (VOL / FILTER / BOTH), DEPTH, SMOOTH, PHASE.
                    setUpPatternSelector (listCell (lane ("shaper.pattern"), "PATTERN", "Pattern: a curated one, or CUSTOM (your own steps; draw on the display)"));
                    auto* rate = processor.apvts().getParameter (lane ("shaper.rate"));
                    steppedKnob (lane ("shaper.rate"), "RATE", [rate] (double v) {
                        return rate != nullptr ? rate->getText (rate->convertTo0to1 (static_cast<float> (v)), 16) : juce::String (v);
                    });
                    segmentCell (lane ("shaper.target"), "TARGET", { { "VOL", "Volume", 0.0f }, { "FILTER", "Filter", 1.0f }, { "BOTH", "Both", 2.0f } },
                                 "What the pattern shapes: the volume, a low-pass filter, or both");
                    knob (level, "DEPTH", percent);
                    knob (lane ("shaper.smooth"), "SMOOTH", percent);
                    if (lane.ids.phaseId.isNotEmpty())
                        knob (lane.ids.phaseId, "PHASE", [] (double v) { return juce::String (juce::roundToInt (v * 0.16)) + "/16"; });
                    if (wide)
                    {
                        // The precision editor's brushes and actions.
                        toolbar = std::make_unique<ShaperToolbar> (tool, accent());
                        toolbar->onCopyFrom = [this] { copyFromMenu(); };
                        toolbar->onClear = [this] { setCustom ([] { ShaperPattern p; for (auto& st : p) st = { 1.0f, 1.0f, StepShape::hold }; return p; }()); };
                        toolbar->onSave = [this] { savePattern(); };
                        toolbar->onLoad = [this] { loadMenu(); };
                        toolbar->onToolChanged = [this] { if (visual != nullptr) visual->repaint(); };
                        addAndMakeVisible (*toolbar);
                    }
                    break;
                }
            }
            // V4's TAPE deck (opt-in): the reels' speed wobbles as WOW and FLUTTER move the tape.
            if (deckWanted() && m == MovementMode::tape)
                setDeck ([this] { return deckReading(); });
            else
                setDeck (nullptr);
            magnifier.setVisible (shaper);
            layout();
            magnifier.toFront (false);
        }
        juce::String deckCaption() const override { return juce::String::fromUTF8 ("SPEED  \xc2\xb7  WOW / FLUTTER"); }
        TapeDeck::Reading deckReading() const
        {
            auto& s = processor.apvts();
            const double a = 0.01 * value (s, lane ("tape.wow")), b = 0.01 * value (s, lane ("tape.flutter"));
            const double wear = 0.01 * value (s, lane ("tape.wear"));
            const juce::String level = lane.ids.levelId.isNotEmpty() ? lane.ids.levelId : juce::String ("motion");
            const double intensity = 0.01 * value (s, level);
            const double wowHz = shaping::tapeWowHz (a), flutterHz = shaping::tapeFlutterHz (b);
            TapeDeck::Reading r;
            r.value = format::hertz (wowHz);
            r.unit = "wow";
            r.note = "FLUTTER " + format::hertz (flutterHz);
            r.noteLit = wear > 0.0;   // (WEAR roughens it: the display shows how much)
            r.ticks = 9;   // (the speed's deviation, live)
            r.radiansPerSecond = 2.6;
            r.wowHz = wowHz;
            r.flutterHz = flutterHz;
            r.wowDepth = 0.2 * a * intensity;
            r.flutterDepth = 0.04 * b * intensity;
            r.active = processingActive() && laneOn (lane.index) && intensity > 0.0;
            return r;
        }

        void placeVisual (juce::Rectangle<int> column) override
        {
            visual->setBounds (column);
            magnifier.setBounds (juce::Rectangle<int> (20, 20).withPosition (column.getRight() - 26, column.getY() + 5));
            magnifier.toFront (false);
        }
        void layoutExpanded (juce::Rectangle<int> area) override
        {
            // The precision editor: the pattern across the rack, its brushes under it.
            if (toolbar != nullptr)
            {
                toolbar->setBounds (area.removeFromBottom (24));
                area.removeFromBottom (8);
            }
            visual->setBounds (area);
            magnifier.setBounds (juce::Rectangle<int> (20, 20).withPosition (area.getRight() - 26, area.getY() + 5));
            magnifier.toFront (false);
        }

        /** The lanes' actions (the A / B selector's menu): copy, swap; nothing else changes. */
        juce::PopupMenu laneActions()
        {
            juce::PopupMenu menu;
            addLaneActions (menu, processor);
            return menu;
        }
        void laneMenu (int fromTab)
        {
            juce::Component* target = fromTab >= 0 && fromTab < static_cast<int> (laneTabs.size()) ? laneTabs[static_cast<std::size_t> (fromTab)].get() : static_cast<juce::Component*> (this);
            laneActions().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target).withDeletionCheck (*this));
        }
        bool laneOn (int index) const
        {
            const Lane x (processor, index);
            return x.ids.onId.isEmpty() || value (processor.apvts(), x.ids.onId) >= 0.5f;
        }

        /** PATTERN also offers CUSTOM, a copy of any pattern to start from, and the saved ones. */
        void setUpPatternSelector (ValueSelector& sel)
        {
            auto* customParam = processor.apvts().getParameter (lane ("shaper.custom"));
            sel.textOverride = [customParam] { return customParam != nullptr && customParam->getValue() >= 0.5f ? juce::String ("CUSTOM") : juce::String(); };
            sel.onPick = [customParam] (int) {
                if (customParam != nullptr && customParam->getValue() >= 0.5f)
                {
                    customParam->beginChangeGesture();
                    customParam->setValueNotifyingHost (0.0f);
                    customParam->endChangeGesture();
                }
            };
            juce::Component::SafePointer<MovementRack> safe (this);
            sel.extraItems = [safe, customParam] (juce::PopupMenu& menu) {
                menu.addSeparator();
                menu.addItem ("CUSTOM", true, customParam != nullptr && customParam->getValue() >= 0.5f, [safe] { if (safe != nullptr) safe->setCustomOn(); });
                const auto files = InstrumentHost::savedShaperPatterns();
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
            if (auto* c = processor.apvts().getParameter (lane ("shaper.custom")))
            {
                c->beginChangeGesture();
                c->setValueNotifyingHost (1.0f);
                c->endChangeGesture();
            }
            if (visual != nullptr)
                visual->repaint();
            repaint();
        }
        void setCustom (const ShaperPattern& pattern)
        {
            processor.undo().beginNewTransaction ("Shaper pattern");
            processor.setLaneShaperPattern (lane.index, pattern);
            setCustomOn();
        }
        void copyFromMenu()
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<MovementRack> safe (this);
            for (int i = 0; i < RhythmicShaper::patternCount; ++i)
                menu.addItem (RhythmicShaper::patternName (i), [safe, i] { if (safe != nullptr) safe->setCustom (RhythmicShaper::patternSteps (i)); });
            if (processor.movementLaneCount() > 1)
            {
                const int other = lane.index == 0 ? 1 : 0;
                menu.addSeparator();
                menu.addItem ("Lane " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + other)) + "'s pattern", [safe, other] {
                    if (safe == nullptr)
                        return;
                    const Lane from (safe->processor, other);
                    auto& s = safe->processor.apvts();
                    const bool custom = value (s, from ("shaper.custom")) >= 0.5f;
                    safe->setCustom (custom ? safe->processor.laneShaperPattern (other) : RhythmicShaper::patternSteps (juce::roundToInt (value (s, from ("shaper.pattern")))));
                });
            }
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolbar.get()).withDeletionCheck (*this));
        }
        void loadMenu()
        {
            juce::PopupMenu menu;
            juce::Component::SafePointer<MovementRack> safe (this);
            const auto files = InstrumentHost::savedShaperPatterns();
            for (const auto& f : files)
                menu.addItem (f.getFileNameWithoutExtension(), [safe, f] { if (safe != nullptr) safe->loadFile (f); });
            if (files.isEmpty())
                menu.addItem ("No saved patterns yet", false, false, [] {});
            menu.addSeparator();
            menu.addItem ("Show folder", [] {
                InstrumentHost::shaperPatternFolder().createDirectory();
                InstrumentHost::shaperPatternFolder().revealToUser();
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (toolbar.get()).withDeletionCheck (*this));
        }
        void loadFile (const juce::File& file)
        {
            if (const auto pattern = InstrumentHost::readShaperPatternFile (file))
                setCustom (*pattern);
        }
        void savePattern()
        {
            auto* window = new juce::AlertWindow ("Save pattern", "A name for this SHAPER pattern:", juce::MessageBoxIconType::NoIcon);
            window->addTextEditor ("name", "My pattern");
            window->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
            window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<MovementRack> safe (this);
            window->enterModalState (true, juce::ModalCallbackFunction::create ([safe, window] (int result) {
                const auto name = juce::File::createLegalFileName (window->getTextEditorContents ("name").trim());
                if (result == 1 && name.isNotEmpty() && safe != nullptr)
                    InstrumentHost::writeShaperPatternFile (InstrumentHost::shaperPatternFolder().getChildFile (name + InstrumentHost::shaperPatternExtension),
                                                            safe->processor.laneShaperPattern (safe->lane.index));
            }), true);
        }

        Lane lane;
        std::vector<std::unique_ptr<MovementPresetModel>> models;
        ShaperTool tool;
        bool wide = false, builtSync = false;
        int builtMode = -1;
        std::unique_ptr<ShaperToolbar> toolbar;
        MagnifierButton magnifier;
        std::unique_ptr<juce::ParameterAttachment> customAttachment;
        std::vector<std::unique_ptr<rackui::LaneKey>> laneTabs;
        std::unique_ptr<rackui::VerticalSwitch> power;
        std::unique_ptr<rackui::PowerKey> powerSlide;   ///< (full bleed: the rail's slide instead)
        juce::Rectangle<int> onWord;
    };

    /** ECHO: TAPE / BBD / ASCEND and its presets; TIME, FEEDBACK, TONE, AGE (ASCEND: CLOCK,
        INTERVAL, MOTION), then FREE / SYNC and the stereo picture as compact keys; the repeats. */
    class EchoRack final : public UnifiedRack
    {
    public:
        float leftShare() const override { return 0.61f; }   // approved ECHO column
        bool processingActive() const override { return processor.parameterValue ("echo") > 0.0f; }
        explicit EchoRack (InstrumentHost& p) : UnifiedRack (p, "ECHO", design::colour::Macro::echo), model (p)
        {
            setModeSelector ("echo.type", "Tape echo, bucket-brigade (BBD) delay, or ASCEND: repeats an interval up (a pitch shifter in the loop)");
            presetBar->setModel (&model);
            setVisual (std::make_unique<EchoVisual> (p));
            refresh();
        }
        void refresh() override
        {
            const int key = juce::roundToInt (value (processor.apvts(), "echo.type")) * 2 + (value (processor.apvts(), "echo.sync") >= 0.5f ? 1 : 0);
            if (key != built)
                build (key);
        }

    private:
        void build (int key)
        {
            built = key;
            clearCells();
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            const bool ascend = key / 2 == static_cast<int> (EchoType::ascend);
            // TIME: one knob in one place - milliseconds when free, the note values (detents) when
            // synced. AGE (BBD: its wear; its clock follows TIME) or ASCEND's CLOCK.
            if (key % 2 == 1)
                steppedKnob ("echo.division", "TIME", [] (double v) { return juce::String (shaping::echoDivisionName (juce::roundToInt (v))); });
            else
                knob ("echo.time", "TIME", [] (double v) { return format::milliseconds (v); });
            knob ("echo.feedback", "FEEDBACK", percent);
            knob ("echo.tone", "TONE", percent);
            if (ascend)
            {
                knob ("echo.age", "CLOCK", [] (double v) { return juce::String (EchoDelay::clockHz (0.01 * v) / 1000.0, 1) + " kHz"; });
                listCell ("echo.ascend.interval", "INTERVAL", "How far each pass shifts the repeats: an octave up, a fifth, a fourth, or an octave down");
                listCell ("echo.ascend.motion", "MOTION", "RISE: each repeat higher than the last. STEADY: every repeat at the interval. LATE: the first as played, then rising");
            }
            else
                knob ("echo.age", "AGE", percent);
            const juce::String timingTip ("Free time in ms, or a note value synced to the host tempo");
            const juce::String stereoTip ("In the centre, alternating left and right (Ping-Pong), or two heads a little apart");
            if (deckWanted() && key / 2 == static_cast<int> (EchoType::tape))
            {
                // V4's TAPE deck (opt-in): the same TIMING and STEREO in the deck's top row.
                deckSegments ("echo.sync", "TIMING", { { "FREE", "Free", 0.0f }, { "SYNC", "Sync", 1.0f } }, timingTip);
                deckSegments ("echo.stereo", "STEREO", { { "MONO", "Mono", 0.0f }, { "PING", "Ping-Pong", 1.0f }, { "WIDE", "Wide", 2.0f } }, stereoTip);
                setDeck ([this] { return deckReading(); });
            }
            else
            {
                setDeck (nullptr);
                switchCell ("echo.sync", "TIMING", { { "FREE", "Free", 0.0f }, { "SYNC", "Sync", 1.0f } }, timingTip);
                switchCell ("echo.stereo", "STEREO", { { "MONO", "Mono", 0.0f }, { "PING", "Ping-Pong", 1.0f }, { "WIDE", "Wide", 2.0f } }, stereoTip);
            }
            layout();
        }
        void faceplateChanged() override
        {
            built = -1;   // (the TAPE deck is the faceplate's choice)
            refresh();
        }
        juce::String deckCaption() const override { return "REPEATS"; }
        /** The deck's readout: the time (the division and its ms at the host's tempo when
            synced), the tape's speed from it (a longer delay, a slower tape). */
        TapeDeck::Reading deckReading() const
        {
            EchoDelay::Settings e;
            e.type = EchoType::tape;
            e.sync = value (processor.apvts(), "echo.sync") >= 0.5f;
            e.division = juce::roundToInt (value (processor.apvts(), "echo.division"));
            e.timeMs = value (processor.apvts(), "echo.time");
            const double bpm = static_cast<double> (processor.hostTempo());
            const double seconds = std::max (1.0e-3, EchoDelay::timeSeconds (e, bpm));
            TapeDeck::Reading r;
            const auto ms = juce::String (juce::roundToInt (1000.0 * seconds)) + " ms";
            if (e.sync)
            {
                r.value = shaping::echoDivisionName (e.division);
                r.unit = ms + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String (juce::roundToInt (bpm)) + " BPM";
                r.note = "SYNC";
                r.noteLit = true;
                if (auto* division = processor.apvts().getParameter ("echo.division"))
                    r.ticks = std::max (2, division->getNumSteps());
                r.tick = e.division;
            }
            else
            {
                r.value = ms;
                r.unit = "free time";
                r.note = "FREE";
            }
            r.radiansPerSecond = std::clamp (1.6 / seconds, 0.8, 9.0);
            r.active = processingActive();
            return r;
        }

        EchoPresetModel model;
        int built = -1;
    };

    /** SPACE (the reference): the room or engine and its variant, its presets, every knob of
        the engine, and the decay picture whose handles set PRE-DELAY and DECAY. */
    class SpaceRack final : public UnifiedRack
    {
    public:
        float leftShare() const override { return 0.68f; }   // approved SPACE column
        bool processingActive() const override { return processor.parameterValue ("space") > 0.0f; }
        explicit SpaceRack (InstrumentHost& p) : UnifiedRack (p, "SPACE", design::colour::Macro::space), model (p)
        {
            auto& m = setModeSelector ("space.type", "Room, hall, plate or spring - or ABYSS, VINTAGE or BLOOM, SPACE's other engines, and their variants");
            // MODE names the room or the engine (approved: ABYSS); its variant is its preset's
            // name and is chosen in this list's engine submenus.
            m.textOverride = [this] {
                static const char* engines[] { "", "ABYSS", "VINTAGE", "BLOOM" };
                return juce::String (engines[std::clamp (engine(), 0, 3)]);
            };
            m.extraItems = [this] (juce::PopupMenu& menu) {
                juce::Component::SafePointer<SpaceRack> safe (this);
                menu.addSeparator();
                static const std::array<juce::StringArray, 3> variants { juce::StringArray { "Deep", "Dark", "Inverse", "Endless" },
                                                                         juce::StringArray { "Hall", "Plate", "Chamber", "Room" },
                                                                         juce::StringArray { "Clean", "Vintage", "Tape", "Wash" } };
                static const char* variantIds[] { "space.abyss.variant", "space.vintage.variant", "space.bloom.variant" };
                int index = 1;
                for (const char* name : { "Abyss", "Vintage", "Bloom" })
                {
                    juce::PopupMenu sub;
                    const int now = juce::roundToInt (value (processor.apvts(), variantIds[index - 1]));
                    for (int v = 0; v < 4; ++v)
                        sub.addItem (variants[static_cast<std::size_t> (index - 1)][v], true, engine() == index && now == v,
                                     [safe, index, v] { if (safe != nullptr) safe->setEngine (index, v); });
                    menu.addSubMenu (name, sub, true, nullptr, engine() == index);
                    ++index;
                }
            };
            m.onPick = [this] (int) { setEngine (0); };
            presetBar->setModel (&model);
            setVisual (std::make_unique<SpaceVisual> (p.apvts(), [&p] { return static_cast<double> (p.hostTempo()); }));
            refresh();
        }
        void refresh() override
        {
            const int which = engine();
            const bool sync = value (processor.apvts(), "space.bloom.sync") >= 0.5f;
            const int key = which * 2 + (which == 3 && sync ? 1 : 0);
            if (key != built)
                build (key);
            if (modeSelector != nullptr)
                modeSelector->repaint();
        }

    private:
        int engine() const { return juce::roundToInt (value (processor.apvts(), "space.engine")); }
        void setEngine (int e, int variant = -1)
        {
            static const char* variantIds[] { "space.abyss.variant", "space.vintage.variant", "space.bloom.variant" };
            if (variant >= 0 && e >= 1 && e <= 3)
                setPlain (processor.apvts(), variantIds[e - 1], static_cast<float> (variant));
            if (juce::roundToInt (value (processor.apvts(), "space.engine")) != e)
                setPlain (processor.apvts(), "space.engine", static_cast<float> (e));
            refresh();
        }
        void build (int key)
        {
            built = key;
            clearCells();
            const auto ms = [] (double v) { return format::milliseconds (v); };
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            const auto seconds = [] (double v) { return juce::String (std::clamp (v, 0.2, 8.0), 1) + " s"; };
            switch (key / 2)
            {
                case 1:   // ABYSS
                    knob ("space.preDelay", "PRE-DELAY", ms);
                    knob ("space.size", "SIZE", percent);
                    knob ("space.decay", "DECAY", seconds);
                    knob ("space.damping", "DAMP", percent);
                    knob ("space.abyss.gravity", "GRAVITY", [] (double v) {
                        const int r = juce::roundToInt (v);
                        return (r > 0 ? juce::String ("+") : juce::String()) + juce::String (r) + " %";
                    });
                    knob ("space.abyss.resonance", "RESONANCE", percent);
                    knob ("space.modulation", "MOD", percent);
                    knob ("space.width", "WIDTH", percent);
                    switchCell ("space.abyss.freeze", "FREEZE", { { "ON", "On", 1.0f }, { "OFF", "Off", 0.0f } },
                                "FREEZE: holds the tail as it is (nothing new enters, nothing fades)");
                    break;
                case 2:   // VINTAGE
                    knob ("space.preDelay", "PRE-DELAY", ms);
                    knob ("space.size", "SIZE", percent);
                    knob ("space.decay", "DECAY", seconds);
                    knob ("space.damping", "DAMP", percent);
                    knob ("space.vintage.diffusion", "DIFFUSION", percent);
                    knob ("space.modulation", "MOD", percent);
                    knob ("space.width", "WIDTH", percent);
                    segmentCell ("space.vintage.era", "ERA", { { "EARLY", "Early", 0.0f }, { "LATER", "Later", 1.0f } },
                                 "The early digital reverbs' grain, or the later ones' smoother tail");
                    break;
                case 3:   // BLOOM
                    if (key % 2 == 1)
                        steppedKnob ("space.bloom.division", "TIME", [] (double v) { return juce::String (shaping::echoDivisionName (juce::roundToInt (v))); });
                    else
                        knob ("space.bloom.time", "TIME", ms);
                    knob ("space.bloom.diffusion", "DIFFUSION", percent);
                    knob ("space.decay", "DECAY", seconds);
                    knob ("space.damping", "DAMP", percent);
                    switchCell ("space.bloom.sync", "TIMING", { { "FREE", "Free", 0.0f }, { "SYNC", "Sync", 1.0f } },
                                "BLOOM's time: free (ms), or a note value at the host's tempo");
                    knob ("space.size", "SIZE", percent);
                    knob ("space.modulation", "MOD", percent);
                    knob ("space.width", "WIDTH", percent);
                    break;
                default:   // ROOMS
                {
                    auto* typeParam = processor.apvts().getParameter ("space.type");
                    knob ("space.preDelay", "PRE-DELAY", ms);
                    knob ("space.size", "SIZE", percent);
                    knob ("space.decay", "DECAY", [typeParam] (double v) {
                        double lo = 0.2, hi = 8.0;
                        if (typeParam != nullptr)
                            shaping::decayRange (static_cast<SpaceType> (juce::roundToInt (typeParam->convertFrom0to1 (typeParam->getValue()))), lo, hi);
                        return juce::String (std::clamp (v, lo, hi), 1) + " s";
                    });
                    knob ("space.damping", "DAMP", percent);
                    knob ("space.modulation", "MOD", percent);
                    knob ("space.width", "WIDTH", percent);
                    knob ("space.lowCut", "LOW CUT", [] (double v) { return format::hertz (v); });
                    knob ("space.highCut", "HIGH CUT", [] (double v) { return format::hertz (v); });
                    break;
                }
            }
            layout();
        }

        SpacePresetModel model;
        int built = -1;
    };

    //==========================================================================
    /** ECHO: the machine beside the title, its repeats, then FREE / SYNC and the stereo
        picture, then TIME (a note value when synced, ms when free), FEEDBACK, TONE, AGE. */
    class EchoPanel final : public MacroPanel
    {
    public:
        explicit EchoPanel (InstrumentHost& p) : MacroPanel (p, "ECHO", 286, 86, design::colour::Macro::echo)
        {
            auto& m = mode ("echo.type", "Tape echo, bucket-brigade (BBD) delay, or ASCEND: repeats an interval up (a pitch shifter in the loop)");
            m.onChange = [this] (int) {
                build();
                if (onSizeChanged != nullptr)
                    onSizeChanged();
            };
            setVisual (std::make_unique<EchoVisual> (p));
            sync = std::make_unique<SegmentedControl> (*p.apvts().getParameter ("echo.sync"), juce::StringArray { "FREE", "SYNC" });
            sync->setAccent (accent);
            sync->setTooltip ("Free time in ms, or a note value synced to the host tempo");
            sync->onChange = [this] (int) { build(); };
            addAndMakeVisible (*sync);
            stereo = std::make_unique<SegmentedControl> (*p.apvts().getParameter ("echo.stereo"), juce::StringArray { "MONO", "PING-PONG", "WIDE" });
            stereo->setAccent (accent);
            stereo->setTooltip ("In the centre, alternating left and right, or two heads a little apart");
            addAndMakeVisible (*stereo);
            build();
        }

        juce::Point<int> cardSize() const override
        {
            return { panelWidth, headerTop + visualHeight + gap + tabsHeight + 6 + cellHeight + bottom + (secondaryRow.empty() ? 0 : 2 + secondaryCellHeight) };
        }

        /** Tests: the knobs' captions as built (TIME .. AGE, CLOCK for ASCEND, its row after). */
        juce::StringArray captionsForTest() const
        {
            juce::StringArray c;
            for (auto* k : primaryRow)
                c.add (k->captionText());
            for (auto* k : secondaryRow)
                c.add (k->captionText());
            return c;
        }

    private:
        void build()
        {
            knobs.clear();
            primaryRow.clear();
            secondaryRow.clear();
            const auto percent = [] (double v) { return format::percent (v) + " %"; };
            const bool ascend = juce::roundToInt (value (processor.apvts(), "echo.type")) == static_cast<int> (EchoType::ascend);
            if (sync->selected() == 1)
                knob (false, "echo.division", "TIME", [] (double v) { return juce::String (shaping::echoDivisionName (juce::roundToInt (v))); });
            else
                knob (false, "echo.time", "TIME", [] (double v) { return format::milliseconds (v); });
            knob (false, "echo.feedback", "FEEDBACK", percent);
            knob (false, "echo.tone", "TONE", percent);
            if (ascend)
            {
                // AGE is ASCEND's CLOCK: the rate its loop is held at (13 kHz .. 7 kHz).
                knob (false, "echo.age", "CLOCK", [] (double v) { return juce::String (EchoDelay::clockHz (0.01 * v) / 1000.0, 1) + " kHz"; })
                    .slider.setTooltip ("CLOCK: the rate the repeats are held at - lower is grainier");
                knob (true, "echo.ascend.interval", "INTERVAL", [] (double v) {
                    static const char* names[] = { "+12", "+7", "+5", "-12" };
                    return juce::String (names[std::clamp (juce::roundToInt (v), 0, 3)]);
                }).slider.setTooltip ("How far each pass shifts the repeats: an octave up, a fifth, a fourth, or an octave down");
                knob (true, "echo.ascend.motion", "MOTION", [] (double v) {
                    static const char* names[] = { "RISE", "STEADY", "LATE" };
                    return juce::String (names[std::clamp (juce::roundToInt (v), 0, 2)]);
                }).slider.setTooltip ("RISE: each repeat higher than the last. STEADY: every repeat at the interval. LATE: the first repeat as played, then rising");
            }
            else
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
            if (! secondaryRow.empty())
            {
                area.removeFromTop (2);
                layoutRow (secondaryRow, area.removeFromTop (secondaryCellHeight));
            }
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
        explicit LifePanel (InstrumentHost& p) : MacroPanel (p, "LIFE", 268, 66, design::colour::Macro::life)
        {
            mode ("life.mode", "Natural: subtle; Loose: wider; Fray: now and then a note strays");
            setVisual (std::make_unique<LifeVisual> (p));
            auto& state = p.apvts();
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
        ReimaginedVisual (InstrumentHost& p, int l) : processor (p), layer (l) {}

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
            if (auto* p = processor.apvts().getParameter (InstrumentHost::reimaginedModeParameterId (layer, name)))
                return p->getValue();   // 0..100 % as 0..1
            return 0.5f;
        }
        int choice (const char* name) const
        {
            if (auto* p = processor.apvts().getParameter (InstrumentHost::reimaginedModeParameterId (layer, name)))
                return juce::roundToInt (p->convertFrom0to1 (p->getValue()));
            return 0;
        }
        float amount() const
        {
            if (auto* p = processor.apvts().getParameter (InstrumentHost::reimaginedParameterId (layer)))
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
            if (const auto why = processor.reimaginedModeUnavailable (layer, choice ("mode")); why.isNotEmpty())
                status (g, r, why);
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

        InstrumentHost& processor;
        int layer;
        Watch watch;
    };

    /** REIMAGINED: the mode beside the title, its picture, then that mode's own two or three
        settings (continuous ones as small knobs in the aurora at the layer's amount, stepped
        ones as text selectors). Switching modes keeps every mode's values. */
    class ReimaginedPanel final : public MacroPanel, private juce::Timer
    {
    public:
        ReimaginedPanel (InstrumentHost& p, int l)
            : MacroPanel (p, "REIMAGINED", 270, 80, design::colour::reimagined (amountOf (p, l))), layer (l)
        {
            const auto modeId = InstrumentHost::reimaginedModeParameterId (l, "mode");
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
        static float amountOf (InstrumentHost& p, int l)
        {
            auto* param = p.apvts().getParameter (InstrumentHost::reimaginedParameterId (l));
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
                const auto pid = InstrumentHost::reimaginedModeParameterId (layer, name);
                cells.push_back (&knob (false, pid.toRawUTF8(), caption, percent));
            };
            auto addChoice = [&] (const char* name, const char* caption, const char* tip) {
                auto* param = processor.apvts().getParameter (InstrumentHost::reimaginedModeParameterId (layer, name));
                if (param == nullptr)
                    return;
                selectors.push_back (std::make_unique<ValueSelector> (*param, caption));
                selectors.back()->setTooltip (tip);
                binding::tag (*selectors.back(), param->getParameterID());   // (a product's editor dims a mode setting its engine does not read)
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

std::unique_ptr<MiniPanel> createReimaginedPopup (InstrumentHost& processor, int layer)
{
    return std::make_unique<ReimaginedPanel> (processor, layer);
}

std::unique_ptr<rack::Module> createRackModule (rack::Kind kind, InstrumentHost& processor)
{
    switch (kind)
    {
        case rack::Kind::movement:
        {
            auto m = std::make_unique<MovementRack> (processor);
            m->wearFaceplate (processor.rackFaceplate (static_cast<int> (kind)));
            return m;
        }
        case rack::Kind::echo:
        {
            auto m = std::make_unique<EchoRack> (processor);
            m->wearFaceplate (processor.rackFaceplate (static_cast<int> (kind)));
            return m;
        }
        case rack::Kind::space:
        {
            auto m = std::make_unique<SpaceRack> (processor);
            m->wearFaceplate (processor.rackFaceplate (static_cast<int> (kind)));
            return m;
        }
        case rack::Kind::filter:
        case rack::Kind::morph:
        case rack::Kind::fx: break;   // (a product's own: ShellProduct::productRacks)
    }
    return nullptr;
}

int rackMovementLane (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const MovementRack*> (&module))
        return m->laneShown();
    return -1;
}

juce::StringArray rackMovementTitleRow (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const MovementRack*> (&module))
        return m->titleRowItems();
    return {};
}

juce::Rectangle<int> rackMovementLaneSwitch (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const MovementRack*> (&module))
        return m->laneSwitchBounds();
    return {};
}

void clickRackMovementLaneSwitch (rack::Module& module)
{
    if (auto* m = dynamic_cast<MovementRack*> (&module))
        m->clickLaneSwitch();
}

juce::StringArray rackMovementLaneActions (rack::Module& module)
{
    if (auto* m = dynamic_cast<MovementRack*> (&module))
        return m->laneActionNames();
    return {};
}

void runRackMovementLaneAction (rack::Module& module, int index)
{
    if (auto* m = dynamic_cast<MovementRack*> (&module))
        m->runLaneAction (index);
}

juce::StringArray rackMovementPresetMenuExtras (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const MovementRack*> (&module))
        return m->presetMenuExtras();
    return {};
}

std::vector<juce::Rectangle<int>> rackCellBounds (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const UnifiedRack*> (&module))
        return m->cellBounds();
    return {};
}

juce::Component* rackCell (const rack::Module& module, const juce::String& caption)
{
    if (const auto* m = dynamic_cast<const UnifiedRack*> (&module))
        return m->cellNamed (caption);
    return nullptr;
}

int rackDensity (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const UnifiedRack*> (&module))
        return m->dense() ? 1 : 0;
    return -1;
}

void showRackMovementLane (rack::Module& module, int lane)
{
    if (auto* m = dynamic_cast<MovementRack*> (&module))
        m->showLane (lane);
}

void setRackMovementWide (rack::Module& module, bool wide)
{
    if (auto* m = dynamic_cast<MovementRack*> (&module))
        m->setWide (wide);
}

juce::String rackMovementCaptions (const rack::Module& module)
{
    if (const auto* m = dynamic_cast<const UnifiedRack*> (&module))
        return m->captions();
    return {};
}

juce::String rackPresetText (rack::Module& module)
{
    if (auto* m = dynamic_cast<UnifiedRack*> (&module))
        return m->presets().text();
    return {};
}

bool rackPresetChoose (rack::Module& module, const juce::String& name)
{
    if (auto* m = dynamic_cast<UnifiedRack*> (&module))
        return m->presets().chooseNamed (name);
    return false;
}

void rackPresetStep (rack::Module& module, int delta)
{
    if (auto* m = dynamic_cast<UnifiedRack*> (&module))
        m->presets().step (delta);
}

bool rackPresetSave (rack::Module& module, const juce::String& name)
{
    if (auto* m = dynamic_cast<UnifiedRack*> (&module))
        return m->presets().saveAs (name);
    return false;
}

juce::StringArray rackPresetNames (rack::Module& module)
{
    juce::StringArray names;
    if (auto* m = dynamic_cast<UnifiedRack*> (&module))
        for (const auto& p : m->presets().relevant())
            names.add (p.name);
    return names;
}

juce::Rectangle<int> rackSlot (const rack::Module& module, int slot)
{
    if (const auto* m = dynamic_cast<const UnifiedRack*> (&module))
        return m->slot (slot);
    return {};
}

bool rackTapeDeckShown (const rack::Module& module)
{
    const auto* m = dynamic_cast<const UnifiedRack*> (&module);
    return m != nullptr && m->tapeDeck() != nullptr && m->tapeDeck()->isVisible();
}

juce::Rectangle<int> rackTapeDeckBounds (const rack::Module& module)
{
    const auto* m = dynamic_cast<const UnifiedRack*> (&module);
    return m != nullptr && m->tapeDeck() != nullptr ? m->tapeDeck()->getBounds() : juce::Rectangle<int>();
}

double rackTapeDeckSpeed (const rack::Module& module)
{
    const auto* m = dynamic_cast<const UnifiedRack*> (&module);
    return m != nullptr && m->tapeDeck() != nullptr ? m->tapeDeck()->reelSpeed() : 0.0;
}

void advanceRackTapeDeck (rack::Module& module, double seconds)
{
    if (auto* m = dynamic_cast<UnifiedRack*> (&module); m != nullptr && m->tapeDeck() != nullptr)
        m->tapeDeck()->advance (seconds);
}

void setRackPresetFolderForTest (const juce::File& folder)
{
    rackPresetFolderOverride() = folder;
}

std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, InstrumentHost& processor)
{
    auto& state = processor.apvts();
    auto ms = [] (double v) { return format::milliseconds (v); };
    auto percent = [] (double v) { return format::percent (v) + " %"; };
    switch (macro)
    {
        case MacroPopup::life:
        {
            return std::make_unique<LifePanel> (processor);
        }
        case MacroPopup::drive:
        {
            // The DRIVE macro is the amount; the popover chooses the circuit and voices it.
            auto popup = std::make_unique<MacroPanel> (processor, "DRIVE", 232, 84, design::colour::Macro::drive);
            popup->mode ("drive.mode", "Tube: rich, rounded breakup. Tape: smooth, dense, compressed. Crunch: rawer, forward, articulate.");
            popup->setVisual (std::make_unique<DriveVisual> (state));
            popup->knob (false, "drive.tone", "TONE", percent);   // darker, softer harmonics .. more open
            popup->knob (false, "drive.body", "BODY", percent);   // lean, clear attack .. dense, round, sustained
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

    return std::make_unique<SpacePanel> (processor);
}


const juce::StringArray& popupParameterIds (MacroPopup macro)
{
    static const juce::StringArray life { "life.mode", "life.pitch", "life.tone", "life.attack" };
    static const juce::StringArray drive { "drive.mode", "drive.tone", "drive.body" };
    static const juce::StringArray character { "character.type", "character.min", "character.max", "character.resonance",
                                               "character.drive", "character.envAmount", "character.envAttack", "character.envDecay" };
    static const juce::StringArray movement { "movement.mode", "movement.drift.speed", "movement.drift.pitch", "movement.drift.tone",
                                              "movement.tape.wow", "movement.tape.flutter", "movement.tape.wear",
                                              "movement.chorus.rate", "movement.chorus.width", "movement.chorus.stereo",
                                              "movement.pulse.rate", "movement.pulse.shape", "movement.pulse.stereo",
                                              "movement.shaper.pattern", "movement.shaper.rate", "movement.shaper.target",
                                              "movement.shaper.smooth", "movement.shaper.custom" };
    static const juce::StringArray space { "space.type", "space.decay", "space.preDelay", "space.size", "space.damping",
                                           "space.modulation", "space.width", "space.lowCut", "space.highCut",
                                           "space.engine", "space.abyss.gravity", "space.abyss.freeze", "space.abyss.resonance",
                                           "space.vintage.variant", "space.vintage.era", "space.vintage.diffusion", "space.bloom.variant",
                                           "space.bloom.sync", "space.bloom.division", "space.bloom.time", "space.bloom.diffusion",
                                           "space.abyss.variant" };
    static const juce::StringArray echo { "echo.type", "echo.sync", "echo.division", "echo.time", "echo.feedback",
                                          "echo.tone", "echo.age", "echo.stereo", "echo.ascend.interval", "echo.ascend.motion" };
    switch (macro)
    {
        case MacroPopup::life: return life;
        case MacroPopup::drive: return drive;
        case MacroPopup::character: return character;
        case MacroPopup::movement: return movement;
        case MacroPopup::echo: return echo;
        case MacroPopup::space: break;
    }
    return space;
}

} // namespace osp::plugin
