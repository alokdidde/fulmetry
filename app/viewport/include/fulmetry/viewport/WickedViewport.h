// The real 3D stage: IViewport on Wicked Engine. Built only when FULMETRY_WITH_WICKED is ON.
// This header has no Wicked includes; the engine lives behind a pimpl in WickedViewport.cpp.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fulmetry/core/IViewport.h"
#include "fulmetry/viewport/MeshAsset.h"

namespace fulmetry::viewport {

struct WickedOptions {
    // Frames averaged per renderToImage call, each with the key light moved across a small disk: the samples of
    // the soft (area-light) shadow. 1 is fastest and gives a hard-edged shadow.
    int frames = 8;
    bool outlines = true;  // dark edges from Wicked's depth outline pass
    bool shadows = true;   // soft key-light shadow on the ground and between parts
    bool ambientOcclusion = true;  // screen-space AO (SSAO): grey in crevices and where parts meet the ground
    int msaa = 4;          // samples per pixel; 1 switches to FXAA
    std::string assetRoot;  // directory that relative Part.meshFile paths start from (the project root)
};

class WickedViewport : public IViewport {
public:
    explicit WickedViewport(WickedOptions options = {});
    ~WickedViewport() override;
    WickedViewport(const WickedViewport&) = delete;
    WickedViewport& operator=(const WickedViewport&) = delete;

    Status loadProject(const Project& project) override;
    // Where relative Part.meshFile paths start, for the next loadProject (another project's folder).
    void setAssetRoot(const std::string& root);
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
    void setEnvironment(const std::optional<StageEnvironment>& environment) override;
    void setPartPose(const PartId& part, const std::optional<Transform>& pose) override;

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

    // Frames averaged by the next renderToImage (WickedOptions::frames): 1 while the camera moves (hard shadow),
    // the default 8 once it rests.
    void setSoftShadowFrames(int frames);
    int softShadowFrames() const;
    // Where resetView puts what is visible: a rectangle of the view in fractions (x right, y down), so a
    // workbench can frame the subject in the space its floating panels leave free. `fill` is how much of that
    // rectangle the subject's widest extent takes. Default: the whole view, 0.7.
    void setFramingRect(double left, double top, double right, double bottom, double fill = 0.92);

    // Wall time of the last renderToImage frame loop, in milliseconds (all warm-up frames, GPU wait included).
    double lastRenderMs() const;
    // Parts drawn from their meshFile (the rest are boxes over Part.bounds), and why a meshFile was not used.
    int meshFileCount() const;
    const std::vector<std::string>& meshWarnings() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Writes an RGBA image as PNG through Wicked's bundled stb_image_write.
Status writePng(const Image& image, const std::string& path);

}  // namespace fulmetry::viewport
