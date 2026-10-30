#include "MainSections.h"
#include "Binding.h"
#include "ValueFormat.h"

#include "Design.h"
#include "EngineCard.h"
#include "ModulationPanel.h"

#include "engine/InstrumentEngine.h"

#include <cmath>

namespace osp::plugin
{

//==============================================================================
namespace mixgeometry
{
    std::array<double, 3> shares (double mixX, double mixY) { return InstrumentEngine::triangleShares (mixX, mixY); }

    juce::Point<float> stateFromShares (const std::array<double, 3>& share)
    {
        // triangleShares: A = 1 - x - y/2, B = y, C = x - y/2.
        return { static_cast<float> (share[2] + 0.5 * share[1]), static_cast<float> (share[1]) };
    }

    juce::Point<float> pointFor (const std::array<double, 3>& share, const std::array<juce::Point<float>, 3>& c)
    {
        return { static_cast<float> (share[0] * c[0].x + share[1] * c[1].x + share[2] * c[2].x),
                 static_cast<float> (share[0] * c[0].y + share[1] * c[1].y + share[2] * c[2].y) };
    }

    std::array<double, 3> sharesAt (juce::Point<float> p, const std::array<juce::Point<float>, 3>& c)
    {
        // Barycentric coordinates; outside the triangle the negative ones are clipped and
        // the rest renormalised (as the engine does: the map is affine, so it agrees).
        const auto v0 = c[1] - c[0], v1 = c[2] - c[0], v2 = p - c[0];
        const double d00 = v0.x * v0.x + v0.y * v0.y, d01 = v0.x * v1.x + v0.y * v1.y, d11 = v1.x * v1.x + v1.y * v1.y;
        const double d20 = v2.x * v0.x + v2.y * v0.y, d21 = v2.x * v1.x + v2.y * v1.y;
        const double den = d00 * d11 - d01 * d01;
        if (std::abs (den) < 1.0e-9)
            return { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 };
        std::array<double, 3> w { 0.0, (d11 * d20 - d01 * d21) / den, (d00 * d21 - d01 * d20) / den };
        w[0] = 1.0 - w[1] - w[2];
        double total = 0.0;
        for (auto& v : w)
        {
            v = std::max (0.0, v);
            total += v;
        }
        if (total <= 1.0e-12)
            return { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 };
        for (auto& v : w)
            v /= total;
        return w;
    }

    double blendShare (double blend)
    {
        const double s = std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp (blend, 0.0, 1.0));
        return s * s;
    }

    double blendForShare (double shareB)
    {
        return std::asin (std::sqrt (std::clamp (shareB, 0.0, 1.0))) * 2.0 / juce::MathConstants<double>::pi;
    }
}

//==============================================================================
TriangleMix::TriangleMix (juce::AudioProcessorValueTreeState& state)
    : xAttachment (*state.getParameter ("mix.x"), [this] (float v) { x = v; repaint(); }),
      yAttachment (*state.getParameter ("mix.y"), [this] (float v) { y = v; repaint(); })
{
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
    binding::tag (*this, "mix.x mix.y");
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
    // A bottom left, B bottom right, C on top (the header's MIX, at popup size).
    return { juce::Point<float> (r.getX(), r.getBottom()), juce::Point<float> (r.getRight(), r.getBottom()),
             juce::Point<float> (r.getCentreX(), r.getY()) };
}

void TriangleMix::moveTo (juce::Point<float> where)
{
    // Into the triangle (the same clipping the engine applies), then back to x/y.
    const auto state = mixgeometry::stateFromShares (mixgeometry::sharesAt (where, corners()));
    xAttachment.setValueAsPartOfGesture (state.x);
    yAttachment.setValueAsPartOfGesture (state.y);
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
    const float k = std::clamp ((c[1].x - c[0].x) / 46.0f, 1.0f, 2.4f);
    const auto share = mixgeometry::shares (x, y);
    const auto node = mixgeometry::pointFor (share, c);
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
    design::CachedShadow (juce::Colour (0x331e1c18), 3, { 0, 1 }).drawForPath (g, disc);
    g.setColour (raised);
    g.fillPath (disc);
    g.setColour (mix);
    g.strokePath (disc, juce::PathStrokeType (1.4f));
    g.setColour (accent);
    g.fillEllipse (node.x - 2.0f * k, node.y - 2.0f * k, 4.0f * k, 4.0f * k);

    g.setFont (fonts::make (10.0f * std::sqrt (k), fonts::Weight::medium));
    g.setColour (textDim);
    const float t = 8.0f * std::sqrt (k), gap = 2.0f * k;
    g.drawText ("A", juce::Rectangle<float> (c[0].x - t - gap, c[0].y - t * 0.5f, t, t + 2.0f), juce::Justification::centredRight, false);
    g.drawText ("B", juce::Rectangle<float> (c[1].x + gap, c[1].y - t * 0.5f, t, t + 2.0f), juce::Justification::centredLeft, false);
    g.drawText ("C", juce::Rectangle<float> (c[2].x + gap + 2.0f, c[2].y - t * 0.75f, t, t), juce::Justification::centredLeft, false);
}

//==============================================================================
VolumeSlider::VolumeSlider (juce::AudioProcessorValueTreeState& state)
    : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox),
      attachment (state, "gain", *this)
{
    getProperties().set ("volume", true);
    binding::tag (*this, "gain");
    setTitle ("Volume");
    setTooltip ("Master volume");
    setDoubleClickReturnValue (true, 0.0);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setVelocityBasedMode (false);
}

void VolumeSlider::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    const bool hot = isMouseOverOrDragging();

    // VOLUME on the left, the value on the right (tabular figures), above the track.
    const auto caption = bounds.withHeight (18.0f).reduced (static_cast<float> (thumbRadius) - 1.0f, 0.0f);   // aligned with the track's ends
    g.setFont (fonts::make (12.0f, fonts::Weight::medium, 0.12f));
    g.setColour (colour::textSecondary.withAlpha (hot ? 1.0f : 0.9f));
    g.drawText ("VOLUME", caption, juce::Justification::centredLeft, false);
    g.setFont (fonts::make (12.5f, fonts::Weight::regular, 0.0f).withExtraKerningFactor (0.0f));
    g.setColour (colour::text.withAlpha (hot ? 0.9f : 0.72f));
    g.drawText (format::levelDb (getValue()), caption, juce::Justification::centredRight, false);

    // A hairline recessed track: a dark line with a light lip under it; the part up to the
    // thumb a little warmer (barely an indication).
    const float cy = bounds.getBottom() - 11.0f;
    const float left = static_cast<float> (thumbRadius), right = bounds.getWidth() - static_cast<float> (thumbRadius);
    const float x = static_cast<float> (getPositionOfValue (getValue()));

    // Ceramic: the C1 ivory cap (three grip grooves) on the T3 metal guide, rendered; the
    // value is a fine accent line along the guide up to the cap (exact at any value).
    if (auto* pieces = skin::active())
    {
        const juce::Rectangle<float> rail (left - 1.6f, cy - 1.6f, right - left + 3.2f, 3.2f);
        const auto cap = juce::Rectangle<float> (14.0f, 22.0f).withCentre ({ x, cy });
        const bool drawn = pieces->draw (g, "rail", rail);
        if (drawn)
        {
            g.setColour (colour::accent.withAlpha (hot ? 1.0f : 0.92f));
            g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.6f, std::max (0.0f, x - left), 1.2f), 0.6f);
        }
        if (drawn && pieces->draw (g, isMouseButtonDown() ? "fader-drag" : hot ? "fader-hover" : "fader-normal", cap))
            return;
    }
    g.setColour (juce::Colour (0xffbdb2a2));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.75f, right - left, 1.5f), 0.75f);
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (left + 1.0f, cy + 0.9f, right - left - 2.0f, 0.7f));
    g.setColour (colour::accent.interpolatedWith (juce::Colour (0xff8f7f6c), 0.45f).withAlpha (0.8f));
    g.fillRoundedRectangle (juce::Rectangle<float> (left, cy - 0.75f, std::max (0.0f, x - left), 1.5f), 0.75f);

    // The thumb: a small cream cap with a fine rim and a contact shadow, kin to the knobs.
    const auto disc = juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ x, cy });
    juce::Path shape;
    shape.addEllipse (disc);
    design::CachedShadow (juce::Colour (0x40302418), 3, { 0, 1 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffefbf6), x, disc.getY(), juce::Colour (0xffd8cfc2), x, disc.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colour (hot ? 0xff9c8f7c : 0xffb0a593));
    g.strokePath (shape, juce::PathStrokeType (0.8f));
}

//==============================================================================
HeaderMix::HeaderMix (InstrumentHost& p)
    : processor (p),
      blendAttachment (*p.apvts().getParameter ("ab.blend"), [this] (float v) { blend = v; repaint(); }),
      xAttachment (*p.apvts().getParameter ("mix.x"), [this] (float v) { mixX = v; repaint(); }),
      yAttachment (*p.apvts().getParameter ("mix.y"), [this] (float v) { mixY = v; repaint(); })
{
    blendAttachment.sendInitialUpdate();
    xAttachment.sendInitialUpdate();
    yAttachment.sendInitialUpdate();
    binding::tag (*this, "ab.blend mix.x mix.y");
    setTitle ("Layer mix");
    setTooltip ("Mix: drag between the layers (double-click: equal)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setAlpha (0.0f);
    setVisible (false);
}

void HeaderMix::setLayers (const std::array<bool, 3>& occupied)
{
    int n = 0;
    std::array<int, 3> which { 0, 1, 2 };
    for (int l = 0; l < 3; ++l)
        if (occupied[static_cast<std::size_t> (l)])
            which[static_cast<std::size_t> (n++)] = l;
    if (n == count && which == slots)
        return;
    const int before = count;
    count = n;
    slots = which;
    // One source: nothing to mix. Two: the line fades in (from one) or folds down (from
    // three). Three: the line unfolds into the triangle. 200 ms, no overshoot.
    opacityTarget = count >= 2 && (count < 3 || design::showMixTriangle) ? 1.0f : 0.0f;
    morphTarget = count >= 3 ? 1.0f : 0.0f;
    if (before <= 1 && count >= 2)
        morph = morphTarget;   // appears in its form, it does not unfold from nothing
    if (count >= 2)
        setVisible (true);
    if (! isShowing())
        finishAnimations();
    startTimerHz (60);
    repaint();
}

void HeaderMix::finishAnimations()
{
    morph = morphTarget;
    opacity = opacityTarget;
    readout = readoutTarget;
    setAlpha (opacity);
    setVisible (opacity > 0.0f);
    stopTimer();
    repaint();
}

void HeaderMix::timerCallback()
{
    const float step = 1.0f / (0.2f * 60.0f);
    auto towards = [step] (float& v, float target, float rate = 1.0f) {
        v = target > v ? std::min (target, v + step * rate) : std::max (target, v - step * rate);
    };
    if (readoutTarget <= 0.0f && readout > 0.0f && juce::Time::getMillisecondCounter() < readoutHoldUntil)
        ;   // the readout lingers a moment after an interaction
    else
        towards (readout, readoutTarget, readoutTarget > 0.0f ? 1.5f : 0.6f);
    towards (morph, morphTarget);
    towards (opacity, opacityTarget);
    setAlpha (opacity);
    if (opacity <= 0.0f && opacityTarget <= 0.0f)
        setVisible (false);
    repaint();
    if (juce::exactlyEqual (morph, morphTarget) && juce::exactlyEqual (opacity, opacityTarget) && juce::exactlyEqual (readout, readoutTarget))
        stopTimer();
}

std::array<juce::Point<float>, 3> HeaderMix::corners() const
{
    // A and B never move; C rises from the middle of the line as the triangle unfolds
    // (smoothstep, no overshoot).
    const float cx = 0.5f * static_cast<float> (getWidth());
    const float t = morph * morph * (3.0f - 2.0f * morph);
    return { juce::Point<float> (cx - halfBase, baseY), juce::Point<float> (cx + halfBase, baseY),
             juce::Point<float> (cx, baseY - apexHeight * t) };
}

std::array<double, 3> HeaderMix::shownShares() const
{
    // While modulated, what is heard now (the live mix), else the controls.
    if (count >= 3)
        return modulated ? mixgeometry::shares (liveX, liveY) : mixgeometry::shares (mixX, mixY);
    const double b = mixgeometry::blendShare (modulated ? liveBlend : blend);
    return { 1.0 - b, b, 0.0 };
}

juce::Point<float> HeaderMix::nodePosition() const
{
    // Along the line the node sits at B's power share (so the triangle's base agrees with
    // it); in the triangle at the shares' point; in between, the one blends into the other.
    const auto c = corners();
    const auto b = static_cast<float> (mixgeometry::blendShare (blend));
    const auto onLine = c[0] + (c[1] - c[0]) * b;
    const auto inTriangle = mixgeometry::pointFor (mixgeometry::shares (mixX, mixY), c);
    if (count >= 3 && morph >= 1.0f)
        return inTriangle;
    if (count < 3 && morph <= 0.0f)
        return onLine;
    return onLine + (inTriangle - onLine) * morph;
}

juce::Point<float> HeaderMix::liveNodePosition() const
{
    const auto c = corners();
    if (count >= 3)
        return mixgeometry::pointFor (mixgeometry::shares (liveX, liveY), c);
    return c[0] + (c[1] - c[0]) * static_cast<float> (mixgeometry::blendShare (liveBlend));
}

void HeaderMix::setModulationDrag (int source, bool over)
{
    over = over && source >= 0;
    if (source == modDragSource && over == modDragOver)
        return;
    modDragSource = source;
    modDragOver = over;
    repaint();
}

bool HeaderMix::acceptsModulationDrop (juce::Point<float> inParent) const
{
    if (! isVisible() || count < 2)
        return false;
    const auto p = inParent - getPosition().toFloat();
    return const_cast<HeaderMix*> (this)->hitTest (juce::roundToInt (p.x), juce::roundToInt (p.y));
}

void HeaderMix::refreshModulation()
{
    // The routes onto the MIX (A <-> B: the line, and the triangle's A -> B direction). A
    // bipolar LFO swings both ways around the base; a unipolar source (and an envelope) only
    // towards its depth's sign, as the knobs' halos show it.
    double lo = 0.0, hi = 0.0;
    int source = -1;
    bool routed = false;
    for (const auto& r : processor.modulationRoutes())
    {
        if (r.isDepth() || r.state != mod::RouteState::active || (r.route.dest != mod::Dest::mixPosition && r.route.dest != mod::Dest::mixTowardsC))
            continue;
        routed = true;
        if (source < 0)
            source = mod::sourceIndex (r.route.source);
        if (r.route.dest != mod::Dest::mixPosition)
            continue;
        const double d = r.route.depth;
        const bool bipolar = modui::isBipolar (processor, mod::sourceIndex (r.route.source));
        lo += bipolar ? -std::abs (d) : std::min (0.0, d);
        hi += bipolar ? std::abs (d) : std::max (0.0, d);
    }
    const auto live = processor.liveMix();
    routed = routed && live.modulated;
    juce::Range<float> range;
    if (routed)
    {
        auto share = [] (double b) { return static_cast<float> (mixgeometry::blendShare (std::clamp (b, 0.0, 1.0))); };
        range = { share (blend + lo), share (blend + hi) };
    }
    const bool changed = routed != modulated || range != modRange || source != modSource
                         || (routed && (! juce::exactlyEqual (live.blend, liveBlend) || ! juce::exactlyEqual (live.x, liveX) || ! juce::exactlyEqual (live.y, liveY)));
    modulated = routed;
    modRange = range;
    modSource = source;
    liveBlend = live.blend;
    liveX = live.x;
    liveY = live.y;
    if (changed)
        repaint();
}

juce::Rectangle<float> HeaderMix::captionArea() const
{
    // Above the line, rising with C to clear the apex and its letter.
    const auto c = corners();
    const float top = c[2].y - 24.0f + 4.0f * morph;
    return { 0.5f * static_cast<float> (getWidth()) - 22.0f, top, 44.0f, 13.0f };
}

bool HeaderMix::hitTest (int x, int y)
{
    if (count < 2 || (count >= 3 && ! design::showMixTriangle))
        return false;
    const auto c = corners();
    const auto box = juce::Rectangle<float> (c[0], c[1]).getUnion (juce::Rectangle<float> (c[2], c[2])).expanded (12.0f, 9.0f);
    const juce::Point<float> p (static_cast<float> (x), static_cast<float> (y));
    return box.contains (p) || (count == 3 && captionArea().expanded (4.0f).contains (p));
}

void HeaderMix::showReadout (bool on)
{
    readoutTarget = on ? 1.0f : 0.0f;
    if (! on)
        readoutHoldUntil = juce::Time::getMillisecondCounter() + 700;
    startTimerHz (60);
}

void HeaderMix::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    showReadout (true);
}

void HeaderMix::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
    if (! dragging)
        showReadout (false);
}

void HeaderMix::dragTo (juce::Point<float> where)
{
    const auto c = corners();
    if (count >= 3)
    {
        const auto state = mixgeometry::stateFromShares (mixgeometry::sharesAt (where, c));
        xAttachment.setValueAsPartOfGesture (state.x);
        yAttachment.setValueAsPartOfGesture (state.y);
    }
    else
    {
        // Horizontal only: B's share is how far along A -> B the node is.
        const float f = std::clamp ((where.x - c[0].x) / std::max (1.0f, c[1].x - c[0].x), 0.0f, 1.0f);
        blendAttachment.setValueAsPartOfGesture (static_cast<float> (mixgeometry::blendForShare (f)));
    }
}

void HeaderMix::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        if (onModulationMenu != nullptr)
            onModulationMenu();
        return;
    }
    pressedCaption = count == 3 && captionArea().expanded (4.0f).contains (e.position);
    if (pressedCaption)
        return;
    dragging = true;
    if (count >= 3)
    {
        xAttachment.beginGesture();
        yAttachment.beginGesture();
    }
    else
        blendAttachment.beginGesture();
    showReadout (true);
    dragTo (e.position);
}

void HeaderMix::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging)
        dragTo (e.position);
}

void HeaderMix::mouseUp (const juce::MouseEvent& e)
{
    if (std::exchange (pressedCaption, false))
    {
        if (! e.mouseWasDraggedSinceMouseDown() && onOpenMix != nullptr)
            onOpenMix();
        return;
    }
    if (! std::exchange (dragging, false))
        return;
    if (count >= 3)
    {
        xAttachment.endGesture();
        yAttachment.endGesture();
    }
    else
        blendAttachment.endGesture();
    if (! hovering)
        showReadout (false);
}

void HeaderMix::mouseDoubleClick (const juce::MouseEvent&)
{
    // Equal shares: the middle of the line, the centre of the triangle.
    if (count >= 3)
    {
        xAttachment.setValueAsCompleteGesture (0.5f);
        yAttachment.setValueAsCompleteGesture (1.0f / 3.0f);
    }
    else if (count == 2)
        blendAttachment.setValueAsCompleteGesture (0.5f);
}

void HeaderMix::paint (juce::Graphics& g)
{
    using namespace design;
    if (count < 2 && opacity <= 0.0f)
        return;
    const auto c = corners();
    const auto line = colour::text.withAlpha (0.5f);
    // Hairline geometry: the base always, the sides drawn in as C arrives.
    g.setColour (line);
    g.drawLine ({ c[0], c[1] }, 1.2f);
    if (morph > 0.0f)
    {
        g.setColour (line.withMultipliedAlpha (morph));
        g.drawLine ({ c[0], c[2] }, 1.0f);
        g.drawLine ({ c[1], c[2] }, 1.0f);
    }
    // MIX above it all (quiet micro caption; with three a click opens the large mix).
    g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.14f));
    g.setColour (colour::textSecondary.withAlpha (count == 3 && isMouseOver() ? 1.0f : 0.85f));
    g.drawText ("MIX", captionArea(), juce::Justification::centred, false);

    // The layers' letters at the corners, a breath of each identity in them.
    g.setFont (fonts::make (11.0f, fonts::Weight::regular, 0.04f));
    auto letter = [&] (int index, juce::Rectangle<float> r, juce::Justification j, float alpha) {
        const int l = slots[static_cast<std::size_t> (index)];
        g.setColour (colour::textSecondary.interpolatedWith (colour::identity (l).badgeBottom, 0.35f).withMultipliedAlpha (alpha));
        g.drawText (InstrumentHost::layerName (l), r, j, false);
    };
    letter (0, { c[0].x - 16.0f, c[0].y - 7.0f, 12.0f, 14.0f }, juce::Justification::centredRight, 1.0f);
    letter (1, { c[1].x + 4.0f, c[1].y - 7.0f, 12.0f, 14.0f }, juce::Justification::centredLeft, 1.0f);
    if (morph > 0.0f)
        letter (2, { c[2].x + 5.0f, c[2].y - 4.0f, 12.0f, 12.0f }, juce::Justification::centredLeft, morph);

    // Modulated: the range the routes take the mix through, a thicker stretch of the line in
    // the source's colour (the knobs' halo, here).
    const auto modColour = modui::sourceColour (std::max (0, modSource));
    if (modulated && count < 3 && modRange.getLength() > 0.0f)
    {
        const auto a = c[0] + (c[1] - c[0]) * modRange.getStart(), b = c[0] + (c[1] - c[0]) * modRange.getEnd();
        g.setColour (modColour.withAlpha (0.7f));
        g.drawLine ({ a, b }, 2.6f);
    }
    // A source being dragged: a ring round the MIX (strong when a drop would land on it).
    if (modDragSource >= 0)
    {
        const auto ring = juce::Rectangle<float> (c[0], c[1]).getUnion (juce::Rectangle<float> (c[2], c[2])).expanded (21.0f, 8.0f);
        g.setColour (modui::sourceColour (modDragSource).withAlpha (modDragOver ? 0.85f : 0.35f));
        g.drawRoundedRectangle (ring, 6.0f, modDragOver ? 1.6f : 1.0f);
    }

    // The node: a small cream cap with a fine dark rim and a contact shadow.
    const auto node = nodePosition();
    juce::Path disc;
    disc.addEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (node));
    design::CachedShadow (juce::Colour (0x40302418), 2, { 0, 1 }).drawForPath (g, disc);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffefbf6), node.x, node.y - 4.0f, juce::Colour (0xffdcd3c6), node.x, node.y + 4.0f, false));
    g.fillPath (disc);
    g.setColour (colour::text.withAlpha (0.75f));
    g.strokePath (disc, juce::PathStrokeType (0.9f));
    // ... and where the modulation has the mix now: a hollow ring that moves (the solid node
    // stays where the musician set it).
    if (modulated)
    {
        g.setColour (modColour);
        g.drawEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre (liveNodePosition()), 1.4f);
    }

    // While hovered or dragged: each layer's share, small and tabular, under the line.
    if (readout > 0.0f)
    {
        const auto share = shownShares();
        juce::String text;
        for (int i = 0; i < std::min (count, 3); ++i)
            text << (i > 0 ? "   " : "") << InstrumentHost::layerName (slots[static_cast<std::size_t> (i)]) << " "
                 << juce::roundToInt (100.0 * share[static_cast<std::size_t> (i)]);
        g.setFont (fonts::make (10.5f, fonts::Weight::regular, 0.02f));
        g.setColour (colour::textSecondary.withAlpha (0.9f * readout));
        g.drawText (text, juce::Rectangle<float> (0.0f, baseY + 8.0f, static_cast<float> (getWidth()), 13.0f), juce::Justification::centred, false);
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
        explicit MixPopup (InstrumentHost& p)
            : MiniPanel ("MIX", "HOW MUCH OF EACH LAYER YOU HEAR"), processor (p), triangle (p.apvts())
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
                g.setFont (fonts::make (10.5f, fonts::Weight::medium));
                g.drawText (InstrumentHost::layerName (l), badge, juce::Justification::centred, false);
                row.removeFromLeft (8.0f);
                const auto percent = juce::String (juce::roundToInt (100.0 * share[static_cast<std::size_t> (l)])) + " %";
                g.setFont (fonts::make (12.0f, fonts::Weight::medium));
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

        InstrumentHost& processor;
        TriangleMix triangle;
        std::array<juce::String, 3> names;
        std::array<double, 3> shown {};
        juce::Rectangle<int> readout;
    };
}

std::unique_ptr<MiniPanel> createMixPopup (InstrumentHost& processor)
{
    return std::make_unique<MixPopup> (processor);
}

//==============================================================================
PresetBar::PresetBar (InstrumentHost& p) : processor (p)
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
            if (onOpenLibrary != nullptr)
            {
                menu.addItem (juce::String::fromUTF8 ("Open Library\xe2\x80\xa6"), [safe] {
                    if (safe != nullptr && safe->onOpenLibrary != nullptr)
                        safe->onOpenLibrary();
                });
                menu.addSeparator();
            }
            // Loading replaced the whole patch: the one before it is a click away.
            if (processor.canRestorePreviousState())
            {
                const auto what = processor.previousStateDescription().fromFirstOccurrenceOf ("Before ", false, false);
                const auto label = what.isEmpty() || what == "restoring" ? juce::String ("Back to the previous patch") : "Back to the patch before " + what;
                menu.addItem (label, [safe, opened] { if (safe != nullptr) safe->processor.restorePreviousState(), opened(); });
                menu.addSeparator();
            }
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

SavePresetButton::SavePresetButton() : juce::Button ("Save preset")
{
    setTooltip ("Save preset");
    setTitle ("Save preset");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void SavePresetButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const float k = r.getHeight() / 48.0f;
    if (! skin::drawnBySurface())   // ceramic: the housing draws the key
        draw::raised (g, r.reduced (0.5f), 9.0f * k, colour::cardTop, colour::cardBottom, 0.9f);
    // The header's icons share one stroke (1.5 px at the reference size) and one ink.
    icons::draw (g, icons::Kind::cassette, r.withSizeKeepingCentre (27.0f * k, 27.0f * k).translated (0.0f, down ? 0.5f : 0.0f),
                 colour::text.withAlpha (highlighted || down ? 0.85f : 0.6f), 1.5f * k);
}

void PresetBar::paint (juce::Graphics& g)
{
    using namespace design;
    const auto r = getLocalBounds().toFloat();
    const float k = r.getHeight() / 48.0f;
    if (! skin::drawnBySurface())   // ceramic: the housing draws the key
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
                     b.withSizeKeepingCentre (24.0f * k, 24.0f * k), colour::text.withAlpha (hover == part ? 0.85f : 0.6f), 1.5f * k);
        // A hairline on the pixel grid between the arrow and the name.
        g.setColour (colour::divider);
        g.fillRect (juce::Rectangle<float> (std::round (part == Part::previous ? b.getRight() : b.getX()), r.getY() + 9.0f * k, 1.0f, r.getHeight() - 18.0f * k));
    }
    const auto heart = partBounds (Part::favourite);
    // A favourite: filled in the accent; at rest the quiet outline, the accent outline on hover.
    icons::draw (g, favourite ? icons::Kind::heartFilled : icons::Kind::heart, heart.withSizeKeepingCentre (24.0f * k, 24.0f * k),
                 favourite || hover == Part::favourite ? colour::accent : colour::text.withAlpha (0.6f), 1.5f * k);
    g.setColour (hover == Part::name ? juce::Colours::black : colour::text);
    g.setFont (type::preset (k));
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
// A D S R as small knobs centred at 71, 191, 314 and 437 - kept in proportion now that the
// panel gave room to the sixth macro (450 wide).
void EnvelopePanel::resized()
{
    const int w = getWidth(), h = getHeight();
    graph = juce::Rectangle<int> (12, 26, w - 24, std::max (40, h - 116));   // 70 in the 186 panel; taller panels give it the room
    static constexpr std::array<float, 4> shares { 71.0f / 506.0f, 191.0f / 506.0f, 314.0f / 506.0f, 437.0f / 506.0f };
    for (std::size_t i = 0; i < knobs.size(); ++i)
        knobs[i]->setBounds (juce::Rectangle<int> (std::min (100, w / 5), 87).withCentre ({ juce::roundToInt (shares[i] * static_cast<float> (w)), h - 45 }));
}

void EnvelopePanel::paint (juce::Graphics& g)
{
    using namespace design;
    if (showsTitle)
    {
        g.setColour (colour::text.withAlpha (0.9f));
        g.setFont (type::panelHeader());
        g.drawText ("AMP ENVELOPE", juce::Rectangle<float> (16.0f, 1.0f, 300.0f, 22.0f), juce::Justification::centredLeft, false);
    }

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

void Wheel::setAssignable (juce::Colour colour)
{
    assignable = true;
    socket = colour;
    setTooltip ("Modulation wheel. Drag MOD onto a control to modulate it");
    repaint();
}

juce::Rectangle<float> Wheel::socketArea() const
{
    // The caption's line under the wheel: the word and its socket.
    return { 0.0f, 88.0f, static_cast<float> (getWidth()), static_cast<float> (getHeight()) - 88.0f };
}

void Wheel::mouseMove (const juce::MouseEvent& e)
{
    const bool over = assignable && socketArea().contains (e.position);
    if (over != overSocket)
    {
        overSocket = over;
        setMouseCursor (over ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::UpDownResizeCursor);
        repaint();
    }
}

void Wheel::mouseDown (const juce::MouseEvent& e)
{
    cabling = assignable && socketArea().contains (e.position);
    if (cabling)
        return;
    dragging = true;
    dragStart = value;
}

void Wheel::mouseDrag (const juce::MouseEvent& e)
{
    if (cabling)
    {
        if (e.getDistanceFromDragStart() >= 3 && onSourceDrag != nullptr)
            onSourceDrag (e.getScreenPosition());
        return;
    }
    const float travel = slot().getHeight() * (springBack ? 0.5f : 1.0f);
    value = std::clamp (dragStart - static_cast<float> (e.getDistanceFromDragStartY()) / std::max (1.0f, travel), springBack ? -1.0f : 0.0f, 1.0f);
    if (onMove != nullptr)
        onMove (value);
    repaint();
}

void Wheel::mouseUp (const juce::MouseEvent& e)
{
    if (cabling)
    {
        cabling = false;
        if (e.getDistanceFromDragStart() >= 3 && onSourceDrop != nullptr)
            onSourceDrop (e.getScreenPosition());
        return;
    }
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
    g.setColour (colour::text.withAlpha (0.8f));
    const auto font = fonts::make (12.5f, fonts::Weight::medium, 0.06f);
    g.setFont (font);
    if (! assignable)
    {
        g.drawText (caption, juce::Rectangle<float> (-12.0f, 88.0f, static_cast<float> (getWidth()) + 24.0f, 18.0f), juce::Justification::centred, false);
        return;
    }
    // MOD and its patch socket (the sources' assignment glyph), together centred under the wheel.
    const float textWidth = juce::GlyphArrangement::getStringWidth (font, caption);
    const float left = 0.5f * (static_cast<float> (getWidth()) - (textWidth + 13.0f));
    g.drawText (caption, juce::Rectangle<float> (left, 88.0f, textWidth + 1.0f, 18.0f), juce::Justification::centredLeft, false);
    const juce::Point<float> c (left + textWidth + 8.5f, 97.0f);
    g.setColour (socket.withAlpha (overSocket ? 0.95f : 0.6f));
    g.drawEllipse (juce::Rectangle<float> (8.4f, 8.4f).withCentre (c), 1.25f);
    if (inUse)
        g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre (c));
}

void Wheel::setInUse (bool used)
{
    if (used != inUse)
    {
        inUse = used;
        repaint();
    }
}

} // namespace osp::plugin
