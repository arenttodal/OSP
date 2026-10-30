#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace osp::plugin::skin
{

/**
    The instrument's look. Ceramic is the chosen design (docs/design/warm-ceramic-teak-style-guide.md):
    offline-rendered surfaces (tools/blender-assets/skin) under the plugin's own drawing.
    Classic is the vector drawing it replaced, kept for comparison and as the fallback when
    the rendered pieces cannot be read. Looks only: no parameter, no state, nothing the
    audio thread reads. Not saved; OSP_SKIN=classic|ceramic sets it at start-up and
    Cmd+Option+G toggles it.
*/
enum class Look
{
    classic,
    ceramic
};

Look look() noexcept;
void setLook (Look) noexcept;

/**
    The ceramic skin's rendered pieces, read once per process from the embedded pack
    (Resources/skin/ceramic-skin.zip: manifest.json and one image per piece and level).

    A piece is a part rendered at its place in the design canvas with room (`margin`) for
    its shadow. It is drawn as a nine-slice over a target rectangle (the part's own bounds,
    logical px): the corners keep their size, the edges stretch along their length, the
    middle both ways, so at the size it was rendered it is the render itself. For the
    physical scale it is drawn at (the window's scale times the display's), a piece is
    composed once from its nearest level, resampled by area, then kept and drawn 1:1.

    Lightweight by construction: a level is decoded only to compose a size and let go;
    only the composed images on screen are kept (a bounded cache).

    Message thread only (painting); never touched by the audio thread.
*/
class Pieces
{
public:
    enum class Kind
    {
        opaque,     ///< the window's background
        alpha,      ///< a part with its contact shadow
        overlay     ///< a recess: shading and light laid over the surface it is cut into
    };

    struct Level
    {
        float scale = 0.0f;   ///< stored pixels per design px
        int width = 0, height = 0;
        int entry = -1;       ///< zip entry
    };

    struct Piece
    {
        juce::String name;
        Kind kind = Kind::alpha;
        juce::Rectangle<float> rect;     ///< where it was rendered (design px)
        float margin = 0.0f;             ///< room around rect in the image (design px)
        float insets[4] {};              ///< slice lines from rect's edges: left, top, right, bottom
        float radius = 0.0f;
        juce::Colour face, edge;         ///< its face's colour in the middle; the image's corner (opaque)
        std::vector<Level> levels;       ///< ascending scale
    };

    /** Reads a pack: (data, size) must outlive this (BinaryData does). */
    Pieces (const void* data, size_t size);
    /** The embedded pack. */
    Pieces();
    ~Pieces();

    bool isValid() const noexcept { return valid; }
    juce::String problem() const { return error; }
    const Piece* find (const juce::String& name) const;
    int size() const noexcept { return static_cast<int> (pieces.size()); }

    /** Draws a piece over `target` (the part's own bounds, logical px; its shadow falls
        outside). `face`: the colour its face should show (its light and shading scaled to
        match, the shadow untouched); transparent: as rendered. `moving`: a size in passing
        (an animation): stretched while drawing instead of composed for that size. False if
        there is no such piece (the caller then draws the classic way). */
    bool draw (juce::Graphics&, const juce::String& name, juce::Rectangle<float> target, float opacity = 1.0f,
               juce::Colour face = {}, bool moving = false);

    /** The image `draw` shows for a physical size of the target's expanded bounds. */
    juce::Image composed (const Piece&, int physicalWidth, int physicalHeight, float physicalScale, juce::Colour face);
    /** A level as stored (decoded now; not kept). */
    juce::Image decode (const Piece&, const Level&) const;

    struct Usage
    {
        int composedImages = 0;
        size_t bytes = 0;
    };
    Usage usage() const;
    void release();

    /** The product's own pieces (design::Materials::pack) that replace these by name, when it
        has some: find, draw, composed and decode then answer from them for those names. */
    Pieces* overlayPack() const;

    /** The one set of pieces while any editor (or test) holds a Holder; nullptr otherwise. */
    static Pieces* current() noexcept;

    /** Keeps the embedded pieces alive (SharedResourcePointer: one per process). */
    struct Holder
    {
        Holder();
        ~Holder();
        Pieces& pieces() noexcept { return shared.get(); }

    private:
        juce::SharedResourcePointer<Pieces> shared;
    };

private:
    void read (const void* data, size_t size);

    struct Key
    {
        juce::String name;
        int w, h, scaleMilli;
        juce::uint32 tint;
        bool operator< (const Key& o) const noexcept
        {
            return std::tie (name, w, h, scaleMilli, tint) < std::tie (o.name, o.w, o.h, o.scaleMilli, o.tint);
        }
        bool operator== (const Key& o) const noexcept
        {
            return std::tie (name, w, h, scaleMilli, tint) == std::tie (o.name, o.w, o.h, o.scaleMilli, o.tint);
        }
    };

    std::unique_ptr<juce::MemoryInputStream> stream;
    std::unique_ptr<juce::ZipFile> zip;
    std::map<juce::String, Piece> pieces;
    std::map<Key, juce::Image> cache;
    std::list<Key> order;   ///< least recently used first
    bool isOverlay = false;
    mutable std::unique_ptr<Pieces> overlay;   ///< (made when the materials name a pack)
    mutable const void* overlayData = nullptr;
    bool owns (const Piece& p) const;
    size_t cachedBytes = 0;
    bool valid = false;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Pieces)
};

/** Resamples a (fractional) area of a premultiplied ARGB image to w x h pixels: by area
    where it shrinks, linearly where it grows (separable). */
juce::Image resampleArea (const juce::Image& source, juce::Rectangle<float> area, int width, int height);

/** The ceramic pieces when the look is ceramic and they read; nullptr otherwise (classic). */
Pieces* active() noexcept;

//==============================================================================
/** A1 keys by role (each rendered at its real size, with its role's depth). */
enum class KeyRole
{
    modifier,
    layerState,
    mode,
    eq,
    advanced,
    preset,
    arp,
    generic
};

enum class KeyState
{
    normal,
    hover,
    pressed,
    active
};

/** Draws an A1 key; false when the look is classic (the caller draws its own). */
bool key (juce::Graphics&, juce::Rectangle<float> bounds, float radius, KeyRole, KeyState, float opacity = 1.0f);

/** The corner each key role was rendered with (design px). */
float keyRadius (KeyRole) noexcept;

/**
    A control shown as an A1 key that is drawn by the surface it sits on, so the key's soft
    shadow can fall outside the control's own bounds (a component cannot paint outside
    them). The surface's paint calls drawKeys(); while the look is ceramic the control draws
    only its ink (drawnBySurface()) and calls repaintKey() when its state changes.
*/
class KeyFace
{
public:
    virtual ~KeyFace() = default;
    virtual KeyRole keyRole() const = 0;
    virtual KeyState keyState() const = 0;
};

/** The A1 key of every visible child of `surface` that is a KeyFace, at its bounds. */
void drawKeys (juce::Graphics&, juce::Component& surface);
/** True while the ceramic look draws keys and raised faces on the surface below them. */
bool drawnBySurface() noexcept;

/** A raised face (a source card, the arpeggiator's panel) drawn by the surface below it, for
    the same reason as a key: its rendered shadow falls on that surface. While the look is
    ceramic the component draws only what is on it (design::draw::raisedChildren). */
class SurfaceFace
{
public:
    virtual ~SurfaceFace() = default;
    virtual float surfaceRadius() const = 0;
    /** A card (the cards' colours) or a panel (the panels'). */
    virtual bool surfaceIsCard() const = 0;
};
/** A key's state changed: repaints it with the room its shadow needs. */
void repaintKey (juce::Component&);

} // namespace osp::plugin::skin
