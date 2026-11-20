#include "CustomPatternEditor.h"

#include "AndorParameters.h"
#include "CustomPatternBrowser.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "engine/PatternCapture.h"
#include "model/ArpFactoryPatterns.h"

#include <algorithm>
#include <cmath>

namespace osp::plugin
{

namespace
{
    namespace ap = arppattern;
    using Action = CustomPatternEditor::Action;

    juce::File& libraryOverride()
    {
        static juce::File folder;
        return folder;
    }

    /** The take strip's state, as the musician last left it (a UI preference, never musical). */
    bool& sourcePreference()
    {
        static bool open = false;
        return open;
    }

    juce::String customId (InstrumentHost& host, int card) { return ids::arpCustom (host.cardArpNamespace(), card); }

    const juce::Colour wellText = design::colour::wellText;
    const juce::Colour noteColour = design::colour::arpOnDark;
    const juce::Colour recordRed { 0xffc8492c };
    /** " · " (a non-ASCII literal must be decoded as UTF-8). */
    const juce::String sep = juce::String::fromUTF8 (" \xc2\xb7 ");

    constexpr int maxStepsShown = 32;    ///< longer patterns scroll rather than shrink
    constexpr int freshSteps = 8;        ///< a new pattern: eight empty steps to draw on
    constexpr int snapTicks = arp::ticksPerStep / 4;

    juce::String stepsText (int steps) { return juce::String (steps) + (steps == 1 ? " step" : " steps"); }

    juce::String timingName (const ap::Interpretation& i)
    {
        return i.timing == ap::Interpretation::Timing::steps ? "Even Steps" : "Recorded Rhythm";
    }

    juce::String gridName (int quantum)
    {
        if (quantum == arp::ticksPerStep / 2)
            return "1/2";
        if (quantum == arp::ticksPerStep / 8)
            return "1/8";
        if (quantum == arp::ticksPerStep / 4)
            return "1/4";
        return juce::String (quantum) + " t";
    }

    void drawChevron (juce::Graphics& g, float cx, float cy, juce::Colour ink)
    {
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (ink);
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    /** Undo / redo: a curved arrow. */
    void drawUndoIcon (juce::Graphics& g, juce::Rectangle<float> r, bool redo, juce::Colour ink)
    {
        const auto c = r.getCentre();
        juce::Path arc;
        const float rad = 5.0f;
        if (! redo)
        {
            arc.addCentredArc (c.x + 1.0f, c.y + 1.5f, rad, rad, 0.0f, -juce::MathConstants<float>::halfPi * 1.0f, juce::MathConstants<float>::halfPi * 1.2f, true);
            g.setColour (ink);
            g.strokePath (arc, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.addTriangle (c.x - 6.5f, c.y - 3.5f, c.x - 1.5f, c.y - 6.5f, c.x - 1.5f, c.y - 0.5f);
            g.fillPath (head);
        }
        else
        {
            arc.addCentredArc (c.x - 1.0f, c.y + 1.5f, rad, rad, 0.0f, juce::MathConstants<float>::halfPi * 1.0f, -juce::MathConstants<float>::halfPi * 1.2f, true);
            g.setColour (ink);
            g.strokePath (arc, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            juce::Path head;
            head.addTriangle (c.x + 6.5f, c.y - 3.5f, c.x + 1.5f, c.y - 6.5f, c.x + 1.5f, c.y - 0.5f);
            g.fillPath (head);
        }
    }
}

//==============================================================================
CustomPatternEditor::CustomPatternEditor (InstrumentHost& h, int card, bool fresh)
    : MiniPanel ("CUSTOM PATTERN", "CARD " + InstrumentHost::shownLayerName (juce::jlimit (0, 2, card))), host (h), cardIndex (juce::jlimit (0, 2, card))
{
    setWantsKeyboardFocus (true);
    if (const auto* d = host.customArpDocument (cardIndex))
        before = *d;
    if (before.has_value() && ! fresh)
        doc = *before;
    else
    {
        doc.id = newArpPatternId();
        doc.name = "New pattern";
        doc.interpretation.stepQuarters = stepQuarters();
        // Eight empty steps to draw on (double-click a step for a note): one rest.
        doc.pattern.events.push_back ({ 0, true, freshSteps * arp::ticksPerStep, 0 });
    }
    opened = doc;
    customBefore = host.parameterValue (customId (host, cardIndex));
    history.reset (doc);
    selection = doc.pattern.events.empty() ? -1 : 0;
    sourceOpen = sourcePreference() && ! doc.capture.empty();

    name = std::make_unique<juce::TextEditor>();
    styleTextField (*name);
    name->setText (juce::String (doc.name), false);
    name->setTooltip ("The pattern's name");
    name->onTextChange = [this] {
        doc.name = name->getText().trim().toStdString();
        // A name is not an undo step: every state of this pattern carries the new name.
        const auto id = doc.id, now = doc.name;
        history.updateAll ([&id, &now] (ap::Document& d) {
            if (d.id == id)
                d.name = now;
        });
    };
    name->onReturnKey = [this] { grabKeyboardFocus(); };
    addAndMakeVisible (*name);

    keys = {
        { Action::help, "?" }, { Action::close, "x" },
        { Action::record, "Record" }, { Action::import, "Import" }, { Action::overflow, "..." },
        { Action::toggleSource, "" },
        { Action::timingMenu, "" }, { Action::gridMenu, "" }, { Action::mappingMenu, "" },
        { Action::shorter, "-" }, { Action::longer, "+" },
        { Action::split, "Split" }, { Action::rest, "Make Rest" }, { Action::insertStep, "Add Step" }, { Action::transform, "Transform" },
        { Action::merge, "Merge" }, { Action::extend, "Extend" },
        { Action::undo, "" }, { Action::redo, "" },
        { Action::preview, "Preview" }, { Action::cancel, "Cancel" }, { Action::use, "Apply Pattern" },
    };
    status = doc.pattern.notes() == 0 ? "Double-click a step to place a note, or Record / Import a phrase" : "Click a note to select it";
    startTimerHz (30);
}

CustomPatternEditor::~CustomPatternEditor()
{
    stopTimer();
    // Closed without APPLY PATTERN (CANCEL, Escape, a click outside, the host): the card as it was.
    if (auto* c = host.patternCapture(); c != nullptr && recordingTake)
        c->cancel();
    if (! used)
        restoreCard();
}

juce::Point<int> CustomPatternEditor::cardSize() const { return { width, height + (sourceOpen ? sourceViewHeight : 0) }; }

bool CustomPatternEditor::recording() const noexcept { return recordingTake; }

double CustomPatternEditor::stepQuarters() const
{
    const auto rate = juce::roundToInt (host.parameterValue (ids::arpCardSetting (host.cardArpNamespace(), cardIndex, "rate")));
    return arp::rateQuarters (static_cast<ArpRate> (std::clamp (rate, 0, arp::rateCount - 1)));
}

int CustomPatternEditor::stepsOf (const ap::Pattern& p) const
{
    return (p.lengthTicks() + arp::ticksPerStep - 1) / arp::ticksPerStep;
}

//==============================================================================
// Editing

void CustomPatternEditor::commit (const ap::Pattern& next, const juce::String& what)
{
    doc.pattern = next;
    commitDocument (what);
}

void CustomPatternEditor::commitDocument (const juce::String& what)
{
    history.push (doc);
    if (selection >= static_cast<int> (doc.pattern.events.size()))
        selection = static_cast<int> (doc.pattern.events.size()) - 1;
    if (selection < 0 && ! doc.pattern.events.empty())
        selection = 0;
    if (what.isNotEmpty())
        status = what;
    if (previewOn)
        publishPreview();
    revealSelection();
    repaint();
}

void CustomPatternEditor::restore (const ap::Document& state)
{
    // Undo / redo: the whole document as it was (the take, TIMING and MAPPING with the pattern).
    doc = state;
    if (name->getText().trim() != juce::String (doc.name))
        name->setText (juce::String (doc.name), false);
    if (selection >= static_cast<int> (doc.pattern.events.size()))
        selection = static_cast<int> (doc.pattern.events.size()) - 1;
    if (previewOn)
        publishPreview();
    revealSelection();
    repaint();
}

void CustomPatternEditor::select (int index)
{
    selection = doc.pattern.events.empty() ? -1 : juce::jlimit (0, static_cast<int> (doc.pattern.events.size()) - 1, index);
    revealSelection();
    repaint();
}

void CustomPatternEditor::setSelectedRank (int rank)
{
    auto next = doc.pattern;
    if (selection >= 0 && ap::edit::setRank (next, selection, juce::jlimit (0, ap::maxRankLimit, rank)))
        commit (next, "Rank " + juce::String (rank + 1));
}

bool CustomPatternEditor::hasManualEdits() const
{
    if (doc.capture.empty())
        return false;
    auto how = doc.interpretation;
    how.mapping = doc.pattern.mapping;
    auto rebuilt = ap::interpret (doc.capture, how);
    rebuilt.mapping = doc.pattern.mapping;
    return ! (rebuilt == doc.pattern);
}

bool CustomPatternEditor::hasUnsavedChanges() const
{
    if (recordingTake)
        if (auto* c = host.patternCapture(); c != nullptr && ! c->notes().empty())
            return true;   // (a take in progress)
    if (doc == opened)
        return false;
    return ! (saved.has_value() && doc == *saved);
}

bool CustomPatternEditor::canPerform (Action a) const
{
    const auto& p = doc.pattern;
    const bool has = selection >= 0 && selection < static_cast<int> (p.events.size());
    auto tryEdit = [&] (auto fn) {
        auto copy = p;
        return fn (copy);
    };
    switch (a)
    {
        case Action::record: return host.patternCapture() != nullptr;
        case Action::import: return ! recordingTake;
        case Action::split: return has && tryEdit ([this] (ap::Pattern& c) { return ap::edit::split (c, selection); });
        case Action::merge: return has && tryEdit ([this] (ap::Pattern& c) { return ap::edit::merge (c, selection); });
        case Action::extend: return has && tryEdit ([this] (ap::Pattern& c) {
            return selection + 1 < static_cast<int> (c.events.size()) && ap::edit::extend (c, selection, c.events[static_cast<std::size_t> (selection + 1)].duration);
        });
        // Make Rest: a note becomes a rest, its time kept. (A rest's time goes only by
        // deleteStep, labelled as shortening the pattern.)
        case Action::rest:
        case Action::remove: return has && ! p.events[static_cast<std::size_t> (selection)].rest;
        case Action::insertStep: return tryEdit ([this] (ap::Pattern& c) { return ap::edit::insertStep (c, std::max (0, selection + 1)); });
        case Action::deleteStep: return has && p.events.size() > 1;
        case Action::reverse:
        case Action::mirror:
        case Action::rotate:
        case Action::transform: return p.events.size() > 1;
        case Action::shorter: return tryEdit ([this] (ap::Pattern& c) { return ap::edit::setLength (c, (stepsOf (c) - 1) * arp::ticksPerStep); });
        case Action::longer: return ! p.events.empty() && tryEdit ([this] (ap::Pattern& c) { return ap::edit::setLength (c, (stepsOf (c) + 1) * arp::ticksPerStep); });
        case Action::undo: return history.canUndo();
        case Action::redo: return history.canRedo();
        case Action::use:
        case Action::preview:
        case Action::save:
        case Action::saveCopy:
        case Action::duplicate: return p.valid();
        case Action::rename:
        case Action::deleteFromLibrary: return ! doc.factory && ! doc.id.empty() && libraryFileOf (doc) != juce::File();
        case Action::clear: return ! p.events.empty() || ! doc.capture.empty();
        case Action::timingSteps:
        case Action::timingHalf:
        case Action::timingQuarter:
        case Action::timingEighth:
        case Action::timingMenu: return ! doc.capture.empty() && ! recordingTake;
        case Action::gridMenu: return ! doc.capture.empty() && ! recordingTake && doc.interpretation.timing == ap::Interpretation::Timing::rhythm;
        case Action::toggleSource: return ! doc.capture.empty() || recordingTake;
        default: return true;
    }
}

void CustomPatternEditor::perform (Action a)
{
    if (! canPerform (a))
        return;
    auto next = doc.pattern;
    // Recording or importing over work asks first (Undo brings it back as well).
    auto replacingWork = [this] { return doc.capture.empty() ? doc.pattern.notes() > 0 : hasManualEdits(); };
    switch (a)
    {
        case Action::record:
            if (recordingTake)
            {
                finishTake();
                return;
            }
            if (replacingWork())
                showDialog ("Record a new take?", "The pattern you have will be replaced. You can undo this.", "Cancel", "Record", false, [this] { startTake(); });
            else
                startTake();
            return;
        case Action::import:
            if (replacingWork())
                showDialog ("Import a MIDI phrase?", "The pattern you have will be replaced. You can undo this.", "Cancel", "Import", false, [this] { openImportChooser(); });
            else
                openImportChooser();
            return;
        case Action::preview:
            previewOn = ! previewOn;
            if (previewOn)
                publishPreview();
            else
                restoreCard();
            status = previewOn ? "Preview: hold a chord to hear the pattern on card " + InstrumentHost::shownLayerName (cardIndex) : "Preview off (the card as it was)";
            break;
        case Action::timingSteps: chooseTiming (ap::Interpretation::Timing::steps, doc.interpretation.quantumTicks); return;
        case Action::timingHalf: chooseTiming (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 2); return;
        case Action::timingQuarter: chooseTiming (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 4); return;
        case Action::timingEighth: chooseTiming (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 8); return;
        case Action::contour:
        case Action::degrees:
        {
            const auto mapping = a == Action::contour ? arp::CustomMapping::contour : arp::CustomMapping::degrees;
            if (doc.pattern.mapping == mapping && doc.interpretation.mapping == mapping)
                return;
            ap::edit::setMapping (doc.pattern, mapping);
            doc.interpretation.mapping = mapping;
            commitDocument (a == Action::contour ? "Contour: the gesture's shape, climbing an octave per cycle where there is room"
                                                 : "Degrees: rank 1 plays the lowest held note, rank 2 the next ...");
            return;
        }
        case Action::split: ap::edit::split (next, selection); commit (next, "Split"); return;
        case Action::merge: ap::edit::merge (next, selection); commit (next, "Merged with the next"); return;
        case Action::extend:
            ap::edit::extend (next, selection, next.events[static_cast<std::size_t> (selection + 1)].duration);
            commit (next, "Extended into the rest");
            return;
        case Action::rest:
        case Action::remove: ap::edit::toRest (next, selection); commit (next, "Rest (its time kept)"); return;
        case Action::insertStep:
            ap::edit::insertStep (next, std::max (0, selection + 1));
            selection = std::max (0, selection + 1);
            commit (next, "A step added (the pattern is longer: " + stepsText (stepsOf (next)) + ")");
            return;
        case Action::deleteStep:
            ap::edit::deleteEvent (next, selection);
            commit (next, "Removed with its time (the pattern is shorter: " + stepsText (stepsOf (next)) + ")");
            return;
        case Action::reverse: ap::edit::reverse (next); commit (next, "Reversed"); return;
        case Action::mirror: ap::edit::mirror (next); commit (next, "Mirrored"); return;
        case Action::rotate: ap::edit::rotate (next, 1); commit (next, "Rotated"); return;
        case Action::shorter: ap::edit::setLength (next, (stepsOf (next) - 1) * arp::ticksPerStep); commit (next, "Length " + stepsText (stepsOf (next))); return;
        case Action::longer: ap::edit::setLength (next, (stepsOf (next) + 1) * arp::ticksPerStep); commit (next, "Length " + stepsText (stepsOf (next))); return;
        case Action::undo:
            restore (history.undo());
            status = "Undo";
            return;
        case Action::redo:
            restore (history.redo());
            status = "Redo";
            return;
        case Action::clear:
            doc.capture.clear();
            doc.pattern = {};
            selection = -1;
            commitDocument ("Cleared (Undo brings it back)");
            return;
        case Action::cancel:
        case Action::close: requestClose(); return;
        case Action::use:
        {
            used = true;
            doc.name = name->getText().trim().isEmpty() ? std::string ("Custom pattern") : name->getText().trim().toStdString();
            host.setCustomArpDocument (cardIndex, &doc);
            if (auto* p = host.apvts().getParameter (customId (host, cardIndex)); p != nullptr && p->getValue() < 0.5f)
            {
                host.undo().beginNewTransaction ("Custom pattern");
                p->beginChangeGesture();
                p->setValueNotifyingHost (1.0f);
                p->endChangeGesture();
            }
            CustomPatternBrowser::noteUsed (doc);
            if (onClose != nullptr)
                onClose();
            return;
        }
        case Action::save: saveToLibrary(); return;
        case Action::saveCopy:
            showDialog ("Save a copy to your library", "A new pattern of your own; this one stays as it is.", "Cancel", "Save Copy", false,
                        [this] { saveCopy (dialogField() != nullptr ? dialogField()->getText().trim() : juce::String(), false); },
                        name->getText().trim() + " copy");
            return;
        case Action::duplicate:
            showDialog ("Duplicate", "Saves a copy in your library and goes on editing the copy.", "Cancel", "Duplicate", false,
                        [this] { saveCopy (dialogField() != nullptr ? dialogField()->getText().trim() : juce::String(), true); },
                        name->getText().trim() + " copy");
            return;
        case Action::rename:
            showDialog ("Rename in your library", "Sessions and presets that use this pattern keep their own copy.", "Cancel", "Rename", false,
                        [this] { renameInLibrary (dialogField() != nullptr ? dialogField()->getText().trim() : juce::String()); }, name->getText().trim());
            return;
        case Action::deleteFromLibrary:
            showDialog ("Delete \"" + juce::String (doc.name) + "\" from your library?", "Sessions and presets that use it keep their own copy.", "Cancel",
                        "Delete", true, [this] { deleteFromLibrary(); });
            return;
        case Action::library:
        case Action::browse: openBrowseMenu(); return;
        case Action::showFolder:
            libraryFolder().createDirectory();
            libraryFolder().startAsProcess();
            return;
        case Action::transform: openTransformMenu(); return;
        case Action::overflow: openOverflowMenu(); return;
        case Action::timingMenu: openTimingMenu(); return;
        case Action::gridMenu: openGridMenu(); return;
        case Action::mappingMenu: openMappingMenu(); return;
        case Action::toggleSource: setSourceOpen (! sourceOpen); return;
        case Action::help:
            helpOpen = ! helpOpen;
            repaint();
            return;
    }
    repaint();
}

void CustomPatternEditor::chooseTiming (ap::Interpretation::Timing timing, int quantum)
{
    if (doc.capture.empty())
        return;
    auto how = doc.interpretation;
    how.timing = timing;
    how.quantumTicks = quantum;
    if (how == doc.interpretation)
        return;   // (the same reading: nothing to rebuild)
    if (hasManualEdits())
        showDialog ("Rebuild pattern from original take?", "Your manual edits will be replaced. You can undo this action.", "Cancel", "Rebuild", false,
                    [this, how] { reinterpret (how); });
    else
        reinterpret (how);
}

void CustomPatternEditor::reinterpret (const ap::Interpretation& how)
{
    if (doc.capture.empty())
        return;
    doc.interpretation = how;
    auto read = how;
    read.mapping = doc.pattern.mapping;
    ap::InterpretReport report;
    auto next = ap::interpret (doc.capture, read, &report);
    next.mapping = doc.pattern.mapping;
    doc.pattern = next;
    selection = next.events.empty() ? -1 : 0;
    viewStep = 0;
    commitDocument ({});   // one undo step: the reading and the pattern together
    describe (report);
}

void CustomPatternEditor::describe (const ap::InterpretReport& r)
{
    juce::String s;
    s << r.placedNotes << " of " << r.capturedNotes << " notes";
    if (r.chordNotesDropped > 0)
        s << sep << r.chordNotesDropped << " chord note" << (r.chordNotesDropped == 1 ? "" : "s") << " left out (the highest kept)";
    if (r.beyondLength > 0)
        s << sep << r.beyondLength << " after the loop";
    if (r.beyondCapacity > 0)
        s << sep << r.beyondCapacity << " past the 128-step limit (not placed)";
    status = s;
}

void CustomPatternEditor::useTake (const std::vector<ap::CapturedNote>& notes, double bpm)
{
    if (notes.empty())
    {
        status = "Nothing was recorded";
        repaint();
        return;
    }
    doc.capture = notes;
    doc.captureBpm = bpm;
    auto how = doc.interpretation;
    how.stepQuarters = stepQuarters();
    how.lengthQuarters = 0.0;
    reinterpret (how);
}

void CustomPatternEditor::startTake()
{
    auto* c = host.patternCapture();
    if (c == nullptr || recordingTake)
        return;
    c->start (host.hostSampleRate() > 0.0 ? host.hostSampleRate() : 48000.0);
    recordingTake = true;
    setSourceOpen (true);
    status = "Recording: play the phrase (the arpeggiator steps aside), then Stop";
    repaint();
}

void CustomPatternEditor::finishTake()
{
    auto* c = host.patternCapture();
    if (c == nullptr || ! recordingTake)
        return;
    c->stop();
    recordingTake = false;
    const bool full = c->full();
    useTake (c->notes(), c->bpm());
    if (full)
        status = "The take reached its limit (1024 notes) and stopped there" + sep + status;
    repaint();
}

void CustomPatternEditor::importPhrases (const std::vector<MidiPhrase>& phrases, int pick)
{
    if (phrases.empty())
    {
        status = "No notes in that MIDI file";
        repaint();
        return;
    }
    auto apply = [this, phrases] (int index) {
        if (index < 0 || index >= static_cast<int> (phrases.size()))
            return;
        const auto& p = phrases[static_cast<std::size_t> (index)];
        useTake (p.notes, p.bpm);
        status = juce::String (p.name) + sep + status;
        repaint();
    };
    if (pick >= 0 || phrases.size() == 1)
    {
        apply (std::max (0, pick));
        return;
    }
    // Several tracks or channels: the musician picks the phrase.
    juce::PopupMenu menu;
    menu.addSectionHeader ("WHICH PART?");
    for (int i = 0; i < static_cast<int> (phrases.size()); ++i)
        menu.addItem (i + 1, juce::String (phrases[static_cast<std::size_t> (i)].name) + "  (" + juce::String (static_cast<int> (phrases[static_cast<std::size_t> (i)].notes.size())) + " notes)");
    showMenu (Action::import, menu, [apply] (int result) {
        if (result > 0)
            apply (result - 1);
    });
}

void CustomPatternEditor::openImportChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a MIDI phrase", juce::File(), "*.mid;*.midi");
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (file == juce::File())
            return;
        // Parsed off the message thread, then back to the editor (if it is still open).
        juce::Thread::launch ([safe, path = file.getFullPathName().toStdString()] {
            std::string error;
            auto phrases = readMidiPhrases (path, error);
            juce::MessageManager::callAsync ([safe, phrases = std::move (phrases), error] {
                if (safe == nullptr)
                    return;
                if (phrases.empty())
                {
                    safe->status = "Could not import: " + juce::String (error);
                    safe->repaint();
                    return;
                }
                safe->importPhrases (phrases);
            });
        });
    });
}

void CustomPatternEditor::loadDocument (const ap::Document& d)
{
    doc = d;
    name->setText (juce::String (doc.name), false);
    selection = doc.pattern.events.empty() ? -1 : 0;
    viewStep = 0;
    commitDocument (doc.factory ? "Factory pattern: " + juce::String (doc.name) + " (Save Copy keeps a version of your own)" : "Loaded: " + juce::String (doc.name));
}

//==============================================================================
// Direct manipulation

int CustomPatternEditor::snapTick (int tick) const
{
    const auto mods = juce::ModifierKeys::getCurrentModifiers();
    const int q = mods.isAltDown() ? arp::minEventTicks : snapTicks;
    return juce::roundToInt (static_cast<double> (tick) / q) * q;
}

bool CustomPatternEditor::dragEdge (int index, bool end, int tick)
{
    auto next = doc.pattern;
    if (index < 0 || index >= static_cast<int> (next.events.size()))
        return false;
    const int onset = next.onsetOf (index);
    const int finish = onset + next.events[static_cast<std::size_t> (index)].duration;
    const bool ok = end ? ap::edit::resizeEnd (next, index, tick - onset) : ap::edit::resizeStart (next, index, finish - tick);
    if (! ok)
    {
        status = "Not there: a note's edge stops at the next note, and every event keeps at least a 1/32 step";
        repaint();
        return false;
    }
    if (! end && next.events.size() < doc.pattern.events.size())
        --index;   // (the rest before it was used up)
    else if (! end && next.events.size() > doc.pattern.events.size())
        ++index;   // (a rest opened before it)
    selection = index;
    commit (next, "Length of the note: " + juce::String (static_cast<double> (next.events[static_cast<std::size_t> (index)].duration) / arp::ticksPerStep, 2) + " steps");
    return true;
}

bool CustomPatternEditor::addNoteAt (int tick, int rank)
{
    auto& p = doc.pattern;
    for (int i = 0; i < static_cast<int> (p.events.size()); ++i)
    {
        const int onset = p.onsetOf (i), duration = p.events[static_cast<std::size_t> (i)].duration;
        if (tick < onset || tick >= onset + duration)
            continue;
        if (! p.events[static_cast<std::size_t> (i)].rest)
        {
            select (i);
            return false;   // a note is there already: it is selected, never overwritten
        }
        // One step at the step clicked, inside the rest; slivers too short to stand alone join it.
        int start = std::max (onset, (tick / arp::ticksPerStep) * arp::ticksPerStep);
        if (start - onset > 0 && start - onset < arp::minEventTicks)
            start = onset;
        int length = std::min (arp::ticksPerStep, onset + duration - start);
        if (onset + duration - (start + length) > 0 && onset + duration - (start + length) < arp::minEventTicks)
            length = onset + duration - start;
        auto next = p;
        if (! ap::edit::placeNote (next, i, start - onset, length, juce::jlimit (0, ap::maxRankLimit, rank)))
        {
            status = "No room for a note there (the pattern holds at most 256 events)";
            repaint();
            return false;
        }
        selection = start > onset ? i + 1 : i;
        commit (next, "A note on rank " + juce::String (rank + 1));
        return true;
    }
    return false;   // outside the pattern: nothing is made there
}

namespace
{
    struct ContextItem
    {
        juce::String label;
        std::function<void (CustomPatternEditor&)> run;
        bool separatorBefore = false;
    };

    std::vector<ContextItem> contextItemsFor (const CustomPatternEditor& e, int index)
    {
        std::vector<ContextItem> items;
        const auto& p = e.document().pattern;
        if (index < 0 || index >= static_cast<int> (p.events.size()))
            return items;
        const bool rest = p.events[static_cast<std::size_t> (index)].rest;
        auto can = [&e] (Action a) { return e.canPerform (a); };
        if (rest)
            items.push_back ({ "Make Note", [] (CustomPatternEditor& ed) { ed.setSelectedRank (0); } });
        if (can (Action::split))
            items.push_back ({ "Split", [] (CustomPatternEditor& ed) { ed.perform (Action::split); } });
        if (can (Action::merge))
            items.push_back ({ "Merge with Next", [] (CustomPatternEditor& ed) { ed.perform (Action::merge); } });
        if (can (Action::extend))
            items.push_back ({ "Extend into Rest", [] (CustomPatternEditor& ed) { ed.perform (Action::extend); } });
        if (! rest)
            items.push_back ({ "Make Rest (keep its time)", [] (CustomPatternEditor& ed) { ed.perform (Action::rest); } });
        if (can (Action::insertStep))
            items.push_back ({ "Insert Step After (pattern gets longer)", [] (CustomPatternEditor& ed) { ed.perform (Action::insertStep); }, true });
        if (can (Action::deleteStep))
            items.push_back ({ rest ? "Delete Rest and Its Time (pattern gets shorter)" : "Delete Note and Its Time (pattern gets shorter)",
                               [] (CustomPatternEditor& ed) { ed.perform (Action::deleteStep); }, ! can (Action::insertStep) });
        return items;
    }
}

juce::StringArray CustomPatternEditor::contextActions (int index) const
{
    auto* self = const_cast<CustomPatternEditor*> (this);
    const int was = selection;
    self->selection = index;   // (the menu acts on the event it was opened on)
    juce::StringArray out;
    for (const auto& item : contextItemsFor (*this, index))
        out.add (item.label);
    self->selection = was;
    return out;
}

bool CustomPatternEditor::performContext (int index, const juce::String& item)
{
    select (index);
    for (const auto& c : contextItemsFor (*this, index))
        if (c.label == item)
        {
            c.run (*this);
            return true;
        }
    return false;
}

void CustomPatternEditor::openContextMenu (int index)
{
    select (index);
    const auto items = contextItemsFor (*this, index);
    if (items.empty())
        return;
    juce::PopupMenu menu;
    menu.addSectionHeader (doc.pattern.events[static_cast<std::size_t> (index)].rest ? "REST" : "NOTE" + sep + "RANK " + juce::String (doc.pattern.events[static_cast<std::size_t> (index)].rank + 1));
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
    {
        if (items[static_cast<std::size_t> (i)].separatorBefore)
            menu.addSeparator();
        menu.addItem (i + 1, items[static_cast<std::size_t> (i)].label);
    }
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    const auto area = localAreaToGlobal (eventArea (index).getSmallestIntegerContainer());
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (area), [safe, index, items] (int result) {
        if (safe == nullptr || result <= 0 || result > static_cast<int> (items.size()))
            return;
        safe->select (index);
        items[static_cast<std::size_t> (result - 1)].run (*safe);
    });
}

//==============================================================================
// Menus

void CustomPatternEditor::showMenu (Action anchor, juce::PopupMenu& menu, std::function<void (int)> chosen)
{
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (keyArea (anchor))).withMinimumWidth (keyArea (anchor).getWidth()),
                        [safe, chosen] (int result) {
                            if (safe != nullptr && chosen != nullptr)
                                chosen (result);
                        });
}

void CustomPatternEditor::openTransformMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Reverse", canPerform (Action::reverse));
    menu.addItem (2, "Mirror (high becomes low)", canPerform (Action::mirror));
    menu.addItem (3, "Rotate (start one event later)", canPerform (Action::rotate));
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::transform, menu, [safe] (int r) {
        if (safe == nullptr)
            return;
        if (r == 1)
            safe->perform (Action::reverse);
        else if (r == 2)
            safe->perform (Action::mirror);
        else if (r == 3)
            safe->perform (Action::rotate);
    });
}

void CustomPatternEditor::openTimingMenu()
{
    juce::PopupMenu menu;
    const bool steps = doc.interpretation.timing == ap::Interpretation::Timing::steps;
    menu.addItem (1, "Even Steps (one step per note, the order only)", true, steps);
    menu.addItem (2, "Recorded Rhythm (the take's timing, on a grid)", true, ! steps);
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::timingMenu, menu, [safe] (int r) {
        if (safe == nullptr)
            return;
        if (r == 1)
            safe->perform (Action::timingSteps);
        else if (r == 2)
            safe->chooseTiming (ap::Interpretation::Timing::rhythm, safe->doc.interpretation.quantumTicks);
    });
}

void CustomPatternEditor::openGridMenu()
{
    juce::PopupMenu menu;
    const int q = doc.interpretation.quantumTicks;
    menu.addItem (1, "1/2 step", true, q == arp::ticksPerStep / 2);
    menu.addItem (2, "1/4 step", true, q == arp::ticksPerStep / 4);
    menu.addItem (3, "1/8 step", true, q == arp::ticksPerStep / 8);
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::gridMenu, menu, [safe] (int r) {
        if (safe == nullptr || r <= 0)
            return;
        safe->perform (r == 1 ? Action::timingHalf : (r == 2 ? Action::timingQuarter : Action::timingEighth));
    });
}

void CustomPatternEditor::openMappingMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "Contour (the gesture's shape, climbing an octave per cycle)", true, doc.pattern.mapping == arp::CustomMapping::contour);
    menu.addItem (2, "Degrees (rank 1 = the lowest held note, rank 2 the next ...)", true, doc.pattern.mapping == arp::CustomMapping::degrees);
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::mappingMenu, menu, [safe] (int r) {
        if (safe != nullptr && r > 0)
            safe->perform (r == 1 ? Action::contour : Action::degrees);
    });
}

juce::StringArray CustomPatternEditor::overflowItems() const
{
    juce::StringArray items;
    if (canPerform (Action::save))
        items.add ("Save to Library");
    if (canPerform (Action::saveCopy))
        items.add ("Save Copy...");
    if (canPerform (Action::rename))
        items.add ("Rename...");
    if (canPerform (Action::duplicate))
        items.add ("Duplicate...");
    items.add ("Browse Patterns...");
    items.add ("Show Library Folder");
    if (canPerform (Action::clear))
        items.add ("Clear Pattern");
    if (canPerform (Action::deleteFromLibrary))
        items.add ("Delete from Library...");
    return items;
}

void CustomPatternEditor::openOverflowMenu()
{
    const auto items = overflowItems();
    juce::PopupMenu menu;
    menu.addSectionHeader ("PATTERN");
    for (int i = 0; i < items.size(); ++i)
    {
        if (items[i] == "Clear Pattern" || (items[i] == "Delete from Library..." && ! items.contains ("Clear Pattern")))
            menu.addSeparator();
        menu.addItem (i + 1, items[i]);
    }
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::overflow, menu, [safe, items] (int r) {
        if (safe == nullptr || r <= 0 || r > items.size())
            return;
        const auto& item = items[r - 1];
        const Action a = item == "Save to Library" ? Action::save
                       : item == "Save Copy..." ? Action::saveCopy
                       : item == "Rename..." ? Action::rename
                       : item == "Duplicate..." ? Action::duplicate
                       : item == "Browse Patterns..." ? Action::browse
                       : item == "Show Library Folder" ? Action::showFolder
                       : item == "Clear Pattern" ? Action::clear
                                                 : Action::deleteFromLibrary;
        safe->perform (a);
    });
}

void CustomPatternEditor::openBrowseMenu()
{
    juce::PopupMenu menu, factory, yours;
    std::vector<ap::Document> entries;
    for (const auto& d : ap::factoryPatterns())
    {
        entries.push_back (d);
        factory.addItem (static_cast<int> (entries.size()), juce::String (d.name));
    }
    juce::Array<juce::File> files = libraryFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + arpPatternExtension);
    files.sort();
    for (const auto& f : files)
    {
        std::string error;
        if (auto d = readArpPattern (f.getFullPathName().toStdString(), error))
        {
            entries.push_back (*d);
            yours.addItem (static_cast<int> (entries.size()), f.getFileNameWithoutExtension());
        }
    }
    menu.addSectionHeader ("OPEN IN THE EDITOR");
    menu.addSubMenu ("Your Patterns", yours, yours.getNumItems() > 0);
    menu.addSubMenu ("Factory Patterns", factory);
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showMenu (Action::overflow, menu, [safe, entries] (int r) {
        if (safe != nullptr && r > 0 && r <= static_cast<int> (entries.size()))
            safe->loadDocument (entries[static_cast<std::size_t> (r - 1)]);
    });
}

//==============================================================================
// The library

void CustomPatternEditor::setLibraryFolderForTests (const juce::File& folder) { libraryOverride() = folder; }

juce::File CustomPatternEditor::libraryFolder()
{
    if (libraryOverride() != juce::File())
        return libraryOverride();
    // Product-neutral: ANDOR OSP and every ANDOR product share one pattern library.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR").getChildFile ("Patterns");
}

juce::File CustomPatternEditor::libraryFileOf (const ap::Document& d) const
{
    if (d.id.empty())
        return {};
    for (const auto& f : libraryFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + arpPatternExtension))
    {
        std::string error;
        if (const auto read = readArpPattern (f.getFullPathName().toStdString(), error); read && read->id == d.id)
            return f;
    }
    return {};
}

juce::File CustomPatternEditor::freeLibraryFile (const juce::String& wanted) const
{
    const auto folder = libraryFolder();
    const auto base = juce::File::createLegalFileName (wanted.isEmpty() ? "Pattern" : wanted);
    auto f = folder.getChildFile (base + arpPatternExtension);
    for (int i = 2; f.exists(); ++i)
        f = folder.getChildFile (base + " " + juce::String (i) + arpPatternExtension);
    return f;
}

void CustomPatternEditor::saveToLibrary()
{
    libraryFolder().createDirectory();
    const auto wanted = name->getText().trim();
    if (doc.factory)
    {
        // A factory pattern is never overwritten: saving keeps a copy of your own (a new identity,
        // for every state of this editing session).
        const auto oldId = doc.id, newId = newArpPatternId();
        history.updateAll ([&oldId, &newId] (ap::Document& d) {
            if (d.id == oldId)
            {
                d.id = newId;
                d.factory = false;
            }
        });
        doc.factory = false;
        doc.id = newId;
    }
    doc.name = wanted.isEmpty() ? std::string ("Pattern") : wanted.toStdString();
    auto file = libraryFileOf (doc);
    if (file == juce::File())
        file = freeLibraryFile (wanted);
    std::string error;
    if (writeArpPattern (file.getFullPathName().toStdString(), doc, error))
    {
        saved = doc;
        status = "Saved in your library: " + file.getFileNameWithoutExtension() + " (Apply Pattern uses it here)";
    }
    else
        status = "Could not save: " + juce::String (error);
    repaint();
}

void CustomPatternEditor::saveCopy (const juce::String& wanted, bool switchToCopy)
{
    libraryFolder().createDirectory();
    auto copy = doc;
    copy.factory = false;
    copy.id = newArpPatternId();
    copy.name = (wanted.isEmpty() ? juce::String (doc.name) + " copy" : wanted).toStdString();
    const auto file = freeLibraryFile (juce::String (copy.name));
    std::string error;
    if (! writeArpPattern (file.getFullPathName().toStdString(), copy, error))
    {
        status = "Could not save the copy: " + juce::String (error);
        repaint();
        return;
    }
    if (switchToCopy)
    {
        const auto oldId = doc.id;
        history.updateAll ([&oldId, &copy] (ap::Document& d) {
            if (d.id == oldId)
            {
                d.id = copy.id;
                d.name = copy.name;
                d.factory = false;
            }
        });
        doc.id = copy.id;
        doc.name = copy.name;
        doc.factory = false;
        name->setText (juce::String (doc.name), false);
        saved = doc;
        status = "Duplicated: now editing " + file.getFileNameWithoutExtension();
    }
    else
        status = "A copy is in your library: " + file.getFileNameWithoutExtension();
    repaint();
}

void CustomPatternEditor::renameInLibrary (const juce::String& wanted)
{
    const auto file = libraryFileOf (doc);
    if (file == juce::File() || wanted.isEmpty())
    {
        status = file == juce::File() ? juce::String ("Not in your library yet: Save to Library first") : juce::String ("A name, please");
        repaint();
        return;
    }
    doc.name = wanted.toStdString();
    name->setText (wanted, true);
    const auto target = file.getFileNameWithoutExtension() == juce::File::createLegalFileName (wanted) ? file : freeLibraryFile (wanted);
    std::string error;
    const bool ok = writeArpPattern (target.getFullPathName().toStdString(), doc, error) && (target == file || file.deleteFile());
    if (ok)
        saved = doc;
    status = ok ? "Renamed: " + target.getFileNameWithoutExtension() : "Could not rename: " + juce::String (error);
    repaint();
}

void CustomPatternEditor::deleteFromLibrary()
{
    // Deleting a library entry never touches a session or preset: each keeps its own copy.
    const auto file = libraryFileOf (doc);
    if (file == juce::File())
        status = "Not in your library";
    else if (file.deleteFile())
    {
        saved.reset();
        status = "Deleted from your library (sessions and presets keep their copy)";
    }
    else
        status = "Could not delete " + file.getFileName();
    repaint();
}

//==============================================================================
// The card

void CustomPatternEditor::publishPreview()
{
    const auto compiled = ap::compile (doc.pattern);
    if (! compiled.valid())
        return;
    host.setCustomArpPattern (cardIndex, &compiled);
    cardChanged = true;
    if (auto* p = host.apvts().getParameter (customId (host, cardIndex)); p != nullptr && p->getValue() < 0.5f)
        p->setValueNotifyingHost (1.0f);   // (a preview: not an undo step)
}

void CustomPatternEditor::restoreCard()
{
    if (! cardChanged)
        return;
    cardChanged = false;
    if (before.has_value())
    {
        const auto compiled = ap::compile (before->pattern);
        host.setCustomArpPattern (cardIndex, &compiled);
    }
    else
        host.setCustomArpPattern (cardIndex, nullptr);
    if (auto* p = host.apvts().getParameter (customId (host, cardIndex)))
        p->setValueNotifyingHost (customBefore >= 0.5f ? 1.0f : 0.0f);
}

//==============================================================================
// Closing

bool CustomPatternEditor::dismissRequested()
{
    // What is open inside the panel closes first; then changes neither applied nor saved ask.
    if (dialog != nullptr)
    {
        closeDialog();
        return false;
    }
    if (helpOpen)
    {
        helpOpen = false;
        repaint();
        return false;
    }
    if (used || discardConfirmed || ! hasUnsavedChanges())
        return true;
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    showDialog ("Discard changes to this pattern?", "Your edits are neither applied to the card nor saved to your library.", "Keep Editing", "Discard", true, [safe] {
        if (safe == nullptr)
            return;
        safe->discardConfirmed = true;
        if (safe->onClose != nullptr)
            safe->onClose();
    });
    return false;
}

void CustomPatternEditor::requestClose()
{
    if (dismissRequested() && onClose != nullptr)
        onClose();
}

//==============================================================================
// The dialog

void CustomPatternEditor::showDialog (const juce::String& heading, const juce::String& text, const juce::String& cancel, const juce::String& confirm, bool destructive,
                                      std::function<void()> onConfirm, const juce::String& fieldText)
{
    dialog = std::make_unique<Dialog>();
    dialog->title = heading;
    dialog->text = text;
    dialog->cancelLabel = cancel;
    dialog->confirmLabel = confirm;
    dialog->destructive = destructive;
    dialog->onConfirm = std::move (onConfirm);
    if (fieldText != juce::String::charToString (0))
    {
        dialog->field = std::make_unique<juce::TextEditor>();
        styleTextField (*dialog->field);
        dialog->field->setText (fieldText, false);
        dialog->field->selectAll();
        dialog->field->onReturnKey = [this] { answerDialog (true); };
        dialog->field->onEscapeKey = [this] { answerDialog (false); };
        addAndMakeVisible (*dialog->field);
    }
    name->setEnabled (false);
    layoutDialog();
    if (dialog->field != nullptr && isShowing())
        dialog->field->grabKeyboardFocus();
    else if (isShowing())
        grabKeyboardFocus();
    repaint();
}

void CustomPatternEditor::closeDialog()
{
    if (dialog == nullptr)
        return;
    if (dialog->field != nullptr)
        removeChildComponent (dialog->field.get());
    dialog.reset();
    name->setEnabled (true);
    repaint();
}

void CustomPatternEditor::answerDialog (bool confirm)
{
    if (dialog == nullptr)
        return;
    auto run = confirm ? std::move (dialog->onConfirm) : std::function<void()>();
    // (the confirmation reads the field before the dialog goes)
    if (run != nullptr)
    {
        juce::Component::SafePointer<CustomPatternEditor> safe (this);
        run();
        if (safe == nullptr)
            return;
    }
    closeDialog();
}

void CustomPatternEditor::layoutDialog()
{
    if (dialog == nullptr)
        return;
    const auto c = card();
    const int h = dialog->field != nullptr ? 150 : 118;
    dialog->box = juce::Rectangle<int> (380, h).withCentre (c.getCentre().translated (0, -10));
    auto inner = dialog->box.reduced (20, 16);
    auto buttons = inner.removeFromBottom (28);
    dialog->confirmArea = buttons.removeFromRight (std::max (96, 24 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (type::popupLabel (12.0f), dialog->confirmLabel))));
    buttons.removeFromRight (8);
    dialog->cancelArea = buttons.removeFromRight (std::max (96, 24 + juce::roundToInt (juce::GlyphArrangement::getStringWidth (type::popupLabel (12.0f), dialog->cancelLabel))));
    if (dialog->field != nullptr)
    {
        inner.removeFromBottom (12);
        dialog->field->setBounds (inner.removeFromBottom (26));
    }
}

//==============================================================================
// Time

void CustomPatternEditor::timerCallback()
{
    if (! recordingTake)
        return;
    auto* c = host.patternCapture();
    if (c == nullptr)
        return;
    if (c->poll())
    {
        status = "Recording: " + juce::String (static_cast<int> (c->notes().size())) + " notes (Stop when done)";
        repaint (sourceStrip.getUnion (recorded).expanded (2));
        repaint (statusArea);
    }
    if (c->full())
        finishTake();
}

void CustomPatternEditor::refreshContent()
{
    juce::String key;
    key << static_cast<int> (recordingTake) << static_cast<int> (previewOn) << static_cast<int> (history.canUndo()) << static_cast<int> (history.canRedo());
    if (key != drawnFor)
    {
        drawnFor = key;
        repaint();
    }
}

void CustomPatternEditor::settleNow() { repaint(); }

void CustomPatternEditor::setSourceOpen (bool open)
{
    sourcePreference() = open;
    if (open == sourceOpen)
        return;
    sourceOpen = open;
    if (onSizeChanged != nullptr)
        onSizeChanged();   // the host places the taller (or shorter) card
    else if (getHeight() > 0)
        setSize (getWidth(), cardSize().y + 2 * shadowMargin);
    resized();
    repaint();
}

//==============================================================================
// Layout

void CustomPatternEditor::layoutContent (juce::Rectangle<int> area)
{
    auto place = [this] (Action a, juce::Rectangle<int> r) {
        for (auto& k : keys)
            if (k.action == a)
                k.area = r;
    };
    // The title row: help and close on the right.
    {
        const auto c = card();
        const auto closeBox = juce::Rectangle<int> (c.getRight() - 12 - 22, c.getY() + 6, 22, 22);
        place (Action::close, closeBox);
        place (Action::help, closeBox.translated (-26, 0));
    }

    area = area.reduced (4, 0);
    nameRow = area.removeFromTop (28);
    area.removeFromTop (8);
    sourceStrip = area.removeFromTop (22);
    if (sourceOpen)
    {
        area.removeFromTop (4);
        recorded = area.removeFromTop (sourceViewHeight - 4);
    }
    else
        recorded = {};
    area.removeFromTop (8);
    interpRow = area.removeFromTop (24);
    area.removeFromTop (6);
    footerRow = area.removeFromBottom (30);
    area.removeFromBottom (8);
    toolRow = area.removeFromBottom (26);
    area.removeFromBottom (8);
    grid = area;

    // The name, then RECORD, IMPORT and ··· on the right.
    auto row = nameRow;
    auto takeRight = [&row] (int w, int gap = 6) {
        auto r = row.removeFromRight (w);
        row.removeFromRight (gap);
        return r;
    };
    auto takeLeft = [&row] (int w, int gap = 6) {
        auto r = row.removeFromLeft (w);
        row.removeFromLeft (gap);
        return r;
    };
    place (Action::overflow, takeRight (34));
    place (Action::import, takeRight (78));
    place (Action::record, takeRight (86));
    name->setBounds (row.withWidth (std::min (row.getWidth() - 20, 320)));

    place (Action::toggleSource, sourceStrip);

    // TIMING [Even Steps ▾]  GRID [1/4 ▾]  MAPPING [Contour ▾]  ...  LENGTH [-] 16 steps [+]
    row = interpRow;
    timingCaption = takeLeft (46, 2);
    place (Action::timingMenu, takeLeft (128, 14));
    gridCaption = takeLeft (32, 2);
    place (Action::gridMenu, takeLeft (56, 18));
    mappingCaption = takeLeft (56, 2);
    place (Action::mappingMenu, takeLeft (96, 0));
    place (Action::longer, takeRight (24, 0));
    lengthText = takeRight (70, 0);
    place (Action::shorter, takeRight (24, 6));
    lengthCaption = takeRight (46, 0);

    // Split, Make Rest, Add Step, Transform ▾ | Merge, Extend (when they apply) ... Undo Redo
    row = toolRow;
    place (Action::split, takeLeft (58));
    place (Action::rest, takeLeft (84));
    place (Action::insertStep, takeLeft (78));
    place (Action::transform, takeLeft (98, 18));
    place (Action::merge, takeLeft (66));
    place (Action::extend, takeLeft (70));
    place (Action::redo, takeRight (32, 0));
    place (Action::undo, takeRight (32, 4));

    // Footer: Preview on the left; Cancel and Apply Pattern on the right; the message between.
    row = footerRow;
    place (Action::preview, takeLeft (100, 14));
    place (Action::use, takeRight (130, 8));
    place (Action::cancel, takeRight (80, 14));
    statusArea = row;

    layoutDialog();
}

juce::Rectangle<int> CustomPatternEditor::keyArea (Action a) const
{
    for (const auto& k : keys)
        if (k.action == a)
            return k.area;
    return {};
}

bool CustomPatternEditor::keyShown (Action a) const
{
    switch (a)
    {
        case Action::gridMenu: return doc.interpretation.timing == ap::Interpretation::Timing::rhythm;
        case Action::merge:
        case Action::extend: return canPerform (a);   // contextual: only when they apply
        default: return true;
    }
}

bool CustomPatternEditor::keyEnabled (Action a) const
{
    switch (a)
    {
        case Action::cancel:
        case Action::close:
        case Action::help:
        case Action::overflow:
        case Action::mappingMenu: return true;
        case Action::record: return canPerform (a);
        default: return ! recordingTake && canPerform (a);
    }
}

bool CustomPatternEditor::keyLit (Action a) const
{
    switch (a)
    {
        case Action::record: return recordingTake;
        case Action::preview: return previewOn;
        case Action::help: return helpOpen;
        default: return false;
    }
}

//==============================================================================
// The grid's geometry

int CustomPatternEditor::stepsShown() const
{
    return std::min (std::max (1, stepsOf (doc.pattern)), maxStepsShown);
}

int CustomPatternEditor::viewTicks() const
{
    const int steps = stepsOf (doc.pattern);
    if (steps <= maxStepsShown)
        return std::max (1, doc.pattern.lengthTicks());
    return maxStepsShown * arp::ticksPerStep;
}

juce::Rectangle<float> CustomPatternEditor::plotArea() const
{
    auto r = grid.toFloat().reduced (12.0f, 10.0f).withTrimmedLeft (16.0f).withTrimmedTop (10.0f);
    if (stepsOf (doc.pattern) > maxStepsShown)
        r = r.withTrimmedBottom (8.0f);   // (room for the scroll track)
    return r;
}

float CustomPatternEditor::xOfTick (int tick) const
{
    const auto plot = plotArea();
    const int start = stepsOf (doc.pattern) > maxStepsShown ? viewStep * arp::ticksPerStep : 0;
    return plot.getX() + plot.getWidth() * static_cast<float> (tick - start) / static_cast<float> (viewTicks());
}

int CustomPatternEditor::tickAt (float x) const
{
    const auto plot = plotArea();
    const int start = stepsOf (doc.pattern) > maxStepsShown ? viewStep * arp::ticksPerStep : 0;
    return start + juce::roundToInt ((x - plot.getX()) / std::max (1.0f, plot.getWidth()) * static_cast<float> (viewTicks()));
}

int CustomPatternEditor::rowsShown() const
{
    const auto& p = doc.pattern;
    return std::max (std::max (p.maxRank, p.highestRank()) + 1, 4);
}

int CustomPatternEditor::rankAt (float y) const
{
    const auto plot = plotArea();
    const float rowH = plot.getHeight() / static_cast<float> (rowsShown());
    return juce::jlimit (0, ap::maxRankLimit, static_cast<int> (std::floor ((plot.getBottom() - y) / rowH)));
}

juce::Rectangle<float> CustomPatternEditor::eventArea (int index) const
{
    const auto& p = doc.pattern;
    if (index < 0 || index >= static_cast<int> (p.events.size()))
        return {};
    const auto plot = plotArea();
    const float rowH = plot.getHeight() / static_cast<float> (rowsShown());
    const auto& e = p.events[static_cast<std::size_t> (index)];
    float x0 = xOfTick (p.onsetOf (index)), x1 = xOfTick (p.onsetOf (index) + e.duration);
    if (x1 <= plot.getX() || x0 >= plot.getRight())
        return {};
    x0 = std::max (x0, plot.getX());
    x1 = std::min (x1, plot.getRight());
    const float w = std::max (3.0f, x1 - x0 - 2.0f);
    if (e.rest)
        return { x0 + 1.0f, plot.getY(), w, plot.getHeight() };
    const float h = std::min (std::max (4.0f, rowH - 4.0f), 20.0f);
    const float cy = plot.getBottom() - (static_cast<float> (e.rank) + 0.5f) * rowH;
    return { x0 + 1.0f, cy - 0.5f * h, w, h };
}

int CustomPatternEditor::eventAt (juce::Point<float> pt) const
{
    for (int i = 0; i < static_cast<int> (doc.pattern.events.size()); ++i)
    {
        const auto box = eventArea (i);
        if (! box.isEmpty() && pt.x >= box.getX() - 1.0f && pt.x <= box.getRight() + 1.0f)
            return i;
    }
    return -1;
}

void CustomPatternEditor::setViewStep (int step)
{
    const int steps = stepsOf (doc.pattern);
    const int next = steps > maxStepsShown ? juce::jlimit (0, steps - maxStepsShown, step) : 0;
    if (next != viewStep)
    {
        viewStep = next;
        repaint (grid);
    }
}

void CustomPatternEditor::revealSelection()
{
    const int steps = stepsOf (doc.pattern);
    if (steps <= maxStepsShown)
    {
        viewStep = 0;
        return;
    }
    if (selection < 0 || selection >= static_cast<int> (doc.pattern.events.size()))
    {
        setViewStep (viewStep);
        return;
    }
    const int first = doc.pattern.onsetOf (selection) / arp::ticksPerStep;
    if (first < viewStep)
        setViewStep (first);
    else if (first >= viewStep + maxStepsShown)
        setViewStep (first - maxStepsShown + 1);
}

juce::Rectangle<float> CustomPatternEditor::scrollTrack() const
{
    if (stepsOf (doc.pattern) <= maxStepsShown)
        return {};
    const auto plot = plotArea();
    return { plot.getX(), plot.getBottom() + 3.0f, plot.getWidth(), 4.0f };
}

//==============================================================================
// Mouse and keys

void CustomPatternEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    if (dialog != nullptr)
    {
        if (dialog->confirmArea.contains (pos))
            answerDialog (true);
        else if (dialog->cancelArea.contains (pos))
            answerDialog (false);
        return;   // (the panel waits for the answer)
    }
    if (helpOpen && ! keyArea (Action::help).contains (pos))
    {
        helpOpen = false;
        repaint();
        return;
    }
    for (const auto& k : keys)
        if (keyShown (k.action) && k.area.contains (pos))
        {
            if (keyEnabled (k.action))
                perform (k.action);
            return;
        }
    if (const auto track = scrollTrack(); ! track.isEmpty() && track.expanded (0.0f, 4.0f).contains (e.position))
    {
        const float share = (e.position.x - track.getX()) / track.getWidth();
        setViewStep (juce::roundToInt (share * static_cast<float> (stepsOf (doc.pattern))) - maxStepsShown / 2);
        return;
    }
    if (! grid.contains (pos) || recordingTake)
        return;
    grabKeyboardFocus();
    const int i = eventAt (e.position);
    if (i < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        openContextMenu (i);
        return;
    }
    select (i);
    dragIndex = i;
    dragBase = doc.pattern;
    dragged = false;
    drag = Drag::rank;
    const auto box = eventArea (i);
    if (! doc.pattern.events[static_cast<std::size_t> (i)].rest && e.position.y >= box.getY() - 3.0f && e.position.y <= box.getBottom() + 3.0f)
    {
        if (std::abs (e.position.x - box.getRight()) <= 5.0f)
            drag = Drag::resizeEnd;
        else if (std::abs (e.position.x - box.getX()) <= 5.0f && i > 0)
            drag = Drag::resizeStart;
    }
    dragRank = rankAt (e.position.y);
}

void CustomPatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragIndex < 0 || dragIndex >= static_cast<int> (dragBase.events.size()) || drag == Drag::none)
        return;
    if (! dragged && e.getDistanceFromDragStart() < 3)
        return;
    if (drag == Drag::rank)
    {
        // The note follows the pointer (one undo step on release); a rest dragged becomes a note.
        const int rank = rankAt (e.position.y);
        auto next = dragBase;
        ap::edit::setRank (next, dragIndex, rank);
        if (! (next == doc.pattern))
        {
            doc.pattern = next;
            dragRank = rank;
            dragged = true;
            repaint (grid);
        }
        return;
    }
    // An edge: the event's length on the grid, the pattern's length unchanged.
    const int onset = dragBase.onsetOf (dragIndex);
    const int finish = onset + dragBase.events[static_cast<std::size_t> (dragIndex)].duration;
    const int tick = snapTick (tickAt (e.position.x));
    auto next = dragBase;
    const bool ok = drag == Drag::resizeEnd ? ap::edit::resizeEnd (next, dragIndex, tick - onset) : ap::edit::resizeStart (next, dragIndex, finish - tick);
    if (ok && ! (next == doc.pattern))
    {
        doc.pattern = next;
        dragged = true;
        repaint (grid);
    }
}

void CustomPatternEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragIndex >= 0 && dragged && ! (doc.pattern == dragBase))
    {
        const auto next = doc.pattern;
        int index = dragIndex;
        if (drag == Drag::resizeStart)
            index += static_cast<int> (next.events.size()) - static_cast<int> (dragBase.events.size());
        doc.pattern = dragBase;
        selection = juce::jlimit (0, static_cast<int> (next.events.size()) - 1, index);
        commit (next, drag == Drag::rank ? "Rank " + juce::String (dragRank + 1)
                                         : "Length of the note: " + juce::String (static_cast<double> (next.events[static_cast<std::size_t> (selection)].duration) / arp::ticksPerStep, 2) + " steps");
    }
    dragIndex = -1;
    dragged = false;
    drag = Drag::none;
}

void CustomPatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (dialog != nullptr || recordingTake || ! grid.contains (e.getPosition()))
        return;
    // A rest double-clicked: a note there (one step at most, on the rank under the pointer).
    const int i = eventAt (e.position);
    if (i >= 0 && doc.pattern.events[static_cast<std::size_t> (i)].rest)
        addNoteAt (std::max (0, tickAt (e.position.x)), rankAt (e.position.y));
}

void CustomPatternEditor::mouseMove (const juce::MouseEvent& e)
{
    hoverAt = e.getPosition();
    auto pointer = juce::MouseCursor::NormalCursor;
    if (dialog == nullptr && grid.contains (hoverAt) && ! recordingTake)
    {
        const int i = eventAt (e.position);
        if (i >= 0)
        {
            const auto box = eventArea (i);
            const auto& ev = doc.pattern.events[static_cast<std::size_t> (i)];
            if (ev.rest)
                pointer = juce::MouseCursor::CrosshairCursor;
            else if (e.position.y >= box.getY() - 3.0f && e.position.y <= box.getBottom() + 3.0f
                     && (std::abs (e.position.x - box.getRight()) <= 5.0f || (i > 0 && std::abs (e.position.x - box.getX()) <= 5.0f)))
                pointer = juce::MouseCursor::LeftRightResizeCursor;
            else
                pointer = juce::MouseCursor::UpDownResizeCursor;
        }
    }
    else
        for (const auto& k : keys)
            if (keyShown (k.action) && k.area.contains (hoverAt) && keyEnabled (k.action))
                pointer = juce::MouseCursor::PointingHandCursor;
    setMouseCursor (pointer);
    repaint();
}

void CustomPatternEditor::mouseExit (const juce::MouseEvent&)
{
    hoverAt = { -1, -1 };
    repaint();
}

void CustomPatternEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Long patterns scroll sideways under the wheel; nothing is ever edited by it.
    if (! grid.contains (e.getPosition()) || stepsOf (doc.pattern) <= maxStepsShown)
        return;
    wheelPending -= (std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY) * 16.0f;
    const int steps = static_cast<int> (wheelPending);
    if (steps != 0)
    {
        wheelPending -= static_cast<float> (steps);
        setViewStep (viewStep + steps);
    }
}

bool CustomPatternEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (dialog != nullptr)
    {
        if (k.isKeyCode (juce::KeyPress::escapeKey))
        {
            answerDialog (false);
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::returnKey))
        {
            answerDialog (true);
            return true;
        }
        return true;   // (the panel waits for the answer)
    }
    if (k.isKeyCode (juce::KeyPress::escapeKey))
        return false;   // the popover host asks dismissRequested
    if (mods.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
    {
        perform (mods.isShiftDown() ? Action::redo : Action::undo);
        return true;
    }
    if (name->hasKeyboardFocus (true))
        return false;
    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey))
    {
        select (selection + (k.isKeyCode (juce::KeyPress::leftKey) ? -1 : 1));
        return true;
    }
    if ((k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey)) && selection >= 0)
    {
        const auto& e = doc.pattern.events[static_cast<std::size_t> (selection)];
        setSelectedRank (std::max (0, (e.rest ? 0 : e.rank) + (k.isKeyCode (juce::KeyPress::upKey) ? 1 : -1)));
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::deleteKey) || k.isKeyCode (juce::KeyPress::backspaceKey))
    {
        // Delete: the note becomes a rest (its time kept). Shift-Delete: the event and its time go.
        perform (mods.isShiftDown() ? Action::deleteStep : Action::rest);
        return true;
    }
    switch (juce::CharacterFunctions::toLowerCase (k.getTextCharacter()))
    {
        case 's': perform (Action::split); return true;
        case 'm': perform (Action::merge); return true;
        case 'e': perform (Action::extend); return true;
        case 'r': perform (Action::rest); return true;
        default: break;
    }
    return false;
}

juce::String CustomPatternEditor::getTooltip()
{
    const auto at = getMouseXYRelative();
    if (dialog != nullptr)
        return {};
    const auto card = "card " + InstrumentHost::shownLayerName (cardIndex);
    for (const auto& k : keys)
    {
        if (! keyShown (k.action) || ! k.area.contains (at))
            continue;
        switch (k.action)
        {
            case Action::record: return recordingTake ? "Stop recording" : "Record a phrase from your keys or the host (the arpeggiator steps aside while you play)";
            case Action::import: return "Import a phrase from a MIDI file";
            case Action::overflow: return "Save to Library, copies, rename, browse, clear";
            case Action::toggleSource: return doc.capture.empty() ? juce::String() : (sourceOpen ? "Hide the original take" : "Show the original take (read only)");
            case Action::timingMenu:
                return doc.capture.empty() ? "Record or import a phrase to choose how its timing is read"
                                           : "How the take becomes steps: Even Steps (one step per note) or Recorded Rhythm (its timing, on a grid)";
            case Action::gridMenu: return "The grid the recorded rhythm is placed on, in steps";
            case Action::mappingMenu:
                return doc.pattern.mapping == arp::CustomMapping::contour ? "Contour: the gesture's shape, climbing an octave per cycle where there is room"
                                                                          : "Degrees: rank 1 plays the lowest held note, rank 2 the next ...";
            case Action::shorter: return "One step shorter (only silence at the end is removed)";
            case Action::longer: return "One step longer (a rest at the end)";
            case Action::split: return "Split the selected event in two (S)";
            case Action::rest: return "The selected note becomes a rest; its time stays (R, Delete)";
            case Action::insertStep: return "A rest step after the selected event (the pattern gets longer)";
            case Action::transform: return "Reverse, mirror or rotate the whole pattern";
            case Action::merge: return "Merge the selected event with the next (M)";
            case Action::extend: return "Extend the note into the rest after it (E)";
            case Action::undo: return "Undo (Cmd-Z)";
            case Action::redo: return "Redo (Shift-Cmd-Z)";
            case Action::preview: return previewOn ? "Stop previewing (the card as it was)" : "Hear the pattern on " + card + " while you edit (hold a chord)";
            case Action::cancel: return "Close without applying (the card stays as it was)";
            case Action::use: return "Use this pattern on " + card + " (saved with the project and presets)";
            case Action::help: return "How to edit a pattern";
            case Action::close: return "Close";
            default: return {};
        }
    }
    if (grid.contains (at) && ! recordingTake)
        if (const int i = eventAt (at.toFloat()); i >= 0)
            return doc.pattern.events[static_cast<std::size_t> (i)].rest ? "Double-click to place a note (right-click for more)"
                                                                         : "Drag up or down for its rank, its edge for its length; right-click for more";
    return {};
}

//==============================================================================
// Drawing

void CustomPatternEditor::paintKey (juce::Graphics& g, const Key& k)
{
    using namespace design;
    if (k.area.isEmpty() || ! keyShown (k.action))
        return;
    const auto r = k.area.toFloat().reduced (0.5f);
    const bool enabled = keyEnabled (k.action);
    const bool lit = keyLit (k.action);
    const bool hover = enabled && k.area.contains (hoverAt);
    const auto inkAlpha = enabled ? 0.9f : 0.32f;
    auto neutral = [&] (float radius) {
        g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, r.getY(), colour::buttonBottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);
        g.setColour (hover ? colour::divider.darker (0.12f) : colour::edgeShade);
        g.drawRoundedRectangle (r, radius, 1.0f);
    };
    switch (k.action)
    {
        case Action::help:
        case Action::close:
        {
            if (hover || lit)
            {
                g.setColour (colour::text.withAlpha (lit ? 0.1f : 0.06f));
                g.fillRoundedRectangle (r, 6.0f);
            }
            g.setColour (colour::text.withAlpha (0.75f));
            const auto c = r.getCentre();
            if (k.action == Action::close)
            {
                g.drawLine (c.x - 4.5f, c.y - 4.5f, c.x + 4.5f, c.y + 4.5f, 1.4f);
                g.drawLine (c.x + 4.5f, c.y - 4.5f, c.x - 4.5f, c.y + 4.5f, 1.4f);
            }
            else
            {
                g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre (c), 1.2f);
                g.setFont (fonts::make (10.0f, fonts::Weight::medium));
                g.drawText ("?", juce::Rectangle<float> (14.0f, 14.0f).withCentre (c.translated (0.0f, 0.5f)), juce::Justification::centred, false);
            }
            return;
        }
        case Action::toggleSource: return;   // (paintSource)
        case Action::timingMenu:
            paintSelector (g, k.area, timingName (doc.interpretation), enabled, hover);
            return;
        case Action::gridMenu:
            paintSelector (g, k.area, gridName (doc.interpretation.quantumTicks), enabled, hover);
            return;
        case Action::mappingMenu:
            paintSelector (g, k.area, doc.pattern.mapping == arp::CustomMapping::degrees ? "Degrees" : "Contour", enabled, hover);
            return;
        case Action::use:
        {
            // The one emphasised action.
            if (enabled)
            {
                g.setGradientFill (juce::ColourGradient (hover ? colour::accentTop.brighter (0.05f) : colour::accentTop, 0.0f, r.getY(), colour::accentBottom, 0.0f, r.getBottom(), false));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (juce::Colours::black.withAlpha (0.16f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
                g.setColour (juce::Colours::white);
            }
            else
            {
                neutral (6.0f);
                g.setColour (colour::text.withAlpha (inkAlpha));
            }
            g.setFont (type::popupLabel (12.5f));
            g.drawText (k.label, r, juce::Justification::centred, false);
            return;
        }
        case Action::record:
        {
            if (lit)
            {
                // Recording: a restrained red tint, a solid red square (Stop).
                g.setColour (juce::Colour (0xfff4dcd4));
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (recordRed.withAlpha (0.6f));
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
                g.setColour (recordRed);
                g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() }), 1.5f);
                g.setColour (recordRed.darker (0.35f));
            }
            else
            {
                neutral (6.0f);
                g.setColour (recordRed.withAlpha (enabled ? 0.9f : 0.35f));
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ r.getX() + 16.0f, r.getCentreY() }));
                g.setColour (colour::text.withAlpha (inkAlpha));
            }
            g.setFont (type::popupLabel (12.0f));
            g.drawText (lit ? "Stop" : k.label, r.withTrimmedLeft (26.0f), juce::Justification::centredLeft, false);
            return;
        }
        case Action::preview:
        {
            // On: a pressed dark key (neutral, never the accent), the play mark lit.
            if (lit)
            {
                g.setColour (colour::wellA);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (colour::wellRim);
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            else
                neutral (6.0f);
            juce::Path play;
            const auto c = juce::Point<float> (r.getX() + 17.0f, r.getCentreY());
            play.addTriangle (c.x - 3.5f, c.y - 4.5f, c.x - 3.5f, c.y + 4.5f, c.x + 4.5f, c.y);
            g.setColour (lit ? noteColour : colour::text.withAlpha (inkAlpha));
            g.fillPath (play);
            g.setColour (lit ? juce::Colour (0xffe9e3d6) : colour::text.withAlpha (inkAlpha));
            g.setFont (type::popupLabel (12.0f));
            g.drawText (lit ? "Previewing" : k.label, r.withTrimmedLeft (28.0f), juce::Justification::centredLeft, false);
            return;
        }
        case Action::undo:
        case Action::redo:
            neutral (5.0f);
            drawUndoIcon (g, r, k.action == Action::redo, colour::text.withAlpha (inkAlpha));
            return;
        case Action::overflow:
        {
            neutral (6.0f);
            g.setColour (colour::text.withAlpha (inkAlpha));
            for (int d = -1; d <= 1; ++d)
                g.fillEllipse (juce::Rectangle<float> (3.2f, 3.2f).withCentre (r.getCentre().translated (static_cast<float> (d) * 6.0f, 0.0f)));
            return;
        }
        case Action::transform:
        {
            neutral (5.0f);
            g.setColour (colour::text.withAlpha (inkAlpha));
            g.setFont (type::popupLabel (12.0f));
            g.drawText (k.label, r.withTrimmedLeft (10.0f), juce::Justification::centredLeft, false);
            drawChevron (g, r.getRight() - 12.0f, r.getCentreY(), colour::text.withAlpha (inkAlpha));
            return;
        }
        default:
        {
            neutral (k.action == Action::import || k.action == Action::cancel ? 6.0f : 5.0f);
            g.setColour (colour::text.withAlpha (inkAlpha));
            g.setFont (type::popupLabel (k.action == Action::shorter || k.action == Action::longer ? 14.0f : 12.0f));
            g.drawText (k.label, r, juce::Justification::centred, false);
            return;
        }
    }
}

void CustomPatternEditor::paintSelector (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, bool enabled, bool hover)
{
    using namespace design;
    const auto r = area.toFloat().reduced (0.5f);
    g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, r.getY(), colour::buttonBottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (hover ? colour::divider.darker (0.12f) : colour::edgeShade);
    g.drawRoundedRectangle (r, 5.0f, 1.0f);
    const float alpha = enabled ? 0.88f : 0.35f;
    g.setColour (colour::text.withAlpha (alpha));
    g.setFont (type::popupLabel (12.0f));
    g.drawText (text, r.withTrimmedLeft (9.0f).withTrimmedRight (20.0f), juce::Justification::centredLeft, true);
    drawChevron (g, r.getRight() - 11.0f, r.getCentreY(), colour::text.withAlpha (alpha));
}

void CustomPatternEditor::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    using namespace design;
    // CARD X, quietly, after the title.
    {
        g.setFont (type::popupTitle (13.0f));
        const float titleW = juce::GlyphArrangement::getStringWidth (type::popupTitle (13.0f), title);
        const auto c = card().toFloat();
        g.setFont (type::popupLabel (10.0f));
        g.setColour (colour::textMicro);
        g.drawText ("CARD " + InstrumentHost::shownLayerName (cardIndex), juce::Rectangle<float> (c.getX() + 13.0f + titleW + 10.0f, c.getY() + 7.0f, 80.0f, 20.0f),
                    juce::Justification::centredLeft, false);
    }

    paintSource (g);

    // TIMING / GRID / MAPPING / LENGTH captions.
    g.setFont (type::popupLabel (popover::captionFont));
    auto caption = [&g] (const juce::String& text, juce::Rectangle<int> r, bool enabled) {
        g.setColour (colour::textMicro.withAlpha (enabled ? 1.0f : 0.5f));
        g.drawText (text, r, juce::Justification::centredLeft, false);
    };
    caption ("TIMING", timingCaption, ! doc.capture.empty());
    if (keyShown (Action::gridMenu))
        caption ("GRID", gridCaption, ! doc.capture.empty());
    caption ("MAPPING", mappingCaption, true);
    caption ("LENGTH", lengthCaption, true);
    {
        const auto r = lengthText.toFloat().reduced (3.0f, 1.0f);
        g.setColour (colour::text.withAlpha (0.04f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (colour::text.withAlpha (0.85f));
        g.setFont (type::popupValue (12.0f));
        g.drawText (doc.pattern.events.empty() ? juce::String ("-") : stepsText (stepsOf (doc.pattern)), r, juce::Justification::centred, false);
    }

    paintGrid (g, grid.toFloat());
    for (const auto& k : keys)
        paintKey (g, k);

    // A hairline over the footer, the message beside Preview.
    g.setColour (colour::hairline);
    g.fillRect (juce::Rectangle<float> (static_cast<float> (footerRow.getX()), static_cast<float> (footerRow.getY()) - 4.5f, static_cast<float> (footerRow.getWidth()), 1.0f));
    g.setFont (type::annotation (11.0f));
    g.setColour (colour::textSecondary);
    g.drawText (status, statusArea, juce::Justification::centredLeft, true);

    if (helpOpen)
        paintHelp (g);
    if (dialog != nullptr)
        paintDialog (g);
}

void CustomPatternEditor::paintSource (juce::Graphics& g)
{
    using namespace design;
    const auto strip = sourceStrip.toFloat();
    const auto* live = recordingTake && host.patternCapture() != nullptr ? &host.patternCapture()->notes() : nullptr;
    if (! recordingTake && doc.capture.empty())
    {
        // No take: guidance, not an empty box.
        g.setFont (type::annotation (11.5f));
        g.setColour (colour::textMicro);
        g.drawText ("Draw a pattern below, record a phrase, or import MIDI.", strip.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
        return;
    }
    const bool hover = sourceStrip.contains (hoverAt) && ! recordingTake;
    if (hover)
    {
        g.setColour (colour::text.withAlpha (0.05f));
        g.fillRoundedRectangle (strip, 5.0f);
    }
    // ▸ / ▾ Original Take · 16 notes · 120 BPM
    juce::Path tri;
    const auto c = juce::Point<float> (strip.getX() + 9.0f, strip.getCentreY());
    if (sourceOpen)
        tri.addTriangle (c.x - 3.5f, c.y - 2.0f, c.x + 3.5f, c.y - 2.0f, c.x, c.y + 2.5f);
    else
        tri.addTriangle (c.x - 2.0f, c.y - 3.5f, c.x - 2.0f, c.y + 3.5f, c.x + 2.5f, c.y);
    g.setColour (colour::textSecondary);
    g.fillPath (tri);
    juce::String text;
    if (recordingTake)
    {
        g.setColour (recordRed);
        g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ strip.getX() + 26.0f, strip.getCentreY() }));
        text << "Recording" << sep << static_cast<int> (live != nullptr ? live->size() : 0) << " notes";
        g.setFont (type::popupLabel (11.5f));
        g.setColour (colour::text.withAlpha (0.85f));
        g.drawText (text, strip.withTrimmedLeft (36.0f), juce::Justification::centredLeft, false);
    }
    else
    {
        text << "Original Take";
        g.setFont (type::popupLabel (11.5f));
        g.setColour (colour::text.withAlpha (0.82f));
        g.drawText (text, strip.withTrimmedLeft (20.0f), juce::Justification::centredLeft, false);
        const float w = juce::GlyphArrangement::getStringWidth (type::popupLabel (11.5f), text);
        juce::String details;
        details << juce::String::fromUTF8 ("·  ") << static_cast<int> (doc.capture.size()) << " notes  " << juce::String::fromUTF8 ("·  ") << juce::roundToInt (doc.captureBpm) << " BPM";
        g.setColour (colour::textMicro);
        g.drawText (details, strip.withTrimmedLeft (20.0f + w + 8.0f), juce::Justification::centredLeft, false);
    }
    if (sourceOpen && ! recorded.isEmpty())
        paintRecorded (g, recorded.toFloat());
}

void CustomPatternEditor::paintRecorded (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace design;
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colour::wellRim);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const auto* live = recordingTake && host.patternCapture() != nullptr ? &host.patternCapture()->notes() : nullptr;
    const auto& notes = live != nullptr ? *live : doc.capture;
    const auto area = r.reduced (10.0f, 8.0f);
    if (notes.empty())
    {
        g.setFont (type::popupLabel (10.0f));
        g.setColour (wellText.withAlpha (0.8f));
        g.drawText (recordingTake ? "Waiting for the first note..." : "No take", area, juce::Justification::centred, false);
        return;
    }
    int low = 127, high = 0;
    double end = 0.0;
    for (const auto& n : notes)
    {
        low = std::min (low, n.pitch);
        high = std::max (high, n.pitch);
        end = std::max (end, n.onset + n.duration);
    }
    end = std::max (end, 1.0);
    const float span = static_cast<float> (std::max (6, high - low + 1));
    for (const auto& n : notes)
    {
        const float x0 = area.getX() + static_cast<float> (n.onset / end) * area.getWidth();
        const float x1 = area.getX() + static_cast<float> ((n.onset + n.duration) / end) * area.getWidth();
        const float y = area.getBottom() - (static_cast<float> (n.pitch - low) + 0.5f) / span * area.getHeight();
        g.setColour (wellText.withAlpha (0.4f + 0.5f * static_cast<float> (std::clamp (n.velocity, 1, 127)) / 127.0f));
        g.fillRoundedRectangle (x0, y - 1.5f, std::max (2.0f, x1 - x0), 3.0f, 1.5f);
    }
}

void CustomPatternEditor::paintGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace design;
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (colour::wellRim);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    const auto& p = doc.pattern;
    const auto plot = plotArea();
    if (p.events.empty())
    {
        g.setFont (type::popupLabel (11.0f));
        g.setColour (wellText.withAlpha (0.85f));
        g.drawText ("Empty: Add Step to draw, or Record / Import a phrase", plot, juce::Justification::centred, false);
        return;
    }
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (r.reduced (2.0f).getSmallestIntegerContainer());
    const int ranks = std::max (p.maxRank, p.highestRank()) + 1;
    const int rows = rowsShown();
    const float rowH = plot.getHeight() / static_cast<float> (rows);
    // Rank rows (1 = the lowest note the gesture used), faint beyond the pattern's range.
    g.setFont (type::popupLabel (8.5f));
    for (int k = 0; k < rows; ++k)
    {
        const float y = plot.getBottom() - static_cast<float> (k + 1) * rowH;
        if (k % 2 == 1)
        {
            g.setColour (juce::Colours::white.withAlpha (0.018f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), y, plot.getWidth(), rowH));
        }
        if (k < ranks)
        {
            g.setColour (wellText.withAlpha (0.65f));
            g.drawText (juce::String (k + 1), juce::Rectangle<float> (plot.getX() - 18.0f, y, 13.0f, rowH), juce::Justification::centredRight, false);
        }
    }
    // Step lines: every step faint, every four stronger, numbered quietly above.
    const int steps = stepsOf (p);
    const int firstStep = steps > maxStepsShown ? viewStep : 0;
    const int lastStep = std::min (steps, firstStep + stepsShown());
    for (int s = firstStep; s <= lastStep; ++s)
    {
        const float x = xOfTick (s * arp::ticksPerStep);
        if (x > plot.getRight() + 0.5f)
            break;
        const bool beat = s % 4 == 0;
        g.setColour (colour::wellGrid.withAlpha (beat ? 1.0f : 0.5f).brighter (beat ? 0.25f : 0.0f));
        g.fillRect (juce::Rectangle<float> (x - (beat ? 0.6f : 0.4f), plot.getY(), beat ? 1.2f : 0.8f, plot.getHeight()));
        if (beat && s < lastStep)
        {
            g.setColour (wellText.withAlpha (0.55f));
            g.drawText (juce::String (s + 1), juce::Rectangle<float> (x + 3.0f, plot.getY() - 11.0f, 30.0f, 10.0f), juce::Justification::centredLeft, false);
        }
    }
    // The pattern's end, when it is in view.
    {
        const float end = xOfTick (p.lengthTicks());
        if (end <= plot.getRight() + 0.5f)
        {
            g.setColour (noteColour.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float> (end - 0.75f, plot.getY() - 4.0f, 1.5f, plot.getHeight() + 4.0f));
        }
    }
    // Events: notes as amber bars on their rank, rests as a faint dashed outline; the selection
    // brighter and ringed, with grips on its edges (drag them for its length).
    const int hoverEvent = grid.contains (hoverAt) && dialog == nullptr && ! recordingTake ? eventAt (hoverAt.toFloat()) : -1;
    for (int i = 0; i < static_cast<int> (p.events.size()); ++i)
    {
        const auto box = eventArea (i);
        if (box.isEmpty())
            continue;
        const auto& e = p.events[static_cast<std::size_t> (i)];
        const bool sel = i == selection;
        if (e.rest)
        {
            if (i == hoverEvent)
            {
                // Where a double-click places a note: the step and rank under the pointer.
                const int tick = std::max (0, tickAt (static_cast<float> (hoverAt.x)));
                const int stepStart = std::max (p.onsetOf (i), (tick / arp::ticksPerStep) * arp::ticksPerStep);
                const int stepEnd = std::min (p.onsetOf (i) + e.duration, stepStart + arp::ticksPerStep);
                const int rank = rankAt (static_cast<float> (hoverAt.y));
                const float cy = plot.getBottom() - (static_cast<float> (std::min (rank, rows - 1)) + 0.5f) * rowH;
                const float h = std::min (std::max (4.0f, rowH - 4.0f), 20.0f);
                g.setColour (noteColour.withAlpha (0.16f));
                g.fillRoundedRectangle (juce::Rectangle<float> (xOfTick (stepStart) + 1.0f, cy - 0.5f * h, std::max (3.0f, xOfTick (stepEnd) - xOfTick (stepStart) - 2.0f), h), 3.0f);
            }
            g.setColour (wellText.withAlpha (sel ? 0.55f : 0.16f));
            const float dashes[] { 3.0f, 3.0f };
            juce::Path outline;
            outline.addRoundedRectangle (box.reduced (1.0f, 4.0f), 3.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
            continue;
        }
        const float velocityShade = e.velocity > 0 ? 0.55f + 0.45f * static_cast<float> (e.velocity) / 127.0f : 0.85f;
        g.setColour (noteColour.withAlpha (sel ? 1.0f : (i == hoverEvent ? 0.95f : velocityShade)));
        g.fillRoundedRectangle (box, 3.0f);
        if (sel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.92f));
            g.drawRoundedRectangle (box.expanded (1.5f), 4.0f, 1.4f);
            // Edge grips.
            g.setColour (colour::wellA.withAlpha (0.7f));
            for (const float x : { box.getRight() - 3.5f, box.getX() + 2.0f })
                if (box.getWidth() > 12.0f && (x > box.getCentreX() || i > 0))
                    g.fillRoundedRectangle (juce::Rectangle<float> (1.5f, std::max (4.0f, box.getHeight() - 8.0f)).withCentre ({ x + 0.75f, box.getCentreY() }), 0.75f);
        }
    }
    // The scroll track of a long pattern.
    if (const auto track = scrollTrack(); ! track.isEmpty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.fillRoundedRectangle (track, 2.0f);
        const float share = static_cast<float> (maxStepsShown) / static_cast<float> (steps);
        const float start = static_cast<float> (viewStep) / static_cast<float> (steps);
        g.setColour (wellText.withAlpha (0.6f));
        g.fillRoundedRectangle (track.withX (track.getX() + start * track.getWidth()).withWidth (share * track.getWidth()), 2.0f);
    }
}

void CustomPatternEditor::paintHelp (juce::Graphics& g)
{
    using namespace design;
    const auto box = grid.toFloat().reduced (40.0f, 14.0f);
    g.setColour (juce::Colour (0xfff8f4ec));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (colour::edgeShade);
    g.drawRoundedRectangle (box, 8.0f, 1.0f);
    const juce::StringArray lines {
        "Click a note to select it; drag it up or down for its rank.",
        "Drag a note's edge for its length (the pattern keeps its length).",
        "Double-click a rest to place a note there.",
        "Right-click a note for every action: merge, extend, remove time ...",
        "Delete makes a rest; Shift-Delete removes the event and its time.",
        "Preview plays the pattern on the card. Apply Pattern keeps it here;",
        "Save to Library (in the ... menu) keeps it for other projects.",
    };
    g.setFont (type::popupLabel (10.0f));
    g.setColour (colour::textMicro);
    auto r = box.reduced (18.0f, 12.0f);
    g.drawText ("HOW TO EDIT", r.removeFromTop (16.0f), juce::Justification::centredLeft, false);
    g.setFont (type::annotation (12.0f));
    g.setColour (colour::text.withAlpha (0.85f));
    for (const auto& line : lines)
        g.drawText (line, r.removeFromTop (17.0f), juce::Justification::centredLeft, true);
}

void CustomPatternEditor::paintDialog (juce::Graphics& g)
{
    using namespace design;
    const auto c = card().toFloat();
    g.setColour (juce::Colour (0xff1e1a16).withAlpha (0.16f));
    g.fillRoundedRectangle (c, 14.0f);
    const auto box = dialog->box.toFloat();
    juce::Path shape;
    shape.addRoundedRectangle (box, 10.0f);
    design::CachedShadow (juce::Colour (0x2a1e1a16), 14, { 0, 4 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfffbf8f2), 0.0f, box.getY(), juce::Colour (0xfff2ece1), 0.0f, box.getBottom(), false));
    g.fillPath (shape);
    g.setColour (colour::edgeShade);
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    auto inner = box.reduced (20.0f, 16.0f);
    g.setFont (type::popupLabel (14.0f));
    g.setColour (colour::text);
    g.drawText (dialog->title, inner.removeFromTop (20.0f), juce::Justification::centredLeft, true);
    inner.removeFromTop (6.0f);
    g.setFont (type::annotation (12.0f));
    g.setColour (colour::textSecondary);
    g.drawFittedText (dialog->text, inner.removeFromTop (32.0f).getSmallestIntegerContainer(), juce::Justification::topLeft, 2);
    auto key = [&g] (juce::Rectangle<int> area, const juce::String& label, bool primary, bool destructive) {
        const auto r = area.toFloat().reduced (0.5f);
        if (primary && ! destructive)
        {
            g.setGradientFill (juce::ColourGradient (colour::accentTop, 0.0f, r.getY(), colour::accentBottom, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            g.setColour (juce::Colours::white);
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (colour::buttonTop, 0.0f, r.getY(), colour::buttonBottom, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (destructive ? recordRed.withAlpha (0.55f) : colour::edgeShade);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            g.setColour (destructive ? recordRed.darker (0.2f) : colour::text.withAlpha (0.88f));
        }
        g.setFont (type::popupLabel (12.0f));
        g.drawText (label, r, juce::Justification::centred, false);
    };
    key (dialog->cancelArea, dialog->cancelLabel, false, false);
    key (dialog->confirmArea, dialog->confirmLabel, true, dialog->destructive);
}

} // namespace osp::plugin
