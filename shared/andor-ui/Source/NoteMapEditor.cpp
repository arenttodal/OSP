#include "NoteMapEditor.h"
#include "OspLookAndFeel.h"

#include "analysis/phrase/PhraseEdits.h"
#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>

namespace osp::plugin
{

namespace
{
    const juce::Colour light { 0xffeee8dd };
    const juce::Colour doubt { 0xffe0b45c };      ///< a doubt: quiet amber, never a percentage
    const juce::Colour cellOff { 0xff2b3031 };
    const juce::Colour cellRim { 0xff3e4446 };

    juce::String noteName (int key) { return juce::String (midiNoteName (std::clamp (key, 0, 127))); }

    juce::String centsText (double cents)
    {
        const int c = juce::roundToInt (cents);
        return (c > 0 ? "+" : (c < 0 ? juce::String::fromUTF8 ("\xe2\x88\x92") : juce::String())) + juce::String (std::abs (c)) + " c";
    }

    /** The settings a split or a merge re-describes notes with: the atlas's own. */
    phrase::AnalysisSettings settingsOf (const atlas::NoteAtlas& a)
    {
        phrase::AnalysisSettings s;
        s.detection = InstrumentHost::detectionOf (a);
        s.a4Hz = a.analysis.a4Hz > 0.0 ? a.analysis.a4Hz : 440.0;
        return s;
    }

    constexpr float edgeGrab = 5.0f;
    constexpr float rowHeight = 24.0f;
    constexpr float rowGap = 5.0f;
}

NoteMapEditor::NoteMapEditor (InstrumentHost& h, int l) : host (h), layer (l)
{
    setWantsKeyboardFocus (true);
    setTitle ("Note map " + InstrumentHost::layerName (layer));
    refresh();
}

//==============================================================================
double NoteMapEditor::sampleRate() const
{
    if (atlas != nullptr && ! atlas->sources.empty() && atlas->sources.front().sampleRate > 0.0)
        return atlas->sources.front().sampleRate;
    return 48000.0;
}

std::int64_t NoteMapEditor::sourceFrames() const
{
    if (atlas != nullptr && ! atlas->sources.empty() && atlas->sources.front().numFrames > 0)
        return atlas->sources.front().numFrames;
    return static_cast<std::int64_t> ((instrument != nullptr ? instrument->durationSeconds : 1.0) * sampleRate());
}

const atlas::NoteEvent* NoteMapEditor::note (const std::string& id) const
{
    return atlas != nullptr ? atlas->findNote (id) : nullptr;
}

std::vector<const atlas::NoteEvent*> NoteMapEditor::shownNotes() const
{
    std::vector<const atlas::NoteEvent*> notes;
    if (atlas == nullptr || atlas->sources.empty())
        return notes;
    for (const auto& n : atlas->notes)
        if (n.sourceId == atlas->sources.front().assetId)
            notes.push_back (&n);
    std::sort (notes.begin(), notes.end(), [] (const auto* a, const auto* b) { return a->attackStart < b->attackStart; });
    return notes;
}

std::vector<const atlas::NoteEvent*> NoteMapEditor::selectedNotes() const
{
    std::vector<const atlas::NoteEvent*> notes;
    for (const auto* n : shownNotes())
        if (std::find (selected.begin(), selected.end(), n->id) != selected.end())
            notes.push_back (n);
    return notes;
}

bool NoteMapEditor::isConflict (const std::string& id) const
{
    return std::find (conflicts.begin(), conflicts.end(), id) != conflicts.end();
}

void NoteMapEditor::select (std::vector<std::string> ids)
{
    selected = std::move (ids);
    anchor = selected.empty() ? std::string() : selected.front();
    rebuildInspector();
    repaint();
}

//==============================================================================
void NoteMapEditor::refresh()
{
    const auto now = host.currentInstrument (layer);
    const auto nowAtlas = now != nullptr ? now->atlas : nullptr;
    const auto nowActivity = host.atlasActivity (layer);
    const auto nowState = host.loadState (layer);
    auto nowConflicts = host.atlasConflicts (layer);
    const auto nowReport = host.atlasReport (layer);
    const bool nowLoop = host.parameterValue (InstrumentHost::layerParameterId (layer, "loop")) >= 0.5f;
    const bool changedAtlas = nowAtlas != atlas;
    const bool changedStage = now != instrument;
    if (! changedAtlas && ! changedStage && nowActivity == activity && nowState == loadState && nowConflicts == conflicts && nowReport == report
        && nowLoop == loopOn)
        return;
    instrument = now;
    atlas = nowAtlas;
    activity = nowActivity;
    loadState = nowState;
    conflicts = std::move (nowConflicts);
    report = nowReport;
    loopOn = nowLoop;
    if (changedAtlas)
    {
        pending.reset();   // the edited atlas is here: it shows the moved boundary itself
        // The selection follows the notes that still exist (an edit keeps ids).
        selected.erase (std::remove_if (selected.begin(), selected.end(), [this] (const std::string& id) { return note (id) == nullptr; }), selected.end());
        if (! viewSet || view.getEnd() > sourceFrames())
            defaultView();
    }
    rebuildInspector();
    repaint();
}

void NoteMapEditor::defaultView()
{
    // The notes, with a little air at both ends (the recording's silences left out).
    const auto notes = shownNotes();
    const auto total = std::max<std::int64_t> (1, sourceFrames());
    if (notes.empty())
    {
        view = { 0, total };
        return;
    }
    std::int64_t first = notes.front()->attackStart, last = 0;
    for (const auto* n : notes)
        last = std::max (last, n->sourceEnd);
    const auto margin = std::max<std::int64_t> (static_cast<std::int64_t> (0.05 * sampleRate()), (last - first) / 40);
    view = { std::max<std::int64_t> (0, first - margin), std::min (total, last + margin) };
    if (view.getLength() < 64)
        view = { 0, total };
}

void NoteMapEditor::setViewRange (std::int64_t start, std::int64_t end)
{
    const auto total = std::max<std::int64_t> (1, sourceFrames());
    const auto minimum = std::min<std::int64_t> (total, static_cast<std::int64_t> (0.02 * sampleRate()));
    auto length = std::clamp<std::int64_t> (end - start, minimum, total);
    start = std::clamp<std::int64_t> (start, 0, total - length);
    view = { start, start + length };
    viewSet = true;
    repaint();
}

//==============================================================================
juce::Rectangle<float> NoteMapEditor::graphArea() const
{
    auto r = getLocalBounds().toFloat().withTrimmedTop (42.0f).reduced (12.0f, 0.0f).withTrimmedBottom (12.0f);
    r.setRight (inspector.getX() - 12.0f);
    return r;
}

float NoteMapEditor::xForFrame (std::int64_t frame) const
{
    const auto g = graphArea();
    return g.getX() + g.getWidth() * static_cast<float> (static_cast<double> (frame - view.getStart()) / std::max<double> (1.0, static_cast<double> (view.getLength())));
}

std::int64_t NoteMapEditor::frameForX (float x) const
{
    const auto g = graphArea();
    return view.getStart() + static_cast<std::int64_t> (std::llround (static_cast<double> ((x - g.getX()) / std::max (1.0f, g.getWidth())) * static_cast<double> (view.getLength())));
}

juce::Rectangle<float> NoteMapEditor::regionBounds (const std::string& id) const
{
    const auto* n = note (id);
    if (n == nullptr)
        return {};
    const auto g = graphArea();
    auto start = n->attackStart, end = n->sourceEnd;
    if (pending && pending->noteId == id)
        (pending->edge == Edge::start ? start : end) = pending->frame;
    if (pressed.noteId == id && pressed.edge != Edge::none && dragged)
        (pressed.edge == Edge::start ? start : end) = dragFrame;
    const float x0 = std::max (g.getX(), xForFrame (start)), x1 = std::min (g.getRight(), xForFrame (end));
    if (x1 <= x0)
        return {};
    return { x0, g.getY() + 18.0f, x1 - x0, g.getHeight() - 18.0f };
}

std::vector<std::int64_t> NoteMapEditor::repeatMarkers (const std::string& id) const
{
    std::vector<std::int64_t> frames;
    if (const auto* n = note (id); n != nullptr && n->diagnostics.possibleRepeat)
        for (const auto f : n->diagnostics.repeatFrames)
            if (f > n->attackStart && f < n->sourceEnd)
                frames.push_back (f);
    return frames;
}

NoteMapEditor::Hit NoteMapEditor::hitTest (juce::Point<float> p) const
{
    Hit hit;
    if (! graphArea().contains (p))
        return hit;
    const auto notes = shownNotes();
    // Edges first, the selected note's before the others (neighbours' edges touch).
    auto edgeOf = [&] (const atlas::NoteEvent& n) {
        const auto r = regionBounds (n.id);
        if (r.isEmpty() || p.y < r.getY() || p.y > r.getBottom())
            return Edge::none;
        if (std::abs (p.x - r.getX()) <= edgeGrab)
            return Edge::start;
        if (std::abs (p.x - r.getRight()) <= edgeGrab)
            return Edge::end;
        return Edge::none;
    };
    for (const auto* n : selectedNotes())
        if (const auto e = edgeOf (*n); e != Edge::none)
            return { n->id, e, std::nullopt };
    for (const auto* n : notes)
    {
        const auto r = regionBounds (n->id);
        if (r.isEmpty())
            continue;
        // A repeat marker's handle (the small tab at the region's top).
        for (const auto f : repeatMarkers (n->id))
            if (std::abs (p.x - xForFrame (f)) <= 6.0f && p.y >= r.getY() - 2.0f && p.y <= r.getY() + 16.0f)
                return { n->id, Edge::none, f };
        if (const auto e = edgeOf (*n); e != Edge::none)
            return { n->id, e, std::nullopt };
    }
    for (const auto* n : notes)
        if (regionBounds (n->id).contains (p))
            return { n->id, Edge::none, std::nullopt };
    return hit;
}

//==============================================================================
void NoteMapEditor::edit (const juce::String& name, InstrumentHost::AtlasEdit change)
{
    host.editAtlas (layer, name, std::move (change));
    refresh();
}

void NoteMapEditor::audition (const std::vector<const atlas::NoteEvent*>& notes)
{
    if (notes.empty())
        return;
    std::int64_t start = notes.front()->attackStart, end = notes.front()->sourceEnd;
    for (const auto* n : notes)
    {
        start = std::min (start, n->attackStart);
        end = std::max (end, n->sourceEnd);
    }
    host.auditionAtlasRegion (layer, start, end);
}

void NoteMapEditor::split (const std::string& id, std::int64_t frame)
{
    if (atlas == nullptr || note (id) == nullptr)
        return;
    const auto newId = atlas->nextNoteId();
    edit ("Split note", [id, frame] (atlas::NoteAtlas& a, const AudioData& audio) {
        return ! phrase::edit::split (a, id, frame, audio, settingsOf (a)).empty();
    });
    select ({ id, newId });
}

void NoteMapEditor::mergeSelection()
{
    auto notes = selectedNotes();
    const auto all = shownNotes();
    if (notes.size() == 1)
    {
        // One note: with the next one.
        const auto it = std::find (all.begin(), all.end(), notes.front());
        if (it == all.end() || it + 1 == all.end())
            return;
        notes.push_back (*(it + 1));
    }
    if (notes.size() < 2)
        return;
    // Neighbours only: every note between the first and the last selected must be selected.
    const auto first = std::find (all.begin(), all.end(), notes.front()), last = std::find (all.begin(), all.end(), notes.back());
    if (static_cast<std::size_t> (std::distance (first, last)) + 1 != notes.size())
        return;
    const auto keep = notes.front()->id;
    const int count = static_cast<int> (notes.size());
    edit ("Merge notes", [keep, count] (atlas::NoteAtlas& a, const AudioData& audio) {
        bool changed = false;
        for (int i = 1; i < count; ++i)
            changed = phrase::edit::mergeWithNext (a, keep, audio, settingsOf (a)) || changed;
        return changed;
    });
    select ({ keep });
}

void NoteMapEditor::commitBoundary()
{
    const auto* n = note (pressed.noteId);
    if (n == nullptr || pressed.edge == Edge::none)
        return;
    const auto marker = pressed.edge == Edge::start ? phrase::edit::Marker::attackStart : phrase::edit::Marker::end;
    if ((pressed.edge == Edge::start ? n->attackStart : n->sourceEnd) == dragFrame)
        return;
    const auto id = n->id;
    const auto frame = dragFrame;
    pending = Pending { id, pressed.edge, frame };
    edit (pressed.edge == Edge::start ? "Move note start" : "Move note end", [id, marker, frame] (atlas::NoteAtlas& a, const AudioData& audio) {
        if (! phrase::edit::moveMarker (a, id, marker, frame))
            return false;
        // Its pitch and body follow the new range (a correction of its pitch stays).
        if (auto* moved = a.findNote (id); moved != nullptr && ! moved->corrections.pitch)
        {
            const auto corrections = moved->corrections;
            phrase::edit::reanalyse (a, id, audio, settingsOf (a));
            if (auto* again = a.findNote (id))
                again->corrections = corrections;
        }
        return true;
    });
}

//==============================================================================
bool NoteMapEditor::perform (const juce::String& action)
{
    bool offered = false;
    for (const auto& row : rows)
        for (const auto& c : row.cells)
            offered = offered || (c.action == action && c.enabled && ! c.value);
    if (! offered || atlas == nullptr)
        return false;
    const auto notes = selectedNotes();
    std::vector<std::string> ids;
    for (const auto* n : notes)
        ids.push_back (n->id);
    const auto* first = notes.empty() ? nullptr : notes.front();

    auto shiftRoot = [&] (int semitones, const juce::String& name) {
        if (first == nullptr)
            return;
        const int key = std::clamp (first->rootMidi + semitones, 0, 127);
        const auto id = first->id;
        edit (name + " " + noteName (first->rootMidi) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + noteName (key),
              [id, key] (atlas::NoteAtlas& a, const AudioData&) { return phrase::edit::setRoot (a, id, key); });
    };
    if (action == "pitch+" || action == "pitch-")
        shiftRoot (action == "pitch+" ? 1 : -1, "Pitch");
    else if (action == "oct+" || action == "oct-")
        shiftRoot (action == "oct+" ? 12 : -12, "Octave");
    else if ((action == "fine+" || action == "fine-") && first != nullptr)
    {
        // Fine tune: where the note really is (5 cents a step); in tune, it is played corrected.
        const double step = action == "fine+" ? 0.05 : -0.05;
        const auto id = first->id;
        edit ("Fine tune", [id, step] (atlas::NoteAtlas& a, const AudioData&) {
            auto* n = a.findNote (id);
            if (n == nullptr)
                return false;
            n->midiPitch = std::clamp (n->midiPitch + step, n->rootMidi - 0.5, n->rootMidi + 0.49);
            n->pitchHz = 440.0 * std::pow (2.0, (n->midiPitch - 69.0) / 12.0);
            n->corrections.pitch = true;
            atlas::remapNotes (a);
            return true;
        });
    }
    else if (action == "key+" || action == "key-" || action == "key.auto")
    {
        if (first == nullptr)
            return false;
        const int key = action == "key.auto" ? -1 : std::clamp (first->playKey() + (action == "key+" ? 1 : -1), 0, 127);
        edit (key < 0 ? juce::String ("Play on own key") : "Play on " + noteName (key), [ids, key] (atlas::NoteAtlas& a, const AudioData&) {
            bool changed = false;
            for (const auto& id : ids)
                changed = phrase::edit::assignKey (a, id, key) || changed;
            return changed;
        });
    }
    else if (action == "layer+" || action == "layer-")
    {
        const int delta = action == "layer+" ? 1 : -1;
        edit ("Velocity layer", [ids, delta] (atlas::NoteAtlas& a, const AudioData&) {
            bool changed = false;
            for (const auto& id : ids)
                if (auto* n = a.findNote (id); n != nullptr)
                {
                    const int next = std::clamp (n->velocityLayer + delta, 0, 15);
                    changed = changed || next != n->velocityLayer;
                    n->velocityLayer = next;
                    n->corrections.velocity = true;
                }
            atlas::remapNotes (a);
            return changed;
        });
    }
    else if (action == "include")
    {
        const bool include = first != nullptr && ! first->included;
        edit (include ? "Include note" : "Leave note out", [ids, include] (atlas::NoteAtlas& a, const AudioData&) {
            bool changed = false;
            for (const auto& id : ids)
                changed = phrase::edit::setIncluded (a, id, include) || changed;
            return changed;
        });
    }
    else if (action == "audition")
        audition (notes);
    else if (action == "split" && first != nullptr)
    {
        const auto markers = repeatMarkers (first->id);
        split (first->id, markers.empty() ? (first->attackStart + first->sourceEnd) / 2 : markers.front());
    }
    else if (action == "merge")
        mergeSelection();
    else if (action == "repeat.split" && first != nullptr)
    {
        const auto markers = repeatMarkers (first->id);
        if (markers.empty())
            return false;
        split (first->id, markers.front());
    }
    else if (action == "repeat.keep" && first != nullptr)
    {
        // One note after all: the doubt is answered (a re-detection may raise it again only
        // for a note the musician has not touched).
        const auto id = first->id;
        edit ("Keep as one note", [id] (atlas::NoteAtlas& a, const AudioData&) {
            auto* n = a.findNote (id);
            if (n == nullptr || ! n->diagnostics.possibleRepeat)
                return false;
            n->diagnostics.possibleRepeat = false;
            n->diagnostics.repeatFrames.clear();
            n->corrections.boundaries = true;
            return true;
        });
    }
    else if (action == "conflict.keep" && first != nullptr)
    {
        host.dismissAtlasConflict (layer, first->id);
        refresh();
    }
    else if (action == "conflict.redescribe" && first != nullptr)
    {
        const auto id = first->id;
        host.dismissAtlasConflict (layer, id);
        edit ("Re-describe note", [id] (atlas::NoteAtlas& a, const AudioData& audio) {
            auto* n = a.findNote (id);
            if (n == nullptr)
                return false;
            const bool included = n->included;
            n->corrections = {};
            n->assignedKey = -1;
            if (! phrase::edit::reanalyse (a, id, audio, settingsOf (a)))
                return false;
            if (auto* again = a.findNote (id))
                again->included = included;
            return true;
        });
    }
    else if (action.startsWith ("detect."))
    {
        const auto detection = action == "detect.fewer" ? phrase::Detection::fewer : (action == "detect.more" ? phrase::Detection::more : phrase::Detection::normal);
        host.mapNotes (layer, detection);
        refresh();
    }
    else if (action == "fill" || action == "fill+" || action == "fill-" || action == "tuning")
    {
        auto mapping = atlas->mapping;
        if (action == "fill")
            mapping.fill = ! mapping.fill;
        else if (action == "tuning")
            mapping.correctTuning = ! mapping.correctTuning;
        else
            mapping.maxTransposeSemitones = std::clamp (mapping.maxTransposeSemitones + (action == "fill+" ? 1.0 : -1.0), 1.0, 48.0);
        if (mapping == atlas->mapping)
            return false;
        const auto name = action == "tuning" ? juce::String (mapping.correctTuning ? "Play in tune" : "Play as recorded")
                                             : juce::String ("Fill keyboard");
        edit (name, [mapping] (atlas::NoteAtlas& a, const AudioData&) {
            if (a.mapping == mapping)
                return false;
            a.mapping = mapping;
            return true;
        });
    }
    else
        return false;
    return true;
}

juce::StringArray NoteMapEditor::actions() const
{
    juce::StringArray names;
    for (const auto& row : rows)
        for (const auto& c : row.cells)
            if (! c.value && c.enabled)
                names.add (c.action);
    return names;
}

juce::Rectangle<float> NoteMapEditor::actionArea (const juce::String& action) const
{
    for (const auto& row : rows)
        for (const auto& c : row.cells)
            if (c.action == action)
                return c.area;
    return {};
}

//==============================================================================
juce::String NoteMapEditor::statusText() const
{
    return statusOf (host, layer);
}

juce::String NoteMapEditor::statusOf (const InstrumentHost& host, int layer)
{
    const auto instrument = host.currentInstrument (layer);
    const auto atlas = instrument != nullptr ? instrument->atlas : nullptr;
    switch (host.atlasActivity (layer))
    {
        case InstrumentHost::AtlasActivity::analysing: return "Analyzing";
        case InstrumentHost::AtlasActivity::preparing: return "Preparing";
        case InstrumentHost::AtlasActivity::enhancing: return "Playable " + juce::String::fromUTF8 ("\xc2\xb7") + " enhancing";
        case InstrumentHost::AtlasActivity::none: break;
    }
    if (atlas == nullptr)
        return host.loadState (layer) == InstrumentHost::LoadState::failed ? "Failed" : juce::String();
    if (instrument->atlasStatus != nullptr && instrument->atlasStatus->playableOnly)
        return "Playable";
    const auto conflicts = host.atlasConflicts (layer);
    bool review = ! conflicts.empty() || atlas->analysis.suitability == "unsuitable";
    for (const auto& n : atlas->notes)
        review = review || (n.included && n.diagnostics.any() && ! n.corrections.any());
    return review ? "Needs review" : "Ready";
}

std::array<OspKeyboard::KeyTint, 128> NoteMapEditor::keyMap (const atlas::NoteAtlas& a, juce::Colour colour)
{
    std::array<OspKeyboard::KeyTint, 128> keys {};
    const auto byKey = atlas::notesByKey (a);
    std::vector<int> recorded;
    for (int k = 0; k < 128; ++k)
    {
        const auto& notes = byKey[static_cast<std::size_t> (k)];
        if (notes.empty())
            continue;
        recorded.push_back (k);
        std::array<int, 16> perLayer {};
        for (const int i : notes)
            ++perLayer[static_cast<std::size_t> (std::clamp (a.notes[static_cast<std::size_t> (i)].velocityLayer, 0, 15))];
        keys[static_cast<std::size_t> (k)] = { OspKeyboard::KeyTint::State::recorded, *std::max_element (perLayer.begin(), perLayer.end()), colour };
    }
    if (recorded.empty())
        return keys;
    for (int k = 0; k < 128; ++k)
    {
        auto& key = keys[static_cast<std::size_t> (k)];
        if (key.state == OspKeyboard::KeyTint::State::recorded)
            continue;
        int nearest = 128;
        for (const int r : recorded)
            nearest = std::min (nearest, std::abs (r - k));
        const bool reached = a.mapping.fill && nearest <= static_cast<int> (std::floor (a.mapping.maxTransposeSemitones + 1.0e-9));
        key = { reached ? OspKeyboard::KeyTint::State::filled : OspKeyboard::KeyTint::State::silent, 0, colour };
    }
    return keys;
}

juce::String NoteMapEditor::doubtText (const atlas::NoteEvent& n) const
{
    juce::StringArray words;
    const auto& d = n.diagnostics;
    if (isConflict (n.id))
        words.add ("the new detection hears this differently");
    if (d.possibleOctaveError)
        words.add ("maybe an octave off");
    else if (d.uncertainPitch)
        words.add ("pitch uncertain");
    if (d.possibleRepeat)
        words.add ("maybe two notes");
    if (d.possibleSplit)
        words.add ("maybe one note with its neighbour");
    if (d.uncertainBoundary)
        words.add ("boundary uncertain");
    if (d.unvoiced)
        words.add ("mostly breath or noise");
    if (d.quietAlternate)
        words.add ("a quieter take: left off the keyboard");
    return words.joinIntoString (juce::String::fromUTF8 (" \xc2\xb7 "));
}

juce::String NoteMapEditor::sustainText (const atlas::NoteEvent& n) const
{
    // What the instrument does with the note when it is held (the existing policies, no new DSP).
    if (! n.included)
        return "Left out: not on the keyboard";
    if (! loopOn)
        return "LOOP is off: plays as recorded";
    if (host.parameterValue (InstrumentHost::layerParameterId (layer, "sourceMode")) >= 0.5f)
        return "Granular: grains hold it from its own region";
    if (instrument == nullptr || instrument->set == nullptr)
        return {};
    if (instrument->atlasStatus != nullptr && instrument->atlasStatus->playableOnly)
        return "Sustain being prepared: plays as recorded meanwhile";
    for (const auto& m : instrument->set->members)
        if (m.regionId == n.id && m.model != nullptr)
        {
            const auto& c = m.model->original.continuation;
            if (c.canSustain)
                return "Sustains from its stable body";
            if (c.reason == "stable region too short")
                return "Plays as recorded: under 0.3 s of stable sound to sustain from";
            return "Plays as recorded: " + juce::String (c.reason);
        }
    if (instrument->atlasStatus != nullptr && std::find (instrument->atlasStatus->skippedNoteIds.begin(), instrument->atlasStatus->skippedNoteIds.end(), n.id)
                                                  != instrument->atlasStatus->skippedNoteIds.end())
        return "Not loaded: the layer's memory is full";
    return {};
}

//==============================================================================
void NoteMapEditor::rebuildInspector()
{
    rows.clear();
    if (atlas == nullptr)
    {
        layoutRows();
        return;
    }
    auto cell = [] (juce::String action, juce::String text, bool on = false, bool enabled = true) {
        Cell c;
        c.action = std::move (action);
        c.text = std::move (text);
        c.on = on;
        c.enabled = enabled;
        return c;
    };
    auto valueCell = [] (juce::String text) {
        Cell c;
        c.text = std::move (text);
        c.value = true;
        return c;
    };
    const juce::String minus = juce::String::fromUTF8 ("\xe2\x88\x92");
    const bool narrow = inspector.getWidth() < 200.0f;
    const auto notes = selectedNotes();
    const auto all = shownNotes();

    if (notes.size() == 1)
    {
        const auto& n = *notes.front();
        rows.push_back ({ "PITCH", { cell ("pitch-", minus), cell ("pitch+", "+"), valueCell ("OCT"), cell ("oct-", minus), cell ("oct+", "+") } });
        rows.push_back ({ "FINE", { cell ("fine-", minus), valueCell (centsText (n.centsOffset)), cell ("fine+", "+") } });
        rows.push_back ({ narrow ? "KEY" : "PLAYS ON", { cell ("key-", minus), valueCell (noteName (n.playKey())), cell ("key+", "+"),
                                                          cell ("key.auto", "AUTO", n.assignedKey < 0) } });
        rows.push_back ({ narrow ? "VEL" : "VEL LAYER", { cell ("layer-", minus, false, n.velocityLayer > 0), valueCell (juce::String (n.velocityLayer + 1)),
                                                          cell ("layer+", "+", false, n.velocityLayer < 15) } });
        rows.push_back ({ {}, { cell ("include", n.included ? "INCLUDED" : "LEFT OUT", n.included), cell ("audition", "AUDITION") } });
        const auto it = std::find (all.begin(), all.end(), notes.front());
        rows.push_back ({ {}, { cell ("split", "SPLIT"), cell ("merge", narrow ? "MERGE" : juce::String::fromUTF8 ("MERGE \xe2\x86\x92"), false, it != all.end() && it + 1 != all.end()) } });
        if (! repeatMarkers (n.id).empty())
            rows.push_back ({ narrow ? "TWO?" : "TWO NOTES?", { cell ("repeat.split", "SPLIT"), cell ("repeat.keep", "KEEP ONE") } });
        if (isConflict (n.id))
            rows.push_back ({ narrow ? "NEW" : "DETECTION", { cell ("conflict.keep", "KEEP MINE"), cell ("conflict.redescribe", narrow ? "NEW" : "USE NEW") } });
        Row sustain;
        sustain.note = sustainText (n);
        rows.push_back (sustain);
    }
    else if (notes.size() > 1)
    {
        const auto& n = *notes.front();
        rows.push_back ({ narrow ? "KEY" : "PLAYS ON", { cell ("key-", minus), valueCell (noteName (n.playKey())), cell ("key+", "+"), cell ("key.auto", "AUTO") } });
        rows.push_back ({ {}, { cell ("include", n.included ? "INCLUDED" : "LEFT OUT", n.included), cell ("audition", "AUDITION") } });
        const auto first = std::find (all.begin(), all.end(), notes.front()), last = std::find (all.begin(), all.end(), notes.back());
        const bool adjacent = static_cast<std::size_t> (std::distance (first, last)) + 1 == notes.size();
        rows.push_back ({ {}, { cell ("merge", "MERGE", false, adjacent), cell ("layer+", "LOUDER"), cell ("layer-", "SOFTER") } });
        Row hint;
        hint.note = "PLAYS ON puts them on one key: its takes, in turn (round robin)";
        rows.push_back (hint);
    }
    else
    {
        const auto detection = InstrumentHost::detectionOf (*atlas);
        const bool busy = activity != InstrumentHost::AtlasActivity::none;
        rows.push_back ({ narrow ? "DETECT" : "DETECTION", { cell ("detect.fewer", "FEWER", detection == phrase::Detection::fewer, ! busy),
                                                             cell ("detect.normal", narrow ? "NORM" : "NORMAL", detection == phrase::Detection::normal, ! busy),
                                                             cell ("detect.more", "MORE", detection == phrase::Detection::more, ! busy) } });
        const auto& m = atlas->mapping;
        rows.push_back ({ "FILL", { cell ("fill", m.fill ? "ON" : "OFF", m.fill), cell ("fill-", minus, false, m.fill),
                                    valueCell (juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (m.maxTransposeSemitones)) + " st"),
                                    cell ("fill+", "+", false, m.fill) } });
        rows.push_back ({ narrow ? "TUNE" : "TUNING", { cell ("tuning", m.correctTuning ? "IN TUNE" : "AS PLAYED", m.correctTuning) } });
        Row sustain;
        int sustaining = 0, included = 0;
        for (const auto* n : all)
            if (n->included)
            {
                ++included;
                sustaining += sustainText (*n).startsWith ("Sustains") ? 1 : 0;
            }
        sustain.note = ! loopOn ? juce::String ("LOOP is off: notes play as recorded")
                                : juce::String (sustaining) + " of " + juce::String (included) + " notes sustain (0.3 s of stable sound needed)";
        rows.push_back (sustain);
        Row verdict;
        if (atlas->analysis.suitability == "unsuitable")
            verdict.note = "Mostly unpitched, chords or noise: these notes are unreliable";
        else if (atlas->analysis.suitability == "silent")
            verdict.note = "Nothing to map: the recording is (almost) silent";
        else if (instrument != nullptr && instrument->atlasStatus != nullptr && instrument->atlasStatus->partial)
            verdict.note = juce::String (static_cast<int> (instrument->atlasStatus->skippedNoteIds.size())) + " notes not loaded: the layer's memory is full";
        else if (report.isNotEmpty())
            verdict.note = report;
        else
            verdict.note = narrow ? "Click a note to edit it" : "Click a note to edit it, double-click to split it";
        rows.push_back (verdict);
    }
    layoutRows();
}

void NoteMapEditor::layoutRows()
{
    // The inspector's rows: a caption at the left, the cells sharing the rest.
    const auto box = inspector.reduced (12.0f, 0.0f);
    const bool narrow = inspector.getWidth() < 200.0f;
    const float captionWidth = narrow ? 44.0f : 74.0f;
    // Rows shrink to fit a short page (three layers): every control stays reachable.
    int cellRows = 0, noteRows = 0;
    for (const auto& row : rows)
        (row.cells.empty() ? noteRows : cellRows) += 1;
    const float room = inspector.getBottom() - 8.0f - (inspector.getY() + 50.0f);
    const float needed = static_cast<float> (cellRows) * (rowHeight + rowGap) + static_cast<float> (noteRows) * (34.0f + rowGap);
    const float k = needed > room ? std::max (0.68f, room / std::max (1.0f, needed)) : 1.0f;
    const float cellHeight = rowHeight * k, gapHeight = rowGap * k, noteHeight = 34.0f * k;
    float y = inspector.getY() + 50.0f;
    for (auto& row : rows)
    {
        row.y = y;
        if (row.cells.empty())
        {
            row.height = noteHeight;
            y += row.height + gapHeight;
            continue;
        }
        row.height = cellHeight;
        auto area = juce::Rectangle<float> (box.getX(), y, box.getWidth(), cellHeight);
        if (row.label.isNotEmpty())
            area.removeFromLeft (captionWidth);
        float units = 0.0f;
        for (const auto& c : row.cells)
            units += c.value ? 1.4f : (c.text.length() <= 1 ? 0.8f : 1.6f);
        const float gap = 4.0f;
        const float unit = (area.getWidth() - gap * static_cast<float> (row.cells.size() - 1)) / std::max (1.0f, units);
        float x = area.getX();
        for (auto& c : row.cells)
        {
            const float w = unit * (c.value ? 1.4f : (c.text.length() <= 1 ? 0.8f : 1.6f));
            c.area = { x, y, w, cellHeight };
            x += w + gap;
        }
        y += cellHeight + gapHeight;
    }
}

void NoteMapEditor::resized()
{
    const float w = static_cast<float> (getWidth());
    const float inspectorWidth = w > 1000.0f ? 330.0f : (w > 600.0f ? 250.0f : 176.0f);
    inspector = { w - inspectorWidth - 8.0f, 42.0f, inspectorWidth, static_cast<float> (getHeight()) - 50.0f };
    // The top row: EQ | DAMPEN | NOTES, the state, undo / redo, close.
    eqTab = { 12.0f, 9.0f, 34.0f, 24.0f };
    dampenTab = { 48.0f, 9.0f, 64.0f, 24.0f };
    titleArea = { 120.0f, 8.0f, 90.0f, 26.0f };
    closeArea = { w - 32.0f, 10.0f, 22.0f, 22.0f };
    redoArea = { closeArea.getX() - 34.0f, 10.0f, 22.0f, 22.0f };
    undoArea = { redoArea.getX() - 26.0f, 10.0f, 22.0f, 22.0f };
    statusArea = { titleArea.getRight() + 8.0f, 9.0f, std::max (0.0f, undoArea.getX() - 12.0f - titleArea.getRight() - 8.0f), 24.0f };
    if (! viewSet)
        defaultView();
    rebuildInspector();
}

//==============================================================================
void NoteMapEditor::paintWave (juce::Graphics& g, juce::Rectangle<float> graph)
{
    using namespace design;
    if (instrument == nullptr || instrument->peakMax.empty() || instrument->durationSeconds <= 0.0)
        return;
    const auto& hi = instrument->peakMax;
    const auto& lo = instrument->peakMin;
    float maxAbs = 1.0e-4f;
    for (std::size_t i = 0; i < hi.size(); ++i)
        maxAbs = std::max ({ maxAbs, hi[i], -lo[i] });
    const double size = static_cast<double> (hi.size());
    const double framesPerBucket = static_cast<double> (sourceFrames()) / size;
    const float mid = graph.getY() + 18.0f + 0.5f * (graph.getHeight() - 18.0f);
    const float half = 0.45f * (graph.getHeight() - 18.0f);
    const int columns = std::max (1, juce::roundToInt (graph.getWidth()));
    g.setColour (colour::identity (layer).wave.withAlpha (0.42f));
    for (int c = 0; c < columns; ++c)
    {
        const auto f0 = static_cast<double> (view.getStart()) + static_cast<double> (view.getLength()) * c / columns;
        const auto f1 = static_cast<double> (view.getStart()) + static_cast<double> (view.getLength()) * (c + 1) / columns;
        auto b0 = static_cast<std::size_t> (std::clamp (f0 / framesPerBucket, 0.0, size - 1.0));
        auto b1 = static_cast<std::size_t> (std::clamp (f1 / framesPerBucket, static_cast<double> (b0) + 1.0, size));
        float top = 0.0f, bottom = 0.0f;
        for (auto b = b0; b < b1; ++b)
        {
            top = std::max (top, hi[b]);
            bottom = std::min (bottom, lo[b]);
        }
        auto shape = [maxAbs, half] (float a) { return half * std::pow (std::clamp (std::abs (a) / maxAbs, 0.0f, 1.0f), 0.85f); };
        const float x = graph.getX() + static_cast<float> (c);
        g.fillRect (juce::Rectangle<float> (x, mid - shape (top), 1.0f, std::max (1.0f, shape (top) + shape (bottom))));
    }
}

void NoteMapEditor::paintRegions (juce::Graphics& g, juce::Rectangle<float> graph)
{
    using namespace design;
    const auto& id = colour::identity (layer);
    const auto tone = id.wave;
    g.saveState();
    g.reduceClipRegion (graph.getSmallestIntegerContainer());
    for (const auto* n : shownNotes())
    {
        const auto r = regionBounds (n->id);
        if (r.isEmpty())
            continue;
        const bool isSelected = std::find (selected.begin(), selected.end(), n->id) != selected.end();
        const bool hot = hover.noteId == n->id;
        const auto box = r.reduced (1.0f, 0.0f);
        if (n->included)
        {
            g.setColour (tone.withAlpha (isSelected ? 0.20f : (hot ? 0.11f : 0.07f)));
            g.fillRoundedRectangle (box, 5.0f);
            g.setColour (tone.withAlpha (isSelected ? 0.95f : 0.48f));
            g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, isSelected ? 1.6f : 1.0f);
            // The sustained body: a quiet bar along the foot.
            const float s0 = std::max (box.getX() + 3.0f, xForFrame (n->sustain.start)), s1 = std::min (box.getRight() - 3.0f, xForFrame (n->sustain.end));
            if (s1 > s0 + 1.0f)
            {
                g.setColour (tone.withAlpha (isSelected ? 0.6f : 0.32f));
                g.fillRoundedRectangle ({ s0, box.getBottom() - 6.0f, s1 - s0, 3.0f }, 1.5f);
            }
        }
        else
        {
            // Left out: a dashed outline, no fill.
            juce::Path outline, dashed;
            outline.addRoundedRectangle (box.reduced (0.5f), 5.0f);
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.setColour (colour::wellText.withAlpha (isSelected ? 0.9f : 0.5f));
            g.fillPath (dashed);
        }
        // Edges of the selected note: handles to drag.
        if (isSelected && n->included)
        {
            g.setColour (tone);
            for (const float x : { box.getX(), box.getRight() })
                g.fillRoundedRectangle ({ x - 1.5f, box.getCentreY() - 12.0f, 3.0f, 24.0f }, 1.5f);
        }
        // Possible repeats: a dashed line with a small tab to split there.
        for (const auto f : repeatMarkers (n->id))
        {
            const float x = xForFrame (f);
            juce::Path line, dashed;
            line.startNewSubPath (x, box.getY() + 14.0f);
            line.lineTo (x, box.getBottom() - 8.0f);
            const float dashes[] = { 3.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            const bool tabHot = hover.repeat.has_value() && *hover.repeat == f;
            g.setColour (doubt.withAlpha (tabHot ? 0.95f : 0.6f));
            g.fillPath (dashed);
            juce::Path tab;
            tab.addTriangle (x - 5.0f, box.getY() + 3.0f, x + 5.0f, box.getY() + 3.0f, x, box.getY() + 11.0f);
            g.fillPath (tab);
        }
        // Its name (and cents when there is room); a doubt is a small amber dot, details only
        // when selected (the inspector).
        if (box.getWidth() > 22.0f)
        {
            juce::String label = noteName (n->playKey());
            const bool roomy = box.getWidth() > 64.0f;
            if (roomy && std::abs (n->centsOffset) >= 1.0)
                label << " " << centsText (n->centsOffset).upToFirstOccurrenceOf (" ", false, false);
            g.setFont (type::popupValue (12.0f));
            g.setColour (light.withAlpha (n->included ? (isSelected ? 1.0f : 0.82f) : 0.45f));
            const auto text = box.reduced (6.0f, 0.0f).withHeight (20.0f).translated (0.0f, 4.0f);
            g.drawText (label, text, juce::Justification::topLeft, true);
            const bool doubtful = n->included && ((n->diagnostics.any() && ! n->corrections.any()) || isConflict (n->id));
            if (doubtful)
            {
                const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
                if (text.getX() + tw + 12.0f < box.getRight())
                {
                    g.setColour (doubt.withAlpha (0.9f));
                    const auto dot = juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ text.getX() + tw + 7.0f, text.getY() + 7.5f });
                    isConflict (n->id) ? g.drawEllipse (dot.expanded (0.5f), 1.2f) : g.fillEllipse (dot);
                }
            }
            if (n->assignedKey >= 0 && roomy)
            {
                // Played on another key than it was sung on: its own pitch, quietly.
                g.setFont (type::popupLabel (10.5f));
                g.setColour (colour::wellText);
                g.drawText (juce::String ("from ") + noteName (n->rootMidi), text.translated (0.0f, 15.0f), juce::Justification::topLeft, true);
            }
        }
    }
    g.restoreState();
}

void NoteMapEditor::paintInspector (juce::Graphics& g)
{
    using namespace design;
    const auto box = inspector;
    g.setColour (juce::Colour (0xff181c1d).withAlpha (0.92f));
    g.fillRoundedRectangle (box, 8.0f);
    g.setColour (cellRim.withAlpha (0.7f));
    g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
    if (atlas == nullptr)
    {
        g.setFont (type::popupLabel (12.0f));
        g.setColour (colour::wellText);
        g.drawFittedText (activity == InstrumentHost::AtlasActivity::analysing ? "Finding the notes" : "No notes mapped",
                          box.reduced (14.0f).getSmallestIntegerContainer(), juce::Justification::centred, 3);
        return;
    }
    // The headline: the selected note (its name, cents, doubts), or the map.
    const auto notes = selectedNotes();
    auto head = box.reduced (12.0f, 0.0f).withY (box.getY() + 8.0f).withHeight (24.0f);
    auto sub = head.translated (0.0f, 21.0f).withHeight (16.0f);
    if (notes.size() == 1)
    {
        const auto& n = *notes.front();
        g.setFont (type::popupValue (19.0f));
        g.setColour (light);
        const auto name = noteName (n.rootMidi);
        g.drawText (name, head, juce::Justification::centredLeft, false);
        const float nw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
        g.setFont (type::popupValue (13.0f));
        g.setColour (light.withAlpha (0.7f));
        g.drawText (centsText (n.centsOffset), head.withTrimmedLeft (nw + 8.0f), juce::Justification::centredLeft, false);
        // Its takes on its key (round robin), at the right.
        int takes = 0, index = 0;
        for (const auto* other : shownNotes())
            if (other->included && other->playKey() == n.playKey() && other->velocityLayer == n.velocityLayer)
            {
                ++takes;
                index += other->attackStart < n.attackStart ? 1 : 0;
            }
        if (n.included && takes > 1)
        {
            g.setFont (type::popupLabel (11.0f));
            g.setColour (colour::wellText.brighter (0.3f));
            g.drawText ("TAKE " + juce::String (index + 1) + "/" + juce::String (takes), head, juce::Justification::centredRight, false);
        }
        const auto doubts = doubtText (n);
        g.setFont (type::popupLabel (11.0f));
        g.setColour (doubts.isNotEmpty() ? doubt.withAlpha (0.9f) : colour::wellText);
        const double seconds = static_cast<double> (n.sourceEnd - n.attackStart) / sampleRate();
        g.drawText (doubts.isNotEmpty() ? doubts : juce::String (seconds, 2) + " s" + (n.corrections.any() ? juce::String::fromUTF8 (" \xc2\xb7 edited") : juce::String()),
                    sub, juce::Justification::centredLeft, true);
    }
    else
    {
        int included = 0, review = 0;
        for (const auto* n : shownNotes())
        {
            included += n->included ? 1 : 0;
            review += n->included && ((n->diagnostics.any() && ! n->corrections.any()) || isConflict (n->id)) ? 1 : 0;
        }
        g.setFont (type::popupValue (17.0f));
        g.setColour (light);
        g.drawText (notes.size() > 1 ? juce::String (static_cast<int> (notes.size())) + " notes selected" : juce::String (included) + (included == 1 ? " note" : " notes"),
                    head, juce::Justification::centredLeft, false);
        g.setFont (type::popupLabel (11.0f));
        g.setColour (review > 0 ? doubt.withAlpha (0.9f) : colour::wellText);
        if (notes.empty())
            g.drawText (review > 0 ? juce::String (review) + " to look at (amber)" : juce::String ("nothing to review"), sub, juce::Justification::centredLeft, true);
    }

    for (const auto& row : rows)
    {
        if (row.cells.empty())
        {
            g.setFont (type::popupLabel (11.0f));
            g.setColour (colour::wellText.brighter (0.15f));
            g.drawFittedText (row.note, juce::Rectangle<float> (box.getX() + 12.0f, row.y, box.getWidth() - 24.0f, row.height).getSmallestIntegerContainer(),
                              juce::Justification::topLeft, 2, 0.9f);
            continue;
        }
        if (row.label.isNotEmpty())
        {
            g.setFont (type::popupLabel (10.5f));
            g.setColour (colour::wellText.brighter (0.2f));
            g.drawText (row.label, juce::Rectangle<float> (box.getX() + 12.0f, row.y, 80.0f, row.height), juce::Justification::centredLeft, false);
        }
        for (const auto& c : row.cells)
        {
            if (c.value)
            {
                g.setFont (type::popupValue (12.5f));
                g.setColour (light.withAlpha (0.9f));
                g.drawText (c.text, c.area, juce::Justification::centred, false);
                continue;
            }
            const auto accent = colour::identity (layer).arc;
            const auto cellBox = c.area.reduced (0.5f, 0.0f);
            g.setColour (c.on ? accent.withAlpha (0.32f) : cellOff);
            g.fillRoundedRectangle (cellBox, 5.0f);
            g.setColour (c.on ? accent : cellRim);
            g.drawRoundedRectangle (cellBox.reduced (0.5f), 5.0f, 1.0f);
            g.setFont (c.text.length() <= 1 ? type::popupValue (14.0f) : type::popupLabel (11.0f));
            g.setColour (light.withAlpha (! c.enabled ? 0.28f : (c.on ? 1.0f : 0.75f)));
            g.drawText (c.text, cellBox, juce::Justification::centred, false);
        }
    }
}

void NoteMapEditor::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    // Over the waveform, as the EQ: the same graphite.
    g.setColour (colour::wellA.withAlpha (0.94f));
    g.fillRoundedRectangle (bounds, 10.0f);
    const auto graph = graphArea();

    // A quiet time grid (seconds), as the display's.
    {
        // A label every 70 px at most (three layers' narrow page included).
        const double seconds = static_cast<double> (view.getLength()) / sampleRate();
        const double labels = std::max (2.0, static_cast<double> (graph.getWidth()) / 70.0);
        double step = 60.0;
        for (const double s : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
            if (seconds / s <= labels)
            {
                step = s;
                break;
            }
        const int decimals = step >= 1.0 ? 0 : (juce::exactlyEqual (step, 0.25) ? 2 : 1);
        g.setFont (type::popupLabel (10.5f));
        const double t0 = std::ceil (static_cast<double> (view.getStart()) / sampleRate() / step) * step;
        for (double t = t0; t * sampleRate() < static_cast<double> (view.getEnd()); t += step)
        {
            const float x = xForFrame (static_cast<std::int64_t> (t * sampleRate()));
            g.setColour (colour::wellGrid.withAlpha (0.8f));
            g.fillRect (juce::Rectangle<float> (x, graph.getY() + 16.0f, 1.0f, graph.getHeight() - 16.0f));
            if (x < graph.getRight() - 24.0f)
            {
                g.setColour (colour::wellText);
                g.drawText (juce::String (t, decimals) + " s", juce::Rectangle<float> (x - 30.0f, graph.getY(), 60.0f, 14.0f),
                            juce::Justification::centred, false);
            }
        }
    }
    paintWave (g, graph);
    paintRegions (g, graph);
    if (atlas == nullptr || shownNotes().empty())
    {
        g.setFont (type::popupLabel (12.5f));
        g.setColour (colour::wellText);
        g.drawText (activity == InstrumentHost::AtlasActivity::analysing ? juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6") : juce::String ("No notes found"),
                    graph, juce::Justification::centred, false);
    }
    paintInspector (g);

    // The top row: EQ | DAMPEN | NOTES A, the state, undo / redo, close.
    g.setFont (type::popupLabel (12.0f));
    g.setColour (colour::wellText.brighter (0.2f));
    g.drawText ("EQ", eqTab, juce::Justification::centred, false);
    g.drawText ("DAMPEN", dampenTab, juce::Justification::centred, false);
    g.setFont (type::panelHeader());
    g.setColour (light.withAlpha (0.95f));
    g.drawText ("NOTES " + InstrumentHost::layerName (layer), titleArea, juce::Justification::centredLeft, false);
    {
        const auto status = statusText();
        g.setFont (type::popupLabel (11.5f));
        const bool working = activity != InstrumentHost::AtlasActivity::none || status == "Playable";
        g.setColour (status == "Needs review" ? doubt.withAlpha (0.9f) : (working ? colour::identity (layer).led.withAlpha (0.9f) : colour::wellText.brighter (0.2f)));
        g.drawText (status.toUpperCase(), statusArea, juce::Justification::centredLeft, true);
    }
    auto arrow = [&g] (juce::Rectangle<float> r, bool redo, bool enabled) {
        const auto c = r.reduced (5.0f);
        juce::Path p;
        p.addCentredArc (c.getCentreX(), c.getCentreY() + 1.0f, 5.5f, 5.0f, 0.0f, redo ? -1.9f : 1.9f, redo ? 1.2f : -1.2f, true);
        g.setColour (colour::wellText.brighter (0.3f).withAlpha (enabled ? 1.0f : 0.35f));
        g.strokePath (p, juce::PathStrokeType (1.3f));
        const auto tip = juce::Point<float> (c.getCentreX() + (redo ? 1.0f : -1.0f) * 5.5f * std::sin (1.2f), c.getCentreY() + 1.0f - 5.0f * std::cos (1.2f));
        g.fillEllipse (juce::Rectangle<float> (3.0f, 3.0f).withCentre (tip));
    };
    arrow (undoArea, false, host.undo().canUndo());
    arrow (redoArea, true, host.undo().canRedo());
    {
        const auto x = closeArea.reduced (6.0f);
        g.setColour (colour::wellText.brighter (0.3f));
        g.drawLine (x.getX(), x.getY(), x.getRight(), x.getBottom(), 1.4f);
        g.drawLine (x.getRight(), x.getY(), x.getX(), x.getBottom(), 1.4f);
    }
}

//==============================================================================
void NoteMapEditor::mouseMove (const juce::MouseEvent& e)
{
    const auto now = hitTest (e.position);
    if (now.noteId != hover.noteId || now.edge != hover.edge || now.repeat != hover.repeat)
    {
        hover = now;
        repaint (graphArea().getSmallestIntegerContainer());
    }
    bool overCell = false;
    for (const auto& row : rows)
        for (const auto& c : row.cells)
            overCell = overCell || (! c.value && c.enabled && c.area.contains (e.position));
    const bool overKey = closeArea.contains (e.position) || undoArea.contains (e.position) || redoArea.contains (e.position) || eqTab.contains (e.position)
                         || dampenTab.contains (e.position);
    setMouseCursor (now.edge != Edge::none ? juce::MouseCursor::LeftRightResizeCursor
                                           : (now.repeat.has_value() || overCell || overKey ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor));
    juce::String tip;
    if (now.repeat.has_value())
        tip = "Maybe two notes: click to split here";
    else if (now.edge == Edge::start)
        tip = "Drag: where the note starts";
    else if (now.edge == Edge::end)
        tip = "Drag: where the note ends";
    setTooltip (tip);
}

void NoteMapEditor::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

void NoteMapEditor::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    pressAt = e.position;
    dragged = false;
    panning = false;
    pressed = {};
    if (closeArea.contains (e.position))
    {
        if (onClose != nullptr)
            onClose();
        return;
    }
    if (eqTab.contains (e.position) || dampenTab.contains (e.position))
    {
        if (onShowEq != nullptr)
            onShowEq (dampenTab.contains (e.position));
        return;
    }
    if (undoArea.contains (e.position) || redoArea.contains (e.position))
    {
        undoArea.contains (e.position) ? host.undo().undo() : host.undo().redo();
        refresh();
        return;
    }
    if (inspector.contains (e.position))
    {
        for (const auto& row : rows)
            for (const auto& c : row.cells)
                if (c.area.contains (e.position) && ! c.value && c.enabled)
                {
                    perform (c.action);
                    return;
                }
        return;
    }
    pressed = hitTest (e.position);
    if (pressed.noteId.empty())
    {
        panning = true;
        panFrom = view.getStart();
        return;
    }
    if (pressed.edge != Edge::none)
    {
        const auto* n = note (pressed.noteId);
        dragFrame = n != nullptr ? (pressed.edge == Edge::start ? n->attackStart : n->sourceEnd) : 0;
        if (std::find (selected.begin(), selected.end(), pressed.noteId) == selected.end())
            select ({ pressed.noteId });
        return;
    }
    if (pressed.repeat.has_value())
        return;   // on release: split there
    // A note: selected (shift: the range from the last one, cmd / ctrl: one more or one less).
    const auto all = shownNotes();
    if (e.mods.isShiftDown() && ! anchor.empty())
    {
        const auto a = std::find_if (all.begin(), all.end(), [this] (const auto* n) { return n->id == anchor; });
        const auto b = std::find_if (all.begin(), all.end(), [this] (const auto* n) { return n->id == pressed.noteId; });
        if (a != all.end() && b != all.end())
        {
            std::vector<std::string> range;
            for (auto it = std::min (a, b); it <= std::max (a, b); ++it)
                range.push_back ((*it)->id);
            const auto keep = anchor;
            selected = range;
            anchor = keep;
            rebuildInspector();
            repaint();
            return;
        }
    }
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        auto next = selected;
        if (const auto it = std::find (next.begin(), next.end(), pressed.noteId); it != next.end())
            next.erase (it);
        else
            next.push_back (pressed.noteId);
        selected = next;
        rebuildInspector();
        repaint();
        return;
    }
    select ({ pressed.noteId });
}

void NoteMapEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() > 3)
        dragged = true;
    if (! dragged)
        return;
    const auto g = graphArea();
    if (panning)
    {
        const auto frames = static_cast<std::int64_t> (static_cast<double> (pressAt.x - e.position.x) / std::max (1.0f, g.getWidth()) * static_cast<double> (view.getLength()));
        setViewRange (panFrom + frames, panFrom + frames + view.getLength());
        return;
    }
    if (pressed.edge == Edge::none)
        return;
    const auto* n = note (pressed.noteId);
    if (n == nullptr)
        return;
    // Between its neighbours (a note never swallows another), at least 30 ms long.
    const auto minimum = static_cast<std::int64_t> (0.03 * sampleRate());
    std::int64_t lo = 0, hi = sourceFrames();
    const auto all = shownNotes();
    const auto it = std::find (all.begin(), all.end(), n);
    if (pressed.edge == Edge::start)
    {
        if (it != all.begin())
            lo = (*(it - 1))->attackStart + minimum;
        hi = n->sourceEnd - minimum;
    }
    else
    {
        lo = n->attackStart + minimum;
        if (it != all.end() && it + 1 != all.end())
            hi = (*(it + 1))->sourceEnd - minimum;
    }
    dragFrame = std::clamp (frameForX (e.position.x), lo, std::max (lo, hi));
    repaint (g.getSmallestIntegerContainer());
}

void NoteMapEditor::mouseUp (const juce::MouseEvent&)
{
    if (panning)
    {
        if (! dragged && ! selected.empty())
            select ({});
        panning = false;
        return;
    }
    if (pressed.noteId.empty())
        return;
    if (pressed.edge != Edge::none)
    {
        if (dragged)
            commitBoundary();
        pressed = {};
        dragged = false;
        repaint();
        return;
    }
    if (pressed.repeat.has_value() && ! dragged)
    {
        split (pressed.noteId, *pressed.repeat);
        pressed = {};
        return;
    }
    if (! dragged)
        audition (selectedNotes().size() > 1 ? std::vector<const atlas::NoteEvent*> { note (pressed.noteId) } : selectedNotes());
    pressed = {};
}

void NoteMapEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto hit = hitTest (e.position);
    if (hit.noteId.empty() || hit.edge != Edge::none || hit.repeat.has_value())
        return;
    split (hit.noteId, frameForX (e.position.x));
}

void NoteMapEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (! graphArea().contains (e.position))
        return;
    const bool scroll = e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY);
    const auto length = static_cast<double> (view.getLength());
    if (scroll)
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        const auto frames = static_cast<std::int64_t> (-delta * 0.5 * length);
        setViewRange (view.getStart() + frames, view.getEnd() + frames);
        return;
    }
    // Zoom around the pointer: the frame under it stays under it.
    const auto at = frameForX (e.position.x);
    const double factor = std::pow (2.0, -wheel.deltaY * 2.0);
    const double next = length * factor;
    const double ratio = static_cast<double> (at - view.getStart()) / std::max (1.0, length);
    const auto start = at - static_cast<std::int64_t> (ratio * next);
    setViewRange (start, start + static_cast<std::int64_t> (next));
}

bool NoteMapEditor::keyPressed (const juce::KeyPress& key)
{
    // The selected notes: left / right selects the neighbour, space auditions, up / down the pitch.
    const auto all = shownNotes();
    if (key == juce::KeyPress::spaceKey)
        return perform ("audition");
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        return perform (key == juce::KeyPress::upKey ? (key.getModifiers().isShiftDown() ? "oct+" : "pitch+") : (key.getModifiers().isShiftDown() ? "oct-" : "pitch-"));
    if ((key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey) && ! all.empty())
    {
        const auto sel = selectedNotes();
        auto it = sel.empty() ? all.begin() : std::find (all.begin(), all.end(), sel.front());
        if (! sel.empty())
        {
            if (key == juce::KeyPress::rightKey && it + 1 != all.end())
                ++it;
            else if (key == juce::KeyPress::leftKey && it != all.begin())
                --it;
        }
        select ({ (*it)->id });
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (! selected.empty())
            select ({});
        else if (onClose != nullptr)
            onClose();
        return true;
    }
    return false;
}

} // namespace osp::plugin
