#include "KeyMapEditor.h"

#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace colour = look::colour;
    namespace type = look::type;
    using drums::Collision;
    using drums::KeyMap;
    using drums::Zone;

    constexpr float headerHeight = 40.0f;
    constexpr float bannerHeight = 28.0f;
    constexpr float labelWidth = 132.0f;
    constexpr float inspectorWidth = 236.0f;
    constexpr float keyboardHeight = 62.0f;
    constexpr float scrollHeight = 12.0f;
    constexpr float edgeGrab = 5.0f;
    constexpr juce::uint32 learnTimeoutMs = 30000;

    const juce::Colour laneA { 0xfff3ede3 };
    const juce::Colour laneB { 0xffece5d9 };
    const juce::Colour danger { 0xffc8452b };
} // namespace

KeyMapEditor::KeyMapEditor (DrumsProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);

    look::makeSegment (padMode, 31);
    look::makeSegment (expandedMode, 31);
    padMode.onClick = [this] {
        cancelPreview();
        proc.setKeyboardMode (drums::KeyboardMode::pad);
        refreshFromProcessor();
        if (onModeChanged)
            onModeChanged();
    };
    expandedMode.onClick = [this] {
        cancelPreview();
        proc.setKeyboardMode (drums::KeyboardMode::expanded);
        refreshFromProcessor();
        if (onModeChanged)
            onModeChanged();
    };

    autoMap.setTextWhenNothingSelected ("AUTO MAP");
    autoMap.addItem ("Standard Drum Kit", static_cast<int> (AutoMap::standard));
    autoMap.addItem ("Compact Chromatic", static_cast<int> (AutoMap::chromatic));
    autoMap.addItem ("One Octave Per Pad", static_cast<int> (AutoMap::oneOctave));
    autoMap.addItem ("Two Octaves Per Pad", static_cast<int> (AutoMap::twoOctaves));
    autoMap.addItem ("Fit From Selected Root", static_cast<int> (AutoMap::fromSelectedRoot));
    autoMap.onChange = [this] {
        if (const int id = autoMap.getSelectedId(); id > 0)
            previewAutoMap (static_cast<AutoMap> (id));
        autoMap.setSelectedId (0, juce::dontSendNotification);
    };

    learnButton.onClick = [this] { learn == Learn::off ? startLearn() : cancelLearn(); };
    overlapButton.setClickingTogglesState (true);
    overlapButton.setTooltip ("Advanced: let zones (or PAD notes) overlap, so one key plays several pads. Off keeps every key to one pad.");
    overlapButton.onClick = [this] {
        auto next = proc.arrangement();
        next.map.allowOverlap = overlapButton.getToggleState();
        proc.applyEdit (next, next.map.allowOverlap ? "Allow overlap" : "Exclusive keys");
        refreshFromProcessor();
    };
    undoButton.onClick = [this] {
        cancelGesture();
        proc.undoManager().undo();
        refreshFromProcessor();
    };
    redoButton.onClick = [this] {
        cancelGesture();
        proc.undoManager().redo();
        refreshFromProcessor();
    };
    zoomOut.onClick = [this] { zoomBy (1.5, low + span * 0.5); };
    zoomIn.onClick = [this] { zoomBy (1.0 / 1.5, low + span * 0.5); };
    fitButton.onClick = [this] { fitAssigned(); };
    allButton.onClick = [this] { showFullRange(); };
    doneButton.onClick = [this] {
        cancelLearn();
        cancelPreview();
        cancelGesture();
        if (onClose)
            onClose();
    };
    applyButton.onClick = [this] { applyPreview(); };
    cancelButton.onClick = [this] {
        if (preview)
            cancelPreview();
        else
            cancelLearn();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &padMode, &expandedMode, &autoMap, &learnButton, &overlapButton, &undoButton, &redoButton,
                                                              &zoomOut, &zoomIn, &fitButton, &allButton, &doneButton })
        addAndMakeVisible (c);
    addChildComponent (applyButton);
    addChildComponent (cancelButton);

    scroll.setAutoHide (false);
    scroll.setRangeLimits (0.0, 128.0);
    scroll.addListener (this);
    addAndMakeVisible (scroll);

    auto box = [this] (juce::Slider& s, juce::Label& l, const juce::String& text) {
        s.setSliderStyle (juce::Slider::IncDecButtons);
        s.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 24);
        s.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
        s.setRange (0.0, 127.0, 1.0);
        s.textFromValueFunction = [] (double v) { return look::noteName (static_cast<int> (v)) + " " + juce::String (static_cast<int> (v)); };
        s.valueFromTextFunction = [] (const juce::String& t) {
            // Accepts "36" or "C2" (OSP's names: MIDI 60 = C4).
            const auto trimmed = t.trim();
            if (trimmed.containsOnly ("0123456789"))
                return trimmed.getDoubleValue();
            for (int n = 0; n < 128; ++n)
                if (look::noteName (n).equalsIgnoreCase (trimmed.upToFirstOccurrenceOf (" ", false, false)))
                    return static_cast<double> (n);
            return trimmed.getDoubleValue();
        };
        s.onValueChange = [this] { inspectorEdited(); };
        addAndMakeVisible (s);
        l.setText (text, juce::dontSendNotification);
        l.setFont (type::controlLabel (0.85f));
        l.setColour (juce::Label::textColourId, colour::textSecondary);
        addAndMakeVisible (l);
    };
    box (noteBox, noteLabel, "NOTE");
    box (lowBox, lowLabel, "LOW");
    box (highBox, highLabel, "HIGH");
    box (rootBox, rootLabel, "ROOT");
    resetButton.setTooltip ("Back to the factory note / zone for this pad");
    resetButton.onClick = [this] {
        const int pad = proc.selectedPad;
        auto next = proc.keyMap();
        if (expanded())
            next.zone[static_cast<std::size_t> (pad)] = drums::defaultZones()[static_cast<std::size_t> (pad)];
        else
            next.padNote[static_cast<std::size_t> (pad)] = drums::defaultPadNotes()[static_cast<std::size_t> (pad)];
        propose (next, pad, expanded() ? Gesture::resizeLow : Gesture::padNote, next.padNote[static_cast<std::size_t> (pad)],
                 next.zone[static_cast<std::size_t> (pad)].low, "Reset assignment", true);
    };
    clearButton.setTooltip ("Remove this pad's note / zone (it stays silent in this mode)");
    clearButton.onClick = [this] {
        auto next = proc.arrangement();
        if (expanded())
            drums::clearZone (next.map, proc.selectedPad);
        else
            drums::setPadNote (next.map, proc.selectedPad, drums::noNote);
        proc.applyEdit (next, "Clear assignment");
        refreshFromProcessor();
    };
    addAndMakeVisible (resetButton);
    addAndMakeVisible (clearButton);

    refreshFromProcessor();
    fitAssigned();
    startTimerHz (30);
}

KeyMapEditor::~KeyMapEditor()
{
    proc.setLearnArmed (false);
    scroll.removeListener (this);
}

bool KeyMapEditor::expanded() const { return proc.keyboardMode() == drums::KeyboardMode::expanded; }

// ---------------------------------------------------------------------------------------
// Geometry: ONE note -> x mapping for every lane and the keyboard.
// ---------------------------------------------------------------------------------------

juce::Rectangle<float> KeyMapEditor::timelineArea() const
{
    auto r = getLocalBounds().toFloat();
    r.removeFromTop (headerHeight + bannerHeight);
    r.removeFromRight (inspectorWidth + 12.0f);
    r.removeFromLeft (labelWidth);
    r.removeFromBottom (keyboardHeight + scrollHeight + 4.0f);
    return r;
}

juce::Rectangle<float> KeyMapEditor::keyboardArea() const
{
    const auto t = timelineArea();
    return { t.getX(), t.getBottom() + 2.0f, t.getWidth(), keyboardHeight };
}

int KeyMapEditor::laneCount() const { return proc.nineLayout ? 9 : 8; }

int KeyMapEditor::padOfLane (int lane) const { return proc.padAtSlot (lane); }

int KeyMapEditor::laneOfPad (int pad) const
{
    const int s = proc.slotOfPad (pad);
    return s < laneCount() ? s : -1;
}

juce::Rectangle<float> KeyMapEditor::laneArea (int lane) const
{
    const auto t = timelineArea();
    const float h = t.getHeight() / static_cast<float> (laneCount());
    return { t.getX(), t.getY() + h * static_cast<float> (lane), t.getWidth(), h };
}

float KeyMapEditor::noteToX (double note) const noexcept
{
    const auto t = timelineArea();
    return t.getX() + static_cast<float> ((note - low) / span) * t.getWidth();
}

double KeyMapEditor::xToNote (float x) const noexcept
{
    const auto t = timelineArea();
    return low + static_cast<double> ((x - t.getX()) / std::max (1.0f, t.getWidth())) * span;
}

void KeyMapEditor::setView (double newLow, double visible)
{
    span = juce::jlimit (12.0, 128.0, visible);
    low = juce::jlimit (0.0, 128.0 - span, newLow);
    updateScrollBar();
    repaint();
}

void KeyMapEditor::zoomBy (double factor, double anchorNote)
{
    const double newSpan = juce::jlimit (12.0, 128.0, span * factor);
    const double t = (anchorNote - low) / span;
    setView (anchorNote - t * newSpan, newSpan);
}

void KeyMapEditor::fitAssigned()
{
    const auto& m = shownMap();
    int lo = 128, hi = -1;
    for (int lane = 0; lane < laneCount(); ++lane)
    {
        const auto i = static_cast<std::size_t> (padOfLane (lane));
        if (expanded() && m.zone[i].enabled())
        {
            lo = std::min (lo, m.zone[i].low);
            hi = std::max (hi, m.zone[i].high);
        }
        else if (! expanded() && m.padNote[i] >= 0)
        {
            lo = std::min (lo, m.padNote[i]);
            hi = std::max (hi, m.padNote[i]);
        }
    }
    if (hi < 0)
    {
        setView (24.0, 72.0);
        return;
    }
    const double visible = std::max (24.0, static_cast<double> (hi - lo + 1) + 6.0);
    setView (lo - (visible - (hi - lo + 1)) * 0.5, visible);
}

void KeyMapEditor::updateScrollBar()
{
    scroll.setCurrentRange (low, span, juce::dontSendNotification);
}

void KeyMapEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    low = juce::jlimit (0.0, 128.0 - span, newRangeStart);
    repaint();
}

void KeyMapEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (static_cast<int> (headerHeight)).reduced (0, 6);
    header.removeFromLeft (110);   // the title
    padMode.setBounds (header.removeFromLeft (64));
    expandedMode.setBounds (header.removeFromLeft (96));
    header.removeFromLeft (12);
    autoMap.setBounds (header.removeFromLeft (190));
    header.removeFromLeft (8);
    learnButton.setBounds (header.removeFromLeft (76));
    header.removeFromLeft (8);
    overlapButton.setBounds (header.removeFromLeft (124));
    doneButton.setBounds (header.removeFromRight (76));
    header.removeFromRight (14);
    allButton.setBounds (header.removeFromRight (58));
    fitButton.setBounds (header.removeFromRight (46));
    zoomIn.setBounds (header.removeFromRight (30));
    zoomOut.setBounds (header.removeFromRight (30));
    header.removeFromRight (14);
    redoButton.setBounds (header.removeFromRight (62));
    undoButton.setBounds (header.removeFromRight (62));

    auto banner = r.removeFromTop (static_cast<int> (bannerHeight)).reduced (0, 2);
    cancelButton.setBounds (banner.removeFromRight (80));
    banner.removeFromRight (6);
    applyButton.setBounds (banner.removeFromRight (80));

    const auto k = keyboardArea();
    scroll.setBounds (juce::Rectangle<float> (k.getX(), k.getBottom() + 3.0f, k.getWidth(), scrollHeight).toNearestInt());

    auto inspector = getLocalBounds().withTrimmedTop (static_cast<int> (headerHeight + bannerHeight)).removeFromRight (static_cast<int> (inspectorWidth)).reduced (14, 12);
    inspector.removeFromTop (54);   // pad name and mode
    auto row = [&inspector] (juce::Label& l, juce::Slider& s) {
        auto line = inspector.removeFromTop (28);
        l.setBounds (line.removeFromLeft (54));
        s.setBounds (line);
        inspector.removeFromTop (6);
    };
    {
        // NOTE (PAD) and LOW (EXPANDED) share the first row: only one map is edited at a time.
        auto first = inspector;
        row (lowLabel, lowBox);
        auto line = first.removeFromTop (28);
        noteLabel.setBounds (line.removeFromLeft (54));
        noteBox.setBounds (line);
    }
    row (highLabel, highBox);
    row (rootLabel, rootBox);
    inspector.removeFromTop (44);   // range size, transposition
    auto buttons = inspector.removeFromTop (28);
    resetButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 3));
    buttons.removeFromLeft (6);
    clearButton.setBounds (buttons);
    updateScrollBar();
}

// ---------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------

const KeyMap& KeyMapEditor::shownMap() const noexcept
{
    if (gesture != Gesture::none)
        return candidate;
    if (pending)
        return pending->candidate;
    if (preview)
        return *preview;
    return proc.keyMap();
}

void KeyMapEditor::refreshFromProcessor()
{
    const bool exp = expanded();
    padMode.setToggleState (! exp, juce::dontSendNotification);
    expandedMode.setToggleState (exp, juce::dontSendNotification);
    overlapButton.setToggleState (proc.keyMap().allowOverlap, juce::dontSendNotification);
    undoButton.setEnabled (proc.undoManager().canUndo());
    redoButton.setEnabled (proc.undoManager().canRedo());
    autoMap.setItemEnabled (static_cast<int> (AutoMap::oneOctave), exp);
    autoMap.setItemEnabled (static_cast<int> (AutoMap::twoOctaves), exp);
    applyButton.setVisible (preview.has_value());
    cancelButton.setVisible (preview.has_value() || learn != Learn::off);
    learnButton.setButtonText (learn == Learn::off ? "LEARN" : "LEARNING");
    learnButton.setToggleState (learn != Learn::off, juce::dontSendNotification);
    updateInspector();
    repaint();
}

void KeyMapEditor::updateInspector()
{
    const bool exp = expanded();
    const auto& m = shownMap();
    const auto i = static_cast<std::size_t> (proc.selectedPad);
    updatingInspector = true;
    for (auto* c : std::initializer_list<juce::Component*> { &noteBox, &noteLabel })
        c->setVisible (! exp);
    for (auto* c : std::initializer_list<juce::Component*> { &lowBox, &lowLabel, &highBox, &highLabel, &rootBox, &rootLabel })
        c->setVisible (exp);
    noteBox.setValue (std::max (0, m.padNote[i]), juce::dontSendNotification);
    noteBox.setEnabled (m.padNote[i] >= 0 || true);
    const auto& z = m.zone[i];
    lowBox.setValue (std::max (0, z.low), juce::dontSendNotification);
    highBox.setValue (std::max (0, z.high), juce::dontSendNotification);
    rootBox.setValue (std::max (0, z.rootNote()), juce::dontSendNotification);
    updatingInspector = false;
}

void KeyMapEditor::inspectorEdited()
{
    if (updatingInspector)
        return;
    const int pad = proc.selectedPad;
    const auto i = static_cast<std::size_t> (pad);
    auto next = proc.keyMap();
    if (! expanded())
    {
        next.padNote[i] = static_cast<int> (noteBox.getValue());
        propose (next, pad, Gesture::padNote, next.padNote[i], -1, "Assign note", true);
        return;
    }
    const int lo = static_cast<int> (lowBox.getValue()), hi = static_cast<int> (highBox.getValue());
    const int root = static_cast<int> (rootBox.getValue());
    auto z = next.zone[i];
    if (lo != std::max (0, z.low) || hi != std::max (0, z.high) || ! z.enabled())
    {
        z.low = std::min (lo, hi);
        z.high = std::max (lo, hi);
        if (z.root >= 0 && ! z.contains (z.root))
            z.root = drums::noNote;
        next.zone[i] = z;
        propose (next, pad, Gesture::resizeLow, -1, z.low, "Resize zone", true);
    }
    else if (root != z.rootNote())
    {
        if (drums::setZoneRoot (next, pad, root == z.low ? drums::noNote : root).applied)
            commit (next, "Move root");
        else
            status = "The root must lie inside the zone (" + look::noteName (z.low) + "-" + look::noteName (z.high) + ").";
        refreshFromProcessor();
    }
}

std::vector<int> KeyMapEditor::layoutOrder() const
{
    // The visible pads in screen order, plus a hidden ninth pad that holds a sound.
    std::vector<int> order;
    for (int s = 0; s < laneCount(); ++s)
        order.push_back (proc.padAtSlot (s));
    if (! proc.nineLayout && proc.padInfo (proc.padAtSlot (8)).sound != nullptr)
        order.push_back (proc.padAtSlot (8));
    return order;
}

std::vector<int> KeyMapEditor::collidersFor (const KeyMap& m, int pad) const
{
    if (expanded())
        return drums::collisions (m, pad, m.zone[static_cast<std::size_t> (pad)]);
    std::vector<int> others;
    const int note = m.padNote[static_cast<std::size_t> (pad)];
    if (note >= 0)
        for (int q = 0; q < drums::maxPads; ++q)
            if (q != pad && m.padNote[static_cast<std::size_t> (q)] == note)
                others.push_back (q);
    return others;
}

void KeyMapEditor::commit (const KeyMap& next, const juce::String& action)
{
    auto a = proc.arrangement();
    a.map = next;
    proc.applyEdit (a, action);
    status = action;
}

bool KeyMapEditor::commitDirect (const Pending& p)
{
    // Rebuild the result through the engine's validated edits, never by copying the drawing.
    auto next = proc.keyMap();
    const auto i = static_cast<std::size_t> (p.pad);
    const auto policy = next.allowOverlap ? Collision::overlap : Collision::refuse;
    drums::EditResult r;
    switch (p.gesture)
    {
        case Gesture::move: r = drums::moveZone (next, p.pad, p.newLow, policy); break;
        case Gesture::resizeLow:
        case Gesture::resizeHigh:
        case Gesture::create: r = drums::resizeZone (next, p.pad, p.candidate.zone[i].low, p.candidate.zone[i].high, policy); break;
        case Gesture::root:
        {
            const auto& z = p.candidate.zone[i];
            r = drums::setZoneRoot (next, p.pad, z.root);
            break;
        }
        case Gesture::padNote: r = drums::setPadNote (next, p.pad, p.note, next.allowOverlap); break;
        case Gesture::none: return false;
    }
    if (! r.applied)
    {
        status = juce::String (r.message);
        return false;
    }
    // A resize keeps an explicit root from the candidate (e.g. a learnt or reset zone).
    if ((p.gesture == Gesture::resizeLow || p.gesture == Gesture::resizeHigh || p.gesture == Gesture::create) && p.candidate.zone[i].root >= 0)
        drums::setZoneRoot (next, p.pad, p.candidate.zone[i].root);
    commit (next, p.action);
    return true;
}

void KeyMapEditor::propose (const KeyMap& proposed, int pad, Gesture g, int note, int newLow, const juce::String& action, bool askUser)
{
    Pending p;
    p.candidate = proposed;
    p.pad = pad;
    p.gesture = g;
    p.note = note;
    p.newLow = newLow;
    p.action = action;
    p.colliders = collidersFor (proposed, pad);
    if (p.colliders.empty() || proposed.allowOverlap)
    {
        commitDirect (p);
        pending.reset();
    }
    else
    {
        pending = p;
        juce::StringArray names;
        for (int q : p.colliders)
            names.add (proc.padDisplayName (q).toUpperCase());
        status = "In the way: " + names.joinIntoString (", ") + ". Choose Swap, Move or Cancel.";
        if (askUser)
            showResolutionMenu();
    }
    refreshFromProcessor();
}

bool KeyMapEditor::canSwap() const
{
    return pending && pending->colliders.size() == 1 && (pending->gesture == Gesture::move || pending->gesture == Gesture::padNote);
}

bool KeyMapEditor::resolveSwap()
{
    if (! canSwap())
        return false;
    auto next = proc.keyMap();
    const auto& p = *pending;
    drums::EditResult r;
    if (p.gesture == Gesture::move)
        r = drums::moveZone (next, p.pad, p.newLow, Collision::swap);
    else
    {
        // PAD: the other pad takes this pad's old note.
        const int other = p.colliders.front();
        const int old = next.padNote[static_cast<std::size_t> (p.pad)];
        next.padNote[static_cast<std::size_t> (p.pad)] = p.note;
        next.padNote[static_cast<std::size_t> (other)] = old;
        r.applied = true;
    }
    if (r.applied)
        commit (next, p.action + " (swap)");
    else
        status = juce::String (r.message);
    pending.reset();
    refreshFromProcessor();
    return r.applied;
}

bool KeyMapEditor::resolveMoveOthers()
{
    if (! pending)
        return false;
    const auto p = *pending;
    pending.reset();
    auto next = proc.keyMap();
    const auto i = static_cast<std::size_t> (p.pad);
    bool ok = true;
    if (expanded())
    {
        next.zone[i] = p.candidate.zone[i];
        for (int q : p.colliders)
            ok = ok && drums::moveZoneToFreeSpace (next, q).applied;
        for (int a = 0; ok && a < drums::maxPads; ++a)
            ok = drums::collisions (next, a, next.zone[static_cast<std::size_t> (a)]).empty() || next.allowOverlap;
    }
    else
    {
        next.padNote[i] = p.note;
        for (int q : p.colliders)
        {
            // The nearest note no pad answers.
            int best = -1;
            for (int d = 1; d < 128 && best < 0; ++d)
                for (int cand : { p.note + d, p.note - d })
                {
                    if (cand < 0 || cand > 127 || best >= 0)
                        continue;
                    bool used = false;
                    for (int x = 0; x < drums::maxPads; ++x)
                        used = used || next.padNote[static_cast<std::size_t> (x)] == cand;
                    if (! used)
                        best = cand;
                }
            ok = ok && best >= 0;
            if (best >= 0)
                next.padNote[static_cast<std::size_t> (q)] = best;
        }
    }
    if (ok)
        commit (next, p.action + " (moved the others)");
    else
        status = "There is no free place for the pads in the way; nothing changed.";
    refreshFromProcessor();
    return ok;
}

bool KeyMapEditor::resolveLayer()
{
    if (! pending || ! proc.keyMap().allowOverlap)
        return false;
    const auto p = *pending;
    pending.reset();
    const bool ok = commitDirect (p);
    refreshFromProcessor();
    return ok;
}

void KeyMapEditor::resolveCancel()
{
    pending.reset();
    status = "Cancelled; nothing changed.";
    refreshFromProcessor();
}

void KeyMapEditor::showResolutionMenu()
{
    // Off screen (no window: headless tests) the choice waits in the banner instead.
    if (! pending || getPeer() == nullptr)
        return;
    juce::PopupMenu menu;
    juce::StringArray names;
    for (int q : pending->colliders)
        names.add (proc.padDisplayName (q).toUpperCase());
    menu.addSectionHeader ("In the way: " + names.joinIntoString (", "));
    if (canSwap())
        menu.addItem ("Swap with " + names[0], [this] { resolveSwap(); });
    menu.addItem (pending->colliders.size() == 1 ? "Move " + names[0] + " to a free place" : "Move them to free places", [this] { resolveMoveOthers(); });
    if (proc.keyMap().allowOverlap)
        menu.addItem ("Layer (play both)", [this] { resolveLayer(); });
    menu.addSeparator();
    menu.addItem ("Cancel", [this] { resolveCancel(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                        [safe = juce::Component::SafePointer<KeyMapEditor> (this)] (int result) {
                            if (result == 0 && safe != nullptr && safe->pending)
                                safe->resolveCancel();   // dismissed = cancel, never a silent change
                        });
}

// ---------------------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------------------

KeyMapEditor::Gesture KeyMapEditor::gestureAt (int pad, juce::Point<float> p, int note) const
{
    const auto& m = proc.keyMap();
    const auto i = static_cast<std::size_t> (pad);
    if (! expanded())
        return m.padNote[i] == note ? Gesture::padNote : Gesture::none;
    const auto& z = m.zone[i];
    if (! z.enabled())
        return Gesture::create;
    const float xl = noteToX (z.low), xr = noteToX (z.high + 1);
    const auto lane = laneArea (laneOfPad (pad));
    const float rx = noteToX (z.rootNote() + 0.5);
    if (p.y < lane.getY() + lane.getHeight() * 0.45f && std::abs (p.x - rx) <= 7.0f && p.x > xl + 2.0f && p.x < xr - 2.0f)
        return Gesture::root;
    if (std::abs (p.x - xl) <= edgeGrab)
        return Gesture::resizeLow;
    if (std::abs (p.x - xr) <= edgeGrab)
        return Gesture::resizeHigh;
    if (p.x > xl && p.x < xr)
        return Gesture::move;
    return Gesture::none;
}

void KeyMapEditor::beginGesture (int pad, Gesture g, int note)
{
    cancelPreview();
    pending.reset();
    gesture = g;
    gesturePad = pad;
    grabNote = note;
    gestureNote = note;
    gestureStart = proc.keyMap();
    candidate = gestureStart;
    if (g == Gesture::create)
        gestureTo (note);
    repaint();
}

void KeyMapEditor::gestureTo (int note)
{
    if (gesture == Gesture::none)
        return;
    note = juce::jlimit (0, 127, note);
    gestureNote = note;
    candidate = gestureStart;
    const auto i = static_cast<std::size_t> (gesturePad);
    auto& z = candidate.zone[i];
    const auto& z0 = gestureStart.zone[i];
    switch (gesture)
    {
        case Gesture::move:
        {
            const int width = z0.width();
            const int newLow = juce::jlimit (0, 128 - width, z0.low + (note - grabNote));
            const int shift = newLow - z0.low;
            z = Zone { z0.low + shift, z0.high + shift, z0.root >= 0 ? z0.root + shift : drums::noNote };
            break;
        }
        case Gesture::resizeLow:
            z.low = std::min (note, z0.high);
            if (z.root >= 0 && ! z.contains (z.root))
                z.root = drums::noNote;
            break;
        case Gesture::resizeHigh:
            z.high = std::max (note, z0.low);
            if (z.root >= 0 && ! z.contains (z.root))
                z.root = drums::noNote;
            break;
        case Gesture::root:
            z.root = juce::jlimit (z0.low, z0.high, note);
            if (z.root == z.low)
                z.root = drums::noNote;   // the default: the lowest key
            break;
        case Gesture::padNote: candidate.padNote[i] = note; break;
        case Gesture::create:
        {
            // A new zone of one octave from the key clicked (kept inside MIDI).
            const int lowNote = juce::jlimit (0, 116, note);
            z = Zone { lowNote, lowNote + 11, drums::noNote };
            break;
        }
        case Gesture::none: break;
    }
    repaint();
}

void KeyMapEditor::cancelGesture()
{
    if (gesture == Gesture::none)
        return;
    gesture = Gesture::none;
    status = "Cancelled; nothing changed.";
    repaint();
}

void KeyMapEditor::endGesture (bool askUser)
{
    if (gesture == Gesture::none)
        return;
    const auto g = gesture;
    const int pad = gesturePad;
    gesture = Gesture::none;
    if (candidate == gestureStart)
    {
        repaint();
        return;
    }
    const auto i = static_cast<std::size_t> (pad);
    juce::String action;
    switch (g)
    {
        case Gesture::move: action = "Move zone"; break;
        case Gesture::resizeLow:
        case Gesture::resizeHigh: action = "Resize zone"; break;
        case Gesture::root: action = "Move root"; break;
        case Gesture::padNote: action = "Move note"; break;
        case Gesture::create: action = "New zone"; break;
        case Gesture::none: break;
    }
    propose (candidate, pad, g, candidate.padNote[i], candidate.zone[i].low, action, askUser);
}

void KeyMapEditor::mouseMove (const juce::MouseEvent& e)
{
    juce::MouseCursor shape = juce::MouseCursor::NormalCursor;
    if (timelineArea().contains (e.position))
    {
        const int lane = juce::jlimit (0, laneCount() - 1, static_cast<int> ((e.position.y - timelineArea().getY()) / laneArea (0).getHeight()));
        const int note = static_cast<int> (std::floor (xToNote (e.position.x)));
        switch (gestureAt (padOfLane (lane), e.position, note))
        {
            case Gesture::resizeLow:
            case Gesture::resizeHigh: shape = juce::MouseCursor::LeftRightResizeCursor; break;
            case Gesture::move:
            case Gesture::padNote:
            case Gesture::root: shape = juce::MouseCursor::DraggingHandCursor; break;
            case Gesture::create:
            case Gesture::none: shape = juce::MouseCursor::PointingHandCursor; break;
        }
    }
    else if (keyboardArea().contains (e.position) && ! expanded())
        shape = juce::MouseCursor::PointingHandCursor;
    setMouseCursor (shape);
}

void KeyMapEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    clickOnly = false;
    const int note = juce::jlimit (0, 127, static_cast<int> (std::floor (xToNote (e.position.x))));
    if (timelineArea().contains (e.position))
    {
        const int lane = juce::jlimit (0, laneCount() - 1, static_cast<int> ((e.position.y - timelineArea().getY()) / laneArea (0).getHeight()));
        const int pad = padOfLane (lane);
        if (onSelect)
            onSelect (pad);
        const auto g = gestureAt (pad, e.position, note);
        if (g == Gesture::none)
        {
            // PAD mode: clicking an empty cell assigns that note to this lane's pad.
            if (! expanded())
            {
                clickOnly = true;
                gesturePad = pad;
                gestureNote = note;
            }
            refreshFromProcessor();
            return;
        }
        beginGesture (pad, g, note);
        refreshFromProcessor();
        return;
    }
    if (keyboardArea().contains (e.position) && ! expanded())
    {
        clickOnly = true;
        gesturePad = proc.selectedPad;
        gestureNote = note;
        return;
    }
    // The label column selects too.
    if (e.position.x < timelineArea().getX() && e.position.y >= timelineArea().getY() && e.position.y < timelineArea().getBottom())
    {
        const int lane = juce::jlimit (0, laneCount() - 1, static_cast<int> ((e.position.y - timelineArea().getY()) / laneArea (0).getHeight()));
        if (onSelect)
            onSelect (padOfLane (lane));
        refreshFromProcessor();
    }
}

void KeyMapEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (gesture == Gesture::none)
        return;
    // Edges follow the boundary between keys; everything else the key under the mouse.
    const double n = xToNote (e.position.x);
    int note = static_cast<int> (std::floor (n));
    if (gesture == Gesture::resizeHigh)
        note = static_cast<int> (std::floor (n - 0.5));
    else if (gesture == Gesture::resizeLow)
        note = static_cast<int> (std::floor (n + 0.5));
    gestureTo (note);
}

void KeyMapEditor::mouseUp (const juce::MouseEvent&)
{
    if (clickOnly)
    {
        clickOnly = false;
        auto next = proc.keyMap();
        next.padNote[static_cast<std::size_t> (gesturePad)] = gestureNote;
        propose (next, gesturePad, Gesture::padNote, gestureNote, -1, "Assign note", true);
        return;
    }
    endGesture (true);
}

void KeyMapEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        zoomBy (w.deltaY > 0 ? 1.0 / 1.25 : 1.25, xToNote (e.position.x));
        return;
    }
    const float d = std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY;
    setView (low - static_cast<double> (d) * span * 0.25, span);
}

bool KeyMapEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        if (gesture != Gesture::none)
            cancelGesture();
        else if (pending)
            resolveCancel();
        else if (preview)
            cancelPreview();
        else if (learn != Learn::off)
            cancelLearn();
        else
            return false;
        return true;
    }
    return false;
}

void KeyMapEditor::visibilityChanged()
{
    if (! isVisible())
    {
        cancelLearn();
        cancelGesture();
    }
    else
        refreshFromProcessor();
}

// ---------------------------------------------------------------------------------------
// MIDI Learn and Auto Map
// ---------------------------------------------------------------------------------------

void KeyMapEditor::startLearn()
{
    cancelPreview();
    learn = expanded() ? Learn::rangeLow : Learn::note;
    learnLow = -1;
    learnStarted = juce::Time::getMillisecondCounter();
    proc.setLearnArmed (true);
    status = expanded() ? "LEARN RANGE for " + proc.padDisplayName (proc.selectedPad).toUpperCase() + ": play the LOWEST key."
                        : "LEARN NOTE for " + proc.padDisplayName (proc.selectedPad).toUpperCase() + ": play a key.";
    refreshFromProcessor();
}

void KeyMapEditor::cancelLearn()
{
    if (learn == Learn::off)
        return;
    learn = Learn::off;
    proc.setLearnArmed (false);
    status = "Learn cancelled.";
    refreshFromProcessor();
}

void KeyMapEditor::learnedNote (int note)
{
    if (learn == Learn::off || note < 0 || note > 127)
        return;
    const int pad = proc.selectedPad;
    const auto i = static_cast<std::size_t> (pad);
    if (learn == Learn::note)
    {
        learn = Learn::off;
        proc.setLearnArmed (false);
        auto next = proc.keyMap();
        next.padNote[i] = note;
        status = "Learnt " + look::noteWithNumber (note);
        propose (next, pad, Gesture::padNote, note, -1, "Learn note", true);
        return;
    }
    if (learn == Learn::rangeLow)
    {
        learnLow = note;
        learn = Learn::rangeHigh;
        learnStarted = juce::Time::getMillisecondCounter();
        status = "Lowest " + look::noteWithNumber (note) + ". Now play the HIGHEST key.";
        refreshFromProcessor();
        return;
    }
    learn = Learn::off;
    proc.setLearnArmed (false);
    auto next = proc.keyMap();
    const int lo = std::min (learnLow, note), hi = std::max (learnLow, note);
    next.zone[i] = Zone { lo, hi, drums::noNote };
    status = "Learnt " + look::noteWithNumber (lo) + " to " + look::noteWithNumber (hi);
    propose (next, pad, Gesture::resizeLow, -1, lo, "Learn range", true);
}

bool KeyMapEditor::previewAutoMap (AutoMap preset)
{
    cancelGesture();
    pending.reset();
    auto next = proc.keyMap();
    const auto order = layoutOrder();
    const bool exp = expanded();
    const int selected = proc.selectedPad;
    const auto i = static_cast<std::size_t> (selected);
    juce::String name, note;
    drums::EditResult r;
    switch (preset)
    {
        case AutoMap::standard:
            name = "Standard Drum Kit";
            r = exp ? drums::applyStandardZones (next) : drums::applyStandardPadNotes (next);
            break;
        case AutoMap::chromatic:
            name = "Compact Chromatic";
            r = exp ? drums::applyZoneLayout (next, 1, order, 36) : drums::applyChromaticPadNotes (next, order, 36);
            break;
        case AutoMap::oneOctave:
            name = "One Octave Per Pad";
            r = drums::applyZoneLayout (next, 12, order, 36);
            break;
        case AutoMap::twoOctaves:
        {
            name = "Two Octaves Per Pad";
            int width = 24;
            if (width * static_cast<int> (order.size()) > 128)
            {
                width = drums::widestFittingWidth (static_cast<int> (order.size()));
                note = "24 keys x " + juce::String (order.size()) + " pads exceed MIDI's 128: " + juce::String (width) + " keys each (the most that fits).";
            }
            r = drums::applyZoneLayout (next, width, order, 0);
            break;
        }
        case AutoMap::fromSelectedRoot:
        {
            name = "Fit From Selected Root";
            const int start = exp ? (proc.keyMap().zone[i].enabled() ? proc.keyMap().zone[i].low : 36)
                                  : (proc.keyMap().padNote[i] >= 0 ? proc.keyMap().padNote[i] : 36);
            r = exp ? drums::applyZoneLayout (next, 12, order, start) : drums::applyChromaticPadNotes (next, order, start);
            if (r.applied && exp && next.zone[i].low != start)
                note = "Moved down to " + look::noteWithNumber (next.zone[static_cast<std::size_t> (order.front())].low) + " so every zone fits.";
            break;
        }
    }
    if (! r.applied)
    {
        status = name + ": " + juce::String (r.message);
        refreshFromProcessor();
        return false;
    }
    const auto changed = drums::changedPads (proc.keyMap(), next, exp ? drums::KeyboardMode::expanded : drums::KeyboardMode::pad);
    preview = next;
    previewText = "PREVIEW  " + name + ": " + juce::String (changed.size()) + (changed.size() == 1 ? " pad changes" : " pads change") + " in the "
                  + (exp ? juce::String ("EXPANDED") : juce::String ("PAD")) + " map (the other map is kept)." + (note.isNotEmpty() ? "  " + note : juce::String());
    status = previewText;
    fitAssigned();   // show the whole proposal
    refreshFromProcessor();
    return true;
}

void KeyMapEditor::applyPreview()
{
    if (! preview)
        return;
    auto next = proc.arrangement();
    next.map = *preview;
    preview.reset();
    proc.applyEdit (next, "Auto Map");
    status = "Auto Map applied (undo restores the previous map).";
    refreshFromProcessor();
}

void KeyMapEditor::cancelPreview()
{
    if (! preview)
        return;
    preview.reset();
    status = "Auto Map cancelled.";
    refreshFromProcessor();
}

void KeyMapEditor::timerCallback()
{
    if (learn != Learn::off)
    {
        for (int n : proc.takeLearnedNotes())
            learnedNote (n);
        if (learn != Learn::off && juce::Time::getMillisecondCounter() - learnStarted > learnTimeoutMs)
        {
            cancelLearn();
            status = "Learn timed out (nothing played for 30 s).";
        }
    }
    bool dirty = false;
    for (int n = 0; n < 128; ++n)
    {
        const bool h = proc.isNoteHeld (n);
        dirty = dirty || h != held[static_cast<std::size_t> (n)];
        held[static_cast<std::size_t> (n)] = h;
    }
    if (proc.lastPlayedNote() != lastShownNote)
    {
        lastShownNote = proc.lastPlayedNote();
        dirty = true;
    }
    if (expandedMode.getToggleState() != expanded() || undoButton.isEnabled() != proc.undoManager().canUndo()
        || redoButton.isEnabled() != proc.undoManager().canRedo())
        refreshFromProcessor();
    if (dirty)
        repaint();
}

// ---------------------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------------------

void KeyMapEditor::paint (juce::Graphics& g)
{
    look::panel (g, getLocalBounds().toFloat());
    // Header title
    g.setColour (colour::text);
    g.setFont (look::type::sectionTitle());
    g.drawText ("KEY MAP", juce::Rectangle<int> (16, 0, 100, static_cast<int> (headerHeight)), juce::Justification::centredLeft);

    // Banner: what is going on (learn, preview, collision, last action).
    auto banner = juce::Rectangle<float> (16.0f, headerHeight, static_cast<float> (getWidth()) - 32.0f, bannerHeight).reduced (0.0f, 2.0f);
    const bool attention = learn != Learn::off || preview.has_value() || pending.has_value();
    if (attention)
    {
        g.setColour ((pending ? danger : colour::accent).withAlpha (0.12f));
        g.fillRoundedRectangle (banner, 5.0f);
    }
    g.setColour (pending ? danger : attention ? colour::text : colour::textSecondary);
    g.setFont (type::micro (13.0f));
    auto text = status;
    if (text.isEmpty())
        text = expanded() ? "EXPANDED: drag a zone to move it, its edges to resize, the triangle to set the root. Click an empty lane for a new octave zone."
                          : "PAD: drag a note to move it, or click any cell (or key) to give the selected pad that note.";
    g.drawFittedText (text, banner.reduced (10.0f, 0.0f).withTrimmedRight (preview || learn != Learn::off ? 176.0f : 0.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 1.0f);

    paintLanes (g);
    paintKeyboard (g);
    paintInspectorBackground (g);
}

void KeyMapEditor::paintLanes (juce::Graphics& g)
{
    const auto& m = shownMap();
    const auto t = timelineArea();
    const bool exp = expanded();
    const float pxPerNote = t.getWidth() / static_cast<float> (span);
    const int first = std::max (0, static_cast<int> (std::floor (low)));
    const int last = std::min (127, static_cast<int> (std::ceil (low + span)));

    g.saveState();
    for (int lane = 0; lane < laneCount(); ++lane)
    {
        const auto r = laneArea (lane);
        const int pad = padOfLane (lane);
        const auto i = static_cast<std::size_t> (pad);
        const bool selected = proc.selectedPad == pad;
        const auto accent = look::category (proc.padInfo (pad).category);

        // Label column
        auto label = juce::Rectangle<float> (t.getX() - labelWidth, r.getY(), labelWidth - 6.0f, r.getHeight()).reduced (6.0f, 2.0f);
        if (selected)
        {
            g.setColour (colour::accent.withAlpha (0.12f));
            g.fillRoundedRectangle (label, 4.0f);
        }
        g.setColour (accent);
        g.fillRoundedRectangle (label.removeFromLeft (3.0f).reduced (0.0f, 3.0f), 1.5f);
        label.removeFromLeft (6.0f);
        look::badge (g, label.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, std::min (18.0f, r.getHeight() - 6.0f)), pad + 1,
                     selected ? colour::accent : juce::Colour (0xffe2dbcf), selected ? juce::Colours::white : colour::text);
        label.removeFromLeft (6.0f);
        g.setColour (colour::text);
        g.setFont (plugin::fonts::make (12.5f, look::Weight::semibold, 0.05f));
        g.drawFittedText (proc.padDisplayName (pad).toUpperCase(), label.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);

        // Lane background and semitone / octave grid
        g.setColour (lane % 2 == 0 ? laneA : laneB);
        g.fillRect (r);
        g.reduceClipRegion (t.toNearestInt());
        for (int n = first; n <= last; ++n)
        {
            const float x = noteToX (n);
            if (look::isBlackKey (n) && pxPerNote >= 5.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.035f));
                g.fillRect (juce::Rectangle<float> (x, r.getY(), pxPerNote, r.getHeight()));
            }
            if (n % 12 == 0)
            {
                g.setColour (colour::divider.withAlpha (0.9f));
                g.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
            }
            else if (pxPerNote >= 7.0f)
            {
                g.setColour (colour::hairline.withAlpha (0.6f));
                g.fillRect (juce::Rectangle<float> (x, r.getY(), 1.0f, r.getHeight()));
            }
        }

        const bool active = gesture != Gesture::none && gesturePad == pad;
        const bool pendingHere = pending && pending->pad == pad;
        std::vector<int> colliders;
        if (active || pendingHere)
            colliders = collidersFor (m, pad);
        const bool collides = ! colliders.empty() && ! m.allowOverlap;
        const auto fill = collides ? danger : accent;

        if (exp)
        {
            const auto& z = m.zone[i];
            if (z.enabled())
            {
                const auto block = juce::Rectangle<float> (noteToX (z.low), r.getY() + 4.0f, noteToX (z.high + 1) - noteToX (z.low), r.getHeight() - 8.0f);
                g.setColour (fill.withAlpha (selected || active ? 0.85f : 0.62f));
                g.fillRoundedRectangle (block, 4.0f);
                g.setColour (fill.darker (0.35f));
                g.drawRoundedRectangle (block.reduced (0.5f), 4.0f, active || selected ? 1.6f : 1.0f);
                // Edge grips
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                for (float ex : { block.getX() + 2.5f, block.getRight() - 4.0f })
                    g.fillRoundedRectangle (juce::Rectangle<float> (ex, block.getCentreY() - 6.0f, 1.5f, 12.0f), 0.75f);
                // Root marker: a triangle on top of the root key and a thin line.
                const float rx = noteToX (z.rootNote() + 0.5);
                juce::Path tri;
                tri.addTriangle (rx - 5.0f, block.getY() + 1.0f, rx + 5.0f, block.getY() + 1.0f, rx, block.getY() + 8.0f);
                g.setColour (juce::Colours::white);
                g.fillPath (tri);
                g.setColour (juce::Colours::white.withAlpha (0.55f));
                g.fillRect (juce::Rectangle<float> (rx - 0.5f, block.getY() + 8.0f, 1.0f, block.getHeight() - 10.0f));
                // Range text inside the block where it fits.
                const auto range = look::noteName (z.low) + "-" + look::noteName (z.high) + "   root " + look::noteName (z.rootNote());
                g.setColour (juce::Colours::white);
                g.setFont (type::micro (12.0f));
                if (block.getWidth() > 120.0f)
                    g.drawFittedText (range, block.reduced (12.0f, 0.0f).withTrimmedTop (6.0f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
                // Played keys inside this zone: a dot at the key (what the pad hears now).
                for (int n = z.low; n <= z.high; ++n)
                    if (held[static_cast<std::size_t> (n)])
                    {
                        g.setColour (juce::Colours::white);
                        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ noteToX (n + 0.5), block.getBottom() - 6.0f }));
                    }
                if (active || pendingHere)
                {
                    // The new place, as names and MIDI numbers, above the block.
                    const auto info = look::noteWithNumber (z.low) + " - " + look::noteWithNumber (z.high) + "   root " + look::noteName (z.rootNote())
                                      + (collides ? "   overlaps " + proc.padDisplayName (colliders.front()).toUpperCase() : juce::String());
                    g.setFont (type::micro (12.0f));
                    const float w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (type::micro (12.0f), info)) + 16.0f;
                    auto tag = juce::Rectangle<float> (block.getX(), block.getY() - 2.0f, w, 18.0f).withY (r.getY() + 1.0f);
                    tag.setX (juce::jlimit (t.getX(), t.getRight() - w, block.getX()));
                    g.setColour (collides ? danger : colour::text);
                    g.fillRoundedRectangle (tag, 3.0f);
                    g.setColour (juce::Colours::white);
                    g.drawText (info, tag, juce::Justification::centred);
                }
            }
            else
            {
                g.setColour (colour::textMicro);
                g.setFont (type::micro (12.0f));
                g.drawText ("no zone: click to add one", r.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
            }
        }
        else
        {
            const int note = m.padNote[i];
            if (note >= 0)
            {
                // A one-key marker: a round-ended pill, unlike a zone's block.
                const float cw = std::max (8.0f, pxPerNote);
                const auto marker = juce::Rectangle<float> (noteToX (note) + (pxPerNote - cw) * 0.5f, r.getY() + 5.0f, cw, r.getHeight() - 10.0f);
                g.setColour (fill.withAlpha (selected || active ? 0.95f : 0.8f));
                g.fillRoundedRectangle (marker, std::min (cw, marker.getHeight()) * 0.5f);
                if (held[static_cast<std::size_t> (note)])
                {
                    g.setColour (juce::Colours::white);
                    g.drawRoundedRectangle (marker.expanded (1.5f), std::min (cw, marker.getHeight()) * 0.5f, 1.5f);
                }
                g.setColour (colour::textSecondary);
                g.setFont (type::micro (12.0f));
                auto info = look::noteWithNumber (note);
                if (collides)
                {
                    g.setColour (danger);
                    info << "   also " << proc.padDisplayName (colliders.front()).toUpperCase();
                }
                g.drawText (info, juce::Rectangle<float> (marker.getRight() + 6.0f, r.getY(), 220.0f, r.getHeight()), juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (colour::textMicro);
                g.setFont (type::micro (12.0f));
                g.drawText ("no note: click a cell to assign one", r.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
            }
        }
        g.restoreState();
        g.saveState();
    }
    g.restoreState();

    // Overlaps (only when allowed): a visible hatch where keys play several pads.
    if (exp && m.allowOverlap)
        for (int n = first; n <= last; ++n)
        {
            std::array<drums::Hit, drums::maxPads> hits {};
            if (drums::resolve (m, drums::KeyboardMode::expanded, n, hits) > 1)
            {
                g.setColour (danger.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float> (noteToX (n), t.getY(), pxPerNote, t.getHeight()));
            }
        }
}

void KeyMapEditor::paintKeyboard (juce::Graphics& g)
{
    // Equal-width semitone columns on the lanes' mapping, so a key sits exactly under its
    // cells at every zoom (a piano roll's keyboard).
    const auto k = keyboardArea();
    const float px = k.getWidth() / static_cast<float> (span);
    const int first = std::max (0, static_cast<int> (std::floor (low)));
    const int last = std::min (127, static_cast<int> (std::ceil (low + span)));
    g.saveState();
    g.reduceClipRegion (k.toNearestInt());
    g.setColour (colour::whiteKeyBottom);
    g.fillRect (k);
    const auto& m = shownMap();
    for (int n = first; n <= last; ++n)
    {
        const float x = noteToX (n);
        const auto col = juce::Rectangle<float> (x, k.getY(), px, k.getHeight());
        const bool black = look::isBlackKey (n);
        // Which pad the key belongs to (colour wash), in the shown map.
        std::array<drums::Hit, drums::maxPads> hits {};
        const int count = drums::resolve (m, expanded() ? drums::KeyboardMode::expanded : drums::KeyboardMode::pad, n, hits);
        if (count > 0)
        {
            g.setColour (look::category (proc.padInfo (hits[0].pad).category).withAlpha (0.28f));
            g.fillRect (col.withTrimmedTop (k.getHeight() * 0.62f));
        }
        if (black)
        {
            g.setGradientFill (juce::ColourGradient (colour::blackKeyTop, x, k.getY(), colour::blackKeyBottom, x, k.getY() + k.getHeight() * 0.6f, false));
            g.fillRect (col.withHeight (k.getHeight() * 0.6f).reduced (std::min (1.0f, px * 0.1f), 0.0f));
        }
        if (held[static_cast<std::size_t> (n)])
        {
            g.setColour (colour::accent.withAlpha (0.85f));
            g.fillRect (black ? col.withHeight (k.getHeight() * 0.6f) : col.withTrimmedTop (k.getHeight() * 0.6f));
        }
        // Key boundaries: full height between two white keys, the lower part elsewhere.
        const bool whiteBoundary = n % 12 == 0 || n % 12 == 5;
        g.setColour (colour::keyLine);
        g.fillRect (juce::Rectangle<float> (x, whiteBoundary ? k.getY() : k.getY() + k.getHeight() * 0.6f, 1.0f,
                                            whiteBoundary ? k.getHeight() : k.getHeight() * 0.4f));
        if (n % 12 == 0)
        {
            g.setColour (colour::textSecondary);
            g.setFont (type::micro (11.5f));
            g.drawText (look::noteName (n), juce::Rectangle<float> (x + 2.0f, k.getBottom() - 15.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
        }
    }
    g.restoreState();
    g.setColour (colour::divider);
    g.drawRect (k, 1.0f);
}

void KeyMapEditor::paintInspectorBackground (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().withTrimmedTop (headerHeight + bannerHeight).removeFromRight (inspectorWidth).reduced (4.0f, 4.0f);
    g.setColour (juce::Colour (0xfff7f2ea));
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour::hairline);
    g.drawRoundedRectangle (r, 8.0f, 1.0f);
    const int pad = proc.selectedPad;
    const auto i = static_cast<std::size_t> (pad);
    auto inner = r.reduced (10.0f, 8.0f);
    look::badge (g, inner.removeFromTop (22.0f).removeFromLeft (22.0f), pad + 1, colour::accent, juce::Colours::white);
    g.setColour (colour::text);
    g.setFont (plugin::fonts::make (15.0f, look::Weight::semibold, 0.04f));
    g.drawFittedText (proc.padDisplayName (pad).toUpperCase(), juce::Rectangle<float> (r.getX() + 40.0f, r.getY() + 8.0f, r.getWidth() - 50.0f, 22.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 1.0f);
    g.setColour (colour::textMicro);
    g.setFont (type::micro (12.0f));
    g.drawText (expanded() ? "EXPANDED zone" : "PAD note", juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 32.0f, r.getWidth() - 20.0f, 18.0f),
                juce::Justification::centredLeft);

    // Range size and the transposition of the last key played.
    const auto& m = shownMap();
    juce::String line1, line2;
    if (expanded())
    {
        const auto& z = m.zone[i];
        if (z.enabled())
        {
            line1 = juce::String (z.width()) + (z.width() == 1 ? " key" : " keys") + ", root " + look::noteWithNumber (z.rootNote());
            const int n = proc.lastPlayedNote();
            line2 = z.contains (n) ? look::noteWithNumber (n) + " plays " + (n - z.rootNote() >= 0 ? "+" : "") + juce::String (n - z.rootNote()) + " st"
                                   : "Play a key in the zone to see its pitch";
        }
        else
            line1 = "No zone: this pad is silent in EXPANDED.";
    }
    else
    {
        line1 = m.padNote[i] >= 0 ? "Plays at its natural pitch on " + look::noteName (m.padNote[i]) : "No note: silent in PAD mode.";
        line2 = "TUNE (sample editor) still applies";
    }
    const float y = static_cast<float> (expanded() ? rootBox.getBottom() : noteBox.getBottom()) + 8.0f;
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (12.5f));
    g.drawFittedText (line1, juce::Rectangle<float> (r.getX() + 12.0f, y, r.getWidth() - 24.0f, 18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
    g.drawFittedText (line2, juce::Rectangle<float> (r.getX() + 12.0f, y + 18.0f, r.getWidth() - 24.0f, 18.0f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
}

} // namespace osp::drumsplugin
