#pragma once

#include "model/ArpPatternModel.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace osp::arppattern
{

/**
    The `.andorpattern` codec in pure C++ (no JUCE): the one place that decides what a pattern
    document means as JSON (docs/analysis-schema.md, "Custom ARP pattern").

    { "schemaVersion": 1, "kind": "andor.arp-pattern", "id", "name", "description", "factory",
      "pattern": { "mapping": "contour" | "degrees", "maxRank", "events": [ [rank|-1, ticks, velocity?] ] },
      "interpretation": { "timing": "rhythm" | "steps", "quantumTicks", "stepQuarters",
                          "lengthQuarters", "chord": "highest" | "lowest", "mapping" },
      "capture": { "bpm", "notes": [ [onsetQuarters, durationQuarters, pitch, velocity] ] } }

    Every product reads and writes patterns through it: ANDOR OSP and the ANDOR shell through the
    thin JUCE adapter in io/ArpPatternJson (which only translates juce::var to and from JsonValue,
    so their text stays JUCE's), and ANDOR ARP's Max external and plugin directly. A newer schema,
    another kind or an invalid pattern is refused, never misread; unknown fields are ignored.

    Message thread only: it allocates.
*/

/** A small JSON value: enough for pattern documents (and their round trip through juce::var). */
struct JsonValue
{
    enum class Type
    {
        null,
        boolean,
        number,
        string,
        array,
        object
    };

    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    bool integer = false;   ///< written without a fraction (JSON has one number type; JUCE keeps two)
    std::string text;
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;   ///< in order; a repeated key: the last wins

    static JsonValue makeBool (bool v);
    static JsonValue makeInt (long long v);
    static JsonValue makeDouble (double v);
    static JsonValue makeString (std::string v);
    static JsonValue makeArray();
    static JsonValue makeObject();

    bool isObject() const noexcept { return type == Type::object; }
    bool isArray() const noexcept { return type == Type::array; }
    bool isNumber() const noexcept { return type == Type::number; }
    bool isString() const noexcept { return type == Type::string; }
    bool isBool() const noexcept { return type == Type::boolean; }

    /** A member (a null value when absent or when this is not an object). */
    const JsonValue& operator[] (std::string_view key) const noexcept;
    void set (std::string key, JsonValue value);
    void append (JsonValue value) { items.push_back (std::move (value)); }

    /** As juce::var converts: numbers truncate toward zero (saturating), booleans are 0 / 1,
        strings read their leading number, anything else 0. */
    int toInt() const noexcept;
    double toDouble() const noexcept;
};

/** Strict RFC 8259 JSON (UTF-8, \u escapes with surrogate pairs); nothing after the value. */
std::optional<JsonValue> parseJson (std::string_view text, std::string& error);
/** Multi-line, two-space indent; arrays of numbers on one line; numbers with at most 6 decimals. */
std::string writeJson (const JsonValue& value);

inline constexpr const char* documentKind = "andor.arp-pattern";

JsonValue documentToJson (const Document& document);
std::optional<Document> documentFromJson (const JsonValue& json, std::string& error);

std::string documentToText (const Document& document);
std::optional<Document> documentFromText (std::string_view text, std::string& error);

} // namespace osp::arppattern
