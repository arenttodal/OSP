#include "ZoneEditor.h"

#include "OspLookAndFeel.h"

#include "core/PitchMath.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace osp::plugin
{

namespace
{
    const juce::Colour light { 0xffeee8dd };
    /** The bars' colours (the mockup's ceramic glazes), by pitch group. */
    const juce::Colour glazes[] = { juce::Colour (0xffe07b58), juce::Colour (0xffd8a75a), juce::Colour (0xff7fa58e),
                                    juce::Colour (0xff7d93b5), juce::Colour (0xffb58ab3) };

    juce::String keyName (int key) { return juce::String (midiNoteName (std::clamp (key, 0, 127))); }

    bool isBlack (int key)
    {
        const int p = ((key % 12) + 12) % 12;
        return p == 1 || p == 3 || p == 6 || p == 8 || p == 10;
    }

    bool isAudioFile (const juce::String& path)
    {
        return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;m4a;caf");
    }
}

ZoneEditor::ZoneEditor (InstrumentHost& h, int l) : host (h), layer (l)
{
    setOpaque (false);
    refresh();
}

int ZoneEditor::rowCount() const noexcept
{
    int rows = 1;
    for (const auto& z : shown)
        rows = std::max (rows, z.layer + 1);
    return rows;
}

void ZoneEditor::refresh()
{
    const auto current = host.currentInstrument (layer);
    if (current == nullptr || current->set == nullptr)
    {
        if (instrument != nullptr)
        {
            instrument.reset();
            shown.clear();
            repaint();
        }
        return;
    }
    if (current == instrument && current->generation == shownGeneration)
        return;
    instrument = current;
    shownGeneration = current->generation;
    pending = false;
    rebuild();
}

void ZoneEditor::rebuild()
{
    const int keep = selected >= 0 && selected < static_cast<int> (shown.size()) ? shown[static_cast<std::size_t> (selected)].members.front() : -1;
    shown.clear();
    selected = -1;
    if (instrument == nullptr || instrument->set == nullptr)
        return;
    const auto& set = *instrument->set;
    manual = set.zoned;
    std::map<std::pair<int, int>, std::size_t> index;
    for (int id = 0; id < static_cast<int> (set.members.size()); ++id)
    {
        const auto& m = set.members[static_cast<std::size_t> (id)];
        if (m.role == SampleRole::articulation)
            continue;
        const auto key = std::make_pair (m.pitchGroup, m.layer);
        auto found = index.find (key);
        if (found == index.end())
        {
            Zone z;
            z.group = m.pitchGroup;
            z.layer = m.layer;
            z.rootMidi = set.groups[static_cast<std::size_t> (m.pitchGroup)].rootMidi;
            z.name = juce::File (juce::String::fromUTF8 (m.filename.c_str())).getFileNameWithoutExtension();
            if (set.zoned)
            {
                z.low = m.hasKeyRange() ? m.keyLow : -1;
                z.high = m.hasKeyRange() ? m.keyHigh : -1;
            }
            else
            {
                // The keys the automatic mapping gives this pitch group.
                z.low = -1;
                z.high = -1;
                for (int k = 0; k < 128; ++k)
                    if (set.groupFor (k) == m.pitchGroup)
                    {
                        z.low = z.low < 0 ? k : z.low;
                        z.high = k;
                    }
            }
            index[key] = shown.size();
            shown.push_back (std::move (z));
            found = index.find (key);
        }
        shown[found->second].members.push_back (id);
    }
    // Lowest zone first in each row (the draw order and FILL GAPS's order).
    std::stable_sort (shown.begin(), shown.end(), [] (const Zone& a, const Zone& b) { return a.layer != b.layer ? a.layer < b.layer : a.rootMidi < b.rootMidi; });
    for (int i = 0; i < static_cast<int> (shown.size()); ++i)
        if (std::find (shown[static_cast<std::size_t> (i)].members.begin(), shown[static_cast<std::size_t> (i)].members.end(), keep) != shown[static_cast<std::size_t> (i)].members.end())
            selected = i;
    // The keyboard: the samples' roots and an octave either side (at least three octaves);
    // zones that run further (to both ends after FILL GAPS) are clipped at its edges.
    if (! shown.empty())
    {
        double lo = 127.0, hi = 0.0;
        for (const auto& z : shown)
        {
            lo = std::min (lo, z.rootMidi);
            hi = std::max (hi, z.rootMidi);
        }
        lowKey = static_cast<int> (std::floor (lo)) - 12;
        highKey = static_cast<int> (std::ceil (hi)) + 12;
        if (const int missing = 36 - (highKey - lowKey); missing > 0)
        {
            lowKey -= missing / 2;
            highKey += missing - missing / 2;
        }
        lowKey = std::clamp (lowKey, 0, 91);
        highKey = std::clamp (highKey, lowKey + 36, 127);
    }
    repaint();
}

std::optional<double> ZoneEditor::pinnedRoot (const std::string& filename) const
{
    if (instrument != nullptr)
        for (const auto& a : instrument->assignments)
            if (a.filename == filename && a.rootMidi)
                return a.rootMidi;
    return std::nullopt;
}

std::vector<SetAssignment> ZoneEditor::assignmentsFor (const std::vector<Zone>& zones, bool withKeys) const
{
    std::vector<SetAssignment> out;
    if (instrument == nullptr || instrument->set == nullptr)
        return out;
    const auto& set = *instrument->set;
    for (const auto& z : zones)
        for (int id : z.members)
        {
            const auto& m = set.members[static_cast<std::size_t> (id)];
            SetAssignment a;
            a.filename = m.filename;
            a.role = m.role;
            a.layer = z.layer;
            a.rootMidi = pinnedRoot (m.filename);
            if (withKeys && z.low >= 0 && z.high >= z.low)
            {
                a.keyLow = z.low;
                a.keyHigh = z.high;
            }
            out.push_back (std::move (a));
        }
    // Articulations are not on the map: their own corrections stay as they were.
    for (const auto& a : instrument->assignments)
        if (a.role == SampleRole::articulation)
            out.push_back (a);
    return out;
}

void ZoneEditor::commit (const std::vector<Zone>& zones, bool withKeys)
{
    shown = zones;
    manual = withKeys;
    pending = true;
    host.setSetAssignments (layer, assignmentsFor (zones, withKeys));
    repaint();
}

void ZoneEditor::selectZone (int index)
{
    selected = index >= 0 && index < static_cast<int> (shown.size()) ? index : -1;
    repaint();
}

bool ZoneEditor::perform (const juce::String& action)
{
    if (action == "close")
    {
        if (onClose != nullptr)
            onClose();
        return true;
    }
    if (instrument == nullptr || shown.empty())
        return false;
    if (action == "auto")
    {
        if (! manual)
            return false;
        commit (shown, false);
        return true;
    }
    if (action == "manual")
    {
        if (manual)
            return false;
        commit (shown, true);
        return true;
    }
    if (action == "fill")
    {
        // Each row's zones meet half-way across a gap; the outermost reach both ends.
        auto zones = shown;
        std::map<int, std::vector<std::size_t>> rows;
        for (std::size_t i = 0; i < zones.size(); ++i)
            if (zones[i].low >= 0)
                rows[zones[i].layer].push_back (i);
        for (auto& [row, ids] : rows)
        {
            std::sort (ids.begin(), ids.end(), [&] (std::size_t a, std::size_t b) { return zones[a].low < zones[b].low; });
            zones[ids.front()].low = 0;
            zones[ids.back()].high = 127;
            for (std::size_t k = 0; k + 1 < ids.size(); ++k)
            {
                auto& a = zones[ids[k]];
                auto& b = zones[ids[k + 1]];
                if (b.low > a.high + 1)
                {
                    const int mid = (a.high + b.low) / 2;
                    a.high = mid;
                    b.low = mid + 1;
                }
            }
        }
        commit (zones, true);
        return true;
    }
    if (action == "add")
    {
        chooser = std::make_unique<juce::FileChooser> ("Add samples to the set", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.caf");
        juce::Component::SafePointer<ZoneEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe] (const juce::FileChooser& fc) {
                                  if (safe != nullptr && ! fc.getResults().isEmpty())
                                      safe->host.addSamplesToSet (safe->layer, fc.getResults());
                              });
        return true;
    }
    if (action.startsWith ("root"))
    {
        if (selected < 0)
            return false;
        const auto& z = shown[static_cast<std::size_t> (selected)];
        const auto& set = *instrument->set;
        std::vector<SetAssignment> assignments = assignmentsFor (shown, manual);
        for (auto& a : assignments)
            for (int id : z.members)
                if (a.filename == set.members[static_cast<std::size_t> (id)].filename)
                {
                    if (action == "root.auto")
                        a.rootMidi.reset();
                    else
                        a.rootMidi = std::round (z.rootMidi) + (action == "root+" ? 1.0 : -1.0);
                }
        if (action == "root.auto" && ! std::any_of (z.members.begin(), z.members.end(), [&] (int id) { return pinnedRoot (set.members[static_cast<std::size_t> (id)].filename).has_value(); }))
            return false;
        pending = true;
        host.setSetAssignments (layer, std::move (assignments));
        return true;
    }
    return false;
}

juce::Rectangle<float> ZoneEditor::actionArea (const juce::String& action) const
{
    if (action == "close") return closeArea;
    if (action == "auto") return autoArea;
    if (action == "manual") return manualArea;
    if (action == "fill") return fillArea;
    if (action == "add") return addArea;
    if (action == "root-") return rootDown;
    if (action == "root+") return rootUp;
    if (action == "root.auto") return rootAuto;
    return {};
}

void ZoneEditor::resized()
{
    auto r = getLocalBounds().toFloat().reduced (12.0f, 8.0f);
    auto top = r.removeFromTop (30.0f);
    titleArea = top.removeFromLeft (110.0f);
    closeArea = top.removeFromRight (26.0f).withSizeKeepingCentre (26.0f, 26.0f);
    top.removeFromRight (8.0f);
    addArea = top.removeFromRight (118.0f).reduced (0.0f, 3.0f);
    top.removeFromRight (6.0f);
    fillArea = top.removeFromRight (82.0f).reduced (0.0f, 3.0f);
    autoArea = top.removeFromLeft (64.0f).reduced (0.0f, 3.0f);
    manualArea = top.removeFromLeft (74.0f).reduced (0.0f, 3.0f);
    r.removeFromTop (8.0f);
    side = r.removeFromRight (std::min (230.0f, r.getWidth() * 0.3f));
    r.removeFromRight (10.0f);
    rowLabels = r.removeFromLeft (44.0f);
    keyboard = r.removeFromBottom (std::clamp (r.getHeight() * 0.28f, 26.0f, 44.0f));
    lanes = r;
    rowLabels = rowLabels.withTrimmedBottom (keyboard.getHeight());
    auto buttons = side.withTrimmedTop (side.getHeight() - 26.0f).reduced (8.0f, 0.0f);
    const float w = buttons.getWidth() / 3.0f;
    rootDown = buttons.removeFromLeft (w).reduced (2.0f, 0.0f);
    rootUp = buttons.removeFromLeft (w).reduced (2.0f, 0.0f);
    rootAuto = buttons.reduced (2.0f, 0.0f);
}

juce::Rectangle<float> ZoneEditor::laneArea() const { return lanes; }

float ZoneEditor::keyWidth() const
{
    return lanes.getWidth() / static_cast<float> (highKey - lowKey + 1);
}

float ZoneEditor::xForKey (int key) const
{
    return lanes.getX() + static_cast<float> (key - lowKey) * keyWidth();
}

int ZoneEditor::rowOfLayer (int velocityLayer) const
{
    return rowCount() - 1 - velocityLayer;   // the hardest on top
}

int ZoneEditor::layerOfY (float y) const
{
    const int rows = rowCount();
    const float h = lanes.getHeight() / static_cast<float> (rows);
    if (y < lanes.getY())
        return std::min (rows, 7);   // above the top row: a new, harder layer
    const int row = std::clamp (static_cast<int> ((y - lanes.getY()) / h), 0, rows - 1);
    return rows - 1 - row;
}

juce::Rectangle<float> ZoneEditor::zoneBounds (int index) const
{
    if (index < 0 || index >= static_cast<int> (shown.size()))
        return {};
    const auto& z = shown[static_cast<std::size_t> (index)];
    if (z.low < 0)
        return {};
    const int rows = rowCount();
    const float h = lanes.getHeight() / static_cast<float> (rows);
    const int lo = std::max (z.low, lowKey), hi = std::min (z.high, highKey);
    if (hi < lo)
        return {};
    const float x0 = xForKey (lo), x1 = xForKey (hi + 1);
    return { x0 + 1.0f, lanes.getY() + h * static_cast<float> (rowOfLayer (z.layer)) + 4.0f, std::max (4.0f, x1 - x0 - 2.0f), h - 8.0f };
}

void ZoneEditor::paint (juce::Graphics& g)
{
    using namespace design;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (colour::wellA);
    g.fillRoundedRectangle (bounds, 10.0f);

    // Title, AUTO | MANUAL, FILL GAPS, + ADD SAMPLES, close.
    g.setFont (type::popupLabel (12.0f));
    const auto title = "ZONES " + InstrumentHost::layerName (layer);
    g.setColour (light.withAlpha (0.95f));
    g.drawText (title, titleArea, juce::Justification::centredLeft, false);
    g.setColour (colour::identity (layer).arc);
    g.fillRect (juce::Rectangle<float> (titleArea.getX(), titleArea.getCentreY() + 9.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), title), 1.5f));
    auto pill = [&g] (juce::Rectangle<float> r, const juce::String& text, bool on, bool enabled) {
        g.setColour (on ? light.withAlpha (0.16f) : juce::Colours::transparentBlack);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (light.withAlpha (enabled ? 0.22f : 0.08f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (type::popupLabel (10.5f));
        g.setColour (light.withAlpha (enabled ? (on ? 0.95f : 0.7f) : 0.3f));
        g.drawText (text, r, juce::Justification::centred, false);
    };
    const bool any = ! shown.empty();
    pill (autoArea, "AUTO", ! manual, any);
    pill (manualArea, "MANUAL", manual, any);
    pill (fillArea, "FILL GAPS", false, any);
    pill (addArea, "+ ADD SAMPLES", false, instrument != nullptr);
    {
        const auto x = closeArea.reduced (7.5f);
        juce::Path cross;
        cross.startNewSubPath (x.getTopLeft());
        cross.lineTo (x.getBottomRight());
        cross.startNewSubPath (x.getTopRight());
        cross.lineTo (x.getBottomLeft());
        g.setColour (colour::wellText.brighter (0.3f));
        g.strokePath (cross, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (light.withAlpha (0.08f));
    g.fillRect (juce::Rectangle<float> (12.0f, titleArea.getBottom() + 3.0f, bounds.getWidth() - 24.0f, 1.0f));

    // Lanes: one per velocity layer, octave lines behind the bars.
    const int rows = rowCount();
    const float rowH = lanes.getHeight() / static_cast<float> (rows);
    g.setColour (colour::wellB.withAlpha (0.6f));
    g.fillRoundedRectangle (lanes.withBottom (keyboard.getBottom()), 7.0f);
    for (int k = lowKey; k <= highKey; ++k)
        if (k % 12 == 0)
        {
            g.setColour (colour::wellGrid.withAlpha (0.5f));
            g.fillRect (juce::Rectangle<float> (xForKey (k), lanes.getY(), 1.0f, lanes.getHeight()));
        }
    g.setFont (type::popupLabel (9.5f));
    for (int row = 0; row < rows; ++row)
    {
        const int velocityLayer = rows - 1 - row;
        const juce::String label = rows == 1 ? juce::String() : rows == 2 ? (velocityLayer == 1 ? "HARD" : "SOFT") : "VEL " + juce::String (velocityLayer + 1);
        g.setColour (colour::wellText);
        g.drawText (label, rowLabels.withY (lanes.getY() + rowH * static_cast<float> (row)).withHeight (rowH), juce::Justification::centredLeft, false);
        if (row > 0)
        {
            g.setColour (light.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (lanes.getX(), lanes.getY() + rowH * static_cast<float> (row), lanes.getWidth(), 1.0f));
        }
    }
    for (int i = 0; i < static_cast<int> (shown.size()); ++i)
    {
        const auto r = zoneBounds (i);
        if (r.isEmpty())
            continue;
        const auto& z = shown[static_cast<std::size_t> (i)];
        const auto glaze = glazes[static_cast<std::size_t> (z.group) % std::size (glazes)];
        g.setColour (glaze.withAlpha (manual ? 1.0f : 0.82f));
        g.fillRoundedRectangle (r, 6.0f);
        if (i == selected)
        {
            g.setColour (light);
            g.drawRoundedRectangle (r.expanded (1.0f), 7.0f, 2.0f);
            for (const float x : { r.getX() + 3.0f, r.getRight() - 5.0f })
                g.fillRoundedRectangle (juce::Rectangle<float> (x, r.getY() + r.getHeight() * 0.25f, 2.0f, r.getHeight() * 0.5f), 1.0f);
        }
        const auto text = r.reduced (7.0f, 3.0f);
        g.setColour (juce::Colour (0xff1d2324));
        if (text.getWidth() > 18.0f)
        {
            g.setFont (type::popupLabel (10.5f).boldened());
            g.drawText (z.name, text.withHeight (std::min (14.0f, text.getHeight() * 0.5f)), juce::Justification::topLeft, true);
            g.setFont (type::popupLabel (9.5f));
            const auto info = keyName (static_cast<int> (std::lround (z.rootMidi))) + (z.members.size() > 1 ? "  " + juce::String (z.members.size()) + " takes" : juce::String());
            g.setColour (juce::Colour (0xff1d2324).withAlpha (0.75f));
            g.drawText (info, text.withTrimmedTop (text.getHeight() * 0.5f), juce::Justification::bottomLeft, true);
        }
        // The root: a dot on the key it plays as recorded (when inside the bar).
        const int root = static_cast<int> (std::lround (z.rootMidi));
        if (root >= std::max (z.low, lowKey) && root <= std::min (z.high, highKey))
        {
            const float cx = xForKey (root) + keyWidth() * 0.5f;
            g.setColour (juce::Colour (0xff1d2324));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, r.getBottom() - 6.0f }));
            g.setColour (juce::Colours::white);
            g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ cx, r.getBottom() - 6.0f }), 1.3f);
        }
    }
    paintKeyboard (g);
    paintSide (g);
    if (dropHover)
    {
        g.setColour (colour::identity (layer).arc.withAlpha (0.18f));
        g.fillRoundedRectangle (lanes.withBottom (keyboard.getBottom()), 7.0f);
        g.setColour (light.withAlpha (0.9f));
        g.setFont (type::popupLabel (12.0f));
        g.drawText ("Drop to add to the set", lanes, juce::Justification::centred, false);
    }
    else if (shown.empty())
    {
        g.setColour (colour::wellText);
        g.setFont (type::popupLabel (11.5f));
        g.drawText ("No samples", lanes, juce::Justification::centred, false);
    }
}

void ZoneEditor::paintKeyboard (juce::Graphics& g) const
{
    const auto kb = keyboard;
    g.setColour (juce::Colour (0xfff4efe6));
    g.fillRect (kb);
    std::vector<bool> lit (128, false);
    if (selected >= 0 && selected < static_cast<int> (shown.size()))
        for (int k = std::max (0, shown[static_cast<std::size_t> (selected)].low); k <= shown[static_cast<std::size_t> (selected)].high && k < 128; ++k)
            lit[static_cast<std::size_t> (k)] = true;
    const float w = keyWidth();
    for (int k = lowKey; k <= highKey; ++k)
        if (lit[static_cast<std::size_t> (k)])
        {
            g.setColour (juce::Colour (0xfff3d6c8));
            g.fillRect (juce::Rectangle<float> (xForKey (k), kb.getY(), w, kb.getHeight()));
        }
    // Keys drawn by semitone (each key the same width as its zone column), black ones shorter.
    for (int k = lowKey; k <= highKey; ++k)
    {
        const float x = xForKey (k);
        if (isBlack (k))
        {
            g.setColour (juce::Colour (lit[static_cast<std::size_t> (k)] ? 0xff6b4a3c : 0xff2b2a28));
            g.fillRect (juce::Rectangle<float> (x + w * 0.1f, kb.getY(), w * 0.8f, kb.getHeight() * 0.6f));
        }
        else
        {
            // The seam to the next white key: at the key's edge (E-F, B-C), else under the black key.
            g.setColour (juce::Colour (0xffcfc5b6));
            g.fillRect (juce::Rectangle<float> (x + w * (isBlack (k + 1) ? 1.5f : 1.0f) - 0.5f, kb.getY(), 1.0f, kb.getHeight()));
        }
        if (k % 12 == 0)
        {
            g.setColour (juce::Colour (0xff8a847a));
            g.setFont (type::popupLabel (8.5f));
            g.drawText ("C" + juce::String (k / 12 - 1), juce::Rectangle<float> (x + 1.0f, kb.getBottom() - 12.0f, 24.0f, 11.0f), juce::Justification::centredLeft, false);
        }
    }
}

void ZoneEditor::paintSide (juce::Graphics& g) const
{
    using namespace design;
    g.setColour (colour::wellB.withAlpha (0.8f));
    g.fillRoundedRectangle (side, 7.0f);
    auto r = side.reduced (10.0f, 8.0f);
    if (selected < 0 || selected >= static_cast<int> (shown.size()) || instrument == nullptr)
    {
        g.setFont (type::popupLabel (11.0f));
        g.setColour (colour::wellText);
        const auto count = instrument != nullptr && instrument->set != nullptr ? instrument->set->members.size() : 0;
        g.drawFittedText (juce::String (count) + (count == 1 ? " sample" : " samples") + "\n\nClick a zone to hear it. Drag its edges to resize it, "
                              "its body to move it, up or down to another velocity row. Drop files here to add them.",
                          r.toNearestInt(), juce::Justification::topLeft, 8);
        return;
    }
    const auto& z = shown[static_cast<std::size_t> (selected)];
    const auto& set = *instrument->set;
    const auto& first = set.members[static_cast<std::size_t> (z.members.front())];
    g.setFont (type::popupLabel (12.5f).boldened());
    g.setColour (light);
    g.drawText (z.name, r.removeFromTop (16.0f), juce::Justification::centredLeft, true);
    g.setFont (type::popupLabel (9.5f));
    g.setColour (colour::wellText);
    g.drawText (juce::String::fromUTF8 (first.filename.c_str()), r.removeFromTop (14.0f), juce::Justification::centredLeft, true);
    r.removeFromTop (8.0f);
    const int rows = rowCount();
    const int velLow = z.layer * 128 / rows, velHigh = (z.layer + 1) * 128 / rows - 1;
    const auto& continuation = first.model != nullptr ? first.model->original.continuation : ContinuationModel {};
    const juce::String loop = continuation.canSustain ? "Loops" : (continuation.pedal.valid ? "Holds while the key is down" : "Plays once");
    const bool pinned = std::any_of (z.members.begin(), z.members.end(), [&] (int id) { return pinnedRoot (set.members[static_cast<std::size_t> (id)].filename).has_value(); });
    const std::pair<juce::String, juce::String> lines[] = {
        { "KEYS", z.low < 0 ? juce::String ("none") : keyName (z.low) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + keyName (z.high) },
        { "ROOT", keyName (static_cast<int> (std::lround (z.rootMidi))) + (pinned ? " (set)" : " (detected)") },
        { "VELOCITY", juce::String (std::max (1, velLow)) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + juce::String (velHigh) },
        { "TAKES", juce::String (z.members.size()) + (z.members.size() > 1 ? juce::String::fromUTF8 (set.cycleTakes ? " \xc2\xb7 in turn" : " \xc2\xb7 at random") : juce::String()) },
        { "LOOP", loop },
    };
    for (const auto& [k, v] : lines)
    {
        auto line = r.removeFromTop (17.0f);
        g.setFont (type::popupLabel (9.5f));
        g.setColour (colour::wellText);
        g.drawText (k, line.removeFromLeft (64.0f), juce::Justification::centredLeft, false);
        g.setFont (type::popupLabel (10.5f));
        g.setColour (light.withAlpha (0.9f));
        g.drawText (v, line, juce::Justification::centredLeft, true);
    }
    auto button = [&g] (juce::Rectangle<float> b, const juce::String& text, bool enabled) {
        g.setColour (light.withAlpha (enabled ? 0.22f : 0.08f));
        g.drawRoundedRectangle (b.reduced (0.5f), 5.0f, 1.0f);
        g.setFont (type::popupLabel (10.0f));
        g.setColour (light.withAlpha (enabled ? 0.8f : 0.3f));
        g.drawText (text, b, juce::Justification::centred, false);
    };
    button (rootDown, juce::String::fromUTF8 ("ROOT \xe2\x88\x92"), true);
    button (rootUp, "ROOT +", true);
    button (rootAuto, "AUTO", pinned);
}

void ZoneEditor::mouseMove (const juce::MouseEvent& e)
{
    auto cursor = juce::MouseCursor::NormalCursor;
    for (int i = static_cast<int> (shown.size()) - 1; i >= 0; --i)
    {
        const auto r = zoneBounds (i);
        if (r.contains (e.position))
        {
            cursor = e.position.x < r.getX() + 6.0f || e.position.x > r.getRight() - 6.0f ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor;
            break;
        }
    }
    for (const auto& a : { autoArea, manualArea, fillArea, addArea, closeArea, rootDown, rootUp, rootAuto })
        if (a.contains (e.position))
            cursor = juce::MouseCursor::PointingHandCursor;
    setMouseCursor (cursor);
}

void ZoneEditor::mouseDown (const juce::MouseEvent& e)
{
    drag = Drag::none;
    dragChanged = false;
    for (const char* action : { "close", "auto", "manual", "fill", "add" })
        if (actionArea (action).contains (e.position))
        {
            perform (action);
            return;
        }
    if (selected >= 0)
        for (const char* action : { "root-", "root+", "root.auto" })
            if (actionArea (action).contains (e.position))
            {
                perform (action);
                return;
            }
    for (int i = static_cast<int> (shown.size()) - 1; i >= 0; --i)
    {
        const auto r = zoneBounds (i);
        if (! r.contains (e.position))
            continue;
        selected = i;
        dragStart = shown[static_cast<std::size_t> (i)];
        drag = e.position.x < r.getX() + 6.0f ? Drag::left : (e.position.x > r.getRight() - 6.0f ? Drag::right : Drag::move);
        host.auditionSetMember (layer, dragStart.members.front());
        repaint();
        return;
    }
    if (lanes.contains (e.position))
        selectZone (-1);
}

void ZoneEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (drag == Drag::none || selected < 0 || selected >= static_cast<int> (shown.size()))
        return;
    auto& z = shown[static_cast<std::size_t> (selected)];
    const int delta = static_cast<int> (std::lround (e.getDistanceFromDragStartX() / keyWidth()));
    const int low = std::max (0, dragStart.low), high = std::max (low, dragStart.high);
    if (drag == Drag::move)
    {
        const int shift = std::clamp (delta, -low, 127 - high);
        z.low = low + shift;
        z.high = high + shift;
        if (std::abs (e.getDistanceFromDragStartY()) > 8)
            z.layer = layerOfY (e.position.y);
        else
            z.layer = dragStart.layer;
    }
    else if (drag == Drag::left)
        z.low = std::clamp (low + delta, 0, high);
    else
        z.high = std::clamp (high + delta, low, 127);
    dragChanged = z.low != dragStart.low || z.high != dragStart.high || z.layer != dragStart.layer;
    repaint();
}

void ZoneEditor::mouseUp (const juce::MouseEvent&)
{
    if (drag != Drag::none && dragChanged)
    {
        // The first edit makes the map its own: every zone keeps the keys it shows.
        auto zones = shown;
        commit (zones, true);
    }
    drag = Drag::none;
    dragChanged = false;
}

bool ZoneEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return instrument != nullptr && std::any_of (files.begin(), files.end(), [] (const juce::String& f) { return isAudioFile (f); });
}

void ZoneEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    juce::Array<juce::File> audio;
    for (const auto& f : files)
        if (isAudioFile (f))
            audio.add (juce::File (f));
    if (! audio.isEmpty())
        host.addSamplesToSet (layer, audio);
    repaint();
}

} // namespace osp::plugin
