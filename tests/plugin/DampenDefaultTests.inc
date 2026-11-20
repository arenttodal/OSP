// DAMPEN on by default (owner's choice, 0.9.0-beta.1; ANDOR OSP only): every layer of the seven
// built-in Starting States has it on, so a new instance (which opens on the first) starts with
// it on. The parameter's own default stays off, so sessions and presets - older ones included -
// recall exactly what they stored, and every product built from the shared schema is unchanged.
// Included at the end of PluginProcessorTests.cpp (after BuiltInStatesTests.inc: ScopedFirstState).

namespace dampendefault
{
    float dampenOn (OspAudioProcessor& p, int layer) { return p.parameterValue (OspAudioProcessor::dampenParameterId (layer, "enabled")); }
}

TEST_CASE ("dampen default: every layer of every Starting State has DAMPEN on; a new instance opens with it on", "[plugin][dampen-default]")
{
    using namespace dampendefault;
    {
        ScopedFirstState first;
        OspAudioProcessor fresh;
        for (int layer = 0; layer < 3; ++layer)
            CHECK (dampenOn (fresh, layer) == 1.0f);
    }
    OspAudioProcessor p;
    for (int s = 0; s < p.getNumPrograms(); ++s)
    {
        INFO (p.getProgramName (s));
        for (int layer = 0; layer < 3; ++layer)
            p.setParameterValue (OspAudioProcessor::dampenParameterId (layer, "enabled"), 0.0f);
        p.setCurrentProgram (s);
        for (int layer = 0; layer < 3; ++layer)
            CHECK (dampenOn (p, layer) == 1.0f);
    }
}

TEST_CASE ("dampen default: a saved session keeps its own DAMPEN (off stays off)", "[plugin][dampen-default]")
{
    using namespace dampendefault;
    OspAudioProcessor p;
    p.setParameterValue (OspAudioProcessor::dampenParameterId (0, "enabled"), 0.0f);
    p.setParameterValue (OspAudioProcessor::dampenParameterId (1, "enabled"), 1.0f);
    juce::MemoryBlock state;
    p.getStateInformation (state);
    ScopedFirstState first;   // (a new instance opens on Starting State 1; the session wins)
    OspAudioProcessor q;
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    CHECK (dampenOn (q, 0) == 0.0f);
    CHECK (dampenOn (q, 1) == 1.0f);
}
