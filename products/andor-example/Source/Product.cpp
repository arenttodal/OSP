// ANDOR Example: what makes it itself (identity, namespaces, macros) and its engine.
// Written by tools/create_andor_instrument.py from shared/andor-shell/template.

#include "DiagnosticEngine.h"
#include "ShellProduct.h"

namespace andor::shell
{

const ProductInfo& productInfo()
{
    static const ProductInfo info = [] {
        ProductInfo p;
        p.name = "ANDOR Example";
        p.slug = "example";
        // The header: ANDOR in the display face's bold, the product in its light, the tagline under them.
        p.identity = { "ANDOR", "/EXAMPLE", "Built from the ANDOR shell" };
        p.presetExtension = ".andorexample";
        p.presetFolder = "ANDOR Example";
        p.stateType = "ANDOR_EXAMPLE";
        // The macro row: ANDOR OSP's six until the product binds its own (IDs of the ANDOR
        // schema, AndorParameters.h; names and hints are the product's).
        p.macros = osp::plugin::frame::ospMacroSpecs();
        return p;
    }();
    return info;
}

std::unique_ptr<Engine> createEngine()
{
    // The shell's labelled diagnostic engine: replace it with the product's own (ShellEngine.h).
    // Until then the instrument says DIAGNOSTIC ENGINE in its footer and shows every control
    // the engine does not read disabled.
    return std::make_unique<DiagnosticEngine>();
}

} // namespace andor::shell
