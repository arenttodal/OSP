#include "PluginEditor.h"

#include "core/PitchMath.h"
#include "io/AudioFileIO.h"

#include <map>

namespace osp::plugin
{

namespace
{
    const juce::String dot = juce::String::fromUTF8 ("  \xc2\xb7  ");

    /** A readable time step for the display's grid: 3 to 8 lines over the recording. */
    double gridStep (double seconds)
    {
        for (double step : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0 })
            if (seconds / step <= 8.0)
                return step;
        return 300.0;
    }

    juce::String secondsText (double t, double step)
    {
        // "0 s", "0.5 s", "1 s" (no trailing zeros).
        auto text = juce::String (t, step < 0.5 ? 2 : 1);
        while (text.containsChar ('.') && (text.endsWithChar ('0') || text.endsWithChar ('.')))
            text = text.dropLastCharacters (1);
        return text + " s";
    }
}

//==============================================================================
void WaveformView::setInstrument (std::shared_ptr<const LoadedInstrument> newInstrument)
{
    instrument = std::move (newInstrument);
    cacheDirty = true;
    repaint();
}

void WaveformView::setLoading (bool isLoading)
{
    if (loading != isLoading)
    {
        loading = isLoading;
        cacheDirty = true;
        repaint();
    }
}

void WaveformView::setDragHighlight (bool on)
{
    dragHighlight = on;
    repaint();
}

void WaveformView::setLayer (int newLayer)
{
    if (layer != newLayer)
    {
        layer = newLayer;
        cacheDirty = true;
        repaint();
    }
}

void WaveformView::setGranularView (bool on, float position, float spread)
{
    if (on != granular || std::abs (position - grainPosition) > 1.0e-4f || std::abs (spread - grainSpread) > 1.0e-4f)
    {
        granular = on;
        grainPosition = position;
        grainSpread = spread;
        repaint();
    }
}

juce::Rectangle<float> WaveformView::plotArea() const
{
    // Top row: A/B tabs and the blend (children of the editor); bottom row: file and mode.
    return getLocalBounds().toFloat().reduced (18.0f, 0.0f).withTrimmedTop (44.0f).withTrimmedBottom (30.0f);
}

void WaveformView::setGrains (const GrainDot* dots, int count)
{
    if (count == 0 && grains.empty())
        return;
    grains.assign (dots, dots + count);
    repaint (plotArea().expanded (8.0f).getSmallestIntegerContainer());
}

void WaveformView::setPlayheads (const GrainDot* heads, int count)
{
    if (count == 0 && playheads.empty())
        return;
    playheads.assign (heads, heads + count);
    repaint (plotArea().expanded (8.0f).getSmallestIntegerContainer());
}

void WaveformView::paint (juce::Graphics& g)
{
    using namespace palette;
    const float scale = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = std::max (1, juce::roundToInt (static_cast<float> (getWidth()) * scale));
    const int h = std::max (1, juce::roundToInt (static_cast<float> (getHeight()) * scale));
    if (cacheDirty || ! cache.isValid() || cache.getWidth() != w || cache.getHeight() != h)
    {
        cache = juce::Image (juce::Image::ARGB, w, h, true);
        juce::Graphics cg (cache);
        cg.addTransform (juce::AffineTransform::scale (scale));
        paintStatic (cg);
        cacheDirty = false;
    }
    g.drawImage (cache, getLocalBounds().toFloat());

    const auto bounds = getLocalBounds().toFloat();
    const auto plot = plotArea();
    const bool hasWave = instrument != nullptr && ! instrument->peakMax.empty();
    if (hasWave && granular && ! loading)
    {
        // Where grains may come from: SPREAD as a soft band (half the length either side at
        // 100 %), POS as a line.
        const float x = plot.getX() + grainPosition * plot.getWidth();
        const float half = (grainSpread * 0.5f + 0.004f) * plot.getWidth();
        const auto band = juce::Rectangle<float> (x - half, plot.getY() - 4.0f, 2.0f * half, plot.getHeight() + 8.0f).getIntersection (plot.expanded (0.0f, 4.0f));
        g.setColour (accent.withAlpha (0.08f));
        g.fillRect (band);
        g.setColour (accent.withAlpha (0.55f));
        g.drawVerticalLine (juce::roundToInt (x), plot.getY() - 4.0f, plot.getBottom() + 4.0f);

        // The grains playing now: each a read head - a faint line through the waveform where
        // it reads, and a dot scattered on its own lane, as big and bright as its window.
        for (const auto& grain : grains)
        {
            const float gx = plot.getX() + grain.position * plot.getWidth();
            const float level = std::clamp (grain.level, 0.0f, 1.0f);
            g.setColour (accent.withAlpha (0.12f + 0.38f * level));
            g.drawVerticalLine (juce::roundToInt (gx), plot.getY(), plot.getBottom());
            // Lanes below the granular controls, so no grain hides behind them.
            const float top = plot.getY() + 0.42f * plot.getHeight();
            const float gy = top + grain.lane * (plot.getBottom() - 6.0f - top);
            const float r = 2.5f + 3.5f * level;
            g.setColour (display.withAlpha (0.55f + 0.35f * level));   // a dark ring reads on the pale waveform
            g.fillEllipse (gx - r - 1.5f, gy - r - 1.5f, 2.0f * r + 3.0f, 2.0f * r + 3.0f);
            g.setColour (accent.withAlpha (0.35f + 0.65f * level));
            g.fillEllipse (gx - r, gy - r, 2.0f * r, 2.0f * r);
            g.setColour (juce::Colours::white.withAlpha (0.6f * level));
            g.fillEllipse (gx - 0.35f * r, gy - 0.35f * r, 0.7f * r, 0.7f * r);
        }
    }

    if (hasWave && ! loading)
    {
        // One Shot: a read head per playing note, following the recording (and jumping
        // where the sustain loops or the release joins the ending). Brightness = the note's level.
        for (const auto& head : playheads)
        {
            const float x = plot.getX() + head.position * plot.getWidth();
            const float level = 0.25f + 0.75f * std::clamp (head.level, 0.0f, 1.0f);
            g.setColour (accent.withAlpha (0.18f * level));
            g.fillRect (juce::Rectangle<float> (x - 3.0f, plot.getY() - 4.0f, 6.0f, plot.getHeight() + 8.0f));
            g.setColour (accent.withAlpha (0.95f * level));
            g.fillRect (juce::Rectangle<float> (x - 0.75f, plot.getY() - 4.0f, 1.5f, plot.getHeight() + 8.0f));
            juce::Path cap;
            cap.addTriangle (x - 4.0f, plot.getY() - 9.0f, x + 4.0f, plot.getY() - 9.0f, x, plot.getY() - 3.0f);
            g.fillPath (cap);
        }
    }

    if (dragHighlight)
    {
        g.setColour (accent);
        g.drawRoundedRectangle (bounds.reduced (1.5f), 7.0f, 2.5f);
    }
}

void WaveformView::paintStatic (juce::Graphics& g)
{
    using namespace palette;
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (display);
    g.fillRoundedRectangle (bounds, 7.0f);
    // Inset: shade under the top edge, a fine dark rim.
    g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.38f), 0.0f, bounds.getY(),
                                             juce::Colours::transparentBlack, 0.0f, bounds.getY() + 16.0f, false));
    g.fillRoundedRectangle (bounds.withHeight (16.0f), 7.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 7.0f, 1.0f);

    const auto plot = plotArea();
    const bool hasWave = instrument != nullptr && ! instrument->peakMax.empty();

    if (hasWave && instrument->durationSeconds > 0.0)
    {
        // Time grid with second markers, note information top right.
        const double duration = instrument->durationSeconds;
        const double step = gridStep (duration);
        g.setFont (fonts::make (11.0f));
        for (double t = 0.0; t < duration - 0.25 * step; t += step)
        {
            const float x = plot.getX() + static_cast<float> (t / duration) * plot.getWidth();
            g.setColour (displayLine);
            g.drawVerticalLine (juce::roundToInt (x), bounds.getY() + 28.0f, bounds.getBottom() - 26.0f);
            g.setColour (displayText);
            g.drawText (secondsText (t, step), juce::Rectangle<float> (x + 5.0f, bounds.getY() + 28.0f, 60.0f, 14.0f),
                        juce::Justification::centredLeft, false);
        }
        juce::Path centre;
        centre.startNewSubPath (bounds.getX() + 10.0f, plot.getCentreY());
        centre.lineTo (bounds.getRight() - 10.0f, plot.getCentreY());
        juce::Path dashed;
        const float dashes[] = { 2.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, centre, dashes, 2);
        g.setColour (displayLine.withAlpha (0.8f));
        g.fillPath (dashed);

        // Length, under the blend.
        g.setColour (displayText);
        g.drawText (juce::String (duration, duration < 10.0 ? 2 : 1) + " s", bounds.reduced (14.0f, 0.0f).withTrimmedTop (28.0f).withHeight (14.0f),
                    juce::Justification::centredRight, false);
    }

    if (hasWave)
    {
        const auto& lo = instrument->peakMin;
        const auto& hi = instrument->peakMax;
        const float mid = plot.getCentreY();
        float maxAbs = 1.0e-4f; // display is scaled to the recording's own peak so quiet sources stay visible
        for (std::size_t i = 0; i < hi.size(); ++i)
            maxAbs = std::max ({ maxAbs, hi[i], -lo[i] });
        const float scale = plot.getHeight() * 0.48f / maxAbs;
        const int x0 = static_cast<int> (plot.getX()), width = static_cast<int> (plot.getWidth());
        g.setColour (wave.withAlpha (loading ? 0.25f : 0.92f));
        for (int x = 0; x < width; ++x)
        {
            const auto b = std::min (static_cast<std::size_t> (static_cast<double> (x) / width * static_cast<double> (hi.size())), hi.size() - 1);
            const float top = mid - hi[b] * scale;
            const float bottom = mid - lo[b] * scale;
            g.drawVerticalLine (x0 + x, top, std::max (top + 1.0f, bottom));
        }
        if (instrument->startSeconds > 0.0 && instrument->durationSeconds > 0.0)
        {
            // Notes start here (analysed onset), not at the beginning of the file.
            const float x = plot.getX() + static_cast<float> (instrument->startSeconds / instrument->durationSeconds) * plot.getWidth();
            g.setColour (accent.withAlpha (0.85f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY() - 4.0f, plot.getBottom() + 4.0f);
        }
    }

    {
        // The edited layer and its file, bottom left.
        auto row = bounds.reduced (12.0f, 0.0f).withTrimmedTop (bounds.getHeight() - 24.0f).withHeight (16.0f);
        g.setColour (housing);
        g.setFont (fonts::make (11.5f, fonts::Weight::semibold));
        g.drawText (OspAudioProcessor::layerName (layer), row.removeFromLeft (14.0f), juce::Justification::centredLeft, false);
        g.setColour (displayText);
        g.setFont (fonts::make (11.5f));
        const auto name = instrument != nullptr ? juce::String::fromUTF8 (instrument->filename.c_str()) : juce::String ("No sample loaded");
        g.drawText (name, row.withWidth (std::min (row.getWidth(), bounds.getWidth() * 0.55f)), juce::Justification::centredLeft, true);
    }

    if (! hasWave && ! loading)
    {
        // First-run guidance (spec §90 onboarding): three steps, no manual needed.
        auto area = getLocalBounds().reduced (12);
        const int lineHeight = 20;
        auto block = area.withSizeKeepingCentre (area.getWidth(), 34 + 3 * lineHeight + 8);
        g.setColour (housing);
        g.setFont (fonts::make (26.0f, fonts::Weight::semibold, 0.12f));
        g.drawText ("DROP A SOUND", block.removeFromTop (34), juce::Justification::centred);
        block.removeFromTop (8);
        g.setColour (displayText);
        g.setFont (fonts::make (13.5f));
        for (const auto* line : { "1  One tonal recording (WAV, AIFF, FLAC) \xe2\x80\x94 or several takes, or a folder",
                                  "2  Play and hold: notes keep going, repeated notes never sound the same",
                                  "3  Shape it with LIFE, DYNAMICS, CHARACTER, MOVEMENT and SPACE \xe2\x80\x94 click a name for more" })
            g.drawText (juce::String::fromUTF8 (line), block.removeFromTop (lineHeight), juce::Justification::centred);
    }

    if (loading)
    {
        g.setColour (housing);
        g.setFont (fonts::make (18.0f, fonts::Weight::semibold, 0.14f));
        g.drawText (juce::String::fromUTF8 ("ANALYZING\xe2\x80\xa6"), getLocalBounds(), juce::Justification::centred);
    }

}

//==============================================================================
SamplesPanel::SamplesPanel (OspAudioProcessor& p) : ospProcessor (p)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

void SamplesPanel::paint (juce::Graphics& g)
{
    g.setColour (palette::display);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 7.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 7.0f, 1.0f);
}

void SamplesPanel::setInstrument (std::shared_ptr<const LoadedInstrument> instrument)
{
    if (instrument == nullptr || instrument->set == nullptr)
    {
        rows.clear();
        content.removeAllChildren();
        shownGeneration = 0;
        return;
    }
    if (instrument->generation == shownGeneration)
        return;
    shownGeneration = instrument->generation;
    rows.clear();
    content.removeAllChildren();
    const auto& set = *instrument->set;
    for (const auto& group : set.groups)
    {
        auto header = std::make_unique<Row>();
        header->isHeader = true;
        header->name.setText (juce::String (midiNoteName (static_cast<int> (std::lround (group.rootMidi))))
                                  + (group.layers > 1 ? "   " + juce::String (group.layers) + " velocity layers" : juce::String()),
                              juce::dontSendNotification);
        header->name.setFont (fonts::make (14.0f, fonts::Weight::semibold));
        header->name.setColour (juce::Label::textColourId, palette::housingLight);
        content.addAndMakeVisible (header->name);
        rows.push_back (std::move (header));
        for (int id : group.members)
        {
            const auto& member = set.members[static_cast<std::size_t> (id)];
            auto row = std::make_unique<Row>();
            row->name.setText (juce::String::fromUTF8 (member.filename.c_str()), juce::dontSendNotification);
            row->name.setColour (juce::Label::textColourId, palette::housingLight);
            row->info.setText (member.userAssigned ? juce::String ("set by you")
                                                   : "auto " + juce::String (static_cast<int> (std::lround (member.confidence * 100))) + "%",
                               juce::dontSendNotification);
            row->info.setColour (juce::Label::textColourId, palette::displayText);
            row->role.addItemList ({ "Pitch", "Velocity layer", "Round robin", "Articulation" }, 1);
            row->role.setSelectedId (static_cast<int> (member.role) + 1, juce::dontSendNotification);
            for (int l = 1; l <= 4; ++l)
                row->layer.addItem ("Layer " + juce::String (l), l);
            row->layer.setSelectedId (std::clamp (member.layer + 1, 1, 4), juce::dontSendNotification);
            // Root: the detected pitch, or a correction (octave errors on unusual sources).
            row->root.addItem ("root auto", 1);
            for (int note = 24; note <= 108; ++note)
                row->root.addItem ("root " + juce::String (midiNoteName (note)), note + 2);
            int pinnedRoot = 1;
            for (const auto& a : instrument->assignments)
                if (a.filename == member.filename && a.rootMidi)
                    pinnedRoot = static_cast<int> (std::lround (*a.rootMidi)) + 2;
            row->root.setSelectedId (pinnedRoot, juce::dontSendNotification);
            const auto filename = member.filename;
            auto* rolePtr = &row->role;
            auto* layerPtr = &row->layer;
            auto* rootPtr = &row->root;
            auto apply = [this, filename, rolePtr, layerPtr, rootPtr] {
                std::optional<double> root;
                if (rootPtr->getSelectedId() >= 2)
                    root = static_cast<double> (rootPtr->getSelectedId() - 2);
                ospProcessor.reassignSample (filename, static_cast<SampleRole> (rolePtr->getSelectedId() - 1), layerPtr->getSelectedId() - 1, root);
            };
            row->role.onChange = apply;
            row->layer.onChange = apply;
            row->root.onChange = apply;
            for (juce::Component* c : { static_cast<juce::Component*> (&row->name), static_cast<juce::Component*> (&row->info),
                                        static_cast<juce::Component*> (&row->role), static_cast<juce::Component*> (&row->layer),
                                        static_cast<juce::Component*> (&row->root) })
                content.addAndMakeVisible (c);
            rows.push_back (std::move (row));
        }
    }
    resized();
}

void SamplesPanel::resized()
{
    viewport.setBounds (getLocalBounds().reduced (8));
    const int width = viewport.getWidth() - viewport.getScrollBarThickness();
    int y = 0;
    for (auto& row : rows)
    {
        if (row->isHeader)
        {
            row->name.setBounds (0, y + 6, width, 22);
            y += 30;
            continue;
        }
        auto line = juce::Rectangle<int> (0, y, width, 26);
        row->layer.setBounds (line.removeFromRight (90));
        line.removeFromRight (6);
        row->root.setBounds (line.removeFromRight (100));
        line.removeFromRight (6);
        row->role.setBounds (line.removeFromRight (130));
        line.removeFromRight (6);
        row->info.setBounds (line.removeFromRight (90));
        row->name.setBounds (line.withTrimmedLeft (12));
        y += 30;
    }
    content.setSize (width, std::max (y, 10));
}

//==============================================================================
void OspKeyboard::drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver,
                                 juce::Colour, juce::Colour)
{
    using namespace palette;
    auto top = ivory, bottom = juce::Colour (0xffe6dfd1);
    if (isDown)
    {
        top = juce::Colour (0xfff6d9c6);
        bottom = juce::Colour (0xfff0c3a6);
    }
    else if (isOver)
        top = top.brighter (0.05f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, area.getY(), bottom, 0.0f, area.getBottom(), false));
    g.fillRect (area);
    // Key front at the bottom, separator on the right.
    g.setColour (juce::Colours::black.withAlpha (0.08f));
    g.fillRect (area.withTop (area.getBottom() - 3.0f));
    g.setColour (border.withAlpha (0.9f));
    g.drawVerticalLine (static_cast<int> (area.getRight()), area.getY(), area.getBottom());

    const auto text = getWhiteNoteText (note);
    if (text.isNotEmpty())
    {
        g.setColour (isDown ? accent.darker (0.3f) : textDim);
        g.setFont (fonts::make (std::min (10.5f, area.getWidth() * 0.62f), fonts::Weight::medium));
        g.drawText (text, area.withTrimmedLeft (3.0f).withTrimmedBottom (5.0f).removeFromBottom (12.0f), juce::Justification::centredLeft, false);
    }
}

void OspKeyboard::drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour)
{
    using namespace palette;
    auto r = area.reduced (0.5f, 0.0f);
    auto top = isDown ? accent.darker (0.35f) : juce::Colour (0xff3b3c3f);
    auto bottom = isDown ? accent.darker (0.55f) : juce::Colour (0xff1b1c1e);
    if (isOver && ! isDown)
        top = top.brighter (0.15f);
    g.setGradientFill (juce::ColourGradient (top, 0.0f, r.getY(), bottom, 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle (r.withTrimmedTop (-3.0f), 2.0f);
    // A lit bevel near the front edge.
    const auto front = r.withTop (r.getBottom() - r.getHeight() * 0.12f).reduced (r.getWidth() * 0.12f, 0.0f);
    g.setColour (juce::Colours::white.withAlpha (isDown ? 0.08f : 0.16f));
    g.fillRoundedRectangle (front.withTrimmedBottom (2.0f), 1.5f);
}

juce::String OspKeyboard::getWhiteNoteText (int note)
{
    // MIDI 60 = C4, as everywhere else in OSP.
    return note % 12 == 0 ? "C" + juce::String (note / 12 - 1) : juce::String();
}

//==============================================================================
void MenuButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId), highlighted, down);
    const auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (palette::text);
    for (int i = -1; i <= 1; ++i)
        g.fillRoundedRectangle (c.x - 7.0f, c.y + static_cast<float> (i) * 4.5f - 0.75f, 14.0f, 1.5f, 0.75f);
}

//==============================================================================
void OspAudioProcessorEditor::OutsideClickWatcher::mouseDown (const juce::MouseEvent& e)
{
    auto* c = e.eventComponent;
    if (editor.popup == nullptr)
    {
        editor.closedByLabelPress = -1;
        return;
    }
    if (c == editor.popup.get() || editor.popup->isParentOf (c))
        return;
    int pressed = -1;
    for (int i = 0; i < 5; ++i)
        if (c == editor.macros[static_cast<std::size_t> (i)].label.get())
            pressed = i;
    if (c == &editor.advancedButton)
        pressed = advancedPopup;
    editor.closedByLabelPress = pressed == editor.popupIndex ? pressed : -1;
    editor.closePopup();
}

OspAudioProcessorEditor::OspAudioProcessorEditor (OspAudioProcessor& p)
    : AudioProcessorEditor (p),
      ospProcessor (p),
      keyboard (p.keyboardState)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (waveform);

    for (auto* label : { &rootLabel, &titleLabel, &characterLabel, &detailLabel, &statusLabel })
    {
        label->setColour (juce::Label::textColourId, palette::text);
        label->setBorderSize ({ 0, 0, 0, 0 });
        addAndMakeVisible (*label);
    }
    rootLabel.setFont (fonts::make (46.0f, fonts::Weight::semibold, -0.01f));
    titleLabel.setFont (fonts::make (17.0f, fonts::Weight::semibold, 0.06f));
    titleLabel.setText ("OSP/2-OSP", juce::dontSendNotification);
    characterLabel.setFont (fonts::make (11.5f, fonts::Weight::medium, 0.14f));
    detailLabel.setFont (fonts::make (12.5f));
    detailLabel.setColour (juce::Label::textColourId, palette::textDim);
    statusLabel.setFont (fonts::make (12.5f));
    statusLabel.setColour (juce::Label::textColourId, palette::textDim);

    addChildComponent (samplesPanel);

    // A/B layers inside the display: tabs (edit focus), blend, mode switch, granular overlay.
    layerTabs.setTitle ("Edit layer");
    layerTabs.onSelect = [this] (int layer) {
        ospProcessor.setEditLayer (layer);
        showLayer (layer);
    };
    addAndMakeVisible (layerTabs);
    addAndMakeVisible (blendControl);

    // Starting states (spec §101) and the menu (presets, instrument files, undo, size).
    for (int i = 0; i < ospProcessor.getNumPrograms(); ++i)
        stateBox.addItem (ospProcessor.getProgramName (i), i + 1);
    stateBox.setSelectedId (ospProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
    stateBox.setTitle ("Starting state");
    stateBox.setTooltip ("Starting states change how the instrument behaves; your sound stays");
    stateBox.onChange = [this] { ospProcessor.setCurrentProgram (stateBox.getSelectedId() - 1); };
    addAndMakeVisible (stateBox);
    menuButton.setTitle ("Menu");
    menuButton.setTooltip ("Presets, instrument files, undo, size");
    menuButton.onClick = [this] { showMenu(); };
    addAndMakeVisible (menuButton);
    setWantsKeyboardFocus (true);

    // The instrument's primary controls. A name with settings behind it opens its popup.
    const std::array<std::pair<const char*, const char*>, 6> macroInfo { {
        { "life", "LIFE" }, { "dynamics", "DYNAMICS" }, { "character", "CHARACTER" },
        { "motion", "MOVEMENT" }, { "space", "SPACE" }, { "reimagined", "ORIGINAL / REIMAGINED" } } };
    const std::array<const char*, 5> popupHints { "Life: how performances vary", "Dynamics: velocity curve, envelope, tone",
                                                  "Character: filter type, range, resonance, drive, envelope",
                                                  "Movement: drift, tape, chorus or pulse", "Space: room, chamber, plate or spring" };
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        auto& knob = macros[i];
        const bool hasPopup = i < 5;
        knob.label = std::make_unique<MacroLabel> (macroInfo[i].second, hasPopup, ! hasPopup);
        knob.label->setInterceptsMouseClicks (hasPopup, false);
        if (hasPopup)
        {
            knob.label->setTooltip (popupHints[i]);
            const int index = static_cast<int> (i);
            knob.label->onClick = [this, index] {
                if (closedByLabelPress == index)
                {
                    closedByLabelPress = -1;
                    return;
                }
                openPopup (index);
            };
        }
        addAndMakeVisible (*knob.label);

        knob.slider.setRotaryParameters (OspLookAndFeel::rotaryStart, OspLookAndFeel::rotaryEnd, true);
        knob.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 20);
        knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ospProcessor.parameters, macroInfo[i].first, knob.slider);
        if (auto* param = ospProcessor.parameters.getParameter (macroInfo[i].first))
            knob.slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue())); // resets the amount only
        knob.slider.setTitle (macroInfo[i].second);
        addAndMakeVisible (knob.slider);
    }

    advancedButton.getProperties().set ("caps", true);
    advancedButton.setTitle ("Advanced settings");
    advancedButton.setTooltip ("Velocity range, tuning, bend, output, pitch character, sustain, MPE");
    advancedButton.onClick = [this] {
        if (closedByLabelPress == advancedPopup)
        {
            closedByLabelPress = -1;
            return;
        }
        openPopup (advancedPopup);
    };
    addAndMakeVisible (advancedButton);

    keyboard.setAvailableRange (21, 108);
    keyboard.setOctaveForMiddleC (4); // MIDI 60 = C4, as everywhere else in OSP
    keyboard.setScrollButtonsVisible (false);
    keyboard.setBlackNoteLengthProportion (0.6f);
    keyboard.setColour (juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    keyboard.setColour (juce::MidiKeyboardComponent::keySeparatorLineColourId, palette::border);
    keyboard.setColour (juce::MidiKeyboardComponent::whiteNoteColourId, palette::ivory);
    keyboard.setColour (juce::MidiKeyboardComponent::blackNoteColourId, palette::ebony);
    addAndMakeVisible (keyboard);

    // Housing grain: a fixed speckle tile (UI only).
    texture = juce::Image (juce::Image::ARGB, 128, 128, true);
    juce::Random grain (0x05b);
    for (int y = 0; y < texture.getHeight(); ++y)
        for (int x = 0; x < texture.getWidth(); ++x)
            if (grain.nextFloat() < 0.35f)
                texture.setPixelAt (x, y, (grain.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (0.02f + 0.025f * grain.nextFloat()));

    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClicks);

    setResizable (true, true);
    setResizeLimits (760, 540, 1800, 1200);
    setSize (900, 620);
    setScaleFactor (ospProcessor.uiScale());

    showLayer (ospProcessor.editLayer());
    if (ospProcessor.advancedOpen())
        openPopup (advancedPopup);
    refreshInstrumentInfo();
    updateCustomisedDots();
    startTimerHz (30);   // the granular cloud moves at the display rate
}

OspAudioProcessorEditor::~OspAudioProcessorEditor()
{
    stopTimer();
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClicks);
    popup.reset();
    setLookAndFeel (nullptr);
}

void OspAudioProcessorEditor::openPopup (int which)
{
    const bool keepAdvanced = which == advancedPopup;
    closePopup();
    if (which < 0 || which > advancedPopup)
        return;
    popup = which == advancedPopup ? createAdvancedPopup (ospProcessor) : createMacroPopup (static_cast<MacroPopup> (which), ospProcessor);
    popupIndex = which;
    addAndMakeVisible (*popup);
    popup->toFront (false);
    positionPopup();
    if (which < 5)
        macros[static_cast<std::size_t> (which)].label->setOpen (true);
    if (keepAdvanced)
    {
        ospProcessor.setAdvancedOpen (true);
        advancedButton.setToggleState (true, juce::dontSendNotification);
    }
    if (isShowing())
        grabKeyboardFocus(); // Escape closes it
}

void OspAudioProcessorEditor::closePopup()
{
    if (popup == nullptr)
        return;
    if (popupIndex >= 0 && popupIndex < 5)
        macros[static_cast<std::size_t> (popupIndex)].label->setOpen (false);
    if (popupIndex == advancedPopup)
    {
        ospProcessor.setAdvancedOpen (false);
        advancedButton.setToggleState (false, juce::dontSendNotification);
    }
    popup.reset();
    popupIndex = -1;
}

void OspAudioProcessorEditor::positionPopup()
{
    if (popup == nullptr)
        return;
    const auto size = popup->cardSize();
    const int m = MiniPanel::shadowMargin;
    const auto anchor = popupIndex < 5 ? macros[static_cast<std::size_t> (popupIndex)].label->getBounds() : advancedButton.getBounds();
    int x = popupIndex < 5 ? anchor.getCentreX() - size.x / 2 : anchor.getX();
    int y = anchor.getY() - 6 - size.y;
    const auto limits = getLocalBounds().reduced (14);
    x = juce::jlimit (limits.getX(), std::max (limits.getX(), limits.getRight() - size.x), x);
    y = std::max (limits.getY(), y);
    popup->setBounds (x - m, y - m, size.x + 2 * m, size.y + 2 * m);
}

void OspAudioProcessorEditor::updateCustomisedDots()
{
    for (int i = 0; i < 5; ++i)
    {
        bool customised = false;
        for (const auto& id : popupParameterIds (static_cast<MacroPopup> (i)))
            if (auto* param = ospProcessor.parameters.getParameter (id))
                customised = customised || std::abs (param->getValue() - param->getDefaultValue()) > 1.0e-4f;
        macros[static_cast<std::size_t> (i)].label->setCustomised (customised);
    }
}

void OspAudioProcessorEditor::showLayer (int layer)
{
    shownLayer = layer;
    layerTabs.setSelected (layer);
    waveform.setLayer (layer);
    if (auto* param = ospProcessor.parameters.getParameter (OspAudioProcessor::layerParameterId (layer, "sourceMode")))
    {
        modeSwitch = std::make_unique<SourceModeSwitch> (*param);
        modeSwitch->setTooltip ("Layer " + OspAudioProcessor::layerName (layer) + ": play the recording through, or as grains");
        modeSwitch->onChange = [this] (int) { updateGranularView(); };
        addAndMakeVisible (*modeSwitch);
    }
    granularOverlay = std::make_unique<GranularOverlay> (ospProcessor.parameters, layer);
    addChildComponent (*granularOverlay);
    samplesShown = false;
    samplesPanel.setVisible (false);
    if (popup != nullptr)
        popup->toFront (false);
    refreshInstrumentInfo();
    resized();
    updateGranularView();
}

void OspAudioProcessorEditor::updateGranularView()
{
    if (shownLayer < 0)
        return;
    auto value = [this] (const char* name) {
        auto* p = ospProcessor.parameters.getParameter (OspAudioProcessor::layerParameterId (shownLayer, name));
        return p != nullptr ? p->convertFrom0to1 (p->getValue()) : 0.0f;
    };
    const bool granular = value ("sourceMode") >= 0.5f;
    waveform.setGranularView (granular, 0.01f * value ("granular.position"), 0.01f * value ("granular.spread"));
    if (granularOverlay != nullptr && granularOverlay->isVisible() != granular)
        granularOverlay->setVisible (granular);
}

void OspAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace palette;
    g.fillAll (frame);

    // Housing: rounded chassis, lit from above, a little grain.
    const auto housingBounds = getLocalBounds().toFloat().reduced (5.0f);
    juce::Path shell;
    shell.addRoundedRectangle (housingBounds, 14.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 10, { 0, 3 }).drawForPath (g, shell);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffede5d6), 0.0f, housingBounds.getY(),
                                             juce::Colour (0xffe1d6c3), 0.0f, housingBounds.getBottom(), false));
    g.fillPath (shell);
    if (texture.isValid())
    {
        g.setTiledImageFill (texture, 0, 0, 1.0f);
        g.fillPath (shell);
    }
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.drawLine (housingBounds.getX() + 14.0f, housingBounds.getY() + 1.0f, housingBounds.getRight() - 14.0f, housingBounds.getY() + 1.0f, 1.2f);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.strokePath (shell, juce::PathStrokeType (1.0f));

    // Divider between the pitch and the description.
    if (rootLabel.getText().isNotEmpty())
    {
        const float x = static_cast<float> (rootLabel.getRight()) + 8.0f;
        g.setColour (border);
        g.drawLine (x, static_cast<float> (rootLabel.getY()) + 8.0f, x, static_cast<float> (rootLabel.getBottom()) - 6.0f, 1.0f);
    }

    // Bezel around the display (a slight recess into the housing).
    const auto bezel = waveform.getBounds().toFloat().expanded (3.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xffbfb39c), 0.0f, bezel.getY(), juce::Colour (0xfff4eee4), 0.0f, bezel.getBottom(), false));
    g.fillRoundedRectangle (bezel, 9.0f);

    // Keyboard bed.
    const auto bed = keyboard.getBounds().toFloat().expanded (3.0f);
    g.setColour (juce::Colour (0xff2c2e31));
    g.fillRoundedRectangle (bed, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.drawLine (bed.getX() + 5.0f, bed.getBottom() + 1.0f, bed.getRight() - 5.0f, bed.getBottom() + 1.0f, 1.0f);

    // Status light: orange when ready, dim while empty or loading.
    const auto state = ospProcessor.loadState();
    const auto light = juce::Rectangle<float> (static_cast<float> (statusLabel.getX()) - 14.0f, static_cast<float> (statusLabel.getBounds().getCentreY()) - 4.0f, 8.0f, 8.0f);
    g.setColour (state == OspAudioProcessor::LoadState::ready ? accent : (state == OspAudioProcessor::LoadState::failed ? accent.darker (0.5f) : border));
    g.fillEllipse (light);
    g.setColour (juce::Colours::black.withAlpha (0.2f));
    g.drawEllipse (light, 0.8f);
}

void OspAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (5).reduced (22, 14);

    // Top: pitch, title and description; on the right only the starting state and the menu.
    auto header = area.removeFromTop (62);
    {
        auto right = header.removeFromRight (176).withSizeKeepingCentre (176, 26).translated (0, -8);
        menuButton.setBounds (right.removeFromRight (34));
        right.removeFromRight (8);
        stateBox.setBounds (right);
    }
    rootLabel.setBounds (header.removeFromLeft (std::max (64, juce::roundToInt (juce::GlyphArrangement::getStringWidth (rootLabel.getFont(), rootLabel.getText())) + 6)));
    header.removeFromLeft (18);
    auto description = header.withSizeKeepingCentre (header.getWidth(), 52);
    titleLabel.setBounds (description.removeFromTop (22));
    characterLabel.setBounds (description.removeFromTop (16));
    detailLabel.setBounds (description);

    area.removeFromTop (12);

    // Bottom zone: keyboard, then status (left) and Advanced (right, under the keyboard).
    auto bottom = area.removeFromBottom (22);
    advancedButton.setBounds (bottom.removeFromRight (104));
    statusLabel.setBounds (bottom.withTrimmedLeft (16));
    area.removeFromBottom (6);
    keyboard.setBounds (area.removeFromBottom (72).reduced (3, 0));
    keyboard.setKeyWidth (static_cast<float> (keyboard.getWidth()) / 52.0f); // 88 keys = 52 white keys
    area.removeFromBottom (14);

    // Macro row.
    auto macroRow = area.removeFromBottom (150);
    const int macroWidth = macroRow.getWidth() / static_cast<int> (macros.size());
    for (auto& knob : macros)
    {
        auto cell = macroRow.removeFromLeft (macroWidth);
        // ORIGINAL <-> REIMAGINED may use the margin beside the last cell (its label is not clickable).
        knob.label->setBounds (cell.removeFromTop (20).expanded (&knob == &macros.back() ? 14 : 0, 0));
        cell.removeFromTop (2);
        knob.slider.setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 108), cell.getHeight()));
    }
    area.removeFromBottom (12);

    // The display and the layer controls inside it.
    waveform.setBounds (area.reduced (3, 3));
    const auto display = waveform.getBounds();
    layerTabs.setBounds (display.getX() + 12, display.getY() + 8, 42, 16);
    blendControl.setBounds (display.getRight() - 12 - 132, display.getY() + 7, 132, 18);
    if (modeSwitch != nullptr)
        modeSwitch->setBounds (display.getRight() - 12 - 168, display.getBottom() - 24, 168, 16);
    if (granularOverlay != nullptr)
    {
        const auto size = GranularOverlay::preferredSize();
        granularOverlay->setBounds (display.getCentreX() - size.x / 2, display.getY() + 44, size.x, size.y);
    }
    samplesPanel.setBounds (display.withTrimmedTop (28).withTrimmedBottom (28).reduced (4, 0));
    positionPopup();
}

bool OspAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).isDirectory() || io::isSupportedAudioExtension (std::filesystem::path (f.toStdString())))
            return true;
    return false;
}

void OspAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    waveform.setDragHighlight (false);
    juce::Array<juce::File> audioFiles;
    for (const auto& f : files)
    {
        const juce::File file (f);
        if (file.isDirectory())
        {
            // A dropped folder is a set: every supported file directly inside it.
            for (const auto& entry : juce::RangedDirectoryIterator (file, false, "*", juce::File::findFiles))
                if (io::isSupportedAudioExtension (std::filesystem::path (entry.getFile().getFullPathName().toStdString())))
                    audioFiles.add (entry.getFile());
        }
        else if (io::isSupportedAudioExtension (std::filesystem::path (f.toStdString())))
            audioFiles.add (file);
    }
    if (! audioFiles.isEmpty())
        ospProcessor.loadFiles (audioFiles);
}

bool OspAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && popup != nullptr)
    {
        // Closed asynchronously: the key may have come through one of the popup's children.
        juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closePopup(); });
        return true;
    }
    const bool command = key.getModifiers().isCommandDown();
    if (command && key.getKeyCode() == 'Z')
    {
        if (key.getModifiers().isShiftDown())
            ospProcessor.undoManager.redo();
        else
            ospProcessor.undoManager.undo();
        refreshInstrumentInfo();
        return true;
    }
    return false;
}

void OspAudioProcessorEditor::showMenu()
{
    juce::PopupMenu menu;

    // The edited layer's sample (spec: header actions live here).
    const int layer = ospProcessor.editLayer();
    const auto layerLetter = OspAudioProcessor::layerName (layer);
    const auto loaded = ospProcessor.currentInstrument();
    menu.addSectionHeader ("Layer " + layerLetter);
    menu.addItem ((loaded != nullptr ? "Replace sample in " : "Load sample into ") + layerLetter + juce::String::fromUTF8 ("\xe2\x80\xa6"), [this] { chooseFile(); });
    menu.addItem ("Load example into " + layerLetter, [this] { ospProcessor.loadExample(); });
    menu.addItem ("Clear layer " + layerLetter, loaded != nullptr, false, [this] {
        ospProcessor.clearLayer();
        refreshInstrumentInfo();
    });
    menu.addItem ("Samples", loaded != nullptr && loaded->set != nullptr, samplesShown, [this] {
        samplesShown = ! samplesShown;
        samplesPanel.setVisible (samplesShown);
    });
    {
        juce::PopupMenu root;
        const auto overrideMidi = ospProcessor.rootOverride();
        root.addItem ("Auto (detected)", true, ! overrideMidi.has_value(), [this] {
            ospProcessor.changeRootOverride (std::nullopt);
            refreshInstrumentInfo();
        });
        for (int octave = 0; octave <= 8; ++octave)
        {
            juce::PopupMenu notes;
            for (int note = 12 * (octave + 1); note < 12 * (octave + 2) && note < 128; ++note)
                notes.addItem (juce::String (midiNoteName (note)), true, overrideMidi && std::lround (*overrideMidi) == note, [this, note] {
                    ospProcessor.changeRootOverride (static_cast<double> (note));
                    refreshInstrumentInfo();
                });
            root.addSubMenu ("Octave " + juce::String (octave), notes);
        }
        menu.addSubMenu ("Root " + layerLetter + (overrideMidi ? ": " + juce::String (midiNoteName (static_cast<int> (std::lround (*overrideMidi)))) : juce::String (": auto")), root,
                         loaded != nullptr);
    }
    menu.addSeparator();

    // The user's presets and instruments, by folder (sub-folders become sub-menus).
    auto browse = [this] (const juce::File& folder, const juce::String& extension, bool instrument) {
        juce::PopupMenu list;
        std::map<juce::String, juce::PopupMenu> subFolders;
        const auto current = ospProcessor.currentPresetFile();
        for (const auto& file : OspAudioProcessor::findFiles (folder, extension))
        {
            const auto parent = file.getParentDirectory();
            auto& target = parent == folder ? list : subFolders[parent.getRelativePathFrom (folder)];
            target.addItem (file.getFileNameWithoutExtension(), true, ! instrument && file == current, [this, file, instrument] {
                juce::String error;
                const bool ok = instrument ? ospProcessor.importInstrument (file, error) : ospProcessor.loadPreset (file);
                ospProcessor.showMessage (ok ? "Opened " + file.getFileNameWithoutExtension() : (error.isEmpty() ? "Could not open " + file.getFileName() : error));
                refreshInstrumentInfo();
            });
        }
        for (auto& [name, sub] : subFolders)
            list.addSubMenu (name, sub);
        if (list.getNumItems() == 0)
            list.addItem (instrument ? "No instruments yet" : "No presets yet", false, false, [] {});
        list.addSeparator();
        list.addItem ("Show folder", [folder] {
            folder.createDirectory();
            folder.revealToUser();
        });
        return list;
    };

    menu.addSubMenu ("Presets", browse (OspAudioProcessor::presetFolder(), OspAudioProcessor::presetExtension, false));
    menu.addItem ("Previous preset", [this] { if (ospProcessor.stepPreset (-1)) presetOpened(); });
    menu.addItem ("Next preset", [this] { if (ospProcessor.stepPreset (1)) presetOpened(); });
    menu.addItem ("Save preset...", [this] { choosePresetFile (true, false); });
    menu.addItem ("Open preset file...", [this] { choosePresetFile (false, false); });
    menu.addSeparator();
    menu.addSubMenu ("Instruments", browse (OspAudioProcessor::instrumentFolder(), OspAudioProcessor::instrumentExtension, true));
    menu.addItem ("Export instrument...", ospProcessor.currentInstrument() != nullptr, false, [this] { choosePresetFile (true, true); });
    menu.addItem ("Open instrument file...", [this] { choosePresetFile (false, true); });
    menu.addSeparator();
    menu.addItem ("Undo " + ospProcessor.undoManager.getUndoDescription(), ospProcessor.undoManager.canUndo(), false,
                  [this] { ospProcessor.undoManager.undo(); refreshInstrumentInfo(); });
    menu.addItem ("Redo " + ospProcessor.undoManager.getRedoDescription(), ospProcessor.undoManager.canRedo(), false,
                  [this] { ospProcessor.undoManager.redo(); refreshInstrumentInfo(); });
    juce::PopupMenu size;
    for (int percent : { 80, 100, 125, 150, 200 })
        size.addItem (juce::String (percent) + " %", true, std::abs (ospProcessor.uiScale() * 100.0f - static_cast<float> (percent)) < 1.0f,
                      [this, percent] {
                          ospProcessor.setUiScale (static_cast<float> (percent) / 100.0f);
                          setScaleFactor (ospProcessor.uiScale());
                      });
    menu.addSubMenu ("Interface size", size);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (menuButton));
}

void OspAudioProcessorEditor::choosePresetFile (bool save, bool instrument)
{
    const juce::String pattern = juce::String ("*") + (instrument ? OspAudioProcessor::instrumentExtension : OspAudioProcessor::presetExtension);
    auto folder = instrument ? OspAudioProcessor::instrumentFolder() : OspAudioProcessor::presetFolder();
    folder.createDirectory();
    chooser = std::make_unique<juce::FileChooser> (save ? (instrument ? "Export instrument" : "Save preset") : (instrument ? "Import instrument" : "Load preset"),
                                                   folder, pattern);
    const auto browserFlags = (save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                                    : juce::FileBrowserComponent::openMode)
                              | juce::FileBrowserComponent::canSelectFiles;
    chooser->launchAsync (browserFlags, [this, save, instrument] (const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file == juce::File())
            return;
        const auto extension = instrument ? OspAudioProcessor::instrumentExtension : OspAudioProcessor::presetExtension;
        if (save && file.getFileExtension() != extension)
            file = file.withFileExtension (extension);
        juce::String error;
        bool ok = false;
        if (instrument)
            ok = save ? ospProcessor.exportInstrument (file, error) : ospProcessor.importInstrument (file, error);
        else
            ok = save ? ospProcessor.savePreset (file) : ospProcessor.loadPreset (file);
        ospProcessor.showMessage (ok ? (save ? "Saved " : "Opened ") + file.getFileName() : (error.isEmpty() ? "Could not open " + file.getFileName() : error));
        refreshInstrumentInfo();
    });
}

void OspAudioProcessorEditor::presetOpened()
{
    ospProcessor.showMessage ("Preset: " + ospProcessor.currentPresetFile().getFileNameWithoutExtension());
    refreshInstrumentInfo();
}

void OspAudioProcessorEditor::chooseFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a sound", juce::File(), "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.flac");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc) {
                              juce::Array<juce::File> files;
                              for (const auto& f : fc.getResults())
                                  if (f.existsAsFile())
                                      files.add (f);
                              if (! files.isEmpty())
                                  ospProcessor.loadFiles (files);
                          });
}

void OspAudioProcessorEditor::refreshInstrumentInfo()
{
    const auto instrument = ospProcessor.currentInstrument();
    waveform.setInstrument (instrument);
    samplesPanel.setInstrument (instrument);
    const bool isSet = instrument != nullptr && instrument->set != nullptr;
    if (! isSet && samplesShown)
    {
        samplesShown = false;
        samplesPanel.setVisible (false);
    }
    for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
        layerTabs.setLoaded (layer, ospProcessor.currentInstrument (layer) != nullptr);

    const auto overrideMidi = ospProcessor.rootOverride();

    if (instrument == nullptr)
    {
        rootLabel.setText ("", juce::dontSendNotification);
        characterLabel.setText ("", juce::dontSendNotification);
        detailLabel.setText ("", juce::dontSendNotification);
        return;
    }

    const auto& pitch = instrument->analysis.pitch;
    juce::String root;
    if (overrideMidi)
        root = midiNoteName (static_cast<int> (std::lround (*overrideMidi)));
    else if (pitch.detected)
        root = pitch.noteName;
    else
        root = "?";
    rootLabel.setText (root, juce::dontSendNotification);
    characterLabel.setText (juce::String::fromUTF8 (instrument->character.c_str()).toUpperCase(), juce::dontSendNotification);

    juce::String detail = juce::String (instrument->filename) + dot + juce::String (instrument->durationSeconds, 2) + " s";
    if (pitch.midiNote >= 0)
        detail << dot << "detected " << pitch.noteName << " " << (pitch.centsOffset >= 0 ? "+" : "")
               << juce::String (static_cast<int> (std::lround (pitch.centsOffset))) << "c (" << pitch.confidenceLevel << ")";
    else
        detail << dot << "no pitch detected - choose a root";
    detailLabel.setText (detail, juce::dontSendNotification);
}

void OspAudioProcessorEditor::timerCallback()
{
    const auto state = ospProcessor.loadState();
    waveform.setLoading (state == OspAudioProcessor::LoadState::loading);
    if (stateBox.getSelectedId() != ospProcessor.getCurrentProgram() + 1)
        stateBox.setSelectedId (ospProcessor.getCurrentProgram() + 1, juce::dontSendNotification);

    if (ospProcessor.editLayer() != shownLayer)
        showLayer (ospProcessor.editLayer());   // e.g. a recalled session
    updateGranularView();
    {
        // The grains the edited layer is playing now.
        const auto& snapshot = ospProcessor.grainSnapshot (shownLayer);
        const int count = std::clamp (snapshot.count.load (std::memory_order_acquire), 0, InstrumentEngine::GrainSnapshot::capacity);
        std::array<WaveformView::GrainDot, InstrumentEngine::GrainSnapshot::capacity> dots;
        for (int i = 0; i < count; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            dots[k] = { snapshot.position[k].load (std::memory_order_relaxed), snapshot.level[k].load (std::memory_order_relaxed),
                        snapshot.lane[k].load (std::memory_order_relaxed) };
        }
        waveform.setGrains (dots.data(), count);
        const int heads = std::clamp (snapshot.playheads.load (std::memory_order_acquire), 0, InstrumentEngine::GrainSnapshot::playheadCapacity);
        for (int i = 0; i < heads; ++i)
        {
            const auto k = static_cast<std::size_t> (i);
            dots[k] = { snapshot.playheadPosition[k].load (std::memory_order_relaxed), snapshot.playheadLevel[k].load (std::memory_order_relaxed), 0.5f };
        }
        waveform.setPlayheads (dots.data(), heads);
    }
    const auto instrument = ospProcessor.currentInstrument();
    const auto generation = instrument != nullptr ? instrument->generation : 0;
    if (generation != shownGeneration || state != shownState)
    {
        shownGeneration = generation;
        shownState = state;
        refreshInstrumentInfo();
        repaint(); // status light
    }
    updateCustomisedDots();

    juce::String status;
    switch (state)
    {
        case OspAudioProcessor::LoadState::empty: status = "No sound loaded"; break;
        case OspAudioProcessor::LoadState::loading: status = juce::String::fromUTF8 ("Understanding pitch\xe2\x80\xa6"); break;
        case OspAudioProcessor::LoadState::ready: status = ospProcessor.stageMessage(); break;
        case OspAudioProcessor::LoadState::failed: status = "Could not load that file"; break;
    }
    status << dot << "voices " << ospProcessor.activeVoices.load();
    const auto preset = ospProcessor.currentPresetFile();
    if (preset != juce::File())
        status << dot << "preset " << preset.getFileNameWithoutExtension();
    const auto message = ospProcessor.statusMessage();
    if (message.isNotEmpty())
        status << dot << message;
    if (statusLabel.getText() != status)
        statusLabel.setText (status, juce::dontSendNotification);
}

} // namespace osp::plugin
