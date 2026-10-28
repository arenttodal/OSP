#pragma once

// The host-facing parameter schema as text (one line per parameter: ID, version hint, kind,
// name, label, range, skew, default, steps, value texts, choices), and its check against a
// golden listing (OSP_UPDATE_GOLDEN=1 rewrites it). ANDOR OSP and every product built from
// the ANDOR Instrument Shell must list the same schema.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
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

    inline void checkAgainstGolden (const juce::String& listing, const juce::String& goldenName)
    {
        const auto golden = juce::File (OSP_SOURCE_DIR).getChildFile ("tests/plugin/golden").getChildFile (goldenName);
        if (const char* update = std::getenv ("OSP_UPDATE_GOLDEN"); update != nullptr && juce::String (update) == "1")
        {
            golden.getParentDirectory().createDirectory();
            REQUIRE (golden.replaceWithText (listing, false, false, "\n"));
        }
        INFO ("missing golden; run with OSP_UPDATE_GOLDEN=1 to create: " << golden.getFullPathName());
        REQUIRE (golden.existsAsFile());
        const auto expected = juce::StringArray::fromLines (golden.loadFileAsString());
        const auto actual = juce::StringArray::fromLines (listing);
        CHECK (actual.size() == expected.size());
        for (int i = 0; i < std::min (actual.size(), expected.size()); ++i)
        {
            INFO ("parameter " << i);
            CHECK (actual[i] == expected[i]);
        }
    }
}

