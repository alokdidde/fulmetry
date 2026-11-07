// The stage: shows a frame of the 3D view, scaled to cover the stage, under the floating panels.
//
// With the Wicked viewport (S2) the workbench renders the stage offscreen and hands each frame to setFrame
// (RGBA8, the core Image type; see LiveStage). Without it, it shows a pre-rendered image of the rover.
// It also draws the pinned comment's leader: a dot on the anchor and a line to the card. Owned by S5.
#pragma once
#include <Raven.h>

#include <optional>
#include <string>

#include "fulmetry/core/IViewport.h"

namespace fulmetry::ui {

class StageImage {
public:
    // Registers the <Stage> XML tag; the window built afterwards gets this stage. One stage per process.
    void registerTag();
    Rid widget() const { return widget_; }

    // Loads a PNG or JPEG frame. Returns false when it cannot be read.
    bool loadFile(const std::string& path);
    // Shows a rendered frame (RGBA8, width * height * 4 bytes). Empty frames are ignored.
    void setFrame(const Image& frame);
    int frameWidth() const { return width_; }
    int frameHeight() const { return height_; }

    // Where a pixel of the frame lands in the stage widget (cover fit, centred), in widget units.
    void frameToWidget(float px, float py, float& x, float& y) const;

    // The pinned comment's leader, in widget units: from the anchor (x0, y0) to the card (x1, y1).
    struct Leader {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool operator==(const Leader&) const = default;
    };
    void setLeader(const std::optional<Leader>& leader);

private:
    static Rid create(const char* tag, void* user);
    static void paint(Rid widget, RPainter* painter, float w, float h, void* user);
    void coverRect(float w, float h, float& x, float& y, float& cw, float& chh) const;

    Rid widget_ = R_INVALID_ID;
    Rid texture_ = R_INVALID_ID;
    int width_ = 0, height_ = 0;
    std::optional<Leader> leader_;
};

}  // namespace fulmetry::ui
