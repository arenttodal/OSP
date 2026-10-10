#pragma once

#include "Design.h"
#include "LibraryPanel.h"
#include "OspLookAndFeel.h"

namespace osp::plugin
{

/** A card over the dimmed Library window (save, settings, missing sounds, scan, trim, a name,
    a choice). Shared by LibraryPanel.cpp and LibraryCapture.cpp. */
class LibraryPanel::Sheet : public juce::Component
{
public:
    Sheet (LibraryPanel& p, juce::String t, juce::Point<int> size) : panel (p), title (std::move (t)), cardSize (size)
    {
        setTitle (title);
        setWantsKeyboardFocus (true);
    }
    juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (cardSize.x, cardSize.y); }
    juce::Rectangle<int> content() const { return card().reduced (24).withTrimmedTop (40); }
    void paint (juce::Graphics& g) override
    {
        g.setColour (design::colour::housingBottom.withAlpha (0.72f));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 14.0f);
        design::draw::raised (g, card().toFloat(), 14.0f, design::colour::cardTop, design::colour::cardBottom, 1.4f);
        g.setColour (design::colour::text);
        g.setFont (type::panelHeader());
        g.drawText (title, card().reduced (24, 18).withHeight (24), juce::Justification::centredLeft);
        paintContent (g);
    }
    virtual void paintContent (juce::Graphics&) {}
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! card().contains (e.getPosition()))
            panel.closeSheet();   // a click beside the card closes it (nothing is changed)
    }
    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key == juce::KeyPress::escapeKey)
        {
            panel.closeSheet();
            return true;
        }
        return false;
    }

protected:
    LibraryPanel& panel;
    juce::String title;
    juce::Point<int> cardSize;
};


/**
    The inspector for candidates (Stages 6-8): a scanned file or an Inbox item that is not a
    Library sound yet. Its waveform and preview, what the analysis thinks it is (and the
    user's correction), its details, and what can be done with it: import it (with its
    evidence), import and load it into a layer, trim or slice it, or set it aside (never
    deleted). With several rows selected, the same actions for all of them.
*/
class LibraryPanel::CandidateInspector final : public juce::Component
{
public:
    explicit CandidateInspector (LibraryPanel& panel);
    void update();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    LibraryPanel& panel;
    juce::TextButton play { juce::String::fromUTF8 ("\xe2\x96\xb6") };
    juce::ComboBox kind;
    std::unique_ptr<juce::Button> importButton;
    std::array<juce::TextButton, 4> loadButtons;
    juce::TextButton trim { juce::String::fromUTF8 ("Trim / Slice\xe2\x80\xa6") }, aside { "Set aside" };
    juce::Rectangle<int> wave;
    int detailsTop = 0;
};

} // namespace osp::plugin
