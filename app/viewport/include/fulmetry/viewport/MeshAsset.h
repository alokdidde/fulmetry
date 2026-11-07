// Part meshes for the viewport: binary glTF loading, box fallback and ray picking. No GPU, no Wicked: this
// builds and is tested in every configuration, and WickedViewport turns a MeshAsset into a Wicked mesh.
#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fulmetry/core/Math.h"
#include "fulmetry/core/Result.h"

namespace fulmetry::viewport {

struct MeshSubset {
    uint32_t indexOffset = 0, indexCount = 0;
    std::string material;                          // glTF material name, for debugging
    std::optional<std::array<float, 4>> baseColor;  // linear RGBA; nullopt = use Part.color
    float roughness = 0.55f, metallic = 0.0f;
};

// Triangles in the part's own frame: millimetres, right-handed, Z up (the frame of Part.bounds).
// Triangles wind counter-clockwise seen from outside.
struct MeshAsset {
    std::vector<std::array<float, 3>> positions, normals;
    std::vector<uint32_t> indices;
    std::vector<MeshSubset> subsets;
    Aabb bounds;
    std::string units;  // how the file was read: "m, Y up" (glTF), "mm, Z up", "mm, Y up" or "m, Z up"

    size_t triangleCount() const { return indices.size() / 3; }
};

// Loads a .glb or .gltf. Node transforms are applied. glTF says metres and +Y up (what build123d writes); a file
// written in millimetres or Z up is recognised by comparing its extent with `expected` (Part.bounds), and the
// reading that fits best wins, glTF's own on a tie. Primitives that are not triangles are skipped.
Result<MeshAsset> loadGltf(const std::string& path, const Aabb& expected = {});

// A plain box over `b`, for parts without a mesh.
MeshAsset boxMesh(const Aabb& b);

// Nearest hit of a ray (part frame, any direction length) with the mesh. Normal faces the ray origin.
struct MeshHit {
    double t = 0;  // in units of `dir`
    Vec3 normal;
};
std::optional<MeshHit> intersect(const MeshAsset& mesh, const Vec3& origin, const Vec3& dir);

// Rotations by a unit quaternion (Fulmetry's Quat: w, x, y, z).
Vec3 rotate(const Quat& q, const Vec3& v);
Vec3 rotateInverse(const Quat& q, const Vec3& v);

}  // namespace fulmetry::viewport
