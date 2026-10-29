#pragma once

#include "InstrumentFrame.h"
#include "ShellEngine.h"

#include <array>
#include <functional>
#include <memory>
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
};

/** Defined by the product (Product.cpp). */
const ProductInfo& productInfo();
/** Defined by the product (Product.cpp): a new instance of its engine. */
std::unique_ptr<Engine> createEngine();

} // namespace andor::shell
