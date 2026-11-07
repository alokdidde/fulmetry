// The stage: shows a frame of the 3D view, scaled to cover the stage, under the floating panels.
//
// Until the Wicked viewport (S2) renders offscreen into the workbench, it shows a pre-rendered image of the
// rover. S2's frames replace it through setFrame (RGBA8, the core Image type), so nothing else changes when
// the real renderer arrives. Owned by S5.
#pragma once
#include <Raven.h>

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

private:
    static Rid create(const char* tag, void* user);
    static void paint(Rid widget, RPainter* painter, float w, float h, void* user);
    void coverRect(float w, float h, float& x, float& y, float& cw, float& chh) const;

    Rid widget_ = R_INVALID_ID;
    Rid texture_ = R_INVALID_ID;
    int width_ = 0, height_ = 0;
};

}  // namespace fulmetry::ui
