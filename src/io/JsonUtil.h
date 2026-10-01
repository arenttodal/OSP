#pragma once

#include <juce_core/juce_core.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osp::json
{

/** Thin helpers over juce::var / juce::JSON so call sites stay readable. */

juce::var object();
juce::var array();

void set (juce::var& obj, const char* key, const juce::var& value);

/** Rounds to a fixed number of decimals (keeps reports compact and diff-friendly). */
double round (double value, int decimals);
juce::var number (double value, int decimals = 4);
juce::var str (const std::string& s);
juce::var floatArray (const std::vector<float>& values, int decimals);
juce::var stringArray (const std::vector<std::string>& values);

std::string toString (const juce::var& value);
bool writeFile (const std::filesystem::path& path, const juce::var& value, std::string& error);

std::optional<juce::var> parse (const std::string& text, std::string& error);
std::optional<juce::var> readFile (const std::filesystem::path& path, std::string& error);

// Typed getters with defaults (missing or wrong-typed fields return the default).
double getDouble (const juce::var& obj, const char* key, double fallback);
int getInt (const juce::var& obj, const char* key, int fallback);
bool getBool (const juce::var& obj, const char* key, bool fallback);
std::string getString (const juce::var& obj, const char* key, const std::string& fallback = {});
std::vector<float> getFloatArray (const juce::var& obj, const char* key);
bool has (const juce::var& obj, const char* key);

} // namespace osp::json
