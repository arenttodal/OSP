#pragma once

#include "engine/KeyDna.h"
#include "engine/Modulation.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <optional>

namespace osp::plugin::keyDnaState
{

/**
    KEY DNA in a saved state: the `KeyDna` child (schema 1), as ANDOR OSP writes it and every
    product with KEY DNA reads it (one codec, so a profile reopens identically everywhere).

    Per layer: its switch, individuality and seed; per parameter (position .. pan, by id) its
    curve depth, variation, compiled curve (omitted when flat) and the editor's drawn points.
    Numbers are round-trip exact. AMOUNT is a host parameter, not part of it.
*/
constexpr int layerCount = 3;
constexpr int schemaVersion = 1;

using Curves = std::array<std::array<std::optional<mod::Curve>, keydna::paramCount>, layerCount>;

juce::ValueTree toTree (const std::array<keydna::Settings, layerCount>& layers, const Curves& curves);
/** Absent, or from a schema this build does not know: every layer off (never a guess). */
std::array<keydna::Settings, layerCount> fromTree (const juce::ValueTree& tree);
/** The editor's drawn points (none where a curve was never drawn). */
Curves curvesFromTree (const juce::ValueTree& tree);

/** The curve the editor shows for a parameter: its drawn points, else (a curve saved
    without points) nine points along its table; `fallback` when the table is flat. */
mod::Curve editorCurve (const keydna::Settings& settings, const std::optional<mod::Curve>& drawn, keydna::Param param, const mod::Curve& fallback);

} // namespace osp::plugin::keyDnaState
