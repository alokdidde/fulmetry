#include "StageImage.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace fulmetry::ui {

void StageImage::registerTag() { Rxml_register_tag("Stage", &StageImage::create, nullptr, this); }

Rid StageImage::create(const char*, void* user) {
    auto* self = static_cast<StageImage*>(user);
    self->widget_ = Rwidget_create(R_WIDGET_CUSTOM);
    Rcustom_set_paint_callback(self->widget_, &StageImage::paint, self);
    return self->widget_;
}

bool StageImage::loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return false;
    int w = 0, h = 0;
    uint8_t* px = Rimage_decode(bytes.data(), bytes.size(), &w, &h);
    if (!px) return false;
    Image frame;
    frame.width = w;
    frame.height = h;
    frame.rgba.assign(px, px + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    Rpixels_free(px);
    setFrame(frame);
    return true;
}

void StageImage::setFrame(const Image& frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.rgba.size() < static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height) * 4)
        return;
    if (texture_ == R_INVALID_ID) texture_ = Rtexture_create_from_pixels(frame.rgba.data(), frame.width, frame.height);
    else Rtexture_update(texture_, frame.rgba.data(), frame.width, frame.height);
    width_ = frame.width;
    height_ = frame.height;
    if (widget_ != R_INVALID_ID) Rwidget_invalidate(widget_);
}

void StageImage::coverRect(float w, float h, float& x, float& y, float& cw, float& chh) const {
    if (width_ <= 0 || height_ <= 0) {
        x = y = 0;
        cw = w;
        chh = h;
        return;
    }
    const float s = std::max(w / static_cast<float>(width_), h / static_cast<float>(height_));
    cw = static_cast<float>(width_) * s;
    chh = static_cast<float>(height_) * s;
    x = (w - cw) * 0.5f;
    y = (h - chh) * 0.5f;
}

void StageImage::frameToWidget(float px, float py, float& x, float& y) const {
    float wx, wy, ww, wh;
    Rwidget_get_rect(widget_, &wx, &wy, &ww, &wh);
    float cx, cy, cw, chh;
    coverRect(ww, wh, cx, cy, cw, chh);
    const float s = width_ > 0 ? cw / static_cast<float>(width_) : 1.0f;
    x = cx + px * s;
    y = cy + py * s;
}

void StageImage::paint(Rid, RPainter* p, float w, float h, void* user) {
    auto* self = static_cast<StageImage*>(user);
    // the high-key stage colour behind the frame (prototype: #e8ecf0)
    Rdraw_rect(p, 0, 0, w, h, 0, 0.91f, 0.925f, 0.94f, 1);
    if (self->texture_ == R_INVALID_ID) return;
    float x, y, cw, chh;
    self->coverRect(w, h, x, y, cw, chh);
    Rdraw_push_clip_rect(p, 0, 0, w, h);
    Rdraw_image(p, self->texture_, x, y, cw, chh, 1, 1, 1, 1);
    Rdraw_pop_clip_rect(p);
}

}  // namespace fulmetry::ui
