#include "SpriteKnobRenderer.h"

#include "BinaryData.h"

#include <atomic>
#include <cmath>

namespace osp::plugin
{

namespace
{
    constexpr int manifestSchema = 1;
    constexpr size_t keptSizes = 3;   // exact-size sets kept (a resize passes through many)

    juce::String readEntry (juce::ZipFile& zip, int index)
    {
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (index));
        return in != nullptr ? in->readEntireStreamAsString() : juce::String();
    }
} // namespace

//==============================================================================
SpriteKnobAsset::SpriteKnobAsset (const std::vector<std::pair<const void*, size_t>>& packs)
{
    auto fail = [this] (const juce::String& why) {
        valid = false;
        error = why;
        levelData.clear();
    };
    if (packs.empty())
    {
        fail ("no packs");
        return;
    }
    bool first = true;
    for (const auto& [data, size] : packs)
    {
        Level level;
        level.stream = std::make_unique<juce::MemoryInputStream> (data, size, false);
        level.zip = std::make_unique<juce::ZipFile> (*level.stream);
        const int manifestIndex = level.zip->getIndexOfFileName ("manifest.json");
        if (manifestIndex < 0)
            {
                fail ("a pack has no manifest.json");
                return;
            }
        const auto manifest = juce::JSON::parse (readEntry (*level.zip, manifestIndex));
        if (static_cast<int> (manifest["schemaVersion"]) != manifestSchema)
            {
                fail ("unsupported manifest schemaVersion " + manifest["schemaVersion"].toString());
                return;
            }
        if (manifest["frames"].toString() != "frames/%03d.png")
            {
                fail ("unexpected frame naming " + manifest["frames"].toString());
                return;
            }
        Info here;
        here.name = manifest["name"].toString();
        here.frameCount = static_cast<int> (manifest["frameCount"]);
        here.bodyRadius = static_cast<float> (static_cast<double> (manifest["bodyRadius"]));
        here.angleStart = static_cast<float> (static_cast<double> (manifest["angleStart"]));
        here.angleEnd = static_cast<float> (static_cast<double> (manifest["angleEnd"]));
        level.side = static_cast<int> (manifest["frameSize"]);
        if (here.frameCount < 2 || level.side < 8 || here.bodyRadius <= 0.05f || here.bodyRadius > 0.5f
            || ! (here.angleEnd > here.angleStart))
            {
                fail ("implausible manifest in " + here.name);
                return;
            }
        if (first)
        {
            sheet = here;
            first = false;
        }
        else if (here.name != sheet.name || here.frameCount != sheet.frameCount || std::abs (here.bodyRadius - sheet.bodyRadius) > 1.0e-4f
                 || std::abs (here.angleStart - sheet.angleStart) > 1.0e-5f || std::abs (here.angleEnd - sheet.angleEnd) > 1.0e-5f)
            {
                fail ("packs of " + sheet.name + " disagree");
                return;
            }
        level.entries.resize (static_cast<size_t> (here.frameCount));
        for (int i = 0; i < here.frameCount; ++i)
        {
            const int entry = level.zip->getIndexOfFileName (juce::String ("frames/") + juce::String (i).paddedLeft ('0', 3) + ".png");
            if (entry < 0)
                {
                fail ("frame " + juce::String (i) + " missing from " + here.name);
                return;
            }
            level.entries[static_cast<size_t> (i)] = entry;
        }
        level.frames.resize (static_cast<size_t> (here.frameCount));
        for (const auto& other : levelData)
            if (other.side == level.side)
                {
                fail ("two packs of " + here.name + " at " + juce::String (level.side) + " px");
                return;
            }
        levelData.push_back (std::move (level));
    }
    std::sort (levelData.begin(), levelData.end(), [] (const Level& a, const Level& b) { return a.side < b.side; });
    for (const auto& level : levelData)
        sheet.levels.push_back (level.side);
    valid = true;
}

SpriteKnobAsset::~SpriteKnobAsset() = default;

int SpriteKnobAsset::frameForAngle (float angle) const noexcept
{
    if (! valid)
        return 0;
    const float t = (angle - sheet.angleStart) / (sheet.angleEnd - sheet.angleStart);
    return juce::jlimit (0, sheet.frameCount - 1, juce::roundToInt (t * static_cast<float> (sheet.frameCount - 1)));
}

float SpriteKnobAsset::angleOfFrame (int frame) const noexcept
{
    if (! valid)
        return 0.0f;
    return sheet.angleStart + (sheet.angleEnd - sheet.angleStart) * static_cast<float> (frame) / static_cast<float> (sheet.frameCount - 1);
}

int SpriteKnobAsset::levelFor (int physicalSide) const noexcept
{
    for (size_t i = 0; i < levelData.size(); ++i)
        if (levelData[i].side >= physicalSide)
            return static_cast<int> (i);
    return static_cast<int> (levelData.size()) - 1;
}

juce::Image SpriteKnobAsset::storedFrame (int levelIndex, int frame, bool keep)
{
    if (! valid || levelIndex < 0 || levelIndex >= static_cast<int> (levelData.size()) || frame < 0 || frame >= sheet.frameCount)
        return {};
    auto& level = levelData[static_cast<size_t> (levelIndex)];
    auto& slot = level.frames[static_cast<size_t> (frame)];
    if (! slot.isValid())
    {
        std::unique_ptr<juce::InputStream> in (level.zip->createStreamForEntry (level.entries[static_cast<size_t> (frame)]));
        if (in == nullptr)
            return {};
        auto image = juce::PNGImageFormat().decodeImage (*in);
        if (! image.isValid() || image.getWidth() != level.side || image.getHeight() != level.side)
            return {};
        image = image.getFormat() == juce::Image::ARGB ? image : image.convertedToFormat (juce::Image::ARGB);
        if (! keep)
            return image;
        slot = image;
    }
    return slot;
}

juce::Image SpriteKnobAsset::frameAtSide (int frame, int side)
{
    if (! valid || side < 1 || frame < 0 || frame >= sheet.frameCount)
        return {};
    auto found = sized.find (side);
    if (found == sized.end())
    {
        // A new size: keep the most recent few, the oldest goes.
        if (sizedOrder.size() >= keptSizes)
        {
            sized.erase (sizedOrder.front());
            sizedOrder.erase (sizedOrder.begin());
        }
        found = sized.emplace (side, std::vector<juce::Image> (static_cast<size_t> (sheet.frameCount))).first;
        sizedOrder.push_back (side);
    }
    auto& slot = found->second[static_cast<size_t> (frame)];
    if (! slot.isValid())
    {
        // Kept as stored only when that is the size drawn; otherwise decoded, resampled to
        // the drawn size and let go (memory holds what is on screen, not every level).
        const int level = levelFor (side);
        const bool exact = levelData[static_cast<size_t> (level)].side == side;
        const auto stored = storedFrame (level, frame, exact);
        if (! stored.isValid())
            return {};
        slot = exact ? stored : resampleByArea (stored, side);
    }
    return slot;
}

void SpriteKnobAsset::draw (juce::Graphics& g, juce::Point<float> centre, float bodyRadius, float angle, float opacity)
{
    if (! valid || bodyRadius <= 0.0f)
        return;
    // Exactly the physical pixels the frame covers at this scale (window x display).
    const float scale = std::max (0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float logicalSide = bodyRadius / sheet.bodyRadius;
    const int side = std::max (8, juce::roundToInt (logicalSide * scale));
    const auto image = frameAtSide (frameForAngle (angle), side);
    if (! image.isValid())
        return;
    const float drawn = static_cast<float> (side) / scale;
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (opacity);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, juce::Rectangle<float> (drawn, drawn).withCentre (centre));
}

SpriteKnobAsset::Usage SpriteKnobAsset::usage() const
{
    Usage u;
    auto bytesOf = [] (const juce::Image& image) { return static_cast<size_t> (image.getWidth()) * static_cast<size_t> (image.getHeight()) * 4u; };
    for (const auto& level : levelData)
        for (const auto& frame : level.frames)
            if (frame.isValid())
            {
                ++u.decodedFrames;
                u.bytes += bytesOf (frame);
            }
    for (const auto& [side, frames] : sized)
    {
        const int levelSide = levelData[static_cast<size_t> (levelFor (side))].side;
        for (const auto& frame : frames)
            if (frame.isValid())
            {
                ++u.sizedFrames;
                if (levelSide != side)   // at a level's own size it is the stored frame itself
                    u.bytes += bytesOf (frame);
            }
    }
    return u;
}

void SpriteKnobAsset::release()
{
    for (auto& level : levelData)
        for (auto& frame : level.frames)
            frame = {};
    sized.clear();
    sizedOrder.clear();
}

//==============================================================================
juce::Image resampleByArea (const juce::Image& sourceImage, int side)
{
    const auto source = sourceImage.getFormat() == juce::Image::ARGB ? sourceImage : sourceImage.convertedToFormat (juce::Image::ARGB);
    const int sw = source.getWidth(), sh = source.getHeight();
    if (side <= 0 || sw <= 0 || sh <= 0)
        return {};
    if (side >= sw || side >= sh)
        return side == sw && side == sh ? source.createCopy() : source.rescaled (side, side, juce::Graphics::highResamplingQuality);

    // Each destination pixel averages exactly the source area it covers: whole pixels at
    // full weight, the partly covered ones at their share. Separable: rows, then columns.
    struct Tap
    {
        int first = 0;
        std::vector<float> weights;
    };
    auto taps = [] (int sourceSize, int destSize) {
        std::vector<Tap> out (static_cast<size_t> (destSize));
        const double step = static_cast<double> (sourceSize) / static_cast<double> (destSize);
        for (int d = 0; d < destSize; ++d)
        {
            const double a = d * step, b = (d + 1) * step;
            auto& tap = out[static_cast<size_t> (d)];
            tap.first = static_cast<int> (std::floor (a));
            const int last = std::min (sourceSize - 1, static_cast<int> (std::ceil (b)) - 1);
            for (int i = tap.first; i <= last; ++i)
                tap.weights.push_back (static_cast<float> ((std::min (b, i + 1.0) - std::max (a, static_cast<double> (i))) / step));
        }
        return out;
    };
    const auto across = taps (sw, side), down = taps (sh, side);

    // Rows first, into floats (premultiplied ARGB stays premultiplied: averaging is exact).
    std::vector<float> rows (static_cast<size_t> (sh) * static_cast<size_t> (side) * 4u, 0.0f);
    {
        const juce::Image::BitmapData in (source, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < sh; ++y)
            for (int x = 0; x < side; ++x)
            {
                const auto& tap = across[static_cast<size_t> (x)];
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (size_t k = 0; k < tap.weights.size(); ++k)
                {
                    const auto* p = reinterpret_cast<const juce::PixelARGB*> (in.getPixelPointer (tap.first + static_cast<int> (k), y));
                    const float w = tap.weights[k];
                    acc[0] += w * p->getAlpha();
                    acc[1] += w * p->getRed();
                    acc[2] += w * p->getGreen();
                    acc[3] += w * p->getBlue();
                }
                float* dst = rows.data() + (static_cast<size_t> (y) * static_cast<size_t> (side) + static_cast<size_t> (x)) * 4u;
                for (int c = 0; c < 4; ++c)
                    dst[c] = acc[c];
            }
    }
    juce::Image out (juce::Image::ARGB, side, side, true);
    {
        const juce::Image::BitmapData target (out, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < side; ++y)
        {
            const auto& tap = down[static_cast<size_t> (y)];
            for (int x = 0; x < side; ++x)
            {
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (size_t k = 0; k < tap.weights.size(); ++k)
                {
                    const float* src = rows.data() + (static_cast<size_t> (tap.first + static_cast<int> (k)) * static_cast<size_t> (side) + static_cast<size_t> (x)) * 4u;
                    const float w = tap.weights[k];
                    for (int c = 0; c < 4; ++c)
                        acc[c] += w * src[c];
                }
                auto clampByte = [] (float v) { return static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (v))); };
                const auto alpha = clampByte (acc[0]);
                // Premultiplied: no colour above its alpha (rounding could otherwise do it).
                auto* q = reinterpret_cast<juce::PixelARGB*> (target.getPixelPointer (x, y));
                q->setARGB (alpha, std::min (alpha, clampByte (acc[1])), std::min (alpha, clampByte (acc[2])), std::min (alpha, clampByte (acc[3])));
            }
        }
    }
    return out;
}

//==============================================================================
SpriteKnobLibrary::SpriteKnobLibrary()
{
    // Every embedded "<name>-<side>.zip" is a level of the asset <name>.
    std::map<juce::String, std::vector<std::pair<const void*, size_t>>> packs;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String original (BinaryData::getNamedResourceOriginalFilename (BinaryData::namedResourceList[i]));
        if (! original.endsWithIgnoreCase (".zip"))
            continue;
        const auto stem = original.dropLastCharacters (4);
        const auto name = stem.upToLastOccurrenceOf ("-", false, false);
        if (name.isEmpty() || ! stem.fromLastOccurrenceOf ("-", false, false).containsOnly ("0123456789"))
            continue;
        int size = 0;
        if (const auto* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size); data != nullptr && size > 0)
            packs[name].emplace_back (data, static_cast<size_t> (size));
    }
    for (const auto& [name, levels] : packs)
    {
        auto asset = std::make_unique<SpriteKnobAsset> (levels);
        jassert (asset->isValid());   // an embedded asset that does not read is a build mistake
        assets.emplace (name, std::move (asset));
    }
}

SpriteKnobAsset* SpriteKnobLibrary::find (const juce::String& name)
{
    const auto found = assets.find (name);
    return found != assets.end() && found->second->isValid() ? found->second.get() : nullptr;
}

//==============================================================================
namespace
{
    KnobSkin skinFromEnvironment()
    {
        const auto value = juce::SystemStats::getEnvironmentVariable ("OSP_KNOB_SKIN", {}).trim().toLowerCase();
        if (value == "original")
            return KnobSkin::original;
        if (value == "pilot")
            return KnobSkin::pilot;
        // The chosen design (ceramic, docs/design/warm-ceramic-teak-style-guide.md): all six
        // macros rendered.
        return KnobSkin::macros;
    }

    std::atomic<int>& skinState()
    {
        static std::atomic<int> state { static_cast<int> (skinFromEnvironment()) };
        return state;
    }
} // namespace

KnobSkin knobSkin() noexcept { return static_cast<KnobSkin> (skinState().load()); }
void setKnobSkin (KnobSkin skin) noexcept { skinState() = static_cast<int> (skin); }

KnobSkin nextKnobSkin (KnobSkin skin) noexcept
{
    switch (skin)
    {
        case KnobSkin::original: return KnobSkin::pilot;
        case KnobSkin::pilot:    return KnobSkin::macros;
        case KnobSkin::macros:   break;
    }
    return KnobSkin::original;
}

juce::String knobSkinName (KnobSkin skin)
{
    switch (skin)
    {
        case KnobSkin::original: return "original";
        case KnobSkin::pilot:    return "pilot";
        case KnobSkin::macros:   break;
    }
    return "macros";
}

SpriteKnobAsset* spriteFor (const juce::Slider& slider, SpriteKnobLibrary& library)
{
    const auto& props = slider.getProperties();
    const auto name = props["sprite"].toString();
    if (name.isEmpty())
        return nullptr;
    switch (knobSkin())
    {
        case KnobSkin::original: return nullptr;
        case KnobSkin::pilot:    return static_cast<bool> (props["spritePilot"]) ? library.find (name) : nullptr;
        case KnobSkin::macros:   break;
    }
    return library.find (name);
}

} // namespace osp::plugin
