#include <doctest/doctest.h>

#include "TestSupport.h"

using namespace fulmetry;

TEST_CASE("rover fixture loads") {
    Project p = test::rover();
    CHECK(p.name == "Desk rover");
    REQUIRE(p.objects.size() == 2);
    CHECK(p.objects[0].parts.size() == 13);
    CHECK(p.findPart(PartId("part:rover/wheel-l"))->kind == PartKind::Printed);
    CHECK(p.findPart(PartId("part:rover/camera-head"))->joint->maxDeg == 80);
    CHECK(p.links.at(0).label == "ESP-NOW 2.4 GHz");
    CHECK(p.checks.size() == 5);
    CHECK(p.comments.at(0).anchor.point.has_value());
}

TEST_CASE("save is stable and round-trips") {
    Project p = test::rover();
    std::string a = serializeProject(p);
    auto q = parseProject(a);
    REQUIRE(q.ok());
    CHECK(serializeProject(q.value()) == a);
}

TEST_CASE("bad manifests are errors") {
    CHECK(parseProject("{").error().code == "invalid_manifest");
    CHECK_FALSE(parseProject(R"({"schemaVersion":2,"name":"x","objects":[]})").ok());
    CHECK(loadProject("/nope/fulmetry.project.json").error().code == "not_found");
}

TEST_CASE("enum string forms match the schema") {
    CHECK(std::string(toName(PartKind::Hardware)) == "hardware");
    CHECK(partKindFromString("board") == PartKind::Board);
    CHECK_FALSE(partKindFromString("widget").has_value());
    CHECK(verdictFromString("unresolved") == Verdict::Unresolved);
}
