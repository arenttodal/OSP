// The host-facing parameter schema (IDs, version hints, names, ranges, defaults, choices,
// value texts) is frozen: automation and sessions depend on it. The shared ANDOR interface
// and the Instrument Shell build the same schema, so it is pinned here against a golden
// listing. Intentional additions: OSP_UPDATE_GOLDEN=1 osp_plugin_tests "[parameter-schema]".
// Included at the end of PluginProcessorTests.cpp.

#include "../support/ParameterSchema.h"

TEST_CASE ("plugin: ANDOR OSP's parameter schema is unchanged (IDs, order, ranges, defaults, texts)", "[plugin][parameter-schema]")
{
    OspAudioProcessor p;
    parameterschema::checkAgainstGolden (parameterschema::describe (p), "osp-parameter-schema.txt");
}
