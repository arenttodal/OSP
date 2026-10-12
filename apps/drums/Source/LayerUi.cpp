#include "LayerUi.h"

#include "CeramicSkin.h"
#include "DrumsLook.h"

namespace osp::drumsplugin
{

namespace
{
    namespace cc = ceramic::colour;
    namespace type = look::type;

    juce::String velocityText (const drums::LayerParams& p) { return juce::String (p.velocityLow) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (p.velocityHigh); }
} // namespace

LayerDrop defaultLayerDrop (int fileCount) noexcept { return fileCount > 1 ? LayerDrop::roundRobin : LayerDrop::replace; }

bool isAudioFileName (const juce::String& path) { return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;caf"); }

juce::PopupMenu duplicateMenu (DrumsProcessor& proc, int pad, std::function<void (int)> onDone)
{
    juce::PopupMenu m;
    const bool has = ! proc.padLayers (pad).layers.empty();
    const int empty = proc.firstEmptyPad();
    auto copyTo = [&proc, pad, onDone] (int to) {
        if (proc.duplicatePad (pad, to) && onDone)
            onDone (to);
    };
    m.addItem (empty >= 0 ? "Next empty pad (" + juce::String (empty + 1) + ")" : juce::String ("No empty pad"), has && empty >= 0, false,
               [copyTo, empty] { copyTo (empty); });
    m.addSeparator();
    for (int slot = 0; slot < DrumsProcessor::numPads; ++slot)
    {
        const int other = proc.padAtSlot (slot);
        if (other == pad || other == empty)
            continue;
        const bool occupied = ! proc.padLayers (other).layers.empty();
        m.addItem ("Pad " + juce::String (other + 1) + "  " + proc.padDisplayName (other) + (occupied ? "  (replaces it)" : ""), has, false,
                   [copyTo, other] { copyTo (other); });
    }
    return m;
}

void offerLayerDrop (DrumsProcessor& proc, int pad, const juce::Array<juce::File>& dropped, juce::Component* target, std::optional<LayerDrop> preferred)
{
    juce::Array<juce::File> files;
    for (const auto& f : dropped)
        if (isAudioFileName (f.getFullPathName()) && files.size() < drums::maxLayers)
            files.add (f);
    if (files.isEmpty())
        return;
    const bool populated = ! proc.padLayers (pad).layers.empty();
    const auto fallback = preferred.value_or (defaultLayerDrop (files.size()));
    if ((! populated && files.size() == 1) || target == nullptr || target->getPeer() == nullptr)
    {
        proc.addPadLayers (pad, files, fallback);
        return;
    }
    juce::PopupMenu m;
    const auto what = files.size() == 1 ? juce::String ("1 file") : juce::String (files.size()) + " files";
    m.addSectionHeader (what + " onto " + proc.padDisplayName (pad).toUpperCase());
    auto add = [&] (const juce::String& text, LayerDrop how) {
        m.addItem (text, true, how == fallback, [&proc, pad, files, how] { proc.addPadLayers (pad, files, how); });
    };
    const auto verb = populated ? juce::String ("Add as ") : juce::String ("Load as ");
    add (verb + "round robin (a different one each hit)", LayerDrop::roundRobin);
    add (verb + "velocity layers (soft to hard)", LayerDrop::velocity);
    add (verb + "a stack (all together)", LayerDrop::stack);
    if (populated)
        add (files.size() == 1 ? "Replace the pad's sample" : "Replace the pad's samples", LayerDrop::replace);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target));
}

// ---------------------------------------------------------------------------------------
// Layer list
// ---------------------------------------------------------------------------------------

LayerList::LayerList (DrumsProcessor& p) : proc (p)
{
    startTimerHz (15);
}

LayerList::~LayerList() { stopTimer(); }

int LayerList::pad() const noexcept { return proc.selectedPad; }
int LayerList::layerCount() const { return static_cast<int> (proc.padLayers (pad()).layers.size()); }
int LayerList::visibleRows() const { return std::max (1, static_cast<int> ((static_cast<float> (getHeight()) - 8.0f) / rowHeight)); }

juce::Rectangle<float> LayerList::rowRect (int row) const
{
    return { 4.0f, 4.0f + rowHeight * static_cast<float> (row - scroll), static_cast<float> (getWidth()) - 8.0f, rowHeight - 2.0f };
}

int LayerList::rowAt (float y) const
{
    const int row = scroll + static_cast<int> (std::floor ((y - 4.0f) / rowHeight));
    return row >= 0 && row < layerCount() && y >= 4.0f ? row : -1;
}

void LayerList::refresh()
{
    const int n = layerCount();
    const int selected = proc.selectedLayer[static_cast<std::size_t> (pad())];
    scroll = juce::jlimit (0, std::max (0, n - visibleRows()), scroll);
    if (selected < scroll)
        scroll = selected;
    else if (selected >= scroll + visibleRows())
        scroll = selected - visibleRows() + 1;
    repaint();
}

void LayerList::select (int layer)
{
    proc.selectedLayer[static_cast<std::size_t> (pad())] = layer;
    refresh();
    if (onSelectionChanged)
        onSelectionChanged();
}

void LayerList::timerCallback()
{
    const int last = proc.lastLayerPlayed (pad());
    if (last != shownLastPlayed)
    {
        shownLastPlayed = last;
        repaint();
    }
}

void LayerList::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    ceramic::recess (g, bounds, 6.0f, fileOver ? cc::keyOn : juce::Colour (0xfff7f2ea));
    const auto& layers = proc.padLayers (pad());
    const int n = static_cast<int> (layers.layers.size());
    if (n == 0)
    {
        g.setColour (cc::textMicro);
        g.setFont (type::micro (13.0f));
        g.drawFittedText (proc.padInfo (pad()).loading ? "Loading..." : "No sample. Drop files here or on the pad.",
                          getLocalBounds().reduced (12, 0), juce::Justification::centredLeft, 2, 1.0f);
        return;
    }
    const auto accent = look::category (proc.padInfo (pad()).category);
    const int selected = proc.selectedLayer[static_cast<std::size_t> (pad())];
    const int lastPlayed = proc.lastLayerPlayed (pad());
    g.saveState();
    g.reduceClipRegion (getLocalBounds().reduced (2));
    for (int row = scroll; row < std::min (n, scroll + visibleRows() + 1); ++row)
    {
        const auto& layer = layers.layers[static_cast<std::size_t> (row)];
        auto r = rowRect (row);
        if (row == selected)
        {
            g.setColour (cc::keyOn.withAlpha (0.85f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (cc::accent);
            g.fillRect (r.withWidth (2.5f));
        }
        else if (row == hover)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.fillRoundedRectangle (r, 4.0f);
        }
        // Play (hold to audition)
        const auto play = r.removeFromLeft (24.0f).reduced (5.0f, 6.0f);
        ceramic::iconPlay (g, play, row == auditioning ? cc::accent : cc::textSecondary);
        // Number, with a lit lens when the last hit played this layer.
        const auto num = r.removeFromLeft (30.0f);
        if (row == lastPlayed)
            ceramic::lens (g, { num.getX() + 4.0f, num.getCentreY() }, 7.0f, accent, true);
        g.setColour (cc::textMicro);
        g.setFont (plugin::fonts::make (12.0f, look::Weight::semibold));
        g.drawText (juce::String (row + 1), num.withTrimmedLeft (11.0f), juce::Justification::centredLeft);
        // Right: velocity range (velocity mode), else the level when not 0 dB.
        juce::String right;
        if (layer.sound == nullptr)
            right = "missing";
        else if (layers.mode == drums::LayerMode::velocity || layer.params.velocityLow > 1 || layer.params.velocityHigh < 127)
            right = velocityText (layer.params);
        else if (std::abs (layer.params.gainDb) > 0.05)
            right = juce::String (layer.params.gainDb, 1) + " dB";
        g.setFont (type::micro (12.0f));
        g.setColour (layer.sound == nullptr ? juce::Colour (0xffb5533c) : cc::textSecondary);
        const auto rightBox = r.removeFromRight (right.isEmpty() ? 4.0f : 64.0f);
        g.drawText (right, rightBox.withTrimmedRight (6.0f), juce::Justification::centredRight);
        g.setColour (layer.sound == nullptr ? cc::textMicro : cc::text);
        g.setFont (type::micro (13.0f));
        g.drawFittedText (juce::String::fromUTF8 (layer.displayName().c_str()), r.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
    }
    // Reorder: where the dragged row would land.
    if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom)
    {
        const auto r = rowRect (dragTo);
        g.setColour (cc::accent);
        g.fillRect (juce::Rectangle<float> (r.getX(), dragTo > dragFrom ? r.getBottom() : r.getY() - 2.0f, r.getWidth(), 2.0f));
    }
    g.restoreState();
    // Scroll hint
    if (n > visibleRows())
    {
        const float track = bounds.getHeight() - 12.0f;
        const float h = track * static_cast<float> (visibleRows()) / static_cast<float> (n);
        const float y = 6.0f + (track - h) * static_cast<float> (scroll) / static_cast<float> (std::max (1, n - visibleRows()));
        g.setColour (cc::textMicro.withAlpha (0.5f));
        g.fillRoundedRectangle (juce::Rectangle<float> (bounds.getRight() - 5.0f, y, 3.0f, h), 1.5f);
    }
}

void LayerList::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position.y);
    if (row != hover)
    {
        hover = row;
        repaint();
    }
}

void LayerList::mouseExit (const juce::MouseEvent&)
{
    hover = -1;
    repaint();
}

void LayerList::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position.y);
    dragFrom = dragTo = -1;
    if (row < 0)
        return;
    if (e.mods.isPopupMenu())
    {
        select (row);
        showMenu (row);
        return;
    }
    if (e.position.x < rowRect (row).getX() + 24.0f)
    {
        auditioning = row;
        proc.auditionLayer (pad(), row, 100);
        repaint();
        return;
    }
    select (row);
    dragFrom = row;
}

void LayerList::mouseDrag (const juce::MouseEvent& e)
{
    if (dragFrom < 0 || e.getDistanceFromDragStart() < 4)
        return;
    dragTo = juce::jlimit (0, layerCount() - 1, scroll + static_cast<int> (std::floor ((e.position.y - 4.0f) / rowHeight)));
    repaint();
}

void LayerList::mouseUp (const juce::MouseEvent&)
{
    if (auditioning >= 0)
    {
        proc.releaseAuditionedPad (pad());
        auditioning = -1;
    }
    if (dragFrom >= 0 && dragTo >= 0 && dragTo != dragFrom)
    {
        proc.moveLayer (pad(), dragFrom, dragTo);
        if (onSelectionChanged)
            onSelectionChanged();
    }
    dragFrom = dragTo = -1;
    refresh();
}

void LayerList::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int row = rowAt (e.position.y);
    if (row >= 0 && e.position.x >= rowRect (row).getX() + 24.0f)
        startRename (row);
}

void LayerList::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    const int n = layerCount();
    scroll = juce::jlimit (0, std::max (0, n - visibleRows()), scroll - (wheel.deltaY > 0.0f ? 1 : wheel.deltaY < 0.0f ? -1 : 0));
    repaint();
}

void LayerList::startRename (int layer)
{
    if (layer < 0 || layer >= layerCount())
        return;
    renameEditor = std::make_unique<juce::TextEditor>();
    auto& ed = *renameEditor;
    ed.setFont (type::micro (13.0f));
    ed.setText (juce::String::fromUTF8 (proc.padLayers (pad()).layers[static_cast<std::size_t> (layer)].displayName().c_str()), false);
    ed.selectAll();
    ed.setBounds (rowRect (layer).withTrimmedLeft (54.0f).toNearestInt());
    const int p = pad();
    auto finish = [this, p, layer] (bool keep) {
        if (renameEditor == nullptr)
            return;
        const auto text = renameEditor->getText();
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LayerList> (this)] {
            if (safe != nullptr)
                safe->renameEditor.reset();
        });
        if (keep)
            proc.renameLayer (p, layer, text);
    };
    ed.onReturnKey = [finish] { finish (true); };
    ed.onFocusLost = [finish] { finish (true); };
    ed.onEscapeKey = [finish] { finish (false); };
    addAndMakeVisible (ed);
    ed.grabKeyboardFocus();
}

void LayerList::showMenu (int layer)
{
    const int p = pad();
    const int n = layerCount();
    juce::PopupMenu m;
    const bool missing = proc.padLayers (p).layers[static_cast<std::size_t> (layer)].sound == nullptr;
    m.addItem (missing ? "Relink..." : "Replace sample...", [this, p, layer, missing] {
        chooser = std::make_unique<juce::FileChooser> (missing ? "Find the missing sample" : "Replace this layer's sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.caf");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, p, layer] (const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile())
                proc.relinkLayer (p, layer, fc.getResult());
        });
    });
    m.addItem ("Rename", [this, layer] { startRename (layer); });
    m.addItem ("Duplicate", n < drums::maxLayers, false, [this, p, layer] {
        proc.duplicateLayer (p, layer);
        refresh();
    });
    m.addItem ("Move up", layer > 0, false, [this, p, layer] {
        proc.moveLayer (p, layer, layer - 1);
        refresh();
    });
    m.addItem ("Move down", layer < n - 1, false, [this, p, layer] {
        proc.moveLayer (p, layer, layer + 1);
        refresh();
    });
    m.addSeparator();
    m.addItem ("Remove", [this, p, layer] {
        proc.removeLayer (p, layer);
        refresh();
    });
    if (getPeer() != nullptr)
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (rowRect (layer).toNearestInt())));
}

bool LayerList::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (isAudioFileName (f))
            return true;
    return false;
}

void LayerList::fileDragEnter (const juce::StringArray&, int, int)
{
    fileOver = true;
    repaint();
}

void LayerList::fileDragExit (const juce::StringArray&)
{
    fileOver = false;
    repaint();
}

void LayerList::filesDropped (const juce::StringArray& files, int, int)
{
    fileOver = false;
    juce::Array<juce::File> list;
    for (const auto& f : files)
        list.add (juce::File (f));
    offerLayerDrop (proc, pad(), list, this);
    repaint();
}

// ---------------------------------------------------------------------------------------
// Velocity range
// ---------------------------------------------------------------------------------------

void VelocityRange::setRange (int low, int high, juce::Colour colour)
{
    lo = juce::jlimit (1, 127, low);
    hi = juce::jlimit (lo, 127, high);
    fill = colour;
    repaint();
}

juce::Rectangle<float> VelocityRange::bar() const { return getLocalBounds().toFloat().reduced (8.0f, 0.0f).withSizeKeepingCentre (static_cast<float> (getWidth()) - 16.0f, 12.0f); }
float VelocityRange::xOf (int velocity) const { return bar().getX() + bar().getWidth() * static_cast<float> (velocity - 1) / 126.0f; }
int VelocityRange::velocityAt (float x) const { return juce::jlimit (1, 127, 1 + juce::roundToInt ((x - bar().getX()) / bar().getWidth() * 126.0f)); }

void VelocityRange::paint (juce::Graphics& g)
{
    const auto b = bar();
    ceramic::recess (g, b, 6.0f);
    const auto r = juce::Rectangle<float> (xOf (lo), b.getY() + 2.0f, std::max (2.0f, xOf (hi) - xOf (lo)), b.getHeight() - 4.0f);
    g.setColour (fill.withAlpha (0.85f));
    g.fillRoundedRectangle (r, 4.0f);
    for (float x : { r.getX(), r.getRight() })
    {
        const auto handle = juce::Rectangle<float> (10.0f, 18.0f).withCentre ({ x, b.getCentreY() });
        ceramic::contactShadow (g, handle, 3.0f, 0.6f);
        g.setColour (cc::cardTop);
        g.fillRoundedRectangle (handle, 3.0f);
        g.setColour (fill.darker (0.3f));
        g.drawRoundedRectangle (handle, 3.0f, 1.0f);
    }
}

void VelocityRange::mouseDown (const juce::MouseEvent& e)
{
    const float x = e.position.x;
    const float dl = std::abs (x - xOf (lo)), dh = std::abs (x - xOf (hi));
    if (dl < 9.0f || dh < 9.0f)
        grab = dl <= dh ? Grab::low : Grab::high;
    else if (x > xOf (lo) && x < xOf (hi))
    {
        grab = Grab::both;
        grabOffset = velocityAt (x) - lo;
    }
    else
        grab = x < xOf (lo) ? Grab::low : Grab::high;
    mouseDrag (e);
    if (onChange)
        onChange (lo, hi, true);
}

void VelocityRange::mouseDrag (const juce::MouseEvent& e)
{
    const int v = velocityAt (e.position.x);
    int l = lo, h = hi;
    if (grab == Grab::low)
        l = std::min (v, hi);
    else if (grab == Grab::high)
        h = std::max (v, lo);
    else if (grab == Grab::both)
    {
        const int width = hi - lo;
        l = juce::jlimit (1, 127 - width, v - grabOffset);
        h = l + width;
    }
    if (l == lo && h == hi)
        return;
    lo = l;
    hi = h;
    repaint();
    if (onChange && e.getNumberOfClicks() >= 0 && e.mouseWasDraggedSinceMouseDown())
        onChange (lo, hi, false);
}

} // namespace osp::drumsplugin
