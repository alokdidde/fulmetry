// OCCT implementation of ICadKernel (S3). Built only when FULMETRY_WITH_OCCT is ON.
// No OCCT types leak through this header; the shapes live behind a pimpl.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fulmetry/core/ICadKernel.h"

namespace fulmetry::cad {

// Frames: tessellate, boundingBox, massProperties and section answer in the part's local frame (the frame
// of the STEP file or of Part.bounds). minDistance places both parts with Part.placement first.
// Face ids are "<part id>#<n>", n counting faces in TopExp::MapShapes order from 0.
class OcctCadKernel : public ICadKernel {
public:
    OcctCadKernel();
    ~OcctCadKernel() override;
    OcctCadKernel(const OcctCadKernel&) = delete;
    OcctCadKernel& operator=(const OcctCadKernel&) = delete;

    Status loadPart(const Part& part, const std::string& path) override;
    Result<Mesh> tessellate(const PartId& part, double linearToleranceMm) const override;
    Result<Aabb> boundingBox(const PartId& part) const override;
    Result<MassProperties> massProperties(const PartId& part, double densityGPerCm3) const override;
    Result<double> minDistance(const PartId& a, const PartId& b) const override;
    Result<std::vector<Polyline>> section(const PartId& part, const Plane& plane) const override;

    // Writes a loaded part's shape (local frame) to a STEP file. Used by tests and by export.
    Status writeStep(const PartId& part, const std::string& path) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fulmetry::cad
