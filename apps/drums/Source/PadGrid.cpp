#include "PadGrid.h"

#include "CeramicSkin.h"
#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace colour = look::colour;
    namespace type = look::type;

    bool isAudioFile (const juce::String& path) { return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;caf"); }
    const juce::String dragPrefix = "andor-drums-slot:";

    // Pad surface: OSP's dark waveform well, so the pads read as instruments, not buttons.
    const juce::Colour tileTop { 0xff262a2b };
    const juce::Colour tileBottom { 0xff1d2122 };
    constexpr float corner = 7.0f;
    constexpr float headerHeight = 30.0f;
    constexpr float footerHeight = 24.0f;
    constexpr float compactBelow = 112.0f;   // shorter cells (3 x 3) fold the footer into the header
} // namespace

// ---------------------------------------------------------------------------------------
// PadView
// ---------------------------------------------------------------------------------------

PadView::PadView (PadGrid& g, int slot) : grid (g), slotIndex (slot)
{
    setRepaintsOnMouseActivity (false);
}

int PadView::pad() const { return grid.processor().padAtSlot (slotIndex); }

juce::Rectangle<float> PadView::headerArea() const { return getLocalBounds().toFloat().reduced (5.0f).withTrimmedTop (4.0f).removeFromTop (headerHeight); }
juce::Rectangle<float> PadView::badgeArea() const { return headerArea().reduced (8.0f, 6.0f).removeFromLeft (20.0f); }
juce::Rectangle<float> PadView::muteArea() const
{
    auto h = headerArea().reduced (8.0f, 6.0f);
    return h.removeFromRight (20.0f);
}
juce::Rectangle<float> PadView::soloArea() const
{
    auto h = headerArea().reduced (8.0f, 6.0f);
    h.removeFromRight (24.0f);
    return h.removeFromRight (20.0f);
}
juce::Rectangle<float> PadView::footerArea() const
{
    const auto r = getLocalBounds().toFloat().reduced (5.0f);
    return r.getHeight() < compactBelow ? juce::Rectangle<float>() : r.withTop (r.getBottom() - footerHeight);
}
juce::Rectangle<float> PadView::menuArea() const
{
    if (footerArea().isEmpty())
    {
        auto h = headerArea().reduced (8.0f, 6.0f);
        h.removeFromRight (48.0f);
        return h.removeFromRight (20.0f);
    }
    return footerArea().reduced (8.0f, 4.0f).removeFromRight (18.0f);
}
juce::Rectangle<float> PadView::waveArea() const
{
    auto r = getLocalBounds().toFloat().reduced (5.0f);
    r.removeFromTop (headerHeight + 4.0f);
    if (! footerArea().isEmpty())
        r.removeFromBottom (footerHeight);
    return r.reduced (8.0f, 2.0f);
}

PadView::Hover PadView::hitTest (juce::Point<float> p) const
{
    if (badgeArea().expanded (3.0f).contains (p))
        return Hover::badge;
    if (soloArea().expanded (2.0f).contains (p))
        return Hover::solo;
    if (muteArea().expanded (2.0f).contains (p))
        return Hover::mute;
    if (menuArea().expanded (3.0f).contains (p))
        return Hover::menu;
    if (waveArea().contains (p))
        return Hover::wave;
    return Hover::other;
}

bool PadView::paramOn (const char* name) const
{
    return grid.processor().parameters().getRawParameterValue (DrumsProcessor::padParamId (pad(), name))->load() > 0.5f;
}

void PadView::toggleParam (const char* name)
{
    if (auto* p = grid.processor().parameters().getParameter (DrumsProcessor::padParamId (pad(), name)))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
    }
    grid.repaintAll();   // solo changes what every pad means
}

void PadView::refresh()
{
    auto& proc = grid.processor();
    const auto hits = proc.padHits (pad());
    const int voices = proc.padVoices (pad());
    bool dirty = false;
    if (hits != lastHits)
    {
        lastHits = hits;
        flash = 1.0f;
        dirty = true;
    }
    else if (flash > 0.0f)
    {
        flash = std::max (0.0f, flash - 0.1f);
        dirty = true;
    }
    if (voices != lastVoices)
    {
        lastVoices = voices;
        dirty = true;
    }
    if (dirty)
        repaint();
}

void PadView::paint (juce::Graphics& g)
{
    auto& proc = grid.processor();
    const int p = pad();
    const auto& info = proc.padInfo (p);
    const bool selected = proc.selectedPad == p;
    const bool muted = paramOn ("mute"), soloed = paramOn ("solo");
    bool anySolo = false;
    for (int q = 0; q < DrumsProcessor::numPads; ++q)
        anySolo = anySolo || proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (q, "solo"))->load() > 0.5f;
    const bool silenced = muted || (anySolo && ! soloed);
    const auto accent = look::category (info.category);
    const auto r = getLocalBounds().toFloat().reduced (1.0f);

    // Selected: an accent frame around the tile (inside the cell, so the tile never moves).
    const auto tile = r.reduced (4.0f);
    if (selected)
    {
        g.setColour (colour::accent.withAlpha (0.25f));
        g.fillRoundedRectangle (r.reduced (0.5f), corner + 3.0f);
        g.setColour (colour::accent);
        g.drawRoundedRectangle (r.reduced (1.5f), corner + 2.5f, 2.6f);
    }
    else
        ceramic::contactShadow (g, tile, corner, 1.1f);
    g.setGradientFill (juce::ColourGradient (tileTop, tile.getX(), tile.getY(), tileBottom, tile.getX(), tile.getBottom(), false));
    g.fillRoundedRectangle (tile, corner);

    // The category strip along the top; brighter while the pad plays.
    {
        juce::Path strip;
        strip.addRoundedRectangle (tile.getX(), tile.getY(), tile.getWidth(), 5.0f, corner, corner, true, true, false, false);
        g.setColour (accent.withAlpha (silenced ? 0.35f : 0.75f + 0.25f * flash));
        g.fillPath (strip);
    }
    if (flash > 0.0f || lastVoices > 0)
    {
        g.setColour (accent.withAlpha (0.05f + 0.13f * flash));
        g.fillRoundedRectangle (tile, corner);
    }

    // Header: badge (handle), name, [note in compact cells], S, M, [menu in compact cells].
    look::badge (g, badgeArea(), p + 1, selected ? colour::accent : juce::Colour (0xffe9e3d8), selected ? juce::Colours::white : colour::text);
    if (hover == Hover::badge)
    {
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawRoundedRectangle (badgeArea().expanded (1.5f), 3.5f, 1.0f);
    }
    auto toggle = [&] (juce::Rectangle<float> a, const char* text, bool on, juce::Colour onColour, bool hot) {
        g.setColour (on ? onColour : juce::Colour (0xff3a3f40).withAlpha (hot ? 1.0f : 0.8f));
        g.fillRoundedRectangle (a, 3.0f);
        g.setColour (on ? juce::Colours::white : juce::Colour (0xffb8b2a8));
        g.setFont (plugin::fonts::make (11.0f, look::Weight::semibold));
        g.drawText (text, a, juce::Justification::centred);
    };
    toggle (soloArea(), "S", soloed, colour::accent, hover == Hover::solo);
    toggle (muteArea(), "M", muted, juce::Colour (0xff8a847b), hover == Hover::mute);

    auto header = headerArea().reduced (8.0f, 6.0f);
    header.removeFromLeft (28.0f);
    header.removeFromRight (48.0f);
    const bool compact = footerArea().isEmpty();
    if (compact)
        header.removeFromRight (24.0f);
    const auto noteLabel = grid.processor().keyboardMode() == drums::KeyboardMode::pad
                               ? look::noteName (proc.keyMap().padNote[static_cast<std::size_t> (p)])
                               : (proc.keyMap().zone[static_cast<std::size_t> (p)].enabled()
                                      ? look::noteName (proc.keyMap().zone[static_cast<std::size_t> (p)].low) + "-"
                                            + look::noteName (proc.keyMap().zone[static_cast<std::size_t> (p)].high)
                                      : juce::String ("no zone"));
    if (compact)
    {
        g.setColour (juce::Colour (0xffa8a297));
        g.setFont (type::micro (12.0f));
        g.drawFittedText (noteLabel, header.removeFromRight (64.0f).toNearestInt(), juce::Justification::centredRight, 1, 1.0f);
    }
    g.setColour (silenced ? juce::Colour (0xff8a847b) : juce::Colour (0xfff2ece2));
    g.setFont (plugin::fonts::make (13.0f, look::Weight::semibold, 0.06f));
    g.drawFittedText (proc.padDisplayName (p).toUpperCase(), header.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);

    // Waveform (the striking surface) or the pad's state.
    const auto wave = waveArea();
    if (info.sound != nullptr && ! info.sound->peakMax.empty())
    {
        const float start = static_cast<float> (proc.parameters().getRawParameterValue (DrumsProcessor::padParamId (p, "start"))->load());
        look::waveform (g, wave.reduced (2.0f, 4.0f), info.sound->peakMin, info.sound->peakMax,
                        accent.interpolatedWith (juce::Colours::white, 0.12f).withAlpha (silenced ? 0.35f : 0.95f), start);
    }
    else
    {
        juce::String state = "Drop a sample";
        if (info.loading)
            state = "Loading...";
        else if (! info.missingFilename.empty())
            state = "Missing: " + juce::String::fromUTF8 (info.missingFilename.c_str());
        else if (! info.error.empty())
            state = "Could not load";
        g.setColour (info.missingFilename.empty() && info.error.empty() ? juce::Colour (0xff7d7a73) : juce::Colour (0xffd98a6a));
        g.setFont (type::micro (12.5f));
        g.drawFittedText (state, wave.toNearestInt().reduced (4, 0), juce::Justification::centred, 2, 1.0f);
        if (info.sound == nullptr && ! info.loading)
        {
            g.setColour (juce::Colour (0xff4a4f50));
            const float dashes[] = { 4.0f, 4.0f };
            juce::Path outline;
            outline.addRoundedRectangle (wave.reduced (2.0f), 5.0f);
            juce::PathStrokeType (1.0f).createDashedStroke (outline, outline, dashes, 2);
            g.fillPath (outline);
        }
    }
    if (hover == Hover::wave && info.sound != nullptr)
    {
        // Strike hint: a small play glyph (the waveform plays the pad).
        juce::Path play;
        const auto c = wave.getTopRight().translated (-12.0f, 10.0f);
        play.addTriangle (c.x - 4.0f, c.y - 5.0f, c.x - 4.0f, c.y + 5.0f, c.x + 5.0f, c.y);
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.fillPath (play);
    }

    // Footer: note (or range) · layers, and the menu.
    if (! compact)
    {
        auto f = footerArea().reduced (10.0f, 3.0f);
        juce::String text = noteLabel;
        if (info.sound != nullptr)
        {
            const int layers = info.sound->set != nullptr ? static_cast<int> (info.sound->set->members.size()) : 1;
            if (layers > 1)
                text << "  \xc2\xb7  " << layers << " layers";
        }
        g.setColour (juce::Colour (0xffb8b2a8));
        g.setFont (type::micro (12.5f));
        g.drawFittedText (text, f.withTrimmedRight (24.0f).toNearestInt(), juce::Justification::centred, 1, 1.0f);
    }
    {
        const auto m = menuArea();
        g.setColour (juce::Colour (0xffb8b2a8).withAlpha (hover == Hover::menu ? 1.0f : 0.7f));
        for (int i = 0; i < 3; ++i)
            g.fillRect (juce::Rectangle<float> (m.getX() + 3.0f, m.getCentreY() - 4.0f + 4.0f * static_cast<float> (i), m.getWidth() - 6.0f, 1.3f));
    }

    if (fileOver || padOver)
    {
        g.setColour (colour::accent);
        const float dashes[] = { 6.0f, 4.0f };
        juce::Path outline;
        outline.addRoundedRectangle (tile.reduced (1.0f), corner);
        juce::PathStrokeType (2.0f).createDashedStroke (outline, outline, dashes, 2);
        g.fillPath (outline);
        g.setColour (colour::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (tile, corner);
        g.setColour (juce::Colours::white);
        g.setFont (type::micro (12.5f));
        g.drawText (padOver ? "Swap places" : (info.sound != nullptr ? "Replace sample" : "Load sample"), wave.toNearestInt(), juce::Justification::centredBottom);
    }
}

void PadView::resized()
{
    if (renameEditor != nullptr)
        renameEditor->setBounds (headerArea().reduced (34.0f, 4.0f).withTrimmedRight (40.0f).toNearestInt());
}

void PadView::mouseMove (const juce::MouseEvent& e)
{
    const auto h = hitTest (e.position);
    if (h != hover)
    {
        hover = h;
        setMouseCursor (h == Hover::badge ? juce::MouseCursor::DraggingHandCursor
                        : h == Hover::wave || h == Hover::solo || h == Hover::mute || h == Hover::menu ? juce::MouseCursor::PointingHandCursor
                                                                                                         : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void PadView::mouseExit (const juce::MouseEvent&)
{
    hover = Hover::none;
    repaint();
}

void PadView::mouseDown (const juce::MouseEvent& e)
{
    const int p = pad();
    if (e.mods.isPopupMenu())
    {
        if (grid.onSelect)
            grid.onSelect (p);
        showMenu();
        return;
    }
    switch (hitTest (e.position))
    {
        case Hover::solo: toggleParam ("solo"); return;
        case Hover::mute: toggleParam ("mute"); return;
        case Hover::menu:
            if (grid.onSelect)
                grid.onSelect (p);
            showMenu();
            return;
        case Hover::wave:
        {
            if (grid.onSelect)
                grid.onSelect (p);
            const auto w = waveArea();
            const float y = 1.0f - (e.position.y - w.getY()) / std::max (1.0f, w.getHeight());
            grid.processor().auditionPad (p, juce::jlimit (30, 127, static_cast<int> (45.0f + 82.0f * y)));
            striking = true;
            return;
        }
        case Hover::badge:
        case Hover::other:
        case Hover::none:
            if (grid.onSelect)
                grid.onSelect (p);
            return;
    }
}

void PadView::mouseDrag (const juce::MouseEvent& e)
{
    if (hitTest (e.mouseDownPosition) == Hover::badge && e.getDistanceFromDragStart() > 4 && ! grid.isDragAndDropActive())
    {
        juce::Image ghost (juce::Image::ARGB, getWidth(), getHeight(), true);
        {
            juce::Graphics g (ghost);
            g.setOpacity (0.75f);
            paintEntireComponent (g, false);
        }
        grid.startDragging (dragPrefix + juce::String (slotIndex), this, juce::ScaledImage (ghost), true);
    }
}

void PadView::mouseUp (const juce::MouseEvent&)
{
    if (striking)
    {
        grid.processor().releaseAuditionedPad (pad());
        striking = false;
    }
}

void PadView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (headerArea().contains (e.position) && hitTest (e.position) == Hover::other)
        startRename();
}

void PadView::startRename()
{
    renameEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *renameEditor;
    ed.setFont (plugin::fonts::make (13.0f, look::Weight::semibold));
    ed.setText (grid.processor().padDisplayName (pad()), false);
    ed.selectAll();
    ed.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xfff5eee4));
    ed.setColour (juce::TextEditor::textColourId, colour::text);
    ed.setColour (juce::TextEditor::outlineColourId, colour::accent);
    auto finish = [this] (bool commit) {
        if (renameEditor == nullptr)
            return;
        const auto text = renameEditor->getText();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<PadView> (this)] {
            if (safe != nullptr)
                safe->renameEditor.reset();
        });
        if (commit)
            grid.processor().setPadName (pad(), text);
    };
    ed.onReturnKey = [finish] { finish (true); };
    ed.onEscapeKey = [finish] { finish (false); };
    ed.onFocusLost = [finish] { finish (true); };
    addAndMakeVisible (ed);
    resized();
    ed.grabKeyboardFocus();
}

void PadView::showMenu()
{
    auto& proc = grid.processor();
    const int p = pad();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Pad " + juce::String (p + 1) + "  " + proc.padDisplayName (p));
    menu.addItem ("Load sample...", [this, p] {
        if (grid.onLoadRequest)
            grid.onLoadRequest (p);
    });
    menu.addItem ("Rename", [safe = juce::Component::SafePointer<PadView> (this)] {
        if (safe != nullptr)
            safe->startRename();
    });
    juce::PopupMenu categories;
    for (int c = 0; c < 10; ++c)
    {
        const auto cat = static_cast<drums::DrumCategory> (c);
        categories.addItem (look::categoryName (cat), true, proc.padInfo (p).category == cat, [&proc, p, cat] { proc.setPadCategory (p, cat, true); });
    }
    menu.addSubMenu ("Category", categories);
    juce::PopupMenu chokes;
    for (int c = 0; c <= 8; ++c)
        chokes.addItem (c == 0 ? juce::String ("No choke") : "Choke group " + juce::String (c), true, proc.padInfo (p).chokeGroup == c,
                        [&proc, p, c] { proc.setPadChoke (p, c); });
    menu.addSubMenu ("Choke", chokes);
    menu.addSeparator();
    menu.addItem ("Mute", true, paramOn ("mute"), [this] { toggleParam ("mute"); });
    menu.addItem ("Solo", true, paramOn ("solo"), [this] { toggleParam ("solo"); });
    juce::PopupMenu swaps;
    for (int s = 0; s < grid.visibleCells(); ++s)
        if (s != slotIndex)
        {
            const int other = proc.padAtSlot (s);
            swaps.addItem ("Pad " + juce::String (other + 1) + "  " + proc.padDisplayName (other), [&proc, a = slotIndex, s] { proc.swapPadSlots (a, s); });
        }
    menu.addSubMenu ("Swap places with", swaps);
    menu.addSeparator();
    menu.addItem ("Clear pad", proc.padInfo (p).sound != nullptr || ! proc.padInfo (p).missingFilename.empty(), false, [&proc, p] { proc.clearPad (p); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (menuArea().toNearestInt())));
}

bool PadView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFile (f))
            return true;
    return false;
}

void PadView::fileDragEnter (const juce::StringArray&, int, int)
{
    fileOver = true;
    repaint();
}

void PadView::fileDragExit (const juce::StringArray&)
{
    fileOver = false;
    repaint();
}

void PadView::filesDropped (const juce::StringArray& files, int, int)
{
    fileOver = false;
    // One file per pad until layering (round robin / velocity / stack) arrives: the first audio file.
    for (const auto& f : files)
        if (isAudioFile (f))
        {
            grid.processor().loadPad (pad(), juce::File (f));
            if (grid.onSelect)
                grid.onSelect (pad());
            break;
        }
    repaint();
}

bool PadView::isInterestedInDragSource (const SourceDetails& d)
{
    return d.description.toString().startsWith (dragPrefix);
}

void PadView::itemDragEnter (const SourceDetails& d)
{
    padOver = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue() != slotIndex;
    repaint();
}

void PadView::itemDragExit (const SourceDetails&)
{
    padOver = false;
    repaint();
}

void PadView::itemDropped (const SourceDetails& d)
{
    padOver = false;
    const int from = d.description.toString().fromFirstOccurrenceOf (dragPrefix, false, false).getIntValue();
    grid.processor().swapPadSlots (from, slotIndex);   // undoable; sounds and MIDI stay with their pads
    grid.repaintAll();
}

// ---------------------------------------------------------------------------------------
// PadGrid
// ---------------------------------------------------------------------------------------

PadGrid::PadGrid (DrumsProcessor& p) : proc (p)
{
    for (int s = 0; s < DrumsProcessor::numPads; ++s)
    {
        cells[static_cast<std::size_t> (s)] = std::make_unique<PadView> (*this, s);
        addAndMakeVisible (*cells[static_cast<std::size_t> (s)]);
    }
}

void PadGrid::resized()
{
    // Equal cells: the grid divides its area exactly; leftover pixels go to the margins,
    // never to one cell.
    using L = DrumsProcessor::PadLayout;
    const int columns = proc.padLayout == L::row8 ? 8 : proc.padLayout == L::grid8 ? 4 : 3;
    const int rows = proc.padLayout == L::row8 ? 1 : proc.padLayout == L::grid8 ? 2 : 3;
    const auto area = getLocalBounds().toFloat();
    const float cw = std::floor ((area.getWidth() - gap * static_cast<float> (columns - 1)) / static_cast<float> (columns));
    const float ch = std::floor ((area.getHeight() - gap * static_cast<float> (rows - 1)) / static_cast<float> (rows));
    const float usedW = cw * static_cast<float> (columns) + gap * static_cast<float> (columns - 1);
    const float x0 = std::floor ((area.getWidth() - usedW) * 0.5f);
    for (int s = 0; s < DrumsProcessor::numPads; ++s)
    {
        auto& c = *cells[static_cast<std::size_t> (s)];
        const bool shown = s < columns * rows;
        c.setVisible (shown);
        if (shown)
            c.setBounds (juce::Rectangle<float> (x0 + static_cast<float> (s % columns) * (cw + gap), static_cast<float> (s / columns) * (ch + gap), cw, ch)
                             .toNearestInt());
    }
}

void PadGrid::refresh()
{
    for (auto& c : cells)
        if (c->isVisible())
            c->refresh();
}

void PadGrid::repaintAll()
{
    for (auto& c : cells)
        c->repaint();
}

} // namespace osp::drumsplugin
