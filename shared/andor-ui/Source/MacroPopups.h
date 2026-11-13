#pragma once

#include "EffectRack.h"
#include "ShapingPopups.h"

namespace osp::plugin
{

// ANDOR OSP's macro popovers (LIFE, DRIVE, CHARACTER, MOVEMENT, SPACE, ECHO) and its REIMAGINED
// popover, shared so a sister product shows exactly the same ones. They need only the
// InstrumentHost (its parameters, the shaper's custom pattern and a few live displays).

/** Which macro a popup belongs to (the order of the macro row). */
enum class MacroPopup { life, drive, character, movement, space, echo };

/** The parameters behind a macro's popup (the "customised" dot compares them to their defaults). */
const juce::StringArray& popupParameterIds (MacroPopup macro);
/** The macro popups (MacroPopups.cpp): each with its own visualisation of what it does. */
std::unique_ptr<MiniPanel> createMacroPopup (MacroPopup macro, InstrumentHost& host);
/** REIMAGINED (one per layer): the mode, its picture and its own two or three settings. */
std::unique_ptr<MiniPanel> createReimaginedPopup (InstrumentHost& host, int layer);
/** An effect's module for the effects rack (EffectRack.h), built from the same parts as its
    popover; nullptr for a kind not built yet. */
std::unique_ptr<rack::Module> createRackModule (rack::Kind kind, InstrumentHost& host);
/** CHARACTER's filter response (dB) at f: the CharacterFilter designs, as its popover and its
    rack draw them (type: FilterType as an int; cutoff fc in Hz; resonance 0..0.9; TILT's dB). */
double characterResponseDb (int type, double f, double fc, double resonance, double tiltDb);
/** The MOVEMENT module's lane shown (tests), -1 for another module. */
int rackMovementLane (const rack::Module& module);
/** MOVEMENT presets: one lane's settings (mode, every mode's own, INTENSITY, SYNC, CYCLE,
    PHASE, CUSTOM steps) in a small JSON file in Documents/ANDOR/Movement Presets. Loading one
    changes that lane only (it is switched on), nothing else of the instrument. */
juce::File movementPresetFolder();
juce::Array<juce::File> savedMovementPresets();
bool writeMovementPreset (InstrumentHost& host, int lane, const juce::File& file);
bool readMovementPreset (InstrumentHost& host, int lane, const juce::File& file);
/** MOVEMENT's lanes: copy one onto the other (not its ON), or swap them (ON too). */
void copyMovementLane (InstrumentHost& host, int from, int to);
void swapMovementLanes (InstrumentHost& host);
/** Tests: show a lane in the MOVEMENT module. */
void showRackMovementLane (rack::Module& module, int lane);
/** Tests: the MOVEMENT rack's title row (its components' names), its lane's power (bounds, a
    click), the lanes' actions (A / B selector's menu; run one by index) and the presets' menu's. */
juce::StringArray rackMovementTitleRow (const rack::Module& module);
juce::Rectangle<int> rackMovementLaneSwitch (const rack::Module& module);   ///< the shown lane's power (title row)
/** Tests: a rack module's layout family for its mode (0 standard, 1 dense: eight controls or
    more - narrower slots, the knobs as large). */
int rackDensity (const rack::Module& module);
/** Tests: every control cell's bounds in order, and the control of the cell captioned `caption`
    (a knob, or a cell's switch / list), or null. */
std::vector<juce::Rectangle<int>> rackCellBounds (const rack::Module& module);
juce::Component* rackCell (const rack::Module& module, const juce::String& caption);
void clickRackMovementLaneSwitch (rack::Module& module);
juce::StringArray rackMovementLaneActions (rack::Module& module);
void runRackMovementLaneAction (rack::Module& module, int index);
juce::StringArray rackMovementPresetMenuExtras (const rack::Module& module);
/** Tests and snapshots: SHAPER's editor widened across the rack (its magnifier). */
void setRackMovementWide (rack::Module& module, bool wide);
/** Tests: a module's presets bar - what it shows, choosing one by name, the arrows, saving
    the settings as the musician's own, the presets of the mode it is in. */
juce::String rackPresetText (rack::Module& module);
bool rackPresetChoose (rack::Module& module, const juce::String& name);
void rackPresetStep (rack::Module& module, int delta);
bool rackPresetSave (rack::Module& module, const juce::String& name);
juce::StringArray rackPresetNames (rack::Module& module);
/** Tests: a module's slot (2 MODE, 3 PRESET, 4 the first control cell, 5 the picture), in its
    own coordinates. */
juce::Rectangle<int> rackSlot (const rack::Module& module, int slot);
/** Tests: V4's TAPE deck (opt-in by the faceplate) - shown, its bounds, its reels' speed
    (rad/s), and a step of its animation (what its timer does, `seconds` at a time). */
bool rackTapeDeckShown (const rack::Module& module);
juce::Rectangle<int> rackTapeDeckBounds (const rack::Module& module);
double rackTapeDeckSpeed (const rack::Module& module);
void advanceRackTapeDeck (rack::Module& module, double seconds);
/** Tests: user presets saved under this folder instead of Documents/ANDOR. */
void setRackPresetFolderForTest (const juce::File& folder);
/** Tests: a module's knobs' captions, in order (MOVEMENT "INTENSITY SPEED PITCH TONE", ECHO, SPACE). */
juce::String rackMovementCaptions (const rack::Module& module);

} // namespace osp::plugin
