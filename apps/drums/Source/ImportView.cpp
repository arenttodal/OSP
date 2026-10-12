#include "ImportView.h"

#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace cc = ceramic::colour;
    namespace type = look::type;

    constexpr float headerHeight = 52.0f;
    constexpr int columns = 4;
    constexpr float cardHeight = 136.0f, gap = 10.0f, rowHeight = 22.0f;

    juce::Colour confidenceColour (double c)
    {
        return c >= 0.75 ? juce::Colour (0xff6f9b5c) : c >= 0.55 ? juce::Colour (0xffd29b3a) : juce::Colour (0xffa8a29a);
    }
} // namespace

ImportView::ImportView (DrumsProcessor& p) : proc (p)
{
    importButton.onClick = [this] {
        const auto report = proc.applyImport();
        if (onReport)
            onReport ("Import samples", report);
        if (! proc.importSession().open() && onClose)
            onClose();
        refresh();
    };
    cancel.onClick = [this] {
        proc.closeImport();
        if (onClose)
            onClose();
    };
    reset.onClick = [this] {
        proc.resetImportSuggestions();
        selection.clear();
        refresh();
    };
    join.onClick = [this] { groupSelection(); };
    join.setTooltip ("Put the selected files on one pad, as round robins");
    reset.setTooltip ("Group and place every file as first suggested");
    for (auto* b : { &importButton, &cancel, &reset, &join })
        addAndMakeVisible (b);
}

void ImportView::refresh()
{
    const auto& s = proc.importSession();
    importButton.setEnabled (! s.loading() && ! s.groups.empty());
    reset.setEnabled (! s.loading());
    join.setEnabled (selection.size() >= 2);
    repaint();
}

void ImportView::resized()
{
    const int w = getWidth();
    importButton.setBounds (w - 146, 10, 130, 34);
    cancel.setBounds (w - 258, 10, 104, 34);
    reset.setBounds (w - 424, 10, 158, 34);
    join.setBounds (w - 568, 10, 136, 34);
}

juce::Rectangle<float> ImportView::cardRect (int group) const
{
    const float width = (static_cast<float> (getWidth()) - 32.0f - gap * (columns - 1)) / columns;
    const int row = group / columns - scrollRow;
    const int col = group % columns;
    return { 16.0f + static_cast<float> (col) * (width + gap), headerHeight + 8.0f + static_cast<float> (row) * (cardHeight + gap), width, cardHeight };
}

juce::Rectangle<float> ImportView::padLabelRect (int group) const { return cardRect (group).reduced (10.0f, 0.0f).withTrimmedTop (10.0f).withHeight (22.0f).withTrimmedRight (110.0f); }
juce::Rectangle<float> ImportView::categoryRect (int group) const
{
    const auto c = cardRect (group).reduced (10.0f, 0.0f).withTrimmedTop (10.0f).withHeight (22.0f);
    return c.withLeft (c.getRight() - 104.0f);
}

juce::Rectangle<float> ImportView::fileRect (int group, int row) const
{
    return cardRect (group).reduced (8.0f, 0.0f).withTrimmedTop (40.0f + static_cast<float> (row) * rowHeight).withHeight (rowHeight);
}

void ImportView::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    const auto& s = proc.importSession();
    g.setColour (cc::text);
    g.setFont (plugin::fonts::make (14.5f, look::Weight::semibold, 0.08f));
    g.drawText ("IMPORT SAMPLES", juce::Rectangle<int> (18, 0, 170, static_cast<int> (headerHeight)), juce::Justification::centredLeft);
    int failed = 0, duplicates = 0, tray = 0, placedFiles = 0;
    for (const auto& it : s.items)
    {
        failed += ! it.error.empty() ? 1 : 0;
        duplicates += it.duplicateOf >= 0 ? 1 : 0;
    }
    for (const auto& gr : s.groups)
    {
        tray += gr.pad < 0 ? 1 : 0;
        placedFiles += gr.pad >= 0 ? static_cast<int> (gr.items.size()) : 0;
    }
    juce::String summary;
    if (s.loading())
        summary << "Loading and listening... " << static_cast<int> (s.items.size()) - s.remaining << " of " << static_cast<int> (s.items.size());
    else
    {
        summary << static_cast<int> (s.items.size()) << " files   " << static_cast<int> (s.groups.size()) << " groups   " << placedFiles << " files onto pads";
        if (tray > 0)
            summary << "   " << tray << " in the tray";
        if (failed > 0)
            summary << "   " << failed << " could not load";
        if (duplicates > 0)
            summary << "   " << duplicates << " duplicate" << (duplicates == 1 ? "" : "s");
    }
    g.setColour (cc::textSecondary);
    g.setFont (type::micro (13.0f));
    g.drawFittedText (summary, juce::Rectangle<int> (190, 10, getWidth() - 790, 34), juce::Justification::centredLeft, 1, 1.0f);

    // Group cards
    const auto body = juce::Rectangle<float> (0.0f, headerHeight, static_cast<float> (getWidth()), static_cast<float> (getHeight()) - headerHeight - 44.0f);
    g.saveState();
    g.reduceClipRegion (body.toNearestInt());
    for (int gi = 0; gi < static_cast<int> (s.groups.size()); ++gi)
    {
        const auto r = cardRect (gi);
        if (r.getBottom() < body.getY() || r.getY() > body.getBottom())
            continue;
        const auto& group = s.groups[static_cast<std::size_t> (gi)];
        const auto colour = look::category (group.category);
        ceramic::contactShadow (g, r, 8.0f, 0.6f);
        g.setColour (group.pad < 0 ? juce::Colour (0xfff1ece4) : cc::cardTop);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (juce::Colour (0xffd8cfc2));
        g.drawRoundedRectangle (r, 8.0f, 1.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r.withWidth (5.0f).reduced (0.0f, 6.0f).translated (2.0f, 0.0f), 2.0f);
        // Destination (click: a pad, or the tray) and a conflict.
        const auto pl = padLabelRect (gi);
        g.setFont (plugin::fonts::make (13.5f, look::Weight::semibold, 0.04f));
        g.setColour (group.pad < 0 ? cc::textMicro : cc::text);
        juce::String where = group.pad < 0 ? juce::String ("TRAY (no pad)") : "PAD " + juce::String (group.pad + 1);
        const bool conflict = group.pad >= 0 && ! proc.padLayers (group.pad).layers.empty();
        g.drawText (where + juce::String (juce::CharPointer_UTF8 ("  \xe2\x96\xbe")), pl, juce::Justification::centredLeft);
        if (conflict)
        {
            g.setColour (juce::Colour (0xffc0633d));
            g.setFont (type::micro (11.5f));
            g.drawText ("replaces " + proc.padDisplayName (group.pad), pl.withTrimmedLeft (78.0f), juce::Justification::centredLeft);
        }
        // Category (click: change it)
        const auto cr = categoryRect (gi);
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (cr, 4.0f);
        g.setColour (cc::text);
        g.setFont (type::micro (12.0f));
        g.drawText (juce::String (look::categoryName (group.category)) + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")), cr, juce::Justification::centred);
        // Files
        const int n = static_cast<int> (group.items.size());
        for (int row = 0; row < std::min (n, rowsShown); ++row)
        {
            const int item = group.items[static_cast<std::size_t> (row)];
            const auto& it = s.items[static_cast<std::size_t> (item)];
            auto fr = fileRect (gi, row);
            if (selection.count (item) > 0)
            {
                g.setColour (cc::keyOn);
                g.fillRoundedRectangle (fr, 3.0f);
            }
            ceramic::iconPlay (g, fr.removeFromLeft (20.0f).reduced (5.0f, 6.0f), cc::textSecondary);
            const auto dot = fr.removeFromLeft (12.0f);
            g.setColour (confidenceColour (it.guess.confidence));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (dot.getCentre()));
            if (it.guess.layered)
            {
                g.setColour (juce::Colour (0xffd29b3a));
                g.drawText ("?", fr.removeFromRight (14.0f), juce::Justification::centred);
            }
            g.setColour (cc::text);
            g.setFont (type::micro (12.5f));
            g.drawFittedText (it.file.getFileName(), fr.toNearestInt().withTrimmedLeft (4), juce::Justification::centredLeft, 1, 1.0f);
        }
        if (n > rowsShown)
        {
            g.setColour (cc::textMicro);
            g.setFont (type::micro (11.5f));
            g.drawText ("+ " + juce::String (n - rowsShown) + " more (round robins)", fileRect (gi, rowsShown).translated (0.0f, -4.0f), juce::Justification::centredLeft);
        }
        else if (n > 1)
        {
            g.setColour (cc::textMicro);
            g.setFont (type::micro (11.5f));
            g.drawText (juce::String (n) + " round robins", fileRect (gi, n).translated (0.0f, -2.0f), juce::Justification::centredLeft);
        }
    }
    g.restoreState();

    // Not placed: could not load, duplicates.
    juce::StringArray notes;
    for (const auto& it : s.items)
    {
        if (! it.error.empty())
            notes.add (it.file.getFileName() + ": " + juce::String::fromUTF8 (it.error.c_str()));
        else if (it.duplicateOf >= 0)
            notes.add (it.file.getFileName() + ": same sound as " + s.items[static_cast<std::size_t> (it.duplicateOf)].file.getFileName());
    }
    g.setColour (notes.isEmpty() ? cc::textMicro : juce::Colour (0xffb5533c));
    g.setFont (type::micro (12.0f));
    const auto footer = juce::Rectangle<int> (18, getHeight() - 40, getWidth() - 36, 32);
    g.drawFittedText (notes.isEmpty() ? juce::String ("Click a file to hear it; right-click to give it its own pad or move it. The dot: green sure, amber likely, grey unsure.")
                                      : "Not imported: " + notes.joinIntoString ("   "),
                      footer, juce::Justification::centredLeft, 2, 1.0f);
}

void ImportView::mouseMove (const juce::MouseEvent& e)
{
    setTooltip (tooltipFor (e.position));
}

juce::String ImportView::tooltipFor (juce::Point<float> pos) const
{
    const auto& s = proc.importSession();
    for (int gi = 0; gi < static_cast<int> (s.groups.size()); ++gi)
        for (int row = 0; row < std::min (rowsShown, static_cast<int> (s.groups[static_cast<std::size_t> (gi)].items.size())); ++row)
            if (fileRect (gi, row).contains (pos))
            {
                const auto& it = s.items[static_cast<std::size_t> (s.groups[static_cast<std::size_t> (gi)].items[static_cast<std::size_t> (row)])];
                return juce::String (look::categoryName (it.guess.category)) + ", " + juce::String (juce::roundToInt (it.guess.confidence * 100.0)) + " % ("
                       + juce::String::fromUTF8 (it.guess.reason.c_str()) + ")";
            }
    return {};
}

void ImportView::mouseDown (const juce::MouseEvent& e)
{
    const auto& s = proc.importSession();
    for (int gi = 0; gi < static_cast<int> (s.groups.size()); ++gi)
    {
        if (padLabelRect (gi).contains (e.position))
        {
            showPadMenu (gi);
            return;
        }
        if (categoryRect (gi).contains (e.position))
        {
            showCategoryMenu (gi);
            return;
        }
        const auto& group = s.groups[static_cast<std::size_t> (gi)];
        for (int row = 0; row < std::min (rowsShown, static_cast<int> (group.items.size())); ++row)
            if (fileRect (gi, row).contains (e.position))
            {
                const int item = group.items[static_cast<std::size_t> (row)];
                if (e.mods.isPopupMenu())
                {
                    showItemMenu (gi, item);
                    return;
                }
                if (e.mods.isShiftDown() || e.mods.isCommandDown())
                    selection.count (item) > 0 ? (void) selection.erase (item) : (void) selection.insert (item);
                else
                    selection = { item };
                proc.auditionImportItem (item);
                refresh();
                return;
            }
    }
}

void ImportView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int rows = (static_cast<int> (proc.importSession().groups.size()) + columns - 1) / columns;
    scrollRow = juce::jlimit (0, std::max (0, rows - 3), scrollRow - (wheel.deltaY > 0.0f ? 1 : wheel.deltaY < 0.0f ? -1 : 0));
    repaint();
}

void ImportView::splitItem (int item)
{
    auto groups = proc.importSession().groups;
    for (auto& g : groups)
    {
        auto it = std::find (g.items.begin(), g.items.end(), item);
        if (it == g.items.end() || g.items.size() < 2)
            continue;
        g.items.erase (it);
        DrumsProcessor::ImportGroup own;
        own.items = { item };
        own.category = proc.importSession().items[static_cast<std::size_t> (item)].guess.category;
        own.pad = -1;   // to the tray until it gets a pad
        groups.push_back (own);
        proc.editImportGroups (groups, "Split");
        return;
    }
}

void ImportView::moveItem (int item, int toGroup)
{
    auto groups = proc.importSession().groups;
    if (toGroup < 0 || toGroup >= static_cast<int> (groups.size()))
        return;
    for (auto& g : groups)
        g.items.erase (std::remove (g.items.begin(), g.items.end(), item), g.items.end());
    groups[static_cast<std::size_t> (toGroup)].items.push_back (item);
    groups.erase (std::remove_if (groups.begin(), groups.end(), [] (const auto& g) { return g.items.empty(); }), groups.end());
    proc.editImportGroups (groups, "Move file");
}

void ImportView::groupSelection()
{
    if (selection.size() < 2)
        return;
    auto groups = proc.importSession().groups;
    // Into the group of the first selected file.
    int target = -1;
    for (int g = 0; g < static_cast<int> (groups.size()) && target < 0; ++g)
        for (int i : groups[static_cast<std::size_t> (g)].items)
            if (i == *selection.begin())
                target = g;
    if (target < 0)
        return;
    auto keep = groups[static_cast<std::size_t> (target)];
    for (auto& g : groups)
        g.items.erase (std::remove_if (g.items.begin(), g.items.end(), [this] (int i) { return selection.count (i) > 0; }), g.items.end());
    keep.items.erase (std::remove_if (keep.items.begin(), keep.items.end(), [this] (int i) { return selection.count (i) > 0; }), keep.items.end());
    for (int i : selection)
        keep.items.push_back (i);
    groups[static_cast<std::size_t> (target)] = keep;
    groups.erase (std::remove_if (groups.begin(), groups.end(), [] (const auto& g) { return g.items.empty(); }), groups.end());
    proc.editImportGroups (groups, "Group files");
    selection.clear();
    refresh();
}

void ImportView::setGroupPad (int group, int pad)
{
    auto groups = proc.importSession().groups;
    if (group < 0 || group >= static_cast<int> (groups.size()))
        return;
    // A pad holds one group: the one that had it goes to the tray.
    for (auto& g : groups)
        if (pad >= 0 && g.pad == pad)
            g.pad = -1;
    groups[static_cast<std::size_t> (group)].pad = pad;
    proc.editImportGroups (groups, "Group destination");
}

void ImportView::setGroupCategory (int group, drums::DrumCategory c)
{
    auto groups = proc.importSession().groups;
    if (group < 0 || group >= static_cast<int> (groups.size()))
        return;
    groups[static_cast<std::size_t> (group)].category = c;
    proc.editImportGroups (groups, "Group category");
}

void ImportView::showPadMenu (int group)
{
    juce::PopupMenu m;
    for (int slot = 0; slot < DrumsProcessor::numPads; ++slot)
    {
        const int p = proc.padAtSlot (slot);
        const bool used = ! proc.padLayers (p).layers.empty();
        m.addItem ("Pad " + juce::String (p + 1) + "  " + proc.padDisplayName (p) + (used ? "  (replaces it)" : ""), [this, group, p] { setGroupPad (group, p); });
    }
    m.addSeparator();
    m.addItem ("Keep in the tray", [this, group] { setGroupPad (group, -1); });
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (padLabelRect (group).toNearestInt())));
}

void ImportView::showCategoryMenu (int group)
{
    juce::PopupMenu m;
    for (int c = 0; c < 10; ++c)
    {
        const auto cat = static_cast<drums::DrumCategory> (c);
        m.addItem (look::categoryName (cat), [this, group, cat] { setGroupCategory (group, cat); });
    }
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (categoryRect (group).toNearestInt())));
}

void ImportView::showItemMenu (int group, int item)
{
    const auto& s = proc.importSession();
    juce::PopupMenu m;
    m.addItem ("Play", [this, item] { proc.auditionImportItem (item); });
    m.addItem ("Own pad (split from the group)", s.groups[static_cast<std::size_t> (group)].items.size() > 1, false, [this, item] { splitItem (item); });
    juce::PopupMenu moveTo;
    for (int g = 0; g < static_cast<int> (s.groups.size()); ++g)
        if (g != group)
        {
            const auto& gr = s.groups[static_cast<std::size_t> (g)];
            moveTo.addItem ((gr.pad >= 0 ? "Pad " + juce::String (gr.pad + 1) : juce::String ("Tray")) + "  " + look::categoryName (gr.category) + ": "
                                + s.items[static_cast<std::size_t> (gr.items.front())].file.getFileNameWithoutExtension(),
                            [this, item, g] { moveItem (item, g); });
        }
    m.addSubMenu ("Move to group", moveTo);
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

} // namespace osp::drumsplugin
