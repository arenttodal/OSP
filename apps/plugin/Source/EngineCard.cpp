#include "EngineCard.h"

#include "Design.h"

#include "core/PitchMath.h"

#include <cmath>

namespace osp::plugin
{

//==============================================================================
namespace icons
{
    void draw (juce::Graphics& g, Kind kind, juce::Rectangle<float> area, juce::Colour colour, float stroke)
    {
        const float s = std::min (area.getWidth(), area.getHeight());
        const auto r = area.withSizeKeepingCentre (s, s);
        const auto c = r.getCentre();
        juce::Path p;
        const juce::PathStrokeType line (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (colour);
        switch (kind)
        {
            case Kind::link:
            {
                // Two thin interlocking links on a diagonal, open inside (links read heavier
                // than lines: drawn a touch finer than the others).
                const float w = s * 0.44f, h = s * 0.22f;
                juce::Path a, b;
                a.addRoundedRectangle (-w * 0.5f, -h * 0.5f, w, h, h * 0.5f);
                b = a;
                a.applyTransform (juce::AffineTransform::translation (-s * 0.14f, 0.0f).rotated (-0.785f).translated (c.x, c.y));
                b.applyTransform (juce::AffineTransform::translation (s * 0.14f, 0.0f).rotated (-0.785f).translated (c.x, c.y));
                const juce::PathStrokeType fine (stroke * 0.9f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
                g.strokePath (a, fine);
                g.strokePath (b, fine);
                return;
            }
            case Kind::reverse:
            {
                // Playback, backwards: two thin open chevrons pointing left (not an undo arrow,
                // not solid play triangles).
                for (float dx : { -0.11f, 0.11f })
                {
                    juce::Path t;
                    t.startNewSubPath (c.x + (dx + 0.12f) * s, c.y - 0.22f * s);
                    t.lineTo (c.x + (dx - 0.12f) * s, c.y);
                    t.lineTo (c.x + (dx + 0.12f) * s, c.y + 0.22f * s);
                    g.strokePath (t, line);
                }
                return;
            }
            case Kind::loop:
            {
                // Two curved arrows chasing each other round a circle: a loop, not a reload.
                const float radius = s * 0.27f;
                const float pi = 3.14159265f;
                for (float from : { -0.35f * pi, 0.65f * pi })
                {
                    const float to = from + 0.72f * pi;
                    juce::Path arc;
                    arc.addCentredArc (c.x, c.y, radius, radius, 0.0f, from, to, true);
                    g.strokePath (arc, line);
                    // The arrowhead at the arc's end, along its direction of travel (clockwise).
                    const juce::Point<float> tip (c.x + radius * std::sin (to), c.y - radius * std::cos (to));
                    const juce::Point<float> along (std::cos (to), std::sin (to)), out (std::sin (to), -std::cos (to));
                    const float head = s * 0.13f;
                    juce::Path arrow;
                    arrow.startNewSubPath (tip - along * head + out * (0.75f * head));
                    arrow.lineTo (tip);
                    arrow.lineTo (tip - along * head - out * (0.75f * head));
                    g.strokePath (arrow, line);
                }
                return;
            }
            case Kind::follow:
            {
                // An amplitude contour, a quick rise and a long fall, over a faint baseline (a
                // single line reads lighter than the others: drawn a touch stronger).
                p.startNewSubPath (r.getX() + 0.14f * s, r.getY() + 0.7f * s);
                p.quadraticTo (r.getX() + 0.25f * s, r.getY() + 0.24f * s, r.getX() + 0.37f * s, r.getY() + 0.28f * s);
                p.cubicTo (r.getX() + 0.52f * s, r.getY() + 0.32f * s, r.getX() + 0.62f * s, r.getY() + 0.64f * s, r.getX() + 0.86f * s, r.getY() + 0.7f * s);
                g.strokePath (p, juce::PathStrokeType (stroke * 1.12f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour (colour.withMultipliedAlpha (0.35f));
                g.drawLine (r.getX() + 0.14f * s, r.getY() + 0.76f * s, r.getX() + 0.86f * s, r.getY() + 0.76f * s, stroke * 0.7f);
                return;
            }
            case Kind::dots:
                for (int i = -1; i <= 1; ++i)
                    g.fillEllipse (c.x - 1.6f, c.y + static_cast<float> (i) * s * 0.26f - 1.6f, 3.2f, 3.2f);
                return;
            case Kind::chevronDown:
                p.startNewSubPath (c.x - 0.2f * s, c.y - 0.1f * s);
                p.lineTo (c.x, c.y + 0.1f * s);
                p.lineTo (c.x + 0.2f * s, c.y - 0.1f * s);
                g.strokePath (p, line);
                return;
            case Kind::chevronLeft:
            case Kind::chevronRight:
            {
                const float d = kind == Kind::chevronLeft ? 1.0f : -1.0f;
                p.startNewSubPath (c.x + d * 0.1f * s, c.y - 0.2f * s);
                p.lineTo (c.x - d * 0.1f * s, c.y);
                p.lineTo (c.x + d * 0.1f * s, c.y + 0.2f * s);
                g.strokePath (p, line);
                return;
            }
            case Kind::heart:
            case Kind::heartFilled:
            {
                const float w = s * 0.36f;
                p.startNewSubPath (c.x, c.y + 0.3f * s);
                p.cubicTo (c.x - 1.4f * w, c.y - 0.05f * s, c.x - 0.6f * w, c.y - 0.45f * s, c.x, c.y - 0.12f * s);
                p.cubicTo (c.x + 0.6f * w, c.y - 0.45f * s, c.x + 1.4f * w, c.y - 0.05f * s, c.x, c.y + 0.3f * s);
                p.closeSubPath();
                if (kind == Kind::heartFilled)
                    g.fillPath (p);
                g.strokePath (p, line);
                return;
            }
            case Kind::plus:
                g.drawLine (c.x - 0.25f * s, c.y, c.x + 0.25f * s, c.y, stroke);
                g.drawLine (c.x, c.y - 0.25f * s, c.x, c.y + 0.25f * s, stroke);
                return;
        }
    }
}

namespace
{
    double gridStep (double seconds)
    {
        for (double step : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 })
            if (seconds / step <= 8.0)
                return step;
        return 300.0;
    }

    juce::String secondsText (double t, double step)
    {
        auto text = juce::String (t, step < 0.5 ? 2 : 1);
        while (text.containsChar ('.') && (text.endsWithChar ('0') || text.endsWithChar ('.')))
            text = text.dropLastCharacters (1);
        return text + " s";
    }

}

namespace
{
    /** A layer's waveform tones, darkest to lightest (A amber / earth, B mineral / slate, C sage). */
    struct WaveTones
    {
        juce::Colour deep, body, light, pale, core, accent;
    };
    const WaveTones& waveTones (int layer)
    {
        static const std::array<WaveTones, 3> tones { {
            // A: deep brown -> burnt amber -> amber -> warm sand -> a restrained pale highlight.
            { juce::Colour (0xff5e2b12), juce::Colour (0xffb8682c), juce::Colour (0xffe09a52), juce::Colour (0xffeec899), juce::Colour (0xfff7e6cc), juce::Colour (0xffe8873f) },
            // B: deep graphite -> mineral blue -> blue-grey -> muted lavender-grey -> pale cool.
            { juce::Colour (0xff27313d), juce::Colour (0xff587390), juce::Colour (0xff92a6bd), juce::Colour (0xffc6c7d8), juce::Colour (0xffe8edf4), juce::Colour (0xffb9b4cc) },
            { juce::Colour (0xff36513f), juce::Colour (0xff6b9577), juce::Colour (0xffa4c8aa), juce::Colour (0xffdcecd9), juce::Colour (0xfff6fff3), juce::Colour (0xffc9e0a8) },
        } };
        return tones[static_cast<std::size_t> (juce::jlimit (0, 2, layer))];
    }

    /** A small deterministic generator for the display's particles (the same picture every time). */
    struct Scatter
    {
        std::uint32_t state = 0x9e3779b9u;
        float next() noexcept
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return static_cast<float> (state & 0xffffffu) / 16777216.0f;
        }
    };
}

//==============================================================================
void SourceDisplay::setInstrument (std::shared_ptr<const LoadedInstrument> next)
{
    instrument = std::move (next);
    cacheDirty = true;
    repaint();
}

void SourceDisplay::setLayer (int l)
{
    if (layer != l)
    {
        layer = l;
        cacheDirty = true;
        repaint();
    }
}

void SourceDisplay::setFocused (bool f)
{
    if (focused != f)
    {
        focused = f;
        cacheDirty = true;
        repaint();
    }
}

void SourceDisplay::setLoading (bool l)
{
    if (loading != l)
    {
        loading = l;
        cacheDirty = true;
        repaint();
    }
}

void SourceDisplay::setView (const View& v)
{
    if (! (v == view))
    {
        view = v;
        cacheDirty = true;
        repaint();
    }
}

void SourceDisplay::setGrains (const Dot* dots, int count)
{
    if (count == 0 && grains.empty())
        return;
    grains.assign (dots, dots + count);
    repaint();
}

void SourceDisplay::setPlayheads (const Dot* dots, int count)
{
    if (count == 0 && heads.empty())
        return;
    heads.assign (dots, dots + count);
    repaint();
}

void SourceDisplay::setDropLabel (const juce::String& label)
{
    if (dropLabel != label)
    {
        dropLabel = label;
        repaint();
    }
}

void SourceDisplay::setBottomInset (int pixels)
{
    if (bottomInset != pixels)
    {
        bottomInset = pixels;
        cacheDirty = true;
        repaint();
    }
}

void SourceDisplay::setOverlayBand (int pixels)
{
    if (overlayBand != pixels)
    {
        overlayBand = pixels;
        repaint();
    }
}

juce::Rectangle<float> SourceDisplay::plotArea() const
{
    // Reference: time labels in a 20 px strip at the top; t = 0 sits 26 px in from the left.
    return getLocalBounds().toFloat().withTrimmedLeft (26.0f).withTrimmedRight (13.0f).withTrimmedTop (22.0f).withTrimmedBottom (12.0f + static_cast<float> (bottomInset));
}

juce::Range<double> SourceDisplay::window() const
{
    if (instrument == nullptr || instrument->durationSeconds <= 0.0)
        return { 0.0, 1.0 };
    const double duration = instrument->durationSeconds;
    const double soundEnd = std::clamp (duration - instrument->analysis.envelope.trailingSilenceSeconds, 0.0, duration);
    const double begin = std::clamp (instrument->startSeconds, 0.0, soundEnd);
    // A little air after the last audible moment, so the fade-out reads as an ending.
    const double end = std::min (duration, soundEnd + std::max (0.02, 0.03 * (soundEnd - begin)));
    if (end - begin < 0.02)
        return { 0.0, duration };   // (almost) nothing audible: show the whole file
    return { begin, end };
}

float SourceDisplay::xAtSeconds (double seconds, juce::Rectangle<float> plot) const
{
    const auto w = window();
    return plot.getX() + static_cast<float> ((seconds - w.getStart()) / std::max (1.0e-6, w.getLength())) * plot.getWidth();
}

float SourceDisplay::xAtFraction (double fraction, juce::Rectangle<float> plot) const
{
    const double duration = instrument != nullptr ? instrument->durationSeconds : 1.0;
    return std::clamp (xAtSeconds (fraction * duration, plot), plot.getX(), plot.getRight());
}

double SourceDisplay::startSeconds() const
{
    if (instrument == nullptr || instrument->durationSeconds <= 0.0)
        return 0.0;
    const double begin = instrument->startSeconds, end = instrument->durationSeconds;
    if (! view.reverse)
        return begin + view.start * (end - begin);
    const double trailing = instrument->analysis.envelope.trailingSilenceSeconds;
    const double soundEnd = std::clamp (end - trailing, begin, end);
    return soundEnd - view.start * (soundEnd - begin);
}

void SourceDisplay::paint (juce::Graphics& g)
{
    using namespace palette;
    const float scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * scale));
    const int h = std::max (1, juce::roundToInt (static_cast<float> (getHeight()) * scale));
    if (cacheDirty || ! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (scale));
        paintStatic (cg);
        cacheDirty = false;
    }
    g.drawImage (cache, getLocalBounds().toFloat());

    const auto plot = plotArea();
    const bool hasWave = instrument != nullptr && ! instrument->peakMax.empty() && ! loading;
    const auto& tones = waveTones (layer);
    const auto inner = getLocalBounds().toFloat().reduced (2.0f);
    if (hasWave && view.granular)
    {
        // Where grains may come from (SPREAD, half the length either side at 100 %), as a
        // translucent pane over the waveform; POS a fine bright line with a flare at the centre.
        const float x = xAtFraction (view.position, plot);
        const float left = xAtFraction (view.position - (view.spread * 0.5f + 0.004f), plot);
        const float right = xAtFraction (view.position + (view.spread * 0.5f + 0.004f), plot);
        const auto pane = juce::Rectangle<float> (left, inner.getY() + 28.0f, right - left, inner.getHeight() - 46.0f).getIntersection (inner);
        g.setColour (tones.light.withAlpha (0.09f));
        g.fillRect (pane);
        g.setColour (tones.light.withAlpha (0.18f));
        g.fillRect (pane.withWidth (1.0f));
        g.fillRect (pane.withLeft (pane.getRight() - 1.0f));
        g.setColour (tones.light.withAlpha (0.65f));
        g.fillRect (juce::Rectangle<float> (x - 0.6f, pane.getY(), 1.2f, pane.getHeight()));
        const float mid = plot.getCentreY();
        juce::ColourGradient flare (tones.core.withAlpha (0.9f), x, mid, tones.core.withAlpha (0.0f), x + 9.0f, mid, true);
        g.setGradientFill (flare);
        g.fillEllipse (x - 9.0f, mid - 9.0f, 18.0f, 18.0f);
        // The grains playing now: small bright motes on their lanes, a faint read line each.
        for (const auto& grain : grains)
        {
            const float gx = xAtFraction (grain.position, plot);
            const float level = std::clamp (grain.level, 0.0f, 1.0f);
            g.setColour (tones.light.withAlpha (0.05f + 0.18f * level));
            g.fillRect (juce::Rectangle<float> (gx - 0.5f, pane.getY(), 1.0f, pane.getHeight()));
            const float gy = plot.getY() + 6.0f + grain.lane * (plot.getHeight() - 12.0f);
            const float r = 0.8f + 1.0f * level;
            g.setColour (tones.core.withAlpha (0.3f + 0.5f * level));
            g.fillEllipse (gx - r, gy - r, 2.0f * r, 2.0f * r);
        }
    }
    if (hasWave)
    {
        // One Shot read heads (the earlier, slim treatment): a fine accent line where each
        // playing note reads, as bright as it is loud; no glow.
        for (const auto& head : heads)
        {
            const float x = xAtFraction (head.position, plot);
            const float level = 0.3f + 0.7f * std::clamp (head.level, 0.0f, 1.0f);
            g.setColour (juce::Colour (0xfff07a3c).withAlpha (0.95f * level));
            g.fillRect (juce::Rectangle<float> (x - 0.6f, plot.getY(), 1.2f, plot.getHeight()));
        }
    }
    if (overlayBand > 0)
    {
        // The granular controls float here: the waveform shows through, quietened.
        const auto band = inner.withTop (inner.getBottom() - static_cast<float> (overlayBand));
        const auto shade = design::colour::wellA;
        g.setGradientFill (juce::ColourGradient (shade.withAlpha (0.0f), 0.0f, band.getY() - 12.0f, shade.withAlpha (0.88f), 0.0f, band.getY() + 14.0f, false));
        g.fillRect (band.withTop (band.getY() - 12.0f));
    }
    if (dropLabel.isNotEmpty())
    {
        const auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (graphite.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 7.0f);
        juce::Path outline;
        outline.addRoundedRectangle (r, 7.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.5f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (accent);
        g.fillPath (dashed);
        g.setColour (raised);
        g.setFont (fonts::make (15.0f, fonts::Weight::medium, 0.08f));
        g.drawText (dropLabel, r, juce::Justification::centred, false);
    }
}

void SourceDisplay::paintStatic (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    draw::well (g, bounds.reduced (0.5f), 9.0f, layer == 1 ? colour::wellB : colour::wellA);

    const auto plot = plotArea();
    const bool hasWave = instrument != nullptr && ! instrument->peakMax.empty();
    if (loading || ! hasWave)
    {
        if (loading)
        {
            g.setColour (colour::wellText);
            g.setFont (fonts::make (14.0f, fonts::Weight::medium, 0.12f));
            g.drawText (juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"), bounds, juce::Justification::centred, false);
        }
        else
        {
            // An empty slot (clear all samples, a starting state): where its sound goes.
            const auto hint = bounds.reduced (16.0f, 14.0f);
            juce::Path outline, dashed;
            outline.addRoundedRectangle (hint, 7.0f);
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour::wellText.withAlpha (0.35f));
            g.fillPath (dashed);
            g.setColour (colour::wellText);
            g.setFont (fonts::make (16.0f, fonts::Weight::regular, 0.12f));
            g.drawText ("DROP A SOUND", hint.withTrimmedBottom (0.5f * hint.getHeight() - 4.0f).withTrimmedTop (0.5f * hint.getHeight() - 24.0f),
                        juce::Justification::centred, false);
            g.setColour (colour::wellText.withAlpha (0.6f));
            g.setFont (fonts::make (13.0f, fonts::Weight::regular, 0.04f));
            g.drawText ("its settings are kept", hint.withTrimmedTop (0.5f * hint.getHeight() + 4.0f).withHeight (16.0f), juce::Justification::centred, false);
        }
        return;
    }

    const double duration = std::max (1.0e-3, instrument->durationSeconds);
    const auto shown = window();
    const float mid = plot.getCentreY();
    // Quiet grid: a line at every labelled time (true times in the file), a fainter one
    // between; four rows.
    {
        const double step = gridStep (shown.getLength());
        const auto inner = bounds.reduced (2.0f);
        for (int row = 1; row < 4; ++row)
        {
            g.setColour (colour::wellGrid.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getY() + inner.getHeight() * static_cast<float> (row) / 4.0f, inner.getWidth(), 1.0f));
        }
        g.setFont (fonts::make (14.5f, fonts::Weight::regular, 0.02f));
        for (double t = 0.5 * step * std::ceil (shown.getStart() / (0.5 * step) + 1.0e-9); t < shown.getEnd() + step; t += 0.5 * step)
        {
            if (t <= 1.0e-9)
                continue;
            const float x = xAtSeconds (t, plot);
            if (x < inner.getX() + 2.0f)
                continue;
            if (x > inner.getRight() - 2.0f)
                break;
            const bool labelFits = x < inner.getRight() - 30.0f;
            const bool major = std::abs (std::fmod (t + 1.0e-9, step)) < 1.0e-6;
            g.setColour (colour::wellGrid.withAlpha (major ? 0.95f : 0.45f));
            g.fillRect (juce::Rectangle<float> (x, inner.getY(), 1.0f, inner.getHeight()));
            if (major && labelFits && getWidth() > 240)
            {
                g.setColour (colour::wellText);
                g.drawText (secondsText (t, step), juce::Rectangle<float> (x - 40.0f, bounds.getY() + 5.0f, 80.0f, 18.0f), juce::Justification::centred, false);
            }
        }
    }

    const auto& tones = waveTones (layer);
    const auto& lo = instrument->peakMin;
    const auto& hi = instrument->peakMax;
    const auto& rms = instrument->peakRms;
    const auto& bright = instrument->peakBright;
    const bool haveRms = rms.size() == hi.size() && bright.size() == hi.size();
    float maxAbs = 1.0e-4f;   // scaled to the recording's own peak so quiet sources stay visible
    for (std::size_t i = 0; i < hi.size(); ++i)
        maxAbs = std::max ({ maxAbs, hi[i], -lo[i] });
    const float half = plot.getHeight() * 0.47f;
    // Display scale: amplitude compressed (power 0.6) so quiet detail and tails read, as in
    // the reference; the ghost envelope uses a 45 dB range.
    auto shape = [maxAbs, half] (float a) { return half * std::pow (std::clamp (std::abs (a) / maxAbs, 0.0f, 1.0f), 0.85f) * (a < 0.0f ? -1.0f : 1.0f); };
    auto shapeDb = [maxAbs, half] (float a) {
        const float x = std::clamp (std::abs (a) / maxAbs, 0.0f, 1.0f);
        const float db = 20.0f * std::log10 (std::max (x, 1.0e-6f));
        return half * (0.5f * std::clamp ((db + 36.0f) / 36.0f, 0.0f, 1.0f) + 0.5f * std::pow (x, 0.6f));
    };

    // Columns at the cache's own resolution (two per logical pixel on a 2x display).
    const float scale = std::max (1.0f, std::abs (g.getInternalContext().getPhysicalPixelScaleFactor()));
    const int columns = std::max (1, juce::roundToInt (plot.getWidth() * scale));
    const float colWidth = plot.getWidth() / static_cast<float> (columns);
    struct Column
    {
        float lo = 0.0f, hi = 0.0f, rms = 0.0f, bright = 0.0f;
    };
    std::vector<Column> cols (static_cast<std::size_t> (columns));
    // The peak buckets under column c: the shown window's share of the whole file.
    auto bucketRange = [&hi, &shown, duration, columns] (int c) {
        const double size = static_cast<double> (hi.size());
        auto at = [&] (int k) { return (shown.getStart() + shown.getLength() * k / columns) / duration * size; };
        const auto first = std::min (hi.size() - 1, static_cast<std::size_t> (std::max (0.0, at (c))));
        const auto last = std::max (first + 1, std::min (hi.size(), static_cast<std::size_t> (std::max (0.0, at (c + 1)))));
        return std::pair<std::size_t, std::size_t> { first, last };
    };
    Scatter pick { 0x51ed270bu };
    for (int c = 0; c < columns; ++c)
    {
        const auto [first, last] = bucketRange (c);
        Column col;
        double energy = 0.0, b = 0.0;
        // The peaks of the bucket at the column's centre (a short window, shorter than a
        // period: the waveform keeps its texture instead of a flat envelope), and the
        // column's true extremes for the outline.
        // (A deterministic jitter of where in the column: a steady tone does not alias into combs.)
        const auto centre = std::min (hi.size() - 1, first + static_cast<std::size_t> (pick.next() * static_cast<float> (last - first)));
        col.lo = 0.65f * lo[centre];
        col.hi = 0.65f * hi[centre];
        for (auto i = first; i < last; ++i)
        {
            col.lo = std::min (col.lo, 0.35f * lo[i] + 0.65f * lo[centre]);
            col.hi = std::max (col.hi, 0.35f * hi[i] + 0.65f * hi[centre]);
            if (haveRms)
            {
                energy += static_cast<double> (rms[i]) * rms[i];
                b += bright[i];
            }
        }
        const auto n = static_cast<double> (last - first);
        col.rms = haveRms ? static_cast<float> (std::sqrt (energy / n)) : 0.35f * std::max (col.hi, -col.lo);
        col.bright = haveRms ? static_cast<float> (b / n) : 0.5f;
        cols[static_cast<std::size_t> (c)] = col;
    }

    // The waveform, column by column, in four layers that stay inside its own outline (no
    // halo, no light outside it): (1) the true peak silhouette, crisp and irregular, in a
    // dark saturated tone; (2) the dense body from the column's own peaks; (3) the energy
    // (RMS) inside it, lighter; (4) a spine following the energy, lightest; then sparse thin
    // filaments where the analysis finds onsets. Brighter moments lean lighter and cooler,
    // louder ones fuller; positive and negative sides use their own peaks.
    const auto& flux = instrument->peakFlux;
    const bool haveFlux = flux.size() == hi.size();
    auto toneAt = [&tones] (float b, float t) {
        // t: 0 outer .. 1 core; b: brightness 0 dull .. 1 bright.
        const auto edge = tones.deep.interpolatedWith (tones.body, 0.25f + 0.35f * b);
        const auto body = tones.body.interpolatedWith (tones.light, 0.15f + 0.55f * b);
        const auto core = tones.light.interpolatedWith (tones.pale, 0.55f + 0.45f * b);
        return t < 0.5f ? edge.interpolatedWith (body, 2.0f * t) : body.interpolatedWith (core, 2.0f * t - 1.0f);
    };
    const float tint = focused ? 1.0f : 0.82f;
    Scatter detail { 0x2545f491u };
    for (int c = 0; c < columns; ++c)
    {
        const auto& col = cols[static_cast<std::size_t> (c)];
        const auto [first, last] = bucketRange (c);
        float trueLo = 0.0f, trueHi = 0.0f, onset = 0.0f;
        for (auto i = first; i < last; ++i)
        {
            trueLo = std::min (trueLo, lo[i]);
            trueHi = std::max (trueHi, hi[i]);
            if (haveFlux)
                onset = std::max (onset, flux[i]);
        }
        const float x = plot.getX() + static_cast<float> (c) * colWidth;
        const float level = std::clamp (col.rms / maxAbs, 0.0f, 1.0f);
        const float fullness = 0.75f + 0.25f * std::sqrt (level);
        // (1) The outer silhouette: the true extremes.
        const float outTop = mid - shape (trueHi), outBottom = mid - shape (trueLo);
        g.setColour (toneAt (col.bright, 0.05f).withAlpha (0.5f * tint));
        g.fillRect (juce::Rectangle<float> (x, outTop, colWidth, std::max (colWidth * 0.6f, outBottom - outTop)));
        // (2) The body: the column's own peaks (texture, not an envelope) drawn in towards the
        // energy, so the silhouette stays visible around it.
        const float span = std::max (col.hi - col.lo, 1.0e-6f);
        const float up = std::clamp (col.hi / span, 0.25f, 0.75f);
        const float bodyHi = 0.62f * col.hi + 0.38f * (2.0f * up * 1.3f * col.rms);
        const float bodyLo = 0.62f * col.lo - 0.38f * (2.0f * (1.0f - up) * 1.3f * col.rms);
        const float bodyTop = mid - shape (bodyHi), bodyBottom = mid - shape (bodyLo);
        {
            juce::ColourGradient fill (toneAt (col.bright, 0.28f).withAlpha (0.78f * tint * fullness), 0.0f, bodyTop,
                                       toneAt (col.bright, 0.28f).withAlpha (0.78f * tint * fullness), 0.0f, bodyBottom, false);
            fill.addColour (0.5, toneAt (col.bright, 0.5f).withAlpha (0.9f * tint * fullness));
            g.setGradientFill (fill);
            g.fillRect (juce::Rectangle<float> (x, bodyTop, colWidth, std::max (colWidth * 0.6f, bodyBottom - bodyTop)));
        }
        // (3) The energy: RMS, shared between the sides as the peaks are; lighter.
        const float energy = std::min (shape (col.rms * 1.5f), 0.8f * 0.5f * (bodyBottom - bodyTop));
        const float eTop = mid - 2.0f * up * energy, eBottom = mid + 2.0f * (1.0f - up) * energy;
        if (eBottom - eTop > 0.4f)
        {
            juce::ColourGradient fill (toneAt (col.bright, 0.62f).withAlpha (0.8f * tint), 0.0f, eTop,
                                       toneAt (col.bright, 0.62f).withAlpha (0.8f * tint), 0.0f, eBottom, false);
            fill.addColour (0.5, toneAt (col.bright, 0.86f).withAlpha (0.92f * tint));
            g.setGradientFill (fill);
            g.fillRect (juce::Rectangle<float> (x, eTop, colWidth, eBottom - eTop));
        }
        // (4) The spine: the densest energy, following its level (it thins to nothing in
        // silence; it is not a line across the display).
        const float spine = 0.4f * energy;
        if (spine > 0.35f)
        {
            g.setColour (toneAt (col.bright, 1.0f).withAlpha ((0.6f + 0.3f * level) * tint));
            g.fillRect (juce::Rectangle<float> (x, mid - 2.0f * up * spine, colWidth, 2.0f * spine));
        }
        // Detail: thin filaments to the true peak at onsets (and, sparsely, where the peaks
        // stand well above the energy), from the analysis - not decoration.
        const float crest = std::max (trueHi, -trueLo) / std::max (col.rms, 1.0e-6f);
        const float chance = 0.45f * onset * onset * onset + (crest > 6.0f ? 0.012f : 0.0f);
        if (detail.next() < chance)
        {
            const float side = detail.next() < up ? -1.0f : 1.0f;
            const float reach = side < 0.0f ? mid - outTop : outBottom - mid;
            g.setColour (toneAt (col.bright, 0.9f).withAlpha ((0.22f + 0.4f * onset) * tint));
            g.fillRect (juce::Rectangle<float> (x, side < 0.0f ? mid - reach : mid, std::max (0.5f, colWidth * 0.8f), reach));
        }
    }

    // Particles (as in the earlier build): fine dust around the body out to its envelope,
    // as many and as far out as the sound is loud there (texture, never noise).
    {
        Scatter rng;
        for (int c = 0; c < columns; ++c)
            for (int n = 0; n < 2; ++n)
            {
                const auto& col = cols[static_cast<std::size_t> (c)];
                const float amp = std::max (col.hi, -col.lo) / maxAbs;
                const float chance = (view.granular ? 0.45f : 0.55f) * std::sqrt (amp) + 0.03f;
                if (rng.next() > chance)
                    continue;
                const float side = rng.next() < 0.5f ? -1.0f : 1.0f;
                // Mostly just outside the body, thinning out towards the sound's envelope.
                const float body = std::max (shape (std::max (col.hi, -col.lo)), 0.6f * shapeDb (col.rms));
                const float reach = body * (0.9f + 0.85f * std::pow (rng.next(), 1.4f)) + 1.5f + 8.0f * rng.next() * std::sqrt (amp);
                const float y = mid + side * std::min (half * 1.05f, reach);
                const float x = plot.getX() + static_cast<float> (c) * colWidth;
                const float d = 0.9f + 1.2f * rng.next();
                g.setColour (toneAt (col.bright, 0.9f).withAlpha ((0.3f + 0.55f * rng.next()) * tint));
                g.fillEllipse (x - 0.5f * d, y - 0.5f * d, d, d);
            }
    }

    // The loop region (One Shot + LOOP): a slim bracket along the bottom.
    if (! view.granular && view.loop && instrument->model != nullptr && instrument->model->original.continuation.canSustain
        && instrument->model->original.source != nullptr)
    {
        const auto& cont = instrument->model->original.continuation;
        const double frames = std::max<double> (1.0, static_cast<double> (instrument->model->original.source->numFrames()));
        const float a = xAtFraction (cont.sustainStartFrame / frames, plot);
        const float b = xAtFraction (cont.sustainEndFrame / frames, plot);
        const float y = bounds.getBottom() - 9.0f;
        g.setColour (colour::wellText.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (a, y, b - a, 1.2f));
        g.fillRect (juce::Rectangle<float> (b - 1.0f, y - 4.0f, 1.2f, 4.0f));
    }

    // START: a fine accent line from a small flag at the top to a dot at the foot.
    if (! view.granular)
    {
        const float x = std::clamp (xAtSeconds (startSeconds(), plot), plot.getX(), plot.getRight());
        g.setColour (juce::Colour (0x8c000000));
        if (view.reverse)
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), plot.getRight() - x, plot.getHeight()));
        else if (x > plot.getX() + 1.0f)
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), x - plot.getX(), plot.getHeight()));
        // A fine accent line with a small cap (the earlier, slim treatment): no glow, no handle.
        g.setColour (colour::accent.withAlpha (0.95f));
        g.fillRect (juce::Rectangle<float> (x - 0.6f, plot.getY() - 2.0f, 1.2f, plot.getHeight() + 4.0f));
        juce::Path cap;
        const float d = view.reverse ? -1.0f : 1.0f;
        cap.addTriangle (x, plot.getY() - 8.0f, x + d * 6.5f, plot.getY() - 5.0f, x, plot.getY() - 2.0f);
        g.fillPath (cap);
    }
}

//==============================================================================
class LayerKnob::Dial final : public juce::Slider
{
public:
    Dial (OspAudioProcessor& p, int l, juce::String c, bool semitones)
        : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox),
          processor (p), layer (l), control (std::move (c)), snapSemitones (semitones)
    {
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        beginGesture();
        juce::Slider::mouseDown (e);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp (e);
        user = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        beginGesture();
        juce::Slider::mouseDoubleClick (e);
        user = false;
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        beginGesture();
        juce::Slider::mouseWheelMove (e, wheel);
        user = false;
    }
    void valueChanged() override
    {
        // LINK: only the musician's own gestures move the other linked layers.
        if (! user)
            return;
        const double now = getValue();
        processor.applyLinkedDelta (layer, control, static_cast<float> (now - last));
        last = now;
    }
    double snapValue (double value, DragMode) override
    {
        // TUNE moves in semitones; Alt (Option) drags finely.
        if (snapSemitones && ! juce::ModifierKeys::currentModifiers.isAltDown())
            return std::round (value);
        return value;
    }

private:
    void beginGesture()
    {
        user = true;
        last = getValue();
    }
    OspAudioProcessor& processor;
    int layer;
    juce::String control;
    bool snapSemitones;
    bool user = false;
    double last = 0.0;
};

LayerKnob::LayerKnob (OspAudioProcessor& p, int layer, const juce::String& control, const juce::String& c)
    : processor (p), caption (c), creative (control == "reimagined")
{
    // REIMAGINED keeps its shipped IDs: A's is the instrument's `reimagined`.
    const auto id = creative ? OspAudioProcessor::reimaginedParameterId (layer) : OspAudioProcessor::layerParameterId (layer, control);
    parameter = processor.parameters.getParameter (id);
    dial = std::make_unique<Dial> (processor, layer, control, control == "tune");
    dial->setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    dial->getProperties().set ("arc", static_cast<juce::int64> ((creative ? palette::accent : palette::layer (layer)).getARGB()));
    dial->getProperties().set ("bipolar", control == "tune" || control == "pan");
    dial->setTitle ("Layer " + OspAudioProcessor::layerName (layer) + " " + caption);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, id, *dial);
    if (parameter != nullptr)
    {
        dial->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        dial->setTooltip (creative ? juce::String ("Reimagined: how far this source moves from its original character")
                                   : parameter->getName (64) + (control == "tune" ? juce::String (" (Alt-drag: fine)") : juce::String()));
    }
    dial->onValueChange = [this] { repaint(); };   // also when automation or LINK moves it (the attachment moves the dial)
    addAndMakeVisible (*dial);
}

void LayerKnob::setCompact (bool c)
{
    if (compact != c)
    {
        compact = c;
        resized();
        repaint();
    }
}

// Reference geometry (layer knob, 110 x 125; the five-knob row draws it at 87 %): caption
// centred 13 px from the top, the dial (92 px with its ticks) centred at 61, the value
// centred at 112.
void LayerKnob::resized()
{
    const float k = static_cast<float> (getHeight()) / 125.0f;
    dial->setBounds (juce::Rectangle<float> (92.0f * k, 92.0f * k).withCentre ({ 0.5f * static_cast<float> (getWidth()), 61.0f * k }).getSmallestIntegerContainer());
}

void LayerKnob::paint (juce::Graphics& g)
{
    const float k = static_cast<float> (getHeight()) / 125.0f;
    const auto w = static_cast<float> (getWidth());
    g.setColour (design::colour::text.withAlpha (creative ? 0.97f : 0.9f));
    // REIMAGINED: a touch tighter so it sits in the row like the short names.
    const auto labelFont = creative ? fonts::make (14.5f * k, fonts::Weight::medium, 0.02f) : type::controlLabel (k);
    g.setFont (labelFont);
    const auto labelArea = juce::Rectangle<float> (0.0f, 2.0f * k, w, 22.0f * k);
    g.drawText (caption, labelArea, juce::Justification::centred, false);
    if (creative)
    {
        // A small coral light after the name, like the macros' (the more creative control).
        const float textWidth = juce::GlyphArrangement::getStringWidth (labelFont, caption);
        const juce::Point<float> c (0.5f * w + 0.5f * textWidth + 6.0f * k, labelArea.getCentreY());
        if (c.x + 3.0f * k < w)
            design::draw::led (g, c, 5.0f * k, palette::accent, 0.3f);
    }
    g.setColour (design::colour::text.withAlpha (0.9f));
    g.setFont (type::controlValue (k));
    const auto value = parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String();
    g.drawText (value, juce::Rectangle<float> (0.0f, 100.0f * k, w, 24.0f * k), juce::Justification::centred, false);
}

//==============================================================================
ModifierButton::ModifierButton (juce::RangedAudioParameter& p, icons::Kind i, juce::Colour c)
    : icon (i), onColour (c),
      attachment (p, [this] (float v) {
          on = v >= 0.5f;
          repaint();
      })
{
    attachment.sendInitialUpdate();
    tip = p.getName (64);
    setTooltip (tip);
    setTitle (tip);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ModifierButton::setSuppressed (bool s, const juce::String& why)
{
    if (suppressed != s)
    {
        suppressed = s;
        setTooltip (s ? tip + ": " + why : tip);
        setMouseCursor (s ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }
}

void ModifierButton::mouseUp (const juce::MouseEvent& e)
{
    if (! suppressed && getLocalBounds().contains (e.getPosition()))
        attachment.setValueAsCompleteGesture (on ? 0.0f : 1.0f);
}

void ModifierButton::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.5f);
    // One state rule for all four: off, a neutral raised key with a soft graphite icon at
    // about 60 %; on, the key pressed in with a fine accent rim and the icon in the accent.
    // Thin strokes (about 1.3 px at the reference size), crisp at any scale.
    const bool active = on && ! suppressed;
    design::draw::button (g, r, 0.16f * r.getHeight(), active, isMouseOver() && ! suppressed, onColour);
    const auto colour = active ? onColour.darker (0.15f).withAlpha (0.95f) : design::colour::text.withAlpha (suppressed ? 0.22f : 0.6f);
    const float icon = 0.6f * r.getHeight();
    icons::draw (g, this->icon, r.withSizeKeepingCentre (icon, icon), colour, std::max (1.0f, 0.03f * r.getHeight()));
}

//==============================================================================
ModeSelector::ModeSelector (juce::RangedAudioParameter& p)
    : attachment (p, [this] (float v) {
          current = v >= 0.5f ? 1 : 0;
          repaint();
          if (onChange != nullptr)
              onChange (current);
      })
{
    attachment.sendInitialUpdate();
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle (p.getName (64));
}

void ModeSelector::choose (int mode)
{
    if (mode != current)
        attachment.setValueAsCompleteGesture (static_cast<float> (mode));
}

void ModeSelector::mouseUp (const juce::MouseEvent& e)
{
    if (! getLocalBounds().contains (e.getPosition()))
        return;
    juce::PopupMenu menu;
    juce::Component::SafePointer<ModeSelector> safe (this);
    menu.addItem ("One Shot", true, current == 0, [safe] { if (safe != nullptr) safe->choose (0); });
    menu.addItem ("Granular", true, current == 1, [safe] { if (safe != nullptr) safe->choose (1); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMinimumWidth (getWidth()).withDeletionCheck (*this));
}

void ModeSelector::paint (juce::Graphics& g)
{
    // A compact source property (157 x 41 in the reference): raised face, name, chevron.
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float k = r.getHeight() / 39.0f;
    design::draw::button (g, r, 7.0f * k, false, isMouseOver(), design::colour::accent);
    // Quiet: the waveform is the hero, the mode only a property of it.
    g.setColour (design::colour::text.withAlpha (0.95f));
    g.setFont (type::sourceMode (k));
    auto text = r.withTrimmedLeft (17.0f * k).withTrimmedRight (14.0f * k);
    icons::draw (g, icons::Kind::chevronDown, text.removeFromRight (16.0f * k).withSizeKeepingCentre (16.0f * k, 16.0f * k), design::colour::text.withAlpha (0.55f), 1.5f * k);
    g.drawText (current == 1 ? "Granular" : "One Shot", text, juce::Justification::centredLeft, false);
}

//==============================================================================
void EngineCard::MenuDots::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    const auto r = getLocalBounds().toFloat();
    if (highlighted)
    {
        g.setColour (design::colour::text.withAlpha (0.06f));
        g.fillRoundedRectangle (r, 6.0f);
    }
    // Three round dots, 10 px apart (reference).
    const float d = 5.0f * r.getHeight() / 36.0f, gap = 10.0f * r.getHeight() / 36.0f;
    g.setColour (design::colour::text);
    for (int i = -1; i <= 1; ++i)
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (r.getCentre().translated (0.0f, static_cast<float> (i) * gap)));
}

EngineCard::EngineCard (OspAudioProcessor& p, int layer) : processor (p), layerIndex (layer)
{
    sourceDisplay.setLayer (layer);
    addAndMakeVisible (sourceDisplay);

    if (auto* param = processor.parameters.getParameter (OspAudioProcessor::layerParameterId (layer, "sourceMode")))
    {
        mode = std::make_unique<ModeSelector> (*param);
        mode->setTooltip ("Layer " + OspAudioProcessor::layerName (layer) + ": play the recording through, or as a cloud of grains");
        mode->onChange = [this] (int) { updateMode(); };
        addAndMakeVisible (*mode);
    }
    menuButton.setTooltip ("Replace, root, remove...");
    menuButton.onClick = [this] {
        if (onMenu != nullptr)
            onMenu (layerIndex, menuButton);
    };
    addAndMakeVisible (menuButton);

    const std::array<std::pair<const char*, const char*>, 5> controls { { { "start", "START" }, { "tune", "TUNE" }, { "pan", "PAN" }, { "level", "LEVEL" },
                                                                         { "reimagined", "REIMAGINED" } } };
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        knobs[i] = std::make_unique<LayerKnob> (processor, layer, controls[i].first, controls[i].second);
        addAndMakeVisible (*knobs[i]);
    }
    const std::array<std::pair<const char*, icons::Kind>, 4> mods { { { "link", icons::Kind::link }, { "reverse", icons::Kind::reverse },
                                                                       { "loop", icons::Kind::loop }, { "follow", icons::Kind::follow } } };
    for (std::size_t i = 0; i < modifiers.size(); ++i)
        if (auto* param = processor.parameters.getParameter (OspAudioProcessor::layerParameterId (layer, mods[i].first)))
        {
            modifiers[i] = std::make_unique<ModifierButton> (*param, mods[i].second, palette::accent);
            addAndMakeVisible (*modifiers[i]);
        }
    modifiers[0]->setTooltip ("Link: START, TUNE, PAN, LEVEL and REIMAGINED move together with the other linked layers");
    modifiers[1]->setTooltip ("Reverse: play the recording (or the grains) backwards");
    modifiers[2]->setTooltip ("Loop: sustain a held note with the recording's own loops (off: play it once)");
    modifiers[3]->setTooltip ("Follow: keep the recording's own loudness contour (off: hold it level, like a sustained tone)");

    const std::array<std::pair<const char*, const char*>, 5> grain { { { "granular.position", "POS" }, { "granular.size", "SIZE" },
                                                                        { "granular.density", "DENS" }, { "granular.tune", "TUNE" },
                                                                        { "granular.spread", "SPREAD" } } };
    for (std::size_t i = 0; i < granularKnobs.size(); ++i)
    {
        const auto id = OspAudioProcessor::layerParameterId (layer, grain[i].first);
        auto* param = processor.parameters.getParameter (id);
        MiniKnob::Formatter f = [param] (double) { return param != nullptr ? param->getCurrentValueAsText() + (param->getLabel().isNotEmpty() ? " " + param->getLabel() : juce::String()) : juce::String(); };
        if (i == 0 || i == 4)
            f = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
        else if (i == 1)
            f = [] (double v) { return format::milliseconds (v); };
        else if (i == 2)
            f = [] (double v) { return juce::String (v, v < 10.0 ? 1 : 0) + "/s"; };
        else if (i == 3)
            f = [] (double v) { const int st = juce::roundToInt (v); return (st > 0 ? "+" : "") + juce::String (st) + " st"; };
        granularKnobs[i] = std::make_unique<MiniKnob> (processor.parameters, id, grain[i].second, std::move (f));
        granularKnobs[i]->setOnDark (true);
        granularKnobs[i]->slider.getProperties().set ("arc", static_cast<juce::int64> (palette::layer (layer).getARGB()));
        addChildComponent (*granularKnobs[i]);
    }

    // A click anywhere on the card (its controls included) makes it the edited layer.
    addMouseListener (this, true);
    updateMode();
    refresh();
}

EngineCard::~EngineCard()
{
    removeMouseListener (this);
}

void EngineCard::mouseDown (const juce::MouseEvent&)
{
    if (onFocus != nullptr)
        onFocus (layerIndex);
}

void EngineCard::setDensity (EngineLayoutDensity d)
{
    if (density != d)
    {
        density = d;
        resized();
        repaint();
    }
}

void EngineCard::setFocused (bool f)
{
    if (focused != f)
    {
        focused = f;
        sourceDisplay.setFocused (f);
        repaint();
    }
}

void EngineCard::mouseMove (const juce::MouseEvent& e)
{
    // Granular controls come up while the pointer is over the display (or one of them).
    const auto where = e.getEventRelativeTo (this).getPosition();
    showGranularControls (sourceDisplay.getBounds().contains (where));
}

void EngineCard::mouseExit (const juce::MouseEvent& e)
{
    const auto where = e.getEventRelativeTo (this).getPosition();
    if (! getLocalBounds().contains (where))
        showGranularControls (false);
}

void EngineCard::showGranularControls (bool show)
{
    const bool granular = mode != nullptr && mode->mode() == 1;
    bool dragging = false;
    for (auto& k : granularKnobs)
        dragging = dragging || k->slider.isMouseButtonDown();
    const bool visible = granular && (show || dragging);
    if (visible == granularShown)
        return;
    granularShown = visible;
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (auto& k : granularKnobs)
    {
        if (visible)
        {
            k->setAlpha (0.0f);
            k->setVisible (true);
            animator.fadeIn (k.get(), 140);
        }
        else
            animator.fadeOut (k.get(), 160);
    }
    sourceDisplay.setOverlayBand (visible ? 80 : 0);
}

void EngineCard::updateMode()
{
    const bool granular = mode != nullptr && mode->mode() == 1;
    if (! granular)
    {
        granularShown = false;
        for (auto& k : granularKnobs)
            k->setVisible (false);
        sourceDisplay.setOverlayBand (0);
    }
    if (modifiers[2] != nullptr)
        modifiers[2]->setSuppressed (granular, "Granular sustains by itself");
    resized();
}

void EngineCard::refresh()
{
    const auto instrument = processor.currentInstrument (layerIndex);
    const bool loading = processor.loadState (layerIndex) == OspAudioProcessor::LoadState::loading;
    const auto generation = instrument != nullptr ? instrument->generation : 0;
    if (generation != shownGeneration || loading != shownLoading)
    {
        shownGeneration = generation;
        shownLoading = loading;
        sourceDisplay.setInstrument (instrument);
        sourceDisplay.setLoading (loading);
        fileText = instrument != nullptr ? juce::String::fromUTF8 (instrument->filename.c_str()) : juce::String (loading ? "Loading" : "Empty slot");
        repaint();
    }
    // Root: the correction if there is one, else the detected note.
    juce::String root;
    if (const auto overrideMidi = processor.rootOverride (layerIndex))
        root = midiNoteName (static_cast<int> (std::lround (*overrideMidi)));
    else if (instrument != nullptr)
        root = instrument->analysis.pitch.detected ? juce::String (instrument->analysis.pitch.noteName) : juce::String ("?");
    if (root != rootText)
    {
        rootText = root;
        repaint();
    }

    auto value = [this] (const char* name) { return processor.parameterValue (OspAudioProcessor::layerParameterId (layerIndex, name)); };
    SourceDisplay::View view;
    view.granular = value ("sourceMode") >= 0.5f;
    view.start = 0.01f * value ("start");
    view.position = std::clamp (0.01f * (value ("granular.position") + value ("start")), 0.0f, 1.0f);
    view.spread = 0.01f * value ("granular.spread");
    view.loop = value ("loop") >= 0.5f;
    view.reverse = value ("reverse") >= 0.5f;
    sourceDisplay.setView (view);

    const auto& snapshot = processor.grainSnapshot (layerIndex);
    std::array<SourceDisplay::Dot, InstrumentEngine::GrainSnapshot::capacity> dots;
    const int count = std::clamp (snapshot.count.load (std::memory_order_acquire), 0, InstrumentEngine::GrainSnapshot::capacity);
    for (int i = 0; i < count; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        dots[k] = { snapshot.position[k].load (std::memory_order_relaxed), snapshot.level[k].load (std::memory_order_relaxed),
                    snapshot.lane[k].load (std::memory_order_relaxed) };
    }
    sourceDisplay.setGrains (dots.data(), view.granular ? count : 0);
    const int heads = std::clamp (snapshot.playheads.load (std::memory_order_acquire), 0, InstrumentEngine::GrainSnapshot::playheadCapacity);
    for (int i = 0; i < heads; ++i)
    {
        const auto k = static_cast<std::size_t> (i);
        dots[k] = { snapshot.playheadPosition[k].load (std::memory_order_relaxed), snapshot.playheadLevel[k].load (std::memory_order_relaxed), 0.5f };
    }
    sourceDisplay.setPlayheads (dots.data(), heads);
}

void EngineCard::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    draw::raised (g, bounds, layout::cardRadius, colour::cardTop, colour::cardBottom);

    // Header: the layer's badge, its light, the root and the file.
    const auto& id = colour::identity (layerIndex);
    {
        const auto badge = badgeArea.toFloat();
        juce::Path shape;
        shape.addRoundedRectangle (badge, 6.0f);
        juce::DropShadow (juce::Colour (0x40302418), 4, { 0, 2 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (id.badgeTop, 0.0f, badge.getY(), id.badgeBottom, 0.0f, badge.getBottom(), false));
        g.fillPath (shape);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.strokePath (shape, juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 0.5f));
        g.setColour (juce::Colour (0xfffff6ec));
        g.setFont (fonts::make (29.0f, fonts::Weight::regular));
        g.drawText (OspAudioProcessor::layerName (layerIndex), badge.translated (0.0f, 0.5f), juce::Justification::centred, false);
    }
    draw::led (g, ledCentre, 12.0f, id.led, 1.0f);
    auto row = textArea.toFloat();
    if (rootText.isNotEmpty())
    {
        const auto rootFont = type::sourceRoot();
        g.setFont (rootFont);
        g.setColour (colour::text.withAlpha (0.92f));
        const float w = juce::GlyphArrangement::getStringWidth (rootFont, rootText);
        g.drawText (rootText, row.removeFromLeft (w + 2.0f), juce::Justification::centredLeft, false);
        row.removeFromLeft (17.0f);
    }
    // Metadata: quieter than the root and the controls.
    g.setColour (colour::textSecondary);
    g.setFont (type::sourceFilename());
    g.drawText (fileText, row, juce::Justification::centredLeft, true);

    // Hairline between the knobs and the modifiers.
    if (! dividerArea.isEmpty())
    {
        g.setColour (colour::divider);
        g.fillRect (dividerArea.toFloat());
    }
}

void EngineCard::resized()
{
    // Reference geometry: the two-layer card is 683 x 463 px. One layer stretches it
    // across the band (and, without a mix band, a little taller), three narrow it and put
    // the modifiers in a row under the knobs. Everything below the display hangs from
    // the card's foot, so a taller card gives its height to the display.
    const auto w = static_cast<float> (getWidth());
    const auto h = static_cast<float> (getHeight());
    const bool triple = density == EngineLayoutDensity::triple;
    auto at = [] (juce::Rectangle<float> r) { return r.getSmallestIntegerContainer(); };

    badgeArea = at ({ 21.0f, 10.0f, 47.0f, 43.0f });
    ledCentre = { 94.0f, 31.5f };
    menuButton.setBounds (at ({ w - 51.0f, 14.0f, 30.0f, 36.0f }));
    const float modeWidth = triple ? 128.0f : 157.0f;
    if (mode != nullptr)
        mode->setBounds (at ({ w - 62.0f - modeWidth, 12.0f, modeWidth, 41.0f }));
    textArea = at ({ 117.0f, 14.0f, w - 62.0f - modeWidth - 12.0f - 117.0f, 36.0f });

    const float wellHeight = h - (triple ? 267.0f : 206.0f);
    sourceDisplay.setBounds (at ({ 16.0f, 63.0f, w - 32.0f, wellHeight }));

    // START TUNE PAN LEVEL REIMAGINED: one family at 87 % of the four-knob size, on one
    // pitch, with a little more room at the row's ends than between neighbours.
    auto row = [this, &at] (float left, float right, float cellWidth, float cellHeight, float centreY, bool compactKnobs) {
        const float pitch = (right - left) / (static_cast<float> (knobs.size() - 1) + 2.0f * 0.62f);
        for (std::size_t i = 0; i < knobs.size(); ++i)
        {
            knobs[i]->setCompact (compactKnobs);
            const float cx = left + (0.62f + static_cast<float> (i)) * pitch;
            knobs[i]->setBounds (at (juce::Rectangle<float> (std::min (cellWidth, pitch + 6.0f), cellHeight).withCentre ({ cx, centreY })));
        }
    };

    if (! triple)
    {
        // The 2 x 2 modifiers at the right edge, behind a hairline; the knobs left of it.
        const float right = w - 16.0f;
        const float buttonsX = right - 144.0f;
        const float buttonsY = h - 123.0f;
        row (16.0f, buttonsX - 13.0f, 0.87f * 110.0f, 0.87f * 125.0f, buttonsY + 50.5f, false);
        dividerArea = at ({ buttonsX - 13.0f, buttonsY, 1.0f, 101.0f });
        const std::array<juce::Rectangle<float>, 4> cells { { { 0.0f, 0.0f, 66.0f, 45.0f }, { 77.0f, 0.0f, 67.0f, 45.0f },
                                                                { 0.0f, 55.0f, 66.0f, 46.0f }, { 77.0f, 55.0f, 67.0f, 46.0f } } };
        for (std::size_t i = 0; i < modifiers.size(); ++i)
            modifiers[i]->setBounds (at (cells[i].translated (buttonsX, buttonsY)));
    }
    else
    {
        row (16.0f, w - 16.0f, 0.87f * 100.0f, 0.87f * 113.0f, h - 136.0f, true);
        dividerArea = {};
        const float gap = 9.0f, bw = (w - 32.0f - 3.0f * gap) / 4.0f;
        for (std::size_t i = 0; i < modifiers.size(); ++i)
            modifiers[i]->setBounds (at ({ 16.0f + static_cast<float> (i) * (bw + gap), h - 66.0f, bw, 45.0f }));
    }

    // Granular: POS SIZE DENS TUNE SPREAD fade in over the display's foot while the pointer
    // is over it (the display alone otherwise, as in the reference).
    const auto display = sourceDisplay.getBounds().toFloat();
    const float strip = 74.0f;
    const auto band = display.withTop (display.getBottom() - strip).reduced (12.0f, 6.0f);
    const float gw = band.getWidth() / 5.0f;
    for (std::size_t i = 0; i < granularKnobs.size(); ++i)
        granularKnobs[i]->setBounds (at ({ band.getX() + static_cast<float> (i) * gw, band.getY(), gw, band.getHeight() }));
    sourceDisplay.setBottomInset (0);
    sourceDisplay.setOverlayBand (granularShown ? 80 : 0);
}

} // namespace osp::plugin
