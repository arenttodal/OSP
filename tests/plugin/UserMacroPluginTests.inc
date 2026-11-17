// USER MACROS in ANDOR OSP (docs/research/osp-user-macros/): ANDOR ANALOG's six USER macros,
// ported - the parameters, the anchored zero-jump assignments the engine adds before anything reads
// a parameter, the state, undo and eligibility. Included after AtlasRefineTests.inc.
#include "UserMacroEditor.h"

namespace
{
    /** A sound on layer A (the plucked tutorial note), prepared at 48 kHz. */
    void loadPluck (OspAudioProcessor& p)
    {
        juce::String error;
        const auto files = osp::plugin::tutorial::files (osp::plugin::tutorial::Example::pluck, error);
        REQUIRE (files.size() == 1);
        p.loadFile (files[0], 0);
        REQUIRE (p.waitForLoads (60000));
        p.pollLoads();
        p.prepareToPlay (48000.0, 256);
    }

    /** `blocks` blocks of 256 holding A3 from the first: the left channel. */
    std::vector<float> renderNote (OspAudioProcessor& p, int blocks, const std::function<void (int)>& eachBlock = {})
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, 256);
        for (int b = 0; b < blocks; ++b)
        {
            if (eachBlock)
                eachBlock (b);
            juce::MidiBuffer midi;
            if (b == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, 57, static_cast<juce::uint8> (100)), 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + 256);
        }
        return out;
    }

    void setNormalised (OspAudioProcessor& p, const juce::String& id, float value)
    {
        auto* param = p.parameters.getParameter (id);
        REQUIRE (param != nullptr);
        param->setValueNotifyingHost (value);
    }
} // namespace

TEST_CASE ("user macros: six appended parameters at 0; nothing assigned is bit-exact, moving a macro included", "[plugin][usermacro]")
{
    OspAudioProcessor p;
    const auto& all = p.getParameters();
    REQUIRE (all.size() >= 6);
    for (int m = 0; m < 6; ++m)
    {
        // The last six, in order, 0..100 %, default 0.
        auto* param = dynamic_cast<juce::AudioParameterFloat*> (all[all.size() - 6 + m]);
        REQUIRE (param != nullptr);
        CHECK (param->paramID == "userMacro." + juce::String (m + 1));
        CHECK (param->getName (40) == "User Macro " + juce::String (m + 1));
        CHECK (param->get() == 0.0f);
        CHECK (param->getNormalisableRange().end == 100.0f);
        CHECK_FALSE (p.userMacroReads (param->paramID));   // (a macro never moves a macro)
    }
    // Nothing saved for them (no assignment, default names).
    CHECK (p.userMacroAssignments()->isDefault());
    {
        juce::MemoryBlock state;
        p.getStateInformation (state);
        const auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), static_cast<int> (state.getSize()));
        REQUIRE (xml != nullptr);
        CHECK (xml->getChildByName ("UserMacros") == nullptr);
    }
    // Moving an unassigned macro: bit-exact.
    loadPluck (p);
    const auto still = renderNote (p, 40);
    OspAudioProcessor q;
    loadPluck (q);
    const auto moved = renderNote (q, 40, [&q] (int b) { setNormalised (q, "userMacro.1", static_cast<float> (b % 10) / 10.0f); });
    CHECK (still == moved);
}

TEST_CASE ("user macros: an assignment never jumps, then moves the engine's value, never the knob; undo; state round trip", "[plugin][usermacro]")
{
    OspAudioProcessor p;
    loadPluck (p);
    namespace um = osp::plugin::usermacros;
    // Eligible: a continuous control the engine reads; not a switch, a route, the seed or a macro.
    CHECK (um::eligible (p, "layerA.level"));
    CHECK (um::eligible (p, "space"));
    CHECK_FALSE (um::eligible (p, "layerA.mute"));
    CHECK_FALSE (um::eligible (p, "mod.route1.depth"));
    CHECK_FALSE (um::eligible (p, "seed"));
    CHECK_FALSE (um::eligible (p, "userMacro.2"));
    CHECK_FALSE (um::eligible (p, "no.such.parameter"));
    // Macro 1 at 73 %, LEVEL where it is: assigned, the sound unchanged at the anchor.
    setNormalised (p, "userMacro.1", 0.73f);
    renderNote (p, 4);
    const float base = p.parameters.getParameter ("layerA.level")->getValue();
    REQUIRE (um::assign (p, 0, "layerA.level"));
    REQUIRE (p.userMacroTable().count == 1);
    const auto& map = p.userMacroAssignments()->maps[0].front().map;
    CHECK (map.anchorMacro == Approx (0.73f).margin (1.0e-3));
    CHECK (map.anchorTarget == Approx (base));
    CHECK (map.low < map.anchorTarget);    // (a visible default range: a quarter of the travel each way)
    CHECK (map.high > map.anchorTarget);
    renderNote (p, 4);
    CHECK (std::abs (p.userMacroOffsetOf ("layerA.level")) < 1.0e-6f);   // 0 at the anchor (float rounding of 73 %): no jump
    // Macro to 100 %: the engine's LEVEL rises (glides in ~20 ms); the knob itself never moves.
    setNormalised (p, "userMacro.1", 1.0f);
    renderNote (p, 40);
    CHECK (p.userMacroOffsetOf ("layerA.level") == Approx (map.high - map.anchorTarget).margin (1.0e-3));
    CHECK (p.parameters.getParameter ("layerA.level")->getValue() == Approx (base));
    // Undo: no assignment; the offset glides out.
    CHECK (p.undo().getUndoDescription().startsWith ("Assign"));
    REQUIRE (p.undo().undo());
    CHECK (p.userMacroTable().count == 0);
    renderNote (p, 60);
    CHECK (p.userMacroOffsetOf ("layerA.level") == 0.0f);
    REQUIRE (p.undo().redo());
    CHECK (p.userMacroTable().count == 1);
    // Named, saved, recalled; a state without them clears them.
    um::edit (p, [] (osp::plugin::UserMacroState& s) { s.names[0] = "BLOOM"; }, "Rename");
    juce::MemoryBlock saved;
    p.getStateInformation (saved);
    OspAudioProcessor r;
    r.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    REQUIRE (r.waitForLoads (60000));
    r.pollLoads();
    CHECK (*r.userMacroAssignments() == *p.userMacroAssignments());
    CHECK (r.userMacroAssignments()->displayName (0) == "BLOOM");
    CHECK (r.userMacroTable().count == 1);
    OspAudioProcessor blank;
    juce::MemoryBlock none;
    blank.getStateInformation (none);
    r.setStateInformation (none.getData(), static_cast<int> (none.getSize()));
    CHECK (r.userMacroAssignments()->isDefault());
    CHECK (r.userMacroTable().count == 0);
}

TEST_CASE ("user macros: bad saved data is made safe - unknown targets, duplicates, over eight, NaN, long names", "[plugin][usermacro]")
{
    juce::XmlElement xml ("UserMacros");
    xml.setAttribute ("schema", 1);
    auto* macro = xml.createNewChildElement ("Macro");
    macro->setAttribute ("index", 0);
    macro->setAttribute ("name", juce::String::fromUTF8 ("Br\xc3\xa4nde \xe2\x80\x94 a very long name indeed, far too long"));
    for (int i = 0; i < 12; ++i)
    {
        auto* m = macro->createNewChildElement ("Map");
        m->setAttribute ("target", i == 1 ? juce::String ("layerA.level") : (i == 2 ? juce::String ("bad target!") : "layerA." + juce::String (i == 0 ? "level" : "pan")));
        m->setAttribute ("anchorMacro", i == 3 ? std::numeric_limits<double>::quiet_NaN() : 0.5);
        m->setAttribute ("low", 7.0);
    }
    xml.createNewChildElement ("Macro")->setAttribute ("index", 9);
    const auto s = osp::plugin::UserMacroState::fromXml (&xml);
    CHECK (s.names[0].length() <= 24);
    REQUIRE (s.maps[0].size() == 2);   // level once (no duplicate), pan once, the bad ID rejected
    for (const auto& a : s.maps[0])
    {
        CHECK (std::isfinite (a.map.anchorMacro));
        CHECK (a.map.low <= 1.0f);
    }
    OspAudioProcessor p;
    // An unknown target in a session is kept but never compiled (never another parameter).
    auto withUnknown = s;
    withUnknown.maps[1].push_back ({ "no.such.parameter", {}, false, {} });
    p.setUserMacroAssignments (withUnknown);
    CHECK (p.userMacroTable().count == 2);
}
