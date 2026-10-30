#include "Design.h"
#include "CeramicSkin.h"
#include "SpriteKnobRenderer.h"

#include <cstring>
#include <optional>
#include <unordered_map>

namespace osp::plugin::design
{

namespace
{
    struct ShadowMask
    {
        juce::Image image;
        juce::Rectangle<int> area;   ///< relative to the path's integer origin
    };

    std::unordered_map<std::uint64_t, ShadowMask>& shadowMasks()
    {
        static std::unordered_map<std::uint64_t, ShadowMask> masks;
        return masks;
    }

    void mix (std::uint64_t& h, std::uint64_t v) noexcept
    {
        h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }

    void mix (std::uint64_t& h, float v) noexcept
    {
        std::uint32_t bits = 0;
        std::memcpy (&bits, &v, sizeof (bits));
        mix (h, static_cast<std::uint64_t> (bits));
    }
}

int CachedShadow::cachedMaskCount()
{
    return static_cast<int> (shadowMasks().size());
}

void CachedShadow::drawForPath (juce::Graphics& g, const juce::Path& path) const
{
    // Exactly juce::DropShadow::drawForPath's mask, for the whole area (not just the clip:
    // that is what makes it reusable), on the path's own integer grid.
    const auto bounds = path.getBounds().getSmallestIntegerContainer();
    const auto origin = bounds.getPosition();
    std::uint64_t key = 0x5348414430ull;
    mix (key, static_cast<std::uint64_t> (radius));
    mix (key, static_cast<std::uint64_t> (static_cast<std::uint32_t> (offset.x)) << 32 | static_cast<std::uint32_t> (offset.y));
    mix (key, static_cast<std::uint64_t> (path.isUsingNonZeroWinding()));
    for (juce::Path::Iterator it (path); it.next();)
    {
        mix (key, static_cast<std::uint64_t> (it.elementType));
        const float ox = static_cast<float> (origin.x), oy = static_cast<float> (origin.y);
        mix (key, it.x1 - ox);
        mix (key, it.y1 - oy);
        if (it.elementType == juce::Path::Iterator::quadraticTo || it.elementType == juce::Path::Iterator::cubicTo)
        {
            mix (key, it.x2 - ox);
            mix (key, it.y2 - oy);
        }
        if (it.elementType == juce::Path::Iterator::cubicTo)
        {
            mix (key, it.x3 - ox);
            mix (key, it.y3 - oy);
        }
    }
    auto& masks = shadowMasks();
    auto found = masks.find (key);
    if (found == masks.end())
    {
        if (masks.size() > 256)   // animations make new shapes: keep the cache bounded
            masks.clear();
        ShadowMask mask;
        mask.area = (bounds.withPosition (0, 0) + offset).expanded (radius + 1);
        if (mask.area.getWidth() <= 2 || mask.area.getHeight() <= 2)
            return;
        mask.image = juce::Image (juce::Image::SingleChannel, mask.area.getWidth(), mask.area.getHeight(), true,
                                  *g.getInternalContext().getPreferredImageTypeForTemporaryImages());
        mask.image.setBackupEnabled (false);
        {
            juce::Graphics g2 (mask.image);
            g2.setColour (juce::Colours::white);
            g2.fillPath (path, juce::AffineTransform::translation (static_cast<float> (offset.x - mask.area.getX() - origin.x),
                                                                   static_cast<float> (offset.y - mask.area.getY() - origin.y)));
        }
        mask.image.getPixelData()->applySingleChannelBoxBlurEffect (radius);
        found = masks.emplace (key, std::move (mask)).first;
    }
    const auto& mask = found->second;
    g.setColour (colour);
    g.drawImageAt (mask.image, origin.x + mask.area.getX(), origin.y + mask.area.getY(), true);
}

//==============================================================================
// The product's materials (Materials in Design.h)

namespace
{
    std::optional<Materials>& productMaterials()
    {
        static std::optional<Materials> m;
        return m;
    }
    int scopedMaterials = 0;
}

void useMaterials (const Materials& m)
{
    productMaterials() = m;
    if (scopedMaterials == 0)
        detail::materials = m;
}

ScopedMaterials::ScopedMaterials (const Materials& m) : previous (detail::materials)
{
    ++scopedMaterials;
    detail::materials = m;
}

ScopedMaterials::~ScopedMaterials()
{
    --scopedMaterials;
    // The product's own once no scope is left (it may have set them meanwhile).
    detail::materials = scopedMaterials == 0 && productMaterials().has_value() ? *productMaterials() : previous;
}

}

namespace osp::plugin::design::draw
{

namespace
{
    juce::Colour average (juce::Colour a, juce::Colour b) { return a.interpolatedWith (b, 0.5f); }

    template <size_t n>
    int nearest (const int (&values)[n], float x)
    {
        int best = values[0];
        for (const int v : values)
            if (std::abs (static_cast<float> (v) - x) < std::abs (static_cast<float> (best) - x))
                best = v;
        return best;
    }

    /** The ceramic piece for a raised surface: the cards and panels at their rendered sizes,
        smaller panels at the nearest rendered corner. */
    juce::String raisedPiece (juce::Rectangle<float> r, float radius, bool& card)
    {
        card = std::abs (radius - layout::cardRadius) < 0.5f;
        if (card)
        {
            static constexpr int widths[] = { 451, 683, 1379 };
            return "card-" + juce::String (nearest (widths, r.getWidth()));
        }
        if (std::abs (radius - layout::panelRadius) < 0.5f && r.getWidth() > 600.0f)
            return r.getHeight() > 196.0f ? "panel-macro" : "panel-arp";
        static constexpr int radii[] = { 7, 9, 10, 12 };
        return "raised-r" + juce::String (nearest (radii, radius));
    }

    /** The face a piece shows for a requested colour: as rendered for the colour its role was
        designed with, proportionally lighter or darker for another. */
    juce::Colour faceFor (juce::Colour rendered, juce::Colour designed, juce::Colour requested)
    {
        auto channel = [] (juce::uint8 r, juce::uint8 d, juce::uint8 q) {
            return static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (static_cast<float> (r) * static_cast<float> (q) / static_cast<float> (std::max<juce::uint8> (1, d)))));
        };
        if (requested.getARGB() == designed.getARGB() || ! rendered.isOpaque())
            return {};
        return juce::Colour (channel (rendered.getRed(), designed.getRed(), requested.getRed()), channel (rendered.getGreen(), designed.getGreen(), requested.getGreen()),
                             channel (rendered.getBlue(), designed.getBlue(), requested.getBlue()));
    }
}

juce::Colour surround()
{
    if (auto* pieces = skin::active())
        if (const auto* p = pieces->find ("housing"); p != nullptr && p->edge.isOpaque())
            return p->edge;
    return colour::backdropTop;
}

void housing (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    using namespace colour;
    // Ceramic: the rendered surround, housing and teak cheeks (taller with the arpeggiator's
    // editor: the nine-slice stretches the middle).
    if (auto* pieces = skin::active(); pieces != nullptr && pieces->draw (g, "housing", bounds))
        return;
    // The surround: a warm surface, a touch darker at the top left.
    g.setGradientFill (juce::ColourGradient (backdropTop, bounds.getX(), bounds.getY(), backdropBottom, bounds.getRight(), bounds.getBottom(), false));
    g.fillRect (bounds);

    // The body grows with the instrument (the arpeggiator's editor adds height below the macros).
    // ... and the modulation bay adds width on the right: the same margins to the edges.
    const auto r = layout::housing.withBottom (bounds.getBottom() - (height - layout::housing.getBottom()))
                       .withRight (bounds.getRight() - (width - layout::housing.getRight()));
    juce::Path body;
    body.addRoundedRectangle (r, layout::housingRadius);
    // One broad, quiet shadow (a physical object resting on the surface), then a close one.
    CachedShadow (juce::Colour (0x2a3a2a1c), 26, { 0, 10 }).drawForPath (g, body);
    CachedShadow (juce::Colour (0x22302418), 4, { 0, 2 }).drawForPath (g, body);
    // Powder-coated body: barely brighter at the top, warmer at the foot.
    g.setGradientFill (juce::ColourGradient (housingTop, 0.0f, r.getY(), housingBottom, 0.0f, r.getBottom(), false));
    g.fillPath (body);
    // A faint lit rim along the top and left, a darker one at the foot.
    {
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (body);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), 0.0f, r.getY(), juce::Colours::white.withAlpha (0.0f), 0.0f, r.getY() + 6.0f, false));
        g.fillRect (r.withHeight (6.0f));
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.25f), r.getX(), 0.0f, juce::Colours::white.withAlpha (0.0f), r.getX() + 5.0f, 0.0f, false));
        g.fillRect (r.withWidth (5.0f));
    }
    g.setColour (housingRim.withAlpha (0.55f));
    g.strokePath (body, juce::PathStrokeType (1.0f));
}

void raised (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour top, juce::Colour bottom, float shadow)
{
    using namespace colour;
    if (auto* pieces = skin::active())
    {
        bool card = false;
        const auto name = raisedPiece (r, radius, card);
        if (const auto* p = pieces->find (name))
        {
            const auto designed = card ? average (cardTop, cardBottom) : average (panelTop, panelBottom);
            if (pieces->draw (g, name, r, 1.0f, faceFor (p->face, designed, average (top, bottom))))
                return;
        }
    }
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    if (shadow > 0.0f)
    {
        CachedShadow (juce::Colour (0x1e3a2a1c).withMultipliedAlpha (shadow), juce::roundToInt (9.0f * shadow), { 0, juce::roundToInt (3.0f * shadow) }).drawForPath (g, shape);
        CachedShadow (juce::Colour (0x1c302418).withMultipliedAlpha (shadow), 2, { 0, 1 }).drawForPath (g, shape);
    }
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    // Edges: lit along the top, shaded along the bottom (the hairline sits in between).
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    g.setColour (edgeLight.withAlpha (0.9f));
    juce::Path topEdge;
    topEdge.addRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    g.strokePath (topEdge, juce::PathStrokeType (1.2f));
    g.setColour (edgeShade.withAlpha (0.35f));
    juce::Path bottomEdge;
    bottomEdge.addRoundedRectangle (r.translated (0.0f, -1.0f), radius);
    g.strokePath (bottomEdge, juce::PathStrokeType (1.0f));
    g.setColour (hairline.withAlpha (0.8f));
    g.strokePath (shape, juce::PathStrokeType (0.8f));
}

void raisedChildren (juce::Graphics& g, juce::Component& parent)
{
    if (! skin::drawnBySurface())
        return;
    auto* pieces = skin::active();
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (auto* child : parent.getChildren())
        if (const auto* face = dynamic_cast<const skin::SurfaceFace*> (child); face != nullptr && child->isVisible())
        {
            // As the face fades and moves with its component (a layer settling in).
            bool card = false;
            const auto r = child->getBounds().toFloat();
            const auto name = raisedPiece (r, face->surfaceRadius(), card);
            pieces->draw (g, name, r, child->getAlpha(), {}, animator.isAnimating (child));
        }
}

void well (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
{
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    auto* pieces = skin::active();
    static constexpr int bezels[] = { 8, 9, 10, 12 };
    // Ceramic: the display sits in a 1.6 px lip cut into the surface (rendered).
    if (pieces != nullptr)
        pieces->draw (g, "bezel-r" + juce::String (nearest (bezels, radius)), r.expanded (1.6f));
    // The rim catches light from above (kept in both looks: the approved render has it).
    {
        juce::Path lip;
        lip.addRoundedRectangle (r.expanded (1.0f), radius + 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.55f));
        g.strokePath (lip.createPathWithRoundedCorners (0.0f), juce::PathStrokeType (1.0f), juce::AffineTransform::translation (0.0f, 1.0f));
    }
    g.setColour (fill);
    g.fillPath (shape);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    // Recessed: an inner shadow under the top edge, a faint reflection near the bottom.
    // A touch deeper: the shade under the top edge reaches a little further, the sides
    // darken faintly, the lower lip catches a little more light; a clean dark edge.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.55f), 0.0f, r.getY(), juce::Colours::black.withAlpha (0.0f), 0.0f, r.getY() + 13.0f, false));
    g.fillRect (r.withHeight (13.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.18f), r.getX(), 0.0f, juce::Colours::black.withAlpha (0.0f), r.getX() + 7.0f, 0.0f, false));
    g.fillRect (r.withWidth (7.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.12f), r.getRight(), 0.0f, juce::Colours::black.withAlpha (0.0f), r.getRight() - 6.0f, 0.0f, false));
    g.fillRect (r.withLeft (r.getRight() - 6.0f));
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.0f), 0.0f, r.getBottom() - 14.0f, juce::Colours::white.withAlpha (0.05f), 0.0f, r.getBottom(), false));
    g.fillRect (r.withTop (r.getBottom() - 14.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (r.reduced (radius, 0.0f).withTop (r.getBottom() - 2.0f).withHeight (1.0f));
    g.setColour (colour::wellRim);
    g.strokePath (shape, juce::PathStrokeType (1.2f));
}

void button (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool active, bool hover, juce::Colour activeColour)
{
    using namespace colour;
    if (skin::active() != nullptr)
    {
        // Ceramic: an A1 key. On in the accent: the warm face; on in another colour (a
        // macro's own): pressed, with that colour's fine rim.
        const bool accentOn = active && activeColour.getARGB() == accent.getARGB();
        const auto state = accentOn ? skin::KeyState::active : active ? skin::KeyState::pressed : hover ? skin::KeyState::hover : skin::KeyState::normal;
        if (skin::key (g, r, radius, skin::KeyRole::generic, state))
        {
            if (active && ! accentOn)
            {
                g.setColour (activeColour.withAlpha (0.7f));
                g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
            }
            return;
        }
    }
    juce::Path shape;
    shape.addRoundedRectangle (r, radius);
    CachedShadow (juce::Colour (0x24302418), 3, { 0, 1 }).drawForPath (g, shape);
    if (active)
    {
        // On: pressed in - a slightly deeper warm face, a soft shade under its top edge and a
        // fine accent rim (the state is a small accent, never a coloured block).
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff1e3d6), 0.0f, r.getY(), juce::Colour (0xfff6ebe1), 0.0f, r.getBottom(), false));
        g.fillPath (shape);
        {
            juce::Graphics::ScopedSaveState state (g);
            g.reduceClipRegion (shape);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x22502a10), 0.0f, r.getY(), juce::Colour (0x00502a10), 0.0f, r.getY() + 0.3f * r.getHeight(), false));
            g.fillRect (r);
        }
        g.setColour (activeColour.withAlpha (0.7f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
        return;
    }
    g.setGradientFill (juce::ColourGradient (hover ? buttonTop.brighter (0.2f) : buttonTop, 0.0f, r.getY(), buttonBottom, 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    juce::Graphics::ScopedSaveState state (g);
    g.reduceClipRegion (shape);
    juce::Path lit;
    lit.addRoundedRectangle (r.translated (0.0f, 1.0f), radius);
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.strokePath (lit, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colour (0xffcbc2b4));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
}

} // namespace osp::plugin::design::draw

namespace osp::plugin::design::colour
{
namespace
{
    struct Lab
    {
        float l, a, b;
    };
    float toLinear (float c) { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
    float toGamma (float c) { return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow (c, 1.0f / 2.4f) - 0.055f; }
    Lab toOklab (juce::Colour c)
    {
        const float r = toLinear (c.getFloatRed()), g = toLinear (c.getFloatGreen()), b = toLinear (c.getFloatBlue());
        const float l = std::cbrt (0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
        const float m = std::cbrt (0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
        const float s = std::cbrt (0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
        return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s, 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s };
    }
    juce::Colour fromOklab (Lab c)
    {
        const float l = c.l + 0.3963377774f * c.a + 0.2158037573f * c.b;
        const float m = c.l - 0.1055613458f * c.a - 0.0638541728f * c.b;
        const float s = c.l - 0.0894841775f * c.a - 1.2914855480f * c.b;
        const float l3 = l * l * l, m3 = m * m * m, s3 = s * s * s;
        auto channel = [] (float v) { return static_cast<juce::uint8> (juce::roundToInt (255.0f * juce::jlimit (0.0f, 1.0f, toGamma (v)))); };
        return juce::Colour (channel (4.0767416621f * l3 - 3.3077115913f * m3 + 0.2309699292f * s3),
                             channel (-1.2684380046f * l3 + 2.6097574011f * m3 - 0.3413193965f * s3),
                             channel (-0.0041960863f * l3 - 0.7034186147f * m3 + 1.7076147010f * s3));
    }
}

juce::Colour reimagined (float position)
{
    // Built once: 256 steps of the continuum (paint just looks them up).
    static const std::array<juce::Colour, 256> table = [] {
        struct Stop
        {
            float at;
            juce::Colour colour;
        };
        static const Stop stops[] = { { 0.0f, juce::Colour (0xffb96e55) }, { 0.18f, juce::Colour (0xffcf994e) }, { 0.35f, juce::Colour (0xffbfaa5a) },
                                      { 0.5f, juce::Colour (0xff7f9270) }, { 0.65f, juce::Colour (0xff668f8a) }, { 0.82f, juce::Colour (0xff748b9d) },
                                      { 1.0f, juce::Colour (0xff948399) } };
        std::array<juce::Colour, 256> t;
        for (std::size_t i = 0; i < t.size(); ++i)
        {
            const float x = static_cast<float> (i) / static_cast<float> (t.size() - 1);
            std::size_t k = 1;
            while (k + 1 < std::size (stops) && x > stops[k].at)
                ++k;
            const auto& a = stops[k - 1];
            const auto& b = stops[k];
            const float u = juce::jlimit (0.0f, 1.0f, (x - a.at) / (b.at - a.at));
            const auto la = toOklab (a.colour), lb = toOklab (b.colour);
            t[i] = fromOklab ({ la.l + u * (lb.l - la.l), la.a + u * (lb.a - la.a), la.b + u * (lb.b - la.b) });
        }
        return t;
    }();
    const auto index = static_cast<std::size_t> (juce::roundToInt (juce::jlimit (0.0f, 1.0f, position) * 255.0f));
    return table[index];
}
} // namespace osp::plugin::design::colour

namespace osp::plugin::design::draw
{

void knob (juce::Graphics& g, juce::Point<float> c, float r, float position, const KnobStyle& style)
{
    using namespace colour;
    position = juce::jlimit (0.0f, 1.0f, position);
    auto at = [c] (float radius, float angle) { return c + juce::Point<float> (std::sin (angle), -std::cos (angle)) * radius; };
    const float angle = style.startAngle + position * (style.endAngle - style.startAngle);

    // Ticks: small dark dots around the travel.
    if (style.ticks)
        for (int i = 0; i < style.tickCount; ++i)
        {
            const float a = style.startAngle + (style.endAngle - style.startAngle) * static_cast<float> (i) / static_cast<float> (style.tickCount - 1);
            const auto p = at (style.tickRadius * r, a);
            const float d = std::max (1.6f, 0.07f * r);
            g.setColour (tick.withAlpha (style.enabled ? 0.85f : 0.35f));
            g.fillEllipse (p.x - 0.5f * d, p.y - 0.5f * d, d, d);
        }

    // Track and value arc, hugging the rim.
    const bool thin = style.arcWidth > 0.0f;
    const float arcRadius = style.arcRadius * r, arcWidth = thin ? style.arcWidth : std::max (2.4f, 0.12f * r);
    {
        // The track: a soft grey ring, shaded on its inner edge.
        juce::Path track;
        track.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, style.startAngle, style.endAngle, true);
        // (Graphite: a dark groove cut into the well instead, no light on its edge.)
        g.setColour (style.graphite ? juce::Colour (0xff383533) : juce::Colour (0xffc3b9aa));
        g.strokePath (track, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (style.graphite ? 0.0f : 0.35f));
        juce::Path lit;
        lit.addCentredArc (c.x, c.y, arcRadius + 0.3f * arcWidth, arcRadius + 0.3f * arcWidth, 0.0f, style.startAngle, style.endAngle, true);
        g.strokePath (lit, juce::PathStrokeType (0.35f * arcWidth));
        const float from = style.bipolar ? 0.5f * (style.startAngle + style.endAngle) : style.startAngle;
        if (style.spectral && style.enabled)
        {
            // REIMAGINED: the arc grows from the rust origin through the continuum only as
            // far as the value (never the whole spectrum at once); at 0 a small rust tick.
            const float span = style.endAngle - style.startAngle;
            const float reach = std::max (position, 0.018f);
            const int segments = std::max (2, juce::roundToInt (96.0f * reach));
            const float overlap = 0.35f * span * reach / static_cast<float> (segments);
            for (int i = 0; i < segments; ++i)
            {
                const float t0 = reach * static_cast<float> (i) / static_cast<float> (segments);
                const float t1 = reach * static_cast<float> (i + 1) / static_cast<float> (segments);
                juce::Path piece;
                piece.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, style.startAngle + t0 * span, std::min (style.startAngle + t1 * span + overlap, style.startAngle + reach * span), true);
                const bool end = i == 0 || i == segments - 1;
                g.setColour (colour::reimagined (0.5f * (t0 + t1)).withMultipliedBrightness (style.spectralLift));
                g.strokePath (piece, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, end ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt));
            }
        }
        else if (std::abs (angle - from) > 0.01f && style.enabled)
        {
            // The value arc deepens towards where it starts and is brightest at the value.
            juce::Path value;
            value.addCentredArc (c.x, c.y, arcRadius, arcRadius, 0.0f, std::min (from, angle), std::max (from, angle), true);
            const auto p0 = at (arcRadius, from), p1 = at (arcRadius, angle);
            if (thin)   // the macro identity: one flat, slightly translucent colour, no glow
                g.setColour (style.arc);
            else
                g.setGradientFill (juce::ColourGradient (style.arc.darker (0.55f), p0.x, p0.y, style.arc.brighter (0.12f), p1.x, p1.y, false));
            g.strokePath (value, juce::PathStrokeType (arcWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // A rendered cap: its own light, shadow and indicator, at the same radius.
    if (style.sprite != nullptr && style.sprite->isValid() && ! style.graphite)
    {
        style.sprite->draw (g, c, r, angle, style.enabled ? 1.0f : 0.45f);
        return;
    }
    if (style.graphite)
    {
        // Graphite: the same body on a dark well. A darker rim lit at the top left, a charcoal
        // cap with a quiet sheen, a cream engraved pointer (it must read on dark).
        const auto body = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
        juce::Path disc;
        disc.addEllipse (body);
        CachedShadow (juce::Colour (0x8c000000), juce::roundToInt (0.22f * r) + 2, { 0, juce::roundToInt (0.09f * r) + 1 }).drawForPath (g, disc);
        // (A muted bronze rim, the cap warm charcoal.)
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff9a7454), c.x - 0.7f * r, c.y - 0.8f * r, juce::Colour (0xff221a15), c.x + 0.6f * r, c.y + 0.9f * r, false));
        g.fillEllipse (body);
        const float capR = 0.86f * r;
        const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4b4642), c.x - 0.35f * r, c.y - 0.55f * r, juce::Colour (0xff24211f), c.x + 0.5f * r, c.y + 0.85f * r, true));
        g.fillEllipse (cap);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.14f), c.x, cap.getY(), juce::Colours::white.withAlpha (0.0f), c.x, c.y, false));
        g.fillEllipse (cap.reduced (0.08f * r).withTrimmedBottom (0.7f * r));
        const auto p0 = at (style.pointerFrom * r, angle), p1 = at (style.pointerTo * r, angle);
        g.setColour (juce::Colour (0xffeee6da).withAlpha (style.enabled ? 0.95f : 0.4f));
        g.drawLine ({ p0, p1 }, std::max (1.6f, 0.08f * r));
        return;
    }

    // Body: contact shadow, bevelled rim (lit top-left, shaded bottom-right), lit cap.
    const auto body = juce::Rectangle<float> (2.0f * r, 2.0f * r).withCentre (c);
    {
        juce::Path disc;
        disc.addEllipse (body);
        // Seated, not floating: a slightly firmer contact shadow close under the body.
        CachedShadow (juce::Colour (0x46302418), juce::roundToInt (0.22f * r) + 2, { 0, juce::roundToInt (0.09f * r) + 1 }).drawForPath (g, disc);
        CachedShadow (juce::Colour (0x26302418), juce::roundToInt (0.06f * r) + 1, { 0, 1 }).drawForPath (g, disc);
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffdfaf4), c.x - 0.7f * r, c.y - 0.8f * r, knobRim, c.x + 0.6f * r, c.y + 0.9f * r, false));
    g.fillEllipse (body);
    const float capR = 0.88f * r;
    const auto cap = juce::Rectangle<float> (2.0f * capR, 2.0f * capR).withCentre (c);
    // The groove between rim and cap.
    g.setColour (juce::Colour (0x58665a48));   // a crisper rim
    g.drawEllipse (cap.expanded (0.6f), std::max (0.8f, 0.03f * r));
    juce::ColourGradient capFill (knobCapTop, c.x - 0.35f * r, c.y - 0.55f * r, knobCapBottom, c.x + 0.5f * r, c.y + 0.85f * r, true);
    g.setGradientFill (capFill);
    g.fillEllipse (cap);
    // A soft highlight across the top of the cap.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.55f), c.x, cap.getY(), juce::Colours::white.withAlpha (0.0f), c.x, c.y, false));
    g.fillEllipse (cap.reduced (0.08f * r).withTrimmedBottom (0.7f * r));

    // Pointer: a dark groove from near the centre to the cap's edge.
    const auto p0 = at (style.pointerFrom * r, angle), p1 = at (style.pointerTo * r, angle);
    g.setColour (style.pointer.withAlpha (style.enabled ? 1.0f : 0.4f));
    // Engraved rather than painted: a finer, darker mark.
    g.drawLine ({ p0, p1 }, std::max (1.8f, 0.085f * r));
}

void led (juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour c, float glow)
{
    const float r = 0.5f * diameter;
    if (glow > 0.0f)
    {
        juce::ColourGradient halo (c.withAlpha (0.35f * glow), centre.x, centre.y, c.withAlpha (0.0f), centre.x + 2.2f * r, centre.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (4.4f * r, 4.4f * r).withCentre (centre));
    }
    // A small glossy bead: dark rim, bright centre, a highlight up and to the left.
    juce::ColourGradient body (c.brighter (0.5f), centre.x - 0.3f * r, centre.y - 0.35f * r, c.darker (0.55f), centre.x + 0.8f * r, centre.y + 0.9f * r, true);
    g.setGradientFill (body);
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre (centre));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.fillEllipse (juce::Rectangle<float> (0.42f * diameter, 0.32f * diameter).withCentre (centre + juce::Point<float> (-0.18f * diameter, -0.22f * diameter)));
}

} // namespace osp::plugin::design::draw
