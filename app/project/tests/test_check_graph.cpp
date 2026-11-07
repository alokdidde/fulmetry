#include <doctest/doctest.h>

#include "TestSupport.h"
#include "fulmetry/project/CheckGraph.h"

using namespace fulmetry;

namespace {

const PartId kWheelL("part:rover/wheel-l");
const PartId kWheelR("part:rover/wheel-r");
const CheckId kRug("chk:rover/rug");

// Returns a copy: a reference return trips GCC's -Wdangling-reference on the string argument.
Check check(const Project& p, const std::string& id) {
    for (const auto& c : p.checks)
        if (c.id.value == id) return c;
    throw std::runtime_error("no check " + id);
}

}  // namespace

TEST_CASE("check graph: checksFor lists the checks that read a part") {
    Project p = test::rover();
    CheckGraph g(p);
    CHECK(g.checksFor(kWheelL) == std::vector<CheckId>{kRug});
    CHECK(g.checksFor(PartId("part:rover/main-board")) ==
          std::vector<CheckId>{CheckId("chk:rover/erc"), CheckId("chk:rover/fit")});
    CHECK(g.checksFor(PartId("part:rover/top-shell")).empty());
}

TEST_CASE("check graph: editing wheel-l flips the rug check, and only checks that list wheel-l") {
    Project p = test::rover();
    const Project before = p;
    CheckGraph g(p);

    auto changed = g.partChanged(kWheelL, "sha256:aaa");
    REQUIRE(changed.ok());
    CHECK(changed.value() == std::vector<CheckId>{kRug});

    const Check rug = check(p, "chk:rover/rug");
    CHECK(rug.verdict == Verdict::Unresolved);
    CHECK(rug.reason == "Changed: Left wheel");
    CHECK(rug.inputHashes.at(kWheelL.value) == "sha256:aaa");
    CHECK(rug.inputHashes.count(kWheelR.value) == 0);

    REQUIRE(p.checks.size() == before.checks.size());
    for (size_t i = 0; i < p.checks.size(); ++i) {
        if (p.checks[i].id == kRug) continue;
        CAPTURE(p.checks[i].id.value);
        CHECK(p.checks[i].verdict == before.checks[i].verdict);
        CHECK(p.checks[i].reason == before.checks[i].reason);
        CHECK(p.checks[i].inputHashes == before.checks[i].inputHashes);
    }
    CHECK(check(p, "chk:rover/tilt").verdict == Verdict::Supported);
}

TEST_CASE("check graph: the same hash twice is a no-op") {
    Project p = test::rover();
    CheckGraph g(p);
    REQUIRE(g.partChanged(kWheelL, "sha256:aaa").ok());
    REQUIRE(g.recordVerdict(kRug, Verdict::Supported, "80 mm wheels hold speed").ok());

    auto again = g.partChanged(kWheelL, "sha256:aaa");
    REQUIRE(again.ok());
    CHECK(again.value().empty());
    CHECK(check(p, "chk:rover/rug").verdict == Verdict::Supported);
    CHECK(check(p, "chk:rover/rug").reason == "80 mm wheels hold speed");

    auto next = g.partChanged(kWheelL, "sha256:bbb");
    REQUIRE(next.ok());
    CHECK(next.value() == std::vector<CheckId>{kRug});
}

TEST_CASE("check graph: recording a verdict stores the current input hashes") {
    Project p = test::rover();
    CheckGraph g(p);
    REQUIRE(g.partChanged(kWheelL, "sha256:aaa").ok());
    REQUIRE(g.partChanged(kWheelR, "sha256:bbb").ok());

    REQUIRE(g.recordVerdict(kRug, Verdict::Refuted, "stalled twice").ok());
    const Check rug = check(p, "chk:rover/rug");
    CHECK(rug.verdict == Verdict::Refuted);
    CHECK(rug.reason == "stalled twice");
    CHECK(rug.inputHashes == std::map<std::string, std::string>{{kWheelL.value, "sha256:aaa"},
                                                                 {kWheelR.value, "sha256:bbb"}});

    // Inputs with no known hash are left out rather than stored empty.
    REQUIRE(g.recordVerdict(CheckId("chk:rover/erc"), Verdict::Supported, "").ok());
    CHECK(check(p, "chk:rover/erc").inputHashes.empty());
}

TEST_CASE("check graph: errors") {
    Project p = test::rover();
    CheckGraph g(p);
    CHECK(g.partChanged(PartId("part:rover/nope"), "x").error().code == "not_found");
    CHECK(g.recordVerdict(CheckId("chk:nope"), Verdict::Supported, "").error().code == "not_found");
    CHECK(g.recordVerdict(kRug, Verdict::Unresolved, "").error().code == "invalid_argument");
}

TEST_CASE("check graph: save and load keep inputHashes and seed the graph") {
    Project p = test::rover();
    {
        CheckGraph g(p);
        REQUIRE(g.partChanged(kWheelL, "sha256:aaa").ok());
        REQUIRE(g.partChanged(kWheelR, "sha256:bbb").ok());
        REQUIRE(g.recordVerdict(kRug, Verdict::Supported, "ok").ok());
    }
    std::string text = serializeProject(p);
    auto q = parseProject(text);
    REQUIRE(q.ok());
    CHECK(check(q.value(), "chk:rover/rug").inputHashes == check(p, "chk:rover/rug").inputHashes);
    CHECK(serializeProject(q.value()) == text);

    Project loaded = q.value();
    CheckGraph g(loaded);
    CHECK(g.currentHash(kWheelL) == "sha256:aaa");
    auto same = g.partChanged(kWheelL, "sha256:aaa");
    REQUIRE(same.ok());
    CHECK(same.value().empty());
    CHECK(check(loaded, "chk:rover/rug").verdict == Verdict::Supported);
}
