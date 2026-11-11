// The shared FX library's contracts (shared/andor-ui Fx*): the parameters it builds for ANDOR
// ANALOG are, line for line, the ones ANALOG ships (analog product/golden/parameter-schema.txt,
// its `analog.fx.*` lines, copied to tests/plugin/golden/analog-fx-parameter-schema.txt), so
// ANALOG can repoint to the shared code without one automation lane moving.
// Included at the end of PluginProcessorTests.cpp.

#include "FxParameters.h"

namespace
{
    /** A processor holding only the FX library's parameters (for their listing). */
    class FxOnlyProcessor final : public juce::AudioProcessor
    {
    public:
        FxOnlyProcessor (const juce::String& prefix, int hint, int retroHint)
            : state (*this, nullptr, "FX", [&] {
                  juce::AudioProcessorValueTreeState::ParameterLayout layout;
                  osp::plugin::fxparams::addTo (layout, prefix, hint, retroHint);
                  return layout;
              }())
        {
        }
        const juce::String getName() const override { return "FX"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}
        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        juce::AudioProcessorValueTreeState state;
    };
}

TEST_CASE ("fx shared: the shared FX parameters, built for ANDOR ANALOG, are ANALOG's 379 exactly", "[plugin][fx][parameter-schema]")
{
    FxOnlyProcessor p ("analog.", 28, 29);
    const auto expected = parameterschema::ospGolden ("analog-fx-parameter-schema.txt");
    REQUIRE (expected.size() >= 379);
    parameterschema::compareListings (parameterschema::describe (p), expected);
}

TEST_CASE ("fx shared: every spec writes its own setting and a Reader reads them all", "[plugin][fx]")
{
    using namespace osp::plugin;
    FxOnlyProcessor p ("", 30, 30);
    fxparams::Reader reader;
    reader.attach (p.state, "");
    // Every parameter is found under OSP's IDs.
    int count = 0;
    for (int t = 4; t < osp::fx::typeCount; ++t)
        for (int slot = 0; slot < osp::fx::capacity[static_cast<std::size_t> (t)]; ++slot)
            for (const auto& spec : fxparams::specsOf (static_cast<osp::fx::Type> (t)))
            {
                REQUIRE (p.state.getParameter (fxparams::idOf ("", { static_cast<osp::fx::Type> (t), slot }, spec.name)) != nullptr);
                ++count;
            }
    CHECK (count == 379);
    CHECK (p.getParameters().size() == 379);
    // Defaults read back as fx::Settings' own defaults (the specs and the DSP agree).
    osp::fx::Settings read;
    read.retro[1].mix = 0.123f;
    reader.read (read);
    const osp::fx::Settings defaults;
    CHECK (read.retro[1].mix == defaults.retro[1].mix);
    CHECK (read.spread[0].mix == defaults.spread[0].mix);
    // A moved parameter arrives in its slot's settings.
    auto* mix = p.state.getParameter ("fx.retro.2.mix");
    REQUIRE (mix != nullptr);
    mix->setValueNotifyingHost (mix->convertTo0to1 (25.0f));
    reader.read (read);
    CHECK (read.retro[1].mix == Catch::Approx (0.25f));
    CHECK (read.retro[0].mix == defaults.retro[0].mix);
}
