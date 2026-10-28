#pragma once

// The host-facing parameter schema as text (one line per parameter: ID, version hint, kind,
// name, label, range, skew, default, steps, value texts, choices), and its check against a
// golden listing (OSP_UPDATE_GOLDEN=1 rewrites it). ANDOR OSP and every product built from
// the ANDOR Instrument Shell must list the same schema.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace parameterschema
{
    inline juce::String describe (juce::AudioProcessor& processor)
    {
        juce::StringArray lines;
        for (auto* base : processor.getParameters())
        {
            auto* p = dynamic_cast<juce::RangedAudioParameter*> (base);
            REQUIRE (p != nullptr);
            const auto& range = p->getNormalisableRange();
            juce::String kind = "float";
            juce::String extra;
            if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
            {
                kind = "choice";
                extra = " [" + c->choices.joinIntoString ("|") + "]";
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (p) != nullptr)
                kind = "bool";
            else if (dynamic_cast<juce::AudioParameterInt*> (p) != nullptr)
                kind = "int";
            auto text = [p] (float normalised) { return p->getText (normalised, 64); };
            lines.add (p->getParameterID() + " v" + juce::String (p->getVersionHint()) + " " + kind + " \"" + p->getName (128) + "\" label=\""
                       + p->getLabel() + "\" range=" + juce::String (range.start, 4) + ".." + juce::String (range.end, 4) + "/"
                       + juce::String (range.interval, 4) + " skew=" + juce::String (range.skew, 5) + " default="
                       + juce::String (range.convertFrom0to1 (p->getDefaultValue()), 4) + " steps=" + juce::String (p->getNumSteps())
                       + " texts=" + text (0.0f) + "|" + text (0.5f) + "|" + text (1.0f) + extra);
        }
        return lines.joinIntoString ("\n") + "\n";
    }

    /** True when two value texts say the same thing. The texts are probes of each
        parameter's value-to-text at normalised 0, 0.5 and 1. At 0.5 the value is computed
        (range and skew), and its last digits depend on the platform's floating point: Apple
        Silicon contracts multiply-adds, so the midpoint of -100..100 prints "-0.00" there and
        "0.00" on x86, and 1000..20000 Hz with skew prints 5000.0004883 instead of 5000.0.
        So a leading number may differ by rounding (1 part in a million, or the sign of zero).
        Everything else in the text (its unit, its words, its decimals) must match exactly. */
    inline bool sameValueText (const juce::String& a, const juce::String& b)
    {
        if (a == b)
            return true;
        const auto split = [] (const juce::String& t, double& number, juce::juce_wchar& sign, juce::String& rest) {
            int end = 0;
            sign = t[0] == '-' || t[0] == '+' ? t[0] : 0;
            if (t[end] == '-' || t[end] == '+')
                ++end;
            const int digitsStart = end;
            while (juce::CharacterFunctions::isDigit (t[end]) || t[end] == '.')
                ++end;
            if (end == digitsStart)
                return false;
            const auto digits = t.substring (digitsStart, end);
            number = t.substring (0, end).getDoubleValue();
            // The decimals are part of the text, not of the rounding: keep them in `rest`.
            rest = juce::String (digits.contains (".") ? digits.length() - digits.indexOf (".") : 0) + "#" + t.substring (end);
            return true;
        };
        double na = 0.0, nb = 0.0;
        juce::juce_wchar sa = 0, sb = 0;
        juce::String ra, rb;
        if (! split (a, na, sa, ra) || ! split (b, nb, sb, rb) || ra != rb)
            return false;
        // Only zero may change its sign ("-0.00" and "0.00"); "+18" and "18" are different texts.
        if (sa != sb && ! (std::fpclassify (na) == FP_ZERO && std::fpclassify (nb) == FP_ZERO))
            return false;
        return std::abs (na - nb) <= 1.0e-6 * std::max ({ 1.0, std::abs (na), std::abs (nb) });
    }

    /** Two listing lines are the same parameter: every field exact, the value texts as
        sameValueText says. */
    inline bool sameLine (const juce::String& actual, const juce::String& expected)
    {
        if (actual == expected)
            return true;
        const auto at = actual.indexOf (" texts="), et = expected.indexOf (" texts=");
        if (at < 0 || et < 0 || actual.substring (0, at) != expected.substring (0, et))
            return false;
        // The choices (" [a|b|c]") follow the three texts; they are compared exactly.
        auto texts = [] (const juce::String& line, int from, juce::String& choices) {
            auto t = line.substring (from + 7);
            const auto bracket = t.indexOf (" [");
            choices = bracket >= 0 ? t.substring (bracket) : juce::String();
            return juce::StringArray::fromTokens (bracket >= 0 ? t.substring (0, bracket) : t, "|", "");
        };
        juce::String ca, ce;
        const auto ta = texts (actual, at, ca), te = texts (expected, et, ce);
        if (ca != ce || ta.size() != te.size())
            return false;
        for (int i = 0; i < ta.size(); ++i)
            if (! sameValueText (ta[i], te[i]))
                return false;
        return true;
    }

    inline void checkAgainstGoldenFile (const juce::String& listing, const juce::File& golden)
    {
        if (const char* update = std::getenv ("OSP_UPDATE_GOLDEN"); update != nullptr && juce::String (update) == "1")
        {
            golden.getParentDirectory().createDirectory();
            REQUIRE (golden.replaceWithText (listing, false, false, "\n"));
        }
        {
            INFO ("missing golden; run with OSP_UPDATE_GOLDEN=1 to create: " << golden.getFullPathName());
            REQUIRE (golden.existsAsFile());
        }
        const auto expected = juce::StringArray::fromLines (golden.loadFileAsString());
        const auto actual = juce::StringArray::fromLines (listing);
        CHECK (actual.size() == expected.size());
        for (int i = 0; i < std::min (actual.size(), expected.size()); ++i)
        {
            INFO ("parameter " << i << "\n  actual:   " << actual[i] << "\n  expected: " << expected[i]);
            CHECK (sameLine (actual[i], expected[i]));
        }
    }

    inline void checkAgainstGolden (const juce::String& listing, const juce::String& goldenName)
    {
        checkAgainstGoldenFile (listing, juce::File (OSP_SOURCE_DIR).getChildFile ("tests/plugin/golden").getChildFile (goldenName));
    }
}
