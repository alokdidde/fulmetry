#include "fulmetry/ui/DriveTest.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

namespace fulmetry::ui {

namespace {

Quat mul(const Quat& a, const Quat& b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
Quat conj(const Quat& q) { return {q.w, -q.x, -q.y, -q.z}; }
Vec3 rot(const Quat& q, const Vec3& v) {
    const Quat r = mul(mul(q, Quat{0, v.x, v.y, v.z}), conj(q));
    return {r.x, r.y, r.z};
}
// a then b: the transform that applies b first, then a.
Transform compose(const Transform& a, const Transform& b) {
    return {a.position + rot(a.rotation, b.position), mul(a.rotation, b.rotation)};
}
Transform inverse(const Transform& t) {
    const Quat qi = conj(t.rotation);
    return {rot(qi, t.position * -1.0), qi};
}

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

}  // namespace

Result<SimSetup> parseSimSetup(const std::string& text, const Project& project) {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return Error{"invalid_setup", "not a JSON object"};
    try {
        SimSetup s;
        s.name = j.value("name", "Simulation");
        s.description = j.value("description", "");
        s.model = j.at("model").get<std::string>();
        s.object = ObjectId(j.at("object").get<std::string>());
        const Object* obj = project.findObject(s.object);
        if (!obj) return Error{"invalid_setup", "no object " + s.object.value};
        for (const auto& [body, parts] : j.at("bodies").items())
            for (const auto& p : parts) {
                const std::string id = p.get<std::string>();
                if (id != "*" && !project.findPart(PartId(id))) return Error{"invalid_setup", "no part " + id};
                s.bodies[body].push_back(PartId(id));
            }
        if (j.contains("floor")) {
            s.floorName = j["floor"].value("name", "Floor");
            s.environment.floorColor = j["floor"].value("color", "#ffffff");
        }
        for (const auto& p : j.value("props", nlohmann::json::array())) {
            StageProp prop;
            prop.name = p.value("name", "");
            const auto& mn = p.at("box").at("min");
            const auto& mx = p.at("box").at("max");
            prop.box = {{mn[0].get<double>(), mn[1].get<double>(), mn[2].get<double>()},
                        {mx[0].get<double>(), mx[1].get<double>(), mx[2].get<double>()}};
            prop.color = p.value("color", "#cccccc");
            if (p.value("surface", false)) {
                s.surfaceBoxes.push_back(prop.box);
                s.surfaceNames.push_back(prop.name);
            }
            s.environment.props.push_back(std::move(prop));
        }
        for (const auto& r : j.value("route", nlohmann::json::array()))
            s.route.push_back({r.at("seconds").get<double>(), {r.value("throttle", 0.0), r.value("steer", 0.0)}});
        return s;
    } catch (const std::exception& e) {
        return Error{"invalid_setup", e.what()};
    }
}

DriveTest::DriveTest(ISimulation& sim, IViewport* viewport, Project project, SimSetup setup)
    : sim_(sim), vp_(viewport), project_(std::move(project)), setup_(std::move(setup)) {
    std::string rest;
    for (const auto& [body, parts] : setup_.bodies)
        for (const auto& p : parts)
            if (p.value == "*") rest = mainBody_ = body;
            else bodyOf_[p] = body;
    if (const Object* o = project_.findObject(setup_.object))
        for (const auto& p : o->parts)
            if (!bodyOf_.count(p.id) && !rest.empty()) bodyOf_[p.id] = rest;
    // the drive wheels' size, for the findings: the parts that ride on a wheel body
    for (const auto& [part, body] : bodyOf_)
        if (body.find("wheel") != std::string::npos)
            if (const Part* p = project_.findPart(part)) {
                const Vec3 s = p->bounds.size();
                wheelMm_ = std::max(wheelMm_, std::max({s.x, s.y, s.z}));
            }
}

Status DriveTest::start(const std::string& mjcfXml) {
    if (auto st = sim_.loadModel(mjcfXml); !st.ok()) return st;
    sim_.reset(0);
    rest_.clear();
    for (const auto& [part, body] : bodyOf_)
        if (!rest_.count(body)) {
            auto pose = sim_.bodyPose(body);
            if (!pose.ok()) return Error{"invalid_setup", "the model has no body " + body};
            rest_[body] = pose.value();
        }
    findings_.clear();
    running_ = true;
    if (vp_) {
        vp_->showObject(setup_.object);
        vp_->setExplode(0);
        vp_->setXray(false);
        vp_->setSelected(std::nullopt);
        vp_->setEnvironment(setup_.environment);
    }
    restart();
    return Ok{};
}

void DriveTest::stop() {
    running_ = route_ = false;
    if (!vp_) return;
    for (const auto& [part, body] : bodyOf_) vp_->setPartPose(part, std::nullopt);
    vp_->setEnvironment(std::nullopt);
}

void DriveTest::restart() {
    sim_.reset(0);
    input_ = {};
    sim_.setDriveInput(input_);
    route_ = false;
    routeIndex_ = 0;
    routeT_ = 0;
    history_.clear();
    sampleT_ = 0;
    floorCruise_ = surfaceSpeedSum_ = surfaceTime_ = stuckT_ = 0;
    surfaceEnter_ = -1;
    stalls_ = 0;
    peakCurrent_ = 0;
    distance_ = 0;
    auto p = sim_.bodyPose(mainBody_);
    lastPosition_ = position_ = p.ok() ? p->position : Vec3{};
    observe(0);
    applyPoses();
}

void DriveTest::runRoute() {
    if (!running_) return;
    restart();
    route_ = !setup_.route.empty();
    if (route_) {
        input_ = setup_.route.front().input;
        sim_.setDriveInput(input_);
    }
}

bool DriveTest::routeDone() const { return routeIndex_ >= setup_.route.size(); }

void DriveTest::setDrive(const DriveInput& in) {
    if (!running_) return;
    route_ = false;
    input_ = in;
    sim_.setDriveInput(in);
}

void DriveTest::step(double dt) {
    if (!running_ || !(dt > 0)) return;
    // The route advances in the same fixed time as the physics, so a run is the same at any frame rate.
    double left = dt;
    while (left > 1e-9) {
        double chunk = std::min(left, 0.02);
        if (route_ && routeIndex_ < setup_.route.size()) {
            const double remain = setup_.route[routeIndex_].seconds - routeT_;
            chunk = std::min(chunk, std::max(remain, 1e-6));
        }
        sim_.step(chunk);
        observe(chunk);
        left -= chunk;
        if (route_ && routeIndex_ < setup_.route.size()) {
            routeT_ += chunk;
            if (routeT_ >= setup_.route[routeIndex_].seconds - 1e-9) {
                routeT_ = 0;
                ++routeIndex_;
                input_ = routeIndex_ < setup_.route.size() ? setup_.route[routeIndex_].input : DriveInput{};
                sim_.setDriveInput(input_);
                if (routeIndex_ >= setup_.route.size()) {
                    route_ = false;
                    routeFindings();
                }
            }
        }
    }
    applyPoses();
}

void DriveTest::applyPoses() {
    if (!vp_) return;
    for (const auto& [part, body] : bodyOf_) {
        const Part* p = project_.findPart(part);
        auto now = sim_.bodyPose(body);
        if (!p || !now.ok()) continue;
        // where the part sits relative to its body at the start, carried along with the body
        const Transform delta = compose(now.value(), inverse(rest_[body]));
        vp_->setPartPose(part, compose(delta, p->placement));
    }
}

std::string DriveTest::surface() const { return onSurface_ ? surfaceName_ : setup_.floorName; }

void DriveTest::addFinding(Finding f) {
    for (const auto& x : findings_)
        if (x.id == f.id) return;
    findings_.insert(findings_.begin(), std::move(f));
}

std::string DriveTest::obstacle() const {
    // the nearest prop that stands up from the floor, within a rover's length of its centre
    std::string best = "an obstacle";
    double bestD = 250;
    for (const auto& p : setup_.environment.props) {
        if (p.box.max.z < 20) continue;
        const double dx = std::max({p.box.min.x - position_.x, 0.0, position_.x - p.box.max.x});
        const double dy = std::max({p.box.min.y - position_.y, 0.0, position_.y - p.box.max.y});
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < bestD) {
            bestD = d;
            best = "the " + p.name;
            for (size_t i = 4; i < best.size(); ++i) best[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(best[i])));
        }
    }
    return best;
}

void DriveTest::surfaceFinding() {
    if (surfaceTime_ < 0.8 || floorCruise_ <= 0.05) return;
    const double avg = surfaceSpeedSum_ / surfaceTime_, ratio = avg / floorCruise_;
    const std::string where = surfaceName_.empty() ? std::string("the rug") : surfaceName_;
    const std::string wheels = wheelMm_ > 0 ? fmt("\xC3\x98%.0f mm wheels", wheelMm_) : std::string("The wheels");
    Finding f;
    f.id = "surface";
    f.good = ratio >= 0.8;
    f.text = wheels + (f.good ? " crossed " : " slowed to ") +
             (f.good ? where + " at " + fmt("%.0f%%", ratio * 100) + " of floor speed (" + fmt("%.2f m/s", avg) +
                           ") in " + fmt("%.1f s", sim_.time() - surfaceEnter_) + "."
                     : fmt("%.0f%%", ratio * 100) + " of floor speed on " + where + " (" + fmt("%.2f m/s", avg) +
                           ").");
    if (!f.good) {
        for (const auto& [part, body] : bodyOf_)
            if (body.find("wheel") != std::string::npos) { f.part = part; break; }
        f.fixText = "It slows down on the rug in the simulation. Make the wheels bigger with more grip.";
    }
    addFinding(f);
}

void DriveTest::routeFindings() {
    // a run that ends on the rug still says how the rug went
    if (onSurface_) surfaceFinding();
    if (floorCruise_ <= 0) return;
    Finding f;
    f.id = "speed";
    f.text = "Top speed " + fmt("%.2f m/s", floorCruise_) + " on the floor; " + fmt(100 - battery_ < 1 ? "%.2f%%" : "%.1f%%", 100 - battery_) +
             " of the battery used over " + fmt("%.1f m", distance_) + ".";
    addFinding(f);
}

void DriveTest::observe(double dt) {
    auto t = sim_.telemetry();
    speed_ = t["speed_mps"];
    current_ = t["current_a"];
    battery_ = t.count("battery_pct") ? t["battery_pct"] : 100.0;
    rssi_ = t["rssi_dbm"];
    peakCurrent_ = std::max(peakCurrent_, current_);
    if (auto p = sim_.bodyPose(mainBody_); p.ok()) {
        position_ = p->position;
        const Vec3 d = position_ - lastPosition_;
        distance_ += std::sqrt(d.x * d.x + d.y * d.y) / 1000.0;
        lastPosition_ = position_;
    }
    // on a surface prop: the physics' contact when it reports one, else the footprint over the prop
    bool on = false;
    std::string name;
    for (size_t i = 0; i < setup_.surfaceBoxes.size(); ++i) {
        const Aabb& b = setup_.surfaceBoxes[i];
        if (position_.x >= b.min.x && position_.x <= b.max.x && position_.y >= b.min.y && position_.y <= b.max.y) {
            on = true;
            name = setup_.surfaceNames[i];
        }
    }
    if (t.count("on_rug") && !setup_.surfaceNames.empty()) {
        on = t["on_rug"] > 0.5 || on;
        if (on && name.empty()) name = setup_.surfaceNames.front();
    }
    const bool full = std::abs(input_.throttle) >= 0.9 && std::abs(input_.steer) < 0.1;
    if (dt > 0) {
        if (!on && full) floorCruise_ = std::max(floorCruise_, std::abs(speed_));
        if (on && !onSurface_) {
            surfaceEnter_ = sim_.time();
            surfaceSpeedSum_ = surfaceTime_ = 0;
        }
        if (on && full) {
            surfaceSpeedSum_ += std::abs(speed_) * dt;
            surfaceTime_ += dt;
        }
        if (!on && onSurface_) surfaceFinding();
        // a stall: full throttle and no movement for half a second
        if (std::abs(input_.throttle) > 0.5 && std::abs(speed_) < 0.03) stuckT_ += dt;
        else stuckT_ = 0;
        if (stuckT_ >= 0.5) {
            stuckT_ = -1e9;  // once per stall
            ++stalls_;
            Finding f;
            f.id = "stall" + std::to_string(stalls_);
            f.good = false;
            f.text = on ? "Stalled on " + name + " at full throttle." : "Stalled at full throttle against " + obstacle() + ".";
            addFinding(f);
        }
        sampleT_ += dt;
        if (sampleT_ >= 0.1 || history_.empty()) {
            sampleT_ = 0;
            history_.push_back({sim_.time(), speed_, on});
            if (history_.size() > 600) history_.erase(history_.begin());
        }
    }
    if (on) surfaceName_ = name;
    onSurface_ = on;
}

CameraState DriveTest::startCamera() {
    CameraState c;
    c.fovDegrees = 30;
    c.target = position_ + Vec3{620, 80, 0};  // the rover, and the room ahead of it
    c.eye = c.target + Vec3{-1500, -2050, 1350};
    cameraAnchor_ = position_;
    return c;
}

CameraState DriveTest::chase(const CameraState& current) {
    // Moves the camera with the rover, keeping whatever orbit and zoom the person gave it.
    CameraState c = current;
    const Vec3 d = position_ - cameraAnchor_;
    cameraAnchor_ = position_;
    c.target = c.target + Vec3{d.x, d.y, 0};
    c.eye = c.eye + Vec3{d.x, d.y, 0};
    return c;
}

}  // namespace fulmetry::ui
