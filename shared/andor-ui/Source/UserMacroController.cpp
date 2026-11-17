#include "UserMacroController.h"

#include "Binding.h"
#include "Design.h"
#include "OspLookAndFeel.h"
#include "ShapingPopups.h"

#include <algorithm>

namespace osp::plugin
{

namespace
{
    // The pieces' places in the editor (the editor's own components, the controller's mouse
    // targets): the mouse targets sit over CORE's places, so their hit areas are the frame's.
    constexpr float lensSize = 22.0f, lensBelowKnob = 65.0f;
} // namespace

UserMacroController::UserMacroController (Editor editor, UserMacroHooks h)
    : ed (std::move (editor)), hooks (std::move (h)), host (*ed.host)
{
}

UserMacroController::~UserMacroController()
{
    if (ed.modOverlay != nullptr)
    {
        ed.modOverlay->extendContextMenu = nullptr;
        ed.modOverlay->macroEligible = nullptr;
        ed.modOverlay->anyMacroArcs = nullptr;
        ed.modOverlay->macroArcs = nullptr;
        ed.modOverlay->macroOffset = nullptr;
        ed.modOverlay->editMacroRange = nullptr;
        ed.modOverlay->resetMacroEnd = nullptr;
    }
}

juce::Colour UserMacroController::colour (int macro) const
{
    return design::colour::macro (std::clamp (macro, 0, frame::macroCount - 1));
}

MacroLens* UserMacroController::lens (int macro) noexcept
{
    return bankTab != nullptr && macro >= 0 && macro < frame::macroCount ? lenses[static_cast<std::size_t> (macro)].get() : nullptr;
}

void UserMacroController::setUp()
{
    auto& content = *ed.content;
    juce::WeakReference<UserMacroController> safe (this);
    // The second bank: six knobs in the very places of the first (the same knob, colours and
    // light), hidden until USER is chosen. Their names open their assignments; a double-click
    // renames; a right-click offers both. Their lenses are grab handles.
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const int macro = static_cast<int> (i);
        frame::MacroSpec spec { hooks.parameterIds[i], hooks.name != nullptr ? hooks.name (host, macro) : "MACRO " + juce::String (macro + 1),
                                "User macro " + juce::String (macro + 1) + ": click for its assignments, double-click to rename" };
        frame::setUpMacro (userRow[i], macro, spec, *ed.parameters, content, true);
        userRow[i].slider.setTitle ("User Macro " + juce::String (macro + 1));
        userRow[i].label->onClick = [safe, macro] {
            if (safe == nullptr)
                return;
            if (safe->ed.consumeLabelPress != nullptr && safe->ed.consumeLabelPress (macro))
                return;   // (the press that just closed its panel on its own name)
            safe->openPanel (macro);
        };
        userRow[i].label->setRackActions ([safe, macro] { if (safe != nullptr) safe->startRename (macro); },
                                          [safe, macro] { if (safe != nullptr) safe->labelMenu (macro); });
        auto l = std::make_unique<MacroLens>();
        l->onClick = [safe, macro] { if (safe != nullptr) safe->openPanel (macro); };
        l->onDragMove = [safe, macro] (juce::Point<int> screen) {
            if (safe == nullptr)
                return;
            auto& self = *safe;
            if (self.dragMacro != macro)
            {
                if (self.ed.closePopup != nullptr)
                    self.ed.closePopup();
                self.dragMacro = macro;
                self.collectTargets();
                self.overlay.beginDrag (self.colour (macro), self.hooks.name (self.host, macro));
            }
            self.overlay.pointAt (self.ed.content->getLocalPoint (nullptr, screen).toFloat());
        };
        l->onDragEnd = [safe, macro] (juce::Point<int> screen, bool cancelled) {
            if (safe == nullptr)
                return;
            auto& self = *safe;
            const auto id = cancelled ? juce::String() : self.overlay.controlAt (self.ed.content->getLocalPoint (nullptr, screen).toFloat());
            self.dragMacro = -1;
            self.overlay.stop();
            if (id.isNotEmpty() && self.hooks.assign (self.host, macro, id))
            {
                self.openPanel (macro, id);   // (made neutral; its range in the panel)
                if (self.ed.learnAction != nullptr)
                    self.ed.learnAction ("usermacro.assigned", "macros", id);
            }
            self.refresh();
        };
        content.addChildComponent (*l);
        lenses[i] = std::move (l);
    }
    bankTab = std::make_unique<MacroBankTab>();
    bankTab->onChange = [safe] (int bank) {
        if (safe == nullptr)
            return;
        safe->showBank (bank);
        if (safe->ed.learnAction != nullptr)
            safe->ed.learnAction ("macroBank.changed", "macros.bank", {});   // (a click on CORE / USER: the musician's)
    };
    content.addAndMakeVisible (*bankTab);
    // Over everything while assigning (it takes clicks only when picking, only on a target).
    overlay.onPicked = [safe] (const juce::String& id) {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        const int macro = self.pickMacro;
        self.pickMacro = -1;
        self.overlay.stop();
        if (macro >= 0 && self.hooks.assign (self.host, macro, id))
        {
            self.openPanel (macro, id);
            if (self.ed.learnAction != nullptr)
                self.ed.learnAction ("usermacro.assigned", "macros", id);
        }
        self.refresh();
    };
    overlay.onCancel = [safe] { if (safe != nullptr) safe->cancelAssign(); };
    content.addAndMakeVisible (overlay);

    if (auto* mod = ed.modOverlay)
    {
        // A control's right-click menu: its existing items unchanged, first; then its macros
        // (Assign to Macro, and for an assigned control its range, removal, details).
        mod->extendContextMenu = [safe] (juce::PopupMenu& menu, const juce::String& id) {
            if (safe == nullptr)
                return;
            safe->addMenuItems (menu, id);
            if (safe->ed.learnAction != nullptr)
                safe->ed.learnAction ("control.menu.opened", "control", id);
        };
        // The halos draw the USER macros' ranges in their own vocabulary (docs/research/
        // osp-user-macros/; ANDOR ANALOG's docs/MACRO_DESTINATION_FIRST.md).
        mod->macroEligible = [safe] (const juce::String& id) {
            return safe != nullptr && safe->hooks.eligible != nullptr && safe->hooks.eligible (safe->host, id);
        };
        mod->anyMacroArcs = [safe] {
            if (safe == nullptr || safe->hooks.targets == nullptr)
                return false;
            for (int m = 0; m < frame::macroCount; ++m)
                if (! safe->hooks.targets (safe->host, m).isEmpty())
                    return true;
            return false;
        };
        mod->macroArcs = [safe] (const juce::String& id) {
            std::vector<ModulationOverlay::MacroArc> arcs;
            if (safe == nullptr || safe->hooks.ranges == nullptr)
                return arcs;
            for (const auto& r : safe->hooks.ranges (safe->host, id))
            {
                ModulationOverlay::MacroArc a;
                a.macro = r.macro;
                a.colour = safe->colour (r.macro);
                a.name = safe->title (r.macro);
                a.atMin = r.atMin;
                a.atMax = r.atMax;
                a.now = r.now;
                a.base = r.base;
                a.macroValue = r.macroValue;
                a.enabled = r.enabled;
                arcs.push_back (a);
            }
            return arcs;
        };
        mod->macroOffset = [safe] (const juce::String& id) {
            return safe != nullptr && safe->hooks.offsetNow != nullptr ? safe->hooks.offsetNow (safe->host, id) : 0.0f;
        };
        mod->editMacroRange = [safe] (int m, const juce::String& id, int end, float position, ModulationOverlay::MacroEdit phase) {
            if (safe == nullptr || safe->hooks.editRange == nullptr)
                return;
            auto& self = *safe;
            self.hooks.editRange (self.host, m, id, end, position, static_cast<int> (phase));
            if (phase == ModulationOverlay::MacroEdit::begin && m != self.ed.modOverlay->macroFocus())
                self.setChosen (m);   // (editing it: its controls shown)
            if (phase == ModulationOverlay::MacroEdit::commit || phase == ModulationOverlay::MacroEdit::cancel)
                self.refresh();
            if (phase == ModulationOverlay::MacroEdit::commit && self.ed.learnAction != nullptr)
                self.ed.learnAction ("usermacro.range.changed", "macros", id);   // (an end dragged on the halo)
        };
        mod->resetMacroEnd = [safe] (int m, const juce::String& id, int end) {
            if (safe != nullptr && safe->hooks.resetEnd != nullptr)
                safe->hooks.resetEnd (safe->host, m, id, end);
        };
    }
    // Touching a USER knob chooses its macro (its controls' rings shown while it moves them).
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const int macro = static_cast<int> (i);
        userRow[i].slider.onDragStart = [safe, macro] { if (safe != nullptr) safe->setChosen (macro); };
    }
    shownBank = -1;
    showBank (ed.savedBank != nullptr ? ed.savedBank() : 0);
    refresh();
}

void UserMacroController::place()
{
    if (bankTab == nullptr || ed.coreRow == nullptr)
        return;
    auto& macros = *ed.coreRow;
    // USER 1..6 read left to right: each takes the place of the CORE knob in its column (CORE's
    // data order is not its visual order).
    std::array<std::size_t, frame::macroCount> column {};
    for (std::size_t i = 0; i < column.size(); ++i)
        column[i] = i;
    std::sort (column.begin(), column.end(), [&macros] (std::size_t a, std::size_t b) {
        return macros[a].slider.getX() < macros[b].slider.getX();
    });
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const auto& at = macros[column[i]];
        if (userRow[i].label != nullptr && at.label != nullptr)
            userRow[i].label->setBounds (at.label->getBounds());
        userRow[i].slider.setBounds (at.slider.getBounds());
        // The lens: where the frame draws the light under the knob, a 22-unit target.
        const auto knob = at.slider.getBounds().toFloat();
        lenses[i]->setBounds (juce::Rectangle<float> (lensSize, lensSize).withCentre ({ knob.getCentreX(), knob.getCentreY() + lensBelowKnob }).toNearestInt());
    }
    // CORE and USER: two folder tabs rising from the macro panel's top-left edge (nothing below
    // them moves; their targets are larger than drawn).
    if (ed.macroPanelArea != nullptr)
    {
        const auto panel = ed.macroPanelArea();
        bankTab->setBounds (MacroBankTab::boundsFor (panel).toNearestInt());
        bankTab->setPanel (panel, design::layout::panelRadius);
    }
    overlay.setBounds (ed.content->getLocalBounds());
    overlay.toFront (false);
}

void UserMacroController::showBank (int bank)
{
    if (bankTab == nullptr)
        return;
    bank = bank == 1 ? 1 : 0;
    const bool changed = bank != shownBank;
    shownBank = bank;
    bankTab->setBank (bank);
    if (changed)
    {
        if (ed.saveBank != nullptr)
            ed.saveBank (bank);
        cancelAssign();
        if (openMacro >= 0 && ed.closePopup != nullptr)
            ed.closePopup();
        renameField.reset();
        renaming = -1;
    }
    if (bank == 0)
        chosenMacro = -1;
    // Presentation only: the knobs shown change, the sound never does.
    auto& macros = *ed.coreRow;
    for (std::size_t i = 0; i < macros.size(); ++i)
    {
        const bool core = bank == 0;
        macros[i].slider.setVisible (core);
        if (macros[i].label != nullptr)
            macros[i].label->setVisible (core);
        userRow[i].slider.setVisible (! core);
        if (userRow[i].label != nullptr)
            userRow[i].label->setVisible (! core);
        lenses[i]->setVisible (! core);
    }
    if (changed && ed.onBankShown != nullptr)
        ed.onBankShown (bank);
    ed.content->repaint();
}

void UserMacroController::refresh()
{
    if (bankTab == nullptr)
        return;
    juce::WeakReference<UserMacroController> safe (this);
    for (std::size_t i = 0; i < userRow.size(); ++i)
    {
        const int macro = static_cast<int> (i);
        const auto name = hooks.name (host, macro);
        if (auto* label = userRow[i].label.get())
        {
            if (label->getTitle() != name)
            {
                // (the label's text is its own: a new one in its place, its handlers as before)
                auto onClick = label->onClick;
                const auto bounds = label->getBounds();
                const bool visible = label->isVisible();
                userRow[i].label = std::make_unique<MacroLabel> (name, true, false);
                userRow[i].label->setAccent (colour (macro));
                userRow[i].label->setTooltip ("User macro " + juce::String (macro + 1) + ": click for its assignments, double-click to rename");
                userRow[i].label->onClick = onClick;
                userRow[i].label->setRackActions ([safe, macro] { if (safe != nullptr) safe->startRename (macro); },
                                                  [safe, macro] { if (safe != nullptr) safe->labelMenu (macro); });
                binding::tag (*userRow[i].label, hooks.parameterIds[i]);
                ed.content->addChildComponent (*userRow[i].label);
                userRow[i].label->setBounds (bounds);
                userRow[i].label->setVisible (visible);
                overlay.toFront (false);
            }
            const bool lit = hooks.activeCount != nullptr && hooks.activeCount (host, macro) > 0;
            if (userRow[i].label->isCustomised() != lit)
                userRow[i].label->setCustomised (lit);   // (its lens lights while it moves something)
            userRow[i].label->setOpen (openMacro == macro);
        }
        lenses[i]->setTooltip ("Drag to assign " + name);
        userRow[i].slider.setTitle ("User Macro " + juce::String (macro + 1) + " (" + name + ")");
    }
    arcsMacro = -2;   // (the arcs recollected at the next tick)
    ed.content->repaint();
}

void UserMacroController::openPanel (int macro, const juce::String& select)
{
    if (hooks.panel == nullptr || macro < 0 || macro >= frame::macroCount || ed.openPanel == nullptr)
        return;
    cancelAssign();
    auto panel = hooks.panel (host, macro, select);
    if (panel == nullptr)
        return;
    auto* anchor = userRow[static_cast<std::size_t> (macro)].label.get();
    if (ed.openPanel (std::move (panel), *anchor))
        openMacro = macro;
    refresh();
    if (openMacro == macro && ed.learnAction != nullptr)
        ed.learnAction ("usermacro.panel.opened", "macros", {});
}

void UserMacroController::popupClosed()
{
    if (openMacro < 0)
        return;
    openMacro = -1;
    refresh();
}

void UserMacroController::collectTargets()
{
    // Every control on screen a USER macro may move (the product decides), front-most last.
    std::vector<MacroAssignOverlay::Control> found;
    auto& content = *ed.content;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        for (auto* child : c.getChildren())
        {
            if (child == &overlay || child == ed.modOverlay || ! child->isVisible())
                continue;
            if (auto* slider = dynamic_cast<juce::Slider*> (child))
            {
                juce::String id = slider->getProperties()["paramId"].toString();
                if (id.isEmpty())
                    id = binding::tagsOf (*slider)[0];
                const bool own = std::find (hooks.parameterIds.begin(), hooks.parameterIds.end(), id) != hooks.parameterIds.end();
                if (id.isNotEmpty() && ! own && hooks.eligible != nullptr && hooks.eligible (host, id))
                {
                    const auto name = hooks.targetName != nullptr ? hooks.targetName (host, id) : slider->getTitle();
                    found.push_back ({ id, name, content.getLocalArea (slider, slider->getLocalBounds()).toFloat() });
                }
            }
            walk (*child);
        }
    };
    walk (content);
    overlay.setControls (std::move (found));
}

void UserMacroController::beginPick (int macro)
{
    if (macro < 0 || macro >= frame::macroCount)
        return;
    if (ed.closePopup != nullptr)
        ed.closePopup();   // (the panel folds away while a control is chosen; it comes back after)
    pickMacro = macro;
    collectTargets();
    const auto panel = ed.macroPanelArea != nullptr ? ed.macroPanelArea() : juce::Rectangle<float>();
    overlay.beginPick (colour (macro), hooks.name (host, macro), panel);
    overlay.toFront (false);
}

void UserMacroController::cancelAssign()
{
    if (dragMacro >= 0)
        if (auto* l = lenses[static_cast<std::size_t> (dragMacro)].get())
            l->cancelDrag();
    dragMacro = -1;
    pickMacro = -1;
    if (overlay.mode() == MacroAssignOverlay::Mode::drag || overlay.mode() == MacroAssignOverlay::Mode::pick)
        overlay.stop();
}

bool UserMacroController::keyPressed (const juce::KeyPress& key)
{
    if (key != juce::KeyPress::escapeKey)
        return false;
    if (overlay.mode() == MacroAssignOverlay::Mode::drag || overlay.mode() == MacroAssignOverlay::Mode::pick)
    {
        cancelAssign();
        return true;
    }
    return false;
}

void UserMacroController::mouseDownAnywhere (juce::Component* c)
{
    // A click elsewhere returns the halos to their quiet arcs (a click on a macro's own knob or
    // name, or on a halo, keeps it).
    if (chosenMacro < 0 || c == nullptr || c == ed.modOverlay)
        return;
    for (std::size_t i = 0; i < userRow.size(); ++i)
        if (c == &userRow[i].slider || userRow[i].slider.isParentOf (c) || c == userRow[i].label.get() || c == lenses[i].get())
            return;
    // A control the chosen macro moves (turning it, in a popover or on the instrument): its
    // range stays shown - that is the control being worked on, not "elsewhere".
    auto* slider = dynamic_cast<juce::Slider*> (c);
    if (slider == nullptr)
        slider = c->findParentComponentOfClass<juce::Slider>();
    if (slider != nullptr && hooks.targets != nullptr)
        if (const auto id = slider->getProperties()["paramId"].toString(); id.isNotEmpty() && hooks.targets (host, chosenMacro).contains (id))
            return;
    setChosen (-1);
}

void UserMacroController::dragLens (int macro, juce::Point<int> from, juce::Point<int> to, bool cancel)
{
    auto* l = lens (macro);
    if (l == nullptr || l->onDragMove == nullptr)
        return;
    l->onDragMove (from);
    l->onDragMove (to);
    l->onDragEnd (to, cancel);
}

void UserMacroController::tick()
{
    if (bankTab == nullptr)
        return;
    if (hooks.revision != nullptr)
        if (const auto now = hooks.revision (host); now != revision)
        {
            revision = now;   // (an undo, a preset, a session: names and lenses follow)
            refresh();
        }
    updateArcs();
}

void UserMacroController::updateArcs()
{
    if (ed.modOverlay == nullptr)
        return;
    // (on either tab: a macro chosen from a control's menu or its halo shows its ranges where
    // the musician is - the tab changes only when they click it)
    if (dragMacro >= 0 || pickMacro >= 0)
    {
        arcsMacro = -1;
        ed.modOverlay->setMacroFocus (-1);
        return;
    }
    // The focused macro: its panel open, else its name hovered, else the one chosen (from a
    // control's menu, a handle, its knob). Its controls' halos show its ranges, handles live.
    int shown = openMacro;
    for (std::size_t i = 0; i < userRow.size() && shown < 0; ++i)
        if (userRow[i].label != nullptr && userRow[i].label->isVisible() && userRow[i].label->isMouseOver())
            shown = static_cast<int> (i);
    if (shown < 0)
        shown = chosenMacro;
    arcsMacro = shown;
    if (ed.modOverlay->macroFocus() != shown)
        ed.modOverlay->setMacroFocus (shown);
}

void UserMacroController::setChosen (int macro)
{
    // Never switches the macro tab: assigning from a menu or dragging a range on a halo keeps
    // the tab the musician chose (only a click on CORE / USER changes it).
    chosenMacro = macro >= 0 && macro < frame::macroCount ? macro : -1;
    updateArcs();
}

juce::String UserMacroController::menuName (int macro) const
{
    // "Macro 1", or "Macro 1 - BRIGHTNESS" once named (the macro's own name, as on its knob).
    const auto name = hooks.name != nullptr ? hooks.name (host, macro) : juce::String();
    const auto number = "Macro " + juce::String (macro + 1);
    return name.isEmpty() || name.equalsIgnoreCase (number) ? number : number + juce::String::fromUTF8 (" \xe2\x80\x94 ") + name;
}

juce::String UserMacroController::title (int macro) const
{
    const auto name = hooks.name != nullptr ? hooks.name (host, macro) : juce::String();
    const auto number = "MACRO " + juce::String (macro + 1);
    return name.isEmpty() || name.equalsIgnoreCase (number) ? number : number + juce::String::fromUTF8 (" \xc2\xb7 ") + name;
}

void UserMacroController::addMenuItems (juce::PopupMenu& menu, const juce::String& id)
{
    if (hooks.eligible == nullptr || ! hooks.eligible (host, id))
        return;   // (not a control a macro may move: no misleading item)
    juce::WeakReference<UserMacroController> safe (this);
    std::vector<int> assigned;
    if (hooks.targets != nullptr)
        for (int m = 0; m < frame::macroCount; ++m)
            if (hooks.targets (host, m).contains (id))
                assigned.push_back (m);
    menu.addSeparator();
    juce::PopupMenu choose;
    for (int m = 0; m < frame::macroCount; ++m)
    {
        const bool has = std::find (assigned.begin(), assigned.end(), m) != assigned.end();
        choose.addItem (menuName (m), true, has, [safe, m, id] {
            if (safe == nullptr)
                return;
            // Assigned at once, with a visible range around where the control sits (no jump);
            // its macro chosen, so its range is on the knob, ready to drag.
            if (safe->hooks.assign (safe->host, m, id))
            {
                safe->setChosen (m);
                if (safe->ed.learnAction != nullptr)
                    safe->ed.learnAction ("usermacro.assigned", "macros", id);
            }
            safe->refresh();
        });
    }
    menu.addSubMenu ("Assign to Macro", choose);
    if (assigned.empty())
        return;
    // Per assigned macro (one: plain items; several: one submenu each way).
    auto perMacro = [&] (const juce::String& itemTitle, std::function<void (UserMacroController&, int)> action) {
        if (assigned.size() == 1)
        {
            const int m = assigned.front();
            menu.addItem (itemTitle, [safe, m, action] { if (safe != nullptr) action (*safe, m); });
            return;
        }
        juce::PopupMenu sub;
        for (const int m : assigned)
            sub.addItem (menuName (m), [safe, m, action] { if (safe != nullptr) action (*safe, m); });
        menu.addSubMenu (itemTitle, sub);
    };
    perMacro ("Edit Macro Range", [] (UserMacroController& self, int m) { self.setChosen (m); });
    perMacro ("Reset Macro Range", [id] (UserMacroController& self, int m) {
        if (self.hooks.resetRange != nullptr)
            self.hooks.resetRange (self.host, m, id);
        self.setChosen (m);
    });
    perMacro ("Show Macro Assignment Details", [id] (UserMacroController& self, int m) {
        self.showBank (1);
        self.openPanel (m, id);
    });
    for (const int m : assigned)
        menu.addItem ("Remove from " + menuName (m), [safe, m, id] {
            if (safe == nullptr)
                return;
            if (safe->hooks.unassign != nullptr)
                safe->hooks.unassign (safe->host, m, id);
            safe->refresh();
        });
    if (assigned.size() > 1)
        menu.addItem ("Remove All Macro Assignments", [safe, id] {
            if (safe == nullptr)
                return;
            if (safe->hooks.unassignAll != nullptr)
                safe->hooks.unassignAll (safe->host, id);
            safe->refresh();
        });
}

void UserMacroController::startRename (int macro)
{
    if (hooks.rename == nullptr || macro < 0 || macro >= frame::macroCount)
        return;
    if (ed.closePopup != nullptr)
        ed.closePopup();
    auto* label = userRow[static_cast<std::size_t> (macro)].label.get();
    renaming = macro;
    renameField = std::make_unique<juce::TextEditor> ("Rename user macro");
    renameField->setTitle ("Rename user macro " + juce::String (macro + 1));
    renameField->setJustification (juce::Justification::centred);
    renameField->setFont (type::macroLabel (0.7f * static_cast<float> (label->getHeight())));
    renameField->setInputRestrictions (24);
    renameField->setText (hooks.name (host, macro), false);
    renameField->setColour (juce::TextEditor::backgroundColourId, palette::ivory);
    renameField->setColour (juce::TextEditor::textColourId, palette::text);
    renameField->setColour (juce::TextEditor::outlineColourId, design::colour::hairline);
    renameField->setColour (juce::TextEditor::focusedOutlineColourId, colour (macro));
    ed.content->addAndMakeVisible (*renameField);
    renameField->setBounds (label->getBounds().expanded (4, 2));
    renameField->selectAll();
    juce::WeakReference<UserMacroController> safe (this);
    auto commit = [safe] (bool keep) {
        if (safe == nullptr || safe->renameField == nullptr || safe->renaming < 0)
            return;
        auto& self = *safe;
        const int m = self.renaming;
        const auto text = self.renameField->getText();
        self.renaming = -1;
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->renameField.reset(); });
        if (keep && self.hooks.rename != nullptr)
        {
            self.hooks.rename (self.host, m, text);
            if (self.ed.learnAction != nullptr)
                self.ed.learnAction ("usermacro.renamed", "macros", {});
        }
        self.refresh();
    };
    renameField->onReturnKey = [commit] { commit (true); };
    renameField->onEscapeKey = [commit] { commit (false); };
    renameField->onFocusLost = [commit] { commit (true); };
    if (ed.isShowing == nullptr || ed.isShowing())
        if (ed.content->isShowing())
            renameField->grabKeyboardFocus();
}

void UserMacroController::labelMenu (int macro)
{
    juce::PopupMenu menu;
    juce::WeakReference<UserMacroController> safe (this);
    menu.addItem ("Edit Assignments", [safe, macro] { if (safe != nullptr) safe->openPanel (macro); });
    menu.addItem ("Rename", [safe, macro] { if (safe != nullptr) safe->startRename (macro); });
    menu.addSeparator();
    const bool any = hooks.targets != nullptr && ! hooks.targets (host, macro).isEmpty();
    menu.addItem ("Clear Assignments", any, false, [safe, macro] {
        if (safe == nullptr)
            return;
        auto options = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon)
                           .withTitle ("Clear assignments")
                           .withMessage ("Remove every assignment of " + safe->hooks.name (safe->host, macro) + "? (Undo brings them back.)")
                           .withButton ("Clear").withButton ("Cancel").withAssociatedComponent (safe->ed.content);
        juce::AlertWindow::showAsync (options, [safe, macro] (int result) {
            if (safe != nullptr && result == 1)
            {
                safe->hooks.clear (safe->host, macro);
                safe->refresh();
            }
        });
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (userRow[static_cast<std::size_t> (macro)].label.get()));
}

} // namespace osp::plugin
