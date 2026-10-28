#include "InstrumentHost.h"
#include "AndorParameters.h"

#include "engine/Dampen.h"

#include <algorithm>

namespace osp::plugin
{

juce::String InstrumentHost::modLfoId (int lfo, const juce::String& name) { return "mod.lfo" + juce::String (lfo + 1) + "." + name; }
juce::String InstrumentHost::modEnvId (int env, const juce::String& name) { return "mod.env" + juce::String (env + 1) + "." + name; }
juce::String InstrumentHost::modRouteId (int route, const juce::String& name) { return "mod.route" + juce::String (route + 1) + "." + name; }

const juce::StringArray& InstrumentHost::dampenParameterNames()
{
    static const juce::StringArray names { "enabled", "mode", "amount", "frequency", "cutoff" };
    return names;
}

const juce::StringArray& InstrumentHost::eqParameterNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray list { "enabled" };
        for (int b = 0; b < eq::bandCount; ++b)
        {
            const auto band = static_cast<eq::Band> (b);
            const juce::String key = eqBandKey (band);
            list.add (key + ".enabled");
            list.add (key + ".frequency");
            if (eq::hasGain (band))
            {
                list.add (key + ".gain");
                list.add (key + ".q");
            }
            else
                list.add (key + ".slope");
        }
        return list;
    }();
    return names;
}

const char* InstrumentHost::eqBandKey (eq::Band band) noexcept
{
    static const char* keys[] { "hp", "lowShelf", "bell", "highShelf", "lp" };
    return keys[std::clamp (static_cast<int> (band), 0, eq::bandCount - 1)];
}

eq::Settings InstrumentHost::eqFromValues (const std::array<float, 19>& v)
{
    eq::Settings s;
    s.enabled = v[0] >= 0.5f;
    std::size_t i = 1;
    for (int b = 0; b < eq::bandCount; ++b)
    {
        auto& band = s.bands[static_cast<std::size_t> (b)];
        band.enabled = v[i++] >= 0.5f;
        band.frequencyHz = v[i++];
        if (eq::hasGain (static_cast<eq::Band> (b)))
        {
            band.gainDb = v[i++];
            band.q = v[i++];
        }
        else
            band.steep = v[i++] >= 0.5f;
    }
    return s;
}

eq::Settings InstrumentHost::layerEqSettings (int layer) const
{
    std::array<float, 19> v {};
    const auto& names = eqParameterNames();
    for (int i = 0; i < names.size(); ++i)
        v[static_cast<std::size_t> (i)] = parameterValue (eqParameterId (layer, names[i]));
    return eqFromValues (v);
}

const juce::StringArray& InstrumentHost::arpPatternNames()
{
    // Choice order is saved in sessions: never reorder (append only).
    static const juce::StringArray names = [] {
        juce::StringArray n;
        for (int i = 0; i < arp::patternCount; ++i)
            n.add (arp::patternName (static_cast<ArpPattern> (i)));
        return n;
    }();
    return names;
}

const juce::StringArray& InstrumentHost::arpRateNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray n;
        for (int i = 0; i < arp::rateCount; ++i)
            n.add (arp::rateName (static_cast<ArpRate> (i)));
        return n;
    }();
    return names;
}

InstrumentHost::ModCurves InstrumentHost::defaultModulationCurves()
{
    return { mod::defaultLfoCurve(), mod::defaultLfoCurve(), mod::defaultEnvCurve(), mod::defaultEnvCurve(), mod::straightCurve(), mod::straightCurve(),
             dampen::defaultCurve(), dampen::defaultCurve(), dampen::defaultCurve() };
}

juce::String InstrumentHost::encodeModulationCurves (const ModCurves& curves)
{
    // "c3:" then nine curves separated by '|', each its points "x,y,tension" separated by ';'.
    juce::StringArray parts;
    for (const auto& c : curves)
    {
        juce::StringArray points;
        for (int i = 0; i < c.count; ++i)
        {
            const auto& p = c.points[static_cast<std::size_t> (i)];
            points.add (juce::String (p.x, 4) + "," + juce::String (p.y, 4) + "," + juce::String (p.tension, 4));
        }
        parts.add (points.joinIntoString (";"));
    }
    return "c3:" + parts.joinIntoString ("|");
}

std::optional<InstrumentHost::ModCurves> InstrumentHost::decodeModulationCurves (const juce::String& text)
{
    // c1 (state 11): the four LFO and envelope curves; c2 adds VEL and NOTE; c3 DAMPEN A-C.
    const int count = text.startsWith ("c3:") ? 9 : text.startsWith ("c2:") ? 6 : text.startsWith ("c1:") ? 4 : 0;
    if (count == 0)
        return std::nullopt;
    const auto parts = juce::StringArray::fromTokens (text.substring (3), "|", "");
    if (parts.size() != count)
        return std::nullopt;
    auto curves = defaultModulationCurves();
    for (int c = 0; c < count; ++c)
    {
        const auto points = juce::StringArray::fromTokens (parts[c], ";", "");
        auto& curve = curves[static_cast<std::size_t> (c)];
        curve.count = 0;
        for (const auto& point : points)
        {
            const auto v = juce::StringArray::fromTokens (point, ",", "");
            if (v.size() != 3 || curve.count >= mod::maxCurvePoints)
                return std::nullopt;
            curve.points[static_cast<std::size_t> (curve.count++)] = { v[0].getFloatValue(), v[1].getFloatValue(), v[2].getFloatValue() };
        }
        curve.normalise();
    }
    return curves;
}

//==============================================================================
// Parameters, layers, modulation routes: everything here works on the parameters and the
// state tree alone, so every product shares one implementation (moved unchanged from ANDOR
// OSP's processor).

namespace
{
    const juce::Identifier modLinksType ("ModLinks");
}

const juce::Identifier& InstrumentHost::modLinksTreeType()
{
    return modLinksType;
}

namespace
{
    /** Why a source the rules refuse cannot reach a destination (after "X cannot reach Y"). */
    juce::String compatibilityReason (mod::Dest dest)
    {
        const bool envelopeTime = mod::destInfo (dest).modulates != mod::Source::none;
        const bool start = dest == mod::Dest::startA || dest == mod::Dest::startB || dest == mod::Dest::startC;
        return envelopeTime ? ": an envelope's times take the LFOs and the mod wheel"
                            : start ? ": an envelope has no value yet when a note starts"
                                    : ": a POLY LFO has no single value on a shared stage";
    }
}

int InstrumentHost::addModulationRoute (mod::Source source, mod::Dest dest, float depthPercent)
{
    // The first empty slot (or the slot already joining these two: its depth is set).
    int slot = -1;
    for (int r = 0; r < mod::maxRoutes && slot < 0; ++r)
        if (juce::roundToInt (parameterValue (modRouteId (r, "source"))) == static_cast<int> (source)
            && juce::roundToInt (parameterValue (modRouteId (r, "dest"))) == static_cast<int> (dest))
            slot = r;
    for (int r = 0; r < mod::maxRoutes && slot < 0; ++r)
        if (juce::roundToInt (parameterValue (modRouteId (r, "source"))) == 0)
            slot = r;
    if (slot < 0)
        return -1;
    undo().beginNewTransaction ("Add modulation");
    if (juce::roundToInt (parameterValue (modRouteId (slot, "source"))) == 0)
        setModLink (slot, newModRouteUid(), 0);   // a new route: a new identity
    setParameterValue (modRouteId (slot, "source"), static_cast<float> (source));
    setParameterValue (modRouteId (slot, "dest"), static_cast<float> (dest));
    setParameterValue (modRouteId (slot, "depth"), depthPercent);
    setParameterValue (modRouteId (slot, "enabled"), 1.0f);
    commitRouteChange();
    return slot;
}

void InstrumentHost::removeModulationRoute (int slot, bool ownUndoStep)
{
    if (slot < 0 || slot >= mod::maxRoutes)
        return;
    if (ownUndoStep)
        undo().beginNewTransaction ("Remove modulation");
    // The routes moving its depth go with it (no reference is left to a route that is gone).
    auto clear = [this] (int r) {
        setParameterValue (modRouteId (r, "source"), 0.0f);
        setParameterValue (modRouteId (r, "dest"), 0.0f);
        setParameterValue (modRouteId (r, "depth"), 0.0f);
        setParameterValue (modRouteId (r, "enabled"), 1.0f);
        setModLink (r, 0, 0);
    };
    for (const int dependent : depthRoutesOf (slot))
        clear (dependent);
    clear (slot);
    commitRouteChange();
}

void InstrumentHost::commitRouteChange()
{
    // The parameters reach the state tree (and so the undo step) on APVTS's timer; a route's
    // links are written at once. Flushed now, a route change and its links are one undo step
    // whatever happens next (copyState is the only public flush).
    apvts().copyState();
}

juce::ValueTree InstrumentHost::modLinks() const
{
    return apvts().state.getChildWithName (modLinksType);
}

void InstrumentHost::setModLink (int slot, int uid, int targetUid)
{
    if (slot < 0 || slot >= mod::maxRoutes)
        return;
    auto links = apvts().state.getChildWithName (modLinksType);
    if (! links.isValid())
    {
        if (uid <= 0 && targetUid <= 0)
            return;
        links = juce::ValueTree (modLinksType);
        links.setProperty ("schemaVersion", 1, nullptr);
        apvts().state.appendChild (links, &undo());
    }
    const auto n = juce::String (slot + 1);
    for (const auto& [name, value] : { std::pair<juce::String, int> { "uid" + n, uid }, { "target" + n, targetUid } })
    {
        if (value > 0)
            links.setProperty (name, value, &undo());
        else if (links.hasProperty (name))
            links.removeProperty (name, &undo());
    }
}

int InstrumentHost::modRouteUid (int slot) const
{
    const auto links = modLinks();
    return links.isValid() && slot >= 0 && slot < mod::maxRoutes ? static_cast<int> (links.getProperty ("uid" + juce::String (slot + 1), 0)) : 0;
}

int InstrumentHost::newModRouteUid() const
{
    int highest = 0;
    for (int r = 0; r < mod::maxRoutes; ++r)
        highest = std::max (highest, modRouteUid (r));
    return highest + 1;
}

int InstrumentHost::resolveModTarget (int slot) const
{
    // By identity only (the slot holding the target's ID), whatever the parameters say now:
    // the engine checks that it is a route and that the pairing is allowed.
    const auto links = modLinks();
    if (! links.isValid() || slot < 0 || slot >= mod::maxRoutes)
        return -1;
    const int targetUid = static_cast<int> (links.getProperty ("target" + juce::String (slot + 1), 0));
    if (targetUid <= 0)
        return -1;
    for (int r = 0; r < mod::maxRoutes; ++r)
        if (r != slot && modRouteUid (r) == targetUid)
            return r;
    return -1;
}

int InstrumentHost::modRouteTarget (int slot) const
{
    if (slot < 0 || slot >= mod::maxRoutes || juce::roundToInt (parameterValue (modRouteId (slot, "dest"))) != static_cast<int> (mod::Dest::routeDepth))
        return -1;
    return resolveModTarget (slot);
}

std::vector<int> InstrumentHost::depthRoutesOf (int slot) const
{
    std::vector<int> list;
    for (int r = 0; r < mod::maxRoutes; ++r)
        if (r != slot && juce::roundToInt (parameterValue (modRouteId (r, "source"))) != 0 && modRouteTarget (r) == slot)
            list.push_back (r);
    return list;
}

mod::Settings InstrumentHost::modulationProbe() const
{
    mod::Settings probe;   // the rules need the sources' scopes, the loop check the routes
    for (int i = 0; i < 2; ++i)
        probe.lfo[static_cast<std::size_t> (i)].scope = parameterValue (modLfoId (i, "scope")) >= 0.5f ? mod::Scope::poly : mod::Scope::global;
    for (int r = 0; r < mod::maxRoutes; ++r)
    {
        auto& route = probe.routes[static_cast<std::size_t> (r)];
        route.source = static_cast<mod::Source> (std::clamp (juce::roundToInt (parameterValue (modRouteId (r, "source"))), 0, mod::sourceCount));
        route.dest = static_cast<mod::Dest> (std::clamp (juce::roundToInt (parameterValue (modRouteId (r, "dest"))), 0, mod::destCount - 1));
        route.depth = 0.01 * parameterValue (modRouteId (r, "depth"));
        route.enabled = parameterValue (modRouteId (r, "enabled")) >= 0.5f;
        route.target = resolveModTarget (r);
    }
    return probe;
}

bool InstrumentHost::canModulate (mod::Source source, mod::Dest dest) const
{
    const auto probe = modulationProbe();
    return mod::compatible (probe, source, dest) && ! mod::createsCycle (probe, source, dest);
}

juce::String InstrumentHost::depthModulationRefusal (mod::Source source, int targetSlot) const
{
    if (source == mod::Source::none)
        return "Nothing to assign";
    const auto probe = modulationProbe();
    if (targetSlot < 0 || targetSlot >= mod::maxRoutes)
        return "There is no modulation route here: drop on the knob to make one";
    const auto& target = probe.routes[static_cast<std::size_t> (targetSlot)];
    const juce::String name (mod::sourceName (source));
    const auto route = juce::String (mod::sourceName (target.source)) + juce::String::fromUTF8 (" \xe2\x86\x92 ") + mod::destInfo (target.dest).name;
    switch (mod::depthRule (probe, source, targetSlot))
    {
        case mod::DepthRule::ok: return {};
        case mod::DepthRule::noRoute: return "There is no modulation route here: drop on the knob to make one";
        case mod::DepthRule::nested: return name + " cannot move a depth modulation's own amount: one level only";
        case mod::DepthRule::self: return name + " cannot modulate the depth of its own route (" + route + ")";
        case mod::DepthRule::scope: return name + " cannot reach the depth of " + route + compatibilityReason (target.dest);
        case mod::DepthRule::cycle: return name + " cannot reach the depth of " + route + ": it would loop back on itself";
    }
    return {};
}

InstrumentHost::ModAssignResult InstrumentHost::assignDepthModulation (mod::Source source, int targetSlot, float depthPercent)
{
    ModAssignResult result;
    result.error = depthModulationRefusal (source, targetSlot);
    if (result.error.isNotEmpty())
        return result;
    const auto probe = modulationProbe();
    for (int r = 0; r < mod::maxRoutes; ++r)
    {
        const auto& route = probe.routes[static_cast<std::size_t> (r)];
        if (mod::isDepthRoute (route) && route.source == source && route.target == targetSlot)
        {
            result.slot = r;
            result.existed = true;   // selected for editing, as it is
            return result;
        }
    }
    for (int r = 0; r < mod::maxRoutes && result.slot < 0; ++r)
        if (probe.routes[static_cast<std::size_t> (r)].source == mod::Source::none)
            result.slot = r;
    if (result.slot < 0)
    {
        result.error = "All " + juce::String (mod::maxRoutes) + " modulation routes are in use: remove one first";
        return result;
    }
    undo().beginNewTransaction ("Modulate route depth");
    // The target is referred to by its identity (given one now if it had none), never by its slot.
    int targetUid = modRouteUid (targetSlot);
    if (targetUid <= 0)
    {
        targetUid = newModRouteUid();
        setModLink (targetSlot, targetUid, 0);
    }
    setModLink (result.slot, newModRouteUid(), targetUid);
    setParameterValue (modRouteId (result.slot, "source"), static_cast<float> (source));
    setParameterValue (modRouteId (result.slot, "dest"), static_cast<float> (mod::Dest::routeDepth));
    setParameterValue (modRouteId (result.slot, "depth"), depthPercent);
    setParameterValue (modRouteId (result.slot, "enabled"), 1.0f);
    commitRouteChange();
    return result;
}

InstrumentHost::ModAssignResult InstrumentHost::assignModulation (mod::Source source, mod::Dest dest, float depthPercent)
{
    ModAssignResult result;
    const auto probe = modulationProbe();
    if (source == mod::Source::none || dest == mod::Dest::none)
    {
        result.error = "Nothing to assign";
        return result;
    }
    const juce::String pair = juce::String (mod::sourceName (source)) + " cannot reach " + mod::destInfo (dest).name;
    if (dest == mod::Dest::routeDepth)
    {
        result.error = "A route's depth is modulated from its halo";
        return result;
    }
    if (! mod::compatible (probe, source, dest))
    {
        result.error = pair + compatibilityReason (dest);
        return result;
    }
    if (mod::createsCycle (probe, source, dest))
    {
        result.error = pair + ": it would loop back on itself";
        return result;
    }
    for (int r = 0; r < mod::maxRoutes; ++r)
        if (probe.routes[static_cast<std::size_t> (r)].source == source && probe.routes[static_cast<std::size_t> (r)].dest == dest)
        {
            result.slot = r;
            result.existed = true;   // selected for editing, as it is
            return result;
        }
    for (int r = 0; r < mod::maxRoutes && result.slot < 0; ++r)
        if (probe.routes[static_cast<std::size_t> (r)].source == mod::Source::none)
            result.slot = r;
    if (result.slot < 0)
    {
        result.error = "All " + juce::String (mod::maxRoutes) + " modulation routes are in use: remove one first";
        return result;
    }
    undo().beginNewTransaction ("Add modulation");
    setModLink (result.slot, newModRouteUid(), 0);   // a new route: a new identity
    setParameterValue (modRouteId (result.slot, "source"), static_cast<float> (source));
    setParameterValue (modRouteId (result.slot, "dest"), static_cast<float> (dest));
    setParameterValue (modRouteId (result.slot, "depth"), depthPercent);
    setParameterValue (modRouteId (result.slot, "enabled"), 1.0f);
    commitRouteChange();
    return result;
}

std::vector<InstrumentHost::ModRouteInfo> InstrumentHost::modulationRoutes() const
{
    std::vector<ModRouteInfo> list;
    const auto probe = modulationProbe();   // the rules need the sources' scopes, depth routes the others
    for (int r = 0; r < mod::maxRoutes; ++r)
    {
        ModRouteInfo info;
        info.slot = r;
        info.route = probe.routes[static_cast<std::size_t> (r)];
        if (info.route.source == mod::Source::none)
            continue;
        if (! info.isDepth())
            info.route.target = -1;
        info.state = mod::routeState (probe, r);
        list.push_back (info);
    }
    return list;
}

void InstrumentHost::setParameterValue (const juce::String& id, float value)
{
    if (auto* p = apvts().getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

float InstrumentHost::parameterValue (const juce::String& id) const
{
    if (auto* p = apvts().getParameter (id))
        return p->convertFrom0to1 (p->getValue());
    return 0.0f;
}

bool InstrumentHost::isLayerHeard (int layer) const
{
    bool anySolo = false;
    for (int l = 0; l < numLayers; ++l)
        anySolo = anySolo || parameterValue (layerParameterId (l, "solo")) >= 0.5f;
    return anySolo ? parameterValue (layerParameterId (layer, "solo")) >= 0.5f : parameterValue (layerParameterId (layer, "mute")) < 0.5f;
}

void InstrumentHost::soloOnly (int layer)
{
    for (int l = 0; l < numLayers; ++l)
        if (auto* p = apvts().getParameter (layerParameterId (l, "solo")))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (l == layer ? 1.0f : 0.0f);
            p->endChangeGesture();
        }
}

const juce::StringArray& InstrumentHost::layerControlNames()
{
    static const juce::StringArray names { "start", "tune", "pan", "level", "link", "reverse", "loop", "follow" };
    return names;
}

void InstrumentHost::applyLinkedDelta (int layer, const juce::String& control, float delta)
{
    const bool reimaginedControl = control == "reimagined";
    if (std::abs (delta) < 1.0e-9f || ! isLayerLinked (layer) || ! (reimaginedControl || layerControlNames().contains (control)))
        return;
    for (int other = 0; other < numLayers; ++other)
    {
        if (other == layer || ! isLayerOccupied (other) || ! isLayerLinked (other))
            continue;
        if (auto* p = apvts().getParameter (reimaginedControl ? reimaginedParameterId (other) : layerParameterId (other, control)))
        {
            const auto range = p->getNormalisableRange();
            const float next = range.snapToLegalValue (std::clamp (p->convertFrom0to1 (p->getValue()) + delta, range.start, range.end));
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (next));
            p->endChangeGesture();
        }
    }
}

int InstrumentHost::occupiedLayerCount() const
{
    int count = 0;
    for (int layer = 0; layer < numLayers; ++layer)
        count += isLayerOccupied (layer) ? 1 : 0;
    return count;
}

void InstrumentHost::makeLayerAudible (int layer, bool keepMix)
{
    // A layer the musician just added is heard at once: two layers meet in the middle of the
    // blend. A third added to an A/B pair takes a third of the mix while A and B keep the
    // balance they had (a centred pair: all three at the centre). Three dropped together (or
    // a reset) meet at the centre.
    const int count = occupiedLayerCount();
    if (layer >= 1)   // it starts as Reimagined as the instrument (A) is
        setParameterValue (reimaginedParameterId (layer), parameterValue (reimaginedParameterId (0)));
    if (count == 2 && layer >= 1)
        setParameterValue (ids::blend, 0.5f);
    else if (count == 3 && keepMix)
    {
        const double s = std::sin (0.5 * juce::MathConstants<double>::pi * std::clamp (static_cast<double> (parameterValue (ids::blend)), 0.0, 1.0));
        const double shareB = s * s;   // equal-power blend -> B's power share
        // Shares A = 1 - x - y/2, B = y, C = x - y/2: C 1/3, A and B 2/3 in their old ratio.
        const double b = 2.0 / 3.0 * shareB;
        setParameterValue (ids::mixX, static_cast<float> (1.0 / 3.0 + 0.5 * b));
        setParameterValue (ids::mixY, static_cast<float> (b));
    }
    else if (count == 3)
    {
        setParameterValue (ids::mixX, 0.5f);
        setParameterValue (ids::mixY, 1.0f / 3.0f);
    }
}

} // namespace osp::plugin
