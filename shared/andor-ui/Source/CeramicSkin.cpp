#include "CeramicSkin.h"

#include "BinaryData.h"

#include <atomic>
#include <cmath>

namespace osp::plugin::skin
{

namespace
{
    constexpr int manifestSchema = 1;
    constexpr size_t maxCachedImages = 160;
    constexpr size_t maxCachedBytes = 192u * 1024u * 1024u;

    Look lookFromEnvironment()
    {
        const auto value = juce::SystemStats::getEnvironmentVariable ("OSP_SKIN", {}).trim().toLowerCase();
        return value == "classic" ? Look::classic : Look::ceramic;
    }

    std::atomic<int>& lookState()
    {
        static std::atomic<int> state { static_cast<int> (lookFromEnvironment()) };
        return state;
    }

    Pieces*& currentPieces()
    {
        static Pieces* pieces = nullptr;
        return pieces;
    }

    juce::Colour parseColour (const juce::var& v)
    {
        const auto s = v.toString().trim();
        return s.startsWithChar ('#') && s.length() == 7 ? juce::Colour (0xff000000u | static_cast<juce::uint32> (s.substring (1).getHexValue32()))
                                                       : juce::Colour();
    }

    /** One destination pixel's source taps along an axis. */
    struct Taps
    {
        std::vector<int> first;
        std::vector<std::vector<float>> weights;
    };

    /** Taps for [a, b) of a source axis of `size` pixels onto `dest` pixels: the exact area
        each destination pixel covers when shrinking, linear interpolation when growing. */
    Taps taps (double a, double b, int size, int dest)
    {
        Taps t;
        t.first.resize (static_cast<size_t> (dest));
        t.weights.resize (static_cast<size_t> (dest));
        const double step = (b - a) / static_cast<double> (dest);
        const int lo = std::max (0, static_cast<int> (std::floor (a)));
        const int hi = std::min (size - 1, std::max (lo, static_cast<int> (std::ceil (b)) - 1));
        for (int d = 0; d < dest; ++d)
        {
            auto& w = t.weights[static_cast<size_t> (d)];
            if (step >= 1.0)
            {
                const double x0 = a + d * step, x1 = x0 + step;
                const int first = std::max (lo, static_cast<int> (std::floor (x0)));
                const int last = std::min (hi, static_cast<int> (std::ceil (x1)) - 1);
                t.first[static_cast<size_t> (d)] = first;
                double total = 0.0;
                for (int i = first; i <= last; ++i)
                {
                    const double cover = std::min (x1, i + 1.0) - std::max (x0, static_cast<double> (i));
                    w.push_back (static_cast<float> (std::max (0.0, cover)));
                    total += std::max (0.0, cover);
                }
                if (total > 0.0)
                    for (auto& v : w)
                        v = static_cast<float> (v / total);
            }
            else
            {
                const double centre = juce::jlimit (static_cast<double> (lo), static_cast<double> (hi), a + (d + 0.5) * step - 0.5);
                const int i0 = static_cast<int> (std::floor (centre));
                const int i1 = std::min (hi, i0 + 1);
                const auto f = static_cast<float> (centre - i0);
                t.first[static_cast<size_t> (d)] = i0;
                w.push_back (1.0f - f);
                if (i1 > i0)
                    w.push_back (f);
            }
        }
        return t;
    }
} // namespace

Look look() noexcept { return static_cast<Look> (lookState().load()); }
void setLook (Look l) noexcept { lookState() = static_cast<int> (l); }

//==============================================================================
juce::Image resampleArea (const juce::Image& sourceImage, juce::Rectangle<float> area, int width, int height)
{
    if (width <= 0 || height <= 0 || ! sourceImage.isValid() || area.isEmpty())
        return {};
    const auto source = sourceImage.getFormat() == juce::Image::ARGB ? sourceImage : sourceImage.convertedToFormat (juce::Image::ARGB);
    const int sw = source.getWidth(), sh = source.getHeight();
    const auto across = taps (area.getX(), area.getRight(), sw, width);
    const auto down = taps (area.getY(), area.getBottom(), sh, height);

    // Rows first (only the source rows the columns need), into floats; premultiplied stays
    // premultiplied, so averaging is exact.
    const int rowLo = down.first.front();
    int rowHi = rowLo;
    for (size_t d = 0; d < down.first.size(); ++d)
        rowHi = std::max (rowHi, down.first[d] + static_cast<int> (down.weights[d].size()) - 1);
    const int rows = rowHi - rowLo + 1;
    std::vector<float> buffer (static_cast<size_t> (rows) * static_cast<size_t> (width) * 4u, 0.0f);
    {
        const juce::Image::BitmapData in (source, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < rows; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto& w = across.weights[static_cast<size_t> (x)];
                const int first = across.first[static_cast<size_t> (x)];
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (size_t k = 0; k < w.size(); ++k)
                {
                    const auto* p = reinterpret_cast<const juce::PixelARGB*> (in.getPixelPointer (first + static_cast<int> (k), rowLo + y));
                    acc[0] += w[k] * p->getAlpha();
                    acc[1] += w[k] * p->getRed();
                    acc[2] += w[k] * p->getGreen();
                    acc[3] += w[k] * p->getBlue();
                }
                std::copy (acc, acc + 4, buffer.data() + (static_cast<size_t> (y) * static_cast<size_t> (width) + static_cast<size_t> (x)) * 4u);
            }
    }
    juce::Image out (juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
    {
        const juce::Image::BitmapData target (out, juce::Image::BitmapData::writeOnly);
        auto clampByte = [] (float v) { return static_cast<juce::uint8> (juce::jlimit (0, 255, juce::roundToInt (v))); };
        for (int y = 0; y < height; ++y)
        {
            const auto& w = down.weights[static_cast<size_t> (y)];
            const int first = down.first[static_cast<size_t> (y)] - rowLo;
            for (int x = 0; x < width; ++x)
            {
                float acc[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                for (size_t k = 0; k < w.size(); ++k)
                {
                    const float* src = buffer.data() + (static_cast<size_t> (first + static_cast<int> (k)) * static_cast<size_t> (width) + static_cast<size_t> (x)) * 4u;
                    for (int c = 0; c < 4; ++c)
                        acc[c] += w[k] * src[c];
                }
                const auto alpha = clampByte (acc[0]);
                auto* q = reinterpret_cast<juce::PixelARGB*> (target.getPixelPointer (x, y));
                q->setARGB (alpha, std::min (alpha, clampByte (acc[1])), std::min (alpha, clampByte (acc[2])), std::min (alpha, clampByte (acc[3])));
            }
        }
    }
    return out;
}

//==============================================================================
Pieces::Pieces (const void* data, size_t size) { read (data, size); }

Pieces::Pieces()
{
    int size = 0;
    if (const auto* data = BinaryData::getNamedResource ("ceramicskin_zip", size); data != nullptr && size > 0)
        read (data, static_cast<size_t> (size));
    else
        error = "the ceramic pack is not embedded";
}

Pieces::~Pieces() = default;

void Pieces::read (const void* data, size_t size)
{
    auto fail = [this] (const juce::String& why) {
        valid = false;
        error = why;
        pieces.clear();
    };
    stream = std::make_unique<juce::MemoryInputStream> (data, size, false);
    zip = std::make_unique<juce::ZipFile> (*stream);
    const int manifestIndex = zip->getIndexOfFileName ("manifest.json");
    if (manifestIndex < 0)
        return fail ("no manifest.json");
    juce::String text;
    if (std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (manifestIndex)); in != nullptr)
        text = in->readEntireStreamAsString();
    const auto manifest = juce::JSON::parse (text);
    if (static_cast<int> (manifest["schemaVersion"]) != manifestSchema)
        return fail ("unsupported manifest schemaVersion " + manifest["schemaVersion"].toString());
    const auto* list = manifest["pieces"].getArray();
    if (list == nullptr || list->isEmpty())
        return fail ("no pieces");
    auto number = [] (const juce::var& v) { return static_cast<float> (static_cast<double> (v)); };
    for (const auto& item : *list)
    {
        Piece p;
        p.name = item["name"].toString();
        const auto kind = item["kind"].toString();
        p.kind = kind == "opaque" ? Kind::opaque : kind == "overlay" ? Kind::overlay : Kind::alpha;
        const auto* rect = item["rect"].getArray();
        const auto* insets = item["insets"].getArray();
        const auto* levels = item["levels"].getArray();
        if (p.name.isEmpty() || rect == nullptr || rect->size() != 4 || insets == nullptr || insets->size() != 4 || levels == nullptr || levels->isEmpty())
            return fail ("a malformed piece " + p.name);
        p.rect = { number ((*rect)[0]), number ((*rect)[1]), number ((*rect)[2]), number ((*rect)[3]) };
        for (int i = 0; i < 4; ++i)
            p.insets[i] = std::max (0.0f, number ((*insets)[i]));
        p.margin = std::max (0.0f, number (item["margin"]));
        p.radius = number (item["radius"]);
        p.face = parseColour (item["face"]);
        p.edge = parseColour (item["edge"]);
        if (p.rect.isEmpty())
            return fail ("an empty piece " + p.name);
        for (const auto& l : *levels)
        {
            Level level;
            level.scale = number (l["scale"]);
            level.width = static_cast<int> (l["width"]);
            level.height = static_cast<int> (l["height"]);
            level.entry = zip->getIndexOfFileName (l["file"].toString());
            if (level.entry < 0 || level.scale <= 0.0f || level.width < 1 || level.height < 1)
                return fail ("a missing level of " + p.name);
            p.levels.push_back (level);
        }
        std::sort (p.levels.begin(), p.levels.end(), [] (const Level& a, const Level& b) { return a.scale < b.scale; });
        pieces.emplace (p.name, std::move (p));
    }
    valid = true;
}

const Pieces::Piece* Pieces::find (const juce::String& name) const
{
    const auto found = pieces.find (name);
    return valid && found != pieces.end() ? &found->second : nullptr;
}

juce::Image Pieces::decode (const Piece&, const Level& level) const
{
    std::unique_ptr<juce::InputStream> in (zip->createStreamForEntry (level.entry));
    if (in == nullptr)
        return {};
    auto image = juce::ImageFileFormat::loadFrom (*in);   // PNG, or JPEG for the opaque background
    if (! image.isValid() || image.getWidth() != level.width || image.getHeight() != level.height)
        return {};
    return image.getFormat() == juce::Image::ARGB ? image : image.convertedToFormat (juce::Image::ARGB);
}

juce::Image Pieces::composed (const Piece& p, int pw, int ph, float physicalScale, juce::Colour face)
{
    if (pw < 1 || ph < 1 || physicalScale <= 0.0f)
        return {};
    const bool tinted = ! face.isTransparent() && p.face.isOpaque() && face.getARGB() != p.face.getARGB();
    const Key key { p.name, pw, ph, juce::roundToInt (physicalScale * 1000.0f), tinted ? face.getARGB() : 0u };
    if (const auto found = cache.find (key); found != cache.end())
    {
        order.remove (key);
        order.push_back (key);
        return found->second;
    }

    // The nearest level at least as fine as the screen, else the finest.
    const Level* level = &p.levels.back();
    for (const auto& l : p.levels)
        if (l.scale >= physicalScale - 1.0e-3f)
        {
            level = &l;
            break;
        }
    const auto source = decode (p, *level);
    if (! source.isValid())
        return {};

    // Nine slices: source lines at the margin plus the insets (stored px), target lines at
    // the same design distances in physical px (shrunk together if the target is smaller).
    const float sx = static_cast<float> (level->width) / (p.rect.getWidth() + 2.0f * p.margin);
    const float sy = static_cast<float> (level->height) / (p.rect.getHeight() + 2.0f * p.margin);
    const float tx = static_cast<float> (pw), ty = static_cast<float> (ph);
    float l = (p.margin + p.insets[0]) * physicalScale, r = (p.margin + p.insets[2]) * physicalScale;
    float t = (p.margin + p.insets[1]) * physicalScale, b = (p.margin + p.insets[3]) * physicalScale;
    if (l + r > tx)
    {
        const float k = tx / (l + r);
        l *= k;
        r *= k;
    }
    if (t + b > ty)
    {
        const float k = ty / (t + b);
        t *= k;
        b *= k;
    }
    const float srcX[4] = { 0.0f, (p.margin + p.insets[0]) * sx, static_cast<float> (level->width) - (p.margin + p.insets[2]) * sx, static_cast<float> (level->width) };
    const float srcY[4] = { 0.0f, (p.margin + p.insets[1]) * sy, static_cast<float> (level->height) - (p.margin + p.insets[3]) * sy, static_cast<float> (level->height) };
    const int dstX[4] = { 0, juce::roundToInt (l), std::max (juce::roundToInt (l), pw - juce::roundToInt (r)), pw };
    const int dstY[4] = { 0, juce::roundToInt (t), std::max (juce::roundToInt (t), ph - juce::roundToInt (b)), ph };

    juce::Image out (juce::Image::ARGB, pw, ph, true, juce::SoftwareImageType());
    {
        juce::Graphics g (out);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int w = dstX[col + 1] - dstX[col], h = dstY[row + 1] - dstY[row];
                const juce::Rectangle<float> area (srcX[col], srcY[row], srcX[col + 1] - srcX[col], srcY[row + 1] - srcY[row]);
                if (w <= 0 || h <= 0 || area.getWidth() <= 0.0f || area.getHeight() <= 0.0f)
                    continue;
                const auto cell = resampleArea (source, area, w, h);
                g.drawImageAt (cell, dstX[col], dstY[row]);
            }
    }
    if (tinted)
    {
        // Scaled by the face's ratio (premultiplied channels scale the same way): the light
        // and shading keep their proportions, the shadow (black) stays a shadow.
        const juce::Image::BitmapData data (out, juce::Image::BitmapData::readWrite);
        auto gain = [] (juce::uint8 want, juce::uint8 have) { return static_cast<float> (want) / static_cast<float> (std::max<juce::uint8> (1, have)); };
        const float kr = gain (face.getRed(), p.face.getRed()), kg = gain (face.getGreen(), p.face.getGreen()), kb = gain (face.getBlue(), p.face.getBlue());
        for (int y = 0; y < ph; ++y)
            for (int x = 0; x < pw; ++x)
            {
                auto* q = reinterpret_cast<juce::PixelARGB*> (data.getPixelPointer (x, y));
                const auto a = q->getAlpha();
                auto scaled = [a] (juce::uint8 c, float k) { return static_cast<juce::uint8> (std::min (static_cast<int> (a), juce::roundToInt (c * k))); };
                q->setARGB (a, scaled (q->getRed(), kr), scaled (q->getGreen(), kg), scaled (q->getBlue(), kb));
            }
    }

    // Bounded: the oldest go first (a resizing window passes through many sizes).
    const size_t bytes = static_cast<size_t> (pw) * static_cast<size_t> (ph) * 4u;
    while (! order.empty() && (cache.size() >= maxCachedImages || cachedBytes + bytes > maxCachedBytes))
    {
        const auto oldest = cache.find (order.front());
        cachedBytes -= static_cast<size_t> (oldest->second.getWidth()) * static_cast<size_t> (oldest->second.getHeight()) * 4u;
        cache.erase (oldest);
        order.pop_front();
    }
    cache.emplace (key, out);
    order.push_back (key);
    cachedBytes += bytes;
    return out;
}

bool Pieces::draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> target, float opacity, juce::Colour face, bool moving)
{
    const auto* p = find (name);
    if (p == nullptr || target.isEmpty())
        return false;
    const float scale = std::max (0.01f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto outer = target.expanded (p->margin);
    if (moving)
    {
        // A size in passing (an animation): the piece composed once at its rendered size,
        // its nine slices stretched while drawing (no new composition per frame).
        const int rw = std::max (1, juce::roundToInt ((p->rect.getWidth() + 2.0f * p->margin) * scale));
        const int rh = std::max (1, juce::roundToInt ((p->rect.getHeight() + 2.0f * p->margin) * scale));
        const auto image = composed (*p, rw, rh, scale, face);
        if (! image.isValid())
            return false;
        const float sx[4] = { 0.0f, (p->margin + p->insets[0]) * scale, static_cast<float> (rw) - (p->margin + p->insets[2]) * scale, static_cast<float> (rw) };
        const float sy[4] = { 0.0f, (p->margin + p->insets[1]) * scale, static_cast<float> (rh) - (p->margin + p->insets[3]) * scale, static_cast<float> (rh) };
        const float dx[4] = { outer.getX(), outer.getX() + sx[1] / scale, outer.getRight() - (static_cast<float> (rw) - sx[2]) / scale, outer.getRight() };
        const float dy[4] = { outer.getY(), outer.getY() + sy[1] / scale, outer.getBottom() - (static_cast<float> (rh) - sy[2]) / scale, outer.getBottom() };
        juce::Graphics::ScopedSaveState state (g);
        g.setOpacity (opacity);
        g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
            {
                const int x0 = juce::roundToInt (sx[col]), y0 = juce::roundToInt (sy[row]);
                const int w = juce::roundToInt (sx[col + 1]) - x0, h = juce::roundToInt (sy[row + 1]) - y0;
                const float dw = dx[col + 1] - dx[col], dh = dy[row + 1] - dy[row];
                if (w <= 0 || h <= 0 || dw <= 0.0f || dh <= 0.0f)
                    continue;
                g.drawImage (image, dx[col], dy[row], dw, dh, x0, y0, w, h);
            }
        return true;
    }
    const int pw = std::max (1, juce::roundToInt (outer.getWidth() * scale));
    const int ph = std::max (1, juce::roundToInt (outer.getHeight() * scale));
    const auto image = composed (*p, pw, ph, scale, face);
    if (! image.isValid())
        return false;
    juce::Graphics::ScopedSaveState state (g);
    g.setOpacity (opacity);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (image, outer.withSize (static_cast<float> (pw) / scale, static_cast<float> (ph) / scale));
    return true;
}

Pieces::Usage Pieces::usage() const
{
    return { static_cast<int> (cache.size()), cachedBytes };
}

void Pieces::release()
{
    cache.clear();
    order.clear();
    cachedBytes = 0;
}

Pieces* Pieces::current() noexcept { return currentPieces(); }

namespace
{
    int& holders()
    {
        static int count = 0;
        return count;
    }
}

Pieces::Holder::Holder()
{
    ++holders();
    currentPieces() = &shared.get();
}

Pieces::Holder::~Holder()
{
    // The last holder releases the shared pieces (SharedResourcePointer): the pointer goes
    // with them; while another holder lives, it stays.
    if (--holders() == 0)
        currentPieces() = nullptr;
}

Pieces* active() noexcept
{
    auto* p = Pieces::current();
    return look() == Look::ceramic && p != nullptr && p->isValid() ? p : nullptr;
}

//==============================================================================
float keyRadius (KeyRole role) noexcept
{
    switch (role)
    {
        case KeyRole::layerState: return 4.0f;
        case KeyRole::eq:         return 6.0f;
        case KeyRole::preset:     return 12.0f;
        case KeyRole::arp:        return 10.0f;
        case KeyRole::modifier:
        case KeyRole::mode:
        case KeyRole::advanced:
        case KeyRole::generic:    break;
    }
    return 7.0f;
}

void drawKeys (juce::Graphics& g, juce::Component& surface)
{
    if (active() == nullptr)
        return;
    for (auto* child : surface.getChildren())
        if (const auto* face = dynamic_cast<const KeyFace*> (child); face != nullptr && child->isVisible())
            key (g, face->keyArea (child->getBounds().toFloat()), keyRadius (face->keyRole()), face->keyRole(), face->keyState());
}

bool drawnBySurface() noexcept { return active() != nullptr; }

void repaintKey (juce::Component& c)
{
    // The key's shadow reaches a few px beyond it (its piece's margin is 10).
    if (auto* parent = c.getParentComponent(); parent != nullptr && drawnBySurface())
        parent->repaint (c.getBounds().expanded (10));
    else
        c.repaint();
}

bool key (juce::Graphics& g, juce::Rectangle<float> bounds, float radius, KeyRole role, KeyState state, float opacity)
{
    auto* p = active();
    if (p == nullptr)
        return false;
    juce::String name;
    switch (role)
    {
        case KeyRole::modifier:   name = "modifier"; break;
        case KeyRole::layerState: name = "layerState"; break;
        case KeyRole::mode:       name = "mode"; break;
        case KeyRole::eq:         name = "eq"; break;
        case KeyRole::advanced:   name = "advanced"; break;
        case KeyRole::preset:     name = "preset"; break;
        case KeyRole::arp:        name = "arp"; break;
        case KeyRole::generic:
        {
            // The nearest rendered corner.
            static constexpr int radii[] = { 4, 5, 7, 9 };
            int best = radii[0];
            for (const int r : radii)
                if (std::abs (static_cast<float> (r) - radius) < std::abs (static_cast<float> (best) - radius))
                    best = r;
            name = "generic-r" + juce::String (best);
            break;
        }
    }
    const char* states[] = { "normal", "hover", "pressed", "active" };
    return p->draw (g, "key-" + name + "-" + states[static_cast<int> (state)], bounds, opacity);
}

} // namespace osp::plugin::skin
