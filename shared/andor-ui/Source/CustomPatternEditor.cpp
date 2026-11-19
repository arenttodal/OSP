#include "CustomPatternEditor.h"

#include "AndorParameters.h"
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

    juce::String customId (InstrumentHost& host, int card) { return ids::arpCustom (host.cardArpNamespace(), card); }

    const juce::Colour wellText = design::colour::wellText;
    const juce::Colour noteColour = design::colour::arpOnDark;
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
    }
    customBefore = host.parameterValue (customId (host, cardIndex));
    history.reset (doc.pattern);
    selection = doc.pattern.events.empty() ? -1 : 0;

    name = std::make_unique<juce::TextEditor>();
    styleTextField (*name);
    name->setText (juce::String (doc.name), false);
    name->setTooltip ("The pattern's name (SAVE keeps it in your library)");
    name->onTextChange = [this] { doc.name = name->getText().trim().toStdString(); };
    name->onReturnKey = [this] { grabKeyboardFocus(); };
    addAndMakeVisible (*name);

    keys = {
        { Action::record, "RECORD" }, { Action::import, "IMPORT MIDI" }, { Action::preview, "PREVIEW", {}, true },
        { Action::timingSteps, "STEPS" }, { Action::timingHalf, "1/2" }, { Action::timingQuarter, "1/4" }, { Action::timingEighth, "1/8" },
        { Action::contour, "CONTOUR" }, { Action::degrees, "DEGREES" },
        { Action::split, "SPLIT" }, { Action::merge, "MERGE" }, { Action::extend, "EXTEND" }, { Action::rest, "REST" }, { Action::remove, "DELETE" },
        { Action::insertStep, "+ STEP" }, { Action::deleteStep, "- STEP" },
        { Action::reverse, "REVERSE" }, { Action::mirror, "MIRROR" }, { Action::rotate, "ROTATE" },
        { Action::shorter, "-" }, { Action::longer, "+" },
        { Action::undo, "UNDO" }, { Action::redo, "REDO" },
        { Action::save, "SAVE" }, { Action::library, "LIBRARY" },
        { Action::clear, "CLEAR" }, { Action::cancel, "CANCEL" }, { Action::use, "USE PATTERN" },
    };
    status = doc.pattern.events.empty() ? "RECORD a phrase from your keys or the host, or IMPORT a MIDI file" : "Select an event to edit it";
    startTimerHz (30);
}

CustomPatternEditor::~CustomPatternEditor()
{
    stopTimer();
    // Closed without USE PATTERN (CANCEL, Escape, a click outside): the card as it was.
    if (auto* c = host.patternCapture(); c != nullptr && recordingTake)
        c->cancel();
    if (! used)
        restoreCard();
}

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
    history.push (next);
    if (selection >= static_cast<int> (doc.pattern.events.size()))
        selection = static_cast<int> (doc.pattern.events.size()) - 1;
    status = what;
    if (previewOn)
        publishPreview();
    repaint();
}

void CustomPatternEditor::select (int index)
{
    selection = doc.pattern.events.empty() ? -1 : juce::jlimit (0, static_cast<int> (doc.pattern.events.size()) - 1, index);
    repaint();
}

void CustomPatternEditor::setSelectedRank (int rank)
{
    auto next = doc.pattern;
    if (selection >= 0 && ap::edit::setRank (next, selection, juce::jlimit (0, ap::maxRankLimit, rank)))
        commit (next, "Rank " + juce::String (rank + 1));
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
        case Action::split: return has && tryEdit ([this] (ap::Pattern& c) { return ap::edit::split (c, selection); });
        case Action::merge: return has && tryEdit ([this] (ap::Pattern& c) { return ap::edit::merge (c, selection); });
        case Action::extend: return has && tryEdit ([this] (ap::Pattern& c) {
            return selection + 1 < static_cast<int> (c.events.size()) && ap::edit::extend (c, selection, c.events[static_cast<std::size_t> (selection + 1)].duration);
        });
        case Action::rest: return has && ! p.events[static_cast<std::size_t> (selection)].rest;
        case Action::remove: return has && (! p.events[static_cast<std::size_t> (selection)].rest || p.events.size() > 1);
        case Action::insertStep: return tryEdit ([this] (ap::Pattern& c) { return ap::edit::insertStep (c, std::max (0, selection + 1)); });
        case Action::deleteStep: return has && p.events.size() > 1;
        case Action::reverse:
        case Action::mirror:
        case Action::rotate: return p.events.size() > 1;
        case Action::shorter: return tryEdit ([this] (ap::Pattern& c) { return ap::edit::setLength (c, (stepsOf (c) - 1) * arp::ticksPerStep); });
        case Action::longer: return ! p.events.empty() && tryEdit ([this] (ap::Pattern& c) { return ap::edit::setLength (c, (stepsOf (c) + 1) * arp::ticksPerStep); });
        case Action::undo: return history.canUndo();
        case Action::redo: return history.canRedo();
        case Action::use:
        case Action::preview:
        case Action::save:
        case Action::duplicate: return p.valid();
        case Action::rename:
        case Action::deleteFromLibrary: return ! doc.factory && ! doc.id.empty();
        case Action::clear: return ! p.events.empty() || ! doc.capture.empty();
        case Action::timingSteps:
        case Action::timingHalf:
        case Action::timingQuarter:
        case Action::timingEighth: return ! doc.capture.empty();
        default: return true;
    }
}

void CustomPatternEditor::perform (Action a)
{
    if (! canPerform (a))
        return;
    auto next = doc.pattern;
    auto timing = [this] (ap::Interpretation::Timing t, int quantum) {
        doc.interpretation.timing = t;
        doc.interpretation.quantumTicks = quantum;
        reinterpret();
    };
    switch (a)
    {
        case Action::record:
        {
            auto* c = host.patternCapture();
            if (recordingTake)
            {
                finishTake();
                return;
            }
            c->start (host.hostSampleRate() > 0.0 ? host.hostSampleRate() : 48000.0);
            recordingTake = true;
            status = "Recording: play the phrase (the arpeggiator steps aside), then STOP";
            break;
        }
        case Action::import: openImportChooser(); return;
        case Action::preview:
            previewOn = ! previewOn;
            if (previewOn)
                publishPreview();
            else
                restoreCard();
            status = previewOn ? "PREVIEW: hold a chord to hear the pattern on card " + InstrumentHost::shownLayerName (cardIndex) : "Preview off";
            break;
        case Action::timingSteps: timing (ap::Interpretation::Timing::steps, doc.interpretation.quantumTicks); return;
        case Action::timingHalf: timing (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 2); return;
        case Action::timingQuarter: timing (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 4); return;
        case Action::timingEighth: timing (ap::Interpretation::Timing::rhythm, arp::ticksPerStep / 8); return;
        case Action::contour:
        case Action::degrees:
            ap::edit::setMapping (next, a == Action::contour ? arp::CustomMapping::contour : arp::CustomMapping::degrees);
            doc.interpretation.mapping = next.mapping;
            commit (next, a == Action::contour ? "CONTOUR: the gesture's shape, climbing an octave per cycle where there is room"
                                               : "DEGREES: rank 1 plays the lowest held note, rank 2 the next ...");
            return;
        case Action::split: ap::edit::split (next, selection); commit (next, "Split"); return;
        case Action::merge: ap::edit::merge (next, selection); commit (next, "Merged"); return;
        case Action::extend:
            ap::edit::extend (next, selection, next.events[static_cast<std::size_t> (selection + 1)].duration);
            commit (next, "Extended into the rest");
            return;
        case Action::rest: ap::edit::toRest (next, selection); commit (next, "Rest (its time kept)"); return;
        case Action::remove:
            if (! next.events[static_cast<std::size_t> (selection)].rest)
                ap::edit::remove (next, selection), commit (next, "Deleted (its time kept as a rest)");
            else
                ap::edit::deleteEvent (next, selection), commit (next, "Rest removed (the pattern is shorter)");
            return;
        case Action::insertStep:
            ap::edit::insertStep (next, std::max (0, selection + 1));
            selection = std::max (0, selection + 1);
            commit (next, "A step added (the pattern is longer)");
            return;
        case Action::deleteStep: ap::edit::deleteEvent (next, selection); commit (next, "Event removed (the pattern is shorter)"); return;
        case Action::reverse: ap::edit::reverse (next); commit (next, "Reversed"); return;
        case Action::mirror: ap::edit::mirror (next); commit (next, "Mirrored"); return;
        case Action::rotate: ap::edit::rotate (next, 1); commit (next, "Rotated"); return;
        case Action::shorter: ap::edit::setLength (next, (stepsOf (next) - 1) * arp::ticksPerStep); commit (next, "Length " + juce::String (stepsOf (next)) + " steps"); return;
        case Action::longer: ap::edit::setLength (next, (stepsOf (next) + 1) * arp::ticksPerStep); commit (next, "Length " + juce::String (stepsOf (next)) + " steps"); return;
        case Action::undo:
            doc.pattern = history.undo();
            status = "Undo";
            if (previewOn)
                publishPreview();
            select (selection);
            return;
        case Action::redo:
            doc.pattern = history.redo();
            status = "Redo";
            if (previewOn)
                publishPreview();
            select (selection);
            return;
        case Action::clear:
            doc.capture.clear();
            commit ({}, "Cleared");
            selection = -1;
            return;
        case Action::cancel:
            if (onClose != nullptr)
                onClose();
            return;
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
            if (onClose != nullptr)
                onClose();
            return;
        }
        case Action::library: openLibraryMenu(); return;
        case Action::save:
        case Action::rename:
        case Action::duplicate:
        case Action::deleteFromLibrary:
        {
            const auto folder = libraryFolder();
            folder.createDirectory();
            auto fileFor = [&folder] (const ap::Document& d) {
                for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + arpPatternExtension))
                {
                    std::string error;
                    if (const auto read = readArpPattern (f.getFullPathName().toStdString(), error); read && read->id == d.id)
                        return f;
                }
                return juce::File();
            };
            auto freeFile = [&folder] (const juce::String& wanted) {
                const auto base = juce::File::createLegalFileName (wanted.isEmpty() ? "Pattern" : wanted);
                auto f = folder.getChildFile (base + arpPatternExtension);
                for (int i = 2; f.exists(); ++i)
                    f = folder.getChildFile (base + " " + juce::String (i) + arpPatternExtension);
                return f;
            };
            const auto wanted = name->getText().trim();
            std::string error;
            if (a == Action::save)
            {
                if (doc.factory)
                {
                    // A factory pattern is never overwritten: saving keeps a copy of your own.
                    doc.factory = false;
                    doc.id = newArpPatternId();
                }
                doc.name = wanted.isEmpty() ? std::string ("Pattern") : wanted.toStdString();
                auto file = fileFor (doc);
                if (file == juce::File())
                    file = freeFile (wanted);
                status = writeArpPattern (file.getFullPathName().toStdString(), doc, error) ? "Saved in your library: " + file.getFileNameWithoutExtension()
                                                                                             : "Could not save: " + juce::String (error);
            }
            else if (a == Action::rename)
            {
                const auto file = fileFor (doc);
                doc.name = wanted.toStdString();
                if (file == juce::File())
                    status = "Not in your library yet: SAVE first";
                else
                {
                    const auto target = file.getFileNameWithoutExtension() == juce::File::createLegalFileName (wanted) ? file : freeFile (wanted);
                    const bool ok = writeArpPattern (target.getFullPathName().toStdString(), doc, error) && (target == file || file.deleteFile());
                    status = ok ? "Renamed: " + target.getFileNameWithoutExtension() : "Could not rename: " + juce::String (error);
                }
            }
            else if (a == Action::duplicate)
            {
                doc.factory = false;
                doc.id = newArpPatternId();
                doc.name = (wanted.isEmpty() ? juce::String ("Pattern") : wanted).toStdString() + " copy";
                name->setText (juce::String (doc.name), false);
                const auto file = freeFile (juce::String (doc.name));
                status = writeArpPattern (file.getFullPathName().toStdString(), doc, error) ? "Duplicated: " + file.getFileNameWithoutExtension()
                                                                                             : "Could not duplicate: " + juce::String (error);
            }
            else
            {
                // Deleting a library entry never touches a session or preset: each keeps its own copy.
                const auto file = fileFor (doc);
                status = file == juce::File() ? juce::String ("Not in your library")
                                              : (file.deleteFile() ? "Deleted from your library (sessions and presets keep their copy)" : "Could not delete " + file.getFileName());
            }
            repaint();
            return;
        }
    }
    repaint();
}

void CustomPatternEditor::reinterpret()
{
    if (doc.capture.empty())
        return;
    ap::InterpretReport report;
    auto how = doc.interpretation;
    how.mapping = doc.pattern.mapping;
    auto next = ap::interpret (doc.capture, how, &report);
    next.mapping = doc.pattern.mapping;
    selection = next.events.empty() ? -1 : 0;
    commit (next, {});
    describe (report);
}

void CustomPatternEditor::describe (const ap::InterpretReport& r)
{
    juce::String s;
    s << r.placedNotes << " of " << r.capturedNotes << " notes";
    if (r.chordNotesDropped > 0)
        s << " · " << r.chordNotesDropped << " chord note" << (r.chordNotesDropped == 1 ? "" : "s") << " left out (the highest kept)";
    if (r.beyondLength > 0)
        s << " · " << r.beyondLength << " after the loop";
    if (r.beyondCapacity > 0)
        s << " · " << r.beyondCapacity << " past the 128-step limit (not placed)";
    status = s;
}

void CustomPatternEditor::useTake (const std::vector<ap::CapturedNote>& notes, double bpm)
{
    doc.capture = notes;
    doc.captureBpm = bpm;
    doc.interpretation.stepQuarters = stepQuarters();
    doc.interpretation.lengthQuarters = 0.0;
    if (notes.empty())
    {
        status = "Nothing was recorded";
        repaint();
        return;
    }
    reinterpret();
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
        status = "The take reached its limit (1024 notes) and stopped there · " + status;
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
        status = juce::String (p.name) + " · " + status;
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
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (keyArea (Action::import))),
                        [safe, apply] (int result) {
                            if (safe != nullptr && result > 0)
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
    history.push (doc.pattern);
    selection = doc.pattern.events.empty() ? -1 : 0;
    status = juce::String (doc.factory ? "Factory pattern: " : "Loaded: ") + juce::String (doc.name);
    if (previewOn)
        publishPreview();
    repaint();
}

void CustomPatternEditor::setLibraryFolderForTests (const juce::File& folder) { libraryOverride() = folder; }

juce::File CustomPatternEditor::libraryFolder()
{
    if (libraryOverride() != juce::File())
        return libraryOverride();
    // Product-neutral: ANDOR OSP and every ANDOR product share one pattern library.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ANDOR").getChildFile ("Patterns");
}

void CustomPatternEditor::openLibraryMenu()
{
    juce::PopupMenu menu, factory, yours;
    std::vector<ap::Document> entries;
    for (const auto& d : arppattern::factoryPatterns())
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
    menu.addSectionHeader ("LOAD");
    menu.addSubMenu ("Factory patterns", factory);
    menu.addSubMenu ("Your patterns", yours, yours.getNumItems() > 0);
    menu.addSeparator();
    constexpr int renameId = 10001, duplicateId = 10002, deleteId = 10003, revealId = 10004;
    menu.addItem (renameId, "Rename in library to \"" + name->getText().trim() + "\"", canPerform (Action::rename));
    menu.addItem (duplicateId, "Duplicate", canPerform (Action::duplicate));
    menu.addItem (deleteId, "Delete from library", canPerform (Action::deleteFromLibrary));
    menu.addSeparator();
    menu.addItem (revealId, "Show library folder");
    juce::Component::SafePointer<CustomPatternEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (keyArea (Action::library))),
                        [safe, entries] (int result) {
                            if (safe == nullptr || result <= 0)
                                return;
                            if (result == renameId)
                                safe->perform (Action::rename);
                            else if (result == duplicateId)
                                safe->perform (Action::duplicate);
                            else if (result == deleteId)
                                safe->perform (Action::deleteFromLibrary);
                            else if (result == revealId)
                            {
                                libraryFolder().createDirectory();
                                libraryFolder().startAsProcess();
                            }
                            else if (result <= static_cast<int> (entries.size()))
                                safe->loadDocument (entries[static_cast<std::size_t> (result - 1)]);
                        });
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
        status = "Recording: " + juce::String (static_cast<int> (c->notes().size())) + " notes (STOP when done)";
        repaint (recorded.expanded (2));
        repaint (statusRow);
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

//==============================================================================
// Layout and drawing

void CustomPatternEditor::layoutContent (juce::Rectangle<int> area)
{
    area = area.reduced (16, 0).withTrimmedTop (8).withTrimmedBottom (12);
    nameRow = area.removeFromTop (28);
    area.removeFromTop (8);
    recorded = area.removeFromTop (58);
    area.removeFromTop (8);
    statusRow = area.removeFromBottom (18);
    area.removeFromBottom (4);
    actionRow = area.removeFromBottom (28);
    area.removeFromBottom (8);
    editRow = area.removeFromBottom (28);
    area.removeFromBottom (8);
    grid = area;

    // The name, then SAVE and LIBRARY, then RECORD and IMPORT on the right.
    auto row = nameRow;
    auto place = [this] (Action a, juce::Rectangle<int> r) {
        for (auto& k : keys)
            if (k.action == a)
                k.area = r;
    };
    auto takeRight = [&row] (int w) {
        auto r = row.removeFromRight (w);
        row.removeFromRight (6);
        return r;
    };
    place (Action::import, takeRight (96));
    place (Action::record, takeRight (78));
    row.removeFromRight (10);
    place (Action::library, takeRight (72));
    place (Action::save, takeRight (54));
    name->setBounds (row);

    // Editing: SPLIT MERGE EXTEND REST DELETE | + STEP - STEP | REVERSE MIRROR ROTATE | LENGTH - + | UNDO REDO
    row = editRow;
    auto takeLeft = [&row] (int w, int gap = 4) {
        auto r = row.removeFromLeft (w);
        row.removeFromLeft (gap);
        return r;
    };
    place (Action::split, takeLeft (46, 3));
    place (Action::merge, takeLeft (50, 3));
    place (Action::extend, takeLeft (54, 3));
    place (Action::rest, takeLeft (42, 3));
    place (Action::remove, takeLeft (52, 10));
    place (Action::insertStep, takeLeft (48, 3));
    place (Action::deleteStep, takeLeft (48, 10));
    place (Action::reverse, takeLeft (58, 3));
    place (Action::mirror, takeLeft (52, 3));
    place (Action::rotate, takeLeft (52, 10));
    // UNDO and REDO at the right edge (REDO rightmost).
    {
        auto r = editRow;
        place (Action::redo, r.removeFromRight (44));
        r.removeFromRight (4);
        place (Action::undo, r.removeFromRight (44));
    }

    // Actions: PREVIEW | STEPS 1/2 1/4 1/8 | CONTOUR DEGREES | LENGTH - + ... CLEAR CANCEL USE PATTERN
    row = actionRow;
    place (Action::preview, takeLeft (64, 7));
    place (Action::timingSteps, takeLeft (42, 2));
    place (Action::timingHalf, takeLeft (30, 2));
    place (Action::timingQuarter, takeLeft (30, 2));
    place (Action::timingEighth, takeLeft (30, 7));
    place (Action::contour, takeLeft (58, 2));
    place (Action::degrees, takeLeft (58, 7));
    lengthCaption = takeLeft (44, 0);
    place (Action::shorter, takeLeft (22, 2));
    place (Action::longer, takeLeft (22, 4));
    place (Action::use, takeRight (92));
    place (Action::cancel, takeRight (58));
    place (Action::clear, takeRight (58));
}

juce::Rectangle<int> CustomPatternEditor::keyArea (Action a) const
{
    for (const auto& k : keys)
        if (k.action == a)
            return k.area;
    return {};
}

bool CustomPatternEditor::keyLit (Action a) const
{
    switch (a)
    {
        case Action::record: return recordingTake;
        case Action::preview: return previewOn;
        case Action::timingSteps: return doc.interpretation.timing == ap::Interpretation::Timing::steps;
        case Action::timingHalf: return doc.interpretation.timing == ap::Interpretation::Timing::rhythm && doc.interpretation.quantumTicks == arp::ticksPerStep / 2;
        case Action::timingQuarter: return doc.interpretation.timing == ap::Interpretation::Timing::rhythm && doc.interpretation.quantumTicks == arp::ticksPerStep / 4;
        case Action::timingEighth: return doc.interpretation.timing == ap::Interpretation::Timing::rhythm && doc.interpretation.quantumTicks == arp::ticksPerStep / 8;
        case Action::contour: return doc.pattern.mapping == arp::CustomMapping::contour;
        case Action::degrees: return doc.pattern.mapping == arp::CustomMapping::degrees;
        default: return false;
    }
}

void CustomPatternEditor::paint (juce::Graphics& g)
{
    MiniPanel::paint (g);
    using namespace design;
    // Captions.
    g.setFont (fonts::label (9.5f));
    g.setColour (colour::textMicro);
    auto caption = [&g] (const juce::String& text, juce::Rectangle<int> above, const juce::String& right = {}) {
        const auto r = juce::Rectangle<float> (static_cast<float> (above.getX()), static_cast<float> (above.getY()) - 13.0f, static_cast<float> (above.getWidth()), 12.0f);
        g.drawText (text, r, juce::Justification::centredLeft, false);
        if (right.isNotEmpty())
            g.drawText (right, r, juce::Justification::centredRight, false);
    };
    juce::String recordedRight;
    if (! doc.capture.empty())
        recordedRight << static_cast<int> (doc.capture.size()) << " notes · " << juce::roundToInt (doc.captureBpm) << " BPM";
    caption ("RECORDED (the take, for reference)", recorded.withY (recorded.getY() + 2), recordedRight);
    juce::String gridRight;
    if (! doc.pattern.events.empty())
        gridRight << stepsOf (doc.pattern) << " steps · " << doc.pattern.notes() << " notes · ranks 1-" << (std::max (doc.pattern.maxRank, doc.pattern.highestRank()) + 1)
                  << " · " << arp::customMappingName (doc.pattern.mapping);
    caption ("PATTERN (what plays: relative ranks on the step grid)", grid.withY (grid.getY() + 2), gridRight);

    paintRecorded (g, recorded.toFloat().withTrimmedTop (2.0f));
    paintGrid (g, grid.toFloat().withTrimmedTop (2.0f));
    paintKeys (g);

    // LENGTH, before its - and + keys.
    g.setFont (fonts::label (9.5f));
    g.setColour (colour::textSecondary);
    g.drawText ("LENGTH", lengthCaption, juce::Justification::centredLeft, false);

    g.setFont (fonts::make (11.0f));
    g.setColour (colour::textSecondary);
    g.drawText (status, statusRow, juce::Justification::centredLeft, true);
}

void CustomPatternEditor::paintKeys (juce::Graphics& g)
{
    using namespace design;
    for (const auto& k : keys)
    {
        if (k.area.isEmpty())
            continue;
        const auto r = k.area.toFloat().reduced (0.5f);
        const bool enabled = canPerform (k.action) || k.action == Action::cancel || k.action == Action::library || k.action == Action::import;
        const bool lit = keyLit (k.action);
        const bool primary = k.action == Action::use;
        juce::Colour top = colour::panelTop, bottom = colour::panelBottom, text = colour::text;
        if (lit || (primary && enabled))
        {
            top = k.action == Action::record ? juce::Colour (0xffd9532f) : colour::accentTop;
            bottom = k.action == Action::record ? juce::Colour (0xffb83d1f) : colour::accentBottom;
            text = juce::Colours::white;
        }
        g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (lit || primary ? juce::Colours::black.withAlpha (0.18f) : colour::edgeShade);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        g.setColour (text.withAlpha (enabled ? 0.9f : 0.35f));
        g.setFont (fonts::label (k.label.length() > 8 ? 9.5f : 10.0f));
        auto label = k.label;
        if (k.action == Action::record && recordingTake)
            label = "STOP";
        g.drawText (label, r, juce::Justification::centred, false);
    }
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
        g.setFont (fonts::label (10.0f));
        g.setColour (wellText.withAlpha (0.8f));
        g.drawText (recordingTake ? "Waiting for the first note..." : "No take: RECORD or IMPORT MIDI (or edit the pattern directly)", area, juce::Justification::centred, false);
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
        g.setColour (wellText.withAlpha (0.85f));
        g.fillRoundedRectangle (x0, y - 1.5f, std::max (2.0f, x1 - x0), 3.0f, 1.5f);
    }
}

juce::Rectangle<float> CustomPatternEditor::eventArea (int index) const
{
    const auto& p = doc.pattern;
    if (index < 0 || index >= static_cast<int> (p.events.size()))
        return {};
    const auto area = grid.toFloat().withTrimmedTop (2.0f).reduced (12.0f, 12.0f).withTrimmedLeft (18.0f);
    const int length = std::max (1, p.lengthTicks());
    const int ranks = std::max (p.maxRank, p.highestRank()) + 1;
    const float rowH = area.getHeight() / static_cast<float> (std::max (ranks, 4));
    const auto& e = p.events[static_cast<std::size_t> (index)];
    const float x0 = area.getX() + area.getWidth() * static_cast<float> (p.onsetOf (index)) / static_cast<float> (length);
    const float w = area.getWidth() * static_cast<float> (e.duration) / static_cast<float> (length);
    if (e.rest)
        return { x0 + 1.0f, area.getY(), std::max (3.0f, w - 2.0f), area.getHeight() };
    const float y = area.getBottom() - static_cast<float> (e.rank + 1) * rowH;
    return { x0 + 1.0f, y + 2.0f, std::max (3.0f, w - 2.0f), std::max (4.0f, rowH - 4.0f) };
}

void CustomPatternEditor::paintGrid (juce::Graphics& g, juce::Rectangle<float> r)
{
    using namespace design;
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (colour::wellRim);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    const auto& p = doc.pattern;
    const auto area = r.reduced (12.0f, 12.0f).withTrimmedLeft (18.0f);
    if (p.events.empty())
    {
        g.setFont (fonts::label (10.0f));
        g.setColour (wellText.withAlpha (0.8f));
        g.drawText ("The pattern appears here", area, juce::Justification::centred, false);
        return;
    }
    const int length = std::max (1, p.lengthTicks());
    const int ranks = std::max (p.maxRank, p.highestRank()) + 1;
    const int rows = std::max (ranks, 4);
    const float rowH = area.getHeight() / static_cast<float> (rows);
    // Rank rows (numbered 1 = the lowest note the gesture used) and step lines.
    g.setFont (fonts::label (8.5f));
    for (int k = 0; k < rows; ++k)
    {
        const float y = area.getBottom() - static_cast<float> (k + 1) * rowH;
        g.setColour (colour::wellGrid.withAlpha (k < ranks ? 0.9f : 0.4f));
        g.drawHorizontalLine (juce::roundToInt (y + rowH), area.getX(), area.getRight());
        if (k < ranks)
        {
            g.setColour (wellText.withAlpha (0.7f));
            g.drawText (juce::String (k + 1), juce::Rectangle<float> (area.getX() - 18.0f, y, 14.0f, rowH), juce::Justification::centredRight, false);
        }
    }
    const int steps = stepsOf (p);
    for (int s = 0; s <= steps; ++s)
    {
        const float x = area.getX() + area.getWidth() * static_cast<float> (s * arp::ticksPerStep) / static_cast<float> (length);
        if (x > area.getRight() + 0.5f)
            break;
        g.setColour (colour::wellGrid.withAlpha (s % 4 == 0 ? 1.0f : 0.55f));
        g.drawVerticalLine (juce::roundToInt (x), area.getY(), area.getBottom());
    }
    // Events: notes as bars on their rank, rests as a faint outline; the selection ringed.
    for (int i = 0; i < static_cast<int> (p.events.size()); ++i)
    {
        const auto box = eventArea (i);
        const auto& e = p.events[static_cast<std::size_t> (i)];
        const bool sel = i == selection;
        if (e.rest)
        {
            g.setColour (wellText.withAlpha (sel ? 0.5f : 0.18f));
            const float dashes[] { 3.0f, 3.0f };
            juce::Path outline;
            outline.addRoundedRectangle (box.reduced (1.0f, 6.0f), 3.0f);
            juce::Path dashed;
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
            continue;
        }
        g.setColour (noteColour.withAlpha (sel ? 1.0f : 0.82f));
        g.fillRoundedRectangle (box, 3.0f);
        if (sel)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawRoundedRectangle (box.expanded (1.5f), 4.0f, 1.5f);
        }
    }
}

int CustomPatternEditor::rankAt (float y) const
{
    const auto& p = doc.pattern;
    const auto area = grid.toFloat().withTrimmedTop (2.0f).reduced (12.0f, 12.0f);
    const int rows = std::max (std::max (p.maxRank, p.highestRank()) + 1, 4);
    const float rowH = area.getHeight() / static_cast<float> (rows);
    return juce::jlimit (0, ap::maxRankLimit, static_cast<int> ((area.getBottom() - y) / rowH));
}

int CustomPatternEditor::eventAt (juce::Point<float> pt) const
{
    for (int i = 0; i < static_cast<int> (doc.pattern.events.size()); ++i)
    {
        const auto box = eventArea (i);
        if (pt.x >= box.getX() - 1.0f && pt.x <= box.getRight() + 1.0f)
            return i;
    }
    return -1;
}

void CustomPatternEditor::mouseDown (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    for (const auto& k : keys)
        if (k.area.contains (pos))
        {
            perform (k.action);
            return;
        }
    if (grid.contains (pos))
    {
        grabKeyboardFocus();
        const int i = eventAt (e.position);
        if (i >= 0)
        {
            select (i);
            dragFrom = i;
            dragRank = rankAt (e.position.y);
            dragged = false;
        }
        return;
    }
    MiniPanel::mouseUp (e);
}

void CustomPatternEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0 || dragFrom >= static_cast<int> (doc.pattern.events.size()))
        return;
    const int rank = rankAt (e.position.y);
    if (rank != dragRank || doc.pattern.events[static_cast<std::size_t> (dragFrom)].rest)
    {
        // While dragging, the pattern follows (one undo step on release).
        dragRank = rank;
        dragged = true;
        auto& ev = doc.pattern.events[static_cast<std::size_t> (dragFrom)];
        ev.rest = false;
        ev.rank = rank;
        doc.pattern.maxRank = std::max (doc.pattern.maxRank, rank);
        repaint (grid);
    }
}

void CustomPatternEditor::mouseUp (const juce::MouseEvent& e)
{
    if (dragFrom >= 0 && dragged)
    {
        const auto next = doc.pattern;
        commit (next, "Rank " + juce::String (dragRank + 1));
    }
    dragFrom = -1;
    dragged = false;
    MiniPanel::mouseUp (e);
}

bool CustomPatternEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k.isKeyCode (juce::KeyPress::escapeKey))
        return false;   // the popover host closes it (and the destructor restores the card)
    if (mods.isCommandDown() && k.getKeyCode() == 'Z')
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
        perform (Action::remove);
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

} // namespace osp::plugin
