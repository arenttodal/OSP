#include "KeyDnaState.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// (Moved here from ANDOR OSP's processor unchanged: ANDOR OSP's statics now call these.)

namespace osp::plugin::keyDnaState
{

namespace
{
    // KEY DNA's numbers as round-trip-exact text (a reopened session computes the same profile).
    juce::String keyDnaNumber (double value)
    {
        char buffer[40];
        std::snprintf (buffer, sizeof (buffer), "%.17g", value);
        return buffer;
    }
    double keyDnaValue (const juce::var& v, double fallback)
    {
        const auto text = v.toString();
        return text.isEmpty() ? fallback : std::strtod (text.toRawUTF8(), nullptr);
    }
    juce::String encodeCurveTable (const mod::CurveTable& t)
    {
        // The compiled curve, exactly (what is heard): its 256 values, 9 significant digits.
        juce::StringArray values;
        for (float v : t)
        {
            char buffer[24];
            std::snprintf (buffer, sizeof (buffer), "%.9g", static_cast<double> (v));
            values.add (buffer);
        }
        return values.joinIntoString (" ");
    }
    std::optional<mod::CurveTable> decodeCurveTable (const juce::String& text)
    {
        const auto tokens = juce::StringArray::fromTokens (text, " ", "");
        if (tokens.size() != mod::curvePoints)
            return std::nullopt;
        mod::CurveTable t {};
        for (int i = 0; i < mod::curvePoints; ++i)
            t[static_cast<std::size_t> (i)] = std::clamp (std::strtof (tokens[i].toRawUTF8(), nullptr), 0.0f, 1.0f);
        return t;
    }
    // The editor's points ("x y tension" per point, ";" between): the curve as drawn. The
    // compiled table beside it stays what plays.
    juce::String encodeCurvePoints (const mod::Curve& c)
    {
        juce::StringArray points;
        for (int i = 0; i < c.count; ++i)
        {
            const auto& p = c.points[static_cast<std::size_t> (i)];
            char buffer[64];
            std::snprintf (buffer, sizeof (buffer), "%.9g %.9g %.9g", static_cast<double> (p.x), static_cast<double> (p.y), static_cast<double> (p.tension));
            points.add (buffer);
        }
        return points.joinIntoString (";");
    }
    std::optional<mod::Curve> decodeCurvePoints (const juce::String& text)
    {
        const auto points = juce::StringArray::fromTokens (text, ";", "");
        if (points.size() < 2 || points.size() > mod::maxCurvePoints)
            return std::nullopt;
        mod::Curve c;
        c.count = points.size();
        for (int i = 0; i < c.count; ++i)
        {
            const auto values = juce::StringArray::fromTokens (points[i], " ", "");
            if (values.size() != 3)
                return std::nullopt;
            c.points[static_cast<std::size_t> (i)] = { std::strtof (values[0].toRawUTF8(), nullptr), std::strtof (values[1].toRawUTF8(), nullptr),
                                                        std::strtof (values[2].toRawUTF8(), nullptr) };
        }
        c.normalise();
        return c;
    }
}

juce::ValueTree toTree (const std::array<keydna::Settings, layerCount>& layers, const Curves& curves)
{
    // KEY DNA schema 1: per layer its switch, individuality and seed, and per parameter
    // (position .. pan, by id) its curve depth, variation and curve (omitted when flat).
    juce::ValueTree tree ("KeyDna");
    tree.setProperty ("schemaVersion", schemaVersion, nullptr);
    const auto flat = keydna::flatCurve();
    for (int layer = 0; layer < layerCount; ++layer)
    {
        const auto& s = layers[static_cast<std::size_t> (layer)];
        juce::ValueTree l ("Layer");
        l.setProperty ("index", layer, nullptr);
        l.setProperty ("enabled", s.enabled, nullptr);
        l.setProperty ("individuality", keyDnaNumber (s.individuality), nullptr);
        l.setProperty ("seed", juce::String (s.seed), nullptr);
        for (int p = 0; p < keydna::paramCount; ++p)
        {
            const auto param = static_cast<keydna::Param> (p);
            const auto& ps = s[param];
            const auto& drawn = curves[static_cast<std::size_t> (layer)][static_cast<std::size_t> (p)];
            if (juce::exactlyEqual (ps.curveDepth, 0.0) && juce::exactlyEqual (ps.variation, 0.0) && ps.curve == flat && ! drawn.has_value())
                continue;
            juce::ValueTree pt ("Param");
            pt.setProperty ("id", keydna::paramId (param), nullptr);
            pt.setProperty ("depth", keyDnaNumber (ps.curveDepth), nullptr);
            pt.setProperty ("variation", keyDnaNumber (ps.variation), nullptr);
            if (! (ps.curve == flat))
                pt.setProperty ("curve", encodeCurveTable (ps.curve), nullptr);
            if (drawn.has_value())
                pt.setProperty ("points", encodeCurvePoints (*drawn), nullptr);   // Phase 5: the editor's points
            l.appendChild (pt, nullptr);
        }
        tree.appendChild (l, nullptr);
    }
    return tree;
}

Curves curvesFromTree (const juce::ValueTree& tree)
{
    Curves curves {};
    if (! tree.isValid() || static_cast<int> (tree.getProperty ("schemaVersion", 0)) != schemaVersion)
        return curves;
    for (const auto& l : tree)
    {
        const int index = static_cast<int> (l.getProperty ("index", -1));
        if (! l.hasType ("Layer") || index < 0 || index >= layerCount)
            continue;
        for (const auto& pt : l)
            for (int p = 0; p < keydna::paramCount; ++p)
                if (pt.hasType ("Param") && pt.getProperty ("id").toString() == keydna::paramId (static_cast<keydna::Param> (p)) && pt.hasProperty ("points"))
                    curves[static_cast<std::size_t> (index)][static_cast<std::size_t> (p)] = decodeCurvePoints (pt.getProperty ("points").toString());
    }
    return curves;
}

std::array<keydna::Settings, layerCount> fromTree (const juce::ValueTree& tree)
{
    // Absent (an older session) or from a newer schema this build does not know: no KEY DNA
    // (the original Granulator), never a guess.
    std::array<keydna::Settings, layerCount> layers {};
    if (! tree.isValid() || static_cast<int> (tree.getProperty ("schemaVersion", 0)) != schemaVersion)
        return layers;
    for (const auto& l : tree)
    {
        if (! l.hasType ("Layer"))
            continue;
        const int index = static_cast<int> (l.getProperty ("index", -1));
        if (index < 0 || index >= layerCount)
            continue;
        auto& s = layers[static_cast<std::size_t> (index)];
        s.enabled = static_cast<bool> (l.getProperty ("enabled", false));
        s.individuality = std::clamp (keyDnaValue (l.getProperty ("individuality"), 0.0), 0.0, 1.0);
        s.seed = std::strtoull (l.getProperty ("seed", "1").toString().toRawUTF8(), nullptr, 10);
        for (const auto& pt : l)
            for (int p = 0; p < keydna::paramCount; ++p)
            {
                const auto param = static_cast<keydna::Param> (p);
                if (! pt.hasType ("Param") || pt.getProperty ("id").toString() != keydna::paramId (param))
                    continue;
                auto& ps = s[param];
                ps.curveDepth = std::clamp (keyDnaValue (pt.getProperty ("depth"), 0.0), -1.0, 1.0);
                ps.variation = std::clamp (keyDnaValue (pt.getProperty ("variation"), 0.0), 0.0, 1.0);
                if (const auto curve = decodeCurveTable (pt.getProperty ("curve").toString()))
                    ps.curve = *curve;
            }
    }
    return layers;
}

mod::Curve editorCurve (const keydna::Settings& settings, const std::optional<mod::Curve>& drawn, keydna::Param param, const mod::Curve& fallback)
{
    if (drawn.has_value())
        return *drawn;
    const auto& table = settings[param].curve;
    if (table == keydna::flatCurve())
        return fallback;
    // A curve saved without its points (KEY DNA before Phase 5): nine points along its table.
    mod::Curve curve;
    curve.count = 9;
    for (int i = 0; i < curve.count; ++i)
    {
        const auto x = static_cast<float> (i) / 8.0f;
        const auto at = std::min (mod::curvePoints - 1, static_cast<int> (std::lround (x * (mod::curvePoints - 1))));
        curve.points[static_cast<std::size_t> (i)] = { x, table[static_cast<std::size_t> (at)], 0.0f };
    }
    return curve;
}

} // namespace osp::plugin::keyDnaState
