#include "LibraryPanel.h"

#include "Design.h"
#include "OspLookAndFeel.h"
#include "io/ContentHash.h"
#include "library/SoundImport.h"

namespace osp::plugin
{

namespace
{
    using library::AssetType;
    using library::Origin;
    namespace colour = design::colour;

    // The panel's own layout (its local coordinates: reference pixels, 1378 x 875).
    constexpr int headerHeight = 64;
    constexpr int footerHeight = 34;
    constexpr int sidebarWidth = 220;
    constexpr int gutter = 16;
    constexpr int inspectorWidth = 360;
    constexpr int trayHeight = 150;
    constexpr int detailHeight = 128;

    const juce::StringArray soundTypes { "Bass", "Keys", "Pluck", "Pad", "Percussion", "Vocal", "Texture", "Other" };
    const juce::StringArray presetCategories { "Keys", "Pads", "Basses", "Plucks", "Rhythmic", "Textures", "Evolving", "FX", "Experimental" };
    const juce::StringArray templateCategories { "Evolving", "Rhythmic", "Granular", "Keys", "Basses", "Textured", "FX" };

    juce::String originName (Origin o)
    {
        switch (o)
        {
            case Origin::factory: return "Factory";
            case Origin::user: return "User";
            case Origin::pack: return "Pack";
            case Origin::captured: return "Captured";
            case Origin::external: return "External";
        }
        return "User";
    }

    juce::String dateText (std::int64_t seconds)
    {
        if (seconds <= 0)
            return {};
        const juce::Time t (seconds * 1000);
        const auto today = juce::Time::getCurrentTime();
        const auto sameDay = [] (const juce::Time& a, const juce::Time& b) {
            return a.getYear() == b.getYear() && a.getDayOfYear() == b.getDayOfYear();
        };
        if (sameDay (t, today))
            return "Today " + t.formatted ("%H:%M");
        if (sameDay (t, today - juce::RelativeTime::days (1)))
            return "Yesterday";
        return t.formatted ("%Y-%m-%d");
    }

    juce::String bytesText (std::int64_t bytes)
    {
        if (bytes <= 0)
            return "0 KB";
        if (bytes < 1024 * 1024)
            return juce::String (std::max (1, juce::roundToInt (static_cast<double> (bytes) / 1024.0))) + " KB";
        if (bytes < 1024LL * 1024 * 1024)
            return juce::String (static_cast<double> (bytes) / (1024.0 * 1024.0), 1) + " MB";
        return juce::String (static_cast<double> (bytes) / (1024.0 * 1024.0 * 1024.0), 2) + " GB";
    }

    juce::String lengthText (double seconds)
    {
        if (seconds <= 0.0)
            return {};
        if (seconds < 60.0)
            return juce::String (seconds, seconds < 10.0 ? 1 : 0) + " s";
        return juce::String (static_cast<int> (seconds) / 60) + ":" + juce::String (static_cast<int> (seconds) % 60).paddedLeft ('0', 2);
    }

    juce::String noteName (double midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int n = juce::roundToInt (midi);
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1);
    }

    /** A thin level slider in the instrument's colours. */
    void styleLevel (juce::Slider& s)
    {
        s.setColour (juce::Slider::thumbColourId, colour::accent);
        s.setColour (juce::Slider::trackColourId, colour::accentSoft);
        s.setColour (juce::Slider::backgroundColourId, colour::knobTrack);
        s.setColour (juce::Slider::textBoxTextColourId, colour::textSecondary);
    }

    /** The accent key of a sheet or the tray: the one action that commits. */
    class PrimaryButton final : public juce::Button
    {
    public:
        explicit PrimaryButton (const juce::String& label) : juce::Button (label) { setTitle (label); }
        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f, 1.5f);
            const float radius = std::min (9.0f, r.getHeight() * 0.24f);
            const float lift = isEnabled() ? (down ? 0.92f : (highlighted ? 1.06f : 1.0f)) : 1.0f;
            juce::ColourGradient fill (colour::accentTop.withMultipliedBrightness (lift), r.getX(), r.getY(),
                                       colour::accentBottom.withMultipliedBrightness (lift), r.getX(), r.getBottom(), false);
            g.setGradientFill (fill);
            g.setOpacity (isEnabled() ? 1.0f : 0.4f);
            g.fillRoundedRectangle (r, radius);
            g.setColour (juce::Colours::white.withAlpha (isEnabled() ? 0.96f : 0.7f));
            g.setFont (type::button (std::min (17.0f, r.getHeight() * 0.42f)));
            g.drawText (getButtonText(), r.toNearestInt(), juce::Justification::centred, false);
        }
    };

    /** A small star rating (0-5): click a star to set it, the same star again clears it. */
    void drawStars (juce::Graphics& g, juce::Rectangle<float> area, int stars, float size = 12.0f)
    {
        for (int i = 0; i < 5; ++i)
        {
            juce::Path star;
            const auto c = juce::Point<float> (area.getX() + size * 0.5f + static_cast<float> (i) * (size + 3.0f), area.getCentreY());
            star.addStar (c, 5, size * 0.22f, size * 0.5f, 0.0f);
            g.setColour (i < stars ? colour::macro (colour::Macro::drive) : colour::hairline.darker (0.08f));
            g.fillPath (star);
        }
    }
    int starAt (float x, float left, float size = 12.0f)
    {
        const int i = static_cast<int> ((x - left) / (size + 3.0f));
        return x < left || i > 4 ? -1 : i;
    }

    void drawHeart (juce::Graphics& g, juce::Rectangle<float> r, bool on)
    {
        juce::Path heart;
        const float w = r.getWidth(), h = r.getHeight(), x = r.getX(), y = r.getY();
        heart.startNewSubPath (x + w * 0.5f, y + h * 0.92f);
        heart.cubicTo (x - w * 0.15f, y + h * 0.45f, x + w * 0.15f, y - h * 0.08f, x + w * 0.5f, y + h * 0.28f);
        heart.cubicTo (x + w * 0.85f, y - h * 0.08f, x + w * 1.15f, y + h * 0.45f, x + w * 0.5f, y + h * 0.92f);
        heart.closeSubPath();
        if (on)
        {
            g.setColour (colour::accent);
            g.fillPath (heart);
        }
        else
        {
            g.setColour (colour::textSecondary);
            g.strokePath (heart, juce::PathStrokeType (1.3f));
        }
    }

    void drawOverview (juce::Graphics& g, juce::Rectangle<float> r, const std::vector<float>& bins, juce::Colour c)
    {
        if (bins.empty())
            return;
        const float step = r.getWidth() / static_cast<float> (bins.size());
        g.setColour (c);
        for (std::size_t i = 0; i < bins.size(); ++i)
        {
            const float h = std::max (1.0f, bins[i] * r.getHeight() * 0.95f);
            g.fillRect (juce::Rectangle<float> (r.getX() + static_cast<float> (i) * step, r.getCentreY() - h * 0.5f, std::max (1.0f, step - 1.0f), h));
        }
    }

    /** Chips (tags, collections) with a remove cross, and a trailing add field or button. */
    class ChipRow final : public juce::Component
    {
    public:
        std::function<void (const juce::String&)> onRemove;
        void setChips (juce::StringArray chips)
        {
            items = std::move (chips);
            repaint();
        }
        int preferredHeight (int width) const
        {
            int x = 0, rows = 1;
            for (const auto& c : items)
            {
                const int w = chipWidth (c);
                if (x + w > width && x > 0)
                {
                    ++rows;
                    x = 0;
                }
                x += w + 6;
            }
            return rows * 30;
        }
        void paint (juce::Graphics& g) override
        {
            hit.clear();
            int x = 0, y = 0;
            g.setFont (type::micro (14.0f));
            for (const auto& c : items)
            {
                const int w = chipWidth (c);
                if (x + w > getWidth() && x > 0)
                {
                    x = 0;
                    y += 30;
                }
                const auto r = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y) + 2.0f, static_cast<float> (w), 24.0f);
                g.setColour (colour::panelBottom);
                g.fillRoundedRectangle (r, 12.0f);
                g.setColour (colour::divider);
                g.drawRoundedRectangle (r, 12.0f, 1.0f);
                g.setColour (colour::text);
                g.drawText (c, r.withTrimmedLeft (10.0f).withTrimmedRight (22.0f).toNearestInt(), juce::Justification::centredLeft, true);
                const auto cross = juce::Rectangle<float> (r.getRight() - 18.0f, r.getCentreY() - 4.0f, 8.0f, 8.0f);
                g.setColour (colour::textMicro);
                g.drawLine (cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 1.2f);
                g.drawLine (cross.getRight(), cross.getY(), cross.getX(), cross.getBottom(), 1.2f);
                hit.push_back ({ cross.expanded (5.0f), c });
                x += w + 6;
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            for (const auto& [r, name] : hit)
                if (r.contains (e.position) && onRemove != nullptr)
                {
                    onRemove (name);
                    return;
                }
        }

    private:
        static int chipWidth (const juce::String& c)
        {
            return juce::roundToInt (juce::GlyphArrangement::getStringWidth (type::micro (14.0f), c)) + 34;
        }
        juce::StringArray items;
        std::vector<std::pair<juce::Rectangle<float>, juce::String>> hit;
    };
}

void drawLibraryArtwork (juce::Graphics& g, juce::Rectangle<float> area, const std::string& identity, float radius)
{
    // Two of the instrument's mineral colours and an angle, all from the identity's hash: the
    // same asset always looks the same, nothing is downloaded or invented (D-09).
    const auto h = static_cast<std::uint32_t> (juce::String (identity).hashCode());
    static const juce::Colour stones[] { colour::macro (0), colour::macro (1), colour::macro (2), colour::macro (3), colour::macro (4), colour::macro (5),
                                         juce::Colour (0xff6f7a73), juce::Colour (0xffa58f74) };
    const auto a = stones[h % 8u], b = stones[(h / 8u) % 8u == h % 8u ? (h / 8u + 3u) % 8u : (h / 8u) % 8u];
    const float angle = static_cast<float> ((h / 64u) % 360u) * juce::MathConstants<float>::pi / 180.0f;
    const auto c = area.getCentre();
    const auto d = juce::Point<float> (std::cos (angle), std::sin (angle)) * (area.getWidth() * 0.6f);
    juce::Path shape;
    shape.addRoundedRectangle (area, radius);
    g.saveState();
    g.reduceClipRegion (shape);
    g.setGradientFill (juce::ColourGradient (a.withMultipliedSaturation (0.85f), c - d, b.withMultipliedBrightness (0.85f), c + d, false));
    g.fillRect (area);
    // A soft light and a few quiet contour lines: texture without pretending to be a picture.
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.28f), area.getX() + area.getWidth() * 0.3f, area.getY() + area.getHeight() * 0.25f,
                                             juce::Colours::transparentWhite, area.getRight(), area.getBottom(), true));
    g.fillRect (area);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    for (int i = 1; i <= 4; ++i)
    {
        juce::Path line;
        const float y = area.getY() + area.getHeight() * static_cast<float> (i) / 5.0f;
        const float wobble = static_cast<float> ((h >> (i * 3)) % 9u) - 4.0f;
        line.startNewSubPath (area.getX(), y);
        line.quadraticTo (c.x, y + wobble * area.getHeight() * 0.04f, area.getRight(), y - wobble * area.getHeight() * 0.02f);
        g.strokePath (line, juce::PathStrokeType (1.0f));
    }
    g.restoreState();
    g.setColour (colour::edgeShade.withAlpha (0.6f));
    g.drawRoundedRectangle (area, radius, 1.0f);
}

std::string LibraryPanel::assetFromDrag (const juce::var& description)
{
    const auto text = description.toString();
    return text.startsWith ("osp-sound:") ? text.fromFirstOccurrenceOf ("osp-sound:", false, false).toStdString() : std::string();
}

//==============================================================================
// The sidebar: scopes, collections, categories, with counts.

class LibraryPanel::Sidebar final : public juce::Component
{
public:
    explicit Sidebar (LibraryPanel& p) : panel (p)
    {
        setTitle ("Library sections");
        setWantsKeyboardFocus (true);
    }
    void paint (juce::Graphics& g) override
    {
        int y = 0;
        for (const auto& item : panel.side)
        {
            if (item.count < 0)
            {
                y += 10;
                g.setColour (colour::textMicro);
                g.setFont (type::popupTitle (12.5f));
                g.drawText (item.label.toUpperCase(), 10, y, getWidth() - 40, 22, juce::Justification::centredLeft);
                if (item.key == "header:collections")
                {
                    g.setColour (colour::textSecondary);
                    g.setFont (type::micro (18.0f));
                    g.drawText ("+", getWidth() - 30, y, 22, 22, juce::Justification::centred);
                }
                y += 24;
                continue;
            }
            const auto r = juce::Rectangle<int> (0, y, getWidth(), 28);
            const bool selected = item.key == panel.scopeKey;
            if (selected)
            {
                g.setColour (colour::panelBottom.darker (0.04f));
                g.fillRoundedRectangle (r.toFloat().reduced (2.0f, 1.0f), 6.0f);
                g.setColour (colour::accent);
                g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, static_cast<float> (y) + 7.0f, 3.0f, 14.0f), 1.5f);
            }
            else if (r.contains (hover))
            {
                g.setColour (colour::panelBottom.withAlpha (0.6f));
                g.fillRoundedRectangle (r.toFloat().reduced (2.0f, 1.0f), 6.0f);
            }
            g.setColour (selected ? colour::text : colour::textSecondary);
            g.setFont (type::micro (15.0f));
            g.drawText (item.label, r.withTrimmedLeft (14).withTrimmedRight (52), juce::Justification::centredLeft, true);
            g.setColour (colour::textMicro);
            g.setFont (type::micro (13.5f));
            g.drawText (juce::String (item.count), r.withTrimmedRight (10), juce::Justification::centredRight);
            y += 28;
        }
    }
    int contentHeight() const
    {
        int y = 0;
        for (const auto& item : panel.side)
            y += item.count < 0 ? 34 : 28;
        return y + 8;
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        hover = e.getPosition();
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override
    {
        hover = { -1, -1 };
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto* item = itemAt (e.y);
        if (item == nullptr)
            return;
        if (item->count < 0)
        {
            if (item->key == "header:collections" && e.x > getWidth() - 34)
                newCollection();
            return;
        }
        if (e.mods.isPopupMenu() && item->key.startsWith ("collection:"))
        {
            const auto id = item->key.fromFirstOccurrenceOf ("collection:", false, false).toStdString();
            const auto name = item->label;
            juce::PopupMenu menu;
            juce::Component::SafePointer<LibraryPanel> safe (&panel);
            menu.addItem (juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), [safe, id, name] {
                if (safe != nullptr)
                    safe->prompt ("Rename collection", name, [safe, id] (const juce::String& text) {
                        if (safe != nullptr)
                            safe->edit ([id, text] (library::Catalog& c) { return c.renameCollection (id, text.toStdString()); });
                    });
            });
            menu.addItem ("Delete collection (its sounds stay)", [safe, id] {
                if (safe != nullptr)
                {
                    safe->scopeKey = "all";
                    safe->edit ([id] (library::Catalog& c) { return c.deleteCollection (id); }, "Collection deleted");
                }
            });
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            return;
        }
        panel.selectScope (item->key);
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        // Up and down move through the sections (headers are skipped).
        const bool up = key == juce::KeyPress::upKey, down = key == juce::KeyPress::downKey;
        if (! up && ! down)
            return false;
        std::vector<juce::String> keys;
        for (const auto& item : panel.side)
            if (item.count >= 0)
                keys.push_back (item.key);
        auto it = std::find (keys.begin(), keys.end(), panel.scopeKey);
        if (keys.empty())
            return true;
        int index = it == keys.end() ? 0 : static_cast<int> (it - keys.begin()) + (up ? -1 : 1);
        panel.selectScope (keys[static_cast<std::size_t> (juce::jlimit (0, static_cast<int> (keys.size()) - 1, index))]);
        return true;
    }
    void newCollection()
    {
        juce::Component::SafePointer<LibraryPanel> safe (&panel);
        panel.prompt ("New collection", {}, [safe] (const juce::String& name) {
            if (safe != nullptr)
                safe->edit ([name] (library::Catalog& c) { return c.createCollection (name.toStdString()).has_value(); }, "Collection " + name + " made");
        });
    }

private:
    const SideItem* itemAt (int y) const
    {
        int top = 0;
        for (const auto& item : panel.side)
        {
            const int h = item.count < 0 ? 34 : 28;
            if (y >= top && y < top + h)
                return &item;
            top += h;
        }
        return nullptr;
    }
    LibraryPanel& panel;
    juce::Point<int> hover { -1, -1 };
};

//==============================================================================
// The results: a list with a column header; double-click loads, right-click acts, sounds drag.

class LibraryPanel::Results final : public juce::Component, private juce::ListBoxModel
{
public:
    explicit Results (LibraryPanel& p) : panel (p)
    {
        list.setModel (this);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        list.setTitle ("Results");
        list.setOutlineThickness (0);
        list.setMultipleSelectionEnabled (false);
        addAndMakeVisible (list);
        empty.setJustificationType (juce::Justification::centred);
        empty.setColour (juce::Label::textColourId, colour::textMicro);
        empty.setFont (type::micro (15.0f));
        empty.setInterceptsMouseClicks (false, false);
        addChildComponent (empty);
    }
    juce::ListBox list;

    void update()
    {
        list.setRowHeight (panel.view == View::sounds ? 34 : 30);
        list.updateContent();
        list.repaint();
        // "Nothing here" only once the answer is in (not while the catalog is still asked).
        const bool none = panel.rows.empty() && panel.appliedGeneration == panel.queryGeneration;
        empty.setVisible (none);
        if (none)
            empty.setText (panel.search.getText().isNotEmpty() ? "Nothing matches \"" + panel.search.getText() + "\"."
                           : panel.scopeKey == "trash"           ? juce::String ("The trash is empty.")
                           : panel.view == View::sounds          ? juce::String ("No sounds here yet. Every sound you load into the instrument is kept here.")
                           : panel.view == View::presets         ? juce::String ("No presets here yet. Save one with Save\xe2\x80\xa6 (top right).")
                                                                 : juce::String ("No templates here yet."),
                           juce::dontSendNotification);
    }
    struct Column
    {
        juce::String title;
        int width;
    };
    std::vector<Column> columns() const
    {
        if (panel.view == View::sounds)
            return { { "", 150 }, { "Name", 0 }, { "Type", 100 }, { "Length", 64 }, { "Origin", 84 }, { "Date", 96 } };
        if (panel.view == View::templates)
            return { { "Name", 0 }, { "Category", 150 }, { "Origin", 120 }, { "Sources", 80 }, { "Rating", 100 }, { "Date", 110 } };
        return { { "Name", 0 }, { "Category", 160 }, { "Origin", 130 }, { "Rating", 100 }, { "Date", 110 } };
    }
    /** Each column's x and width for a row of `width` (the 0-width column takes the rest). */
    std::vector<juce::Range<int>> columnRanges (int width) const
    {
        const auto cols = columns();
        int fixed = 0;
        for (const auto& c : cols)
            fixed += c.width;
        std::vector<juce::Range<int>> out;
        int x = 12;
        for (const auto& c : cols)
        {
            const int w = c.width > 0 ? c.width : std::max (120, width - 24 - fixed);
            out.push_back ({ x, x + w });
            x += w;
        }
        return out;
    }
    void paint (juce::Graphics& g) override
    {
        const auto ranges = columnRanges (getWidth());
        const auto cols = columns();
        g.setColour (colour::textMicro);
        g.setFont (type::popupLabel (12.5f));
        for (std::size_t i = 0; i < cols.size(); ++i)
            g.drawText (cols[i].title, ranges[i].getStart(), 0, ranges[i].getLength(), 26, juce::Justification::centredLeft);
        g.setColour (colour::hairline);
        g.fillRect (0, 27, getWidth(), 1);
    }
    void resized() override
    {
        list.setBounds (getLocalBounds().withTrimmedTop (28));
        empty.setBounds (getLocalBounds().withTrimmedTop (28).withHeight (90));
    }

private:
    int getNumRows() override { return static_cast<int> (panel.rows.size()); }
    juce::String getNameForRow (int index) override
    {
        if (index < 0 || index >= getNumRows())
            return {};
        const auto& r = panel.rows[static_cast<std::size_t> (index)];
        return r.name + (r.category.isNotEmpty() ? ", " + r.category : juce::String()) + ", " + originName (r.origin);
    }
    void paintListBoxItem (int index, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (index < 0 || index >= getNumRows())
            return;
        const auto& r = panel.rows[static_cast<std::size_t> (index)];
        if (selected)
        {
            g.setColour (colour::panelBottom.darker (0.05f));
            g.fillRoundedRectangle (juce::Rectangle<float> (2.0f, 1.0f, static_cast<float> (width) - 4.0f, static_cast<float> (height) - 2.0f), 6.0f);
            g.setColour (colour::accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (3.0f, static_cast<float> (height) * 0.5f - 7.0f, 3.0f, 14.0f), 1.5f);
        }
        g.setColour (colour::hairline.withAlpha (0.55f));
        g.fillRect (8, height - 1, width - 16, 1);
        const auto ranges = columnRanges (width);
        auto cell = [&] (std::size_t c) { return juce::Rectangle<int> (ranges[c].getStart(), 0, ranges[c].getLength() - 8, height); };
        auto text = [&] (std::size_t c, const juce::String& s, bool primary = false) {
            g.setColour (primary ? colour::text : colour::textSecondary);
            g.setFont (primary ? type::micro (15.5f) : type::micro (14.0f));
            g.drawText (s, cell (c), juce::Justification::centredLeft, true);
        };
        const auto origin = originName (r.origin);
        if (panel.view == View::sounds)
        {
            const auto wave = cell (0).toFloat().reduced (0.0f, 7.0f);
            // Coloured by the sound's Type (the macros' mineral palette), quiet when it has none.
            const int typeIndex = soundTypes.indexOf (r.category, true);
            const auto tint = typeIndex >= 0 ? colour::macro (typeIndex % 6) : colour::textMicro;
            if (const auto* bins = panel.processor.audition().overview (r.id))
                drawOverview (g, wave, *bins, tint.withAlpha (0.85f));
            else
            {
                g.setColour (colour::hairline);
                g.fillRect (wave.withHeight (1.0f).withY (wave.getCentreY()));
            }
            text (1, r.name, true);
            text (2, r.category);
            text (3, lengthText (r.seconds));
            text (4, origin);
            text (5, dateText (r.created));
            if (r.favourite)
                drawHeart (g, juce::Rectangle<float> (static_cast<float> (cell (1).getRight()) - 14.0f, static_cast<float> (height) * 0.5f - 6.0f, 12.0f, 11.0f), true);
            return;
        }
        text (0, r.name, true);
        if (r.favourite)
            drawHeart (g, juce::Rectangle<float> (static_cast<float> (cell (0).getRight()) - 14.0f, static_cast<float> (height) * 0.5f - 6.0f, 12.0f, 11.0f), true);
        text (1, r.category);
        text (2, r.isBuiltIn() ? juce::String ("Factory (built in)") : origin);
        std::size_t c = 3;
        if (panel.view == View::templates)
            text (c++, r.layers > 0 ? juce::String (r.layers) : juce::String());
        if (! r.isBuiltIn())
            drawStars (g, cell (c).toFloat(), r.rating);
        text (c + 1, r.isBuiltIn() ? juce::String() : dateText (r.created));
    }
    void selectedRowsChanged (int) override { panel.selectionChanged(); }
    void listBoxItemClicked (int index, const juce::MouseEvent& e) override
    {
        if (index < 0 || index >= getNumRows())
            return;
        if (e.mods.isPopupMenu())
        {
            panel.showRowMenu (index, e.getScreenPosition());
            return;
        }
        // A click on the stars rates (the same star again clears).
        if (panel.view != View::sounds && ! panel.rows[static_cast<std::size_t> (index)].isBuiltIn())
        {
            const auto ranges = columnRanges (list.getWidth());
            const std::size_t c = panel.view == View::templates ? 4 : 3;
            const int star = starAt (static_cast<float> (e.x), static_cast<float> (ranges[c].getStart()));
            if (star >= 0 && e.x < ranges[c].getStart() + 75)
            {
                const auto& row = panel.rows[static_cast<std::size_t> (index)];
                const int stars = row.rating == star + 1 ? 0 : star + 1;
                panel.edit ([id = row.id, stars] (library::Catalog& cat) { return cat.setRating (id, stars); });
            }
        }
    }
    void listBoxItemDoubleClicked (int index, const juce::MouseEvent&) override
    {
        if (index >= 0 && index < getNumRows())
        {
            list.selectRow (index);
            panel.loadSelected();
        }
    }
    void returnKeyPressed (int) override { panel.loadSelected(); }
    void deleteKeyPressed (int index) override
    {
        if (index >= 0 && index < getNumRows())
            panel.trashRow (panel.rows[static_cast<std::size_t> (index)]);
    }
    juce::var getDragSourceDescription (const juce::SparseSet<int>& selected) override
    {
        if (panel.view != View::sounds || selected.isEmpty())
            return {};
        const int index = selected[0];
        return index >= 0 && index < getNumRows() ? dragDescription (panel.rows[static_cast<std::size_t> (index)].id) : juce::var();
    }

    LibraryPanel& panel;
    juce::Label empty;
};

//==============================================================================
// Presets and templates: the selected one's artwork, details and actions.

class LibraryPanel::Detail final : public juce::Component
{
public:
    explicit Detail (LibraryPanel& p) : panel (p)
    {
        addAndMakeVisible (load);
        addAndMakeVisible (more);
        more.setTitle ("More actions");
        load.onClick = [this] { panel.loadSelected(); };
        more.onClick = [this] {
            if (const int i = panel.selectedRow(); i >= 0)
                panel.showRowMenu (i, more.getScreenBounds().getBottomLeft());
        };
        setTitle ("Selected preset");
    }
    void update()
    {
        const int i = panel.selectedRow();
        const bool has = i >= 0;
        load.setVisible (has);
        more.setVisible (has && ! panel.row (i).isBuiltIn());
        if (has)
        {
            const auto& r = panel.row (i);
            load.setButtonText (r.trashed ? "Restore" : (panel.view == View::templates ? "Load Template" : "Load"));
        }
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        design::draw::raised (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f, colour::panelTop, colour::panelBottom, 0.6f);
        const int i = panel.selectedRow();
        if (i < 0)
        {
            g.setColour (colour::textMicro);
            g.setFont (type::micro (15.0f));
            g.drawText (panel.rows.empty() ? juce::String() : "Select a " + juce::String (panel.view == View::templates ? "template" : "preset") + " to see it here.",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }
        const auto& r = panel.row (i);
        const auto art = juce::Rectangle<float> (16.0f, 16.0f, static_cast<float> (getHeight()) - 32.0f, static_cast<float> (getHeight()) - 32.0f);
        drawLibraryArtwork (g, art, r.id, 8.0f);
        const float x = art.getRight() + 18.0f;
        const float w = static_cast<float> (load.getX()) - x - 20.0f;
        g.setColour (colour::text);
        g.setFont (type::preset (0.9f));
        g.drawText (r.name, juce::Rectangle<float> (x, 16.0f, w - 40.0f, 26.0f), juce::Justification::centredLeft, true);
        heart = juce::Rectangle<float> (x + w - 22.0f, 20.0f, 18.0f, 16.0f);
        if (! r.isBuiltIn())
            drawHeart (g, heart, r.favourite);
        juce::StringArray meta;
        meta.add (r.isBuiltIn() ? juce::String ("Factory") : originName (r.origin));
        if (r.category.isNotEmpty())
            meta.add (r.category);
        if (panel.view == View::presets && r.bytes > 0)
            meta.add (bytesText (r.bytes));
        if (panel.view == View::templates && r.layers > 0)
            meta.add (juce::String (r.layers) + (r.layers == 1 ? " source" : " sources"));
        if (r.trashed)
            meta.add ("in the trash");
        g.setColour (colour::textSecondary);
        g.setFont (type::micro (14.0f));
        g.drawText (meta.joinIntoString (juce::String::fromUTF8 ("  \xc2\xb7  ")), juce::Rectangle<float> (x, 44.0f, w, 20.0f), juce::Justification::centredLeft, true);
        juce::String description = r.notes;
        if (description.isEmpty())
            description = r.isBuiltIn() ? juce::String ("Built in, read only: the sounds you have stay; editing it and saving makes your own copy.")
                        : panel.view == View::templates ? juce::String ("Ready for your own sounds: settings only, no audio.")
                                                        : juce::String();
        g.setColour (colour::textSecondary.withAlpha (0.9f));
        g.drawFittedText (description, juce::Rectangle<float> (x, 66.0f, w, 40.0f).toNearestInt(), juce::Justification::topLeft, 2);
        if (! r.isBuiltIn())
        {
            stars = juce::Rectangle<float> (x, static_cast<float> (getHeight()) - 28.0f, 80.0f, 16.0f);
            drawStars (g, stars, r.rating);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        more.setBounds (r.removeFromRight (40).withSizeKeepingCentre (40, 36));
        r.removeFromRight (8);
        load.setBounds (r.removeFromRight (150).withSizeKeepingCentre (150, 38));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = panel.selectedRow();
        if (i < 0 || panel.row (i).isBuiltIn())
            return;
        const auto& r = panel.row (i);
        if (heart.expanded (4.0f).contains (e.position))
            panel.setFavourite (r, ! r.favourite);
        else if (stars.contains (e.position))
        {
            const int star = starAt (e.position.x, stars.getX());
            if (star >= 0)
                panel.edit ([id = r.id, stars = r.rating == star + 1 ? 0 : star + 1] (library::Catalog& c) { return c.setRating (id, stars); });
        }
    }

private:
    LibraryPanel& panel;
    PrimaryButton load { "Load" };
    juce::TextButton more { juce::String::fromUTF8 ("\xe2\x8b\xaf") };
    juce::Rectangle<float> heart, stars;
};

//==============================================================================
// The sound inspector (mockup panel 5): waveform and preview, Type, collections, tags, notes,
// properties, root, and where it goes.

class LibraryPanel::Inspector final : public juce::Component
{
public:
    explicit Inspector (LibraryPanel& p) : panel (p)
    {
        setTitle ("Sound inspector");
        for (auto* c : std::initializer_list<juce::Component*> { &play, &typeBox, &rootBox, &tagInput, &notes, &addCollection, &tags, &collections })
            addAndMakeVisible (c);
        play.setTitle ("Preview");
        play.onClick = [this] { panel.previewSelected(); };
        typeBox.setTitle ("Type");
        typeBox.setTextWhenNothingSelected ("Type");
        for (int i = 0; i < soundTypes.size(); ++i)
            typeBox.addItem (soundTypes[i], i + 1);
        typeBox.onChange = [this] {
            if (const auto* r = current())
                if (typeBox.getText() != r->category)
                    panel.edit ([id = r->id, t = typeBox.getText().toStdString()] (library::Catalog& c) { return c.setCategory (id, t); });
        };
        rootBox.setTitle ("Root note");
        rootBox.addItem ("Detected", 1);
        for (int n = 24; n <= 96; ++n)
            rootBox.addItem (noteName (n), n + 100);
        rootBox.onChange = [this] {
            const auto* r = current();
            if (r == nullptr)
                return;
            const int id = rootBox.getSelectedId();
            const std::optional<double> root = id > 100 ? std::optional<double> (id - 100) : std::nullopt;
            if (root == r->rootMidi)
                return;
            panel.edit ([aid = r->id, root] (library::Catalog& c) { return c.setRootMidi (aid, root); }, "Root note set");
        };
        tagInput.setTextToShowWhenEmpty ("Add tag", colour::textMicro);
        tagInput.setJustification (juce::Justification::centredLeft);
        tagInput.setIndents (8, 0);
        tagInput.setTitle ("Add tag");
        tagInput.onReturnKey = [this] {
            const auto text = tagInput.getText().trim();
            tagInput.clear();
            if (const auto* r = current(); r != nullptr && text.isNotEmpty())
                panel.edit ([id = r->id, t = text.toStdString()] (library::Catalog& c) { return c.addTag (id, t); });
        };
        tags.onRemove = [this] (const juce::String& tag) {
            if (const auto* r = current())
                panel.edit ([id = r->id, t = tag.toStdString()] (library::Catalog& c) { return c.removeTag (id, t); });
        };
        collections.onRemove = [this] (const juce::String& name) {
            const auto* r = current();
            if (r == nullptr || ! panel.detail)
                return;
            for (const auto& c : panel.detail->collections)
                if (juce::String (c.name) == name)
                    panel.edit ([cid = c.id, id = r->id] (library::Catalog& cat) { return cat.removeFromCollection (cid, id); });
        };
        addCollection.setTitle ("Add to collection");
        addCollection.onClick = [this] { showCollectionMenu(); };
        notes.setMultiLine (true, true);
        notes.setReturnKeyStartsNewLine (false);
        notes.setTextToShowWhenEmpty ("Notes", colour::textMicro);
        notes.setTitle ("Notes");
        notes.onFocusLost = [this] { commitNotes(); };
        notes.onReturnKey = [this] { commitNotes(); };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = loadButtons[static_cast<std::size_t> (i)];
            b.setButtonText (i < 3 ? "Load to " + OspAudioProcessor::layerName (i) : juce::String ("Load to First Empty"));
            b.setTitle (b.getButtonText());
            b.onClick = [this, i] {
                if (i < 3)
                    panel.loadSelectedInto (i);
                else
                    panel.loadSelected();
            };
            addAndMakeVisible (b);
        }
    }
    void update()
    {
        const auto* r = current();
        for (auto* c : std::initializer_list<juce::Component*> { &play, &typeBox, &rootBox, &tagInput, &notes, &addCollection, &tags, &collections })
            c->setVisible (r != nullptr && ! r->trashed);
        for (auto& b : loadButtons)
            b.setVisible (r != nullptr && ! r->trashed);
        if (r != nullptr)
        {
            typeBox.setText (r->category, juce::dontSendNotification);
            rootBox.setSelectedId (r->rootMidi ? juce::roundToInt (*r->rootMidi) + 100 : 1, juce::dontSendNotification);
            if (! notes.hasKeyboardFocus (true))
                notes.setText (r->notes, false);
            tags.setChips (r->tags);
            juce::StringArray names;
            if (panel.detail && panel.detail->id == r->id)
                for (const auto& c : panel.detail->collections)
                    names.add (c.name);
            collections.setChips (names);
        }
        resized();
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        design::draw::raised (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f, colour::panelTop, colour::panelBottom, 0.6f);
        const auto* r = current();
        if (r == nullptr)
        {
            g.setColour (colour::textMicro);
            g.setFont (type::micro (15.0f));
            g.drawFittedText ("Select a sound to hear it, tag it and choose where it goes.", getLocalBounds().reduced (30), juce::Justification::centred, 3);
            return;
        }
        g.setColour (colour::text);
        g.setFont (type::preset (0.82f));
        g.drawText (r->name, juce::Rectangle<int> (play.getRight() + 10, 14, getWidth() - play.getRight() - 50, 30), juce::Justification::centredLeft, true);
        heart = juce::Rectangle<float> (static_cast<float> (getWidth()) - 34.0f, 21.0f, 18.0f, 16.0f);
        drawHeart (g, heart, r->favourite);

        // The waveform: the prepared preview's overview, its playhead while it plays.
        design::draw::well (g, wave.toFloat(), 8.0f, colour::wellA);
        const auto& slot = panel.processor.audition().slot (library::PreviewEngine::browserSlot);
        if (slot.assetId == r->id && slot.sound != nullptr)
        {
            const auto& s = *slot.sound;
            const auto area = wave.toFloat().reduced (8.0f, 10.0f);
            const float step = area.getWidth() / static_cast<float> (s.peakMax.size());
            g.setColour (colour::identity (1).wave.withAlpha (0.9f));
            for (std::size_t i = 0; i < s.peakMax.size(); ++i)
            {
                const float top = area.getCentreY() - s.peakMax[i] * area.getHeight() * 0.5f;
                const float bottom = area.getCentreY() - s.peakMin[i] * area.getHeight() * 0.5f;
                g.fillRect (juce::Rectangle<float> (area.getX() + static_cast<float> (i) * step, top, std::max (1.0f, step), std::max (1.0f, bottom - top)));
            }
            const double head = panel.processor.audition().playheadSeconds (library::PreviewEngine::browserSlot);
            if (head >= 0.0 && s.durationSeconds() > 0.0)
            {
                const float x = area.getX() + area.getWidth() * static_cast<float> (head / s.durationSeconds());
                g.setColour (colour::accent);
                g.fillRect (juce::Rectangle<float> (x, area.getY() - 4.0f, 1.5f, area.getHeight() + 8.0f));
            }
            g.setColour (colour::wellText);
            g.setFont (type::micro (12.5f));
            g.drawText (lengthText (s.durationSeconds()) + (s.rootKnown ? juce::String ("   root ") + noteName (s.rootMidi) : juce::String ("   no clear pitch")),
                        wave.reduced (10, 4), juce::Justification::bottomRight);
        }
        else
        {
            g.setColour (colour::wellText);
            g.setFont (type::micro (13.0f));
            g.drawText (slot.assetId == r->id && slot.error.isNotEmpty() ? slot.error : juce::String::fromUTF8 ("Preparing\xe2\x80\xa6"), wave, juce::Justification::centred);
        }

        auto label = [&g] (const juce::String& text, int x, int y) {
            g.setColour (colour::textMicro);
            g.setFont (type::popupLabel (12.0f));
            g.drawText (text.toUpperCase(), x, y, 120, 18, juce::Justification::centredLeft);
        };
        label ("Type", 16, typeBox.getY() - 20);
        label ("Root note", rootBox.getX(), rootBox.getY() - 20);
        label ("Collections", 16, collections.getY() - 20);
        label ("Tags", 16, tags.getY() - 20);
        label ("Properties", 16, propertiesTop);

        // Properties, two columns.
        juce::StringArray left, right;
        left.add ("Length  " + lengthText (r->seconds));
        left.add (juce::String ("Channels  ") + (r->channels == 1 ? "Mono" : r->channels == 2 ? "Stereo" : juce::String (r->channels)));
        left.add ("Added  " + dateText (r->created));
        left.add ("Last used  " + (r->lastUsed > 0 ? dateText (r->lastUsed) : juce::String ("never")));
        right.add ("Rate  " + (r->sampleRate > 0.0 ? juce::String (r->sampleRate / 1000.0, 1) + " kHz" : juce::String()));
        right.add ("Size  " + (r->bytes > 0 ? bytesText (r->bytes) : juce::String()));
        right.add ("Format  " + r->format.toUpperCase() + (r->bitDepth > 0 ? " " + juce::String (r->bitDepth) + "-bit" : juce::String()));
        right.add ("Uses  " + juce::String (panel.detail && panel.detail->id == r->id ? panel.detail->uses : 0));
        g.setFont (type::micro (13.5f));
        for (int i = 0; i < left.size(); ++i)
        {
            g.setColour (colour::textSecondary);
            g.drawText (left[i], 16, propertiesTop + 18 + i * 18, getWidth() / 2 - 16, 18, juce::Justification::centredLeft, true);
            g.drawText (right[i], getWidth() / 2, propertiesTop + 18 + i * 18, getWidth() / 2 - 16, 18, juce::Justification::centredLeft, true);
        }
        if (panel.detail && panel.detail->id == r->id)
        {
            const auto place = panel.detail->places.isEmpty() ? juce::String() : panel.detail->places[0];
            g.setColour (panel.detail->resolvedWhere.isEmpty() ? colour::accent : colour::textMicro);
            g.setFont (type::micro (12.5f));
            g.drawText (panel.detail->resolvedWhere.isEmpty() ? "Missing: last seen at " + place : place, 16, propertiesTop + 92, getWidth() - 32, 18,
                        juce::Justification::centredLeft, true);
        }
    }
    void resized() override
    {
        const int w = getWidth();
        play.setBounds (14, 14, 32, 30);
        wave = { 16, 52, w - 32, 86 };
        int y = wave.getBottom() + 30;
        typeBox.setBounds (16, y, w / 2 - 24, 30);
        rootBox.setBounds (w / 2 + 8, y, w / 2 - 24, 30);
        y += 30 + 26;
        const int ch = std::max (28, collections.preferredHeight (w - 70));
        collections.setBounds (16, y, w - 70, ch);
        addCollection.setBounds (w - 48, y, 32, 26);
        y += ch + 24;
        const int th = std::max (28, tags.preferredHeight (w - 32));
        tags.setBounds (16, y, w - 32, th);
        y += th + 2;
        tagInput.setBounds (16, y, w - 32, 28);
        y += 34;
        notes.setBounds (16, y, w - 32, 36);
        y += 44;
        propertiesTop = y;
        const int bw = (w - 32 - 12) / 3;
        for (int i = 0; i < 3; ++i)
            loadButtons[static_cast<std::size_t> (i)].setBounds (16 + i * (bw + 6), getHeight() - 84, bw, 32);
        loadButtons[3].setBounds (16, getHeight() - 46, w - 32, 32);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const auto* r = current();
        if (r == nullptr)
            return;
        if (heart.expanded (4.0f).contains (e.position))
            panel.setFavourite (*r, ! r->favourite);
        else if (wave.contains (e.getPosition()))
            panel.previewSelected();
    }

private:
    const LibraryRow* current() const
    {
        const int i = panel.selectedRow();
        return i >= 0 ? &panel.row (i) : nullptr;
    }
    void commitNotes()
    {
        if (const auto* r = current(); r != nullptr && notes.getText() != r->notes)
            panel.edit ([id = r->id, text = notes.getText().toStdString()] (library::Catalog& c) { return c.setNotes (id, text); });
    }
    void showCollectionMenu()
    {
        const auto* r = current();
        if (r == nullptr)
            return;
        juce::Component::SafePointer<LibraryPanel> safe (&panel);
        const auto id = r->id;
        panel.processor.libraryService().request<std::vector<library::Collection>> (
            [] (library::Catalog& c) { return c.collections(); },
            [safe, id, this] (std::vector<library::Collection> all) {
                if (safe == nullptr)
                    return;
                juce::PopupMenu menu;
                for (const auto& c : all)
                    menu.addItem (juce::String (c.name), [safe, cid = c.id, id] {
                        if (safe != nullptr)
                            safe->edit ([cid, id] (library::Catalog& cat) { return cat.addToCollection (cid, id); });
                    });
                if (! all.empty())
                    menu.addSeparator();
                menu.addItem (juce::String::fromUTF8 ("New collection\xe2\x80\xa6"), [safe, id] {
                    if (safe != nullptr)
                        safe->prompt ("New collection", {}, [safe, id] (const juce::String& name) {
                            if (safe != nullptr)
                                safe->edit ([name, id] (library::Catalog& c) {
                                    const auto made = c.createCollection (name.toStdString());
                                    return made && c.addToCollection (*made, id);
                                });
                        });
                });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addCollection));
            });
        panel.processor.libraryService().deliver();
    }

    LibraryPanel& panel;
    juce::TextButton play { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::ComboBox typeBox, rootBox;
    juce::TextEditor tagInput, notes;
    juce::TextButton addCollection { "+" };
    ChipRow tags, collections;
    std::array<juce::TextButton, 4> loadButtons;
    juce::Rectangle<int> wave;
    juce::Rectangle<float> heart;
    int propertiesTop = 0;
};

//==============================================================================
// The A/B/C audition tray (mockup panel 4): sounds heard together before they are committed.

class LibraryPanel::Tray final : public juce::Component, public juce::DragAndDropTarget
{
public:
    explicit Tray (LibraryPanel& p) : panel (p)
    {
        setTitle ("Audition tray");
        for (auto* c : std::initializer_list<juce::Component*> { &playAll, &stop, &keys, &commit, &level })
            addAndMakeVisible (c);
        playAll.setTitle ("Play A, B and C together");
        playAll.onClick = [this] { panel.processor.audition().play (library::PreviewEngine::trayMask); };
        stop.setTitle ("Stop");
        stop.onClick = [this] { panel.processor.audition().stop(); };
        keys.setTitle ("Play the tray on the keyboard");
        keys.setClickingTogglesState (true);
        keys.onClick = [this] { updateKeys(); };
        commit.onClick = [this] { panel.commitTray(); };
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        level.setRange (-36.0, 6.0, 0.5);
        level.setValue (panel.processor.audition().gainDb(), juce::dontSendNotification);
        level.setTitle ("Preview level");
        level.setTooltip ("Preview level (yours, not the patch's)");
        styleLevel (level);
        level.onDragEnd = [this] { panel.processor.audition().setGainDb (static_cast<float> (level.getValue())); };
        level.onValueChange = [this] { panel.processor.audition().setGainDb (static_cast<float> (level.getValue())); };
    }
    ~Tray() override { panel.processor.setPreviewKeys (false, 0u); }
    void updateKeys()
    {
        unsigned mask = 0;
        for (int s = 0; s < Audition::numTraySlots; ++s)
            if (panel.processor.audition().slot (s).sound != nullptr)
                mask |= 1u << s;
        if (mask == 0)
            mask = 1u << library::PreviewEngine::browserSlot;   // an empty tray: the keyboard plays the selected sound
        panel.processor.setPreviewKeys (keys.getToggleState(), mask);
        keys.setButtonText (keys.getToggleState() ? "Keys play the audition" : "Keys play the instrument");
    }
    juce::Rectangle<int> slotBounds (int s) const
    {
        const int w = (getWidth() - 16 - 260 - 3 * 10) / 3;
        return { 16 + s * (w + 10), 34, w, 100 };
    }
    void paint (juce::Graphics& g) override
    {
        design::draw::raised (g, getLocalBounds().toFloat().reduced (1.0f), 12.0f, colour::panelTop, colour::panelBottom, 0.6f);
        g.setColour (colour::textMicro);
        g.setFont (type::popupTitle (12.5f));
        g.drawText ("AUDITION  A  B  C", 16, 8, 300, 20, juce::Justification::centredLeft);
        auto& audition = panel.processor.audition();
        for (int s = 0; s < Audition::numTraySlots; ++s)
        {
            const auto r = slotBounds (s).toFloat();
            const auto& slot = audition.slot (s);
            const bool target = dragOver == s;
            design::draw::well (g, r, 9.0f, target ? colour::wellA.brighter (0.15f) : colour::wellA);
            const auto& id = colour::identity (s);
            const auto badge = juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 10.0f, 22.0f, 22.0f);
            g.setGradientFill (juce::ColourGradient (id.badgeTop, badge.getX(), badge.getY(), id.badgeBottom, badge.getX(), badge.getBottom(), false));
            g.fillRoundedRectangle (badge, 5.0f);
            g.setColour (juce::Colours::white);
            g.setFont (type::button (14.0f));
            g.drawText (OspAudioProcessor::layerName (s), badge, juce::Justification::centred);
            if (slot.assetId.empty())
            {
                g.setColour (colour::wellText);
                g.setFont (type::micro (13.0f));
                g.drawText ("Drag a sound here", r.reduced (10.0f).withTrimmedTop (24.0f).toNearestInt(), juce::Justification::centred);
                continue;
            }
            const auto waveArea = r.withTrimmedTop (38.0f).withTrimmedBottom (24.0f).reduced (10.0f, 2.0f);
            if (slot.sound != nullptr)
            {
                const auto& snd = *slot.sound;
                const float step = waveArea.getWidth() / static_cast<float> (snd.peakMax.size());
                g.setColour (id.wave.withAlpha (0.9f));
                for (std::size_t i = 0; i < snd.peakMax.size(); ++i)
                {
                    const float top = waveArea.getCentreY() - snd.peakMax[i] * waveArea.getHeight() * 0.5f;
                    const float bottom = waveArea.getCentreY() - snd.peakMin[i] * waveArea.getHeight() * 0.5f;
                    g.fillRect (juce::Rectangle<float> (waveArea.getX() + static_cast<float> (i) * step, top, std::max (1.0f, step), std::max (1.0f, bottom - top)));
                }
                const double head = audition.playheadSeconds (s);
                if (head >= 0.0 && snd.durationSeconds() > 0.0)
                {
                    g.setColour (colour::accent);
                    g.fillRect (juce::Rectangle<float> (waveArea.getX() + waveArea.getWidth() * static_cast<float> (head / snd.durationSeconds()), waveArea.getY(), 1.5f,
                                                        waveArea.getHeight()));
                }
            }
            g.setColour (slot.error.isNotEmpty() ? colour::accent : colour::wellText.brighter (0.4f));
            g.setFont (type::micro (13.0f));
            g.drawText (slot.error.isNotEmpty() ? slot.error : (slot.loading ? juce::String::fromUTF8 ("Preparing\xe2\x80\xa6") : slot.name),
                        juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 22.0f, r.getWidth() - 20.0f, 18.0f).toNearestInt(), juce::Justification::centredLeft, true);
            // Play this slot (the badge's right) and clear it (the corner cross).
            const auto playMark = juce::Rectangle<float> (badge.getRight() + 8.0f, badge.getY() + 4.0f, 12.0f, 14.0f);
            juce::Path triangle;
            triangle.addTriangle (playMark.getX(), playMark.getY(), playMark.getX(), playMark.getBottom(), playMark.getRight(), playMark.getCentreY());
            g.setColour (colour::wellText.brighter (0.5f));
            g.fillPath (triangle);
            const auto cross = juce::Rectangle<float> (r.getRight() - 22.0f, r.getY() + 14.0f, 9.0f, 9.0f);
            g.drawLine (cross.getX(), cross.getY(), cross.getRight(), cross.getBottom(), 1.4f);
            g.drawLine (cross.getRight(), cross.getY(), cross.getX(), cross.getBottom(), 1.4f);
        }
        g.setColour (colour::textMicro);
        g.setFont (type::popupLabel (11.5f));
        g.drawText ("PREVIEW LEVEL", level.getX(), level.getY() - 14, level.getWidth(), 14, juce::Justification::centredLeft);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (16, 12);
        auto right = r.removeFromRight (250);
        commit.setBounds (right.removeFromBottom (40));
        right.removeFromBottom (8);
        keys.setBounds (right.removeFromBottom (30));
        right.removeFromBottom (6);
        auto transport = right.removeFromBottom (30);
        playAll.setBounds (transport.removeFromLeft (44));
        transport.removeFromLeft (6);
        stop.setBounds (transport.removeFromLeft (44));
        transport.removeFromLeft (12);
        level.setBounds (transport.withTrimmedTop (10));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int s = 0; s < Audition::numTraySlots; ++s)
        {
            const auto r = slotBounds (s).toFloat();
            if (! r.contains (e.position))
                continue;
            const auto& slot = panel.processor.audition().slot (s);
            if (slot.assetId.empty())
            {
                panel.addSelectedToTray (s);   // a click on an empty slot takes the selected sound
                return;
            }
            if (juce::Rectangle<float> (r.getRight() - 30.0f, r.getY() + 6.0f, 26.0f, 26.0f).contains (e.position))
                panel.processor.audition().clearSlot (s);
            else if (e.mods.isPopupMenu())
            {
                juce::PopupMenu menu;
                juce::Component::SafePointer<LibraryPanel> safe (&panel);
                menu.addItem ("Replace with the selected sound", [safe, s] { if (safe != nullptr) safe->addSelectedToTray (s); });
                menu.addItem ("Clear", [safe, s] { if (safe != nullptr) safe->processor.audition().clearSlot (s); });
                menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
            }
            else
                panel.processor.audition().play (1u << s);
            updateKeys();
            return;
        }
    }
    bool isInterestedInDragSource (const SourceDetails& d) override { return ! assetFromDrag (d.description).empty(); }
    void itemDragMove (const SourceDetails& d) override
    {
        int over = -1;
        for (int s = 0; s < Audition::numTraySlots; ++s)
            if (slotBounds (s).contains (d.localPosition))
                over = s;
        if (over != dragOver)
        {
            dragOver = over;
            repaint();
        }
    }
    void itemDragExit (const SourceDetails&) override
    {
        dragOver = -1;
        repaint();
    }
    void itemDropped (const SourceDetails& d) override
    {
        const auto id = assetFromDrag (d.description);
        for (int s = 0; s < Audition::numTraySlots; ++s)
            if (slotBounds (s).contains (d.localPosition) && ! id.empty())
                panel.processor.audition().setSlot (s, id);
        dragOver = -1;
        updateKeys();
        repaint();
    }

private:
    LibraryPanel& panel;
    juce::TextButton playAll { juce::String::fromUTF8 ("\xe2\x96\xb6") }, stop { juce::String::fromUTF8 ("\xe2\x96\xa0") };
    juce::TextButton keys { "Keys play the instrument" };
    PrimaryButton commit { "Load to Instrument" };
    juce::Slider level;
    int dragOver = -1;
};

//==============================================================================
// Sheets: a card over the dimmed window (save, settings, missing sounds, a name, a choice).

class LibraryPanel::Sheet : public juce::Component
{
public:
    Sheet (LibraryPanel& p, juce::String t, juce::Point<int> size) : panel (p), title (std::move (t)), cardSize (size)
    {
        setTitle (title);
        setWantsKeyboardFocus (true);
    }
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (cardSize.x, cardSize.y); }
    juce::Rectangle<int> content() const { return card().reduced (24).withTrimmedTop (40); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (colour::housingBottom.withAlpha (0.72f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
        design::draw::raised (g, card().toFloat(), 14.0f, colour::cardTop, colour::cardBottom, 1.4f);
        g.setColour (colour::text);
        g.setFont (type::panelHeader());
        g.drawText (title, card().reduced (24, 18).withHeight (24), juce::Justification::centredLeft);
        paintContent (g);
    }
    virtual void paintContent (juce::Graphics&) {}
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! card().contains (e.getPosition()))
            panel.closeSheet();   // a click beside the card closes it (nothing is changed)
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            panel.closeSheet();
            return true;
        }
        return false;
    }

protected:
    LibraryPanel& panel;
    juce::String title;
    juce::Point<int> cardSize;
};

namespace
{
    /** A name (rename, a new collection). */
    class PromptSheet final : public LibraryPanel::Sheet
    {
    public:
        PromptSheet (LibraryPanel& p, const juce::String& t, const juce::String& initial, std::function<void (const juce::String&)> d)
            : Sheet (p, t, { 460, 170 }), done (std::move (d))
        {
            field.setText (initial, false);
            field.setTitle (t);
            field.onReturnKey = [this] { ok.triggerClick(); };
            field.onEscapeKey = [this] { panel.closeSheet(); };
            addAndMakeVisible (field);
            addAndMakeVisible (cancel);
            addAndMakeVisible (ok);
            cancel.onClick = [this] { panel.closeSheet(); };
            ok.onClick = [this] {
                const auto text = field.getText().trim();
                if (text.isEmpty())
                    return;
                auto callback = done;
                panel.closeSheet();
                callback (text);
            };
        }
        void resized() override
        {
            auto r = content();
            field.setBounds (r.removeFromTop (34));
            r.removeFromTop (16);
            auto buttons = r.removeFromTop (38);
            ok.setBounds (buttons.removeFromRight (130));
            buttons.removeFromRight (8);
            cancel.setBounds (buttons.removeFromRight (100));
        }
        void visibilityChanged() override
        {
            if (isVisible())
                field.grabKeyboardFocus();
        }
        juce::TextEditor field;

    private:
        std::function<void (const juce::String&)> done;
        juce::TextButton cancel { "Cancel" };
        PrimaryButton ok { "OK" };
    };

    /** A question with a few answers (the last is the way out). */
    class ChoiceSheet final : public LibraryPanel::Sheet
    {
    public:
        ChoiceSheet (LibraryPanel& p, const juce::String& m, const juce::StringArray& options, std::function<void (int)> d)
            : Sheet (p, "Library", { 520, 190 }), message (m), done (std::move (d))
        {
            for (int i = 0; i < options.size(); ++i)
            {
                auto b = std::make_unique<juce::TextButton> (options[i]);
                b->setTitle (options[i]);
                b->onClick = [this, i] {
                    auto callback = done;
                    panel.closeSheet();
                    callback (i);
                };
                addAndMakeVisible (*b);
                buttons.push_back (std::move (b));
            }
        }
        void paintContent (juce::Graphics& g) override
        {
            g.setColour (colour::textSecondary);
            g.setFont (type::micro (15.0f));
            g.drawFittedText (message, content().withHeight (60), juce::Justification::topLeft, 3);
        }
        void resized() override
        {
            auto r = content().withTrimmedTop (70).withHeight (38);
            const int n = static_cast<int> (buttons.size());
            const int w = n > 0 ? std::min (150, (r.getWidth() - 8 * (n - 1)) / n) : 0;
            for (auto it = buttons.rbegin(); it != buttons.rend(); ++it)
            {
                (*it)->setBounds (r.removeFromRight (w));
                r.removeFromRight (8);
            }
        }
        std::vector<std::unique_ptr<juce::TextButton>> buttons;

    private:
        juce::String message;
        std::function<void (int)> done;
    };
}

//==============================================================================

LibraryPanel::LibraryPanel (OspAudioProcessor& p) : processor (p)
{
    setTitle ("ANDOR/OSP Library");
    setWantsKeyboardFocus (true);
    const char* names[] { "Presets", "Templates", "Sounds" };
    for (std::size_t i = 0; i < tabs.size(); ++i)
    {
        tabs[i].setButtonText (names[i]);
        tabs[i].setTitle (names[i]);
        tabs[i].setRadioGroupId (4711);
        tabs[i].setClickingTogglesState (true);
        tabs[i].onClick = [this, i] {
            if (tabs[i].getToggleState())
                setView (static_cast<View> (i));
        };
        addAndMakeVisible (tabs[i]);
    }
    closeButton.setTitle ("Close the Library");
    closeButton.onClick = [this] {
        if (onClose != nullptr)
            onClose();
    };
    menuButton.setTitle ("Library settings");
    menuButton.onClick = [this] {
        juce::PopupMenu menu;
        juce::Component::SafePointer<LibraryPanel> safe (this);
        menu.addItem (juce::String::fromUTF8 ("Library settings\xe2\x80\xa6"), [safe] { if (safe != nullptr) safe->openSettingsSheet(); });
        menu.addItem (juce::String::fromUTF8 ("Missing sounds\xe2\x80\xa6"), [safe] { if (safe != nullptr) safe->openMissingSheet(); });
        menu.addSeparator();
        menu.addItem ("Show presets folder", [] { OspAudioProcessor::presetFolder().createDirectory(); OspAudioProcessor::presetFolder().revealToUser(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
    };
    saveButton.setTitle ("Save the patch to the Library");
    saveButton.onClick = [this] { openSaveSheet(); };
    backButton.setTitle ("Back to the patch before the last load");
    backButton.onClick = [this] {
        if (processor.restorePreviousState())
        {
            setStatus ("Back to the previous patch", processor.canRestorePreviousState());
            if (onPatchChanged != nullptr)
                onPatchChanged();
        }
    };
    for (auto* c : std::initializer_list<juce::Component*> { &closeButton, &menuButton, &saveButton, &search, &originFilter, &lengthFilter, &sortOrder })
        addAndMakeVisible (c);
    addChildComponent (backButton);
    search.setTextToShowWhenEmpty (juce::String::fromUTF8 ("Search\xe2\x80\xa6"), colour::textMicro);
    search.setTitle ("Search the Library");
    search.setFont (type::micro (16.0f));
    search.setJustification (juce::Justification::centredLeft);
    search.setIndents (12, 0);
    search.onTextChange = [this] { refresh(); };
    search.onEscapeKey = [this] {
        if (search.getText().isNotEmpty())
            search.clear(), refresh();
        else if (onClose != nullptr)
            onClose();
    };
    search.onReturnKey = [this] {
        if (rowCount() > 0)
        {
            selectRow (std::max (0, selectedRow()));
            results->list.grabKeyboardFocus();
        }
    };
    originFilter.setTitle ("Origin");
    originFilter.addItemList ({ "All Origins", "Factory", "User", "Captured", "Packs", "External" }, 1);
    originFilter.setSelectedId (1, juce::dontSendNotification);
    originFilter.onChange = [this] { refresh(); };
    lengthFilter.setTitle ("Length");
    lengthFilter.addItemList ({ "Any Length", "Short (under 1 s)", "1 to 5 s", "Long (over 5 s)" }, 1);
    lengthFilter.setSelectedId (1, juce::dontSendNotification);
    lengthFilter.onChange = [this] { refresh(); };
    sortOrder.setTitle ("Sort");
    sortOrder.addItemList ({ "Name", "Recently used", "Newest", "Rating", "Length" }, 1);
    sortOrder.setSelectedId (1, juce::dontSendNotification);
    sortOrder.onChange = [this] { refresh(); };

    sidebar = std::make_unique<Sidebar> (*this);
    addAndMakeVisible (*sidebar);
    results = std::make_unique<Results> (*this);
    addAndMakeVisible (*results);
    detailStrip = std::make_unique<Detail> (*this);
    addAndMakeVisible (*detailStrip);
    inspector = std::make_unique<Inspector> (*this);
    addChildComponent (*inspector);
    tray = std::make_unique<Tray> (*this);
    addChildComponent (*tray);

    processor.audition().onChange = [safe = juce::Component::SafePointer<LibraryPanel> (this)] {
        if (safe != nullptr)
        {
            safe->results->list.repaint();
            safe->inspector->repaint();
            safe->tray->repaint();
        }
    };
    tabs[0].setToggleState (true, juce::dontSendNotification);
    indexPresetFiles();
    setView (View::presets);
    startTimerHz (30);
}

LibraryPanel::~LibraryPanel()
{
    processor.audition().onChange = nullptr;
    processor.audition().stop();
    processor.setPreviewKeys (false, 0u);
}

library::AssetType LibraryPanel::typeFor (View v) const
{
    return v == View::sounds ? AssetType::sound : (v == View::templates ? AssetType::templateState : AssetType::preset);
}

juce::StringArray LibraryPanel::categoriesFor (View v) const
{
    return v == View::sounds ? soundTypes : (v == View::templates ? templateCategories : presetCategories);
}

void LibraryPanel::setView (View v)
{
    if (sheetComponent != nullptr)
        closeSheet();
    view = v;
    tabs[static_cast<std::size_t> (v)].setToggleState (true, juce::dontSendNotification);
    scopeKey = "all";
    detail.reset();
    rows.clear();   // nothing of the last view while the new one is asked for
    side.clear();
    results->list.deselectAllRows();
    const bool sounds = v == View::sounds;
    inspector->setVisible (sounds);
    tray->setVisible (sounds);
    detailStrip->setVisible (! sounds);
    originFilter.setVisible (sounds);
    lengthFilter.setVisible (sounds);
    if (! sounds)
        processor.setPreviewKeys (false, 0u);
    search.setTextToShowWhenEmpty (juce::String::fromUTF8 (sounds ? "Search sounds\xe2\x80\xa6" : (v == View::templates ? "Search templates\xe2\x80\xa6" : "Search presets\xe2\x80\xa6")),
                                   colour::textMicro);
    resized();
    refresh();
    results->update();
}

void LibraryPanel::selectScope (const juce::String& key)
{
    scopeKey = key;
    sidebar->repaint();
    refresh();
}

void LibraryPanel::setSearchText (const juce::String& text)
{
    search.setText (text, false);
    refresh();
}

int LibraryPanel::selectedRow() const
{
    const int i = results->list.getSelectedRow();
    return i >= 0 && i < rowCount() ? i : -1;
}

void LibraryPanel::selectRow (int index)
{
    results->list.selectRow (index);
    selectionChanged();
}

void LibraryPanel::refresh()
{
    query();
}

void LibraryPanel::query()
{
    // What the window shows, asked for on the Library thread; only the newest answer counts.
    library::SearchQuery q;
    q.text = search.getText().trim().toStdString();
    q.type = typeFor (view);
    q.limit = 500;
    q.sort = std::array<library::SearchQuery::Sort, 5> { library::SearchQuery::Sort::name, library::SearchQuery::Sort::recentlyUsed,
                                                          library::SearchQuery::Sort::added, library::SearchQuery::Sort::rating,
                                                          library::SearchQuery::Sort::length }[static_cast<std::size_t> (juce::jlimit (0, 4, sortOrder.getSelectedId() - 1))];
    const auto key = scopeKey;
    bool builtIns = view == View::templates;
    if (key == "factory")
        q.origin = Origin::factory;
    else if (key == "user")
        q.origin = Origin::user, builtIns = false;
    else if (key == "packs")
        q.origin = Origin::pack, builtIns = false;
    else if (key == "captured")
        q.origin = Origin::captured, builtIns = false;
    else if (key == "favourites")
        q.favouritesOnly = true, builtIns = false;
    else if (key == "recent")
        q.recentOnly = true, q.sort = library::SearchQuery::Sort::recentlyUsed, builtIns = false;
    else if (key == "trash")
        q.trashedOnly = true, builtIns = false;
    else if (key.startsWith ("collection:"))
        q.collection = key.fromFirstOccurrenceOf ("collection:", false, false).toStdString(), builtIns = false;
    else if (key.startsWith ("category:"))
        q.category = key.fromFirstOccurrenceOf ("category:", false, false).toStdString(), builtIns = false;
    if (view == View::sounds)
    {
        static const std::optional<Origin> origins[] { std::nullopt, Origin::factory, Origin::user, Origin::captured, Origin::pack, Origin::external };
        if (const auto o = origins[juce::jlimit (0, 5, originFilter.getSelectedId() - 1)])
            q.origin = o;
        switch (lengthFilter.getSelectedId())
        {
            case 2: q.maxSeconds = 1.0; break;
            case 3: q.minSeconds = 1.0, q.maxSeconds = 5.0; break;
            case 4: q.minSeconds = 5.0; break;
            default: break;
        }
    }
    const auto currentView = view;
    const auto factoryNames = builtIns ? OspAudioProcessor::factoryStartingStates() : juce::StringArray();
    const auto type = typeFor (view);
    const auto fixedCategories = categoriesFor (view);
    const auto generation = ++queryGeneration;
    struct Answer
    {
        std::vector<LibraryRow> rows;
        std::vector<SideItem> side;
    };
    juce::Component::SafePointer<LibraryPanel> safe (this);
    processor.libraryService().request<Answer> (
        [q, factoryNames, type, fixedCategories, currentView] (library::Catalog& c) {
            Answer a;
            if (q.origin != Origin::user && q.origin != Origin::pack)
                for (int i = 0; i < factoryNames.size(); ++i)
                {
                    if (! q.text.empty() && ! factoryNames[i].containsIgnoreCase (juce::String (q.text).trim()))
                        continue;
                    LibraryRow r;
                    r.id = "program:" + std::to_string (i);
                    r.type = AssetType::templateState;
                    r.origin = Origin::factory;
                    r.name = factoryNames[i];
                    r.category = "Starting state";
                    r.program = i;
                    a.rows.push_back (r);
                }
            for (const auto& asset : c.search (q))
            {
                LibraryRow r;
                r.id = asset.id;
                r.type = asset.type;
                r.origin = asset.origin;
                r.name = juce::String (asset.name);
                r.category = juce::String (asset.category);
                r.notes = juce::String (asset.notes);
                r.rating = asset.rating;
                r.favourite = asset.favourite;
                r.trashed = asset.trashed;
                r.created = asset.created;
                r.lastUsed = c.lastUsed (asset.id);
                for (const auto& t : c.tagsOf (asset.id))
                    r.tags.add (juce::String (t.name));
                if (const auto s = c.sound (asset.id))
                {
                    r.contentHash = s->contentHash;
                    r.seconds = s->durationSeconds;
                    r.sampleRate = s->sampleRate;
                    r.channels = s->channels;
                    r.bitDepth = s->bitDepth;
                    r.bytes = s->fileSize;
                    r.format = juce::String (s->format);
                    r.rootMidi = s->rootMidi;
                }
                if (const auto p = c.preset (asset.id))
                {
                    r.file = juce::File (juce::String (p->file));
                    r.layers = p->layers;
                    r.bytes = c.presetBytes (asset.id);
                }
                a.rows.push_back (std::move (r));
            }
            // The sidebar with counts (mockup panels 2-4).
            auto count = [&c, type] (auto setup) {
                library::SearchQuery s;
                s.type = type;
                setup (s);
                return c.count (s);
            };
            const bool sounds = currentView == View::sounds;
            a.side.push_back ({ "header:library", sounds ? "Library" : "Collections", -1 });
            const int builtInCount = currentView == View::templates ? factoryNames.size() : 0;
            a.side.push_back ({ "all", sounds ? "All Sounds" : (currentView == View::templates ? "All Templates" : "All Presets"),
                                count ([] (library::SearchQuery&) {}) + builtInCount });
            if (! sounds)
                a.side.push_back ({ "factory", "Factory", count ([] (library::SearchQuery& s) { s.origin = Origin::factory; }) + builtInCount });
            a.side.push_back ({ "user", "User", count ([] (library::SearchQuery& s) { s.origin = Origin::user; }) });
            if (sounds)
                a.side.push_back ({ "captured", "Captured", count ([] (library::SearchQuery& s) { s.origin = Origin::captured; }) });
            a.side.push_back ({ "packs", "Packs", count ([] (library::SearchQuery& s) { s.origin = Origin::pack; }) });
            a.side.push_back ({ "favourites", "Favorites", count ([] (library::SearchQuery& s) { s.favouritesOnly = true; }) });
            a.side.push_back ({ "recent", "Recent", count ([] (library::SearchQuery& s) { s.recentOnly = true; }) });
            a.side.push_back ({ "trash", "Trash", count ([] (library::SearchQuery& s) { s.trashedOnly = true; }) });
            a.side.push_back ({ "header:collections", "My collections", -1 });
            for (const auto& col : c.collections (type))
                a.side.push_back ({ "collection:" + juce::String (col.id), juce::String (col.name), col.count });
            a.side.push_back ({ "header:categories", sounds ? "Types" : "Categories", -1 });
            auto categories = fixedCategories;
            std::map<juce::String, int> counts;
            for (const auto& [name, n] : c.categoryCounts (type))
            {
                counts[juce::String (name).toLowerCase()] = n;
                if (! categories.contains (juce::String (name), true))
                    categories.add (juce::String (name));
            }
            for (const auto& name : categories)
                a.side.push_back ({ "category:" + name, name, counts[name.toLowerCase()] });
            return a;
        },
        [safe, generation] (Answer a) {
            if (safe == nullptr || generation != safe->queryGeneration)
                return;
            const auto keep = safe->selectedRow() >= 0 ? safe->row (safe->selectedRow()).id : std::string();
            safe->rows = std::move (a.rows);
            safe->side = std::move (a.side);
            safe->appliedGeneration = generation;
            safe->results->update();
            int select = -1;
            for (int i = 0; i < safe->rowCount(); ++i)
                if (safe->rows[static_cast<std::size_t> (i)].id == keep)
                    select = i;
            safe->results->list.selectRow (select, false, true);
            safe->sidebar->setSize (sidebarWidth, std::max (safe->sidebar->contentHeight(), 10));
            safe->resized();
            safe->sidebar->repaint();
            safe->selectionChanged();
        });
}

bool LibraryPanel::settle (int milliseconds)
{
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (milliseconds);
    while (juce::Time::getMillisecondCounter() < until)
    {
        processor.audition().waitUntilIdle (milliseconds);
        processor.libraryService().waitUntilIdle (milliseconds);
        processor.audition().deliver();
        processor.libraryService().deliver();
        if (appliedGeneration == queryGeneration && processor.libraryService().waitUntilIdle (1) && processor.audition().waitUntilIdle (1))
        {
            processor.audition().deliver();
            processor.libraryService().deliver();
            if (appliedGeneration == queryGeneration)
                return true;
        }
    }
    return false;
}

void LibraryPanel::selectionChanged()
{
    detailStrip->update();
    const int i = selectedRow();
    if (view == View::sounds && i >= 0 && ! row (i).trashed)
    {
        processor.audition().cue (row (i).id);
        requestDetail();
    }
    inspector->update();
    tray->updateKeys();
}

void LibraryPanel::requestDetail()
{
    const int i = selectedRow();
    if (i < 0)
        return;
    const auto id = row (i).id;
    const auto hash = row (i).contentHash;
    const auto storeDir = std::filesystem::path (SampleStore::defaultDirectory().getFullPathName().toStdString());
    juce::Component::SafePointer<LibraryPanel> safe (this);
    processor.libraryService().request<LibrarySoundDetail> (
        [id, hash, storeDir] (library::Catalog& c) {
            LibrarySoundDetail d;
            d.id = id;
            for (const auto& place : c.locations (hash))
                if (place != "(store)")
                    d.places.add (juce::String (place));
            const auto where = library::resolveSound (c, storeDir, hash);
            d.resolvedWhere = where.file ? juce::String (where.where) : juce::String();
            d.collections = c.collectionsOf (id);
            d.uses = c.useCount (id);
            return d;
        },
        [safe] (LibrarySoundDetail d) {
            if (safe == nullptr)
                return;
            safe->detail = std::move (d);
            safe->inspector->update();
        });
}

void LibraryPanel::setStatus (const juce::String& text, bool offerBack)
{
    status = text;
    statusOffersBack = offerBack && processor.canRestorePreviousState();
    backButton.setVisible (statusOffersBack);
    repaint (footerArea);
}

void LibraryPanel::edit (std::function<bool (library::Catalog&)> change, const juce::String& done)
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    processor.libraryService().request<bool> (std::move (change), [safe, done] (bool ok) {
        if (safe == nullptr)
            return;
        if (! ok)
            safe->setStatus ("That did not work: the Library is busy or the item is gone. Nothing was changed.");
        else if (done.isNotEmpty())
            safe->setStatus (done);
        safe->refresh();
    });
}

void LibraryPanel::indexPresetFiles()
{
    // Presets and templates saved before the Library existed (or copied into the folders by
    // hand) join the catalog when the window opens. Reading them is the Library thread's.
    std::vector<std::pair<juce::File, bool>> files;
    for (const auto& f : OspAudioProcessor::findFiles (OspAudioProcessor::presetFolder(), OspAudioProcessor::presetExtension))
        files.emplace_back (f, false);
    for (const auto& f : OspAudioProcessor::findFiles (OspAudioProcessor::startingStateFolder(), OspAudioProcessor::startingStateExtension))
        files.emplace_back (f, true);
    juce::StringArray favouriteNames;
    for (const auto& [f, isTemplate] : files)
        if (processor.isFavouriteName (f.getFileNameWithoutExtension()))
            favouriteNames.add (f.getFullPathName());
    processor.libraryService().post ([files, favouriteNames] (library::Catalog& c) {
        for (const auto& [file, isTemplate] : files)
        {
            const auto path = file.getFullPathName().toStdString();
            auto id = c.presetWithFile (path);
            if (! id)
            {
                library::Asset a;
                a.type = isTemplate ? AssetType::templateState : AssetType::preset;
                a.origin = Origin::user;
                a.name = file.getFileNameWithoutExtension().toStdString();
                a.created = file.getCreationTime().toMilliseconds() / 1000;
                library::PresetInfo info;
                info.file = path;
                info.root = "user";
                if (const auto xml = juce::XmlDocument::parse (file))
                {
                    info.stateVersion = xml->getIntAttribute ("stateVersion", 0);
                    if (! isTemplate)
                        info.soundHashes = OspAudioProcessor::soundHashesInState (*xml);
                    info.layers = isTemplate ? xml->getIntAttribute ("keptSlots", 1) : static_cast<int> (info.soundHashes.size());
                }
                id = c.addPreset (a, info);
            }
            // The header's heart (names in Favourites.txt) and the Library's agree.
            if (id)
                c.setFavourite (*id, favouriteNames.contains (file.getFullPathName()));
        }
    });
}

void LibraryPanel::setFavourite (const LibraryRow& r, bool favourite)
{
    if (r.type != AssetType::sound && r.file != juce::File())
        processor.setFavouriteName (r.file.getFileNameWithoutExtension(), favourite);
    edit ([id = r.id, favourite] (library::Catalog& c) { return c.setFavourite (id, favourite); },
          favourite ? r.name + " is a favourite" : r.name + " is no longer a favourite");
}

void LibraryPanel::loadSelected()
{
    const int i = selectedRow();
    if (i < 0)
        return;
    const auto& r = row (i);
    if (r.trashed)
    {
        restoreRow (r);
        return;
    }
    if (view != View::sounds)
    {
        loadRow (r);
        return;
    }
    // A sound into the first empty layer; when all three hold one, the user chooses (A is
    // never replaced silently).
    const int free = processor.firstFreeLayer();
    if (free >= 0)
    {
        loadSound (r.id, free);
        return;
    }
    juce::Component::SafePointer<LibraryPanel> safe (this);
    choose ("A, B and C all hold a sound. Which one should " + r.name + " replace? (The patch before it is kept: Back undoes it.)",
            { "Replace A", "Replace B", "Replace C", "Cancel" }, [safe, id = r.id] (int choice) {
                if (safe != nullptr && choice >= 0 && choice < 3)
                    safe->loadSound (id, choice);
            });
}

void LibraryPanel::loadSelectedInto (int layer)
{
    const int i = selectedRow();
    if (i >= 0 && view == View::sounds && ! row (i).trashed)
        loadSound (row (i).id, layer);
}

void LibraryPanel::loadSound (const std::string& assetId, int layer)
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    setStatus (juce::String::fromUTF8 ("Loading\xe2\x80\xa6"));
    processor.audition().loadIntoLayer (assetId, layer, [safe] (bool ok, const juce::String& message) {
        if (safe == nullptr)
            return;
        safe->setStatus (message, ok);
        if (ok && safe->onPatchChanged != nullptr)
            safe->onPatchChanged();
        safe->refresh();
    });
}

void LibraryPanel::loadRow (const LibraryRow& r)
{
    bool ok = false;
    if (r.isBuiltIn())
    {
        OspAudioProcessor::PresetEntry entry;
        entry.name = r.name;
        entry.program = r.program;
        entry.startingState = true;
        processor.openPresetEntry (entry);
        ok = true;
    }
    else if (! r.file.existsAsFile())
    {
        setStatus (r.name + " cannot be opened: " + r.file.getFullPathName() + " is not there any more.");
        return;
    }
    else
        ok = r.type == AssetType::templateState ? processor.loadStartingState (r.file) : processor.loadPreset (r.file);
    if (! ok)
    {
        setStatus (r.name + " could not be opened (the file may be damaged or from a newer version).");
        return;
    }
    if (! r.isBuiltIn())
        edit ([id = r.id] (library::Catalog& c) { return c.recordUse (id, "loaded"); });
    setStatus ("Loaded " + r.name, true);
    if (onPatchChanged != nullptr)
        onPatchChanged();
}

void LibraryPanel::renameRow (const LibraryRow& r)
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    prompt ("Rename", r.name, [safe, r] (const juce::String& name) {
        if (safe == nullptr)
            return;
        if (r.type == AssetType::sound)
        {
            safe->edit ([id = r.id, name] (library::Catalog& c) { return c.rename (id, name.toStdString()); }, "Renamed to " + name);
            return;
        }
        juce::String error;
        if (! safe->processor.renamePresetFile (r.file, name, error))
            safe->setStatus ("Could not rename: " + error);
        else
            safe->setStatus ("Renamed to " + name);
        safe->processor.libraryService().post ([] (library::Catalog&) {});
        safe->refresh();
    });
}

void LibraryPanel::trashRow (const LibraryRow& r)
{
    if (r.isBuiltIn() || r.trashed)
        return;
    if (r.type == AssetType::sound)
    {
        edit ([id = r.id] (library::Catalog& c) { return c.trash (id); }, r.name + " moved to the trash (Trash: Restore brings it back)");
        return;
    }
    juce::String error;
    if (processor.trashPresetFile (r.file, error))
        setStatus (r.name + " moved to the trash (Trash: Restore brings it back)");
    else
        setStatus ("Could not move it: " + error);
    refresh();
}

void LibraryPanel::restoreRow (const LibraryRow& r)
{
    if (r.type == AssetType::sound)
    {
        edit ([id = r.id] (library::Catalog& c) { return c.restore (id); }, r.name + " restored");
        return;
    }
    juce::String error;
    if (processor.restorePresetFile (r.file, error))
        setStatus (r.name + " restored");
    else
        setStatus ("Could not restore it: " + error);
    refresh();
}

void LibraryPanel::showRowMenu (int index, juce::Point<int> where)
{
    if (index < 0 || index >= rowCount())
        return;
    results->list.selectRow (index);
    const auto r = row (index);
    juce::PopupMenu menu;
    juce::Component::SafePointer<LibraryPanel> safe (this);
    if (r.trashed)
    {
        menu.addItem ("Restore", [safe, r] { if (safe != nullptr) safe->restoreRow (r); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ where.x, where.y, 1, 1 }));
        return;
    }
    if (r.type == AssetType::sound)
    {
        menu.addItem ("Preview", [safe] { if (safe != nullptr) safe->previewSelected(); });
        for (int s = 0; s < 3; ++s)
            menu.addItem ("Audition in " + OspAudioProcessor::layerName (s), [safe, s] { if (safe != nullptr) safe->addSelectedToTray (s); });
        menu.addSeparator();
        for (int l = 0; l < 3; ++l)
            menu.addItem ("Load into " + OspAudioProcessor::layerName (l), [safe, l] { if (safe != nullptr) safe->loadSelectedInto (l); });
        menu.addItem ("Load into first empty", [safe] { if (safe != nullptr) safe->loadSelected(); });
        menu.addSeparator();
    }
    else
    {
        menu.addItem (r.type == AssetType::templateState ? "Load template" : "Load", [safe, r] { if (safe != nullptr) safe->loadRow (r); });
        if (r.isBuiltIn())
        {
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ where.x, where.y, 1, 1 }));
            return;
        }
        menu.addItem ("Duplicate", [safe, r] {
            if (safe == nullptr)
                return;
            juce::String error;
            const auto copy = safe->processor.duplicatePresetFile (r.file, error);
            safe->setStatus (copy ? "Duplicated as " + copy->getFileNameWithoutExtension() : "Could not duplicate: " + error);
            safe->refresh();
        });
        menu.addItem (juce::String::fromUTF8 ("Export file\xe2\x80\xa6"), [safe, r] {
            if (safe == nullptr)
                return;
            safe->chooser = std::make_unique<juce::FileChooser> ("Export " + r.name, juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (r.file.getFileName()),
                                                                 "*" + r.file.getFileExtension());
            safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                        [safe, r] (const juce::FileChooser& fc) {
                                            if (safe == nullptr || fc.getResult() == juce::File())
                                                return;
                                            const bool ok = r.file.copyFileTo (fc.getResult());
                                            safe->setStatus (ok ? "Exported to " + fc.getResult().getFullPathName() : juce::String ("Could not export the file."));
                                        });
        });
        if (r.type == AssetType::preset && r.file == processor.currentPresetFile())
            menu.addItem (juce::String::fromUTF8 ("Export as portable instrument\xe2\x80\xa6"), [safe, r] {
                if (safe == nullptr)
                    return;
                safe->chooser = std::make_unique<juce::FileChooser> ("Export " + r.name, OspAudioProcessor::instrumentFolder().getChildFile (r.name + OspAudioProcessor::instrumentExtension),
                                                                     juce::String ("*") + OspAudioProcessor::instrumentExtension);
                safe->chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                            [safe, r] (const juce::FileChooser& fc) {
                                                if (safe == nullptr || fc.getResult() == juce::File())
                                                    return;
                                                OspAudioProcessor::ExportOptions options;
                                                options.name = r.name;
                                                options.category = r.category;
                                                options.notes = r.notes;
                                                options.tags = r.tags;
                                                juce::String error;
                                                const bool ok = safe->processor.exportInstrument (fc.getResult(), error, options);
                                                safe->setStatus (ok ? "Exported " + fc.getResult().getFileName() : "Could not export: " + error);
                                            });
            });
        juce::PopupMenu categories;
        for (const auto& c : categoriesFor (view))
            categories.addItem (c, true, c == r.category, [safe, id = r.id, c] {
                if (safe != nullptr)
                    safe->edit ([id, c] (library::Catalog& cat) { return cat.setCategory (id, c.toStdString()); });
            });
        menu.addSubMenu ("Category", categories);
        menu.addItem (juce::String::fromUTF8 ("Describe\xe2\x80\xa6"), [safe, r] {
            if (safe != nullptr)
                safe->prompt ("Description", r.notes, [safe, id = r.id] (const juce::String& text) {
                    if (safe != nullptr)
                        safe->edit ([id, text] (library::Catalog& c) { return c.setNotes (id, text.toStdString()); });
                });
        });
        menu.addItem ("Show in Finder", [r] { r.file.revealToUser(); });
    }
    menu.addItem (r.favourite ? "Remove from favourites" : "Add to favourites", [safe, r] { if (safe != nullptr) safe->setFavourite (r, ! r.favourite); });
    juce::PopupMenu rating;
    for (int s = 0; s <= 5; ++s)
        rating.addItem (s == 0 ? juce::String ("No rating") : juce::String::repeatedString (juce::String::fromUTF8 ("\xe2\x98\x85"), s), true, s == r.rating,
                        [safe, id = r.id, s] {
                            if (safe != nullptr)
                                safe->edit ([id, s] (library::Catalog& c) { return c.setRating (id, s); });
                        });
    menu.addSubMenu ("Rating", rating);
    menu.addItem (juce::String::fromUTF8 ("Rename\xe2\x80\xa6"), [safe, r] { if (safe != nullptr) safe->renameRow (r); });
    menu.addSeparator();
    menu.addItem ("Move to Trash", [safe, r] { if (safe != nullptr) safe->trashRow (r); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ where.x, where.y, 1, 1 }));
}

void LibraryPanel::previewSelected()
{
    const int i = selectedRow();
    if (i < 0 || view != View::sounds || row (i).trashed)
        return;
    auto& audition = processor.audition();
    const auto& slot = audition.slot (library::PreviewEngine::browserSlot);
    if (slot.assetId == row (i).id && slot.sound != nullptr)
        audition.play (1u << library::PreviewEngine::browserSlot);
    else
        audition.preview (row (i).id);
}

void LibraryPanel::addSelectedToTray (int slot)
{
    const int i = selectedRow();
    if (i < 0 || view != View::sounds || row (i).trashed)
        return;
    processor.audition().setSlot (slot, row (i).id);
    setStatus (row (i).name + " is in " + OspAudioProcessor::layerName (slot) + " of the audition");
    tray->repaint();
}

void LibraryPanel::commitTray()
{
    juce::Component::SafePointer<LibraryPanel> safe (this);
    setStatus (juce::String::fromUTF8 ("Loading the combination\xe2\x80\xa6"));
    processor.audition().commit ([safe] (bool ok, const juce::String& message) {
        if (safe == nullptr)
            return;
        safe->setStatus (message, ok);
        if (ok && safe->onPatchChanged != nullptr)
            safe->onPatchChanged();
        safe->refresh();
    });
}

//==============================================================================
// Sheets

void LibraryPanel::showSheet (std::unique_ptr<juce::Component> component, const juce::String& name)
{
    sheetComponent = std::move (component);
    sheetName = name;
    addAndMakeVisible (*sheetComponent);
    sheetComponent->setBounds (getLocalBounds());
    sheetComponent->grabKeyboardFocus();
}

void LibraryPanel::closeSheet()
{
    if (sheetComponent == nullptr)
        return;
    // Kept until the next sheet closes: a sheet's own button may be what closed it, and its
    // callback is still running.
    sheetComponent->setVisible (false);
    retiredSheet = std::move (sheetComponent);
    sheetName.clear();
    grabKeyboardFocus();
}

juce::String LibraryPanel::openSheetName() const
{
    return sheetComponent != nullptr ? sheetName : juce::String();
}

void LibraryPanel::prompt (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
{
    showSheet (std::make_unique<PromptSheet> (*this, title, initial, std::move (done)), "prompt");
}

void LibraryPanel::choose (const juce::String& message, juce::StringArray options, std::function<void (int)> done)
{
    showSheet (std::make_unique<ChoiceSheet> (*this, message, options, std::move (done)), "choice");
}

namespace
{
    /** View E: saving the patch as a preset (with its sounds) or a template (without). */
    class SaveSheet final : public LibraryPanel::Sheet
    {
    public:
        SaveSheet (LibraryPanel& p, OspAudioProcessor& proc, bool asTemplate) : Sheet (p, "Save to Library", { 620, 470 }), processor (proc)
        {
            name.setTitle ("Name");
            name.setText (proc.presetDisplayName() == "INIT" ? juce::String() : proc.presetDisplayName(), false);
            name.setTextToShowWhenEmpty ("My Evolving Texture", colour::textMicro);
            preset.setButtonText ("Preset (with sounds): the complete instrument with its current sounds");
            templateType.setButtonText ("Template (no sounds): settings only, audio files will not be included");
            preset.setRadioGroupId (77);
            templateType.setRadioGroupId (77);
            (asTemplate ? templateType : preset).setToggleState (true, juce::dontSendNotification);
            preset.onClick = templateType.onClick = [this] { updateType(); };
            category.setTitle ("Category");
            tags.setTitle ("Tags");
            tags.setTextToShowWhenEmpty ("Tags, separated by commas", colour::textMicro);
            notes.setTitle ("Description");
            notes.setTextToShowWhenEmpty ("Description (optional)", colour::textMicro);
            portable.setButtonText ("Include audio as portable copy (recommended for sharing): an .ospinstrument beside it");
            for (auto* c : std::initializer_list<juce::Component*> { &name, &preset, &templateType, &category, &tags, &notes, &portable, &cancel, &save })
                addAndMakeVisible (c);
            name.onReturnKey = [this] { save.triggerClick(); };
            cancel.onClick = [this] { panel.closeSheet(); };
            save.onClick = [this] { commit(); };
            updateType();
        }
        void updateType()
        {
            const bool isTemplate = templateType.getToggleState();
            category.clear (juce::dontSendNotification);
            category.addItemList (isTemplate ? templateCategories : presetCategories, 1);
            category.setTextWhenNothingSelected ("Category");
            portable.setEnabled (! isTemplate && processor.occupiedLayerCount() > 0);
            if (isTemplate)
                portable.setToggleState (false, juce::dontSendNotification);
        }
        void paintContent (juce::Graphics& g) override
        {
            g.setColour (colour::textMicro);
            g.setFont (type::popupLabel (12.0f));
            for (const auto& [text, c] : std::initializer_list<std::pair<const char*, juce::Component*>> { { "NAME", &name }, { "TYPE", &preset }, { "CATEGORY", &category }, { "TAGS", &tags } })
                g.drawText (text, c->getX(), c->getY() - 18, 200, 16, juce::Justification::centredLeft);
            g.drawText ("ORIGIN  USER", category.getRight() + 20, category.getY() - 18, 200, 16, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = content().withTrimmedTop (18);
            name.setBounds (r.removeFromTop (34));
            r.removeFromTop (28);
            preset.setBounds (r.removeFromTop (26));
            templateType.setBounds (r.removeFromTop (26));
            r.removeFromTop (28);
            category.setBounds (r.removeFromTop (32).withWidth (240));
            r.removeFromTop (26);
            tags.setBounds (r.removeFromTop (32));
            r.removeFromTop (10);
            notes.setBounds (r.removeFromTop (32));
            r.removeFromTop (12);
            portable.setBounds (r.removeFromTop (26));
            auto buttons = r.removeFromBottom (40);
            save.setBounds (buttons.removeFromRight (170));
            buttons.removeFromRight (8);
            cancel.setBounds (buttons.removeFromRight (110));
        }
        void visibilityChanged() override
        {
            if (isVisible())
                name.grabKeyboardFocus();
        }
        void commit()
        {
            const auto fileTitle = juce::File::createLegalFileName (name.getText().trim());
            if (fileTitle.isEmpty())
            {
                name.grabKeyboardFocus();
                return;
            }
            const bool isTemplate = templateType.getToggleState();
            const auto folder = isTemplate ? OspAudioProcessor::startingStateFolder() : OspAudioProcessor::presetFolder();
            const auto file = folder.getChildFile (fileTitle + (isTemplate ? OspAudioProcessor::startingStateExtension : OspAudioProcessor::presetExtension));
            juce::Component::SafePointer<LibraryPanel> safe (&panel);
            auto write = [safe, file, isTemplate, cat = category.getText(), tagText = tags.getText(), text = notes.getText(),
                          withAudio = portable.getToggleState(), &proc = processor] {
                if (safe == nullptr)
                    return;
                file.getParentDirectory().createDirectory();
                const bool ok = isTemplate ? proc.saveStartingState (file) : proc.savePreset (file);
                if (! ok)
                {
                    safe->setStatus ("Could not save " + file.getFileName() + ".");
                    return;
                }
                juce::StringArray list;
                list.addTokens (tagText, ",", "");
                list.trim();
                list.removeEmptyStrings();
                const auto path = file.getFullPathName().toStdString();
                // The record exists once the processor's own save has been indexed (the same
                // queue, in order); then its category, tags and description.
                safe->edit ([path, cat, list, text] (library::Catalog& c) {
                    const auto id = c.presetWithFile (path);
                    if (! id)
                        return false;
                    c.setCategory (*id, cat.toStdString());
                    c.setNotes (*id, text.toStdString());
                    for (const auto& t : list)
                        c.addTag (*id, t.toStdString());
                    return true;
                }, "Saved " + file.getFileNameWithoutExtension());
                if (withAudio)
                {
                    OspAudioProcessor::ExportOptions options;
                    options.name = file.getFileNameWithoutExtension();
                    options.category = cat;
                    options.notes = text;
                    options.tags = list;
                    juce::String error;
                    const auto package = OspAudioProcessor::instrumentFolder().getChildFile (file.getFileNameWithoutExtension() + OspAudioProcessor::instrumentExtension);
                    OspAudioProcessor::instrumentFolder().createDirectory();
                    if (! proc.exportInstrument (package, error, options))
                        safe->setStatus ("Saved, but the portable copy failed: " + error);
                }
                if (safe->onPatchChanged != nullptr)
                    safe->onPatchChanged();
            };
            if (! file.existsAsFile())
            {
                panel.closeSheet();
                write();
                return;
            }
            // Never overwritten silently: the old file goes to the Library's trash first.
            auto& proc = processor;
            panel.closeSheet();
            {
                safe->showSheet (std::make_unique<ChoiceSheet> (*safe, file.getFileNameWithoutExtension() + " exists. Replace it? The old one goes to the trash (Restore brings it back).",
                                                                juce::StringArray { "Replace", "Cancel" },
                                                                [safe, file, write, &proc] (int choice) {
                                                                    if (safe == nullptr || choice != 0)
                                                                        return;
                                                                    juce::String error;
                                                                    if (! proc.trashPresetFile (file, error))
                                                                    {
                                                                        safe->setStatus ("Could not replace it: " + error);
                                                                        return;
                                                                    }
                                                                    write();
                                                                }),
                                 "choice");
            }
        }

    private:
        OspAudioProcessor& processor;
        juce::TextEditor name, tags, notes;
        juce::ToggleButton preset, templateType, portable;
        juce::ComboBox category;
        juce::TextButton cancel { "Cancel" };
        PrimaryButton save { "Save to Library" };
    };

    /** View J: the Library's own settings - preview level, storage, the catalog. */
    class SettingsSheet final : public LibraryPanel::Sheet
    {
    public:
        SettingsSheet (LibraryPanel& p, OspAudioProcessor& proc) : Sheet (p, "Library settings", { 640, 470 }), processor (proc)
        {
            level.setSliderStyle (juce::Slider::LinearHorizontal);
            level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
            level.setRange (-36.0, 6.0, 0.5);
            level.setTextValueSuffix (" dB");
            level.setValue (proc.audition().gainDb(), juce::dontSendNotification);
            level.setTitle ("Preview level");
            styleLevel (level);
            level.onValueChange = [this] { processor.audition().setGainDb (static_cast<float> (level.getValue())); };
            emptyTrash.setTitle ("Empty the Library trash");
            emptyTrash.onClick = [this] { confirmEmpty(); };
            showStore.onClick = [] { SampleStore::defaultDirectory().revealToUser(); };
            showLibrary.onClick = [] { juce::File (juce::String (library::Catalog::defaultFile().string())).revealToUser(); };
            close.onClick = [this] { panel.closeSheet(); };
            for (auto* c : std::initializer_list<juce::Component*> { &level, &emptyTrash, &showStore, &showLibrary, &close })
                addAndMakeVisible (c);
            measure();
        }
        void measure()
        {
            juce::Component::SafePointer<SettingsSheet> safe (this);
            const auto storeDir = std::filesystem::path (SampleStore::defaultDirectory().getFullPathName().toStdString());
            processor.libraryService().request<std::pair<library::StorageUsage, std::string>> (
                [storeDir] (library::Catalog& c) { return std::make_pair (library::storageUsage (c, storeDir), c.integrityCheck()); },
                [safe] (std::pair<library::StorageUsage, std::string> answer) {
                    if (safe == nullptr)
                        return;
                    safe->usage = answer.first;
                    safe->integrity = juce::String (answer.second);
                    safe->measured = true;
                    safe->emptyTrash.setEnabled (true);
                    safe->repaint();
                });
        }
        void paintContent (juce::Graphics& g) override
        {
            auto r = content();
            g.setColour (colour::textMicro);
            g.setFont (type::popupLabel (12.0f));
            g.drawText ("PREVIEW LEVEL (YOURS, NOT THE PATCH'S)", r.getX(), level.getY() - 18, 400, 16, juce::Justification::centredLeft);
            g.drawText ("STORAGE", r.getX(), storageTop, 300, 16, juce::Justification::centredLeft);
            g.setColour (colour::textSecondary);
            g.setFont (type::micro (14.5f));
            const auto unavailable = processor.libraryService().unavailableReason();
            juce::StringArray lines;
            if (unavailable.isNotEmpty())
                lines.add ("The Library is not available: " + unavailable);
            else if (! measured)
                lines.add (juce::String::fromUTF8 ("Measuring\xe2\x80\xa6"));
            else
            {
                lines.add ("Managed copies: " + juce::String (usage.files) + " sounds, " + bytesText (usage.bytes));
                lines.add ("   in the Library " + juce::String (usage.library) + " (" + bytesText (usage.libraryBytes) + "), only in the trash " + juce::String (usage.trashOnly) + " ("
                           + bytesText (usage.trashOnlyBytes) + "), from before the Library " + juce::String (usage.untracked) + " (" + bytesText (usage.untrackedBytes) + ")");
                lines.add ("Every sound you load keeps a copy here, so projects and presets open when the original moves.");
                lines.add ("Catalog check: " + integrity);
            }
            for (int i = 0; i < lines.size(); ++i)
                g.drawFittedText (lines[i], r.getX(), storageTop + 20 + i * 22, r.getWidth(), 22, juce::Justification::centredLeft, 1);
            g.setColour (colour::textMicro);
            g.setFont (type::micro (13.0f));
            g.drawFittedText ("Emptying the trash removes its records for good and moves copies nothing else uses (no other sound, preset or open layer) to the "
                              "system Trash. A project that is not open and uses one of them opens it from its original file, when that still exists.",
                              juce::Rectangle<int> (r.getX(), emptyTrash.getBottom() + 8, r.getWidth(), 54), juce::Justification::topLeft, 3);
        }
        void resized() override
        {
            auto r = content().withTrimmedTop (18);
            level.setBounds (r.removeFromTop (30).withWidth (360));
            r.removeFromTop (24);
            storageTop = r.getY();
            r.removeFromTop (20 + 4 * 22 + 12);
            auto row = r.removeFromTop (36);
            emptyTrash.setBounds (row.removeFromLeft (220));
            row.removeFromLeft (8);
            showStore.setBounds (row.removeFromLeft (170));
            row.removeFromLeft (8);
            showLibrary.setBounds (row.removeFromLeft (170));
            close.setBounds (content().removeFromBottom (38).removeFromRight (110));
        }

    private:
        void confirmEmpty()
        {
            juce::Component::SafePointer<LibraryPanel> safe (&panel);
            auto& proc = processor;
            panel.closeSheet();
            {
                safe->showSheet (std::make_unique<ChoiceSheet> (*safe, "Empty the Library trash? Its records go for good; copies nothing else uses go to the system Trash.",
                                                                juce::StringArray { "Empty trash", "Cancel" },
                                                                [safe, &proc] (int choice) {
                                                                    if (safe == nullptr || choice != 0)
                                                                        return;
                                                                    const auto storeDir = std::filesystem::path (SampleStore::defaultDirectory().getFullPathName().toStdString());
                                                                    const auto inUse = proc.soundsInUse();
                                                                    proc.libraryService().request<library::TrashEmptied> (
                                                                        [storeDir, inUse] (library::Catalog& c) {
                                                                            return library::emptyTrash (c, storeDir, inUse, [] (const std::filesystem::path& file) {
                                                                                return juce::File (juce::String (file.string())).moveToTrash();
                                                                            });
                                                                        },
                                                                        [safe] (library::TrashEmptied done) {
                                                                            if (safe == nullptr)
                                                                                return;
                                                                            safe->setStatus ("Trash emptied: " + juce::String (done.records) + " records, " + juce::String (done.files)
                                                                                             + " files (" + bytesText (done.bytes) + ") moved to the system Trash"
                                                                                             + (done.failed.empty() ? juce::String() : ", " + juce::String (static_cast<int> (done.failed.size())) + " could not be moved"));
                                                                            safe->refresh();
                                                                        });
                                                                }),
                                 "choice");
            }
        }
        OspAudioProcessor& processor;
        juce::Slider level;
        juce::TextButton emptyTrash { juce::String::fromUTF8 ("Empty Library trash\xe2\x80\xa6") }, showStore { "Show sample store" }, showLibrary { "Show Library folder" };
        juce::TextButton close { "Done" };
        library::StorageUsage usage;
        juce::String integrity;
        bool measured = false;
        int storageTop = 0;
    };

    /** View K: sounds whose bytes cannot be found anywhere the Library knows, and relinking them. */
    class MissingSheet final : public LibraryPanel::Sheet, private juce::ListBoxModel
    {
    public:
        struct Missing
        {
            std::string id, hash;
            juce::String name, lastPlace;
        };
        MissingSheet (LibraryPanel& p, OspAudioProcessor& proc) : Sheet (p, "Missing sounds", { 760, 520 }), processor (proc)
        {
            list.setModel (this);
            list.setRowHeight (40);
            list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
            list.setTitle ("Missing sounds");
            addAndMakeVisible (list);
            locate.onClick = [this] { relinkSelected(); };
            forget.onClick = [this] { trashSelected(); };
            close.onClick = [this] { panel.closeSheet(); };
            for (auto* c : std::initializer_list<juce::Component*> { &locate, &forget, &close })
                addAndMakeVisible (c);
            scan();
        }
        void scan()
        {
            juce::Component::SafePointer<MissingSheet> safe (this);
            const auto storeDir = std::filesystem::path (SampleStore::defaultDirectory().getFullPathName().toStdString());
            processor.libraryService().request<std::vector<Missing>> (
                [storeDir] (library::Catalog& c) {
                    std::vector<Missing> found;
                    library::SearchQuery q;
                    q.type = AssetType::sound;
                    q.limit = 100000;
                    for (const auto& a : c.search (q))
                        if (const auto s = c.sound (a.id))
                        {
                            const auto where = library::resolveSound (c, storeDir, s->contentHash);
                            if (! where.file)
                                found.push_back ({ a.id, s->contentHash, juce::String (a.name), where.missing.empty() ? juce::String() : juce::String (where.missing.front().string()) });
                        }
                    return found;
                },
                [safe] (std::vector<Missing> found) {
                    if (safe == nullptr)
                        return;
                    safe->items = std::move (found);
                    safe->scanned = true;
                    safe->list.updateContent();
                    safe->repaint();
                });
        }
        void paintContent (juce::Graphics& g) override
        {
            g.setColour (colour::textSecondary);
            g.setFont (type::micro (14.5f));
            g.drawText (! scanned       ? juce::String::fromUTF8 ("Looking for every sound\xe2\x80\xa6")
                        : items.empty() ? juce::String ("Every sound in the Library is where it should be.")
                                        : juce::String (static_cast<int> (items.size())) + " sounds cannot be found (not in the store, not where they were seen). Locate finds one by its bytes.",
                        content().withHeight (22), juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = content();
            r.removeFromTop (30);
            auto buttons = r.removeFromBottom (38);
            close.setBounds (buttons.removeFromRight (110));
            buttons.removeFromRight (8);
            forget.setBounds (buttons.removeFromLeft (190));
            buttons.removeFromLeft (8);
            locate.setBounds (buttons.removeFromLeft (150));
            r.removeFromBottom (10);
            list.setBounds (r);
        }

    private:
        int getNumRows() override { return static_cast<int> (items.size()); }
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (row < 0 || row >= getNumRows())
                return;
            const auto& m = items[static_cast<std::size_t> (row)];
            if (selected)
            {
                g.setColour (colour::panelBottom.darker (0.05f));
                g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 1.0f, static_cast<float> (width), static_cast<float> (height) - 2.0f), 6.0f);
            }
            g.setColour (colour::text);
            g.setFont (type::micro (15.0f));
            g.drawText (m.name, 10, 2, width - 20, 20, juce::Justification::centredLeft, true);
            g.setColour (colour::textMicro);
            g.setFont (type::micro (12.5f));
            g.drawText (m.lastPlace.isEmpty() ? juce::String ("never seen outside the store") : "last seen at " + m.lastPlace, 10, 20, width - 20, 18, juce::Justification::centredLeft, true);
        }
        juce::String getNameForRow (int row) override { return row >= 0 && row < getNumRows() ? items[static_cast<std::size_t> (row)].name : juce::String(); }
        void relinkSelected()
        {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= getNumRows())
                return;
            const auto m = items[static_cast<std::size_t> (row)];
            chooser = std::make_unique<juce::FileChooser> ("Locate " + m.name, juce::File (m.lastPlace).getParentDirectory(), "*.wav;*.aif;*.aiff;*.flac");
            juce::Component::SafePointer<MissingSheet> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, m] (const juce::FileChooser& fc) {
                if (safe == nullptr || fc.getResult() == juce::File())
                    return;
                const auto path = fc.getResult().getFullPathName().toStdString();
                // Checked by its bytes (never by its name), on the Library thread.
                safe->processor.libraryService().request<bool> (
                    [m, path] (library::Catalog& c) {
                        const auto hex = io::sha256OfFile (path);
                        return hex && "sha256:" + *hex == m.hash && c.addLocation (m.hash, path, "original");
                    },
                    [safe, m] (bool ok) {
                        if (safe == nullptr)
                            return;
                        safe->panel.setStatus (ok ? m.name + " found again" : "That file is not " + m.name + " (its contents differ). Nothing was changed.");
                        safe->scan();
                    });
            });
        }
        void trashSelected()
        {
            const int row = list.getSelectedRow();
            if (row < 0 || row >= getNumRows())
                return;
            const auto m = items[static_cast<std::size_t> (row)];
            juce::Component::SafePointer<MissingSheet> safe (this);
            processor.libraryService().request<bool> ([id = m.id] (library::Catalog& c) { return c.trash (id); }, [safe] (bool) {
                if (safe != nullptr)
                {
                    safe->scan();
                    safe->panel.refresh();
                }
            });
        }
        OspAudioProcessor& processor;
        juce::ListBox list;
        std::vector<Missing> items;
        bool scanned = false;
        juce::TextButton locate { juce::String::fromUTF8 ("Locate\xe2\x80\xa6") }, forget { "Move record to Trash" }, close { "Done" };
        std::unique_ptr<juce::FileChooser> chooser;
    };
}

void LibraryPanel::openSaveSheet()
{
    showSheet (std::make_unique<SaveSheet> (*this, processor, view == View::templates), "save");
}

void LibraryPanel::openSettingsSheet()
{
    showSheet (std::make_unique<SettingsSheet> (*this, processor), "settings");
}

void LibraryPanel::openMissingSheet()
{
    showSheet (std::make_unique<MissingSheet> (*this, processor), "missing");
}

//==============================================================================

void LibraryPanel::timerCallback()
{
    // The Library's answers and the preview's playheads at the display rate.
    processor.libraryService().deliver();
    processor.audition().deliver();
    if (view == View::sounds && processor.audition().isSounding())
    {
        inspector->repaint();
        tray->repaint();
    }
}

void LibraryPanel::paint (juce::Graphics& g)
{
    design::draw::raised (g, getLocalBounds().toFloat(), design::layout::cardRadius, colour::housingTop, colour::housingBottom, 1.6f);
    // The identity, as the instrument's header writes it.
    g.setColour (colour::text);
    g.setFont (fonts::make (30.0f, fonts::Weight::displayBold, 0.01f));
    g.drawText ("ANDOR/OSP", 24, 10, 220, 34, juce::Justification::centredLeft);
    g.setColour (colour::textMicro);
    g.setFont (type::popupTitle (12.0f));
    g.drawText ("LIBRARY", 26, 42, 200, 14, juce::Justification::centredLeft);
    g.setColour (colour::divider);
    g.fillRect (gutter, headerHeight - 1, getWidth() - 2 * gutter, 1);
    g.fillRect (sidebarWidth + gutter + 4, headerHeight + 8, 1, footerArea.getY() - headerHeight - 16);
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (14.0f));
    g.drawText (status, footerArea.withTrimmedRight (backButton.isVisible() ? backButton.getWidth() + 16 : 0), juce::Justification::centredLeft, true);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (headerHeight);
    footerArea = r.removeFromBottom (footerHeight).reduced (gutter + 4, 4);
    {
        auto h = headerArea.reduced (gutter, 14);
        closeButton.setBounds (h.removeFromRight (36));
        h.removeFromRight (8);
        menuButton.setBounds (h.removeFromRight (36));
        h.removeFromRight (12);
        saveButton.setBounds (h.removeFromRight (110));
        const int tabWidth = 140;
        const int x = (getWidth() - 3 * tabWidth) / 2;
        for (std::size_t i = 0; i < tabs.size(); ++i)
            tabs[i].setBounds (x + static_cast<int> (i) * tabWidth, h.getY(), tabWidth - 4, h.getHeight());
    }
    backButton.setBounds (footerArea.removeFromRight (210).withSizeKeepingCentre (210, 26));
    r.removeFromTop (8);
    auto left = r.removeFromLeft (sidebarWidth + gutter).withTrimmedLeft (gutter);
    sidebar->setBounds (left.withHeight (std::max (left.getHeight(), sidebar->contentHeight())).withHeight (left.getHeight()));
    r.removeFromLeft (gutter);
    r.removeFromRight (gutter);
    if (view == View::sounds)
    {
        tray->setBounds (r.removeFromBottom (trayHeight));
        r.removeFromBottom (10);
        inspector->setBounds (r.removeFromRight (inspectorWidth));
        r.removeFromRight (12);
    }
    else
    {
        detailStrip->setBounds (r.removeFromBottom (detailHeight));
        r.removeFromBottom (10);
    }
    auto bar = r.removeFromTop (36);
    sortOrder.setBounds (bar.removeFromRight (150));
    bar.removeFromRight (8);
    if (view == View::sounds)
    {
        lengthFilter.setBounds (bar.removeFromRight (150));
        bar.removeFromRight (8);
        originFilter.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (8);
    }
    search.setBounds (bar);
    r.removeFromTop (10);
    results->setBounds (r);
    if (sheetComponent != nullptr)
        sheetComponent->setBounds (getLocalBounds());
}

bool LibraryPanel::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (sheetComponent != nullptr)
            closeSheet();
        else if (onClose != nullptr)
            onClose();
        return true;
    }
    if (key == juce::KeyPress ('f', juce::ModifierKeys::commandModifier, 0))
    {
        search.grabKeyboardFocus();
        return true;
    }
    if (key == juce::KeyPress::spaceKey && view == View::sounds && ! search.hasKeyboardFocus (false))
    {
        previewSelected();
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        loadSelected();
        return true;
    }
    for (int i = 0; i < 3; ++i)
        if (key == juce::KeyPress ('1' + i, juce::ModifierKeys::commandModifier, 0))
        {
            setView (static_cast<View> (i));
            return true;
        }
    return false;
}

} // namespace osp::plugin
