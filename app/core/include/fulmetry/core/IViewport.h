// The 3D stage. Real implementation: app/viewport (S2, Wicked Engine inside a Raven GUI viewport).
// Fake: app/tests/fakes/FakeViewport.h. Both must pass app/tests/conformance/ViewportConformance.h.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry {

struct PickHit {
    PartId part;
    std::optional<FaceId> face;  // filled when a CAD kernel is attached
    Vec3 point;                  // mm, world
    Vec3 normal;
};

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4
};

enum class StageStyle { HighKey, Realistic };

// Simulate's room around the project: a floor colour and boxes standing on it (rug, wall, sofa). World frame,
// mm, Z up, the floor at z = 0.
struct StageProp {
    std::string name;   // "Rug, 8 mm pile"
    Aabb box;
    std::string color;  // "#rrggbb"
};
struct StageEnvironment {
    std::string floorColor = "#ffffff";
    std::vector<StageProp> props;
};

class IViewport {
public:
    virtual ~IViewport() = default;

    // Scene
    virtual Status loadProject(const Project& project) = 0;  // one entity per part
    virtual void showObject(const std::optional<ObjectId>& object) = 0;  // nullopt = All objects view

    // Selection and visibility
    virtual void setSelected(const std::optional<PartId>& part) = 0;
    virtual void setHovered(const std::optional<PartId>& part) = 0;
    virtual void setHidden(const PartId& part, bool hidden) = 0;
    virtual void isolate(const std::optional<PartId>& part) = 0;
    virtual bool isVisible(const PartId& part) const = 0;

    // View modes (0 = assembled, 1 = fully exploded)
    virtual void setExplode(double amount) = 0;
    virtual void setXray(bool on) = 0;
    virtual void setJointsVisible(bool on) = 0;
    virtual void setStyle(StageStyle style) = 0;
    virtual void setSlicePlane(const std::optional<double>& heightMm) = 0;
    // Simulate: the room (nullopt: the workbench's white ground), and where the physics has a part now (world,
    // mm). A pose overrides the part's placement, object offset and explode; nullopt puts it back.
    virtual void setEnvironment(const std::optional<StageEnvironment>& environment) = 0;
    virtual void setPartPose(const PartId& part, const std::optional<Transform>& pose) = 0;

    // Part placement as drawn (after explode); used by tests and by pins.
    virtual Transform drawnPlacement(const PartId& part) const = 0;

    // Camera
    virtual CameraState camera() const = 0;
    virtual void setCamera(const CameraState& camera) = 0;
    virtual void flyTo(const PartId& part) = 0;
    virtual void resetView() = 0;

    // Picking in pixel coordinates of the current viewport size.
    virtual void resize(int width, int height) = 0;
    virtual std::optional<PickHit> pick(int x, int y) const = 0;
    // Pixel position of a world point, for pins and labels. nullopt when behind the camera.
    virtual std::optional<std::pair<int, int>> project(const Vec3& world) const = 0;

    // Frame loop and offscreen rendering (agents get renders of the anchor; tests compare golden images).
    virtual void update(double dtSeconds) = 0;
    virtual Result<Image> renderToImage(int width, int height) = 0;
};

}  // namespace fulmetry
