#pragma once

#include "engine/InstrumentEngine.h"

#include <juce_core/juce_core.h>

#include <cmath>

namespace osp::plugin::format
{

/** Layer TUNE: "0 st", "+7 st", "-12 st", "+7.25 st". */
inline juce::String semitones (double v)
{
    const double rounded = std::round (v * 100.0) / 100.0;
    const bool whole = std::abs (rounded - std::round (rounded)) < 0.005;
    const auto number = whole ? juce::String (static_cast<int> (std::lround (rounded))) : juce::String (rounded, 2);
    return (rounded > 0.0 ? "+" : "") + number + " st";
}

/** Layer PAN (-100..100): "C", "L 40", "R 100". */
inline juce::String pan (double v)
{
    const int i = static_cast<int> (std::lround (v));
    if (i == 0)
        return "C";
    return (i < 0 ? "L " : "R ") + juce::String (std::abs (i));
}

/** Typed PAN: "L 40" / "R 20" / "C" / plain numbers (negative = left). */
inline float panFromText (const juce::String& text)
{
    const auto t = text.trim().toUpperCase();
    if (t.startsWith ("C"))
        return 0.0f;
    const float amount = t.retainCharacters ("0123456789.").getFloatValue();
    if (t.startsWith ("L"))
        return -amount;
    if (t.startsWith ("R"))
        return amount;
    return t.getFloatValue();
}

/** Layer LEVEL: "-3.0 dB", "+2.5 dB", and silence at the bottom of the range. */
inline juce::String levelDb (double v)
{
    if (v <= LayerSettings::minLevelDb + 0.05)
        return juce::String::fromUTF8 ("-\xe2\x88\x9e dB");
    return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " dB";
}

} // namespace osp::plugin::format
