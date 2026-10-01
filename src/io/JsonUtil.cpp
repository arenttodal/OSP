#include "io/JsonUtil.h"

#include <cmath>

namespace osp::json
{

namespace
{
    juce::File toJuceFile (const std::filesystem::path& path)
    {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute (path, ec);
        return juce::File (juce::String::fromUTF8 ((ec ? path : absolute).string().c_str()));
    }
}

juce::var object() { return juce::var (new juce::DynamicObject()); }
juce::var array() { return juce::var (juce::Array<juce::var>()); }

void set (juce::var& obj, const char* key, const juce::var& value)
{
    if (auto* o = obj.getDynamicObject())
        o->setProperty (juce::Identifier (key), value);
}

double round (double value, int decimals)
{
    if (! std::isfinite (value))
        return 0.0;
    const double scale = std::pow (10.0, decimals);
    return std::round (value * scale) / scale + 0.0; // + 0.0 turns -0 into 0
}

juce::var number (double value, int decimals) { return juce::var (round (value, decimals)); }
juce::var str (const std::string& s) { return juce::var (juce::String::fromUTF8 (s.c_str())); }

juce::var floatArray (const std::vector<float>& values, int decimals)
{
    juce::Array<juce::var> items;
    items.ensureStorageAllocated (static_cast<int> (values.size()));
    for (float v : values)
        items.add (round (static_cast<double> (v), decimals));
    return juce::var (items);
}

juce::var stringArray (const std::vector<std::string>& values)
{
    juce::Array<juce::var> items;
    for (const auto& v : values)
        items.add (str (v));
    return juce::var (items);
}

namespace
{
    /** Puts arrays that contain only numbers on one line (JUCE writes one element per line). */
    std::string collapseNumericArrays (const std::string& text)
    {
        std::string out;
        out.reserve (text.size());
        std::size_t i = 0;
        bool inString = false;
        while (i < text.size())
        {
            // Never touch string contents (file names may contain brackets).
            if (inString)
            {
                if (text[i] == '\\' && i + 1 < text.size())
                {
                    out += text[i++];
                    out += text[i++];
                    continue;
                }
                if (text[i] == '"')
                    inString = false;
                out += text[i++];
                continue;
            }
            if (text[i] == '"')
            {
                inString = true;
                out += text[i++];
                continue;
            }
            if (text[i] == '[')
            {
                const auto close = text.find (']', i);
                if (close != std::string::npos)
                {
                    bool numeric = true;
                    bool hasDigit = false;
                    for (std::size_t k = i + 1; k < close && numeric; ++k)
                    {
                        const char c = text[k];
                        hasDigit = hasDigit || (c >= '0' && c <= '9');
                        numeric = (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E'
                                  || c == ',' || c == ' ' || c == '\n' || c == '\r' || c == '\t';
                    }
                    if (numeric && hasDigit)
                    {
                        out += '[';
                        bool pendingSpace = false;
                        for (std::size_t k = i + 1; k < close; ++k)
                        {
                            const char c = text[k];
                            if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
                                continue;
                            if (pendingSpace)
                                out += ' ';
                            pendingSpace = false;
                            out += c;
                            if (c == ',')
                                pendingSpace = true;
                        }
                        out += ']';
                        i = close + 1;
                        continue;
                    }
                }
            }
            out += text[i++];
        }
        return out;
    }
}

std::string toString (const juce::var& value)
{
    const auto options = juce::JSON::FormatOptions {}
                             .withSpacing (juce::JSON::Spacing::multiLine)
                             .withMaxDecimalPlaces (6);
    return collapseNumericArrays (juce::JSON::toString (value, options).toStdString());
}

bool writeFile (const std::filesystem::path& path, const juce::var& value, std::string& error)
{
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories (path.parent_path(), ec);

    if (! toJuceFile (path).replaceWithText (juce::String::fromUTF8 (toString (value).c_str()) + "\n"))
    {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::optional<juce::var> parse (const std::string& text, std::string& error)
{
    juce::var result;
    const auto status = juce::JSON::parse (juce::String::fromUTF8 (text.c_str()), result);
    if (status.failed())
    {
        error = status.getErrorMessage().toStdString();
        return std::nullopt;
    }
    return result;
}

std::optional<juce::var> readFile (const std::filesystem::path& path, std::string& error)
{
    const auto file = toJuceFile (path);
    if (! file.existsAsFile())
    {
        error = "file not found: " + path.string();
        return std::nullopt;
    }
    auto parsed = parse (file.loadFileAsString().toStdString(), error);
    if (! parsed)
        error = path.string() + ": " + error;
    return parsed;
}

double getDouble (const juce::var& obj, const char* key, double fallback)
{
    const auto& v = obj[juce::Identifier (key)];
    return (v.isDouble() || v.isInt() || v.isInt64()) ? static_cast<double> (v) : fallback;
}

int getInt (const juce::var& obj, const char* key, int fallback)
{
    const auto& v = obj[juce::Identifier (key)];
    return (v.isDouble() || v.isInt() || v.isInt64()) ? static_cast<int> (v) : fallback;
}

bool getBool (const juce::var& obj, const char* key, bool fallback)
{
    const auto& v = obj[juce::Identifier (key)];
    return v.isBool() ? static_cast<bool> (v) : fallback;
}

std::string getString (const juce::var& obj, const char* key, const std::string& fallback)
{
    const auto& v = obj[juce::Identifier (key)];
    return v.isString() ? v.toString().toStdString() : fallback;
}

std::vector<float> getFloatArray (const juce::var& obj, const char* key)
{
    std::vector<float> out;
    if (const auto* items = obj[juce::Identifier (key)].getArray())
    {
        out.reserve (static_cast<std::size_t> (items->size()));
        for (const auto& item : *items)
            out.push_back (static_cast<float> (static_cast<double> (item)));
    }
    return out;
}

bool has (const juce::var& obj, const char* key)
{
    if (auto* o = obj.getDynamicObject())
        return o->hasProperty (juce::Identifier (key));
    return false;
}

} // namespace osp::json
