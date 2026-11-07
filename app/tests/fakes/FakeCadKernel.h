// Fake CAD kernel: every part is its bounds box. Same answers the conformance suite expects from OCCT
// for box-shaped fixture parts.
#pragma once
#include <algorithm>
#include <map>

#include "fulmetry/core/ICadKernel.h"

namespace fulmetry::fakes {

class FakeCadKernel : public ICadKernel {
public:
    Status loadPart(const Part& part, const std::string&) override {
        parts_[part.id] = part;
        return Ok{};
    }
    Result<Mesh> tessellate(const PartId& id, double) const override {
        auto it = parts_.find(id);
        if (it == parts_.end()) return Error{"not_found", id.value};
        const Aabb& b = it->second.bounds;
        Mesh m;
        // 6 faces, 2 triangles each, 4 vertices per face (flat normals).
        const Vec3 n[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int f = 0; f < 6; ++f) {
            Vec3 c[4];
            for (int k = 0; k < 4; ++k) {
                int a = k & 1, bb = (k >> 1) & 1;
                Vec3 v;
                if (n[f].x != 0) v = {n[f].x > 0 ? b.max.x : b.min.x, a ? b.max.y : b.min.y, bb ? b.max.z : b.min.z};
                if (n[f].y != 0) v = {a ? b.max.x : b.min.x, n[f].y > 0 ? b.max.y : b.min.y, bb ? b.max.z : b.min.z};
                if (n[f].z != 0) v = {a ? b.max.x : b.min.x, bb ? b.max.y : b.min.y, n[f].z > 0 ? b.max.z : b.min.z};
                c[k] = v;
            }
            uint32_t base = uint32_t(m.positions.size() / 3);
            for (auto& v : c) {
                m.positions.insert(m.positions.end(), {float(v.x), float(v.y), float(v.z)});
                m.normals.insert(m.normals.end(), {float(n[f].x), float(n[f].y), float(n[f].z)});
            }
            m.indices.insert(m.indices.end(), {base, base + 1, base + 3, base, base + 3, base + 2});
            m.triangleFace.insert(m.triangleFace.end(), {uint32_t(f), uint32_t(f)});
            m.faces.emplace_back(id.value + "#" + std::to_string(f));
        }
        return m;
    }
    Result<Aabb> boundingBox(const PartId& id) const override {
        auto it = parts_.find(id);
        if (it == parts_.end()) return Error{"not_found", id.value};
        return it->second.bounds;
    }
    Result<MassProperties> massProperties(const PartId& id, double density) const override {
        auto it = parts_.find(id);
        if (it == parts_.end()) return Error{"not_found", id.value};
        Vec3 s = it->second.bounds.size();
        double v = s.x * s.y * s.z;
        return MassProperties{v, v / 1000.0 * density, it->second.bounds.center()};
    }
    Result<double> minDistance(const PartId& a, const PartId& b) const override {
        auto ia = parts_.find(a), ib = parts_.find(b);
        if (ia == parts_.end() || ib == parts_.end()) return Error{"not_found", "part"};
        auto world = [](const Part& p) {
            return Aabb{p.placement.position + p.bounds.min, p.placement.position + p.bounds.max};
        };
        Aabb x = world(ia->second), y = world(ib->second);
        auto gap = [](double a0, double a1, double b0, double b1) { return std::max({0.0, b0 - a1, a0 - b1}); };
        Vec3 g{gap(x.min.x, x.max.x, y.min.x, y.max.x), gap(x.min.y, x.max.y, y.min.y, y.max.y),
               gap(x.min.z, x.max.z, y.min.z, y.max.z)};
        return g.length();
    }
    Result<std::vector<Polyline>> section(const PartId& id, const Plane& plane) const override {
        auto it = parts_.find(id);
        if (it == parts_.end()) return Error{"not_found", id.value};
        const Aabb& b = it->second.bounds;
        if (plane.normal.x != 0 || plane.normal.y != 0) return Error{"unsupported", "fake cuts horizontal planes only"};
        double z = plane.origin.z;
        if (z < b.min.z || z > b.max.z) return std::vector<Polyline>{};
        return std::vector<Polyline>{{{b.min.x, b.min.y, z}, {b.max.x, b.min.y, z}, {b.max.x, b.max.y, z},
                                      {b.min.x, b.max.y, z}, {b.min.x, b.min.y, z}}};
    }

private:
    std::map<PartId, Part> parts_;
};

}  // namespace fulmetry::fakes
