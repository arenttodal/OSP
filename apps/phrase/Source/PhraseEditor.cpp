#include "PhraseEditor.h"

#include "andor/phrase/PhraseEdits.h"
#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <tuple>

namespace osp::andorlab
{

namespace
{
    /** A note's colour: its pitch class along the instrument's spectral gradient (warm low .. cool high). */
    juce::Colour noteColour (const atlas::NoteEvent& n)
    {
        return plugin::palette::spectrum (static_cast<float> (((n.playKey() % 12) + 12) % 12) / 11.0f);
    }

    juce::String noteLabel (const atlas::NoteEvent& n)
    {
        const int cents = juce::roundToInt (n.centsOffset);
        return juce::String (midiNoteName (n.playKey())) + (cents == 0 ? juce::String() : (cents > 0 ? " +" : " ") + juce::String (cents));
    }

    juce::String secondsText (std::int64_t frames, double rate) { return juce::String (static_cast<double> (frames) / rate, 3) + " s"; }
}

//==============================================================================
PhraseWaveform::PhraseWaveform (PhraseProcessor& p) : processor (p)
{
    emptyText = "Drop a monophonic phrase here (WAV / AIFF), or use Import";
    setTitle ("Phrase waveform");
}

void PhraseWaveform::refresh()
{
    const auto& doc = processor.document();
    if (doc.source != nullptr)
    {
        if (totalFrames() != doc.source->audio->numFrames())
            setOverview (doc.source->peakMin, doc.source->peakMax, doc.source->audio->numFrames(), doc.source->audio->sampleRate);
    }
    else if (totalFrames() != 0)
        setOverview ({}, {}, 0, 48000.0);
    if (doc.atlas.findNote (selected) == nullptr)
        selected.clear();
    repaint();
}

void PhraseWaveform::setReadHeads (std::vector<std::pair<int, float>> heads)
{
    if (heads != readHeads)
    {
        readHeads = std::move (heads);
        repaint();
    }
}

std::int64_t PhraseWaveform::markerFrame (const atlas::NoteEvent& n, Handle h) const
{
    switch (h)
    {
        case Handle::attack: return n.attackStart;
        case Handle::sustainStart: return n.sustain.start;
        case Handle::sustainEnd: return n.sustain.end;
        case Handle::end: return n.sourceEnd;
        case Handle::none: break;
    }
    return 0;
}

PhraseWaveform::Handle PhraseWaveform::handleAt (juce::Point<float> p) const
{
    const auto* n = processor.document().atlas.findNote (selected);
    if (n == nullptr)
        return Handle::none;
    for (auto h : { Handle::attack, Handle::end, Handle::sustainStart, Handle::sustainEnd })
        if (std::abs (xOf (static_cast<double> (markerFrame (*n, h))) - p.x) < 5.0f)
            return h;
    return Handle::none;
}

void PhraseWaveform::paintOverlay (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto& doc = processor.document();
    if (! hasAudio())
        return;
    for (const auto& n : doc.atlas.notes)
    {
        const float x0 = xOf (static_cast<double> (n.attackStart)), x1 = xOf (static_cast<double> (n.sourceEnd));
        if (x1 < area.getX() || x0 > area.getRight())
            continue;
        const bool isSelected = n.id == selected;
        const auto colour = noteColour (n);
        auto box = juce::Rectangle<float> (x0, area.getY() + 18.0f, std::max (2.0f, x1 - x0), area.getHeight() - 34.0f);
        const float alpha = n.included ? (isSelected ? 0.34f : 0.14f + 0.12f * static_cast<float> (n.confidence)) : 0.05f;
        g.setColour (colour.withAlpha (alpha));
        g.fillRoundedRectangle (box, 4.0f);
        // Confidence: a solid outline when sure, dashed when not.
        g.setColour (colour.withAlpha (n.included ? (isSelected ? 1.0f : 0.75f) : 0.3f));
        if (n.confidence >= 0.6 || isSelected)
            g.drawRoundedRectangle (box, 4.0f, isSelected ? 1.8f : 1.0f);
        else
        {
            juce::Path outline;
            outline.addRoundedRectangle (box, 4.0f);
            juce::Path dashed;
            const float dashes[] = { 4.0f, 3.0f };
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
            g.fillPath (dashed);
        }
        // The sustain body (where loops and grains live) as a band along the bottom.
        if (n.sustain.length() > 0)
        {
            const float s0 = xOf (static_cast<double> (n.sustain.start)), s1 = xOf (static_cast<double> (n.sustain.end));
            g.setColour (colour.withAlpha (n.included ? 0.7f : 0.2f));
            g.fillRect (juce::Rectangle<float> (s0, box.getBottom() - 4.0f, std::max (1.0f, s1 - s0), 3.0f));
        }
        if (x1 - x0 > 22.0f)
        {
            g.setColour (n.included ? colour.brighter (0.35f) : design::colour::wellText);
            g.setFont (ui::label (x1 - x0 > 60.0f ? 12.0f : 10.0f));
            g.drawText (noteLabel (n) + (n.corrections.any() ? " *" : ""), juce::Rectangle<float> (x0 + 3.0f, area.getY() + 1.0f, x1 - x0 + 40.0f, 16.0f),
                        juce::Justification::centredLeft);
        }
        if (isSelected)
        {
            for (auto h : { Handle::attack, Handle::sustainStart, Handle::sustainEnd, Handle::end })
            {
                const float x = xOf (static_cast<double> (dragging == h ? dragFrame : markerFrame (n, h)));
                const bool outer = h == Handle::attack || h == Handle::end;
                g.setColour (outer ? juce::Colours::white.withAlpha (0.9f) : colour.brighter (0.4f));
                g.drawLine (x, box.getY() - 4.0f, x, box.getBottom() + 4.0f, outer ? 2.0f : 1.2f);
                juce::Path grip;
                grip.addTriangle (x - 5.0f, box.getY() - 9.0f, x + 5.0f, box.getY() - 9.0f, x, box.getY() - 2.0f);
                g.fillPath (grip);
            }
        }
    }
    if (cursor >= 0)
    {
        const float x = xOf (static_cast<double> (cursor));
        g.setColour (design::colour::accent.withAlpha (0.9f));
        const float dashes[] = { 3.0f, 3.0f };
        g.drawDashedLine (juce::Line<float> (x, area.getY(), x, area.getBottom()), dashes, 2, 1.2f);
    }
    // Read heads of the sounding voices.
    g.setColour (design::colour::accent);
    for (const auto& [region, frame] : readHeads)
    {
        juce::ignoreUnused (region);
        const float x = xOf (static_cast<double> (frame));
        g.drawLine (x, area.getY() + 14.0f, x, area.getBottom() - 12.0f, 1.4f);
    }
}

void PhraseWaveform::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (handleAt (e.position) != Handle::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
}

void PhraseWaveform::mouseDown (const juce::MouseEvent& e)
{
    if (! hasAudio())
        return;
    dragging = handleAt (e.position);
    if (dragging != Handle::none)
    {
        dragFrame = markerFrame (*processor.document().atlas.findNote (selected), dragging);
        return;
    }
    const auto frame = static_cast<std::int64_t> (frameAt (e.position.x));
    std::string hit;
    for (const auto& n : processor.document().atlas.notes)
        if (frame >= n.attackStart && frame < n.sourceEnd)
            hit = n.id;   // the later of overlapping regions wins (it is drawn on top)
    if (! hit.empty() && hit == selected)
        cursor = frame;   // a click inside the selected note sets the split point
    else
        cursor = -1;
    selected = hit;
    if (onSelect)
        onSelect (selected);
    repaint();
}

void PhraseWaveform::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging == Handle::none)
        return;
    dragFrame = static_cast<std::int64_t> (frameAt (e.position.x));
    repaint();
}

void PhraseWaveform::mouseUp (const juce::MouseEvent&)
{
    if (dragging == Handle::none)
        return;
    const auto marker = dragging == Handle::attack ? phrase::edit::Marker::attackStart
                      : dragging == Handle::sustainStart ? phrase::edit::Marker::sustainStart
                      : dragging == Handle::sustainEnd ? phrase::edit::Marker::sustainEnd : phrase::edit::Marker::end;
    const auto id = selected;
    const auto frame = dragFrame;
    dragging = Handle::none;
    processor.edit ("Move marker", [&] (atlas::NoteAtlas& a) { return phrase::edit::moveMarker (a, id, marker, frame); });
}

void PhraseWaveform::mouseDoubleClick (const juce::MouseEvent&)
{
    if (const auto* n = processor.document().atlas.findNote (selected))
    {
        processor.audition (n->playKey(), true);
        const int key = n->playKey();
        juce::Timer::callAfterDelay (700, [this, key] { processor.audition (key, false); });
    }
}

//==============================================================================
NotePanel::NotePanel (PhraseProcessor& p) : processor (p)
{
    auto editNote = [this] (const juce::String& name, std::function<bool (atlas::NoteAtlas&, const std::string&)> f)
    {
        return [this, name, f]
        {
            if (! id.empty())
                processor.edit (name, [&] (atlas::NoteAtlas& a) { return f (a, id); });
        };
    };
    rootDown.onClick = editNote ("Pitch", [] (auto& a, auto& i) { return phrase::edit::setRoot (a, i, a.findNote (i)->rootMidi - 1); });
    rootUp.onClick = editNote ("Pitch", [] (auto& a, auto& i) { return phrase::edit::setRoot (a, i, a.findNote (i)->rootMidi + 1); });
    octDown.onClick = editNote ("Octave", [] (auto& a, auto& i) { return phrase::edit::setRoot (a, i, a.findNote (i)->rootMidi - 12); });
    octUp.onClick = editNote ("Octave", [] (auto& a, auto& i) { return phrase::edit::setRoot (a, i, a.findNote (i)->rootMidi + 12); });
    keyDown.onClick = editNote ("Key", [] (auto& a, auto& i) { return phrase::edit::assignKey (a, i, std::max (0, a.findNote (i)->playKey() - 1)); });
    keyUp.onClick = editNote ("Key", [] (auto& a, auto& i) { return phrase::edit::assignKey (a, i, std::min (127, a.findNote (i)->playKey() + 1)); });
    keyAuto.onClick = editNote ("Key", [] (auto& a, auto& i) { return phrase::edit::assignKey (a, i, -1); });
    include.onClick = editNote ("Include", [] (auto& a, auto& i) { return phrase::edit::setIncluded (a, i, ! a.findNote (i)->included); });
    audition.onClick = [this]
    {
        if (const auto* n = note())
        {
            const int key = n->playKey();
            processor.audition (key, true);
            juce::Timer::callAfterDelay (800, [this, key] { processor.audition (key, false); });
        }
    };
    split.onClick = [this]
    {
        const auto* n = note();
        if (n == nullptr || processor.document().source == nullptr)
            return;
        auto at = splitPoint ? splitPoint() : -1;
        if (at <= n->attackStart || at >= n->sourceEnd)
            at = (n->attackStart + n->sourceEnd) / 2;
        const auto audio = processor.document().source->audio;
        std::string made;
        const auto target = id;
        const auto settings = processor.analysedSettings();
        processor.edit ("Split", [&] (atlas::NoteAtlas& a)
                        {
                            made = phrase::edit::split (a, target, at, *audio, settings);
                            return ! made.empty();
                        });
    };
    merge.onClick = [this]
    {
        if (id.empty() || processor.document().source == nullptr)
            return;
        const auto audio = processor.document().source->audio;
        const auto target = id;
        const auto settings = processor.analysedSettings();
        processor.edit ("Merge", [&] (atlas::NoteAtlas& a) { return phrase::edit::mergeWithNext (a, target, *audio, settings); });
    };
    reanalyse.onClick = [this]
    {
        if (id.empty() || processor.document().source == nullptr)
            return;
        const auto audio = processor.document().source->audio;
        const auto target = id;
        const auto settings = processor.analysedSettings();
        processor.edit ("Re-analyse note", [&] (atlas::NoteAtlas& a) { return phrase::edit::reanalyse (a, target, *audio, settings); });
    };
    undo.onClick = [this] { processor.undoManager().undo(); };
    redo.onClick = [this] { processor.undoManager().redo(); };
    auto step = [this] (int direction)
    {
        const auto& notes = processor.document().atlas.notes;
        if (notes.empty())
            return;
        int index = processor.document().atlas.indexOf (id);
        index = index < 0 ? 0 : juce::jlimit (0, static_cast<int> (notes.size()) - 1, index + direction);
        if (onSelect)
            onSelect (notes[static_cast<std::size_t> (index)].id);
    };
    previous.onClick = [step] { step (-1); };
    next.onClick = [step] { step (1); };
    for (auto* k : { &rootDown, &rootUp, &octDown, &octUp, &keyDown, &keyUp, &keyAuto, &include, &audition, &split, &merge, &reanalyse,
                     &undo, &redo, &previous, &next })
        addAndMakeVisible (k);
    refresh();
}

const atlas::NoteEvent* NotePanel::note() const
{
    return processor.document().atlas.findNote (id);
}

void NotePanel::setNote (const std::string& noteId)
{
    id = noteId;
    refresh();
}

void NotePanel::refresh()
{
    const auto* n = note();
    for (auto* k : { &rootDown, &rootUp, &octDown, &octUp, &keyDown, &keyUp, &keyAuto, &include, &audition, &split, &merge, &reanalyse })
        k->setEnabled (n != nullptr);
    include.setActive (n != nullptr && n->included);
    include.setText (n != nullptr && ! n->included ? "EXCLUDED" : "INCLUDED");
    keyAuto.setActive (n != nullptr && n->assignedKey < 0);
    undo.setEnabled (processor.undoManager().canUndo());
    redo.setEnabled (processor.undoManager().canRedo());
    repaint();
}

void NotePanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (110);
    auto row = [&] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (8); return x; };
    auto pitchRow = row (28);
    pitchRow.removeFromLeft (92);
    rootDown.setBounds (pitchRow.removeFromLeft (34));
    pitchRow.removeFromLeft (64);
    rootUp.setBounds (pitchRow.removeFromLeft (34));
    pitchRow.removeFromLeft (10);
    octDown.setBounds (pitchRow.removeFromLeft (62));
    pitchRow.removeFromLeft (6);
    octUp.setBounds (pitchRow.removeFromLeft (62));
    auto keyRow = row (28);
    keyRow.removeFromLeft (92);
    keyDown.setBounds (keyRow.removeFromLeft (34));
    keyRow.removeFromLeft (64);
    keyUp.setBounds (keyRow.removeFromLeft (34));
    keyRow.removeFromLeft (10);
    keyAuto.setBounds (keyRow.removeFromLeft (62));
    keyRow.removeFromLeft (6);
    include.setBounds (keyRow.removeFromLeft (100));
    auto actions = row (30);
    const int w = actions.getWidth() / 4;
    for (auto* k : { &audition, &split, &merge })
        k->setBounds (actions.removeFromLeft (w).reduced (2, 0));
    reanalyse.setBounds (actions.reduced (2, 0));
    auto bottom = row (30);
    const int bw = bottom.getWidth() / 4;
    for (auto* k : { &previous, &next, &undo, &redo })
        k->setBounds (bottom.removeFromLeft (bw).reduced (2, 0));
}

void NotePanel::paint (juce::Graphics& g)
{
    ui::panel (g, getLocalBounds().toFloat(), "SELECTED NOTE");
    auto r = getLocalBounds().reduced (16, 12);
    r.removeFromTop (22);
    const auto* n = note();
    if (n == nullptr)
    {
        g.setColour (design::colour::textMicro);
        g.setFont (ui::value (13.0f));
        g.drawFittedText ("Click a note in the waveform to see and correct it.\nDrag its markers to move the attack, sustain and end.",
                          r.removeFromTop (60), juce::Justification::topLeft, 3);
        return;
    }
    const double rate = processor.document().atlas.sources.empty() ? 48000.0 : processor.document().atlas.sources[0].sampleRate;
    g.setColour (noteColour (*n));
    g.fillEllipse (juce::Rectangle<float> (static_cast<float> (r.getX()), static_cast<float> (r.getY()) + 8.0f, 12.0f, 12.0f));
    g.setColour (design::colour::text);
    g.setFont (plugin::fonts::make (26.0f, plugin::fonts::Weight::medium));
    g.drawText (noteLabel (*n) + (juce::roundToInt (n->centsOffset) != 0 ? " c" : ""), r.withTrimmedLeft (20).removeFromTop (30), juce::Justification::centredLeft);
    g.setFont (ui::value (11.5f));
    g.setColour (design::colour::textSecondary);
    g.drawText (juce::String (n->id) + "  " + juce::String (n->pitchHz, 1) + " Hz", r.removeFromTop (30).withTrimmedLeft (170), juce::Justification::centredLeft);
    juce::String detail = "confidence " + juce::String (juce::roundToInt (100.0 * n->confidence)) + ui::u8 (" %  ·  ") + atlas::toString (n->articulation);
    if (n->vibratoDepthCents > 0.0)
        detail << ui::u8 ("  ·  vibrato ") << juce::roundToInt (n->vibratoDepthCents) << " c @ " << juce::String (n->vibratoRateHz, 1) << " Hz";
    g.drawText (detail, r.removeFromTop (18), juce::Justification::centredLeft);
    g.drawText ("attack " + secondsText (n->attackStart, rate) + ui::u8 ("  ·  sustain ") + secondsText (n->sustain.start, rate) + " - "
                    + secondsText (n->sustain.end, rate) + ui::u8 ("  ·  end ") + secondsText (n->sourceEnd, rate),
                r.removeFromTop (18), juce::Justification::centredLeft);
    if (n->corrections.any())
    {
        g.setColour (design::colour::accent);
        g.drawText ("edited by you", r.removeFromTop (16), juce::Justification::centredLeft);
    }
    // Labels of the control rows.
    auto rows = getLocalBounds().reduced (16, 12);
    rows.removeFromTop (110);
    g.setFont (ui::label (11.0f));
    g.setColour (design::colour::textSecondary);
    g.drawText ("PITCH", rows.removeFromTop (28), juce::Justification::centredLeft);
    rows.removeFromTop (8);
    g.drawText ("PLAYS ON", rows.removeFromTop (28), juce::Justification::centredLeft);
    g.setColour (design::colour::text);
    g.setFont (ui::value (13.0f));
    auto pitchBox = getLocalBounds().reduced (16, 12).withTrimmedTop (110).removeFromTop (28).withTrimmedLeft (126).withWidth (64);
    g.drawText (midiNoteName (n->rootMidi), pitchBox, juce::Justification::centred);
    auto keyBox = pitchBox.translated (0, 36);
    g.drawText (midiNoteName (n->playKey()), keyBox, juce::Justification::centred);
}

//==============================================================================
PhraseEditor::Content::Content (PhraseProcessor& p)
    : processor (p),
      wave (p),
      notes (p),
      importKey ("IMPORT"),
      menuKey (ui::u8 ("PRESET  ▾")),
      cancelKey ("CANCEL"),
      mode (p.state, "playMode", { "ONE SHOT", "GATE", "SUSTAIN" }),
      fill (p.state, "fill", { "OFF", "NATURAL", "BALANCED", "WIDE", "EXTENDED" }),
      variation (p.state, "roundRobin", { "IN TURN", "RANDOM", "SHUFFLE" }),
      tuning (p.state, "tuning", { "IN TUNE", "AS PLAYED" }),
      detection (p.state, "detection", { "FEWER", "NORMAL", "MORE" }),
      attack (p.state, "attack", "Attack"),
      release (p.state, "release", "Release"),
      tone (p.state, "tone", "Tone", true, plugin::palette::gold),
      synthetic (p.state, "synthetic", "Synthetic", true, plugin::palette::lavender),
      preserve (p.state, "attackPreserve", "Keep Attack", true, plugin::palette::coral),
      character (p.state, "sustainCharacter", "Sustain Char.", true, plugin::palette::mineral),
      pitch (p.state, "pitch", "Pitch"),
      levelMatch (p.state, "levelMatch", "Level Match"),
      space (p.state, "space", "Space"),
      echo (p.state, "echo", "Echo"),
      velocity (p.state, "velocity", "Velocity"),
      master (p.state, "gain", "Master")
{
    for (auto* c : std::initializer_list<juce::Component*> { &wave, &keys, &notes, &importKey, &menuKey, &cancelKey, &octaveDown, &octaveUp, &mode, &fill,
                                                             &variation, &tuning, &attack, &release, &tone, &synthetic, &preserve, &character,
                                                             &pitch, &levelMatch, &space, &echo, &velocity, &master, &detection, &minNote })
        addAndMakeVisible (c);
    // How readily the recording is cut into notes: changing either re-analyses in the background
    // (corrections kept, one undo step), so a setting can be played before keeping it.
    minNoteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.state, "minNote", minNote);
    minNote.onValueChange = [this] { repaint(); };
    minNote.setTooltip ("Shorter pieces without a pitch of their own join the neighbouring note (re-analyses; UNDO goes back)");
    importKey.onClick = [this] { chooseFile(); };
    menuKey.onClick = [this] { showMenu(); };
    cancelKey.onClick = [this] { processor.cancelLoad(); };
    octaveDown.onClick = [this] { keys.setRange (keys.lowKey() - 12, keys.highKey() - 12); };
    octaveUp.onClick = [this] { keys.setRange (keys.lowKey() + 12, keys.highKey() + 12); };

    wave.onSelect = [this] (const std::string& id) { select (id); };
    notes.onSelect = [this] (const std::string& id) { select (id); };
    notes.splitPoint = [this] { return wave.cursor; };
    keys.setRange (36, 96);
    keys.onNote = [this] (int key, bool down) { processor.audition (key, down); };
    keys.onSelect = [this] (int key)
    {
        // Selecting a key selects its first recorded note (to correct it).
        for (const auto& n : processor.document().atlas.notes)
            if (n.included && n.playKey() == key)
            {
                select (n.id);
                return;
            }
    };
    keys.lookOf = [this] (int key)
    {
        KeyboardMap::KeyLook look;
        const auto model = processor.model();
        if (model == nullptr || ! model->hasNotes())
            return look;
        const auto info = phrase::describeKey (*model, key, processor.currentParams().fill);
        switch (info.state)
        {
            case phrase::KeyState::exact:
                look.fill = plugin::palette::amber;
                look.strong = true;
                break;
            case phrase::KeyState::variations:
                look.fill = plugin::palette::amber;
                look.strong = true;
                look.badge = juce::String (info.variations);
                break;
            case phrase::KeyState::filled:
                look.fill = plugin::palette::amber.withMultipliedSaturation (0.45f).brighter (0.45f);
                break;
            case phrase::KeyState::missing:
                look.hatched = true;
                break;
        }
        return look;
    };
}

void PhraseEditor::Content::select (const std::string& id)
{
    if (wave.selected != id)
        wave.cursor = -1;
    wave.selected = id;
    notes.setNote (id);
    if (const auto* n = processor.document().atlas.findNote (id))
        keys.setSelected (n->playKey());
    else
        keys.setSelected (-1);
    wave.repaint();
}

void PhraseEditor::Content::refresh()
{
    wave.refresh();
    notes.refresh();
    keys.repaint();
    // Keep the mapped notes in view on the keyboard.
    const auto& a = processor.document().atlas;
    int lo = 127, hi = 0;
    for (const auto& n : a.notes)
        if (n.included)
        {
            lo = std::min (lo, n.playKey());
            hi = std::max (hi, n.playKey());
        }
    if (lo <= hi && (lo < keys.lowKey() || hi > keys.highKey()))
    {
        const int centre = (lo + hi) / 2;
        keys.setRange (centre - 30, centre + 30);
    }
    repaint();
}

void PhraseEditor::Content::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a monophonic phrase", juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                          {
                              if (fc.getResult().existsAsFile())
                                  processor.loadFile (fc.getResult());
                          });
}

void PhraseEditor::Content::showMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Load example phrase", [this] { processor.loadExample(); });
    menu.addItem ("Import recording...", [this] { chooseFile(); });
    menu.addSeparator();
    menu.addItem ("Save preset...", [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Save ANDOR Phrase preset", PhraseProcessor::presetFolder(), "*.andorphrase");
                      chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult() != juce::File())
                                                    processor.savePreset (fc.getResult().withFileExtension (PhraseProcessor::presetExtension), error);
                                            });
                  });
    menu.addItem ("Load preset...", [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Load ANDOR Phrase preset", PhraseProcessor::presetFolder(), "*.andorphrase");
                      chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult().existsAsFile())
                                                    processor.loadPreset (fc.getResult(), error);
                                            });
                  });
    menu.addSeparator();
    menu.addItem ("Export Note Atlas (for ANDOR Grain)...", processor.hasSource(), false, [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Export Note Atlas", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.andoratlas");
                      chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult() != juce::File())
                                                    processor.exportAtlas (fc.getResult().withFileExtension (".andoratlas"), error);
                                            });
                  });
    menu.addItem ("Re-analyse (keeps your corrections)", processor.hasSource(), false, [this] { processor.reanalyse(); });
    menu.addItem ("Relink missing sample...", processor.document().sourceMissing, false, [this]
                  {
                      chooser = std::make_unique<juce::FileChooser> ("Find the recording", juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc");
                      chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
                                            {
                                                std::string error;
                                                if (fc.getResult().existsAsFile() && ! processor.relink (fc.getResult(), error))
                                                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Relink", error);
                                            });
                  });
    menu.addSeparator();
    menu.addItem ("Undo", processor.undoManager().canUndo(), false, [this] { processor.undoManager().undo(); });
    menu.addItem ("Redo", processor.undoManager().canRedo(), false, [this] { processor.undoManager().redo(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuKey));
}

void PhraseEditor::Content::resized()
{
    menuKey.setBounds (560, 34, 150, 34);
    importKey.setBounds (720, 34, 96, 34);
    cancelKey.setBounds (826, 34, 80, 34);
    master.setBounds (1124, 18, 82, 84);
    detection.setBounds (640, 80, 236, 24);
    minNote.setBounds (966, 80, 96, 24);
    wave.setBounds (30, 108, 1180, 262);
    octaveDown.setBounds (30, 384, 24, 66);
    octaveUp.setBounds (1186, 384, 24, 66);
    keys.setBounds (58, 384, 1124, 66);
    notes.setBounds (30, 466, 470, 322);

    // The play panel: three choice rows on top, character knobs, then utility knobs.
    const int x0 = 516, y0 = 466;
    mode.setBounds (x0 + 16, y0 + 52, 270, 28);
    tuning.setBounds (x0 + 300, y0 + 52, 180, 28);
    variation.setBounds (x0 + 494, y0 + 52, 184, 28);
    fill.setBounds (x0 + 16, y0 + 108, 400, 28);
    int x = x0 + 18;
    for (auto* k : { &preserve, &character, &synthetic, &tone, &attack, &release })
    {
        k->setBounds (x, y0 + 146, 96, 92);
        x += 106;
    }
    x = x0 + 18;
    for (auto* k : { &levelMatch, &pitch, &velocity, &space, &echo })
    {
        k->setBounds (x, y0 + 246, 84, 68);
        x += 96;
    }
}

void PhraseEditor::Content::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll (design::draw::surround());
    design::draw::housing (g, bounds.reduced (6.0f));
    ui::logoBlock (g, { 40.0f, 30.0f, 420.0f, 56.0f }, "Phrase", ui::u8 ("PERFORMANCE TO INSTRUMENT  ·  RESEARCH PROTOTYPE"));

    // Analysis state in the header.
    auto statusArea = juce::Rectangle<float> (920.0f, 30.0f, 190.0f, 44.0f);
    g.setFont (ui::label (11.0f));
    g.setColour (design::colour::textSecondary);
    g.drawText (processor.isBusy() ? "ANALYSIS" : "STATE", statusArea.removeFromTop (14.0f), juce::Justification::centredLeft);
    g.setColour (design::colour::text);
    g.setFont (ui::value (12.0f));
    g.drawFittedText (statusText, statusArea.toNearestInt(), juce::Justification::topLeft, 2);
    if (processor.isBusy())
    {
        const auto bar = juce::Rectangle<float> (560.0f, 74.0f, 346.0f, 4.0f);
        g.setColour (design::colour::hairline);
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (design::colour::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, processor.progress())), 2.0f);
    }

    // Source name over the waveform.
    const auto& doc = processor.document();
    g.setFont (ui::label (11.5f));
    g.setColour (design::colour::textSecondary);
    juce::String source = doc.source != nullptr ? juce::String (doc.source->fileName) : doc.sourceMissing ? "Missing: " + juce::String (doc.missingName) + "  (menu > Relink)" : "No recording";
    if (doc.source != nullptr)
        source << ui::u8 ("  ·  ") << juce::String (doc.source->audio->durationSeconds(), 1) << ui::u8 (" s  ·  ") << juce::roundToInt (doc.source->audio->sampleRate / 1000.0) << " kHz";
    g.drawText (source, juce::Rectangle<float> (32.0f, 90.0f, 520.0f, 16.0f), juce::Justification::centredLeft);
    // Analysis controls between the header and the waveform.
    g.setFont (ui::label (11.0f));
    g.drawText ("DETECTION", juce::Rectangle<float> (562.0f, 84.0f, 76.0f, 16.0f), juce::Justification::centredLeft);
    g.drawText ("MIN NOTE", juce::Rectangle<float> (894.0f, 84.0f, 70.0f, 16.0f), juce::Justification::centredLeft);
    g.setColour (design::colour::text);
    g.setFont (ui::value (12.0f));
    g.drawText (juce::String (juce::roundToInt (minNote.getValue() * 1000.0)) + " ms", juce::Rectangle<float> (1064.0f, 84.0f, 52.0f, 16.0f),
                juce::Justification::centredLeft);
    g.setColour (design::colour::textSecondary);
    g.drawText (ui::u8 ("KEYBOARD MAP  ·  amber: recorded  ·  number: variations  ·  pale: filled  ·  hatched: not playable"),
                juce::Rectangle<float> (60.0f, 452.0f, 900.0f, 12.0f), juce::Justification::centredLeft);

    ui::panel (g, { 516.0f, 466.0f, 694.0f, 322.0f }, "PLAY");
    g.setFont (ui::label (11.0f));
    g.setColour (design::colour::textSecondary);
    for (const auto& [caption, x, y] : { std::tuple { "PLAY MODE", 532.0f, 502.0f }, std::tuple { "TUNING", 816.0f, 502.0f },
                                         std::tuple { "VARIATIONS", 1010.0f, 502.0f }, std::tuple { "FILL KEYBOARD", 532.0f, 558.0f } })
        g.drawText (caption, juce::Rectangle<float> (x, y, 200.0f, 14.0f), juce::Justification::centredLeft);
    const auto params = processor.currentParams();
    if (params.fill == phrase::FillMode::wide || params.fill == phrase::FillMode::extended)
    {
        g.setColour (design::colour::accent);
        g.drawText ("large transpositions change the timbre", juce::Rectangle<float> (940.0f, 588.0f, 260.0f, 14.0f), juce::Justification::centredLeft);
    }
    if (dragOver)
    {
        g.setColour (design::colour::accent.withAlpha (0.12f));
        g.fillRoundedRectangle (juce::Rectangle<float> (30.0f, 108.0f, 1180.0f, 262.0f), 10.0f);
    }
    // Footer: a summary line.
    int included = 0, keysUsed = 0, variations = 0;
    std::array<int, 128> perKey {};
    for (const auto& n : doc.atlas.notes)
        if (n.included)
        {
            ++included;
            ++perKey[static_cast<std::size_t> (n.playKey())];
        }
    for (int c : perKey)
    {
        keysUsed += c > 0 ? 1 : 0;
        variations += c > 1 ? c - 1 : 0;
    }
    g.setColour (design::colour::textMicro);
    g.setFont (ui::value (11.0f));
    g.drawText (juce::String (included) + " notes on " + juce::String (keysUsed) + " keys, " + juce::String (variations)
                    + ui::u8 (" extra variations  ·  voices ") + juce::String (processor.display.voices.load()) + ui::u8 ("  ·  ANDOR Phrase " JucePlugin_VersionString),
                juce::Rectangle<float> (36.0f, 796.0f, 900.0f, 14.0f), juce::Justification::centredLeft);
}

//==============================================================================
PhraseEditor::PhraseEditor (PhraseProcessor& p)
    : AudioProcessorEditor (p), processor (p), content (p), scaler (content, designWidth, designHeight)
{
    setLookAndFeel (&look);
    addAndMakeVisible (scaler);
    setResizable (true, true);
    constrainer.setFixedAspectRatio (static_cast<double> (designWidth) / designHeight);
    constrainer.setSizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
    setConstrainer (&constrainer);
    setSize (designWidth, designHeight);
    processor.onDocumentChanged = [this] { content.refresh(); };
    content.refresh();
    startTimerHz (30);
}

PhraseEditor::~PhraseEditor()
{
    processor.onDocumentChanged = nullptr;
    setLookAndFeel (nullptr);
}

void PhraseEditor::resized()
{
    scaler.setBounds (getLocalBounds());
}

void PhraseEditor::timerCallback()
{
    const int count = juce::jlimit (0, 32, processor.display.count.load (std::memory_order_acquire));
    std::vector<std::pair<int, float>> heads;
    for (int i = 0; i < count; ++i)
        heads.emplace_back (processor.display.c[static_cast<std::size_t> (i)].load(), processor.display.a[static_cast<std::size_t> (i)].load());
    content.wave.setReadHeads (std::move (heads));
    std::array<std::uint32_t, 4> mask {};
    for (int i = 0; i < 4; ++i)
        mask[static_cast<std::size_t> (i)] = processor.display.heldKeys[i].load();
    content.keys.setSounding (mask);
    const auto text = processor.status();
    content.cancelKey.setVisible (processor.isBusy());
    if (text != content.statusText || processor.isBusy())
    {
        content.statusText = text;
        content.repaint();
    }
}

bool PhraseEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return files.size() >= 1 && io::isSupportedAudioExtension (files[0].toStdString());
}

void PhraseEditor::filesDropped (const juce::StringArray& files, int, int)
{
    content.dragOver = false;
    if (! files.isEmpty())
        processor.loadFile (juce::File (files[0]));
    content.repaint();
}

void PhraseEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    content.dragOver = true;
    content.repaint();
}

void PhraseEditor::fileDragExit (const juce::StringArray&)
{
    content.dragOver = false;
    content.repaint();
}

} // namespace osp::andorlab
