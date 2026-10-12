#include "BreakView.h"

#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace cc = ceramic::colour;
    namespace type = look::type;

    constexpr float headerHeight = 52.0f;
    constexpr float chipWidth = 74.0f, chipHeight = 58.0f, chipGap = 6.0f;

    void styleSlider (juce::Slider& s, double lo, double hi, double value, const juce::String& tip)
    {
        s.setRange (lo, hi, 0.0);
        s.setValue (value, juce::dontSendNotification);
        s.setTooltip (tip);
    }

    juce::String seconds (double s) { return juce::String (s, 3) + " s"; }
} // namespace

BreakView::BreakView (DrumsProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    styleSlider (sensitivity, 0.0, 1.0, 0.5, "Sensitivity: higher finds softer hits (ghost notes)");
    styleSlider (separation, 0.02, 0.25, 0.045, "Minimum gap between two slices");
    for (auto* s : { &sensitivity, &separation })
    {
        s->onDragEnd = [this] {
            auto settings = proc.breakSession().settings;
            settings.sensitivity = sensitivity.getValue();
            settings.minSeparationSeconds = separation.getValue();
            proc.resliceBreak (settings);
        };
        addAndMakeVisible (s);
    }
    midi.setTooltip ("The groove as MIDI on the planned pads: drag it into your DAW, or click to save a .mid file");
    midi.onClick = [this] { exportMidi(); };
    midi.addMouseListener (this, false);
    cancel.onClick = [this] {
        proc.closeBreak();
        if (onClose)
            onClose();
    };
    apply.onClick = [this] { showApplyMenu(); };
    assign.onClick = [this] { showAssignMenu (&assign); };
    category.onClick = [this] { showCategoryMenu (&category); };
    toTray.onClick = [this] { assignSelection (-1); };
    suggest.onClick = [this] {
        auto before = proc.breakSession().slices;
        proc.suggestBreakPlan();
        auto after = proc.breakSession().slices;
        proc.editBreakSlices (before, {}, false);   // back, then forward as one undo step
        proc.editBreakSlices (after, "Suggest plan");
    };
    for (auto* b : { &midi, &cancel, &apply, &assign, &category, &toTray, &suggest })
        addAndMakeVisible (b);
}

void BreakView::refresh()
{
    const auto& s = proc.breakSession();
    sensitivity.setValue (s.settings.sensitivity, juce::dontSendNotification);
    separation.setValue (s.settings.minSeparationSeconds, juce::dontSendNotification);
    std::set<int> kept;
    for (int i : selection)
        if (i < static_cast<int> (s.slices.size()))
            kept.insert (i);
    selection = kept;
    const bool ready = s.sound != nullptr;
    for (auto* b : { &midi, &apply, &suggest })
        b->setEnabled (ready);
    for (auto* b : { &assign, &category, &toTray })
        b->setEnabled (ready && ! selection.empty());
    repaint();
}

juce::Rectangle<float> BreakView::waveArea() const { return { 16.0f, headerHeight + 6.0f, static_cast<float> (getWidth()) - 32.0f, 236.0f }; }
juce::Rectangle<float> BreakView::trayArea() const
{
    const auto w = waveArea();
    return { w.getX(), w.getBottom() + 34.0f, w.getWidth(), chipHeight + 12.0f };
}

float BreakView::xOfFrame (std::int64_t frame) const
{
    const auto& s = proc.breakSession();
    const auto w = waveArea().reduced (8.0f, 0.0f);
    if (s.audio == nullptr || s.audio->numFrames() <= 0)
        return w.getX();
    return w.getX() + w.getWidth() * static_cast<float> (frame) / static_cast<float> (s.audio->numFrames());
}

std::int64_t BreakView::frameAtX (float x) const
{
    const auto& s = proc.breakSession();
    const auto w = waveArea().reduced (8.0f, 0.0f);
    if (s.audio == nullptr)
        return 0;
    return juce::jlimit<std::int64_t> (0, s.audio->numFrames() - 1, static_cast<std::int64_t> ((x - w.getX()) / w.getWidth() * static_cast<float> (s.audio->numFrames())));
}

int BreakView::sliceAtX (float x) const
{
    const auto f = frameAtX (x);
    const auto& slices = proc.breakSession().slices;
    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
        if (f >= slices[static_cast<std::size_t> (i)].start && f < slices[static_cast<std::size_t> (i)].end)
            return i;
    return -1;
}

int BreakView::markerNear (float x) const
{
    const auto& slices = proc.breakSession().slices;
    int best = -1;
    float bestDistance = 6.0f;
    for (int i = 0; i < static_cast<int> (slices.size()); ++i)
    {
        const float d = std::abs (xOfFrame (slices[static_cast<std::size_t> (i)].start) - x);
        if (d < bestDistance)
        {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

juce::Rectangle<float> BreakView::chipRect (int slice) const
{
    const auto t = trayArea().reduced (6.0f);
    return { t.getX() + static_cast<float> (slice - trayScroll) * (chipWidth + chipGap), t.getY(), chipWidth, chipHeight };
}

int BreakView::chipAt (juce::Point<float> p) const
{
    if (! trayArea().contains (p))
        return -1;
    const auto& slices = proc.breakSession().slices;
    for (int i = trayScroll; i < static_cast<int> (slices.size()); ++i)
        if (chipRect (i).contains (p))
            return i;
    return -1;
}

juce::Colour BreakView::sliceColour (const drums::BreakSlice& s) const
{
    // What the slice is (when known), else where it goes; grey in the tray.
    if (s.category != drums::DrumCategory::unknown)
        return look::category (s.category);
    if (s.pad >= 0)
        return look::category (proc.padInfo (s.pad).category);
    return juce::Colour (0xff9a958c);
}

void BreakView::resized()
{
    const int w = getWidth();
    sensitivity.setBounds (300, 22, 180, 24);
    separation.setBounds (530, 22, 160, 24);
    midi.setBounds (w - 352, 10, 86, 34);
    cancel.setBounds (w - 258, 10, 104, 34);
    apply.setBounds (w - 146, 10, 130, 34);
    const auto t = trayArea();
    auto row = juce::Rectangle<int> (static_cast<int> (t.getX()), static_cast<int> (t.getBottom()) + 10, 600, 32);
    for (auto* b : { &assign, &category, &toTray, &suggest })
    {
        b->setBounds (row.removeFromLeft (b == &suggest ? 136 : 124));
        row.removeFromLeft (8);
    }
}

void BreakView::paint (juce::Graphics& g)
{
    ceramic::card (g, getLocalBounds().toFloat(), 12.0f);
    const auto& s = proc.breakSession();
    g.setColour (cc::text);
    g.setFont (plugin::fonts::make (14.5f, look::Weight::semibold, 0.08f));
    g.drawText ("SLICE BREAK", juce::Rectangle<int> (18, 0, 140, static_cast<int> (headerHeight)), juce::Justification::centredLeft);
    g.setColour (cc::textSecondary);
    g.setFont (plugin::fonts::make (11.5f, look::Weight::medium, 0.08f));
    g.drawText ("SENSITIVITY", juce::Rectangle<int> (300, 4, 150, 16), juce::Justification::centredLeft);
    g.drawText ("MIN GAP  " + juce::String (juce::roundToInt (separation.getValue() * 1000.0)) + " ms", juce::Rectangle<int> (530, 4, 160, 16), juce::Justification::centredLeft);
    g.setFont (type::micro (13.0f));
    g.setColour (cc::textSecondary);
    juce::String info = s.file.getFileName();
    if (s.audio != nullptr)
        info << "   " << s.slices.size() << " slices   " << (s.bpm > 0.0 ? juce::String (s.bpm, 1) + " BPM" : juce::String());
    g.drawFittedText (info, juce::Rectangle<int> (720, 10, getWidth() - 1100, 34), juce::Justification::centredLeft, 1, 1.0f);

    // Waveform with the slices
    const auto wave = waveArea();
    const auto inner = ceramic::display (g, wave, 8.0f);
    if (s.sound == nullptr)
    {
        g.setColour (juce::Colour (0xff8d8c86));
        g.setFont (type::micro (14.0f));
        g.drawFittedText (s.loading ? "Loading and slicing..." : "Could not load the break: " + juce::String::fromUTF8 (s.error.c_str()),
                          inner.toNearestInt().reduced (20, 0), juce::Justification::centred, 2, 1.0f);
        return;
    }
    const auto area = inner.reduced (2.0f, 10.0f);
    for (int i = 0; i < static_cast<int> (s.slices.size()); ++i)
    {
        const auto& sl = s.slices[static_cast<std::size_t> (i)];
        const float x0 = xOfFrame (sl.start), x1 = xOfFrame (sl.end);
        const auto c = sliceColour (sl);
        const bool selected = selection.count (i) > 0;
        g.setColour (c.withAlpha (selected ? 0.45f : sl.pad >= 0 ? 0.22f : 0.08f));
        g.fillRect (juce::Rectangle<float> (x0, area.getY(), std::max (1.0f, x1 - x0), area.getHeight()));
    }
    look::waveform (g, area, s.sound->peakMin, s.sound->peakMax, juce::Colour (0xffe9e2d4), 0.0f);
    g.setFont (plugin::fonts::make (11.5f, look::Weight::semibold));
    for (int i = 0; i < static_cast<int> (s.slices.size()); ++i)
    {
        const auto& sl = s.slices[static_cast<std::size_t> (i)];
        const float x = xOfFrame (sl.start);
        const auto c = sliceColour (sl);
        g.setColour ((i == hoverMarker || i == draggingMarker) ? cc::accent : c.brighter (0.4f));
        g.fillRect (juce::Rectangle<float> (x - 0.75f, area.getY(), 1.5f, area.getHeight()));
        const auto flag = juce::Rectangle<float> (x, area.getY(), 22.0f, 15.0f);
        g.fillRoundedRectangle (flag, 3.0f);
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (i + 1), flag, juce::Justification::centred);
        if (sl.pad >= 0)
        {
            g.setColour (c.brighter (0.5f));
            g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92") + juce::String (sl.pad + 1), juce::Rectangle<float> (x + 2.0f, area.getBottom() - 16.0f, 40.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
        if (sl.layered)
        {
            g.setColour (juce::Colour (0xffe0b050));
            g.drawText ("?", juce::Rectangle<float> (x + 24.0f, area.getY(), 12.0f, 15.0f), juce::Justification::centred);
        }
    }

    // Tray
    const auto tray = trayArea();
    g.setColour (cc::textSecondary);
    g.setFont (plugin::fonts::make (12.0f, look::Weight::medium, 0.08f));
    g.drawText ("SLICES  " + juce::String (static_cast<int> (s.slices.size())) + "   drag onto a pad, or select and Assign. Delete removes a boundary.",
                juce::Rectangle<float> (tray.getX(), tray.getY() - 26.0f, tray.getWidth(), 22.0f), juce::Justification::centredLeft);
    ceramic::recess (g, tray, 6.0f, juce::Colour (0xfff7f2ea));
    g.saveState();
    g.reduceClipRegion (tray.reduced (3.0f).toNearestInt());
    for (int i = trayScroll; i < static_cast<int> (s.slices.size()); ++i)
    {
        const auto r = chipRect (i);
        if (r.getX() > tray.getRight())
            break;
        const auto& sl = s.slices[static_cast<std::size_t> (i)];
        const auto c = sliceColour (sl);
        const bool selected = selection.count (i) > 0;
        ceramic::contactShadow (g, r, 5.0f, 0.5f);
        g.setColour (selected ? cc::keyOn : cc::cardTop);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (selected ? cc::accent : juce::Colour (0xffd8cfc2));
        g.drawRoundedRectangle (r, 5.0f, selected ? 1.6f : 1.0f);
        g.setColour (c);
        g.fillRoundedRectangle (r.withHeight (5.0f).reduced (4.0f, 0.0f).translated (0.0f, 4.0f), 2.0f);
        g.setColour (cc::text);
        g.setFont (plugin::fonts::make (15.0f, look::Weight::semibold));
        g.drawText (juce::String (i + 1), r.withTrimmedTop (10.0f).withHeight (20.0f), juce::Justification::centred);
        g.setFont (type::micro (11.0f));
        g.setColour (cc::textSecondary);
        const juce::String where = sl.pad >= 0 ? "PAD " + juce::String (sl.pad + 1) : juce::String ("tray");
        const juce::String what = sl.category != drums::DrumCategory::unknown ? juce::String (look::categoryName (sl.category)) : juce::String();
        g.drawText (where, r.withTrimmedTop (30.0f).withHeight (13.0f), juce::Justification::centred);
        if (what.isNotEmpty())
            g.drawText (what, r.withTrimmedTop (43.0f).withHeight (12.0f), juce::Justification::centred);
        // Strength: a small bar on the left edge.
        g.setColour (c.withAlpha (0.8f));
        const float h = (r.getHeight() - 16.0f) * static_cast<float> (sl.strength);
        g.fillRect (juce::Rectangle<float> (r.getX() + 3.0f, r.getBottom() - 4.0f - h, 2.5f, h));
    }
    g.restoreState();
    // Selection summary
    if (! selection.empty())
    {
        const auto& first = s.slices[static_cast<std::size_t> (*selection.begin())];
        g.setColour (cc::textSecondary);
        g.setFont (type::micro (12.5f));
        g.drawText (juce::String (static_cast<int> (selection.size())) + " selected   slice " + juce::String (*selection.begin() + 1) + " at "
                        + seconds (static_cast<double> (first.onset) / s.audio->sampleRate),
                    juce::Rectangle<float> (tray.getX() + 620.0f, tray.getBottom() + 10.0f, 500.0f, 32.0f), juce::Justification::centredLeft);
    }
}

void BreakView::mouseMove (const juce::MouseEvent& e)
{
    const int m = waveArea().contains (e.position) ? markerNear (e.position.x) : -1;
    if (m != hoverMarker)
    {
        hoverMarker = m;
        setMouseCursor (m >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void BreakView::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent == &midi)
        return;
    grabKeyboardFocus();
    chipDragStarted = false;
    const auto& s = proc.breakSession();
    if (s.sound == nullptr)
        return;
    if (waveArea().contains (e.position))
    {
        const int marker = markerNear (e.position.x);
        if (marker >= 0 && ! e.mods.isPopupMenu())
        {
            draggingMarker = marker;
            markerMoved = false;
            return;
        }
        const int slice = sliceAtX (e.position.x);
        if (slice < 0)
            return;
        if (e.mods.isPopupMenu())
        {
            if (selection.count (slice) == 0)
                selection = { slice };
            showSliceMenu (slice);
        }
        else
        {
            if (e.mods.isShiftDown() || e.mods.isCommandDown())
                selection.count (slice) > 0 ? (void) selection.erase (slice) : (void) selection.insert (slice);
            else
                selection = { slice };
            proc.auditionSlice (slice);
        }
        refresh();
        return;
    }
    const int chip = chipAt (e.position);
    if (chip >= 0)
    {
        if (e.mods.isShiftDown() || e.mods.isCommandDown())
            selection.count (chip) > 0 ? (void) selection.erase (chip) : (void) selection.insert (chip);
        else if (selection.count (chip) == 0)
            selection = { chip };
        if (e.mods.isPopupMenu())
            showSliceMenu (chip);
        else
            proc.auditionSlice (chip);
        refresh();
    }
}

void BreakView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.eventComponent == &midi)
    {
        // Drag the groove into the DAW as a MIDI file.
        if (e.getDistanceFromDragStart() > 8 && ! chipDragStarted)
        {
            chipDragStarted = true;
            const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                                  .getChildFile (juce::File::createLegalFileName (proc.breakSession().file.getFileNameWithoutExtension() + " groove.mid"));
            juce::String error;
            const auto groove = proc.sessionGroove();
            if (proc.writeGrooveMidi (file, error, &groove))
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
        }
        return;
    }
    if (draggingMarker >= 0)
    {
        auto slices = proc.breakSession().slices;
        const auto audio = proc.breakSession().audio;
        if (audio != nullptr && drums::moveSliceStart (slices, draggingMarker, frameAtX (e.position.x), audio->numFrames()))
        {
            proc.editBreakSlices (slices, "Move slice", ! markerMoved);
            markerMoved = true;
        }
        return;
    }
    // Chips: drag the selection onto a pad in the grid.
    const int chip = chipAt (e.mouseDownPosition);
    if (chip >= 0 && ! chipDragStarted && e.getDistanceFromDragStart() > 6)
    {
        chipDragStarted = true;
        if (selection.count (chip) == 0)
            selection = { chip };
        juce::StringArray list;
        for (int i : selection)
            list.add (juce::String (i));
        if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            juce::Image ghost (juce::Image::ARGB, static_cast<int> (chipWidth), static_cast<int> (chipHeight), true);
            {
                juce::Graphics gg (ghost);
                gg.setColour (cc::accent.withAlpha (0.8f));
                gg.fillRoundedRectangle (ghost.getBounds().toFloat().reduced (1.0f), 5.0f);
                gg.setColour (juce::Colours::white);
                gg.setFont (plugin::fonts::make (15.0f, look::Weight::semibold));
                gg.drawText (list.size() == 1 ? juce::String (chip + 1) : juce::String (list.size()) + " slices", ghost.getBounds(), juce::Justification::centred);
            }
            container->startDragging (dragPrefix + list.joinIntoString (","), this, juce::ScaledImage (ghost), true);
        }
    }
}

void BreakView::mouseUp (const juce::MouseEvent&)
{
    draggingMarker = -1;
    markerMoved = false;
}

void BreakView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto& s = proc.breakSession();
    if (s.audio == nullptr || ! waveArea().contains (e.position) || markerNear (e.position.x) >= 0)
        return;
    auto slices = s.slices;
    const int added = drums::addSliceAt (slices, *s.audio, frameAtX (e.position.x), s.settings);
    if (added >= 0)
    {
        proc.editBreakSlices (slices, "Add slice");
        selection = { added };
        refresh();
    }
}

void BreakView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! trayArea().contains (e.position))
        return;
    const int n = static_cast<int> (proc.breakSession().slices.size());
    const int visible = static_cast<int> (trayArea().getWidth() / (chipWidth + chipGap));
    const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    trayScroll = juce::jlimit (0, std::max (0, n - visible), trayScroll - (delta > 0.0f ? 1 : delta < 0.0f ? -1 : 0));
    repaint();
}

bool BreakView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        removeSelectedBoundaries();
        return true;
    }
    if (key == juce::KeyPress::spaceKey && ! selection.empty())
    {
        proc.auditionSlice (*selection.begin());
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        selection.clear();
        refresh();
        return true;
    }
    return false;
}

void BreakView::assignSelection (int pad)
{
    auto slices = proc.breakSession().slices;
    for (int i : selection)
        if (i < static_cast<int> (slices.size()))
            slices[static_cast<std::size_t> (i)].pad = pad;
    proc.editBreakSlices (slices, pad >= 0 ? "Assign slices" : "Slices to tray");
}

void BreakView::setSelectionCategory (drums::DrumCategory c)
{
    auto slices = proc.breakSession().slices;
    for (int i : selection)
        if (i < static_cast<int> (slices.size()))
        {
            slices[static_cast<std::size_t> (i)].category = c;
            slices[static_cast<std::size_t> (i)].confidence = 1.0;   // the user's word
        }
    proc.editBreakSlices (slices, "Slice category");
}

void BreakView::removeSelectedBoundaries()
{
    auto slices = proc.breakSession().slices;
    // From the last, so indices stay valid; the first slice keeps its start.
    for (auto it = selection.rbegin(); it != selection.rend(); ++it)
        drums::removeSlice (slices, *it);
    proc.editBreakSlices (slices, "Remove slices");
    selection.clear();
    refresh();
}

void BreakView::showSliceMenu (int slice)
{
    juce::PopupMenu m;
    m.addItem ("Play", [this, slice] { proc.auditionSlice (slice); });
    juce::PopupMenu pads;
    for (int slot = 0; slot < DrumsProcessor::numPads; ++slot)
    {
        const int p = proc.padAtSlot (slot);
        pads.addItem ("Pad " + juce::String (p + 1) + "  " + proc.padDisplayName (p), [this, p] { assignSelection (p); });
    }
    m.addSubMenu ("Assign to", pads);
    m.addItem ("Keep in the tray", [this] { assignSelection (-1); });
    juce::PopupMenu cats;
    for (int c = 0; c < 10; ++c)
    {
        const auto cat = static_cast<drums::DrumCategory> (c);
        cats.addItem (look::categoryName (cat), [this, cat] { setSelectionCategory (cat); });
    }
    m.addSubMenu ("Category", cats);
    m.addSeparator();
    m.addItem ("Remove boundary (join with the previous)", slice > 0, false, [this] { removeSelectedBoundaries(); });
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void BreakView::showAssignMenu (juce::Component* target)
{
    juce::PopupMenu m;
    for (int slot = 0; slot < DrumsProcessor::numPads; ++slot)
    {
        const int p = proc.padAtSlot (slot);
        const bool occupied = ! proc.padLayers (p).layers.empty();
        m.addItem ("Pad " + juce::String (p + 1) + "  " + proc.padDisplayName (p) + (occupied ? "  (has a sound)" : ""), [this, p] { assignSelection (p); });
    }
    m.addSeparator();
    m.addItem ("Keep in the tray", [this] { assignSelection (-1); });
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void BreakView::showCategoryMenu (juce::Component* target)
{
    juce::PopupMenu m;
    for (int c = 0; c < 10; ++c)
    {
        const auto cat = static_cast<drums::DrumCategory> (c);
        m.addItem (look::categoryName (cat), [this, cat] { setSelectionCategory (cat); });
    }
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

void BreakView::showApplyMenu()
{
    juce::PopupMenu m;
    int planned = 0;
    for (const auto& s : proc.breakSession().slices)
        planned += s.pad >= 0 ? 1 : 0;
    m.addSectionHeader (juce::String (planned) + " slices planned onto pads");
    auto run = [this] (DrumsProcessor::BreakApply how) {
        const auto report = proc.applyBreak (how);
        if (onReport)
            onReport ("Apply break", report);
        if (onClose)
            onClose();
    };
    m.addItem ("New kit from this break", [run] { run (DrumsProcessor::BreakApply::newKit); });
    m.addItem ("Fill the empty pads only", [run] { run (DrumsProcessor::BreakApply::emptyPads); });
    m.addItem ("Merge into this kit (round robins on used pads)", [run] { run (DrumsProcessor::BreakApply::merge); });
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&apply));
}

void BreakView::exportMidi()
{
    if (chipDragStarted)
        return;   // it was a drag into the DAW
    const auto start = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                           .getChildFile (juce::File::createLegalFileName (proc.breakSession().file.getFileNameWithoutExtension() + " groove.mid"));
    chooser = std::make_unique<juce::FileChooser> ("Save the groove as MIDI", start, "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc) {
                              if (fc.getResult() == juce::File())
                                  return;
                              juce::String error;
                              const auto groove = proc.sessionGroove();
                              if (! proc.writeGrooveMidi (fc.getResult().withFileExtension ("mid"), error, &groove) && onReport)
                                  onReport ("Save MIDI", error);
                          });
}

} // namespace osp::drumsplugin
