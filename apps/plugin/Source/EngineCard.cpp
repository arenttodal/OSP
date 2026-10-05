#include "EngineCard.h"

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
                // Two chain links on a diagonal.
                const float w = s * 0.46f, h = s * 0.26f;
                juce::Path a, b;
                a.addRoundedRectangle (-w * 0.5f, -h * 0.5f, w, h, h * 0.5f);
                b = a;
                a.applyTransform (juce::AffineTransform::translation (-s * 0.15f, 0.0f).rotated (-0.785f).translated (c.x, c.y));
                b.applyTransform (juce::AffineTransform::translation (s * 0.15f, 0.0f).rotated (-0.785f).translated (c.x, c.y));
                g.strokePath (a, line);
                g.strokePath (b, line);
                return;
            }
            case Kind::reverse:
            {
                // Playback, backwards: two heads pointing left.
                for (float dx : { -0.18f, 0.12f })
                {
                    juce::Path t;
                    t.addTriangle (c.x + (dx - 0.14f) * s, c.y, c.x + (dx + 0.14f) * s, c.y - 0.2f * s, c.x + (dx + 0.14f) * s, c.y + 0.2f * s);
                    g.strokePath (t, juce::PathStrokeType (stroke, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
                }
                return;
            }
            case Kind::loop:
            {
                const float radius = s * 0.28f;
                p.addCentredArc (c.x, c.y, radius, radius, 0.0f, 0.5f, 5.6f, true);
                g.strokePath (p, line);
                const auto tip = c + juce::Point<float> (std::sin (0.5f), -std::cos (0.5f)) * radius;
                juce::Path head;
                head.startNewSubPath (tip.x - s * 0.13f, tip.y - s * 0.07f);
                head.lineTo (tip.x, tip.y);
                head.lineTo (tip.x - s * 0.02f, tip.y + s * 0.14f);
                g.strokePath (head, line);
                return;
            }
            case Kind::follow:
            {
                // An amplitude contour: a quick rise, a long fall.
                p.startNewSubPath (r.getX() + 0.12f * s, r.getY() + 0.72f * s);
                p.quadraticTo (r.getX() + 0.24f * s, r.getY() + 0.22f * s, r.getX() + 0.36f * s, r.getY() + 0.26f * s);
                p.cubicTo (r.getX() + 0.52f * s, r.getY() + 0.3f * s, r.getX() + 0.62f * s, r.getY() + 0.66f * s, r.getX() + 0.88f * s, r.getY() + 0.72f * s);
                g.strokePath (p, line);
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

    /** A time series' value at t seconds (linear), or `fallback` when it is empty. */
    double seriesAt (const TimeSeries& series, double seconds, double fallback)
    {
        if (series.values.empty() || series.hopSeconds <= 0.0)
            return fallback;
        const double x = std::clamp (seconds / series.hopSeconds, 0.0, static_cast<double> (series.values.size() - 1));
        const auto i = static_cast<std::size_t> (x);
        const auto j = std::min (i + 1, series.values.size() - 1);
        return series.values[i] + (series.values[j] - series.values[i]) * (x - static_cast<double> (i));
    }
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
    return getLocalBounds().toFloat().reduced (10.0f, 0.0f).withTrimmedTop (20.0f).withTrimmedBottom (10.0f + static_cast<float> (bottomInset));
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
    const auto identity = palette::layer (layer);
    if (hasWave && view.granular)
    {
        // Where grains may come from (SPREAD, half the length either side at 100 %) and POS.
        const float x = plot.getX() + view.position * plot.getWidth();
        const float half = (view.spread * 0.5f + 0.004f) * plot.getWidth();
        g.setColour (identity.withAlpha (0.10f));
        g.fillRect (juce::Rectangle<float> (x - half, plot.getY(), 2.0f * half, plot.getHeight()).getIntersection (plot));
        g.setColour (identity.withAlpha (0.75f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        // The grains playing now: a faint read line and a dot on its own lane each.
        for (const auto& grain : grains)
        {
            const float gx = plot.getX() + grain.position * plot.getWidth();
            const float level = std::clamp (grain.level, 0.0f, 1.0f);
            g.setColour (identity.brighter (0.4f).withAlpha (0.08f + 0.3f * level));
            g.drawVerticalLine (juce::roundToInt (gx), plot.getY(), plot.getBottom());
            const float gy = plot.getY() + 6.0f + grain.lane * (plot.getHeight() - 12.0f);
            const float r = 1.8f + 3.0f * level;
            g.setColour (graphite.withAlpha (0.6f));
            g.fillEllipse (gx - r - 1.2f, gy - r - 1.2f, 2.0f * r + 2.4f, 2.0f * r + 2.4f);
            g.setColour (identity.brighter (0.5f).withAlpha (0.4f + 0.6f * level));
            g.fillEllipse (gx - r, gy - r, 2.0f * r, 2.0f * r);
        }
    }
    if (hasWave)
    {
        // One Shot read heads: where every playing note reads now, as bright as it is loud.
        for (const auto& head : heads)
        {
            const float x = plot.getX() + head.position * plot.getWidth();
            const float level = 0.3f + 0.7f * std::clamp (head.level, 0.0f, 1.0f);
            g.setColour (accent.withAlpha (0.16f * level));
            g.fillRect (juce::Rectangle<float> (x - 3.0f, plot.getY(), 6.0f, plot.getHeight()));
            g.setColour (accent.withAlpha (0.95f * level));
            g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getY(), 1.5f, plot.getHeight()));
        }
    }
    if (overlayBand > 0)
    {
        // The granular controls float here: the waveform shows through, quietened.
        const auto band = getLocalBounds().toFloat().removeFromBottom (static_cast<float> (overlayBand));
        g.setGradientFill (juce::ColourGradient (graphite.withAlpha (0.0f), 0.0f, band.getY() - 10.0f, graphite.withAlpha (0.82f), 0.0f,
                                                 band.getY() + 12.0f, false));
        g.fillRect (band.withTop (band.getY() - 10.0f));
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
        g.setFont (fonts::make (15.0f, fonts::Weight::semibold, 0.08f));
        g.drawText (dropLabel, r, juce::Justification::centred, false);
    }
}

void SourceDisplay::paintStatic (juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (graphite);
    g.fillRoundedRectangle (bounds, 8.0f);
    // Recessed: a faint shade under the top edge.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.22f), 0.0f, bounds.getY(),
                                             juce::Colours::transparentBlack, 0.0f, bounds.getY() + 10.0f, false));
    g.fillRoundedRectangle (bounds.withHeight (12.0f), 8.0f);

    const auto plot = plotArea();
    const bool hasWave = instrument != nullptr && ! instrument->peakMax.empty();
    if (loading || ! hasWave)
    {
        if (loading)
        {
            g.setColour (displayText);
            g.setFont (fonts::make (13.0f, fonts::Weight::medium, 0.1f));
            g.drawText (juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"), bounds, juce::Justification::centred, false);
        }
        return;
    }

    const double duration = std::max (1.0e-3, instrument->durationSeconds);
    // Quiet time grid.
    {
        const double step = gridStep (duration);
        g.setFont (fonts::make (9.5f));
        for (double t = step; t < duration - 0.25 * step; t += step)
        {
            const float x = plot.getX() + static_cast<float> (t / duration) * plot.getWidth();
            g.setColour (displayLine.withAlpha (0.55f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
            if (getWidth() > 240)
            {
                g.setColour (displayText.withAlpha (0.6f));
                g.drawText (secondsText (t, step), juce::Rectangle<float> (x + 3.0f, bounds.getY() + 4.0f, 50.0f, 12.0f),
                            juce::Justification::centredLeft, false);
            }
        }
        g.setColour (displayLine.withAlpha (0.8f));
        g.drawHorizontalLine (juce::roundToInt (plot.getCentreY()), plot.getX(), plot.getRight());
    }

    const auto identity = palette::layer (layer);
    const auto& lo = instrument->peakMin;
    const auto& hi = instrument->peakMax;
    const float mid = plot.getCentreY();
    float maxAbs = 1.0e-4f;   // scaled to the recording's own peak so quiet sources stay visible
    for (std::size_t i = 0; i < hi.size(); ++i)
        maxAbs = std::max ({ maxAbs, hi[i], -lo[i] });
    const float yScale = plot.getHeight() * 0.47f / maxAbs;
    const int x0 = static_cast<int> (plot.getX()), width = static_cast<int> (plot.getWidth());

    // The loudness contour as a faint ghost behind the waveform.
    const auto& envelope = instrument->analysis.envelope;
    if (! envelope.rmsDb.values.empty())
    {
        juce::Path ghost;
        const float peakDb = static_cast<float> (envelope.maxRmsDbfs);
        const float ghostScale = plot.getHeight() * 0.47f;
        ghost.startNewSubPath (plot.getX(), mid);
        for (int x = 0; x <= width; x += 3)
        {
            const double t = static_cast<double> (x) / width * duration;
            const float a = std::pow (10.0f, (static_cast<float> (seriesAt (envelope.rmsDb, t, -120.0)) - peakDb) / 20.0f);
            ghost.lineTo (plot.getX() + static_cast<float> (x), mid - a * ghostScale);
        }
        for (int x = width; x >= 0; x -= 3)
        {
            const double t = static_cast<double> (x) / width * duration;
            const float a = std::pow (10.0f, (static_cast<float> (seriesAt (envelope.rmsDb, t, -120.0)) - peakDb) / 20.0f);
            ghost.lineTo (plot.getX() + static_cast<float> (x), mid + a * ghostScale);
        }
        ghost.closeSubPath();
        g.setColour (identity.withAlpha (0.10f));
        g.fillPath (ghost);
        g.setColour (identity.withAlpha (0.22f));
        g.strokePath (ghost, juce::PathStrokeType (0.8f));
    }

    // The waveform in its spectral colours: each moment coloured by its brightness
    // (spectral centroid, 200 Hz .. 6 kHz: warm .. cool), leaning to the layer's identity.
    const auto& centroid = instrument->analysis.spectral.centroidHz;
    const float lowLog = std::log2 (200.0f), highLog = std::log2 (6000.0f);
    for (int x = 0; x < width; ++x)
    {
        const auto b = std::min (static_cast<std::size_t> (static_cast<double> (x) / width * static_cast<double> (hi.size())), hi.size() - 1);
        const float top = mid - hi[b] * yScale;
        const float bottom = mid - lo[b] * yScale;
        const double t = static_cast<double> (x) / width * duration;
        const float hz = static_cast<float> (seriesAt (centroid, t, 800.0));
        const float s = (std::log2 (std::max (hz, 20.0f)) - lowLog) / (highLog - lowLog);
        auto colour = spectrum (s).interpolatedWith (identity, 0.3f);
        if (! focused)
            colour = colour.withMultipliedSaturation (0.55f);
        g.setColour (colour.withAlpha (0.55f));
        g.drawVerticalLine (x0 + x, top, std::max (top + 1.0f, bottom));
        // A brighter core: colour as light under frosted glass.
        const float core = 0.45f * (bottom - top);
        g.setColour (colour.brighter (0.35f).withAlpha (0.85f));
        g.drawVerticalLine (x0 + x, mid - 0.5f * core + (top + bottom - 2.0f * mid) * 0.5f, mid + 0.5f * core + (top + bottom - 2.0f * mid) * 0.5f);
    }

    // The loop region (One Shot + LOOP): a slim bracket under the waveform.
    if (! view.granular && view.loop && instrument->model != nullptr && instrument->model->original.continuation.canSustain
        && instrument->model->original.source != nullptr)
    {
        const auto& cont = instrument->model->original.continuation;
        const double frames = std::max<double> (1.0, static_cast<double> (instrument->model->original.source->numFrames()));
        const float a = plot.getX() + static_cast<float> (cont.sustainStartFrame / frames) * plot.getWidth();
        const float b = plot.getX() + static_cast<float> (cont.sustainEndFrame / frames) * plot.getWidth();
        const float y = plot.getBottom() + 4.0f;
        g.setColour (displayText.withAlpha (0.7f));
        g.drawLine (a, y, b, y, 1.0f);
        g.drawLine (a, y - 3.0f, a, y + 1.0f, 1.0f);
        g.drawLine (b, y - 3.0f, b, y + 1.0f, 1.0f);
    }

    // START: where notes begin (forwards: before it is skipped; REVERSE: from it back).
    if (! view.granular)
    {
        const float x = plot.getX() + static_cast<float> (startSeconds() / duration) * plot.getWidth();
        g.setColour (graphite.withAlpha (0.5f));
        if (view.reverse)
            g.fillRect (juce::Rectangle<float> (x, plot.getY(), plot.getRight() - x, plot.getHeight()));
        else
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), x - plot.getX(), plot.getHeight()));
        g.setColour (accent.withAlpha (0.9f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY() - 2.0f, plot.getBottom() + 2.0f);
        juce::Path cap;
        const float d = view.reverse ? -1.0f : 1.0f;
        cap.addTriangle (x, plot.getY() - 6.0f, x + d * 6.0f, plot.getY() - 3.0f, x, plot.getY());
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
    : processor (p), caption (c)
{
    parameter = processor.parameters.getParameter (OspAudioProcessor::layerParameterId (layer, control));
    dial = std::make_unique<Dial> (processor, layer, control, control == "tune");
    dial->setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
    dial->getProperties().set ("arc", static_cast<juce::int64> (palette::layer (layer).getARGB()));
    dial->getProperties().set ("bipolar", control == "tune" || control == "pan");
    dial->setTitle ("Layer " + OspAudioProcessor::layerName (layer) + " " + caption);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters,
                                                                                           OspAudioProcessor::layerParameterId (layer, control), *dial);
    if (parameter != nullptr)
    {
        dial->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        dial->setTooltip (parameter->getName (64) + (control == "tune" ? juce::String (" (Alt-drag: fine)") : juce::String()));
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

void LayerKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (compact ? 14 : 16);
    r.removeFromBottom (compact ? 15 : 17);
    const int side = std::min (r.getWidth(), r.getHeight());
    dial->setBounds (r.withSizeKeepingCentre (side, side));
}

void LayerKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (palette::textDim);
    g.setFont (fonts::label (compact ? 10.0f : 10.5f));
    g.drawText (caption, r.removeFromTop (compact ? 14 : 16), juce::Justification::centred, false);
    g.setColour (palette::text);
    g.setFont (fonts::make (compact ? 12.0f : 13.0f, fonts::Weight::medium));
    const auto value = parameter != nullptr ? parameter->getCurrentValueAsText() : juce::String();
    g.drawText (value, r.removeFromBottom (compact ? 15 : 17), juce::Justification::centred, false);
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
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    // On: sunk into the card with the accent in the icon and a fine rim (colour as a mark,
    // not a surface); off: a raised key.
    const bool active = on && ! suppressed;
    if (active)
    {
        g.setColour (palette::recessed.interpolatedWith (onColour, 0.12f));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (onColour.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
    else
        OspLookAndFeel::drawCard (g, r, 5.0f, false, isMouseOver() && ! suppressed);
    const auto colour = active ? onColour.darker (0.15f) : palette::text.withAlpha (suppressed ? 0.25f : 0.7f);
    icons::draw (g, icon, r.withSizeKeepingCentre (std::min (r.getHeight() - 8.0f, 22.0f), std::min (r.getHeight() - 8.0f, 22.0f)), colour, 1.5f);
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
    const auto r = getLocalBounds().toFloat().reduced (0.5f);
    OspLookAndFeel::drawCard (g, r, 5.0f, false, isMouseOver());
    g.setColour (palette::text);
    g.setFont (fonts::make (12.5f, fonts::Weight::medium));
    auto text = r.reduced (10.0f, 0.0f);
    icons::draw (g, icons::Kind::chevronDown, text.removeFromRight (12.0f), palette::text.withAlpha (0.7f), 1.4f);
    g.drawText (current == 1 ? "Granular" : "One Shot", text, juce::Justification::centredLeft, false);
}

//==============================================================================
void EngineCard::MenuDots::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    if (highlighted)
    {
        g.setColour (palette::recessed);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }
    icons::draw (g, icons::Kind::dots, getLocalBounds().toFloat().reduced (4.0f), palette::text.withAlpha (0.8f));
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

    const std::array<std::pair<const char*, const char*>, 4> controls { { { "start", "START" }, { "tune", "TUNE" }, { "pan", "PAN" }, { "level", "LEVEL" } } };
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
    modifiers[0]->setTooltip ("Link: START, TUNE, PAN and LEVEL move together with the other linked layers");
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

void EngineCard::updateMode()
{
    const bool granular = mode != nullptr && mode->mode() == 1;
    for (auto& k : granularKnobs)
        k->setVisible (granular);
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
        fileText = instrument != nullptr ? juce::String::fromUTF8 (instrument->filename.c_str()) : juce::String (loading ? "Loading" : "");
        repaint (header);
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
        repaint (header);
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
    using namespace palette;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    juce::Path shape;
    shape.addRoundedRectangle (r, 10.0f);
    juce::DropShadow (juce::Colour (0x141e1c18), 3, { 0, 1 }).drawForPath (g, shape);
    g.setColour (raised);
    g.fillPath (shape);
    g.setColour (hairline);
    g.strokePath (shape, juce::PathStrokeType (1.0f));

    // Header: the layer's letter (its colour when edited, quieter otherwise), root, file.
    auto row = header.toFloat();
    const auto badge = row.removeFromLeft (row.getHeight()).reduced (1.0f);
    const auto identity = palette::layer (layerIndex);
    g.setColour (focused ? identity : identity.withMultipliedSaturation (0.45f).interpolatedWith (raised, 0.35f));
    g.fillRoundedRectangle (badge, 5.0f);
    g.setColour (raised);
    g.setFont (fonts::make (badge.getHeight() * 0.62f, fonts::Weight::semibold));
    g.drawText (OspAudioProcessor::layerName (layerIndex), badge, juce::Justification::centred, false);
    row.removeFromLeft (10.0f);
    if (rootText.isNotEmpty())
    {
        g.setColour (text);
        const auto rootFont = fonts::make (density == EngineLayoutDensity::triple ? 15.0f : 17.0f, fonts::Weight::semibold);
        g.setFont (rootFont);
        const float w = juce::GlyphArrangement::getStringWidth (rootFont, rootText) + 4.0f;
        g.drawText (rootText, row.removeFromLeft (w), juce::Justification::centredLeft, false);
        row.removeFromLeft (8.0f);
    }
    g.setColour (textDim);
    g.setFont (fonts::make (density == EngineLayoutDensity::triple ? 12.0f : 13.0f, fonts::Weight::medium));
    g.drawText (fileText, row, juce::Justification::centredLeft, true);

    // Hairline between the knobs and the modifiers (Hero / Dual).
    if (density != EngineLayoutDensity::triple && modifiers[0] != nullptr)
    {
        const float x = static_cast<float> (modifiers[0]->getX()) - 9.0f;
        g.setColour (hairline);
        g.drawVerticalLine (juce::roundToInt (x), static_cast<float> (modifiers[0]->getY()) + 2.0f, static_cast<float> (modifiers[3]->getBottom()) - 2.0f);
    }
}

void EngineCard::resized()
{
    const bool triple = density == EngineLayoutDensity::triple;
    const bool hero = density == EngineLayoutDensity::hero;
    auto area = getLocalBounds().reduced (hero ? 16 : 12, 12);

    // Header: letter, root and file on the left; mode and menu on the right.
    header = area.removeFromTop (26);
    {
        auto right = header;
        menuButton.setBounds (right.removeFromRight (24));
        right.removeFromRight (6);
        if (mode != nullptr)
            mode->setBounds (right.removeFromRight (triple ? 96 : 108).reduced (0, 1));
        header = header.withTrimmedRight (header.getRight() - (mode != nullptr ? mode->getX() - 8 : right.getRight()));
    }
    area.removeFromTop (10);

    // Controls at the bottom: Hero / Dual one row (knobs, then a 2x2 of modifiers);
    // Triple a row of knobs and a row of modifiers.
    const int knobHeight = hero ? 86 : (triple ? 72 : 80);
    const int modifierRow = triple ? 30 : 0;
    auto controls = area.removeFromBottom (knobHeight + modifierRow + (triple ? 6 : 0));
    area.removeFromBottom (10);
    sourceDisplay.setBounds (area);

    juce::Rectangle<int> knobRow;
    if (triple)
    {
        auto mods = controls.removeFromBottom (modifierRow);
        controls.removeFromBottom (6);
        knobRow = controls;
        const int w = (mods.getWidth() - 3 * 6) / 4;
        for (auto& m : modifiers)
        {
            m->setBounds (mods.removeFromLeft (w));
            mods.removeFromLeft (6);
        }
    }
    else
    {
        const int cell = hero ? 56 : 46, gap = 6;
        auto grid = controls.removeFromRight (2 * cell + gap);
        grid = grid.withSizeKeepingCentre (grid.getWidth(), std::min (grid.getHeight(), 2 * (hero ? 34 : 30) + gap));
        const int h = (grid.getHeight() - gap) / 2;
        for (int i = 0; i < 4; ++i)
            modifiers[static_cast<std::size_t> (i)]->setBounds (grid.getX() + (i % 2) * (cell + gap), grid.getY() + (i / 2) * (h + gap), cell, h);
        controls.removeFromRight (18);
        knobRow = controls;
    }
    const int w = knobRow.getWidth() / 4;
    for (auto& k : knobs)
    {
        k->setCompact (! hero);
        k->setBounds (knobRow.removeFromLeft (w).withSizeKeepingCentre (std::min (w, hero ? 120 : 92), knobRow.getHeight()));
    }

    // Granular: POS SIZE DENS TUNE SPREAD along the bottom of the display - below the
    // waveform when there is room, floating over it when the display is short.
    const int strip = hero ? 62 : (triple ? 54 : 58);
    const bool granular = granularKnobs[0]->isVisible();
    const bool roomy = sourceDisplay.getHeight() >= 2 * strip + 40;
    sourceDisplay.setBottomInset (granular && roomy ? strip - 4 : 0);
    sourceDisplay.setOverlayBand (granular && ! roomy ? strip : 0);
    auto g = sourceDisplay.getBounds().removeFromBottom (strip).reduced (triple ? 4 : 10, 4);
    const int gw = g.getWidth() / 5;
    for (auto& k : granularKnobs)
        k->setBounds (g.removeFromLeft (gw).withSizeKeepingCentre (std::min (gw, 76), g.getHeight()));
}

} // namespace osp::plugin
