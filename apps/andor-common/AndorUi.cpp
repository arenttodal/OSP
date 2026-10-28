#include "AndorUi.h"

#include <cmath>

namespace osp::andorlab
{

namespace ui
{
    using plugin::fonts::Weight;

    juce::Font label (float height) { return plugin::fonts::make (height, Weight::medium, 0.06f); }
    juce::Font value (float height) { return plugin::fonts::make (height, Weight::regular, 0.0f); }
    juce::Font title (float height) { return plugin::fonts::make (height, Weight::semibold, 0.08f); }
    juce::Font logo (float height) { return plugin::fonts::make (height, Weight::displayBold, 0.02f); }

    void panel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& titleText)
    {
        design::draw::raised (g, r, 12.0f, design::colour::panelTop, design::colour::panelBottom);
        if (titleText.isNotEmpty())
        {
            g.setColour (design::colour::textSecondary);
            g.setFont (title (12.5f));
            g.drawText (titleText, r.reduced (16.0f, 9.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
        }
    }

    void well (juce::Graphics& g, juce::Rectangle<float> r)
    {
        design::draw::well (g, r, 10.0f, design::colour::wellA);
    }

    void logoBlock (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& product, const juce::String& subtitle)
    {
        g.setColour (design::colour::text);
        g.setFont (logo (30.0f));
        const auto andor = juce::String ("ANDOR");
        const float andorWidth = juce::GlyphArrangement::getStringWidth (logo (30.0f), andor);
        g.drawText (andor, r.withHeight (34.0f), juce::Justification::centredLeft);
        g.setFont (plugin::fonts::make (30.0f, Weight::displayLight, 0.02f));
        g.setColour (design::colour::accent);
        g.drawText ("/", r.withHeight (34.0f).withTrimmedLeft (andorWidth + 4.0f), juce::Justification::centredLeft);
        g.setColour (design::colour::text);
        g.drawText (product.toUpperCase(), r.withHeight (34.0f).withTrimmedLeft (andorWidth + 20.0f), juce::Justification::centredLeft);
        g.setColour (design::colour::textMicro);
        g.setFont (label (11.5f));
        g.drawText (subtitle, r.withTrimmedTop (36.0f).withHeight (14.0f), juce::Justification::centredLeft);
    }
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& n, bool rendered, juce::Colour arc)
    : name (n)
{
    dial.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    dial.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    dial.setRotaryParameters (plugin::OspLookAndFeel::rotaryStart, plugin::OspLookAndFeel::rotaryEnd, true);
    dial.getProperties().set ("arc", static_cast<juce::int64> (arc.getARGB()));
    if (rendered)
    {
        // The approved rendered ivory cap (tools/blender-assets), with a thin identity arc.
        dial.getProperties().set ("sprite", "macro-ivory");
        dial.getProperties().set ("thinArc", 2.2f);
    }
    else
        dial.getProperties().set ("mini", true);
    dial.setTitle (n);
    dial.setDoubleClickReturnValue (true, state.getParameterRange (parameterId).convertFrom0to1 (state.getParameter (parameterId)->getDefaultValue()));
    dial.onValueChange = [this] { repaint(); };
    addAndMakeVisible (dial);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, parameterId, dial);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (18);
    const int side = std::min (r.getWidth(), r.getHeight());
    dial.setBounds (r.withSizeKeepingCentre (side, side));
}

void Knob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (design::colour::textSecondary);
    g.setFont (ui::label (11.5f));
    g.drawText (name.toUpperCase(), r.removeFromTop (16.0f), juce::Justification::centred);
    g.setColour (design::colour::text);
    g.setFont (ui::value (12.5f));
    g.drawText (dial.getTextFromValue (dial.getValue()), r.removeFromBottom (16.0f), juce::Justification::centred);
}

//==============================================================================
Segmented::Segmented (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, juce::StringArray l) : labels (std::move (l))
{
    parameter = state.getParameter (parameterId);
    jassert (parameter != nullptr);
    attachment = std::make_unique<juce::ParameterAttachment> (*parameter, [this] (float v)
                                                              {
                                                                  index = juce::roundToInt (v);
                                                                  repaint();
                                                              });
    attachment->sendInitialUpdate();
    setTitle (parameter->getName (64));
}

Segmented::~Segmented() = default;

void Segmented::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    const float w = r.getWidth() / static_cast<float> (std::max (1, labels.size()));
    for (int i = 0; i < labels.size(); ++i)
    {
        const auto cell = juce::Rectangle<float> (r.getX() + w * i, r.getY(), w, r.getHeight()).reduced (2.0f, 1.0f);
        design::draw::button (g, cell, 7.0f, i == index, false, design::colour::accent);
        g.setColour (i == index ? design::colour::text : design::colour::textSecondary);
        g.setFont (ui::label (11.0f));
        g.drawText (labels[i], cell, juce::Justification::centred);
    }
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    const int i = juce::jlimit (0, labels.size() - 1, static_cast<int> (e.position.x / (static_cast<float> (getWidth()) / static_cast<float> (labels.size()))));
    attachment->setValueAsCompleteGesture (static_cast<float> (i));
}

//==============================================================================
Choice::Choice (juce::AudioProcessorValueTreeState& state, const juce::String& parameterId, const juce::String& n) : name (n)
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (parameterId)))
        combo.addItemList (p->choices, 1);
    combo.setTitle (n);
    addAndMakeVisible (combo);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (state, parameterId, combo);
}

void Choice::resized()
{
    combo.setBounds (getLocalBounds().withTrimmedTop (17));
}

void Choice::paint (juce::Graphics& g)
{
    g.setColour (design::colour::textSecondary);
    g.setFont (ui::label (11.0f));
    g.drawText (name.toUpperCase(), getLocalBounds().removeFromTop (15), juce::Justification::centredLeft);
}

//==============================================================================
Key::Key (juce::String t, std::function<void()> click) : onClick (std::move (click)), text (std::move (t))
{
    setTitle (text);
}

void Key::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    design::draw::button (g, r, 7.0f, active, isMouseOver(), design::colour::accent);
    g.setColour (isEnabled() ? design::colour::text : design::colour::textMicro);
    g.setFont (ui::label (11.0f));
    g.drawText (text, r, juce::Justification::centred);
}

void Key::mouseUp (const juce::MouseEvent& e)
{
    if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick)
        onClick();
}

//==============================================================================
void WaveformView::setOverview (std::vector<float> mn, std::vector<float> mx, std::int64_t f, double rate)
{
    mins = std::move (mn);
    maxs = std::move (mx);
    frames = f;
    sampleRate = rate > 0.0 ? rate : 48000.0;
    resetZoom();
}

void WaveformView::setVisibleRange (double startFrame, double endFrame)
{
    const double total = static_cast<double> (std::max<std::int64_t> (1, frames));
    viewStart = juce::jlimit (0.0, total - 1.0, startFrame);
    viewEnd = juce::jlimit (viewStart + 64.0, total, endFrame);
    repaint();
}

void WaveformView::resetZoom()
{
    viewStart = 0.0;
    viewEnd = static_cast<double> (std::max<std::int64_t> (1, frames));
    repaint();
}

double WaveformView::frameAt (float x) const noexcept
{
    const auto a = plotArea();
    return viewStart + (viewEnd - viewStart) * juce::jlimit (0.0, 1.0, static_cast<double> ((x - a.getX()) / a.getWidth()));
}

float WaveformView::xOf (double frame) const noexcept
{
    const auto a = plotArea();
    return a.getX() + a.getWidth() * static_cast<float> ((frame - viewStart) / std::max (1.0, viewEnd - viewStart));
}

void WaveformView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (frames <= 0)
        return;
    // Zoom around the pointer.
    const double centre = frameAt (e.position.x);
    const double factor = std::pow (0.85, wheel.deltaY * 4.0);
    setVisibleRange (centre - (centre - viewStart) * factor, centre + (viewEnd - centre) * factor);
}

void WaveformView::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    ui::well (g, bounds);
    const auto a = plotArea();
    g.setColour (design::colour::wellGrid);
    if (frames > 0)
    {
        // A second grid (finer when zoomed).
        const double seconds = (viewEnd - viewStart) / sampleRate;
        const double step = seconds > 20.0 ? 5.0 : seconds > 6.0 ? 1.0 : seconds > 2.0 ? 0.5 : 0.1;
        for (double t = std::ceil (viewStart / sampleRate / step) * step; t * sampleRate < viewEnd; t += step)
        {
            const float x = xOf (t * sampleRate);
            g.drawVerticalLine (juce::roundToInt (x), a.getY(), a.getBottom());
            g.setFont (ui::value (10.0f));
            g.setColour (design::colour::wellText.withAlpha (0.7f));
            g.drawText (juce::String (t, step < 1.0 ? 1 : 0) + "s", juce::Rectangle<float> (x + 3.0f, a.getBottom() - 13.0f, 40.0f, 12.0f), juce::Justification::left);
            g.setColour (design::colour::wellGrid);
        }
    }
    g.drawHorizontalLine (juce::roundToInt (a.getCentreY()), a.getX(), a.getRight());

    if (frames <= 0 || mins.empty())
    {
        g.setColour (design::colour::wellText);
        g.setFont (ui::label (14.0f));
        g.drawText (emptyText, a, juce::Justification::centred);
        paintOverlay (g, a);
        return;
    }
    const auto columns = static_cast<int> (a.getWidth());
    const double perColumn = (viewEnd - viewStart) / std::max (1, columns);
    juce::Path body;
    std::vector<float> top (static_cast<std::size_t> (columns)), bottom (static_cast<std::size_t> (columns));
    for (int c = 0; c < columns; ++c)
    {
        const double f0 = viewStart + perColumn * c, f1 = f0 + perColumn;
        auto b0 = static_cast<std::size_t> (f0 / static_cast<double> (frames) * static_cast<double> (mins.size()));
        auto b1 = static_cast<std::size_t> (f1 / static_cast<double> (frames) * static_cast<double> (mins.size()));
        b0 = std::min (b0, mins.size() - 1);
        b1 = std::clamp (b1, b0 + 1, mins.size());
        float lo = 0.0f, hi = 0.0f;
        for (auto b = b0; b < b1; ++b)
        {
            lo = std::min (lo, mins[b]);
            hi = std::max (hi, maxs[b]);
        }
        top[static_cast<std::size_t> (c)] = a.getCentreY() - hi * a.getHeight() * 0.48f;
        bottom[static_cast<std::size_t> (c)] = a.getCentreY() - lo * a.getHeight() * 0.48f;
    }
    body.startNewSubPath (a.getX(), top[0]);
    for (int c = 1; c < columns; ++c)
        body.lineTo (a.getX() + static_cast<float> (c), top[static_cast<std::size_t> (c)]);
    for (int c = columns - 1; c >= 0; --c)
        body.lineTo (a.getX() + static_cast<float> (c), bottom[static_cast<std::size_t> (c)]);
    body.closeSubPath();
    g.setColour (plugin::palette::wave.withAlpha (0.55f));
    g.fillPath (body);
    g.setColour (plugin::palette::wave.withAlpha (0.85f));
    g.strokePath (body, juce::PathStrokeType (0.8f));
    paintOverlay (g, a);
}

//==============================================================================
void KeyboardMap::setRange (int lowKey, int highKey)
{
    low = juce::jlimit (0, 120, lowKey);
    while (isBlack (low))
        --low;
    high = juce::jlimit (low + 12, 127, highKey);
    while (isBlack (high) && high < 127)
        ++high;
    repaint();
}

void KeyboardMap::setSounding (const std::array<std::uint32_t, 4>& mask)
{
    if (mask != sounding)
    {
        sounding = mask;
        repaint();
    }
}

int KeyboardMap::whiteIndex (int key) const noexcept
{
    int n = 0;
    for (int k = low; k < key; ++k)
        n += isBlack (k) ? 0 : 1;
    return n;
}

int KeyboardMap::numWhite() const noexcept
{
    return whiteIndex (high + 1);
}

juce::Rectangle<float> KeyboardMap::keyBounds (int key) const
{
    const auto r = getLocalBounds().toFloat();
    const float w = r.getWidth() / static_cast<float> (std::max (1, numWhite()));
    if (! isBlack (key))
        return { r.getX() + w * static_cast<float> (whiteIndex (key)), r.getY(), w, r.getHeight() };
    return { r.getX() + w * static_cast<float> (whiteIndex (key)) - w * 0.32f, r.getY(), w * 0.64f, r.getHeight() * 0.6f };
}

int KeyboardMap::keyAt (juce::Point<float> p) const
{
    for (int k = low; k <= high; ++k)
        if (isBlack (k) && keyBounds (k).contains (p))
            return k;
    for (int k = low; k <= high; ++k)
        if (! isBlack (k) && keyBounds (k).contains (p))
            return k;
    return -1;
}

void KeyboardMap::paint (juce::Graphics& g)
{
    auto drawKey = [&] (int k)
    {
        const auto r = keyBounds (k).reduced (0.5f, 0.0f);
        const auto look = lookOf ? lookOf (k) : KeyLook {};
        const bool black = isBlack (k);
        const bool on = (sounding[static_cast<std::size_t> (k >> 5)] >> (k & 31)) & 1u;
        juce::Colour base = black ? plugin::palette::ebony : plugin::palette::ivory;
        if (! look.fill.isTransparent())
            base = black ? look.fill.darker (0.45f) : look.fill;
        g.setColour (base);
        g.fillRoundedRectangle (r, 2.5f);
        if (look.hatched)
        {
            g.setColour ((black ? juce::Colours::white : juce::Colours::black).withAlpha (0.08f));
            for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 5.0f)
                g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 1.0f);
        }
        if (on)
        {
            g.setColour (design::colour::accent.withAlpha (0.85f));
            g.fillRoundedRectangle (r.withTrimmedTop (r.getHeight() * 0.62f).reduced (2.0f, 3.0f), 2.0f);
        }
        if (k == selected)
        {
            g.setColour (design::colour::text);
            g.drawRoundedRectangle (r.reduced (1.0f), 2.5f, 1.6f);
        }
        g.setColour (design::colour::keyLine);
        if (! black)
            g.drawRoundedRectangle (r, 2.5f, 0.8f);
        if (look.badge.isNotEmpty())
        {
            g.setColour (black ? juce::Colours::white.withAlpha (0.9f) : design::colour::text);
            g.setFont (ui::label (9.5f));
            g.drawText (look.badge, r.withTrimmedTop (r.getHeight() * (black ? 0.55f : 0.72f)).withTrimmedBottom (2.0f), juce::Justification::centredBottom);
        }
        if (! black && k % 12 == 0)
        {
            g.setColour (design::colour::textMicro);
            g.setFont (ui::value (9.0f));
            g.drawText ("C" + juce::String (k / 12 - 1), r.withHeight (14.0f).translated (0.0f, 2.0f), juce::Justification::centred);
        }
    };
    for (int k = low; k <= high; ++k)
        if (! isBlack (k))
            drawKey (k);
    for (int k = low; k <= high; ++k)
        if (isBlack (k))
            drawKey (k);
}

void KeyboardMap::mouseDown (const juce::MouseEvent& e)
{
    pressed = keyAt (e.position);
    if (pressed >= 0)
    {
        if (onSelect)
            onSelect (pressed);
        if (onNote)
            onNote (pressed, true);
    }
}

void KeyboardMap::mouseDrag (const juce::MouseEvent& e)
{
    const int k = keyAt (e.position);
    if (k != pressed)
    {
        if (pressed >= 0 && onNote)
            onNote (pressed, false);
        pressed = k;
        if (pressed >= 0 && onNote)
            onNote (pressed, true);
    }
}

void KeyboardMap::mouseUp (const juce::MouseEvent&)
{
    if (pressed >= 0 && onNote)
        onNote (pressed, false);
    pressed = -1;
}

//==============================================================================
ScaledContent::ScaledContent (juce::Component& content, int designWidth, int designHeight) : inner (content), w (designWidth), h (designHeight)
{
    addAndMakeVisible (inner);
    inner.setBounds (0, 0, w, h);
}

void ScaledContent::resized()
{
    const float scale = std::min (static_cast<float> (getWidth()) / static_cast<float> (w), static_cast<float> (getHeight()) / static_cast<float> (h));
    const float x = 0.5f * (static_cast<float> (getWidth()) - w * scale);
    const float y = 0.5f * (static_cast<float> (getHeight()) - h * scale);
    inner.setTransform (juce::AffineTransform::scale (scale).translated (x, y));
}

} // namespace osp::andorlab
