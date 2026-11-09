#pragma once

#include "WellOverlay.h"
#include "CardWellEditor.h"
#include "Design.h"
#include "EffectRack.h"
#include "InstrumentFrame.h"
#include "ShellEngine.h"

#include <array>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace andor::shell
{

/**
    What makes a product built from the ANDOR Instrument Shell itself: its identity and
    namespaces (the plugin's own IDs, bundle and codes are set by andor_add_instrument in its
    CMakeLists.txt), the six macros it shows and its engine. A product defines productInfo()
    and createEngine() in its Product.cpp (tools/create_andor_instrument.py writes one).
*/
struct ProductInfo
{
    juce::String name;                       ///< "ANDOR Example": host menus, window title
    juce::String slug;                       ///< "example": file and state namespaces
    osp::plugin::frame::Identity identity;   ///< the header: ANDOR + "/EXAMPLE" + tagline
    /** The preset files' extension (".andorexample") and folder: Documents/<presetFolder>/Presets. */
    juce::String presetExtension;
    juce::String presetFolder;
    /** The saved state's root type: never shared with another product (OSP's is "OSP"). */
    juce::String stateType;
    /** The macro row (left to right in data order; the fifth and sixth swap places on screen
        as in ANDOR OSP). Each binds one 0..100 % parameter of the ANDOR schema. */
    std::array<osp::plugin::frame::MacroSpec, osp::plugin::frame::macroCount> macros;
    /** A macro's popover (its settings, built from the shared popover parts), or nullptr:
        without one the macro's name opens nothing and does not invite a click. */
    std::function<std::unique_ptr<osp::plugin::MiniPanel> (int macro, osp::plugin::InstrumentHost&)> macroPopover;
    /** The product's own parameters, appended after the ANDOR schema (which stays exactly
        ANDOR OSP's): IDs in the product's own namespace ("analog.*"), never an ANDOR ID.
        Null: the schema alone. Their listing is pinned by the product's own golden file
        (andor_add_instrument_tests ... GOLDEN). */
    std::function<void (juce::AudioProcessorValueTreeState::ParameterLayout&)> addParameters;
    /** Writes the product's factory presets into `folder` (Documents/<product>/Presets/Factory)
        when they are missing or from another version; never touches the musician's own.
        Called once per processor, on the message thread. Null: none. */
    std::function<void (osp::plugin::InstrumentHost&, const juce::File& folder)> installFactoryPresets;
    /** The version of the product's own state (saved with every session and preset as
        "productVersion"; a state without one is version 1). Raise it when a parameter's
        meaning moves elsewhere, and say in migrateState how an older state is brought up to date. */
    int stateVersion = 1;
    /** Called after an older state's parameters are restored (`fromVersion` < stateVersion):
        moves its settings to where this version reads them, so it sounds as it did. Message
        thread. Null: nothing to migrate. */
    std::function<void (osp::plugin::InstrumentHost&, int fromVersion)> migrateState;
    /** The product's own controls a modulation route can reach, in order (they follow ANDOR
        OSP's destinations in the routes' destination list, so their order is part of the
        saved state: append only). Each names its parameter ID; a source dragged onto that
        control assigns it. Empty: ANDOR OSP's destinations only. */
    std::vector<osp::mod::ProductDest> modulationDestinations;
    /** A generator's card: the knob in START's place as one of the product's controls (its
        parameter ID and name), whose name opens `popover` (null: none). Empty ID: START. */
    struct CardControl
    {
        juce::String parameterId, caption;
        std::function<std::unique_ptr<osp::plugin::MiniPanel> (osp::plugin::InstrumentHost&)> popover;
    };
    CardControl generatorStartControl;
    /** A generator's card heading (its name beside M and S) as a door: a click, Return or
        Space on the name opens this popover below it, and closes it again (null: the name is
        plain text, as on every ANDOR OSP card). Generator products only. */
    std::function<std::unique_ptr<osp::plugin::MiniPanel> (osp::plugin::InstrumentHost&)> generatorHeaderPopover;
    /** A generator's in-card editor (opt-in): the card's heading (its name beside M and S)
        toggles this editor in the waveform's place, the waveform's dark well, instead of a
        popover. The EQ takes the well while open and gives it back if the editor is pinned.
        Takes precedence over generatorHeaderPopover. Null: no in-card editor. */
    std::function<std::unique_ptr<osp::plugin::CardWellEditor> (osp::plugin::InstrumentHost&)> generatorWellEditor;
    /** A generator product that also plays one recording (opt-in): layer B takes a dropped or
        chosen file through ANDOR OSP's loader, sample store and card, beside the generator's
        card, and is saved and recalled with the session. This is its name (menus, notices).
        Empty: a generator plays no recordings (every other product unchanged). */
    juce::String hybridSampleName;
    /** A hybrid product's empty recording is a compact dock beside the generator's card
        (about a fifth of the source area: ADD SAMPLE, drop a file or click to browse) rather
        than nothing until a file is dragged in; loading a recording opens its card (the two
        cards then share the area equally) and removing it brings the dock back. Interface
        only. Off: the shell's layout as before. */
    bool hybridSampleDock = false;
    /** A hybrid product's recording card, while it shows the engine's own source
        (Engine::engineSourceName), makes its heading the door to this popover (its sounds, say).
        Unset: the heading stays plain text. Interface only. */
    std::function<std::unique_ptr<osp::plugin::MiniPanel> (osp::plugin::InstrumentHost&)> hybridHeaderPopover;
    /** A second way to fill the hybrid layer (an import), offered quietly on its dock and in its
        card's menu. Unset (no label): neither shows. */
    struct HybridAction
    {
        juce::String label;
        std::function<void (osp::plugin::InstrumentHost&)> run;
    } hybridAction;
    /** Three engines (opt-in, a generator product with a hybrid layer): the three cards always
        shown at equal widths, storage layers in `order` (shown first, second, third: their
        badges, M / S and EQ say A, B, C in that order), a generator's further layers named
        by Engine::layerGeneratorName. The hybrid layer, while empty, is its ADD SAMPLE
        invitation in its own column. The header MIX stays the generator against the hybrid.
        Off: the shell's layout as before. */
    struct ThreeEngines
    {
        bool enabled = false;
        std::array<int, 3> order { 0, 1, 2 };
    } threeEngines;
    /** The per-card arpeggiator (opt-in, a three-engine product whose engine plays card
        lanes: Engine::setCardRouting). The shell adds its parameters (ids::arpLink, arpCard, arpCardSetting): which
        cards the arpeggiator plays (the others hold the keys), LINK, and each further card's
        own PATTERN / RATE / OCTAVES while unlinked; the ARP panel shows them. Off: one
        arpeggiator for the instrument, as in ANDOR OSP. */
    bool cardArpeggiator = false;
    /** The effects rack (osp::plugin::rack, opt-in): the modules a key beside their macro's
        name expands between the macros and the arpeggiator (MOVEMENT, ECHO, SPACE). Empty:
        no keys and no rack, as in ANDOR OSP. */
    std::vector<osp::plugin::rack::Kind> rackModules;
    /** Racks the product builds itself (opt-in; ANDOR ANALOG's FILTER and MORPH): the macro
        (its parameter ID) whose name opens it as the shared ones do, its kind, and how it is
        built (osp::plugin::rack::ParameterRack). An empty macro: no macro opens it (the product's
        own view does: ShellEditor::setRackExpanded). Empty: none, as in ANDOR OSP. */
    struct ProductRack
    {
        juce::String macroParameterId;
        osp::plugin::rack::Kind kind;
        std::function<std::unique_ptr<osp::plugin::rack::Module> (osp::plugin::InstrumentHost&)> create;
    };
    std::vector<ProductRack> productRacks;
    /** Another view of the sources' area (opt-in; ANDOR ANALOG's FX ROUTING): a small borderless
        icon (a branching path) beside the header's SAVE toggles it; while it is shown it takes
        the cards' place - the header, macros, racks, modulation and keyboard stay where they are.
        It is interface only (showing it never changes the sound). Unset: no icon, as in ANDOR OSP. */
    struct SourceAreaView
    {
        juce::String tooltip;   ///< "FX Routing"
        std::function<std::unique_ptr<juce::Component> (osp::plugin::InstrumentHost&)> create;
    };
    SourceAreaView sourceAreaView;
    /** The shared rack modules' hardware faceplates (opt-in; ANDOR ANALOG's), by rack::Kind;
        empty: the approved racks as they are (ANDOR OSP). Interface only. */
    std::map<osp::plugin::rack::Kind, osp::plugin::rack::Faceplate> rackFaceplates;
    /** V3 (opt-in): the rack's module is its whole face (rack::Host::setFullBleed): no panel
        around the faceplates, a borderless X to close. Off: the approved rack (ANDOR OSP). */
    bool fullBleedRacks = false;
    /** MOVEMENT's lanes (InstrumentHost::movementLane), lane 0 first: a product with a second
        lane lists both (the shell keeps the second lane's SHAPER CUSTOM pattern with the
        session and gives it to the engine: Engine::setLaneShaperPattern). Empty: one lane,
        MOVEMENT itself, as in ANDOR OSP. */
    std::vector<osp::plugin::InstrumentHost::MovementLane> movementLanes;
    /** A card's knob slot, as a product sets it (cardControls): its own control (an empty
        ID), a product control, or hidden. */
    struct CardKnob
    {
        juce::String parameterId, caption;
        bool hidden = false;
    };
    /** Per-card knob overrides (opt-in): for a storage layer, its five slots (START TUNE PAN
        LEVEL REIMAGINED). Re-read by the editor on its timer (they may follow the product's
        state, e.g. the hybrid's type). Unset: the cards' own controls. */
    std::function<std::array<CardKnob, 5> (int layer, osp::plugin::InstrumentHost&)> cardControls;
    /** Whether a card shows its four modifier keys (LINK REVERSE LOOP FOLLOW): a product hides
        them where they mean nothing (an oscillator). Unset: shown, as in ANDOR OSP. */
    std::function<bool (int layer, osp::plugin::InstrumentHost&)> cardModifiersShown;
    /** The modifiers as a compact row of icon keys in the dark well (top-left) instead of the
        keys under the knobs (EngineCard::setModifiersInWell), and which of them apply to a
        layer's source now (bits 0 LINK, 1 REVERSE, 2 LOOP, 3 FOLLOW; null: all). Off: ANDOR
        OSP's keys. */
    bool cardModifiersInWell = false;
    /** The modulation panel's six tabs at fixed places whatever is selected (a reserved place
        at the right for the route count; the larger curve editor's key inside the display:
        ModulationPanel::setStableTabs). Off: ANDOR OSP's row. */
    bool stableModulationTabs = false;
    std::function<int (int layer, osp::plugin::InstrumentHost&)> cardModifiersApplicable;
    /** The header MIX's end labels (three engines: the generator side against the hybrid,
        e.g. "A+B" and "C"). Empty: the layers' letters. */
    std::array<juce::String, 2> mixEndLabels;
    /** The header's MIX (off: none - the cards' own levels balance them; the preset bar takes
        its room, SAVE and the source view's key after it). On: ANDOR OSP's header. */
    bool headerMix = true;
    /** The ARPEGGIATOR's collapse as the racks' borderless X (off: the round chevron key). */
    bool crossCloseKeys = false;
    /** Controls in a card's well, revealed by the pointer (opt-in): one per storage layer
        (null: none; see osp::plugin::WellOverlay). Remade when wellOverlayKey changes (a
        model, a source type), so its controls always match what plays. */
    std::function<std::unique_ptr<osp::plugin::WellOverlay> (int layer, osp::plugin::InstrumentHost&)> wellOverlay;
    std::function<juce::String (int layer, osp::plugin::InstrumentHost&)> wellOverlayKey;
    /** The AMP envelope's note (opt-in): a short word drawn on its picture while the engine
        does not follow the envelope as drawn (e.g. "GATE"), or empty. Polled by the editor.
        Unset: the picture as before. Interface only. */
    std::function<juce::String (osp::plugin::InstrumentHost&)> ampEnvelopeNote;
    /** The musician starts editing the AMP envelope (one of its knobs or handles, or a host's
        touch on attack / decay / sustainLevel / release): e.g. the product gives the amplitude
        back to it. Message thread. Unset: nothing listens. */
    std::function<void (osp::plugin::InstrumentHost&)> ampEnvelopeTouched;
    /** A generator's card names its first source mode this (its menu and selector) instead
        of ANDOR OSP's "One Shot" (a synthesizer's "Standard"). Empty: "One Shot", as before. */
    juce::String generatorModeName;
    /** The product's own materials (opt-in): its finish (the accent family, ceramic tones)
        and rendered pieces of its own (its housing's wood, its badge) in place of ANDOR OSP's,
        for every instance of the product. Unset: ANDOR OSP's, exactly. Interface only. */
    std::optional<osp::plugin::design::Materials> materials;
};

/** Defined by the product (Product.cpp). */
const ProductInfo& productInfo();
/** Defined by the product (Product.cpp): a new instance of its engine. */
std::unique_ptr<Engine> createEngine();

} // namespace andor::shell
