#include "RootNoteSelector.h"

#include "Design.h"
#include "OspLookAndFeel.h"

namespace osp::plugin
{

namespace
{
    namespace colour = design::colour;

    constexpr int pickerWidth = 236, pad = 10, rowHeight = 26, keysHeight = 46, gap = 8;
    constexpr int pickerHeight = pad + rowHeight + gap + keysHeight + gap + rowHeight + pad;
    constexpr bool isBlack (int pc) noexcept { return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }
    /** A white key's index (0-6) for C D E F G A B, or the white key a black one follows. */
    constexpr int whiteIndex (int pc) noexcept
    {
        constexpr int map[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
        return map[pc];
    }
}

//==============================================================================
class RootNoteSelector::Picker final : public juce::Component
{
public:
    explicit Picker (RootNoteSelector& o) : owner (o)
    {
        setTitle ("Root note picker");
        setWantsKeyboardFocus (true);
        setSize (pickerWidth, pickerHeight);
        juce::Desktop::getInstance().addGlobalMouseListener (this);
    }
    ~Picker() override { juce::Desktop::getInstance().removeGlobalMouseListener (this); }

    juce::Rectangle<int> detectedRow() const { return { pad, pad, pickerWidth - 2 * pad, rowHeight }; }
    juce::Rectangle<int> keyboard() const { return { pad, detectedRow().getBottom() + gap, pickerWidth - 2 * pad, keysHeight }; }
    juce::Rectangle<int> octaveRow() const { return { pad, keyboard().getBottom() + gap, pickerWidth - 2 * pad, rowHeight }; }
    juce::Rectangle<int> octaveDown() const { return octaveRow().withX (octaveRow().getX() + 58).withWidth (24); }
    juce::Rectangle<int> octaveUp() const { return octaveDown().translated (24 + 30, 0); }

    juce::Rectangle<float> keyBounds (int pc) const
    {
        const auto k = keyboard().toFloat();
        const float white = k.getWidth() / 7.0f;
        if (! isBlack (pc))
            return { k.getX() + white * static_cast<float> (whiteIndex (pc)), k.getY(), white, k.getHeight() };
        const float w = white * 0.62f;
        const float x = k.getX() + white * static_cast<float> (whiteIndex (pc) + 1) - w * 0.5f;
        return { x, k.getY(), w, k.getHeight() * 0.58f };
    }
    int keyAt (juce::Point<float> p) const
    {
        for (int pc = 0; pc < 12; ++pc)   // (black keys first: they sit over the white ones)
            if (isBlack (pc) && keyBounds (pc).contains (p))
                return pc;
        for (int pc = 0; pc < 12; ++pc)
            if (! isBlack (pc) && keyBounds (pc).contains (p))
                return pc;
        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        juce::Path shape;
        shape.addRoundedRectangle (r, 9.0f);
        design::CachedShadow (juce::Colour (0x26281c10), 12, { 0, 4 }).drawForPath (g, shape);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff8f4ec), 0.0f, r.getY(), juce::Colour (0xffeee7db), 0.0f, r.getBottom(), false));
        g.fillPath (shape);
        g.setColour (juce::Colour (0xffd3cabc));
        g.strokePath (shape, juce::PathStrokeType (1.0f));

        // Detected: a full-width choice, its note when the analysis knows it.
        const bool detectedOn = ! owner.given.has_value();
        const auto dr = detectedRow().toFloat();
        g.setColour (detectedOn ? colour::accent.withAlpha (0.16f) : juce::Colours::white.withAlpha (hover == hoverDetected ? 0.7f : 0.45f));
        g.fillRoundedRectangle (dr, 5.0f);
        g.setColour (detectedOn ? colour::accent.withAlpha (0.8f) : colour::hairline.darker (0.05f));
        g.drawRoundedRectangle (dr.reduced (0.5f), 5.0f, 1.0f);
        g.setColour (colour::text.withAlpha (0.9f));
        g.setFont (fonts::make (11.0f, fonts::Weight::medium, 0.06f));
        g.drawText ("DETECTED", dr.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (12.0f, fonts::Weight::regular));
        g.drawText (owner.detectedRoot ? nameOf (*owner.detectedRoot) : juce::String::fromUTF8 ("\xe2\x80\x94"), dr.reduced (10.0f, 0.0f), juce::Justification::centredRight, false);

        // The 12 pitch classes as an octave of keys: white first, black over them.
        const int current = owner.given ? *owner.given : -1;
        for (const bool black : { false, true })
            for (int pc = 0; pc < 12; ++pc)
            {
                if (isBlack (pc) != black)
                    continue;
                const auto kb = keyBounds (pc).reduced (black ? 0.0f : 1.0f, 0.0f);
                const bool enabled = owner.keyEnabled (pc);
                const bool chosen = current >= 0 && current == midiFor (pc, owner.shownOctave);
                const bool over = hover == pc && enabled;
                juce::Colour face = black ? juce::Colour (0xff3a3d3c) : juce::Colour (0xfffbf8f2);
                if (chosen)
                    face = colour::accent;
                else if (over)
                    face = black ? juce::Colour (0xff545957) : juce::Colours::white;
                if (! enabled)
                    face = black ? juce::Colour (0xffa8a39b) : juce::Colour (0xffe6e0d6);
                g.setColour (face);
                g.fillRoundedRectangle (kb, black ? 2.5f : 3.5f);
                g.setColour (black ? juce::Colour (0xff222524) : colour::hairline.darker (0.12f));
                g.drawRoundedRectangle (kb.reduced (0.5f), black ? 2.5f : 3.5f, 1.0f);
                if (! black)
                {
                    g.setColour (chosen ? juce::Colours::white : colour::textSecondary.withAlpha (enabled ? 0.9f : 0.4f));
                    g.setFont (fonts::make (10.0f, chosen ? fonts::Weight::semibold : fonts::Weight::regular));
                    g.drawText (nameOf (midiFor (pc, 4)).dropLastCharacters (1), kb.withTrimmedTop (kb.getHeight() * 0.6f), juce::Justification::centred, false);
                }
            }

        // The octave stepper and the root it gives.
        const auto orow = octaveRow().toFloat();
        g.setColour (colour::textMicro);
        g.setFont (fonts::make (10.5f, fonts::Weight::medium, 0.08f));
        g.drawText ("OCTAVE", orow.withWidth (56.0f), juce::Justification::centredLeft, false);
        auto arrow = [&] (juce::Rectangle<int> box, bool up, bool enabled, bool over) {
            const auto b = box.toFloat().reduced (1.0f, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (! enabled ? 0.2f : over ? 0.85f : 0.55f));
            g.fillRoundedRectangle (b, 4.0f);
            g.setColour (colour::hairline.darker (enabled ? 0.1f : 0.0f));
            g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
            const auto c = b.getCentre();
            juce::Path p;
            p.startNewSubPath (c.x + (up ? -2.0f : 2.0f), c.y - 4.0f);
            p.lineTo (c.x + (up ? 2.0f : -2.0f), c.y);
            p.lineTo (c.x + (up ? -2.0f : 2.0f), c.y + 4.0f);
            g.setColour (colour::text.withAlpha (enabled ? 0.8f : 0.25f));
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        arrow (octaveDown(), false, owner.canStepOctave (-1), hover == hoverDown);
        arrow (octaveUp(), true, owner.canStepOctave (1), hover == hoverUp);
        g.setColour (colour::text);
        g.setFont (fonts::make (13.0f, fonts::Weight::semibold));
        g.drawText (juce::String (owner.shownOctave), juce::Rectangle<int> (octaveDown().getRight(), octaveRow().getY(), octaveUp().getX() - octaveDown().getRight(), rowHeight),
                    juce::Justification::centred, false);
        g.setColour (colour::textMicro);
        g.setFont (fonts::make (10.5f, fonts::Weight::medium, 0.08f));
        const auto rootArea = orow.withTrimmedLeft (static_cast<float> (octaveUp().getRight() - octaveRow().getX()) + 8.0f);
        g.drawText ("ROOT", rootArea, juce::Justification::centredLeft, false);
        g.setColour (colour::text);
        g.setFont (fonts::make (13.0f, fonts::Weight::semibold));
        g.drawText (owner.fieldText(), rootArea, juce::Justification::centredRight, false);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
            return;
        const auto p = e.position;
        int now = keyAt (p);
        if (now < 0)
            now = detectedRow().toFloat().contains (p) ? hoverDetected : octaveDown().toFloat().contains (p) ? hoverDown : octaveUp().toFloat().contains (p) ? hoverUp : -1;
        if (now != hover)
        {
            hover = now;
            repaint();
        }
    }
    void mouseExit (const juce::MouseEvent& e) override
    {
        if (e.eventComponent == this)
        {
            hover = -1;
            repaint();
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.eventComponent != this)
            return;
        const auto p = e.position;
        if (const int pc = keyAt (p); pc >= 0)
            owner.choosePitchClass (pc);
        else if (detectedRow().toFloat().contains (p))
            owner.chooseDetected();
        else if (octaveDown().toFloat().contains (p))
            owner.stepOctave (-1);
        else if (octaveUp().toFloat().contains (p))
            owner.stepOctave (1);
    }
    /** (also the desktop's: the picker listens globally) A press anywhere else closes the
        picker; its field's own press toggles it. */
    void mouseDown (const juce::MouseEvent& e) override
    {
        auto* c = e.eventComponent;
        if (c == nullptr || c == this || isParentOf (c) || c == &owner || owner.isParentOf (c))
            return;
        juce::Component::SafePointer<RootNoteSelector> safe (&owner);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->showPicker (false); });
    }
    bool keyPressed (const juce::KeyPress& key) override { return owner.keyPressed (key); }

private:
    static constexpr int hoverDetected = 100, hoverDown = 101, hoverUp = 102;
    RootNoteSelector& owner;
    int hover = -1;
};

//==============================================================================
RootNoteSelector::RootNoteSelector()
{
    setTitle ("Root note");
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

RootNoteSelector::~RootNoteSelector() = default;

juce::Component* RootNoteSelector::pickerComponent() const noexcept { return picker.get(); }

juce::String RootNoteSelector::nameOf (int midi)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    // (as the Library's rows: C4 = 60; floor division keeps octave -1 for MIDI 0-11)
    const int octave = midi >= 0 ? midi / 12 - 1 : (midi - 11) / 12 - 1;
    return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (octave);
}

void RootNoteSelector::setRoot (std::optional<int> root, std::optional<int> detected)
{
    given = root;
    detectedRoot = detected;
    const auto basis = given ? given : detectedRoot;
    shownOctave = basis ? *basis / 12 - 1 : 3;
    repaint();
    if (picker != nullptr)
        picker->repaint();
}

juce::String RootNoteSelector::fieldText() const
{
    if (given)
        return nameOf (*given);
    return detectedRoot ? "Detected  " + nameOf (*detectedRoot) : juce::String ("Detected");
}

int RootNoteSelector::pitchClassShown() const noexcept
{
    const auto basis = given ? given : detectedRoot;
    return basis ? *basis % 12 : 0;
}

bool RootNoteSelector::canStepOctave (int delta) const noexcept
{
    const int next = shownOctave + delta;
    if (next < -1 || next > 9)
        return false;
    // With a root given, the step moves it: only to a root that exists. (Detected: the keys
    // follow, disabling the ones out of range.)
    return ! given || valid (midiFor (*given % 12, next));
}

void RootNoteSelector::commit (std::optional<int> root)
{
    if (root == given)
        return;
    given = root;
    repaint();
    if (picker != nullptr)
        picker->repaint();
    if (onChange != nullptr)
        onChange (root);
}

void RootNoteSelector::choosePitchClass (int pitchClass)
{
    if (pitchClass < 0 || pitchClass > 11 || ! keyEnabled (pitchClass))
        return;
    commit (midiFor (pitchClass, shownOctave));
}

void RootNoteSelector::chooseDetected()
{
    commit (std::nullopt);
}

void RootNoteSelector::stepOctave (int delta)
{
    if (! canStepOctave (delta))
        return;
    shownOctave += delta;
    if (given)
        commit (midiFor (*given % 12, shownOctave));
    repaint();
    if (picker != nullptr)
        picker->repaint();
}

void RootNoteSelector::showPicker (bool shouldShow)
{
    if (shouldShow == pickerShown())
        return;
    if (! shouldShow)
    {
        picker.reset();
        repaint();
        return;
    }
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return;
    picker = std::make_unique<Picker> (*this);
    // Below the field, its right edge on the field's (the inspector's column), kept inside.
    auto at = juce::Rectangle<int> (getRight() - pickerWidth, getBottom() + 4, pickerWidth, pickerHeight);
    at = at.constrainedWithin (parent->getLocalBounds());
    picker->setBounds (at);
    parent->addAndMakeVisible (*picker);
    picker->toFront (false);
    repaint();
}

void RootNoteSelector::paint (juce::Graphics& g)
{
    drawDropdownField (g, getLocalBounds().toFloat().reduced (0.5f, 1.5f), fieldText(), isMouseOver (true) || pickerShown(), hasKeyboardFocus (true), isEnabled());
}

void RootNoteSelector::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && getLocalBounds().contains (e.getPosition()))
        showPicker (! pickerShown());
}

bool RootNoteSelector::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && pickerShown())
    {
        showPicker (false);
        return true;
    }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey)
    {
        showPicker (! pickerShown());
        return true;
    }
    if (! pickerShown())
        return false;
    const int pc = pitchClassShown();
    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey)
    {
        // The next key that exists in this octave.
        const int step = key == juce::KeyPress::leftKey ? -1 : 1;
        for (int i = 1; i <= 12; ++i)
        {
            const int next = ((pc + step * i) % 12 + 12) % 12;
            if (keyEnabled (next))
            {
                choosePitchClass (next);
                break;
            }
        }
        return true;
    }
    if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
    {
        stepOctave (key == juce::KeyPress::upKey ? 1 : -1);
        return true;
    }
    return false;
}

} // namespace osp::plugin
