// Exact geometry inside the app. Real: app/cad (S3, OCCT). Fake: FakeCadKernel (boxes from part bounds).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry {

struct Mesh {
    std::vector<float> positions;     // xyz, mm
    std::vector<float> normals;       // xyz
    std::vector<uint32_t> indices;    // triangles
    std::vector<uint32_t> triangleFace;  // per triangle: index into faces
    std::vector<FaceId> faces;
};

struct MassProperties {
    double volumeMm3 = 0;
    double massG = 0;
    Vec3 centroid;
};

struct Plane { Vec3 origin; Vec3 normal{0, 0, 1}; };
using Polyline = std::vector<Vec3>;

class ICadKernel {
public:
    virtual ~ICadKernel() = default;
    // Loads a part's exact shape. path is a STEP file; an empty path asks the kernel to use part.bounds.
    virtual Status loadPart(const Part& part, const std::string& path) = 0;
    virtual Result<Mesh> tessellate(const PartId& part, double linearToleranceMm) const = 0;
    virtual Result<Aabb> boundingBox(const PartId& part) const = 0;
    virtual Result<MassProperties> massProperties(const PartId& part, double densityGPerCm3) const = 0;
    virtual Result<double> minDistance(const PartId& a, const PartId& b) const = 0;  // mm, 0 when touching
    virtual Result<std::vector<Polyline>> section(const PartId& part, const Plane& plane) const = 0;
};

}  // namespace fulmetry
