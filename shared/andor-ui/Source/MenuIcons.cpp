#include "MenuIcons.h"

#include "OspLookAndFeel.h"

#include <cmath>

namespace osp::plugin::menuicons
{

namespace
{
    /** Builds in a 16 x 16 design square, then maps to `area`. */
    struct Pen
    {
        juce::Graphics& g;
        juce::Rectangle<float> area;
        juce::Colour colour;
        float stroke;

        juce::Point<float> p (float x, float y) const
        {
            const float s = area.getWidth() / 16.0f;
            return { area.getX() + x * s, area.getY() + y * s };
        }
        float len (float v) const { return v * area.getWidth() / 16.0f; }
        void line (float x0, float y0, float x1, float y1) const
        {
            g.setColour (colour);
            g.drawLine ({ p (x0, y0), p (x1, y1) }, stroke);
        }
        void path (const juce::Path& path16) const
        {
            g.setColour (colour);
            const float s = area.getWidth() / 16.0f;
            g.strokePath (path16, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                          juce::AffineTransform::scale (s).translated (area.getX(), area.getY()));
        }
        void fill (const juce::Path& path16) const
        {
            g.setColour (colour);
            const float s = area.getWidth() / 16.0f;
            g.fillPath (path16, juce::AffineTransform::scale (s).translated (area.getX(), area.getY()));
        }
        void rounded (float x, float y, float w, float h, float r) const
        {
            juce::Path q;
            q.addRoundedRectangle (x, y, w, h, r);
            path (q);
        }
        void circle (float cx, float cy, float r) const
        {
            juce::Path q;
            q.addEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
            path (q);
        }
        void dot (float cx, float cy, float r) const
        {
            juce::Path q;
            q.addEllipse (cx - r, cy - r, 2.0f * r, 2.0f * r);
            fill (q);
        }
        /** An arc (degrees, 0 = up, clockwise) with an optional arrow head at its end. */
        void arc (float cx, float cy, float r, float from, float to, bool head) const
        {
            juce::Path q;
            q.addCentredArc (cx, cy, r, r, 0.0f, juce::degreesToRadians (from), juce::degreesToRadians (to), true);
            path (q);
            if (! head)
                return;
            const float a = juce::degreesToRadians (to);
            const float ex = cx + r * std::sin (a), ey = cy - r * std::cos (a);
            const float dir = to > from ? 1.0f : -1.0f;   // tangent direction
            const float tx = std::cos (a) * dir, ty = std::sin (a) * dir;
            const float nx = std::sin (a), ny = -std::cos (a);
            juce::Path h;
            h.startNewSubPath (ex - 2.6f * tx + 1.9f * nx, ey - 2.6f * ty + 1.9f * ny);
            h.lineTo (ex, ey);
            h.lineTo (ex - 2.6f * tx - 1.9f * nx, ey - 2.6f * ty - 1.9f * ny);
            path (h);
        }
    };

    void document (const Pen& pen)
    {
        juce::Path d;
        d.startNewSubPath (4.0f, 2.0f);
        d.lineTo (9.5f, 2.0f);
        d.lineTo (12.5f, 5.0f);
        d.lineTo (12.5f, 14.0f);
        d.lineTo (4.0f, 14.0f);
        d.closeSubPath();
        d.startNewSubPath (9.5f, 2.0f);
        d.lineTo (9.5f, 5.0f);
        d.lineTo (12.5f, 5.0f);
        pen.path (d);
    }
}

juce::Colour destructiveInk()
{
    return juce::Colour (0xffa4553a);
}

void draw (juce::Graphics& g, Kind kind, juce::Rectangle<float> area, juce::Colour colour)
{
    const Pen pen { g, area, colour, std::max (1.0f, 1.25f * area.getWidth() / 16.0f) };
    switch (kind)
    {
        case Kind::blank: return;
        case Kind::newDocument:
            document (pen);
            pen.line (8.25f, 7.0f, 8.25f, 11.5f);
            pen.line (6.0f, 9.25f, 10.5f, 9.25f);
            return;
        case Kind::save:
        {
            pen.rounded (2.5f, 2.5f, 11.0f, 11.0f, 1.6f);
            pen.rounded (5.0f, 2.5f, 6.0f, 3.6f, 0.6f);
            pen.rounded (4.6f, 8.6f, 6.8f, 4.9f, 0.8f);
            return;
        }
        case Kind::saveAs:
            pen.rounded (5.0f, 5.0f, 8.5f, 9.0f, 1.4f);
            {
                juce::Path back;
                back.startNewSubPath (11.0f, 3.2f);
                back.lineTo (11.0f, 2.8f);
                back.quadraticTo (11.0f, 1.8f, 10.0f, 1.8f);
                back.lineTo (3.5f, 1.8f);
                back.quadraticTo (2.5f, 1.8f, 2.5f, 2.8f);
                back.lineTo (2.5f, 10.5f);
                back.quadraticTo (2.5f, 11.5f, 3.3f, 11.5f);
                pen.path (back);
            }
            return;
        case Kind::open:
        {
            juce::Path f;
            f.startNewSubPath (2.0f, 12.5f);
            f.lineTo (2.0f, 3.5f);
            f.lineTo (6.0f, 3.5f);
            f.lineTo (7.4f, 5.0f);
            f.lineTo (12.5f, 5.0f);
            f.lineTo (12.5f, 7.0f);
            pen.path (f);
            juce::Path front;
            front.startNewSubPath (2.0f, 12.5f);
            front.lineTo (4.2f, 7.3f);
            front.lineTo (14.5f, 7.3f);
            front.lineTo (12.3f, 12.5f);
            front.closeSubPath();
            pen.path (front);
            return;
        }
        case Kind::library:
            // Three spines and a leaning one (the Library key's language: a shelf).
            pen.rounded (2.2f, 3.0f, 2.6f, 10.5f, 0.6f);
            pen.rounded (5.6f, 4.5f, 2.6f, 9.0f, 0.6f);
            {
                juce::Path lean;
                lean.startNewSubPath (9.3f, 5.2f);
                lean.lineTo (11.6f, 4.4f);
                lean.lineTo (14.4f, 12.7f);
                lean.lineTo (12.1f, 13.5f);
                lean.closeSubPath();
                pen.path (lean);
            }
            pen.line (1.6f, 14.2f, 14.6f, 14.2f);
            return;
        case Kind::record:
            pen.rounded (6.0f, 1.8f, 4.0f, 7.6f, 2.0f);
            {
                juce::Path cup;
                cup.startNewSubPath (3.8f, 7.4f);
                cup.quadraticTo (3.8f, 11.4f, 8.0f, 11.4f);
                cup.quadraticTo (12.2f, 11.4f, 12.2f, 7.4f);
                pen.path (cup);
            }
            pen.line (8.0f, 11.4f, 8.0f, 14.2f);
            pen.line (5.8f, 14.2f, 10.2f, 14.2f);
            return;
        case Kind::add:
            pen.line (8.0f, 3.0f, 8.0f, 13.0f);
            pen.line (3.0f, 8.0f, 13.0f, 8.0f);
            return;
        case Kind::mapNotes:
        {
            // A wave becoming a note: the analysis' symbol.
            juce::Path w;
            w.startNewSubPath (1.5f, 9.0f);
            w.quadraticTo (2.8f, 4.5f, 4.1f, 9.0f);
            w.quadraticTo (5.4f, 13.5f, 6.7f, 9.0f);
            pen.path (w);
            pen.line (11.8f, 2.6f, 11.8f, 11.0f);
            pen.line (11.8f, 2.6f, 14.4f, 4.0f);
            {
                juce::Path head;
                head.addEllipse (8.6f, 9.6f, 3.4f, 2.8f);
                pen.fill (head.createPathWithRoundedCorners (0.0f));
            }
            return;
        }
        case Kind::startingState:
            // A template: stacked sheets with a corner fold.
            pen.rounded (4.2f, 4.2f, 9.5f, 9.5f, 1.4f);
            pen.line (2.3f, 11.0f, 2.3f, 3.6f);
            pen.line (2.3f, 3.6f, 2.8f, 2.6f);
            pen.line (3.4f, 2.3f, 11.0f, 2.3f);
            pen.line (6.8f, 8.2f, 11.2f, 8.2f);
            pen.line (6.8f, 10.8f, 9.8f, 10.8f);
            return;
        case Kind::package:
        {
            juce::Path box;
            box.startNewSubPath (8.0f, 1.8f);
            box.lineTo (14.0f, 4.8f);
            box.lineTo (14.0f, 11.2f);
            box.lineTo (8.0f, 14.2f);
            box.lineTo (2.0f, 11.2f);
            box.lineTo (2.0f, 4.8f);
            box.closeSubPath();
            box.startNewSubPath (2.0f, 4.8f);
            box.lineTo (8.0f, 7.8f);
            box.lineTo (14.0f, 4.8f);
            box.startNewSubPath (8.0f, 7.8f);
            box.lineTo (8.0f, 14.2f);
            pen.path (box);
            pen.line (5.0f, 3.3f, 11.0f, 6.3f);
            return;
        }
        case Kind::favourite:
        {
            juce::Path h;
            h.startNewSubPath (8.0f, 13.6f);
            h.cubicTo (1.0f, 8.6f, 1.2f, 3.0f, 4.9f, 2.9f);
            h.cubicTo (6.6f, 2.9f, 7.6f, 4.0f, 8.0f, 5.2f);
            h.cubicTo (8.4f, 4.0f, 9.4f, 2.9f, 11.1f, 2.9f);
            h.cubicTo (14.8f, 3.0f, 15.0f, 8.6f, 8.0f, 13.6f);
            h.closeSubPath();
            pen.path (h);
            return;
        }
        case Kind::recent:
            pen.circle (8.0f, 8.0f, 6.0f);
            pen.line (8.0f, 4.6f, 8.0f, 8.0f);
            pen.line (8.0f, 8.0f, 10.6f, 9.6f);
            return;
        case Kind::settings:
            // Three sliders.
            for (int i = 0; i < 3; ++i)
            {
                const float y = 3.5f + 4.5f * static_cast<float> (i);
                pen.line (2.0f, y, 14.0f, y);
            }
            pen.dot (5.0f, 3.5f, 1.7f);
            pen.dot (10.5f, 8.0f, 1.7f);
            pen.dot (7.0f, 12.5f, 1.7f);
            return;
        case Kind::tutorials:
        {
            // An open book.
            juce::Path b;
            b.startNewSubPath (8.0f, 4.2f);
            b.quadraticTo (5.0f, 2.4f, 1.8f, 3.4f);
            b.lineTo (1.8f, 12.6f);
            b.quadraticTo (5.0f, 11.6f, 8.0f, 13.4f);
            b.quadraticTo (11.0f, 11.6f, 14.2f, 12.6f);
            b.lineTo (14.2f, 3.4f);
            b.quadraticTo (11.0f, 2.4f, 8.0f, 4.2f);
            b.lineTo (8.0f, 13.4f);
            pen.path (b);
            return;
        }
        case Kind::remove:
            pen.line (2.5f, 4.2f, 13.5f, 4.2f);
            pen.line (6.2f, 4.2f, 6.6f, 2.2f);
            pen.line (6.6f, 2.2f, 9.4f, 2.2f);
            pen.line (9.4f, 2.2f, 9.8f, 4.2f);
            {
                juce::Path bin;
                bin.startNewSubPath (3.8f, 4.2f);
                bin.lineTo (4.6f, 13.8f);
                bin.lineTo (11.4f, 13.8f);
                bin.lineTo (12.2f, 4.2f);
                pen.path (bin);
            }
            pen.line (6.6f, 6.8f, 6.8f, 11.4f);
            pen.line (9.4f, 6.8f, 9.2f, 11.4f);
            return;
        case Kind::previous:
            pen.arc (8.5f, 8.5f, 5.2f, 300.0f, 40.0f, false);
            pen.line (3.5f, 5.5f, 3.6f, 9.2f);
            pen.line (3.6f, 9.2f, 7.2f, 8.4f);
            {
                juce::Path back;
                back.addCentredArc (8.5f, 8.5f, 5.2f, 5.2f, 0.0f, juce::degreesToRadians (40.0f), juce::degreesToRadians (250.0f), true);
                pen.path (back);
            }
            return;
        case Kind::init:
            // An empty patch: a circle with a centre point.
            pen.circle (8.0f, 8.0f, 5.6f);
            pen.dot (8.0f, 8.0f, 1.3f);
            return;
        case Kind::reset:
            pen.arc (8.0f, 8.5f, 5.4f, 40.0f, 330.0f, false);
            pen.line (3.2f, 3.4f, 4.2f, 6.6f);
            pen.line (4.2f, 6.6f, 7.4f, 5.6f);
            return;
        case Kind::list:
            for (int i = 0; i < 3; ++i)
            {
                const float y = 4.0f + 4.0f * static_cast<float> (i);
                pen.dot (2.8f, y, 1.0f);
                pen.line (5.6f, y, 13.8f, y);
            }
            return;
        case Kind::exportOut:
            pen.line (8.0f, 10.0f, 8.0f, 2.2f);
            pen.line (5.2f, 5.0f, 8.0f, 2.2f);
            pen.line (8.0f, 2.2f, 10.8f, 5.0f);
            {
                juce::Path tray;
                tray.startNewSubPath (2.5f, 9.0f);
                tray.lineTo (2.5f, 13.5f);
                tray.lineTo (13.5f, 13.5f);
                tray.lineTo (13.5f, 9.0f);
                pen.path (tray);
            }
            return;
        case Kind::importIn:
            pen.line (8.0f, 2.2f, 8.0f, 10.0f);
            pen.line (5.2f, 7.2f, 8.0f, 10.0f);
            pen.line (8.0f, 10.0f, 10.8f, 7.2f);
            {
                juce::Path tray;
                tray.startNewSubPath (2.5f, 9.0f);
                tray.lineTo (2.5f, 13.5f);
                tray.lineTo (13.5f, 13.5f);
                tray.lineTo (13.5f, 9.0f);
                pen.path (tray);
            }
            return;
        case Kind::info:
            pen.circle (8.0f, 8.0f, 6.0f);
            pen.line (8.0f, 7.2f, 8.0f, 11.4f);
            pen.dot (8.0f, 4.9f, 0.9f);
            return;
        case Kind::rename:
        {
            juce::Path pencil;
            pencil.startNewSubPath (3.0f, 13.0f);
            pencil.lineTo (3.6f, 10.2f);
            pencil.lineTo (10.6f, 3.2f);
            pencil.lineTo (12.8f, 5.4f);
            pencil.lineTo (5.8f, 12.4f);
            pencil.closeSubPath();
            pen.path (pencil);
            pen.line (9.2f, 4.6f, 11.4f, 6.8f);
            return;
        }
        case Kind::replace:
            pen.arc (8.0f, 8.0f, 5.2f, 20.0f, 160.0f, true);
            pen.arc (8.0f, 8.0f, 5.2f, 200.0f, 340.0f, true);
            return;
        case Kind::undo:
        case Kind::redo:
        {
            // A hooked arrow, back (undo) or forward (redo).
            const bool back = kind == Kind::undo;
            auto x = [back] (float v) { return back ? v : 16.0f - v; };
            juce::Path hook;
            hook.startNewSubPath (x (12.6f), 12.6f);
            hook.cubicTo (x (12.6f), 8.6f, x (10.4f), 6.4f, x (7.0f), 6.4f);
            hook.lineTo (x (3.4f), 6.4f);
            hook.startNewSubPath (x (6.0f), 3.8f);
            hook.lineTo (x (3.4f), 6.4f);
            hook.lineTo (x (6.0f), 9.0f);
            pen.path (hook);
            return;
        }
        case Kind::reveal:
            // A folder with a look-out arrow.
            {
                juce::Path f;
                f.startNewSubPath (2.5f, 12.5f);
                f.lineTo (2.5f, 4.0f);
                f.lineTo (6.2f, 4.0f);
                f.lineTo (7.6f, 5.5f);
                f.lineTo (10.0f, 5.5f);
                f.startNewSubPath (13.5f, 9.0f);
                f.lineTo (13.5f, 12.5f);
                f.lineTo (2.5f, 12.5f);
                pen.path (f);
            }
            pen.line (9.0f, 9.2f, 13.6f, 4.6f);
            pen.line (10.6f, 4.4f, 13.8f, 4.4f);
            pen.line (13.8f, 4.4f, 13.8f, 7.6f);
            return;
        case Kind::waveform:
            // A short recording: a few bars of a waveform.
            pen.line (2.8f, 7.0f, 2.8f, 9.0f);
            pen.line (5.4f, 4.6f, 5.4f, 11.4f);
            pen.line (8.0f, 2.8f, 8.0f, 13.2f);
            pen.line (10.6f, 5.6f, 10.6f, 10.4f);
            pen.line (13.2f, 7.0f, 13.2f, 9.0f);
            return;
        case Kind::play:
        {
            juce::Path t;
            t.startNewSubPath (5.0f, 3.4f);
            t.lineTo (12.6f, 8.0f);
            t.lineTo (5.0f, 12.6f);
            t.closeSubPath();
            pen.path (t);
            return;
        }
        case Kind::trim:
            // A region between two markers.
            pen.line (3.5f, 2.8f, 3.5f, 13.2f);
            pen.line (12.5f, 2.8f, 12.5f, 13.2f);
            pen.line (3.5f, 2.8f, 5.6f, 2.8f);
            pen.line (12.5f, 13.2f, 10.4f, 13.2f);
            pen.line (6.4f, 8.0f, 9.6f, 8.0f);
            return;
    }
}

void addItem (juce::PopupMenu& menu, Kind kind, const juce::String& text, bool enabled, std::function<void()> action, bool destructive, const juce::String& shortcut)
{
    juce::PopupMenu::Item item (text);
    item.setEnabled (enabled);
    item.setAction (std::move (action));
    item.setImage (make (kind));
    if (destructive)
        item.setColour (destructiveInk());
    if (shortcut.isNotEmpty())
        item.shortcutKeyDescription = shortcut;
    menu.addItem (std::move (item));
}

juce::PopupMenu::Item item (Kind kind, const juce::String& text, std::function<void()> action, bool enabled, bool ticked)
{
    juce::PopupMenu::Item i (text);
    i.setEnabled (enabled).setTicked (ticked).setImage (make (kind)).setAction (std::move (action));
    return i;
}

void addSubMenu (juce::PopupMenu& menu, Kind kind, const juce::String& text, juce::PopupMenu sub, bool enabled)
{
    menu.addSubMenu (text, std::move (sub), enabled, make (kind));
}

namespace
{
    class SourceHeader final : public juce::PopupMenu::CustomComponent
    {
    public:
        SourceHeader (juce::String l, juce::Colour c, juce::String n, juce::String d)
            : juce::PopupMenu::CustomComponent (false), letter (std::move (l)), chip (c), name (std::move (n)), detail (std::move (d))
        {
            setInterceptsMouseClicks (false, false);
        }

        /** The widest the name and the file may make the menu (a long or machine-made file
            name - a hash - is shortened in the middle instead; the card is as wide as for a
            sound called "Atmos - Synth String Bed 01 - B"). */
        static constexpr float maxTextWidth = 180.0f;

        /** `text` within `width` in `font`: as it is if it fits, else its start and its end
            (the extension kept) around an ellipsis. */
        static juce::String elideMiddle (const juce::Font& font, const juce::String& text, float width)
        {
            if (juce::GlyphArrangement::getStringWidth (font, text) <= width)
                return text;
            const auto ellipsis = juce::String::fromUTF8 ("\xe2\x80\xa6");
            const int dot = text.lastIndexOfChar ('.');
            const int tailKeep = dot > 0 && text.length() - dot <= 6 ? text.length() - dot + 6 : 7;   // ("13000.wav")
            for (int keep = text.length() - 1; keep > 2; --keep)
            {
                const int tail = std::min (tailKeep, keep / 2);
                const int head = keep - tail;
                const auto candidate = text.substring (0, head).trimEnd() + ellipsis + text.substring (text.length() - tail).trimStart();
                if (juce::GlyphArrangement::getStringWidth (font, candidate) <= width)
                    return candidate;
            }
            return text.substring (0, 1) + ellipsis;
        }

        void getIdealSize (int& idealWidth, int& idealHeight) override
        {
            const auto nameWidth = juce::GlyphArrangement::getStringWidth (nameFont(), elideMiddle (nameFont(), name, maxTextWidth));
            const auto detailWidth = juce::GlyphArrangement::getStringWidth (detailFont(), elideMiddle (detailFont(), detail, maxTextWidth));
            idealWidth = juce::roundToInt (std::max (nameWidth, detailWidth)) + 44 + 18;   // (JUCE adds the borders)
            idealHeight = 46;
        }

        void paint (juce::Graphics& g) override
        {
            const auto inset = static_cast<float> (OspLookAndFeel::popupSideInset (*this));
            g.addTransform (juce::AffineTransform::translation (inset, 0.0f));
            const auto w = static_cast<float> (getWidth()) - 2.0f * inset;
            const auto h = static_cast<float> (getHeight());
            // The chip: the source's letter, as on its card, in the source's colour.
            const auto box = juce::Rectangle<float> (10.0f, (h - 24.0f) * 0.5f - 1.0f, 24.0f, 24.0f);
            g.setColour (chip);
            g.fillRoundedRectangle (box, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.95f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (letter, box, juce::Justification::centred, false);
            // The name over the file.
            auto text = juce::Rectangle<float> (w, h).withTrimmedLeft (44.0f).withTrimmedRight (12.0f);
            g.setColour (juce::Colour (0xff2f2c28));
            g.setFont (nameFont());
            g.drawText (elideMiddle (nameFont(), name, text.getWidth()), text.withTrimmedTop (h * 0.5f - 17.0f).withHeight (18.0f), juce::Justification::centredLeft, false);
            g.setColour (juce::Colour (0xff8a847a));
            g.setFont (detailFont());
            g.drawText (elideMiddle (detailFont(), detail, text.getWidth()), text.withTrimmedTop (h * 0.5f + 1.0f).withHeight (15.0f), juce::Justification::centredLeft, false);
            // A rule under the identity (the menu's groups follow).
            g.setColour (juce::Colour (0xffe6ddcf));
            g.fillRect (juce::Rectangle<float> (12.0f, h - 1.0f, w - 24.0f, 1.0f));
        }

    private:
        static juce::Font nameFont() { return juce::FontOptions (13.5f, juce::Font::bold); }
        static juce::Font detailFont() { return juce::FontOptions (11.5f); }
        juce::String letter;
        juce::Colour chip;
        juce::String name, detail;
    };
}

void decorate (juce::PopupMenu& menu, std::initializer_list<std::pair<const char*, Kind>> icons)
{
    const auto trail = juce::String::fromUTF8 ("\xe2\x80\xa6.");
    for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
    {
        auto& entry = it.getItem();
        if (entry.isSeparator || entry.isSectionHeader || entry.customComponent != nullptr || entry.image != nullptr)
            continue;
        const auto bare = entry.text.trimCharactersAtEnd (trail);
        auto kind = Kind::blank;
        for (const auto& [text, k] : icons)
        {
            const auto pattern = juce::String::fromUTF8 (text);
            if (pattern.endsWithChar ('*') ? bare.startsWith (pattern.dropLastCharacters (1)) : bare == pattern.trimCharactersAtEnd (trail))
            {
                kind = k;
                break;
            }
        }
        entry.image = make (kind);
        if (kind == Kind::remove && entry.colour == juce::Colour())
            entry.colour = destructiveInk();
    }
}

void addSourceHeader (juce::PopupMenu& menu, const juce::String& title, const juce::String& letter, juce::Colour chip, const juce::String& name,
                      const juce::String& detail)
{
    juce::PopupMenu::Item header;
    header.text = title;   // (what a screen reader says, and the menu's first line in tests)
    header.itemID = 0x7ffe0001;   // (not chosen: disabled, no action)
    header.isEnabled = false;
    header.customComponent = new SourceHeader (letter, chip, name, detail);
    menu.addItem (std::move (header));
}

} // namespace osp::plugin::menuicons
