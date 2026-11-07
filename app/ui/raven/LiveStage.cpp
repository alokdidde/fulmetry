#include "LiveStage.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace fulmetry::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kOrbitPerPx = 0.006;        // radians per stage unit dragged
constexpr double kMinElevation = 4 * kPi / 180, kMaxElevation = 86 * kPi / 180;
constexpr double kZoomPerNotch = 0.87;
constexpr double kMinDistance = 60, kMaxDistance = 5000;  // mm, eye to subject

Vec3 cross(const Vec3& a, const Vec3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
Vec3 unit(const Vec3& v) {
    const double l = v.length();
    return l > 1e-12 ? v * (1.0 / l) : Vec3{};
}
// Rodrigues: v turned by `a` radians about the unit axis k.
Vec3 turn(const Vec3& v, const Vec3& k, double a) {
    return v * std::cos(a) + cross(k, v) * std::sin(a) + k * (k.dot(v) * (1 - std::cos(a)));
}
struct Basis {
    Vec3 f, r, u;
};
Basis basis(const CameraState& c) {
    Basis b;
    b.f = unit(c.target - c.eye);
    b.r = unit(cross(b.f, Vec3{0, 0, 1}));
    b.u = cross(b.r, b.f);
    return b;
}

}  // namespace

LiveStage::LiveStage(IViewport& viewport, WorkbenchModel& model, WorkbenchView& view, Options options)
    : vp_(viewport), model_(model), view_(view), opt_(std::move(options)) {}

void LiveStage::attach() {
    Rid w = view_.stage().widget();
    Rwidget_set_event_callback(w, &LiveStage::onEvent, this);
    view_.setPinLocator([this](const Comment& c) { return anchorOf(c); }, [this] { return subjectRect(); });
    view_.setActionHandler([this](const std::string& action) {
        if (action == "reset-view") needFit_ = true;
    });
}

// ------------------------------------------------------------------ geometry

bool LiveStage::stageSize(int& w, int& h, float& scale) const {
    float x, y, ww, hh;
    Rwidget_get_rect(view_.stage().widget(), &x, &y, &ww, &hh);
    scale = Rwindow_get_scale(view_.window());
    if (scale <= 0) scale = 1;
    w = static_cast<int>(std::lround(ww * scale));
    h = static_cast<int>(std::lround(hh * scale));
    return w > 16 && h > 16;
}

std::optional<std::pair<float, float>> LiveStage::toStage(const Vec3& world) const {
    auto px = vp_.project(world);
    if (!px) return std::nullopt;
    return std::pair<float, float>{static_cast<float>(px->first) / scale_, static_cast<float>(px->second) / scale_};
}

std::optional<std::pair<float, float>> LiveStage::anchorOf(const Comment& c) const {
    const Part* part = model_.project().findPart(c.anchor.part);
    if (!part || !vp_.isVisible(part->id) || width_ <= 0) return std::nullopt;
    // The anchor point is given in the object's frame; it rides with the part (explode, the All objects layout).
    const Vec3 local = c.anchor.point ? *c.anchor.point - part->placement.position : part->bounds.center();
    auto at = toStage(vp_.drawnPlacement(part->id).position + local);
    if (!at || at->first < 0 || at->second < 0 || at->first > width_ / scale_ || at->second > height_ / scale_)
        return std::nullopt;
    return at;
}

std::optional<WorkbenchView::Rect> LiveStage::subjectRect() const {
    std::optional<WorkbenchView::Rect> r;
    for (const Part* p : model_.project().allParts()) {
        if (!vp_.isVisible(p->id) || p->kind == PartKind::Hardware) continue;
        const Vec3 at = vp_.drawnPlacement(p->id).position;
        for (int i = 0; i < 8; ++i) {
            const Aabb& b = p->bounds;
            auto s = toStage(at + Vec3{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z});
            if (!s) continue;
            if (!r) r = WorkbenchView::Rect{s->first, s->second, s->first, s->second};
            r->x0 = std::min(r->x0, s->first), r->y0 = std::min(r->y0, s->second);
            r->x1 = std::max(r->x1, s->first), r->y1 = std::max(r->y1, s->second);
        }
    }
    return r;
}

Vec3 LiveStage::pivot() const {
    Vec3 lo{1e300, 1e300, 1e300}, hi{-1e300, -1e300, -1e300};
    bool any = false;
    for (const Part* p : model_.project().allParts()) {
        if (!vp_.isVisible(p->id) || p->kind == PartKind::Hardware) continue;
        const Vec3 at = vp_.drawnPlacement(p->id).position;
        const Vec3 a = at + p->bounds.min, b = at + p->bounds.max;
        lo = {std::min(lo.x, a.x), std::min(lo.y, a.y), std::min(lo.z, a.z)};
        hi = {std::max(hi.x, b.x), std::max(hi.y, b.y), std::max(hi.z, b.z)};
        any = true;
    }
    return any ? (lo + hi) * 0.5 : vp_.camera().target;
}

// ------------------------------------------------------------------ gestures

void LiveStage::orbit(float dx, float dy) {
    CameraState c = vp_.camera();
    const Vec3 p = pivot();
    // Yaw round the vertical through the subject.
    const double yaw = -dx * kOrbitPerPx;
    c.eye = p + turn(c.eye - p, Vec3{0, 0, 1}, yaw);
    c.target = p + turn(c.target - p, Vec3{0, 0, 1}, yaw);
    // Pitch round the camera's right axis, kept above the ground and short of straight down.
    const Vec3 e = c.eye - p;
    const double el = std::asin(std::clamp(e.z / e.length(), -1.0, 1.0));
    const double to = std::clamp(el + dy * kOrbitPerPx, kMinElevation, kMaxElevation);
    const Vec3 r = basis(c).r;
    c.eye = p + turn(c.eye - p, r, -(to - el));
    c.target = p + turn(c.target - p, r, -(to - el));
    vp_.setCamera(c);
    changed();
}

void LiveStage::pan(float dx, float dy) {
    CameraState c = vp_.camera();
    const Basis b = basis(c);
    const double dist = (pivot() - c.eye).dot(b.f);
    const double perUnit = 2 * std::max(dist, 1.0) * std::tan(c.fovDegrees * kPi / 360) / (height_ / scale_);
    const Vec3 move = (b.r * -dx + b.u * dy) * perUnit;
    c.eye = c.eye + move;
    c.target = c.target + move;
    vp_.setCamera(c);
    changed();
}

void LiveStage::zoom(float notches, float x, float y) {
    CameraState c = vp_.camera();
    const Basis b = basis(c);
    const double dist = (pivot() - c.eye).length();
    const double next = std::clamp(dist * std::pow(kZoomPerNotch, notches), kMinDistance, kMaxDistance);
    if (std::abs(next - dist) < 1e-6) return;
    // Towards the point under the cursor, so it stays where it is.
    const double th = std::tan(c.fovDegrees * kPi / 360), aspect = double(width_) / height_;
    const double nx = (x * scale_ / width_) * 2 - 1, ny = 1 - (y * scale_ / height_) * 2;
    const Vec3 d = unit(b.f + b.r * (nx * th * aspect) + b.u * (ny * th));
    const Vec3 move = d * (dist - next);
    c.eye = c.eye + move;
    c.target = c.target + move;
    vp_.setCamera(c);
    changed();
}

void LiveStage::click(float x, float y) {
    auto hit = vp_.pick(static_cast<int>(x * scale_), static_cast<int>(y * scale_));
    if (opt_.log) {
        std::printf("stage-click %.0f %.0f -> %s\n", static_cast<double>(x), static_cast<double>(y),
                    hit ? hit->part.value.c_str() : "nothing");
        std::fflush(stdout);
    }
    if (hit) view_.selectPart(hit->part);
    else view_.post([this] { model_.clearSelection(); });
}

void LiveStage::hover(float x, float y) {
    auto hit = vp_.pick(static_cast<int>(x * scale_), static_cast<int>(y * scale_));
    std::optional<PartId> part;
    if (hit) part = hit->part;
    hoverPart(part);
}

void LiveStage::hoverPart(const std::optional<PartId>& part) {
    if (part == hovered_) return;
    hovered_ = part;
    vp_.setHovered(part);
    if (opt_.log) {
        std::printf("stage-hover %s\n", part ? part->value.c_str() : "nothing");
        std::fflush(stdout);
    }
    changed();
}

void LiveStage::fit() {
    if (width_ <= 0) return;
    const float w = width_ / scale_, h = height_ / scale_;
    const WorkbenchView::Rect r = view_.freeStageRect();
    if (opt_.setFraming && r.x1 - r.x0 > 0.3f * w && r.y1 - r.y0 > 0.3f * h)
        opt_.setFraming(r.x0 / w, r.y0 / h, r.x1 / w, r.y1 / h);
    vp_.showObject(model_.scope());
    vp_.resetView();
    changed();
}

int LiveStage::onEvent(Rid, const REvent* e, void* user) {
    auto* s = static_cast<LiveStage*>(user);
    switch (e->type) {
        case R_EVENT_MOUSE_DOWN:
            if (s->button_ >= 0) return R_TRUE;
            s->button_ = e->button;
            s->downX_ = s->lastX_ = e->local_x;
            s->downY_ = s->lastY_ = e->local_y;
            s->dragged_ = false;
            s->hoverAt_.reset();
            return R_TRUE;
        case R_EVENT_MOUSE_MOVE: {
            if (s->button_ < 0) {
                s->hoverAt_ = std::pair<float, float>{e->local_x, e->local_y};
                s->hoverSince_ = s->now_;
                return R_FALSE;
            }
            const float dx = e->local_x - s->lastX_, dy = e->local_y - s->lastY_;
            s->lastX_ = e->local_x;
            s->lastY_ = e->local_y;
            if (!s->dragged_ && std::hypot(e->local_x - s->downX_, e->local_y - s->downY_) < 4) return R_TRUE;
            s->dragged_ = true;
            if (s->button_ == R_MOUSE_LEFT) s->orbit(dx, dy);
            else s->pan(dx, dy);
            return R_TRUE;
        }
        case R_EVENT_MOUSE_UP:
            if (s->button_ < 0 || e->button != static_cast<RMouseButton>(s->button_)) return R_FALSE;
            if (!s->dragged_ && s->button_ == R_MOUSE_LEFT) s->clickAt_ = std::pair<float, float>{s->downX_, s->downY_};
            s->button_ = -1;
            return R_TRUE;
        case R_EVENT_MOUSE_WHEEL:
            s->zoom(e->wheel_y, e->local_x, e->local_y);
            return R_TRUE;
        case R_EVENT_MOUSE_LEAVE:
            s->hoverAt_.reset();
            if (s->hovered_) {
                s->hovered_.reset();
                s->vp_.setHovered(std::nullopt);
                s->changed();
            }
            return R_FALSE;
        default:
            return R_FALSE;
    }
}

// ------------------------------------------------------------------ frame

void LiveStage::setSimulating(bool on) {
    simulating_ = on;
    if (!on) {
        // the simulation showed one object, assembled and solid: put the model's view back
        shownTab_ = -1;
        shownExplode_ = shownXray_ = false;
        shownSel_.reset();
        needFit_ = true;
    }
    changed();
}

void LiveStage::syncModel() {
    if (simulating_) {
        if (model_.selectedPart() != shownSel_) {
            shownSel_ = model_.selectedPart();
            vp_.setSelected(shownSel_);
            changed();
        }
        return;
    }
    if (model_.activeTab() != shownTab_) {
        shownTab_ = model_.activeTab();
        vp_.showObject(model_.scope());
        if (hovered_ && !vp_.isVisible(*hovered_)) {
            hovered_.reset();
            vp_.setHovered(std::nullopt);
        }
        needFit_ = true;
    }
    if (model_.exploded() != shownExplode_) {
        shownExplode_ = model_.exploded();
        vp_.setExplode(shownExplode_ ? 1.0 : 0.0);
        needFit_ = true;
    }
    if (model_.xray() != shownXray_) {
        shownXray_ = model_.xray();
        vp_.setXray(shownXray_);
        changed();
    }
    if (model_.selectedPart() != shownSel_) {
        shownSel_ = model_.selectedPart();
        vp_.setSelected(shownSel_);
        changed();
    }
}

void LiveStage::update(double now) {
    now_ = now;
    int w, h;
    float sc;
    if (!stageSize(w, h, sc)) return;
    if (w != width_ || h != height_ || sc != scale_) {
        width_ = w, height_ = h, scale_ = sc;
        vp_.resize(w, h);
        needFit_ = true;
    }
    if (clickAt_) {
        click(clickAt_->first, clickAt_->second);
        clickAt_.reset();
    }
    syncModel();
    if (needFit_ && !simulating_) {
        needFit_ = false;
        fit();
    }
    if (hoverAt_ && now_ - hoverSince_ >= opt_.hoverDelay && button_ < 0) {
        hover(hoverAt_->first, hoverAt_->second);
        hoverAt_.reset();
    }
    if (dirty_) render(false);
    else if (!settled_ && now_ - lastChange_ >= opt_.idleDelay) render(true);
}

void LiveStage::render(bool resting) {
    dirty_ = false;
    const int frames = resting ? std::max(1, opt_.idleFrames) : 1;
    if (opt_.setFrames) opt_.setFrames(frames);
    const float s = resting ? 1.0f : std::clamp(opt_.interactiveScale, 0.1f, 1.0f);
    const int rw = std::max(1, static_cast<int>(std::lround(width_ * s)));
    const int rh = std::max(1, static_cast<int>(std::lround(height_ * s)));
    const auto t0 = std::chrono::steady_clock::now();
    auto img = vp_.renderToImage(rw, rh);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!img.ok()) {
        std::fprintf(stderr, "stage: render failed: %s\n", img.error().message.c_str());
        settled_ = true;
        return;
    }
    view_.stage().setFrame(img.value());
    ++renders_;
    (resting ? idleMs_ : interactiveMs_) = ms;
    if (resting) settled_ = true;
    if (!opt_.log) return;
    std::printf("stage-render %s %dx%d, %d frame%s: %.0f ms\n", resting ? "rest" : "quick", rw, rh, frames,
                frames == 1 ? "" : "s", ms);
    if (resting) {
        const CameraState c = vp_.camera();
        std::printf("stage-settled camera eye %.1f %.1f %.1f target %.1f %.1f %.1f\n", c.eye.x, c.eye.y, c.eye.z,
                    c.target.x, c.target.y, c.target.z);
        reportParts();
    }
    std::fflush(stdout);
}

void LiveStage::reportParts() const {
    float sx, sy, sw, sh;
    Rwidget_get_rect(view_.stage().widget(), &sx, &sy, &sw, &sh);
    for (const char* id : {"part:rover/camera-head", "part:rover/wheel-l", "part:remote/shell"}) {
        const Part* p = model_.project().findPart(PartId{id});
        if (!p || !vp_.isVisible(p->id)) continue;
        // A point that picks the part: its bounds centre, else the first of a grid inside its bounds.
        const Vec3 at = vp_.drawnPlacement(p->id).position;
        for (int i = -1; i < 7 * 7 * 7; ++i) {
            const Aabb& b = p->bounds;
            const Vec3 f = i < 0 ? Vec3{0.5, 0.5, 0.5} : Vec3{(i % 7 + 0.5) / 7, (i / 7 % 7 + 0.5) / 7, (i / 49 + 0.5) / 7};
            auto s = toStage(at + b.min + Vec3{(b.max.x - b.min.x) * f.x, (b.max.y - b.min.y) * f.y, (b.max.z - b.min.z) * f.z});
            if (!s) continue;
            auto hit = vp_.pick(static_cast<int>(s->first * scale_), static_cast<int>(s->second * scale_));
            if (!hit || hit->part != p->id) continue;
            std::printf("part-at %s %.0f %.0f\n", id, static_cast<double>(sx + s->first), static_cast<double>(sy + s->second));
            break;
        }
    }
}

}  // namespace fulmetry::ui
