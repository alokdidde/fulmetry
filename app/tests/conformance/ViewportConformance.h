// Every IViewport (fake and Wicked) must pass this. Call from a TEST_CASE with a factory.
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <memory>

#include "fulmetry/core/IViewport.h"

namespace fulmetry::conformance {

inline void viewportConformance(const std::function<std::unique_ptr<IViewport>()>& make, const Project& project) {
    auto v = make();
    REQUIRE(v->loadProject(project).ok());
    v->resize(640, 400);

    SUBCASE("every part can be picked when isolated") {
        for (const Part* p : project.allParts()) {
            v->isolate(p->id);
            v->flyTo(p->id);
            const Transform t = v->drawnPlacement(p->id);
            auto px = v->project(t.position + p->bounds.center());
            REQUIRE_MESSAGE(px.has_value(), p->id.value);
            auto hit = v->pick(px->first, px->second);
            // A real mesh need not cover the centre of its bounds (a ring of screws, a tub seen through its open
            // top). Then some point inside the bounds must pick it: a 9 x 9 x 9 grid over the box, nearest first.
            for (int i = 0; !hit && i < 9 * 9 * 9; ++i) {
                const Aabb& b = p->bounds;
                const Vec3 f{(i % 9 + 0.5) / 9, (i / 9 % 9 + 0.5) / 9, (i / 81 + 0.5) / 9};
                const Vec3 local{b.min.x + (b.max.x - b.min.x) * f.x, b.min.y + (b.max.y - b.min.y) * f.y,
                                 b.min.z + (b.max.z - b.min.z) * f.z};
                if (auto q = v->project(t.position + local)) hit = v->pick(q->first, q->second);
            }
            REQUIRE_MESSAGE(hit.has_value(), p->id.value);
            CHECK_MESSAGE(hit->part == p->id, p->id.value);
        }
    }
    SUBCASE("hidden parts are not visible or pickable") {
        const Part* p = project.allParts().front();
        v->setHidden(p->id, true);
        CHECK_FALSE(v->isVisible(p->id));
        v->isolate(p->id);
        auto px = v->project(v->drawnPlacement(p->id).position + p->bounds.center());
        if (px) CHECK_FALSE(v->pick(px->first, px->second).has_value());
        v->setHidden(p->id, false);
        CHECK(v->isVisible(p->id));
    }
    SUBCASE("isolate shows one part, clearing it shows all") {
        const auto parts = project.allParts();
        v->isolate(parts[1]->id);
        CHECK(v->isVisible(parts[1]->id));
        CHECK_FALSE(v->isVisible(parts[0]->id));
        v->isolate(std::nullopt);
        for (const Part* p : parts) CHECK(v->isVisible(p->id));
    }
    SUBCASE("explode moves parts and assemble puts them back") {
        const Part* p = project.allParts().front();
        Transform before = v->drawnPlacement(p->id);
        v->setExplode(1.0);
        int moved = 0;
        for (const Part* q : project.allParts())
            if (!(v->drawnPlacement(q->id) == Transform{})) moved++;
        CHECK(moved > 0);
        v->setExplode(0.0);
        CHECK(v->drawnPlacement(p->id) == before);
    }
    SUBCASE("a simulation pose moves a part, and clearing it puts the part back") {
        const Part* p = project.allParts().front();
        const Transform before = v->drawnPlacement(p->id);
        Transform pose;
        pose.position = {1234, -567, 30};
        pose.rotation = {0.7071067811865476, 0, 0, 0.7071067811865476};
        StageEnvironment room;
        room.floorColor = "#c9a47e";
        room.props.push_back({"Rug", {{300, -1000, 0}, {1500, 1000, 4}}, "#1e6fe8"});
        v->setEnvironment(room);
        v->setPartPose(p->id, pose);
        CHECK(v->drawnPlacement(p->id) == pose);
        v->setExplode(1.0);  // a posed part is where the physics says, exploded or not
        CHECK(v->drawnPlacement(p->id) == pose);
        v->setExplode(0.0);
        v->setPartPose(p->id, std::nullopt);
        v->setEnvironment(std::nullopt);
        CHECK(v->drawnPlacement(p->id) == before);
    }
    SUBCASE("render produces an image of the asked size with something drawn") {
        auto img = v->renderToImage(64, 40);
        REQUIRE(img.ok());
        CHECK(img->width == 64);
        CHECK(img->height == 40);
        CHECK(img->rgba.size() == size_t(64 * 40 * 4));
        size_t nonWhite = 0;
        for (size_t i = 0; i < img->rgba.size(); i += 4)
            if (img->rgba[i] != 255 || img->rgba[i + 1] != 255 || img->rgba[i + 2] != 255) ++nonWhite;
        CHECK(nonWhite > 0);
    }
}

}  // namespace fulmetry::conformance
