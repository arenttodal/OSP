#include "model/ArpPatternCodec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace osp::arppattern
{

// ---------------------------------------------------------------------------------------------
// JsonValue
// ---------------------------------------------------------------------------------------------

JsonValue JsonValue::makeBool (bool v)
{
    JsonValue j;
    j.type = Type::boolean;
    j.boolean = v;
    return j;
}

JsonValue JsonValue::makeInt (long long v)
{
    JsonValue j;
    j.type = Type::number;
    j.number = static_cast<double> (v);
    j.integer = true;
    return j;
}

JsonValue JsonValue::makeDouble (double v)
{
    JsonValue j;
    j.type = Type::number;
    j.number = v;
    return j;
}

JsonValue JsonValue::makeString (std::string v)
{
    JsonValue j;
    j.type = Type::string;
    j.text = std::move (v);
    return j;
}

JsonValue JsonValue::makeArray()
{
    JsonValue j;
    j.type = Type::array;
    return j;
}

JsonValue JsonValue::makeObject()
{
    JsonValue j;
    j.type = Type::object;
    return j;
}

const JsonValue& JsonValue::operator[] (std::string_view key) const noexcept
{
    static const JsonValue none;
    if (type != Type::object)
        return none;
    // A repeated key: the last one wins (as juce::DynamicObject keeps it).
    for (auto it = members.rbegin(); it != members.rend(); ++it)
        if (it->first == key)
            return it->second;
    return none;
}

void JsonValue::set (std::string key, JsonValue value)
{
    for (auto& m : members)
        if (m.first == key)
        {
            m.second = std::move (value);
            return;
        }
    members.emplace_back (std::move (key), std::move (value));
}

namespace
{
    double leadingNumber (const std::string& s)
    {
        // juce::String::getDoubleValue: leading whitespace, then the longest number prefix.
        std::size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
        std::istringstream in (s.substr (i));
        in.imbue (std::locale::classic());
        double v = 0.0;
        in >> v;
        return in.fail() ? 0.0 : v;
    }

    int saturate (double v) noexcept
    {
        if (! std::isfinite (v))
            return 0;
        const double t = std::trunc (v);
        if (t >= static_cast<double> (std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        if (t <= static_cast<double> (std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        return static_cast<int> (t);
    }
}

int JsonValue::toInt() const noexcept
{
    switch (type)
    {
        case Type::number:  return saturate (number);
        case Type::boolean: return boolean ? 1 : 0;
        case Type::string:  return saturate (leadingNumber (text));
        default:            return 0;
    }
}

double JsonValue::toDouble() const noexcept
{
    switch (type)
    {
        case Type::number:  return number;
        case Type::boolean: return boolean ? 1.0 : 0.0;
        case Type::string:  return leadingNumber (text);
        default:            return 0.0;
    }
}

// ---------------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------------

namespace
{
    class Parser
    {
    public:
        explicit Parser (std::string_view t) : text (t) {}

        std::optional<JsonValue> parseDocument (std::string& error)
        {
            JsonValue v;
            if (! parseValue (v, 0))
            {
                error = message;
                return std::nullopt;
            }
            skipSpace();
            if (pos != text.size())
            {
                error = fail ("text after the JSON value");
                return std::nullopt;
            }
            return v;
        }

    private:
        static constexpr int maxDepth = 64;   // a pattern document is 3 deep; refuse runaway nesting

        std::string_view text;
        std::size_t pos = 0;
        std::string message;

        std::string fail (const char* what)
        {
            message = std::string ("JSON: ") + what + " at byte " + std::to_string (pos);
            return message;
        }

        void skipSpace() noexcept
        {
            while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r'))
                ++pos;
        }

        bool literal (std::string_view word)
        {
            if (text.substr (pos, word.size()) != word)
                return false;
            pos += word.size();
            return true;
        }

        bool parseValue (JsonValue& out, int depth)
        {
            if (depth > maxDepth)
            {
                fail ("nesting too deep");
                return false;
            }
            skipSpace();
            if (pos >= text.size())
            {
                fail ("unexpected end");
                return false;
            }
            const char c = text[pos];
            if (c == '{')
                return parseObject (out, depth);
            if (c == '[')
                return parseArray (out, depth);
            if (c == '"')
            {
                out.type = JsonValue::Type::string;
                return parseString (out.text);
            }
            if (c == '-' || (c >= '0' && c <= '9'))
                return parseNumber (out);
            if (literal ("true"))
            {
                out = JsonValue::makeBool (true);
                return true;
            }
            if (literal ("false"))
            {
                out = JsonValue::makeBool (false);
                return true;
            }
            if (literal ("null"))
            {
                out = JsonValue {};
                return true;
            }
            fail ("unexpected character");
            return false;
        }

        bool parseObject (JsonValue& out, int depth)
        {
            out = JsonValue::makeObject();
            ++pos;   // {
            skipSpace();
            if (pos < text.size() && text[pos] == '}')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                skipSpace();
                if (pos >= text.size() || text[pos] != '"')
                {
                    fail ("expected a member name");
                    return false;
                }
                std::string key;
                if (! parseString (key))
                    return false;
                skipSpace();
                if (pos >= text.size() || text[pos] != ':')
                {
                    fail ("expected ':'");
                    return false;
                }
                ++pos;
                JsonValue v;
                if (! parseValue (v, depth + 1))
                    return false;
                out.members.emplace_back (std::move (key), std::move (v));
                skipSpace();
                if (pos < text.size() && text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == '}')
                {
                    ++pos;
                    return true;
                }
                fail ("expected ',' or '}'");
                return false;
            }
        }

        bool parseArray (JsonValue& out, int depth)
        {
            out = JsonValue::makeArray();
            ++pos;   // [
            skipSpace();
            if (pos < text.size() && text[pos] == ']')
            {
                ++pos;
                return true;
            }
            for (;;)
            {
                JsonValue v;
                if (! parseValue (v, depth + 1))
                    return false;
                out.items.push_back (std::move (v));
                skipSpace();
                if (pos < text.size() && text[pos] == ',')
                {
                    ++pos;
                    continue;
                }
                if (pos < text.size() && text[pos] == ']')
                {
                    ++pos;
                    return true;
                }
                fail ("expected ',' or ']'");
                return false;
            }
        }

        static void appendUtf8 (std::string& s, std::uint32_t cp)
        {
            if (cp < 0x80)
                s += static_cast<char> (cp);
            else if (cp < 0x800)
            {
                s += static_cast<char> (0xC0 | (cp >> 6));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                s += static_cast<char> (0xE0 | (cp >> 12));
                s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
            else
            {
                s += static_cast<char> (0xF0 | (cp >> 18));
                s += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
                s += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                s += static_cast<char> (0x80 | (cp & 0x3F));
            }
        }

        bool hex4 (std::uint32_t& out)
        {
            if (pos + 4 > text.size())
            {
                fail ("short \\u escape");
                return false;
            }
            out = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char h = text[pos++];
                out <<= 4;
                if (h >= '0' && h <= '9')
                    out |= static_cast<std::uint32_t> (h - '0');
                else if (h >= 'a' && h <= 'f')
                    out |= static_cast<std::uint32_t> (h - 'a' + 10);
                else if (h >= 'A' && h <= 'F')
                    out |= static_cast<std::uint32_t> (h - 'A' + 10);
                else
                {
                    fail ("bad \\u escape");
                    return false;
                }
            }
            return true;
        }

        bool parseString (std::string& out)
        {
            ++pos;   // "
            out.clear();
            while (pos < text.size())
            {
                const char c = text[pos++];
                if (c == '"')
                    return true;
                if (static_cast<unsigned char> (c) < 0x20)
                {
                    fail ("control character in a string");
                    return false;
                }
                if (c != '\\')
                {
                    out += c;   // UTF-8 passes through as bytes
                    continue;
                }
                if (pos >= text.size())
                    break;
                const char e = text[pos++];
                switch (e)
                {
                    case '"':  out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':
                    {
                        std::uint32_t cp = 0;
                        if (! hex4 (cp))
                            return false;
                        if (cp >= 0xD800 && cp <= 0xDBFF && text.substr (pos, 2) == "\\u")
                        {
                            const auto save = pos;
                            pos += 2;
                            std::uint32_t low = 0;
                            if (! hex4 (low))
                                return false;
                            if (low >= 0xDC00 && low <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            else
                                pos = save;   // a lone high surrogate: kept as U+FFFD below
                        }
                        if (cp >= 0xD800 && cp <= 0xDFFF)
                            cp = 0xFFFD;
                        appendUtf8 (out, cp);
                        break;
                    }
                    default:
                        fail ("bad escape");
                        return false;
                }
            }
            fail ("unterminated string");
            return false;
        }

        bool parseNumber (JsonValue& out)
        {
            const auto start = pos;
            bool fraction = false;
            if (text[pos] == '-')
                ++pos;
            auto digits = [this] {
                const auto from = pos;
                while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9')
                    ++pos;
                return pos > from;
            };
            if (! digits())
            {
                fail ("bad number");
                return false;
            }
            if (pos < text.size() && text[pos] == '.')
            {
                ++pos;
                fraction = true;
                if (! digits())
                {
                    fail ("bad number");
                    return false;
                }
            }
            if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E'))
            {
                ++pos;
                fraction = true;
                if (pos < text.size() && (text[pos] == '+' || text[pos] == '-'))
                    ++pos;
                if (! digits())
                {
                    fail ("bad number");
                    return false;
                }
            }
            // The classic locale: a host (Live, a DAW) may run with a decimal comma.
            std::istringstream in (std::string (text.substr (start, pos - start)));
            in.imbue (std::locale::classic());
            double v = 0.0;
            in >> v;   // out of range: the largest finite value, as strtod saturates
            out = JsonValue::makeDouble (v);
            out.integer = ! fraction;
            return true;
        }
    };

    // -----------------------------------------------------------------------------------------
    // Writing
    // -----------------------------------------------------------------------------------------

    void writeString (std::string& out, const std::string& s)
    {
        static const char* hex = "0123456789abcdef";
        out += '"';
        for (const char c : s)
        {
            const auto u = static_cast<unsigned char> (c);
            switch (c)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (u < 0x20)
                    {
                        out += "\\u00";
                        out += hex[u >> 4];
                        out += hex[u & 15];
                    }
                    else
                        out += c;   // UTF-8 as is
            }
        }
        out += '"';
    }

    void writeNumber (std::string& out, const JsonValue& v)
    {
        if (! std::isfinite (v.number))
        {
            out += '0';   // JSON has no NaN or infinity
            return;
        }
        std::ostringstream s;
        s.imbue (std::locale::classic());
        if (v.integer && std::abs (v.number) < 9.0e15)
        {
            s << static_cast<long long> (v.number);
            out += s.str();
            return;
        }
        s << std::fixed << std::setprecision (6) << v.number;
        auto t = s.str();
        if (const auto dot = t.find ('.'); dot != std::string::npos)
        {
            while (t.size() > dot + 2 && t.back() == '0')
                t.pop_back();
        }
        if (t == "-0.0")
            t = "0.0";
        out += t;
    }

    bool allNumbers (const JsonValue& v)
    {
        return std::all_of (v.items.begin(), v.items.end(), [] (const JsonValue& i) { return i.isNumber(); });
    }

    void write (std::string& out, const JsonValue& v, int indent)
    {
        const std::string pad (static_cast<std::size_t> (indent + 2), ' ');
        const std::string close (static_cast<std::size_t> (indent), ' ');
        switch (v.type)
        {
            case JsonValue::Type::null:    out += "null"; break;
            case JsonValue::Type::boolean: out += v.boolean ? "true" : "false"; break;
            case JsonValue::Type::number:  writeNumber (out, v); break;
            case JsonValue::Type::string:  writeString (out, v.text); break;
            case JsonValue::Type::array:
                if (v.items.empty())
                {
                    out += "[]";
                    break;
                }
                if (allNumbers (v))
                {
                    out += '[';
                    for (std::size_t i = 0; i < v.items.size(); ++i)
                    {
                        if (i > 0)
                            out += ", ";
                        writeNumber (out, v.items[i]);
                    }
                    out += ']';
                    break;
                }
                out += "[\n";
                for (std::size_t i = 0; i < v.items.size(); ++i)
                {
                    out += pad;
                    write (out, v.items[i], indent + 2);
                    out += i + 1 < v.items.size() ? ",\n" : "\n";
                }
                out += close + ']';
                break;
            case JsonValue::Type::object:
                if (v.members.empty())
                {
                    out += "{}";
                    break;
                }
                out += "{\n";
                for (std::size_t i = 0; i < v.members.size(); ++i)
                {
                    out += pad;
                    writeString (out, v.members[i].first);
                    out += ": ";
                    write (out, v.members[i].second, indent + 2);
                    out += i + 1 < v.members.size() ? ",\n" : "\n";
                }
                out += close + '}';
                break;
        }
    }
}

std::optional<JsonValue> parseJson (std::string_view text, std::string& error)
{
    // A UTF-8 byte order mark is not JSON, but editors write one.
    if (text.substr (0, 3) == "\xEF\xBB\xBF")
        text.remove_prefix (3);
    return Parser (text).parseDocument (error);
}

std::string writeJson (const JsonValue& value)
{
    std::string out;
    write (out, value, 0);
    return out;
}

// ---------------------------------------------------------------------------------------------
// The document
// ---------------------------------------------------------------------------------------------

namespace
{
    const char* mappingId (arp::CustomMapping m) { return m == arp::CustomMapping::degrees ? "degrees" : "contour"; }
    arp::CustomMapping mappingFrom (const std::string& s) { return s == "degrees" ? arp::CustomMapping::degrees : arp::CustomMapping::contour; }

    /** Six decimals, as every ANDOR JSON number (io/JsonUtil's json::number). */
    JsonValue rounded (double v)
    {
        if (! std::isfinite (v))
            return JsonValue::makeDouble (0.0);
        return JsonValue::makeDouble (std::round (v * 1.0e6) / 1.0e6 + 0.0);
    }

    // The readers below are io/JsonUtil's getInt / getDouble / getBool / getString.
    int getInt (const JsonValue& o, const char* key, int fallback)
    {
        const auto& v = o[key];
        return v.isNumber() ? v.toInt() : fallback;
    }
    double getDouble (const JsonValue& o, const char* key, double fallback)
    {
        const auto& v = o[key];
        return v.isNumber() ? v.number : fallback;
    }
    bool getBool (const JsonValue& o, const char* key, bool fallback)
    {
        const auto& v = o[key];
        return v.isBool() ? v.boolean : fallback;
    }
    std::string getString (const JsonValue& o, const char* key, const std::string& fallback = {})
    {
        const auto& v = o[key];
        return v.isString() ? v.text : fallback;
    }
}

JsonValue documentToJson (const Document& d)
{
    auto root = JsonValue::makeObject();
    root.set ("schemaVersion", JsonValue::makeInt (Document::schemaVersion));
    root.set ("kind", JsonValue::makeString (documentKind));
    root.set ("id", JsonValue::makeString (d.id));
    root.set ("name", JsonValue::makeString (d.name));
    root.set ("description", JsonValue::makeString (d.description));
    root.set ("factory", JsonValue::makeBool (d.factory));

    auto pattern = JsonValue::makeObject();
    pattern.set ("mapping", JsonValue::makeString (mappingId (d.pattern.mapping)));
    pattern.set ("maxRank", JsonValue::makeInt (d.pattern.maxRank));
    auto events = JsonValue::makeArray();
    for (const auto& e : d.pattern.events)
    {
        auto item = JsonValue::makeArray();
        item.append (JsonValue::makeInt (e.rest ? -1 : e.rank));
        item.append (JsonValue::makeInt (e.duration));
        if (e.velocity > 0)
            item.append (JsonValue::makeInt (e.velocity));
        events.append (std::move (item));
    }
    pattern.set ("events", std::move (events));
    root.set ("pattern", std::move (pattern));

    const auto& in = d.interpretation;
    auto how = JsonValue::makeObject();
    how.set ("timing", JsonValue::makeString (in.timing == Interpretation::Timing::steps ? "steps" : "rhythm"));
    how.set ("quantumTicks", JsonValue::makeInt (in.quantumTicks));
    how.set ("stepQuarters", rounded (in.stepQuarters));
    how.set ("lengthQuarters", rounded (in.lengthQuarters));
    how.set ("chord", JsonValue::makeString (in.chord == ChordPolicy::lowest ? "lowest" : "highest"));
    how.set ("mapping", JsonValue::makeString (mappingId (in.mapping)));
    root.set ("interpretation", std::move (how));

    auto capture = JsonValue::makeObject();
    capture.set ("bpm", rounded (d.captureBpm));
    auto notes = JsonValue::makeArray();
    for (const auto& n : d.capture)
    {
        auto item = JsonValue::makeArray();
        item.append (rounded (n.onset));
        item.append (rounded (n.duration));
        item.append (JsonValue::makeInt (n.pitch));
        item.append (JsonValue::makeInt (n.velocity));
        notes.append (std::move (item));
    }
    capture.set ("notes", std::move (notes));
    root.set ("capture", std::move (capture));
    return root;
}

std::optional<Document> documentFromJson (const JsonValue& root, std::string& error)
{
    if (! root.isObject())
    {
        error = "not a pattern (no JSON object)";
        return std::nullopt;
    }
    const int version = getInt (root, "schemaVersion", 0);
    if (version < 1 || version > Document::schemaVersion)
    {
        error = version > Document::schemaVersion ? "made by a newer version (pattern schema " + std::to_string (version) + ")"
                                                  : "not a pattern (no schemaVersion)";
        return std::nullopt;
    }
    if (getString (root, "kind") != documentKind)
    {
        error = "not an ANDOR ARP pattern";
        return std::nullopt;
    }
    Document d;
    d.id = getString (root, "id");
    d.name = getString (root, "name");
    d.description = getString (root, "description");
    d.factory = getBool (root, "factory", false);

    const auto& pattern = root["pattern"];
    d.pattern.mapping = mappingFrom (getString (pattern, "mapping", "contour"));
    d.pattern.maxRank = std::clamp (getInt (pattern, "maxRank", 0), 0, maxRankLimit);
    if (const auto& events = pattern["events"]; events.isArray())
        for (const auto& item : events.items)
        {
            if (! item.isArray() || item.items.size() < 2)
            {
                error = "a pattern event is not [rank, ticks]";
                return std::nullopt;
            }
            const int rank = item.items[0].toInt();
            Event ev { std::max (0, rank), rank < 0, item.items[1].toInt(), item.items.size() > 2 ? item.items[2].toInt() : 0 };
            d.pattern.events.push_back (ev);
        }
    if (! d.pattern.valid())
    {
        error = "the pattern is outside the limits (1-256 events, 1-128 steps, each at least 1/32 step, one note at least)";
        return std::nullopt;
    }

    const auto& how = root["interpretation"];
    auto& in = d.interpretation;
    in.timing = getString (how, "timing", "rhythm") == "steps" ? Interpretation::Timing::steps : Interpretation::Timing::rhythm;
    in.quantumTicks = std::clamp (getInt (how, "quantumTicks", in.quantumTicks), arp::minEventTicks, arp::ticksPerStep);
    in.stepQuarters = std::clamp (getDouble (how, "stepQuarters", in.stepQuarters), 1.0 / 64.0, 4.0);
    in.lengthQuarters = std::clamp (getDouble (how, "lengthQuarters", 0.0), 0.0, 4.0 * arp::maxPatternSteps);
    in.chord = getString (how, "chord", "highest") == "lowest" ? ChordPolicy::lowest : ChordPolicy::highest;
    in.mapping = mappingFrom (getString (how, "mapping", mappingId (d.pattern.mapping)));

    const auto& capture = root["capture"];
    d.captureBpm = std::clamp (getDouble (capture, "bpm", 120.0), 5.0, 1000.0);
    if (const auto& notes = capture["notes"]; notes.isArray())
        for (const auto& item : notes.items)
            if (item.isArray() && item.items.size() >= 4 && static_cast<int> (d.capture.size()) < maxCaptureNotes)
                d.capture.push_back ({ item.items[0].toDouble(), item.items[1].toDouble(), std::clamp (item.items[2].toInt(), 0, 127),
                                       std::clamp (item.items[3].toInt(), 1, 127) });
    return d;
}

std::string documentToText (const Document& d)
{
    return writeJson (documentToJson (d));
}

std::optional<Document> documentFromText (std::string_view text, std::string& error)
{
    const auto parsed = parseJson (text, error);
    if (! parsed)
        return std::nullopt;
    return documentFromJson (*parsed, error);
}

} // namespace osp::arppattern
