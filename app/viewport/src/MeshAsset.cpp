#include "fulmetry/viewport/MeshAsset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#define CGLTF_IMPLEMENTATION
#include "cgltf/cgltf.h"

namespace fulmetry::viewport {

namespace {

using F3 = std::array<float, 3>;

struct Reading {
    const char* name;
    bool yUp;
    double scale;  // file units -> mm
};
// glTF's own convention first: it wins ties.
constexpr Reading kReadings[] = {{"m, Y up", true, 1000.0}, {"mm, Z up", false, 1.0},
                                 {"mm, Y up", true, 1.0},    {"m, Z up", false, 1000.0}};

F3 toPart(const F3& g, const Reading& r) {
    // Y up -> Z up is a proper rotation (x, y, z) -> (x, -z, y), so triangle winding is kept.
    float s = float(r.scale);
    return r.yUp ? F3{g[0] * s, -g[2] * s, g[1] * s} : F3{g[0] * s, g[1] * s, g[2] * s};
}

bool isDefaultWhite(const cgltf_material* m) {
    if (!m || !m->has_pbr_metallic_roughness) return true;
    const auto& p = m->pbr_metallic_roughness;
    return p.base_color_texture.texture == nullptr && p.base_color_factor[0] == 1.f && p.base_color_factor[1] == 1.f &&
           p.base_color_factor[2] == 1.f && p.base_color_factor[3] == 1.f;
}

void computeBounds(MeshAsset& m) {
    if (m.positions.empty()) return;
    F3 lo = m.positions[0], hi = m.positions[0];
    for (const auto& p : m.positions)
        for (int i = 0; i < 3; ++i) lo[i] = std::min(lo[i], p[i]), hi[i] = std::max(hi[i], p[i]);
    m.bounds = {{lo[0], lo[1], lo[2]}, {hi[0], hi[1], hi[2]}};
}

void fillMissingNormals(MeshAsset& m, size_t firstVertex) {
    for (size_t v = firstVertex; v < m.normals.size(); ++v) m.normals[v] = {0, 0, 0};
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        uint32_t a = m.indices[i], b = m.indices[i + 1], c = m.indices[i + 2];
        if (a < firstVertex) continue;
        const F3 &pa = m.positions[a], &pb = m.positions[b], &pc = m.positions[c];
        F3 e1{pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]}, e2{pc[0] - pa[0], pc[1] - pa[1], pc[2] - pa[2]};
        F3 n{e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        for (uint32_t v : {a, b, c})
            for (int k = 0; k < 3; ++k) m.normals[v][k] += n[k];
    }
    for (size_t v = firstVertex; v < m.normals.size(); ++v) {
        F3& n = m.normals[v];
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        n = l > 0 ? F3{n[0] / l, n[1] / l, n[2] / l} : F3{0, 0, 1};
    }
}

// Raw triangles in glTF file space with node transforms applied.
Result<MeshAsset> readFile(const std::string& path) {
    cgltf_options opt{};
    cgltf_data* raw = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &raw) != cgltf_result_success)
        return Error{"mesh_parse", "cannot parse glTF " + path};
    std::unique_ptr<cgltf_data, void (*)(cgltf_data*)> data(raw, cgltf_free);
    if (cgltf_load_buffers(&opt, raw, path.c_str()) != cgltf_result_success)
        return Error{"mesh_buffers", "cannot load the buffers of " + path};
    if (cgltf_validate(raw) != cgltf_result_success) return Error{"mesh_invalid", "invalid glTF " + path};

    MeshAsset m;
    auto addMesh = [&](const cgltf_mesh& mesh, const float* world) {
        for (size_t pi = 0; pi < mesh.primitives_count; ++pi) {
            const cgltf_primitive& prim = mesh.primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                if (prim.attributes[a].type == cgltf_attribute_type_position) pos = prim.attributes[a].data;
                if (prim.attributes[a].type == cgltf_attribute_type_normal) nrm = prim.attributes[a].data;
            }
            if (!pos || pos->count == 0) continue;
            const uint32_t base = uint32_t(m.positions.size());
            for (size_t v = 0; v < pos->count; ++v) {
                float p[3] = {0, 0, 0}, n[3] = {0, 0, 1};
                cgltf_accessor_read_float(pos, v, p, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n, 3);
                // Column-major 4x4. Normals take the upper 3x3 (exact for rotations and uniform scale).
                F3 wp, wn;
                for (int r = 0; r < 3; ++r) {
                    wp[r] = world[r] * p[0] + world[4 + r] * p[1] + world[8 + r] * p[2] + world[12 + r];
                    wn[r] = world[r] * n[0] + world[4 + r] * n[1] + world[8 + r] * n[2];
                }
                float l = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
                if (l > 0) wn = {wn[0] / l, wn[1] / l, wn[2] / l};
                m.positions.push_back(wp);
                m.normals.push_back(wn);
            }
            MeshSubset s;
            s.indexOffset = uint32_t(m.indices.size());
            if (prim.indices) {
                for (size_t i = 0; i < prim.indices->count; ++i)
                    m.indices.push_back(base + uint32_t(cgltf_accessor_read_index(prim.indices, i)));
            } else {
                for (size_t i = 0; i < pos->count; ++i) m.indices.push_back(base + uint32_t(i));
            }
            m.indices.resize(s.indexOffset + (m.indices.size() - s.indexOffset) / 3 * 3);
            s.indexCount = uint32_t(m.indices.size()) - s.indexOffset;
            // A mirroring node transform flips the winding back.
            double det = world[0] * (world[5] * world[10] - world[9] * world[6]) -
                         world[4] * (world[1] * world[10] - world[9] * world[2]) +
                         world[8] * (world[1] * world[6] - world[5] * world[2]);
            if (det < 0)
                for (uint32_t i = s.indexOffset; i < s.indexOffset + s.indexCount; i += 3)
                    std::swap(m.indices[i + 1], m.indices[i + 2]);
            if (!nrm) fillMissingNormals(m, base);
            if (const cgltf_material* mat = prim.material) {
                if (mat->name) s.material = mat->name;
                if (mat->has_pbr_metallic_roughness) {
                    const auto& p = mat->pbr_metallic_roughness;
                    s.roughness = p.roughness_factor;
                    s.metallic = p.metallic_factor;
                }
            }
            if (!isDefaultWhite(prim.material)) {
                const float* c = prim.material->pbr_metallic_roughness.base_color_factor;
                s.baseColor = std::array<float, 4>{c[0], c[1], c[2], c[3]};
            }
            if (s.indexCount) m.subsets.push_back(s);
        }
    };
    static const float kIdentity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    bool any = false;
    for (size_t i = 0; i < raw->nodes_count; ++i) {
        if (!raw->nodes[i].mesh) continue;
        float world[16];
        cgltf_node_transform_world(&raw->nodes[i], world);
        addMesh(*raw->nodes[i].mesh, world);
        any = true;
    }
    if (!any)
        for (size_t i = 0; i < raw->meshes_count; ++i) addMesh(raw->meshes[i], kIdentity);
    if (m.indices.empty()) return Error{"mesh_empty", "no triangles in " + path};
    return m;
}

double fitScore(const Aabb& got, const Aabb& expected) {
    Vec3 g = got.size(), e = expected.size();
    double s = 0;
    for (auto [a, b] : {std::pair{g.x, e.x}, std::pair{g.y, e.y}, std::pair{g.z, e.z}})
        s += std::abs(std::log((a + 1.0) / (b + 1.0)));
    return s;
}

}  // namespace

Result<MeshAsset> loadGltf(const std::string& path, const Aabb& expected) {
    auto file = readFile(path);
    if (!file.ok()) return file.error();
    const MeshAsset& raw = file.value();

    const Reading* best = &kReadings[0];
    const Vec3 es = expected.size();
    if (es.x > 0 && es.y > 0 && es.z > 0) {
        double bestScore = std::numeric_limits<double>::max();
        for (const Reading& r : kReadings) {
            MeshAsset probe;
            probe.positions.reserve(raw.positions.size());
            for (const auto& p : raw.positions) probe.positions.push_back(toPart(p, r));
            computeBounds(probe);
            double s = fitScore(probe.bounds, expected);
            if (s < bestScore - 0.05) bestScore = s, best = &r;
        }
    }
    MeshAsset m = file.value();
    for (auto& p : m.positions) p = toPart(p, *best);
    for (auto& n : m.normals) n = toPart(n, Reading{"", best->yUp, 1.0});
    m.units = best->name;
    computeBounds(m);
    return m;
}

MeshAsset boxMesh(const Aabb& b) {
    MeshAsset m;
    const float lo[3] = {float(b.min.x), float(b.min.y), float(b.min.z)};
    const float hi[3] = {float(b.max.x), float(b.max.y), float(b.max.z)};
    for (int axis = 0; axis < 3; ++axis) {
        for (int side = 0; side < 2; ++side) {
            int u = (axis + 1) % 3, v = (axis + 2) % 3;
            F3 n{0, 0, 0};
            n[axis] = side ? 1.f : -1.f;
            uint32_t base = uint32_t(m.positions.size());
            for (int k = 0; k < 4; ++k) {
                F3 p;
                p[axis] = side ? hi[axis] : lo[axis];
                p[u] = (k == 1 || k == 2) ? hi[u] : lo[u];
                p[v] = (k >= 2) ? hi[v] : lo[v];
                m.positions.push_back(p);
                m.normals.push_back(n);
            }
            // (u, v) is right-handed with the axis, so 0-1-2 winds counter-clockwise seen from +axis.
            if (side) m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            else m.indices.insert(m.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
    m.subsets.push_back({0, uint32_t(m.indices.size()), "box", std::nullopt, 0.55f, 0.0f});
    m.bounds = b;
    m.units = "mm, Z up";
    return m;
}

std::optional<MeshHit> intersect(const MeshAsset& m, const Vec3& o, const Vec3& d) {
    // Slab test against the bounds first.
    double t0 = 0, t1 = std::numeric_limits<double>::max();
    const double os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
    const double lo[3] = {m.bounds.min.x, m.bounds.min.y, m.bounds.min.z};
    const double hi[3] = {m.bounds.max.x, m.bounds.max.y, m.bounds.max.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(ds[i]) < 1e-15) {
            if (os[i] < lo[i] - 1e-6 || os[i] > hi[i] + 1e-6) return std::nullopt;
            continue;
        }
        double a = (lo[i] - 1e-6 - os[i]) / ds[i], b = (hi[i] + 1e-6 - os[i]) / ds[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return std::nullopt;
    }
    std::optional<MeshHit> best;
    for (size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        const F3 &a = m.positions[m.indices[i]], &b = m.positions[m.indices[i + 1]], &c = m.positions[m.indices[i + 2]];
        Vec3 A{a[0], a[1], a[2]}, e1 = Vec3{b[0], b[1], b[2]} - A, e2 = Vec3{c[0], c[1], c[2]} - A;
        Vec3 p{d.y * e2.z - d.z * e2.y, d.z * e2.x - d.x * e2.z, d.x * e2.y - d.y * e2.x};
        double det = e1.dot(p);
        if (std::abs(det) < 1e-18) continue;
        double inv = 1.0 / det;
        Vec3 s = o - A;
        double u = s.dot(p) * inv;
        if (u < 0 || u > 1) continue;
        Vec3 q{s.y * e1.z - s.z * e1.y, s.z * e1.x - s.x * e1.z, s.x * e1.y - s.y * e1.x};
        double v = d.dot(q) * inv;
        if (v < 0 || u + v > 1) continue;
        double t = e2.dot(q) * inv;
        if (t <= 1e-9 || (best && t >= best->t)) continue;
        Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        n = n * (1.0 / n.length());
        if (n.dot(d) > 0) n = n * -1.0;
        best = MeshHit{t, n};
    }
    return best;
}

Vec3 rotate(const Quat& q, const Vec3& v) {
    // v' = v + 2w (u x v) + 2 u x (u x v), u = (x, y, z)
    Vec3 u{q.x, q.y, q.z};
    auto cross = [](const Vec3& a, const Vec3& b) {
        return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    };
    Vec3 t = cross(u, v) * 2.0;
    return v + t * q.w + cross(u, t);
}

Vec3 rotateInverse(const Quat& q, const Vec3& v) { return rotate(Quat{q.w, -q.x, -q.y, -q.z}, v); }

}  // namespace fulmetry::viewport
