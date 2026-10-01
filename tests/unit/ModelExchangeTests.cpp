#include "model/ModelExchange.h"

#include <catch2/catch_test_macros.hpp>

using osp::ModelExchange;

namespace
{
    struct Model
    {
        std::uint64_t generation = 0;
        int value = 0;
    };
}

TEST_CASE ("model exchange: hand-over and deferred destruction", "[unit][model]")
{
    ModelExchange<Model> exchange;
    CHECK (exchange.takePending() == nullptr);

    auto a = std::make_shared<const Model> (Model { 1, 10 });
    std::weak_ptr<const Model> weakA = a;
    exchange.publish (a);
    a.reset();

    const Model* current = exchange.takePending();
    REQUIRE (current != nullptr);
    CHECK (current->value == 10);
    CHECK (exchange.takePending() == nullptr); // delivered exactly once
    exchange.publishOldestInUse (current->generation);

    // B published; audio has not picked it up yet -> A must stay alive.
    exchange.publish (std::make_shared<const Model> (Model { 2, 20 }));
    exchange.collectGarbage();
    CHECK_FALSE (weakA.expired());

    // C supersedes B before the audio thread ever saw B.
    exchange.publish (std::make_shared<const Model> (Model { 3, 30 }));
    current = exchange.takePending();
    REQUIRE (current != nullptr);
    CHECK (current->value == 30);

    // A voice still plays A: audio reports generation 1 as oldest in use.
    exchange.publishOldestInUse (1);
    exchange.collectGarbage();
    CHECK_FALSE (weakA.expired());
    CHECK (exchange.ownedCount() == 3);

    // Voices on A finished: only C remains.
    exchange.publishOldestInUse (3);
    exchange.collectGarbage();
    CHECK (weakA.expired());
    CHECK (exchange.ownedCount() == 1);
    CHECK (exchange.latestModel()->value == 30);
}
