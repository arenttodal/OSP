#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>

namespace osp::plugin::performance
{

/**
    PERFORMANCE MODE R1 (docs: ANDOR ANALOG docs/specs/PERFORMANCE_MODE_R1.md): the shared
    presentation model - which view is shown (Edit / Performance) and which macros the
    performance strip shows, in what order. A view preference only: never a parameter, never
    part of a preset, never read or written on the audio thread.

    A macro's identity is stable: a core macro is its parameter id (the product's own, e.g.
    "drive", "analog.morph.position"); a USER macro is "user:N" (its slot, 0-based) - never its
    display name, so renaming it keeps it selected.

    Where it lives: the instance's session (each plugin in a project keeps its own), else the
    product's Interface.xml (the musician's last choice on this computer), else the default
    (Edit Mode; the six core macros and up to two assigned USER macros).
*/
constexpr int maxMacros = 8;
constexpr int schemaVersion = 1;

/** "user:N" for USER slot N. */
juce::String userId (int slot);
/** USER slot of an id, or -1 (a core macro, or not a macro). */
int userSlotOf (const juce::String& id);

/** What a product offers: its core macros (parameter ids, display order) and USER slots. */
struct Catalog
{
    juce::StringArray core;
    int userSlots = 0;
    bool knows (const juce::String& id) const;
};

/** Whether USER slot N controls something now (one or more active assignments). */
using AssignedFn = std::function<bool (int slot)>;

struct State
{
    bool performance = false;    ///< the view shown
    bool customised = false;     ///< the musician chose the macros (else: the default, which follows assignments)
    juce::StringArray macros;    ///< the chosen ids, in order (customised only; may name an unassigned USER slot)

    bool operator== (const State& o) const { return performance == o.performance && customised == o.customised && macros == o.macros; }
    bool operator!= (const State& o) const { return ! (*this == o); }

    /** <Performance version="1" mode="performance|edit" customised="0|1"><Macro id=".."/>...</Performance> */
    std::unique_ptr<juce::XmlElement> toXml() const;
    /** From that element (any tag check is the caller's); null or unreadable: nothing. Unknown
        attributes and newer versions are read as far as understood. */
    static std::optional<State> fromXml (const juce::XmlElement* xml);
};

/** The default selection: every core macro, then up to (8 - core) assigned USER macros, in
    slot order. */
juce::StringArray defaultSelection (const Catalog& catalog, const AssignedFn& assigned);
/** A stored selection made safe: known ids only, each once, at most eight. Unassigned USER
    slots are kept (they come back when assigned). Empty afterwards: the default. */
juce::StringArray sanitise (const juce::StringArray& ids, const Catalog& catalog, const AssignedFn& assigned);
/** The selection in effect (customised: sanitised; otherwise the default). */
juce::StringArray selection (const State& state, const Catalog& catalog, const AssignedFn& assigned);
/** What the strip shows: the selection without unassigned USER macros (never an inert knob).
    Never empty: if every chosen macro is an unassigned USER slot, the first core macro. */
juce::StringArray visible (const State& state, const Catalog& catalog, const AssignedFn& assigned);

/** Interface.xml (a product's view preferences on this computer): its <Performance> entry,
    if any; and writing it back, every other entry kept. Message thread. */
std::optional<State> readPreference (const juce::File& interfaceFile);
void writePreference (const juce::File& interfaceFile, const State& state);

/** Knob centres (x) for `count` macros spread evenly across `width` (from `left`): the strip's
    one layout rule, shared by every editor. */
std::vector<float> stripCentres (float left, float width, int count);

} // namespace osp::plugin::performance
