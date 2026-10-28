#pragma once

#include "InstrumentFrame.h"
#include "ShellEngine.h"

#include <array>
#include <functional>
#include <memory>

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
};

/** Defined by the product (Product.cpp). */
const ProductInfo& productInfo();
/** Defined by the product (Product.cpp): a new instance of its engine. */
std::unique_ptr<Engine> createEngine();

} // namespace andor::shell
