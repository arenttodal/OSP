#pragma once

#include "CeramicSkin.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace osp::plugin
{

/**
    The keyboard's lower-right action keys (shared by ANDOR OSP and every product built from the
    ANDOR Instrument Shell), in the place the large "Advanced" key had beneath ARP / SUB
    (layout::advanced, 105 x 31): small square ceramic keys with an icon each, no caption.

    - D (a product with an AUX strip only: ANDOR Analog's AUX D): shows or hides the strip.
    - SETTINGS: opens or closes the Advanced popover.
    - INFO: turns the contextual Info (ANDOR Learn) on or off.

    The configuration is the instrument's: with D the three keys fill the area; without it the two
    sit centred (never a placeholder). The component itself takes no click: only its keys do,
    so nothing invisible lies over the keyboard row. It is wider than the area by `margin` on each
    side so the keys' shadows are never cut off; the frame places it (frame::layout).

    Each key is a juce::Button: keyboard focus, accessible title and description, tooltips. A key
    shows "active" (pressed in, its accent) while what it controls is open or on; the editor keeps
    that in step with the real state (setActive).
*/
class KeyboardUtilityActions final : public juce::Component
{
public:
    /** (Append only: products choose their keys by these names.) LIBRARY opens the product's
        own Library (ANDOR OSP's). */
    enum class Action { aux, settings, info, library };

    explicit KeyboardUtilityActions (std::vector<Action> actions = { Action::settings, Action::info });
    ~KeyboardUtilityActions() override;

    /** Which keys there are (left to right). */
    void setActions (std::vector<Action> actions);
    bool has (Action a) const noexcept { return key (a) != nullptr; }

    /** A key was clicked (or activated from the keyboard). */
    std::function<void (Action)> onClick;

    void setActive (Action, bool active);
    bool isActive (Action) const noexcept;
    void setTooltip (Action, const juce::String& tooltip);
    /** The icon's accent (D's purple, Info's green while on). */
    void setAccent (Action, juce::Colour);

    /** The key itself (Learn targets, tests), or nullptr when the instrument has none. */
    juce::Button* button (Action) const noexcept;

    /** Room for the keys' shadows on every side of the 105 x 31 area. */
    static constexpr int margin = 10;
    static constexpr float keySize = 31.0f, gap = 6.0f;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Key;
    Key* key (Action) const noexcept;
    std::vector<std::unique_ptr<Key>> keys;
};

} // namespace osp::plugin
