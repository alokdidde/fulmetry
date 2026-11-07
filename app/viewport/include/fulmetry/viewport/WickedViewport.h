// The real 3D stage: IViewport on Wicked Engine. Built only when FULMETRY_WITH_WICKED is ON.
// This header has no Wicked includes; the engine lives behind a pimpl in WickedViewport.cpp.
#pragma once
#include <memory>

#include "fulmetry/core/IViewport.h"

namespace fulmetry::viewport {

struct WickedOptions {
    // Frames rendered per renderToImage call. Temporal effects (TAA, eye adaption) need a few to settle.
    int warmupFrames = 4;
    bool outlines = true;  // dark edges from Wicked's depth outline pass
};

class WickedViewport : public IViewport {
public:
    explicit WickedViewport(WickedOptions options = {});
    ~WickedViewport() override;
    WickedViewport(const WickedViewport&) = delete;
    WickedViewport& operator=(const WickedViewport&) = delete;

    Status loadProject(const Project& project) override;
    void showObject(const std::optional<ObjectId>& object) override;

    void setSelected(const std::optional<PartId>& part) override;
    void setHovered(const std::optional<PartId>& part) override;
    void setHidden(const PartId& part, bool hidden) override;
    void isolate(const std::optional<PartId>& part) override;
    bool isVisible(const PartId& part) const override;

    void setExplode(double amount) override;
    void setXray(bool on) override;
    void setJointsVisible(bool on) override;
    void setStyle(StageStyle style) override;
    void setSlicePlane(const std::optional<double>& heightMm) override;

    Transform drawnPlacement(const PartId& part) const override;

    CameraState camera() const override;
    void setCamera(const CameraState& camera) override;
    void flyTo(const PartId& part) override;
    void resetView() override;

    void resize(int width, int height) override;
    std::optional<PickHit> pick(int x, int y) const override;
    std::optional<std::pair<int, int>> project(const Vec3& world) const override;

    void update(double dtSeconds) override;
    Result<Image> renderToImage(int width, int height) override;

    // Wall time of the last renderToImage frame loop, in milliseconds (all warm-up frames, GPU wait included).
    double lastRenderMs() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Writes an RGBA image as PNG through Wicked's bundled stb_image_write.
Status writePng(const Image& image, const std::string& path);

}  // namespace fulmetry::viewport
