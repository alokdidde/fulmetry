#include <doctest/doctest.h>

#include <thread>

#include "fulmetry/core/Events.h"

using namespace fulmetry;

TEST_CASE("events are delivered in order on drain") {
    EventBus bus;
    std::vector<std::string> got;
    bus.subscribe([&](const Event& e) {
        if (auto* b = std::get_if<Banner>(&e)) got.push_back(b->text);
    });
    bus.publish(Banner{"a"});
    bus.publish(Banner{"b"});
    CHECK(got.empty());
    CHECK(bus.drain() == 2);
    CHECK(got == std::vector<std::string>{"a", "b"});
}

TEST_CASE("publishing from many threads loses nothing") {
    EventBus bus;
    int n = 0;
    bus.subscribe([&](const Event&) { ++n; });
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&] { for (int i = 0; i < 250; ++i) bus.publish(Banner{"x"}); });
    for (auto& t : ts) t.join();
    bus.drain();
    CHECK(n == 1000);
}

TEST_CASE("unsubscribe stops delivery") {
    EventBus bus;
    int n = 0;
    int id = bus.subscribe([&](const Event&) { ++n; });
    bus.unsubscribe(id);
    bus.publish(Banner{"x"});
    bus.drain();
    CHECK(n == 0);
}
