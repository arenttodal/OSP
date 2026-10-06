#include "MainSections.h"

#include "Design.h"
#include "EngineCard.h"

#include "engine/InstrumentEngine.h"

#include <cmath>

namespace osp::plugin
{

//==============================================================================
TriangleMix::TriangleMix (juce::AudioProcessorValueTreeState& state)
    : xAttachment (*state.getParameter ("mix.x"), [this] (float v) { x = v; repaint(); }),
      yAttachment (*state.getParameter ("mix.y"), [this] (float v) { y = v; repaint(); })
{
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
    setTooltip ("Three-layer mix: drag between A, B and C (double-click: all three equal)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTitle ("Layer mix");
}

std::array<juce::Point<float>, 3> TriangleMix::corners() const
{
    // Room for the corner letters, which grow with the triangle.
    auto r = getLocalBounds().toFloat().reduced (std::max (9.0f, 0.075f * static_cast<float> (getWidth())),
                                                  std::max (7.0f, 0.06f * static_cast<float> (getHeight())));
    const float w = std::min (r.getWidth(), r.getHeight() / 0.866f);
    const float h = w * 0.866f;
    r = r.withSizeKeepingCentre (w, h);
    return { juce::Point<float> (r.getX(), r.getBottom()), juce::Point<float> (r.getCentreX(), r.getY()),
             juce::Point<float> (r.getRight(), r.getBottom()) };
}

void TriangleMix::moveTo (juce::Point<float> where)
{
    const auto c = corners();
    const float w = c[2].x - c[0].x, h = c[0].y - c[1].y;
    // Into the triangle (the same clipping the engine applies), then back to x/y.
    const auto share = InstrumentEngine::triangleShares ((where.x - c[0].x) / std::max (1.0f, w), (c[0].y - where.y) / std::max (1.0f, h));
    xAttachment.setValueAsPartOfGesture (static_cast<float> (0.5 * share[1] + share[2]));
    yAttachment.setValueAsPartOfGesture (static_cast<float> (share[1]));
}

void TriangleMix::mouseDown (const juce::MouseEvent& e)
{
    pressed = true;
    if (onClick != nullptr)
        return;   // a click opens; only a drag moves
    dragging = true;
    xAttachment.beginGesture();
    yAttachment.beginGesture();
    moveTo (e.position);
}

void TriangleMix::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging && pressed && onClick != nullptr && e.getDistanceFromDragStart() > 3)
    {
        dragging = true;
        xAttachment.beginGesture();
        yAttachment.beginGesture();
    }
    if (dragging)
        moveTo (e.position);
}

void TriangleMix::mouseUp (const juce::MouseEvent& e)
{
    const bool wasPressed = std::exchange (pressed, false);
    if (! dragging)
    {
        if (wasPressed && onClick != nullptr && ! e.mouseWasDraggedSinceMouseDown() && e.getNumberOfClicks() < 2)
            onClick();
        return;
    }
    dragging = false;
    xAttachment.endGesture();
    yAttachment.endGesture();
}

void TriangleMix::mouseDoubleClick (const juce::MouseEvent&)
{
    xAttachment.setValueAsCompleteGesture (0.5f);
    yAttachment.setValueAsCompleteGesture (1.0f / 3.0f);
}

void TriangleMix::paint (juce::Graphics& g)
{
    using namespace palette;
    const auto c = corners();
    juce::Path shape;
    shape.addTriangle (c[0], c[1], c[2]);
    g.setColour (recessed);
    g.fillPath (shape);
    g.setColour (hairline.darker (0.08f));
    g.strokePath (shape, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Everything grows with the triangle (64 px in the mix band, ~300 px in its popup).
    const float k = std::clamp ((c[2].x - c[0].x) / 46.0f, 1.0f, 2.4f);
    const auto share = InstrumentEngine::triangleShares (x, y);
    const auto node = juce::Point<float> (c[0].x + x * (c[2].x - c[0].x), c[0].y - y * (c[0].y - c[1].y));
    // Threads to the corners, as strong as each layer's share; the node takes their colours.
    juce::Colour mix = layer (0).withAlpha (0.0f);
    for (std::size_t i = 0; i < 3; ++i)
    {
        g.setColour (layer (static_cast<int> (i)).withAlpha (0.15f + 0.7f * static_cast<float> (share[i])));
        g.drawLine ({ node, c[i] }, 1.2f * std::sqrt (k));
        g.setColour (layer (static_cast<int> (i)));
        g.fillEllipse (c[i].x - 2.5f * k, c[i].y - 2.5f * k, 5.0f * k, 5.0f * k);
    }
    mix = layer (0).interpolatedWith (layer (1), static_cast<float> (share[1] / std::max (1.0e-6, share[0] + share[1])));
    mix = mix.interpolatedWith (layer (2), static_cast<float> (share[2]));
    juce::Path disc;
    disc.addEllipse (node.x - 6.0f * k, node.y - 6.0f * k, 12.0f * k, 12.0f * k);
    juce::DropShadow (juce::Colour (0x331e1c18), 3, { 0, 1 }).drawForPath (g, disc);
    g.setColour (raised);
    g.fillPath (disc);
    g.setColour (mix);
    g.strokePath (disc, juce::PathStrokeType (1.4f));
    g.setColour (accent);
    g.fillEllipse (node.x - 2.0f * k, node.y - 2.0f * k, 4.0f * k, 4.0f * k);

    g.setFont (fonts::make (10.0f * std::sqrt (k), fonts::Weight::semibold));
    g.setColour (textDim);
    const float t = 8.0f * std::sqrt (k), gap = 2.0f * k;
    g.drawText ("A", juce::Rectangle<float> (c[0].x - t - gap, c[0].y - t * 0.5f, t, t + 2.0f), juce::Justification::centredRight, false);
    g.drawText ("B", juce::Rectangle<float> (c[1].x + gap + 2.0f, c[1].y - t * 0.75f, t, t), juce::Justification::centredLeft, false);
    g.drawText ("C", juce::Rectangle<float> (c[2].x + gap, c[2].y - t * 0.5f, t, t + 2.0f), juce::Justification::centredLeft, false);
}

//==============================================================================
ReimaginedTrack::ReimaginedTrack (OspAudioProcessor& p) : processor (p)
{
    for (int l = 0; l < 3; ++l)
        if (auto* param = processor.parameters.getParameter (OspAudioProcessor::reimaginedParameterId (l)))
        {
            attachments[static_cast<std::size_t> (l)] = std::make_unique<juce::ParameterAttachment> (*param, [this, l] (float v) {
                values[static_cast<std::size_t> (l)] = v;
                repaint();
            });
            attachments[static_cast<std::size_t> (l)]->sendInitialUpdate();
        }
    setTooltip ("Original <-> Reimagined, for each layer: from the recording as it is to a transformed version of it");
    setTitle ("Original / Reimagined");
}

void ReimaginedTrack::setLayers (const std::array<bool, 3>& occupied)
{
    shown = occupied;
    if (! shown[0] && ! shown[1] && ! shown[2])
        shown[0] = true;   // with no sound, the instrument's own amount
    repaint();
}

juce::Range<float> ReimaginedTrack::travel() const
{
    return { 11.0f, static_cast<float> (getWidth()) - 11.0f };
}

float ReimaginedTrack::xFor (int layer) const
{
    const auto t = travel();
    return t.getStart() + t.getLength() * 0.01f * values[static_cast<std::size_t> (layer)];
}

int ReimaginedTrack::thumbAt (float x) const
{
    // The nearest thumb; on a tie the edited layer's.
    int best = -1;
    float distance = 1.0e9f;
    for (int l = 0; l < 3; ++l)
        if (shown[static_cast<std::size_t> (l)])
        {
            const float d = std::abs (xFor (l) - x) - (l == processor.editLayer() ? 0.5f : 0.0f);
            if (d < distance)
            {
                distance = d;
                best = l;
            }
        }
    return best;
}

bool ReimaginedTrack::isLinked() const
{
    auto* link = processor.parameters.getRawParameterValue ("reimaginedLink");
    return link == nullptr || link->load() >= 0.5f;
}

void ReimaginedTrack::mouseDown (const juce::MouseEvent& e)
{
    dragging = thumbAt (e.position.x);
    if (dragging < 0)
        return;
    const bool linked = isLinked();
    for (int l = 0; l < 3; ++l)
    {
        const auto i = static_cast<std::size_t> (l);
        moving[i] = attachments[i] != nullptr && shown[i] && (l == dragging || linked);
        dragStart[i] = values[i];
        if (moving[i])
            attachments[i]->beginGesture();
    }
    mouseDrag (e);
}

void ReimaginedTrack::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;
    const auto t = travel();
    const float v = 100.0f * std::clamp ((e.position.x - t.getStart()) / t.getLength(), 0.0f, 1.0f);
    // The others follow by the same amount from where the drag began (offsets come back
    // when the drag returns from an end).
    const float delta = v - dragStart[static_cast<std::size_t> (dragging)];
    for (int l = 0; l < 3; ++l)
    {
        const auto i = static_cast<std::size_t> (l);
        if (moving[i])
            attachments[i]->setValueAsPartOfGesture (l == dragging ? v : std::clamp (dragStart[i] + delta, 0.0f, 100.0f));
    }
}

void ReimaginedTrack::mouseUp (const juce::MouseEvent&)
{
    for (std::size_t i = 0; i < 3; ++i)
        if (moving[i])
            attachments[i]->endGesture();
    moving = {};
    dragging = -1;
}

void ReimaginedTrack::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = thumbAt (e.position.x);
    if (hit < 0)
        return;
    const bool linked = isLinked();
    for (int l = 0; l < 3; ++l)
        if (shown[static_cast<std::size_t> (l)] && (l == hit || linked))
            if (auto* param = processor.parameters.getParameter (OspAudioProcessor::reimaginedParameterId (l)))
                attachments[static_cast<std::size_t> (l)]->setValueAsCompleteGesture (param->convertFrom0to1 (param->getDefaultValue()));
}

//==============================================================================
ReimaginedLinkButton::ReimaginedLinkButton (juce::AudioProcessorValueTreeState& state)
    : juce::Button ("Link Reimagined"), attachment (state, "reimaginedLink", *this)
{
    setClickingTogglesState (true);
    setTitle ("Link Reimagined");
    setTooltip ("Link: the layers' Original <-> Reimagined move together");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void ReimaginedLinkButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    // A tiny key with the link icon: lit in the accent while linked, quiet when not.
    using namespace design;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    draw::button (g, r, 0.25f * r.getHeight(), on, highlighted || down, colour::accent);
    icons::draw (g, icons::Kind::link, r.reduced (0.24f * r.getHeight()), on ? colour::accent.darker (0.15f) : colour::text.withAlpha (0.45f),
                 0.075f * r.getHeight());
}

void ReimaginedTrack::paint (juce::Graphics& g)
{
    using namespace design;
    const auto t = travel();
    const float cy = 0.5f * static_cast<float> (getHeight());
    // A fine track: warm at the Original end, neutral towards Reimagined.
    const auto track = juce::Rectangle<float> (t.getStart(), cy - 1.5f, t.getLength(), 3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8e6a52), track.getX(), 0.0f, juce::Colour (0xff9f9a92), track.getRight(), 0.0f, false));
    g.fillRoundedRectangle (track, 1.25f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.fillRect (track.withY (track.getBottom()).withHeight (0.8f));
    // Thumbs: the edited layer's on top.
    std::array<int, 3> order { 0, 1, 2 };
    std::stable_partition (order.begin(), order.end(), [this] (int l) { return l != processor.editLayer(); });
    for (int l : order)
    {
        if (! shown[static_cast<std::size_t> (l)])
            continue;
        const juce::Point<float> c (xFor (l), cy);
        const auto disc = juce::Rectangle<float> (22.0f, 22.0f).withCentre (c);
        juce::Path shape;
        shape.addEllipse (disc);
        juce::DropShadow (juce::Colour (0x50302418), 4, { 0, 2 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfaf5), c.x - 5.0f, disc.getY(), juce::Colour (0xffd8cfc1), c.x + 5.0f, disc.getBottom(), false));
        g.fillPath (shape);
        g.setColour (juce::Colour (0xffbcb1a1));
        g.strokePath (shape, juce::PathStrokeType (0.8f));
        draw::led (g, c, 10.0f, colour::identity (l).thumb, 0.0f);
    }
}

//==============================================================================
MixSection::MixSection (OspAudioProcessor& p) : processor (p), reimagined (p), linkButton (p.parameters), triangle (p.parameters)
{
    addAndMakeVisible (reimagined);
    addChildComponent (linkButton);

    blend.getProperties().set ("blend", true);
    blend.setTitle ("Layer blend");
    blend.setTooltip ("Blend between the two layers (equal power: the middle is not quieter)");
    blendAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.parameters, "ab.blend", blend);
    blend.setDoubleClickReturnValue (true, 0.5);
    addChildComponent (blend);
    triangle.onClick = [this] {
        if (onOpenMix != nullptr)
            onOpenMix();
    };
    triangle.setTooltip ("Three-layer mix: click for the large mix, or drag here");
    addChildComponent (triangle);
}

void MixSection::mouseUp (const juce::MouseEvent& e)
{
    if (count == 3 && captionArea.contains (e.getPosition()) && ! e.mouseWasDraggedSinceMouseDown() && onOpenMix != nullptr)
        onOpenMix();
}

void MixSection::setLayers (const std::array<bool, 3>& occupied)
{
    int n = 0;
    std::array<int, 3> which { 0, 1, 2 };
    for (int l = 0; l < 3; ++l)
        if (occupied[static_cast<std::size_t> (l)])
            which[static_cast<std::size_t> (n++)] = l;
    reimagined.setLayers (occupied);
    linkButton.setVisible (n >= 2);
    if (n == count && which == slots)
        return;
    count = n;
    slots = which;
    blend.getProperties().set ("leftColour", static_cast<juce::int64> (design::colour::identity (slots[0]).thumb.getARGB()));
    blend.getProperties().set ("rightColour", static_cast<juce::int64> (design::colour::identity (slots[1]).thumb.getARGB()));
    blend.setVisible (count == 2);
    triangle.setVisible (count == 3);
    resized();
    repaint();
}

void MixSection::resized()
{
    // Reference geometry (band 1378 x 83): caption at the left; the tracks run from 470 to
    // 1120 with their letters and words either side; two rows when two layers blend.
    captionArea = juce::Rectangle<int> (24, 8, 330, 67);
    const bool twoRows = count == 2;
    const int blendY = 26, reimaginedY = twoRows ? 59 : 41;
    blend.setBounds (470 - 16, blendY - 18, 650 + 32, 36);
    reimagined.setBounds (470 - 11, reimaginedY - 13, 650 + 22, 26);
    blendRow = twoRows ? juce::Rectangle<int> (380, blendY - 12, 870, 24) : juce::Rectangle<int>();
    reimaginedRow = juce::Rectangle<int> (330, reimaginedY - 10, 950, 20);
    linkButton.setBounds (1286, reimaginedY - 12, 24, 24);   // right of REIMAGINED
    if (count == 3)
        triangle.setBounds (28, 4, 84, 75);
}

void MixSection::paint (juce::Graphics& g)
{
    using namespace design;
    draw::raised (g, getLocalBounds().toFloat(), layout::panelRadius, colour::panelTop, colour::panelBottom);

    // ORIGINAL ... REIMAGINED either side of the track (always: it is global).
    g.setFont (fonts::make (14.5f, fonts::Weight::semibold, 0.11f));
    g.setColour (colour::textSecondary.darker (0.25f));
    const float ry = static_cast<float> (reimaginedRow.getCentreY());
    g.drawText ("ORIGINAL", juce::Rectangle<float> (240.0f, ry - 10.0f, 200.0f, 20.0f), juce::Justification::centredRight, false);
    g.drawText ("REIMAGINED", juce::Rectangle<float> (1152.0f, ry - 10.0f, 200.0f, 20.0f), juce::Justification::centredLeft, false);

    if (count == 2)
    {
        const float by = static_cast<float> (blendRow.getCentreY());
        g.setFont (fonts::make (19.0f, fonts::Weight::bold));
        g.setColour (colour::text);
        g.drawText (OspAudioProcessor::layerName (slots[0]), juce::Rectangle<float> (412.0f, by - 12.0f, 40.0f, 24.0f), juce::Justification::centred, false);
        g.drawText (OspAudioProcessor::layerName (slots[1]), juce::Rectangle<float> (1137.0f, by - 12.0f, 40.0f, 24.0f), juce::Justification::centred, false);
        g.setFont (fonts::make (24.5f, fonts::Weight::bold, 0.06f));
        g.drawText (OspAudioProcessor::layerName (slots[0]) + " / " + OspAudioProcessor::layerName (slots[1]) + " BLEND",
                    juce::Rectangle<float> (33.0f, 22.0f, 330.0f, 36.0f), juce::Justification::centredLeft, false);
    }
    if (count == 3)
    {
        // How much of each is heard (power shares, adding up to 100).
        const auto share = InstrumentEngine::triangleShares (processor.parameterValue ("mix.x"), processor.parameterValue ("mix.y"));
        g.setFont (fonts::make (26.0f, fonts::Weight::bold, 0.06f));
        g.setColour (colour::text);
        g.drawText ("MIX", juce::Rectangle<float> (126.0f, 10.0f, 200.0f, 34.0f), juce::Justification::centredLeft, false);
        g.setFont (fonts::make (16.0f, fonts::Weight::semibold));
        float x = 126.0f;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const auto part = OspAudioProcessor::layerName (static_cast<int> (i)) + " " + juce::String (juce::roundToInt (100.0 * share[i]));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), part) + 14.0f;
            g.setColour (colour::identity (static_cast<int> (i)).badgeBottom.brighter (0.15f));
            g.drawText (part, juce::Rectangle<float> (x, 44.0f, w, 22.0f), juce::Justification::centredLeft, false);
            x += w;
        }
    }
}

//==============================================================================
namespace
{
    /** The large mix: the same triangle at popup size for fine placement, with what each
        corner is and how much of it is heard. */
    class MixPopup final : public MiniPanel, private juce::Timer
    {
    public:
        explicit MixPopup (OspAudioProcessor& p)
            : MiniPanel ("MIX", "HOW MUCH OF EACH LAYER YOU HEAR"), processor (p), triangle (p.parameters)
        {
            triangle.setTooltip ("Drag between A, B and C (double-click: all three equal)");
            addAndMakeVisible (triangle);
            for (int l = 0; l < 3; ++l)
                if (auto instrument = processor.currentInstrument (l))
                    names[static_cast<std::size_t> (l)] = juce::String (instrument->filename);
            startTimerHz (20);
        }

        // Laid out at 0.69 of the reference's unit; the editor scales it up to match the macro popups.
        float unit() const override { return 0.69f; }
        juce::Point<int> cardSize() const override { return { 340, headerHeight() + 8 + 250 + 10 + 3 * 20 + juce::roundToInt (26.0f * unit()) }; }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            using namespace palette;
            const auto share = shares();
            auto rows = readout.toFloat();
            for (int l = 0; l < 3; ++l)
            {
                auto row = rows.removeFromTop (20.0f);
                const auto colour = layer (l);
                auto badge = row.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f);
                g.setColour (colour);
                g.fillRoundedRectangle (badge, 3.5f);
                g.setColour (juce::Colours::white);
                g.setFont (fonts::make (10.5f, fonts::Weight::semibold));
                g.drawText (OspAudioProcessor::layerName (l), badge, juce::Justification::centred, false);
                row.removeFromLeft (8.0f);
                const auto percent = juce::String (juce::roundToInt (100.0 * share[static_cast<std::size_t> (l)])) + " %";
                g.setFont (fonts::make (12.0f, fonts::Weight::semibold));
                g.setColour (colour.darker (0.3f));
                g.drawText (percent, row.removeFromRight (48.0f), juce::Justification::centredRight, false);
                // How much of the row the share fills: a thin bar under the name.
                const auto bar = row.withTrimmedRight (10.0f).removeFromBottom (3.0f);
                g.setColour (recessed);
                g.fillRoundedRectangle (bar, 1.5f);
                g.setColour (colour.withAlpha (0.8f));
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * static_cast<float> (share[static_cast<std::size_t> (l)])), 1.5f);
                g.setFont (fonts::make (11.5f));
                g.setColour (text);
                g.drawText (names[static_cast<std::size_t> (l)], row.withTrimmedRight (10.0f).withTrimmedBottom (4.0f), juce::Justification::centredLeft, true);
            }
        }

    private:
        std::array<double, 3> shares() const
        {
            return InstrumentEngine::triangleShares (processor.parameterValue ("mix.x"), processor.parameterValue ("mix.y"));
        }

        void timerCallback() override
        {
            const auto now = shares();
            if (now != shown)
            {
                shown = now;
                repaint (readout);
            }
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            area.removeFromTop (8);
            triangle.setBounds (area.removeFromTop (250).reduced (24, 0));
            area.removeFromTop (10);
            readout = area.removeFromTop (3 * 20).reduced (4, 0);
        }

        OspAudioProcessor& processor;
        TriangleMix triangle;
        std::array<juce::String, 3> names;
        std::array<double, 3> shown {};
        juce::Rectangle<int> readout;
    };
}

std::unique_ptr<MiniPanel> createMixPopup (OspAudioProcessor& processor)
{
    return std::make_unique<MixPopup> (processor);
}

//==============================================================================
PresetBar::PresetBar (OspAudioProcessor& p) : processor (p)
{
    setTitle ("Preset");
    refresh();
}

void PresetBar::refresh()
{
    const auto n = processor.presetDisplayName();
    const bool f = processor.isFavourite();
    if (n != name || f != favourite)
    {
        name = n;
        favourite = f;
        repaint();
    }
}

juce::Rectangle<float> PresetBar::partBounds (Part part) const
{
    // Reference (446 x 48): arrow cells of 60 and 56 px behind hairlines, the heart just
    // inside the right one, the name centred on the whole bar.
    const auto r = getLocalBounds().toFloat();
    const float k = r.getHeight() / 48.0f;
    const float left = 60.0f * k, right = 56.0f * k;
    switch (part)
    {
        case Part::previous: return r.withWidth (left);
        case Part::next: return r.withTrimmedLeft (r.getWidth() - right);
        case Part::favourite: return juce::Rectangle<float> (r.getRight() - right - 44.0f * k, r.getY(), 40.0f * k, r.getHeight());
        case Part::name: return r.withTrimmedLeft (left).withTrimmedRight (right + 46.0f * k);
        case Part::none: break;
    }
    return {};
}

PresetBar::Part PresetBar::partAt (juce::Point<int> p) const
{
    for (auto part : { Part::previous, Part::next, Part::favourite, Part::name })
        if (partBounds (part).contains (p.toFloat()))
            return part;
    return Part::none;
}

void PresetBar::mouseUp (const juce::MouseEvent& e)
{
    switch (partAt (e.getPosition()))
    {
        case Part::previous: processor.stepPresetList (-1); break;
        case Part::next: processor.stepPresetList (1); break;
        case Part::favourite: processor.toggleFavourite(); break;
        case Part::name:
        {
            juce::PopupMenu menu;
            const auto list = processor.presetList();
            const auto current = processor.presetDisplayName();
            juce::Component::SafePointer<PresetBar> safe (this);
            auto opened = [safe] {
                if (safe == nullptr)
                    return;
                safe->refresh();
                if (safe->onChange != nullptr)
                    safe->onChange();
            };
            // Starting states (settings only, the sounds stay): INIT empties the patch, Reset
            // returns every setting to its default, then the built-in and the saved ones.
            menu.addSectionHeader ("Starting states");
            menu.addItem ("INIT", true, current == "INIT", [safe, opened] { if (safe != nullptr) safe->processor.initPatch(), opened(); });
            menu.addItem ("Reset settings", true, false, [safe, opened] { if (safe != nullptr) safe->processor.resetSettings(), opened(); });
            menu.addSeparator();
            bool presetsHeader = false;
            for (const auto& entry : list)
            {
                if (! entry.startingState && ! presetsHeader)
                {
                    menu.addItem (juce::String::fromUTF8 ("Save starting state\xe2\x80\xa6"), [safe] {
                        if (safe != nullptr && safe->onSaveStartingState != nullptr)
                            safe->onSaveStartingState();
                    });
                    menu.addSectionHeader ("Presets");
                    presetsHeader = true;
                }
                menu.addItem (entry.name, true, entry.name == current, [safe, entry, opened] {
                    if (safe == nullptr)
                        return;
                    safe->processor.openPresetEntry (entry);
                    opened();
                });
            }
            if (! presetsHeader)
                menu.addItem (juce::String::fromUTF8 ("Save starting state\xe2\x80\xa6"), [safe] {
                    if (safe != nullptr && safe->onSaveStartingState != nullptr)
                        safe->onSaveStartingState();
                });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withDeletionCheck (*this));
            return;
        }
        case Part::none: return;
    }
    refresh();
    if (onChange != nullptr)
        onChange();
}

void PresetBar::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const float k = r.getHeight() / 48.0f;
    draw::raised (g, r.reduced (0.5f), 9.0f * k, colour::cardTop, colour::cardBottom, 0.9f);
    const auto hover = isMouseOver() ? partAt (getMouseXYRelative()) : Part::none;
    for (auto part : { Part::previous, Part::next })
    {
        const auto b = partBounds (part);
        if (hover == part)
        {
            g.setColour (colour::text.withAlpha (0.05f));
            g.fillRoundedRectangle (b.reduced (3.0f * k), 6.0f * k);
        }
        icons::draw (g, part == Part::previous ? icons::Kind::chevronLeft : icons::Kind::chevronRight,
                     b.withSizeKeepingCentre (26.0f * k, 26.0f * k), colour::text, 2.3f * k);
        g.setColour (colour::divider);
        g.fillRect (juce::Rectangle<float> (part == Part::previous ? b.getRight() : b.getX(), r.getY() + 8.0f * k, 1.0f, r.getHeight() - 16.0f * k));
    }
    const auto heart = partBounds (Part::favourite);
    icons::draw (g, favourite ? icons::Kind::heartFilled : icons::Kind::heart, heart.withSizeKeepingCentre (26.0f * k, 26.0f * k),
                 favourite || hover == Part::favourite ? colour::accent : colour::text, 2.0f * k);
    g.setColour (hover == Part::name ? juce::Colours::black : colour::text);
    g.setFont (fonts::make (23.0f * k, fonts::Weight::medium));
    g.drawText (name, r.withTrimmedLeft (60.0f * k + 46.0f * k).withTrimmedRight (56.0f * k + 46.0f * k), juce::Justification::centred, true);
}

//==============================================================================
namespace
{
    constexpr float attackMax = 2000.0f, decayMax = 20000.0f, releaseMax = 15000.0f;
    /** A compressed time axis: short times get room, long ones still fit. */
    float timeShare (float ms, float maximum) { return std::log1p (std::max (0.0f, ms) / 20.0f) / std::log1p (maximum / 20.0f); }
    float timeFromShare (float share, float maximum) { return 20.0f * std::expm1 (std::clamp (share, 0.0f, 1.0f) * std::log1p (maximum / 20.0f)); }
}

EnvelopePanel::EnvelopePanel (juce::AudioProcessorValueTreeState& s) : state (s)
{
    auto ms = [] (double v) { return format::milliseconds (v); };
    const std::array<std::pair<const char*, const char*>, 4> ids { { { "attack", "A" }, { "decay", "D" }, { "sustainLevel", "S" }, { "release", "R" } } };
    for (std::size_t i = 0; i < knobs.size(); ++i)
    {
        MiniKnob::Formatter f = i == 2 ? MiniKnob::Formatter ([] (double v) { return juce::String (juce::roundToInt (v)) + " %"; }) : MiniKnob::Formatter (ms);
        knobs[i] = std::make_unique<MiniKnob> (state, ids[i].first, ids[i].second, std::move (f));
        addAndMakeVisible (*knobs[i]);
    }
    setTitle ("Amplitude envelope");
}

EnvelopePanel::Shape EnvelopePanel::shape() const
{
    auto v = [this] (const char* id) {
        auto* p = state.getParameter (id);
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    };
    return { v ("attack"), v ("decay"), 0.01f * v ("sustainLevel"), v ("release") };
}

std::array<juce::Point<float>, 3> EnvelopePanel::handles() const
{
    const auto s = shape();
    const auto r = graph.toFloat().reduced (12.0f, 10.0f);
    const float w = r.getWidth();
    const float peakX = r.getX() + 0.27f * w * timeShare (s.attack, attackMax);
    const float decayX = peakX + 0.27f * w * timeShare (s.decay, decayMax);
    const float sustainY = r.getBottom() - s.sustain * r.getHeight();
    const float holdEnd = decayX + 0.16f * w;
    const float releaseX = holdEnd + 0.28f * w * timeShare (s.release, releaseMax);
    return { juce::Point<float> (peakX, r.getY()), juce::Point<float> (decayX, sustainY), juce::Point<float> (releaseX, r.getBottom()) };
}

// Reference geometry (panel 506 x 186 from 899, 708): the title, a 482 x 70 display, then
// A D S R as small knobs centred at 71, 191, 314 and 437.
void EnvelopePanel::resized()
{
    graph = juce::Rectangle<int> (12, 26, 482, 70);
    static constexpr std::array<int, 4> centres { 71, 191, 314, 437 };
    for (std::size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (juce::Rectangle<int> (100, 87).withCentre ({ centres[i], 141 }));
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    using namespace design;
    g.setColour (colour::text);
    g.setFont (fonts::make (16.5f, fonts::Weight::bold, 0.03f));
    g.drawText ("AMP ENVELOPE", juce::Rectangle<float> (16.0f, 1.0f, 300.0f, 22.0f), juce::Justification::centredLeft, false);

    const auto well = graph.toFloat();
    draw::well (g, well, 8.0f, colour::wellA);
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (graph.reduced (1));
        for (int i = 1; i < 8; ++i)
        {
            g.setColour (colour::wellGrid.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (well.getX() + well.getWidth() * static_cast<float> (i) / 8.0f, well.getY(), 1.0f, well.getHeight()));
        }
    }
    const auto r = well.reduced (12.0f, 10.0f);
    const auto h = handles();
    const float holdEnd = h[1].x + 0.16f * r.getWidth();
    juce::Path curve;
    curve.startNewSubPath (r.getX(), r.getBottom());
    curve.lineTo (h[0]);
    // Decay and release fall exponentially: drawn as curves.
    curve.quadraticTo (h[0].x + 0.15f * (h[1].x - h[0].x), h[1].y, h[1].x, h[1].y);
    curve.lineTo (holdEnd, h[1].y);
    curve.quadraticTo (holdEnd + 0.15f * (h[2].x - holdEnd), h[2].y, h[2].x, h[2].y);
    juce::Path fill (curve);
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();
    g.setGradientFill (juce::ColourGradient (colour::accent.withAlpha (0.38f), 0.0f, r.getY(), colour::accent.withAlpha (0.05f), 0.0f, r.getBottom(), false));
    g.fillPath (fill);
    g.setColour (colour::accent);
    g.strokePath (curve, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (int i = 0; i < 3; ++i)
    {
        const auto p = h[static_cast<std::size_t> (i)];
        const auto dot = juce::Rectangle<float> (12.0f, 12.0f).withCentre (p);
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillEllipse (dot.expanded (1.5f).translated (0.0f, 0.8f));
        g.setColour (i == dragHandle ? colour::accent : juce::Colour (0xfffbf8f2));
        g.fillEllipse (dot);
    }
}

void EnvelopePanel::mouseDown (const juce::MouseEvent& e)
{
    dragHandle = -1;
    const auto h = handles();
    float best = 14.0f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = h[static_cast<std::size_t> (i)].getDistanceFrom (e.position);
        if (d < best)
        {
            best = d;
            dragHandle = i;
        }
    }
    const std::array<std::vector<const char*>, 3> ids { { { "attack" }, { "decay", "sustainLevel" }, { "release" } } };
    if (dragHandle >= 0)
        for (const auto* id : ids[static_cast<std::size_t> (dragHandle)])
            if (auto* p = state.getParameter (id))
                p->beginChangeGesture();
    repaint();
}

void EnvelopePanel::mouseDrag (const juce::MouseEvent& e)
{
    if (dragHandle < 0)
        return;
    const auto r = graph.toFloat().reduced (12.0f, 10.0f);
    const auto h = handles();
    const float w = std::max (1.0f, r.getWidth());
    auto set = [this] (const char* id, float value) {
        if (auto* p = state.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (value)));
    };
    if (dragHandle == 0)
        set ("attack", timeFromShare ((e.position.x - r.getX()) / (0.27f * w), attackMax));
    else if (dragHandle == 1)
    {
        set ("decay", timeFromShare ((e.position.x - h[0].x) / (0.27f * w), decayMax));
        set ("sustainLevel", 100.0f * std::clamp ((r.getBottom() - e.position.y) / r.getHeight(), 0.0f, 1.0f));
    }
    else
        set ("release", timeFromShare ((e.position.x - (h[1].x + 0.16f * w)) / (0.28f * w), releaseMax));
    repaint();
}

void EnvelopePanel::mouseUp (const juce::MouseEvent&)
{
    const std::array<std::vector<const char*>, 3> ids { { { "attack" }, { "decay", "sustainLevel" }, { "release" } } };
    if (dragHandle >= 0)
        for (const auto* id : ids[static_cast<std::size_t> (dragHandle)])
            if (auto* p = state.getParameter (id))
                p->endChangeGesture();
    dragHandle = -1;
    repaint();
}

//==============================================================================
Wheel::Wheel (juce::String c, bool spring, std::function<void (float)> move) : caption (std::move (c)), springBack (spring), onMove (std::move (move))
{
    setTitle (caption);
    setTooltip (springBack ? "Pitch bend (springs back)" : "Modulation: opens MOVEMENT up");
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> Wheel::slot() const
{
    return getLocalBounds().toFloat().withHeight (88.0f);   // reference: 36 x 88, its caption below
}

void Wheel::setValue (float newValue)
{
    if (dragging || std::abs (newValue - value) < 1.0e-4f)
        return;
    value = newValue;
    repaint();
}

void Wheel::mouseDown (const juce::MouseEvent&)
{
    dragging = true;
    dragStart = value;
}

void Wheel::mouseDrag (const juce::MouseEvent& e)
{
    const float travel = slot().getHeight() * (springBack ? 0.5f : 1.0f);
    value = std::clamp (dragStart - static_cast<float> (e.getDistanceFromDragStartY()) / std::max (1.0f, travel), springBack ? -1.0f : 0.0f, 1.0f);
    if (onMove != nullptr)
        onMove (value);
    repaint();
}

void Wheel::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    if (springBack)
    {
        value = 0.0f;
        if (onMove != nullptr)
            onMove (value);
        repaint();
    }
}

void Wheel::paint (juce::Graphics& g)
{
    using namespace design;
    const auto s = slot();
    // A cream bezel around a dark slot; in it the wheel, seen through a narrow window that
    // glows where the wheel is turned: PITCH a long amber segment that rides up and down
    // from the middle, MOD a blue level rising from the bottom.
    draw::raised (g, s, 7.0f, colour::cardTop, colour::cardBottom, 0.7f);
    const auto slotArea = juce::Rectangle<float> (s.getX() + 4.0f, s.getY() + 6.0f, s.getWidth() - 8.0f, s.getHeight() - 10.0f);
    g.setColour (juce::Colour (0xff15110d));
    g.fillRoundedRectangle (slotArea, 4.5f);
    const auto body = slotArea.reduced (1.5f, 1.0f);
    g.setGradientFill (juce::ColourGradient (springBack ? juce::Colour (0xff57504a) : juce::Colour (0xff474845), 0.0f, body.getY(),
                                             springBack ? juce::Colour (0xff201a15) : juce::Colour (0xff1a2125), 0.0f, body.getBottom(), false));
    g.fillRoundedRectangle (body, 3.5f);
    // A lighter rail down the left of the wheel, a shade under the slot's top edge.
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.fillRect (juce::Rectangle<float> (body.getX(), body.getY() + 3.0f, 1.5f, body.getHeight() - 6.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.45f), 0.0f, body.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, body.getY() + 7.0f, false));
    g.fillRect (body.withHeight (7.0f));

    const auto window = juce::Rectangle<float> (slotArea.getX() + 0.23f * slotArea.getWidth(), body.getY() + 2.0f,
                                                0.46f * slotArea.getWidth(), body.getHeight() - 5.0f);
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRect (juce::Rectangle<float> (window.getX() - 1.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));
    g.fillRect (juce::Rectangle<float> (window.getRight() + 0.5f, body.getY() + 2.0f, 1.0f, body.getHeight() - 4.0f));

    juce::Rectangle<float> lit;
    if (springBack)
    {
        const float length = 0.68f * slotArea.getHeight();
        const float centre = slotArea.getCentreY() + 0.07f * slotArea.getHeight() - value * 0.3f * slotArea.getHeight();
        lit = juce::Rectangle<float> (window.getX(), centre - 0.5f * length, window.getWidth(), length).getIntersection (window);
    }
    else
    {
        const float top = window.getBottom() - value * window.getHeight();
        lit = window.withTop (top);
    }
    if (lit.getHeight() > 1.0f)
    {
        juce::ColourGradient light (juce::Colour (springBack ? 0xffffb874 : 0xff3b4e59), 0.0f, lit.getY(),
                                    juce::Colour (springBack ? 0xff833005 : 0xff2b4a5e), 0.0f, lit.getBottom(), false);
        if (springBack)
        {
            light.addColour (0.12, juce::Colour (0xffffb26d));
            light.addColour (0.3, juce::Colour (0xfff58744));
            light.addColour (0.5, juce::Colour (0xffcd5a1e));
            light.addColour (0.75, juce::Colour (0xffa23a06));
        }
        else
        {
            light.addColour (0.18, juce::Colour (0xff4f7290));
            light.addColour (0.45, juce::Colour (0xff86b1d2));
            light.addColour (0.65, juce::Colour (0xff8ebee7));
            light.addColour (0.86, juce::Colour (0xff6899ba));
        }
        g.setGradientFill (light);
        g.fillRoundedRectangle (lit, 2.5f);
        // Rounded across: a shade at either edge, the right a little deeper.
        juce::ColourGradient across (juce::Colours::black.withAlpha (0.3f), lit.getX(), 0.0f, juce::Colours::black.withAlpha (0.4f), lit.getRight(), 0.0f, false);
        across.addColour (0.2, juce::Colours::black.withAlpha (0.0f));
        across.addColour (0.7, juce::Colours::black.withAlpha (0.0f));
        g.setGradientFill (across);
        g.fillRoundedRectangle (lit, 2.5f);
        g.setColour (juce::Colours::white.withAlpha (springBack ? 0.22f : 0.1f));
        g.fillRect (lit.withHeight (1.0f).reduced (1.5f, 0.0f));
    }
    g.setColour (colour::text);
    g.setFont (fonts::make (13.5f, fonts::Weight::bold, 0.03f));
    g.drawText (caption, juce::Rectangle<float> (-12.0f, 88.0f, static_cast<float> (getWidth()) + 24.0f, 18.0f), juce::Justification::centred, false);
}

} // namespace osp::plugin
