// Ported from ANDOR Analog (tests/FxGraphTests.cpp): the FX graph's saved form, its checks and
// its edits (src/engine/fx, shared). The route cases, driven through Analog's synth there, are
// OSP's own here (FxRouteTests.cpp, through InstrumentEngine).
#include "engine/fx/Effects.h"
#include "engine/fx/FxEngine.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>

using namespace osp;
using fx::Graph;
using fx::InstanceId;
using fx::Law;
using fx::Node;
using fx::Type;

namespace
{
    Node fxNode (Type t) { return Node::effect ({ t, 0 }); }

    Graph custom (std::vector<Node> chain)
    {
        Graph g;
        g.custom = true;
        g.chain = std::move (chain);
        return g;
    }
}

TEST_CASE ("fx graph: the saved form round-trips; CLASSIC is drawn as DRIVE, MOVEMENT, ECHO || SPACE (send)", "[fx][graph]")
{
    const auto classic = fx::classicGraph();
    CHECK_FALSE (classic.custom);
    CHECK (fx::toText (classic) == "v1 classic fx drive.1 on fx movement.1 on par send 2 lane 1 { fx echo.1 on } lane 1 { fx space.1 on }");
    Graph back;
    REQUIRE (fx::fromText (fx::toText (classic), back));
    CHECK (back == classic);

    auto g = custom ({ fxNode (Type::echo), fxNode (Type::drive),
                       Node::parallel ({ fxNode (Type::movement) }, { Node::effect ({ Type::space, 0 }) }, Law::linear) });
    g.chain[1].bypassed = true;
    g.chain[2].gains = { 0.3f, 0.7f };
    REQUIRE (fx::validate (g).empty());
    Graph read;
    REQUIRE (fx::fromText (fx::toText (g), read));
    CHECK (read == g);
    CHECK (fx::toText (read) == fx::toText (g));

    // Nonsense is refused and leaves the graph alone.
    Graph untouched = classic;
    for (const char* bad : { "", "v2 custom", "v1 custom fx nothing.1 on", "v1 custom fx eq.9 on", "v1 custom par send 2 lane 1 { fx echo.1 on }",
                             "v1 custom fx echo.1 on fx echo.1 on", "v1 custom par send 2 lane x { } lane 1 { }" })
    {
        INFO (bad);
        CHECK_FALSE (fx::fromText (bad, untouched));
        CHECK (untouched == classic);
    }
}

TEST_CASE ("fx graph: validation - once each, the pools, lanes, depth, crossovers", "[fx][graph]")
{
    CHECK (fx::validate (fx::classicGraph()).empty());
    CHECK_FALSE (fx::validate (custom ({ fxNode (Type::echo), fxNode (Type::echo) })).empty());
    CHECK_FALSE (fx::validate (custom ({ Node::effect ({ Type::drive, 1 }) })).empty());   // one DRIVE
    CHECK (fx::validate (custom ({ Node::effect ({ Type::eq, 0 }), Node::effect ({ Type::eq, 1 }) })).empty());
    CHECK_FALSE (fx::validate (custom ({ Node::effect ({ Type::eq, 2 }) })).empty());

    auto par = Node::parallel ({}, {});
    par.lanes.emplace_back();
    par.gains.push_back (0.0f);
    CHECK_FALSE (fx::validate (custom ({ par })).empty());   // three lanes in a parallel group
    auto negative = Node::parallel ({}, {});
    negative.gains = { -0.5f, 1.5f };
    CHECK_FALSE (fx::validate (custom ({ negative })).empty());

    // Five groups inside each other: too deep.
    Node deep = Node::parallel ({}, {});
    for (int i = 0; i < 5; ++i)
        deep = Node::parallel ({ deep }, {});
    CHECK_FALSE (fx::validate (custom ({ deep })).empty());

    auto split = Node::splitter (fx::SplitKind::bands3, { {}, {}, {} });
    split.lowHz = 3000.0f;
    split.highHz = 1000.0f;
    CHECK_FALSE (fx::validate (custom ({ split })).empty());   // crossovers out of order
    split.lowHz = 200.0f;
    split.highHz = 2000.0f;
    CHECK (fx::validate (custom ({ split })).empty());
    // Valid, but not played yet: compile refuses it (never approximated as a plain parallel).
    CHECK (fx::compile (custom ({ split }), 1) == nullptr);

    // An effect without a processor in this build is not compiled (not insertable).
    for (int t = 0; t < fx::typeCount; ++t)
    {
        const auto type = static_cast<Type> (t);
        const auto compiled = fx::compile (custom ({ fxNode (type) }), 1);
        CHECK ((compiled != nullptr) == fx::implemented (type));
    }
}

TEST_CASE ("fx graph: edits - move, stack, unstack, nudge, remove, bypass; each makes the route CUSTOM", "[fx][graph][edit]")
{
    const InstanceId drive { Type::drive, 0 }, movement { Type::movement, 0 }, echo { Type::echo, 0 }, space { Type::space, 0 };
    auto g = fx::classicGraph();
    // ECHO to the front (out of the pair: the pair dissolves into SPACE alone).
    REQUIRE (fx::moveEffect (g, echo, {}, 0));
    CHECK (g.custom);
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx movement.1 on fx space.1 on");
    // SPACE between DRIVE and MOVEMENT.
    REQUIRE (fx::moveEffect (g, space, {}, 2));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 on fx movement.1 on");
    // Stack ECHO with MOVEMENT: a linear pair in MOVEMENT's place.
    REQUIRE (fx::stackEffect (g, echo, movement));
    CHECK (fx::toText (g) == "v1 custom fx drive.1 on fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on }");
    // A third onto the pair goes into the target's lane, after it.
    REQUIRE (fx::stackEffect (g, drive, echo));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on fx drive.1 on }");
    // Out again, right after the pair.
    REQUIRE (fx::unstackEffect (g, drive));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on } fx drive.1 on");
    // Nudge DRIVE left: it swaps with the pair; the first can go no further left.
    REQUIRE (fx::nudgeEffect (g, drive, -1));
    CHECK (fx::toText (g) == "v1 custom fx space.1 on fx drive.1 on par linear 2 lane 0.5 { fx movement.1 on } lane 0.5 { fx echo.1 on }");
    CHECK_FALSE (fx::nudgeEffect (g, space, -1));
    // Moving ECHO out of its lane dissolves the pair; removing works the same way.
    REQUIRE (fx::moveEffect (g, echo, {}, 0));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx space.1 on fx drive.1 on fx movement.1 on");
    REQUIRE (fx::removeEffect (g, drive));
    CHECK_FALSE (fx::contains (g, drive));
    CHECK_FALSE (fx::removeEffect (g, drive));
    REQUIRE (fx::insertEffect (g, {}, 1, Node::effect (drive)));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 on fx movement.1 on");
    REQUIRE (fx::setBypassed (g, space, true));
    CHECK (fx::toText (g) == "v1 custom fx echo.1 on fx drive.1 on fx space.1 off fx movement.1 on");
    // Refused: inserting a second DRIVE; nothing changes.
    const auto before = g;
    CHECK_FALSE (fx::insertEffect (g, {}, 0, Node::effect (drive)));
    CHECK (g == before);
}
