#include "KeyboardUtilityActions.h"
#include "Design.h"
#include "OspLookAndFeel.h"

namespace osp::plugin
{

/** One square key: its icon only (the face is drawn by the group, as the housing draws keys). */
class KeyboardUtilityActions::Key final : public juce::Button
{
public:
    explicit Key (Action a) : juce::Button (titleOf (a)), action (a)
    {
        setTitle (titleOf (a));
        setDescription (descriptionOf (a));
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        accent = a == Action::aux ? juce::Colour (0xff8a6d84) : juce::Colour (0xff2fbf6c);
    }

    Action action;
    bool active = false;
    juce::Colour accent;

    skin::KeyState keyState() const
    {
        return active || isDown() ? skin::KeyState::pressed : isOver() ? skin::KeyState::hover : skin::KeyState::normal;
    }

    void buttonStateChanged() override { repaintFace(); }
    void focusGained (FocusChangeType) override { repaintFace(); }
    void focusLost (FocusChangeType) override { repaintFace(); }

    void repaintFace()
    {
        if (auto* parent = getParentComponent())
            parent->repaint (getBounds().expanded (KeyboardUtilityActions::margin));
        repaint();
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        using namespace design;
        const auto r = getLocalBounds().toFloat();
        const auto c = r.getCentre().translated (0.0f, down || active ? 0.5f : 0.0f);
        const auto ink = colour::text.withAlpha (highlighted || active ? 0.95f : 0.8f);
        switch (action)
        {
            case Action::aux:
            {
                // A "D" of the AUX badge's family, in AUX's restrained purple (never a power key).
                g.setColour (active ? accent.darker (0.25f) : accent.withAlpha (highlighted ? 1.0f : 0.85f));
                g.setFont (fonts::make (17.0f, fonts::Weight::medium, 0.0f));
                g.drawText ("D", juce::Rectangle<float> (16.0f, 18.0f).withCentre (c.translated (0.0f, 0.5f)), juce::Justification::centred, false);
                break;
            }
            case Action::settings:
            {
                // Three sliders, line art: rails with their handles at different places.
                const float w = 13.0f;
                const float xs[] { 0.68f, 0.3f, 0.55f };
                for (int i = 0; i < 3; ++i)
                {
                    const float y = c.y + (static_cast<float> (i) - 1.0f) * 4.5f;
                    g.setColour (ink.withMultipliedAlpha (0.75f));
                    g.fillRoundedRectangle (c.x - 0.5f * w, y - 0.6f, w, 1.2f, 0.6f);
                    const float hx = c.x - 0.5f * w + xs[i] * w;
                    g.setColour (ink);
                    g.fillEllipse (juce::Rectangle<float> (3.6f, 3.6f).withCentre ({ hx, y }));
                }
                break;
            }
            case Action::library:
            {
                // Books on a shelf, line art in the sliders' weight: two upright spines and a third
                // tilted 5 degrees (the owner's choice, 17 November), standing on its outer foot
                // on the shelf line, so the three stand on one optical baseline.
                const float base = c.y + 6.0f, h = 12.0f;
                g.setColour (ink);
                g.drawRoundedRectangle (juce::Rectangle<float> (c.x - 7.0f, base - h, 3.6f, h), 0.8f, 1.2f);
                g.drawRoundedRectangle (juce::Rectangle<float> (c.x - 2.6f, base - h + 1.5f, 3.6f, h - 1.5f), 0.8f, 1.2f);
                juce::Path leaning;
                leaning.addRoundedRectangle (juce::Rectangle<float> (-3.6f, -h + 1.0f, 3.6f, h - 1.0f), 0.8f);
                leaning.applyTransform (juce::AffineTransform::rotation (juce::degreesToRadians (5.0f)).translated (c.x + 6.2f, base));
                g.strokePath (leaning, juce::PathStrokeType (1.2f));
                g.setColour (ink.withMultipliedAlpha (0.75f));
                g.fillRoundedRectangle (c.x - 8.0f, base + 0.6f, 16.0f, 1.2f, 0.6f);
                break;
            }
            case Action::info:
            {
                // A small circled "i"; on, the circle in Learn's green.
                const auto ring = juce::Rectangle<float> (14.0f, 14.0f).withCentre (c);
                if (active)
                {
                    g.setColour (accent.withAlpha (0.16f));
                    g.fillEllipse (ring);
                }
                g.setColour (active ? accent.darker (0.2f) : ink);
                g.drawEllipse (ring, 1.3f);
                g.fillEllipse (juce::Rectangle<float> (1.9f, 1.9f).withCentre ({ c.x, c.y - 3.1f }));
                g.fillRoundedRectangle (c.x - 0.75f, c.y - 1.0f, 1.5f, 5.0f, 0.75f);
                break;
            }
        }
        if (hasKeyboardFocus (true))
        {
            g.setColour (colour::text.withAlpha (0.35f));
            g.drawRoundedRectangle (r.reduced (1.5f), 6.0f, 1.0f);
        }
    }

private:
    static juce::String titleOf (Action a)
    {
        switch (a)
        {
            case Action::aux: return "AUX D";
            case Action::settings: return "Advanced Settings";
            case Action::info: return "Info";
            case Action::library: return "Library";
        }
        return {};
    }
    static juce::String descriptionOf (Action a)
    {
        switch (a)
        {
            case Action::aux: return "Shows or hides AUX D below the oscillator cards";
            case Action::settings: return "Opens or closes the Advanced settings";
            case Action::info: return "Turns contextual Info on or off";
            case Action::library: return "Opens or closes the Library of sounds and instruments";
        }
        return {};
    }
};

KeyboardUtilityActions::KeyboardUtilityActions (std::vector<Action> actions)
{
    // Only the keys take the mouse: the shadow margin around them is no click target.
    setInterceptsMouseClicks (false, true);
    setActions (std::move (actions));
}

KeyboardUtilityActions::~KeyboardUtilityActions() = default;

void KeyboardUtilityActions::setActions (std::vector<Action> actions)
{
    keys.clear();
    for (const auto a : actions)
    {
        auto k = std::make_unique<Key> (a);
        k->onClick = [this, a] {
            if (onClick != nullptr)
                onClick (a);
        };
        addAndMakeVisible (*k);
        keys.push_back (std::move (k));
    }
    resized();
    repaint();
}

KeyboardUtilityActions::Key* KeyboardUtilityActions::key (Action a) const noexcept
{
    for (const auto& k : keys)
        if (k->action == a)
            return k.get();
    return nullptr;
}

juce::Button* KeyboardUtilityActions::button (Action a) const noexcept { return key (a); }

void KeyboardUtilityActions::setActive (Action a, bool active)
{
    if (auto* k = key (a); k != nullptr && k->active != active)
    {
        k->active = active;
        k->repaintFace();
    }
}

bool KeyboardUtilityActions::isActive (Action a) const noexcept
{
    const auto* k = key (a);
    return k != nullptr && k->active;
}

void KeyboardUtilityActions::setTooltip (Action a, const juce::String& tooltip)
{
    if (auto* k = key (a))
        k->setTooltip (tooltip);
}

void KeyboardUtilityActions::setAccent (Action a, juce::Colour colour)
{
    if (auto* k = key (a))
    {
        k->accent = colour;
        k->repaint();
    }
}

void KeyboardUtilityActions::resized()
{
    // The keys centred in the 105 x 31 area inside the shadow margin: three fill it (31 + 6 +
    // 31 + 6 + 31), two sit in its middle.
    const auto area = getLocalBounds().toFloat().reduced (static_cast<float> (margin));
    const auto n = static_cast<float> (keys.size());
    const float total = n * keySize + std::max (0.0f, n - 1.0f) * gap;
    // (whole units: every key exactly keySize, every gap exactly gap)
    int x = juce::roundToInt (area.getCentreX() - 0.5f * total);
    const int y = juce::roundToInt (area.getCentreY() - 0.5f * keySize);
    const int size = juce::roundToInt (keySize), step = size + juce::roundToInt (gap);
    for (auto& k : keys)
    {
        k->setBounds (x, y, size, size);
        x += step;
    }
}

void KeyboardUtilityActions::paint (juce::Graphics& g)
{
    using namespace design;
    // Each key's face (the housing's ceramic key, or the classic raised one), under its icon.
    for (const auto& k : keys)
    {
        if (! k->isVisible())
            continue;
        const auto b = k->getBounds().toFloat();
        if (skin::key (g, b, 7.0f, skin::KeyRole::eq, k->keyState()))
            continue;
        const auto r = b.reduced (1.0f);
        if (k->keyState() == skin::KeyState::pressed)
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffe2d9cc), 0.0f, r.getY(), juce::Colour (0xffebe4d9), 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 7.0f);
            g.setColour (colour::hairline.darker (0.1f));
            g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
        }
        else
            draw::raised (g, r, 7.0f, k->isOver() ? colour::panelTop.brighter (0.02f) : colour::panelTop, colour::panelBottom);
    }
}

} // namespace osp::plugin
