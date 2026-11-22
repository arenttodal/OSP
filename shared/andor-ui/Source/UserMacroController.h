#pragma once

#include "InstrumentFrame.h"
#include "InstrumentHost.h"
#include "ModulationPanel.h"
#include "UserMacroBank.h"
#include "UserMacroHooks.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace osp::plugin
{

class MiniPanel;

/**
    USER MACROS in an editor: the USER bank (six knobs in CORE's places, their names, lenses and the
    CORE / USER folder tabs), assigning (lens drag, pick mode, a control's right-click menu), the
    names (rename, the label's menu), and the halos' ranges on the controls (ModulationOverlay's
    hooks). ANDOR ANALOG's ShellEditor code (USER MACROS V1.1 and its destination-first, right-click
    and tab fixes), made product-neutral: the editor gives it what it needs (Editor) and calls
    setUp, place, tick, popupClosed, mouseDownAnywhere and keyPressed. ANDOR OSP's editor uses it;
    the shell's ShellEditor moves onto it when ANDOR ANALOG's OSP pin does (docs/research/
    osp-user-macros/00-audit.md). Message thread only.
*/
class UserMacroController
{
public:
    struct Editor
    {
        InstrumentHost* host = nullptr;
        juce::AudioProcessorValueTreeState* parameters = nullptr;
        juce::Component* content = nullptr;                 ///< the instrument (where the knobs live)
        frame::MacroRow* coreRow = nullptr;                 ///< the CORE knobs (USER takes their places)
        ModulationOverlay* modOverlay = nullptr;
        std::function<void()> closePopup;
        /** Opens a panel anchored above `anchor` (the editor's anchored popover); true when open. */
        std::function<bool (std::unique_ptr<MiniPanel>, juce::Component& anchor)> openPanel;
        /** The press that just closed macro `macro`'s panel on its own label (it must not reopen it). */
        std::function<bool (int macro)> consumeLabelPress;
        std::function<void (const juce::String& action, const juce::String& target, const juce::String& parameterId)> learnAction;
        /** The bank shown, an interface preference (0 CORE, 1 USER), never a preset's. */
        std::function<int()> savedBank;
        std::function<void (int bank)> saveBank;
        /** The macro panel's area in content (the tabs rise from its top-left edge). */
        std::function<juce::Rectangle<float>()> macroPanelArea;
        /** After the bank shown changed (the editor's own CORE-only parts, a repaint). */
        std::function<void (int bank)> onBankShown;
        std::function<bool()> isShowing;
    };

    UserMacroController (Editor editor, UserMacroHooks hooks);
    ~UserMacroController();

    void setUp();
    /** After the CORE knobs are placed: USER takes their places, the tabs on the panel's edge. */
    void place();
    /** The editor's timer: names after an undo or a preset, the focused macro's halos. */
    void tick();
    /** The editor's popover closed (the macro panel with it). */
    void popupClosed();
    /** A press anywhere in the editor (a click elsewhere returns the halos to their quiet arcs). */
    void mouseDownAnywhere (juce::Component* c);
    /** Escape during an assignment (a lens drag, a pick) cancels it (true: used). */
    bool keyPressed (const juce::KeyPress& key);

    int bankShown() const noexcept { return shownBank; }
    void showBank (int bank);
    /** The row frame::paint draws (the bank shown). */
    frame::MacroRow& paintedRow() noexcept { return shownBank == 1 ? userRow : *ed.coreRow; }
    frame::MacroRow& row() noexcept { return userRow; }
    MacroLens* lens (int macro) noexcept;
    MacroBankTab* tab() noexcept { return bankTab.get(); }
    MacroAssignOverlay& assignOverlay() noexcept { return overlay; }
    juce::TextEditor* renameEditor() noexcept { return renameField.get(); }
    int panelMacro() const noexcept { return openMacro; }

    void refresh();
    void openPanel (int macro, const juce::String& select = {});
    void beginPick (int macro);
    void cancelAssign();
    /** Tests: a lens dragged from `from` to `to` (screen), dropped or cancelled. */
    void dragLens (int macro, juce::Point<int> from, juce::Point<int> to, bool cancel = false);
    void startRename (int macro);
    void labelMenu (int macro);
    void setChosen (int macro);
    int chosen() const noexcept { return chosenMacro; }
    /** A control's right-click menu: Assign to Macro, and its assignments' range and removal. */
    void addMenuItems (juce::PopupMenu& menu, const juce::String& parameterId);
    /** Whether USER macro `macro` controls something now (Performance Mode shows only those). */
    bool isAssigned (int macro) const { return hooks.activeCount != nullptr && hooks.activeCount (host, macro) > 0; }
    /** USER macro `macro`'s display name (its default "MACRO N" when unnamed). */
    juce::String displayName (int macro) const
    {
        auto n = hooks.name != nullptr ? hooks.name (host, macro) : juce::String();
        return n.isNotEmpty() ? n : "MACRO " + juce::String (macro + 1);
    }
    juce::String menuName (int macro) const;
    juce::String title (int macro) const;
    juce::Colour colour (int macro) const;

private:
    void collectTargets();
    void updateArcs();

    Editor ed;
    UserMacroHooks hooks;
    InstrumentHost& host;
    std::unique_ptr<MacroBankTab> bankTab;
    frame::MacroRow userRow;
    std::array<std::unique_ptr<MacroLens>, 6> lenses;
    MacroAssignOverlay overlay;
    std::unique_ptr<juce::TextEditor> renameField;
    int shownBank = 0, renaming = -1, dragMacro = -1, pickMacro = -1, openMacro = -1, arcsMacro = -1, chosenMacro = -1;
    std::uint64_t revision = 0;
    JUCE_DECLARE_WEAK_REFERENCEABLE (UserMacroController)
};

} // namespace osp::plugin
