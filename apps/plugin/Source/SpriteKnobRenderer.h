#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>
#include <memory>
#include <vector>

namespace osp::plugin
{

/**
    A control rendered offline as a turn of frames (tools/blender-assets): the knob's cap,
    with its own light and contact shadow; never its value arc, ticks or modulation halo,
    which stay vector-drawn around it.

    The asset is one or more packs of the same frames at different sizes (levels). A pack is
    a zip of PNGs, stored: manifest.json and frames/000.png ... The manifest says how many
    frames, their size, where the body is in a frame and which rotary angles the first and
    the last show.

    Lightweight by construction:
    - a frame is decoded only when it is first shown, then kept (a session that leaves a
      knob alone never decodes the rest of its turn);
    - for the size it is drawn at (the window's scale times the display's), a frame is
      resampled once, by area averaging, to exactly the physical pixels it covers, and then
      drawn 1:1: crisp at any scale, with no resampling while painting;
    - one asset serves every knob that shows it, in every plugin instance of the process
      (SpriteKnobLibrary below).

    Message thread only (painting); never touched by the audio thread.
*/
class SpriteKnobAsset
{
public:
    struct Info
    {
        juce::String name;
        int frameCount = 0;
        float bodyRadius = 0.0f;        ///< the body's outer radius, as a fraction of the frame side
        float angleStart = 0.0f;        ///< the angle frame 0 shows (radians clockwise from 12 o'clock)
        float angleEnd = 0.0f;          ///< ... and the last
        std::vector<int> levels;        ///< frame sides in pixels, ascending
    };

    /** Packs as (data, size), any order. Invalid or mismatched packs leave the asset
        invalid (callers then draw the vector knob). The data must outlive the asset
        (BinaryData does). */
    explicit SpriteKnobAsset (const std::vector<std::pair<const void*, size_t>>& packs);
    ~SpriteKnobAsset();

    bool isValid() const noexcept { return valid; }
    const Info& info() const noexcept { return sheet; }
    juce::String problem() const { return error; }

    /** The frame that shows a rotary angle: the nearest, clamped to the turn. */
    int frameForAngle (float angle) const noexcept;
    /** The angle a frame shows. */
    float angleOfFrame (int frame) const noexcept;

    /** The control with its body's outer radius `bodyRadius` (logical pixels) centred on
        `centre`, turned to `angle`. */
    void draw (juce::Graphics&, juce::Point<float> centre, float bodyRadius, float angle, float opacity = 1.0f);

    /** The level a physical frame side is made from: the smallest at least that large,
        else the largest. */
    int levelFor (int physicalSide) const noexcept;
    /** A frame as stored in a level; null if it cannot be read. `keep`: hold it after
        decoding (else it is decoded again when asked again). */
    juce::Image storedFrame (int level, int frame, bool keep = true);
    /** A frame at exactly `side` physical pixels (made on first use, then kept). */
    juce::Image frameAtSide (int frame, int side);

    /** What it holds now (tests, measurements). */
    struct Usage
    {
        int decodedFrames = 0;          ///< stored frames held (decoded and kept)
        int sizedFrames = 0;            ///< exact-size frames made so far (all sizes)
        size_t bytes = 0;               ///< pixel memory of both
    };
    Usage usage() const;
    /** Drops every decoded and resampled frame (they come back as they are needed). */
    void release();

private:
    struct Level
    {
        int side = 0;
        std::unique_ptr<juce::MemoryInputStream> stream;
        std::unique_ptr<juce::ZipFile> zip;
        std::vector<int> entries;       ///< zip entry of each frame
        std::vector<juce::Image> frames;
    };
    std::vector<Level> levelData;
    /** Exact-size frames by side; only the most recent few sizes are kept (a resizing
        window would otherwise collect one set per size). */
    std::map<int, std::vector<juce::Image>> sized;
    std::vector<int> sizedOrder;
    Info sheet;
    bool valid = false;
    juce::String error;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpriteKnobAsset)
};

/** Resamples premultiplied ARGB to a square of `side` pixels by area averaging (a box
    filter over exactly the source area each destination pixel covers). Upscaling falls
    back to JUCE's high-quality resampling. */
juce::Image resampleByArea (const juce::Image& source, int side);

/**
    The plugin's sprite controls, decoded once per process: hold a
    juce::SharedResourcePointer<SpriteKnobLibrary> wherever they are drawn. The memory is
    released when the last holder (the last open editor) goes.
*/
class SpriteKnobLibrary
{
public:
    SpriteKnobLibrary();
    /** The asset of that name, or nullptr when there is none (or it could not be read). */
    SpriteKnobAsset* find (const juce::String& name);

private:
    std::map<juce::String, std::unique_ptr<SpriteKnobAsset>> assets;
};

/**
    Development switch between the original (vector) knob and the rendered one. It changes
    only how knobs look: no parameter, no state, nothing the audio thread reads. Not saved;
    OSP_KNOB_SKIN=original|pilot|macros sets it at start-up (default: macros, the ceramic
    design's), Cmd+Option+K cycles it.
*/
enum class KnobSkin
{
    original,   ///< every knob as before
    pilot,      ///< the pilot macro (LIFE) rendered
    macros      ///< all six macros rendered (for comparison)
};

KnobSkin knobSkin() noexcept;
void setKnobSkin (KnobSkin) noexcept;
KnobSkin nextKnobSkin (KnobSkin) noexcept;
juce::String knobSkinName (KnobSkin);

/** The sprite a slider shows under the current skin, from its properties ("sprite": the
    asset's name, "spritePilot": true on the pilot), or nullptr for the vector knob. */
SpriteKnobAsset* spriteFor (const juce::Slider&, SpriteKnobLibrary&);

} // namespace osp::plugin
