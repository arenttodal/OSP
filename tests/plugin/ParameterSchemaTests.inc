// The host-facing parameter schema (IDs, version hints, names, ranges, defaults, choices,
// value texts) is frozen: automation and sessions depend on it. The shared ANDOR interface
// and the Instrument Shell build the same schema, so it is pinned here against a golden
// listing. Intentional additions: OSP_UPDATE_GOLDEN=1 osp_plugin_tests "[parameter-schema]".
// Included at the end of PluginProcessorTests.cpp.

#include "../support/ParameterSchema.h"

TEST_CASE ("plugin: ANDOR OSP's parameter schema is unchanged (IDs, order, ranges, defaults, texts)", "[plugin][parameter-schema]")
{
    OspAudioProcessor p;
    parameterschema::checkAgainstGolden (parameterschema::describe (p), "osp-parameter-schema.txt");
}

TEST_CASE ("plugin: the schema check allows only floating-point rounding in value texts", "[plugin][parameter-schema]")
{
    using parameterschema::sameLine;
    using parameterschema::sameValueText;
    // Seen on Apple Silicon against the x86 listing: the sign of zero, the last digits.
    CHECK (sameValueText ("-0.00", "0.00"));
    CHECK (sameValueText ("-0.0 dB", "0.0 dB"));
    CHECK (sameValueText ("5000.0004883", "5000.0000000"));
    // A different value, unit, wording or number of decimals is a change.
    CHECK_FALSE (sameValueText ("0.01", "0.00"));
    CHECK_FALSE (sameValueText ("5000.1", "5000.0"));
    CHECK_FALSE (sameValueText ("0.0 dB", "0.0 Hz"));
    CHECK_FALSE (sameValueText ("Off", "On"));
    CHECK_FALSE (sameValueText ("50 %", "50%"));
    CHECK_FALSE (sameValueText ("0.0", "0.00"));
    CHECK_FALSE (sameValueText ("+18.0 dB", "18.0 dB"));
    CHECK_FALSE (sameValueText ("-5.0", "5.0"));
    const juce::String line = "x v1 float \"X\" label=\"dB\" range=-18.0000..18.0000/0.0100 skew=1.00000 default=0.0000 steps=2 texts=";
    CHECK (sameLine (line + "-18.0 dB|-0.0 dB|+18.0 dB", line + "-18.0 dB|0.0 dB|+18.0 dB"));
    CHECK_FALSE (sameLine (line + "a|b|c [p|q]", line + "a|b|c [p|r]"));
    CHECK_FALSE (sameLine (line.replace ("v1", "v2") + "a|b|c", line + "a|b|c"));
}
