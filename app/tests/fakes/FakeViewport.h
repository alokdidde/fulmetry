// Fake 3D stage: parts are boxes (Part::bounds at Part::placement), picking is a ray cast.
// Fast, deterministic and GPU-free, so UI and agent work can run before Wicked is wired.
#pragma once
#include <algorithm>
#include <array>
#include <tuple>
#include <cmath>
#include <limits>
#include <map>
#include <set>

#include "fulmetry/core/IViewport.h"

namespace fulmetry::fakes {

class FakeViewport : public IViewport {
public:
    Status loadProject(const Project& project) override {
        project_ = project;
        hidden_.clear();
        isolated_.reset();
        object_.reset();
        resetView();
        return Ok{};
    }
    void showObject(const std::optional<ObjectId>& object) override { object_ = object; }
    void setSelected(const std::optional<PartId>& part) override { selected_ = part; }
    void setHovered(const std::optional<PartId>& part) override { hovered_ = part; }
    void setHidden(const PartId& part, bool hidden) override {
        if (hidden) hidden_.insert(part); else hidden_.erase(part);
    }
    void isolate(const std::optional<PartId>& part) override { isolated_ = part; }
    bool isVisible(const PartId& part) const override {
        const Part* p = project_.findPart(part);
        if (!p || hidden_.count(part)) return false;
        if (isolated_) return *isolated_ == part;
        if (object_) return objectOf(part) == *object_;
        return true;
    }
    void setExplode(double amount) override { explode_ = std::clamp(amount, 0.0, 1.0); }
    void setXray(bool on) override { xray_ = on; }
    void setJointsVisible(bool on) override { joints_ = on; }
    void setStyle(StageStyle style) override { style_ = style; }
    void setSlicePlane(const std::optional<double>& h) override { slice_ = h; }
    void setEnvironment(const std::optional<StageEnvironment>& env) override { environment_ = env; }
    void setPartPose(const PartId& part, const std::optional<Transform>& pose) override {
        if (pose) poses_[part] = *pose; else poses_.erase(part);
    }
    const std::optional<StageEnvironment>& environment() const { return environment_; }

    Transform drawnPlacement(const PartId& part) const override {
        const Part* p = project_.findPart(part);
        if (!p) return {};
        if (auto it = poses_.find(part); it != poses_.end()) return it->second;
        Transform t = p->placement;
        t.position = objectOffset(objectOf(part)) + t.position;
        if (explode_ > 0) {
            Vec3 c = objectCenter(objectOf(part));
            Vec3 d = t.position - c;
            if (d.length() < 1e-6) d = {0, 0, 1};
            t.position = t.position + d * (explode_ * kExplodeScale);
        }
        return t;
    }

    CameraState camera() const override { return camera_; }
    void setCamera(const CameraState& c) override { camera_ = c; }
    void flyTo(const PartId& part) override {
        const Part* p = project_.findPart(part);
        if (!p) return;
        Vec3 c = drawnPlacement(part).position + p->bounds.center();
        double r = std::max(40.0, p->bounds.size().length());
        camera_.target = c;
        camera_.eye = c + Vec3{1, -1, 0.8} * (r * 1.6);
    }
    void resetView() override { camera_ = CameraState{}; }

    void resize(int w, int h) override { width_ = std::max(1, w); height_ = std::max(1, h); }

    std::optional<PickHit> pick(int x, int y) const override {
        auto [o, d] = ray(x + 0.5, y + 0.5);
        std::optional<PickHit> best;
        double bestT = std::numeric_limits<double>::max();
        for (const Part* p : project_.allParts()) {
            if (!isVisible(p->id)) continue;
            Vec3 pos = drawnPlacement(p->id).position;
            Aabb box{pos + p->bounds.min, pos + p->bounds.max};
            double t; Vec3 n;
            if (hit(box, o, d, t, n) && t < bestT) {
                bestT = t;
                best = PickHit{p->id, std::nullopt, o + d * t, n};
            }
        }
        return best;
    }

    std::optional<std::pair<int, int>> project(const Vec3& w) const override {
        auto [f, r, u] = basis();
        Vec3 v = w - camera_.eye;
        double z = v.dot(f);
        if (z <= 1e-6) return std::nullopt;
        double th = std::tan(camera_.fovDegrees * kPi / 360.0);
        double aspect = double(width_) / height_;
        double nx = v.dot(r) / (z * th * aspect), ny = v.dot(u) / (z * th);
        return std::pair<int, int>{int(std::floor((nx + 1) * 0.5 * width_)), int(std::floor((1 - ny) * 0.5 * height_))};
    }

    void update(double) override {}

    Result<Image> renderToImage(int w, int h) override {
        if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return Error{"bad_size", "fake renders up to 1024 px"};
        int ow = width_, oh = height_;
        resize(w, h);
        Image img{w, h, std::vector<uint8_t>(size_t(w) * h * 4, 255)};
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (auto hit = pick(x, y)) {
                    auto c = colorOf(hit->part);
                    double shade = 0.6 + 0.4 * std::abs(hit->normal.z);
                    uint8_t* px = &img.rgba[(size_t(y) * w + x) * 4];
                    px[0] = uint8_t(c[0] * shade); px[1] = uint8_t(c[1] * shade); px[2] = uint8_t(c[2] * shade);
                }
        resize(ow, oh);
        return img;
    }

    // Test helpers
    const std::optional<PartId>& selected() const { return selected_; }
    bool xray() const { return xray_; }
    double explode() const { return explode_; }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kExplodeScale = 0.8;

    ObjectId objectOf(const PartId& part) const {
        for (const auto& o : project_.objects)
            for (const auto& p : o.parts) if (p.id == part) return o.id;
        return {};
    }
    // Objects stand side by side in the All objects view, 250 mm apart along x.
    Vec3 objectOffset(const ObjectId& id) const {
        for (size_t i = 0; i < project_.objects.size(); ++i)
            if (project_.objects[i].id == id) return {double(i) * 250.0, 0, 0};
        return {};
    }
    Vec3 objectCenter(const ObjectId& id) const {
        const Object* o = project_.findObject(id);
        if (!o || o->parts.empty()) return {};
        Vec3 s;
        for (const auto& p : o->parts) s = s + p.placement.position;
        return objectOffset(id) + s * (1.0 / o->parts.size());
    }
    std::tuple<Vec3, Vec3, Vec3> basis() const {
        Vec3 f = camera_.target - camera_.eye;
        f = f * (1.0 / f.length());
        Vec3 up{0, 0, 1};
        Vec3 r{f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x};
        r = r * (1.0 / r.length());
        Vec3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
        return {f, r, u};
    }
    std::pair<Vec3, Vec3> ray(double px, double py) const {
        auto [f, r, u] = basis();
        double th = std::tan(camera_.fovDegrees * kPi / 360.0);
        double aspect = double(width_) / height_;
        double nx = (px / width_) * 2 - 1, ny = 1 - (py / height_) * 2;
        Vec3 d = f + r * (nx * th * aspect) + u * (ny * th);
        return {camera_.eye, d * (1.0 / d.length())};
    }
    static bool hit(const Aabb& b, const Vec3& o, const Vec3& d, double& tOut, Vec3& nOut) {
        double tmin = -1e300, tmax = 1e300;
        Vec3 n;
        const double os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
        const double mn[3] = {b.min.x, b.min.y, b.min.z}, mx[3] = {b.max.x, b.max.y, b.max.z};
        for (int i = 0; i < 3; ++i) {
            if (std::abs(ds[i]) < 1e-12) {
                if (os[i] < mn[i] || os[i] > mx[i]) return false;
                continue;
            }
            double t1 = (mn[i] - os[i]) / ds[i], t2 = (mx[i] - os[i]) / ds[i];
            double sign = -1;
            if (t1 > t2) { std::swap(t1, t2); sign = 1; }
            if (t1 > tmin) {
                tmin = t1;
                n = {0, 0, 0};
                (i == 0 ? n.x : i == 1 ? n.y : n.z) = sign;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        if (tmax < 0) return false;
        tOut = tmin > 0 ? tmin : tmax;
        nOut = n;
        return true;
    }
    std::array<int, 3> colorOf(const PartId& id) const {
        if (selected_ && *selected_ == id) return {232, 64, 47};  // accent red
        const Part* p = project_.findPart(id);
        if (p && p->color.size() == 7) {
            auto hex = [&](int i) { return std::stoi(p->color.substr(i, 2), nullptr, 16); };
            return {hex(1), hex(3), hex(5)};
        }
        return {240, 240, 238};
    }

    Project project_;
    std::set<PartId> hidden_;
    std::optional<PartId> isolated_, selected_, hovered_;
    std::optional<ObjectId> object_;
    double explode_ = 0;
    bool xray_ = false, joints_ = false;
    StageStyle style_ = StageStyle::HighKey;
    std::optional<double> slice_;
    CameraState camera_;
    int width_ = 1280, height_ = 800;
    std::optional<StageEnvironment> environment_;
    std::map<PartId, Transform> poses_;
};

}  // namespace fulmetry::fakes
