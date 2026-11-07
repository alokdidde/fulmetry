// Fake simulation: a kinematic differential-drive rover on a floor with a rug strip.
// Reads wheel radius from the MJCF (<geom name="wheel_l" ... size="R ..."/>), so wheel size changes
// behave the way the UI spec's rug example expects: bigger wheels cross the rug faster.
#pragma once
#include <algorithm>
#include <cmath>
#include <regex>

#include "fulmetry/core/ISimulation.h"

namespace fulmetry::fakes {

class FakeSimulation : public ISimulation {
public:
    Status loadModel(const std::string& xml) override {
        if (xml.find("<mujoco") == std::string::npos) return Error{"invalid_model", "not MJCF"};
        std::smatch m;
        static const std::regex wheel(R"re(name="wheel_l"[^>]*size="([0-9.]+))re");
        wheelRadiusM_ = std::regex_search(xml, m, wheel) ? std::stod(m[1]) : 0.03;
        static const std::regex track(R"re(name="wheel_l_body"[^>]*pos="[-0-9.]+ ([0-9.]+))re");
        halfTrackM_ = std::regex_search(xml, m, track) ? std::stod(m[1]) : 0.068;
        loaded_ = true;
        reset(0);
        return Ok{};
    }
    void reset(uint64_t seed) override {
        seed_ = seed;
        t_ = x_ = y_ = heading_ = speed_ = spin_ = 0;
        hash_ = 1469598103934665603ull ^ seed;
        input_ = {};
    }
    void setDriveInput(const DriveInput& in) override { input_ = in; }
    void setControl(const std::string&, double) override {}
    void step(double dt) override {
        if (!loaded_) return;
        const double sub = 0.002;
        for (double done = 0; done + 1e-12 < dt; done += sub) {
            double maxSpeed = 0.5 * surfaceFactor();
            double target = input_.throttle * maxSpeed;
            speed_ += (target - speed_) * std::min(1.0, sub * 8.0);
            heading_ += input_.steer * 2.0 * sub;
            spin_ += speed_ / wheelRadiusM_ * sub;
            x_ += std::cos(heading_) * speed_ * sub;
            y_ += std::sin(heading_) * speed_ * sub;
            t_ += sub;
            mix(x_); mix(y_); mix(heading_);
        }
    }
    double time() const override { return t_; }
    // "rover", and its wheels "wheel_l_body" / "wheel_r_body" (on the axle, turning as they roll).
    Result<Transform> bodyPose(const std::string& body) const override {
        const bool l = body == "wheel_l_body", r = body == "wheel_r_body";
        if (body != "rover" && !l && !r) return Error{"not_found", body};
        Transform tr;
        tr.position = {x_ * 1000, y_ * 1000, wheelRadiusM_ * 1000};
        tr.rotation = {std::cos(heading_ / 2), 0, 0, std::sin(heading_ / 2)};
        if (l || r) {
            const double side = (l ? 1 : -1) * halfTrackM_ * 1000;
            tr.position = {tr.position.x - std::sin(heading_) * side, tr.position.y + std::cos(heading_) * side, tr.position.z};
            // heading about z, then the roll about the axle (y)
            const Quat h = tr.rotation, roll{std::cos(spin_ / 2), 0, std::sin(spin_ / 2), 0};
            tr.rotation = {h.w * roll.w - h.z * roll.z, h.w * roll.x - h.z * roll.y, h.w * roll.y + h.z * roll.x,
                           h.w * roll.z + h.z * roll.w};
        }
        return tr;
    }
    std::map<std::string, double> telemetry() const override {
        return {{"speed_mps", speed_}, {"on_rug", onRug() ? 1.0 : 0.0}, {"battery_pct", 100 - t_ * 0.01},
                {"current_a", 0.2 + std::abs(input_.throttle) * (onRug() ? 1.1 : 0.6)}, {"rssi_dbm", -48}};
    }
    uint64_t stateHash() const override { return hash_; }

private:
    // Rug: 8 mm pile, from x = 0.3 m to 1.5 m. Small wheels lose most of their speed on it.
    bool onRug() const { return x_ >= 0.3 && x_ <= 1.5; }
    double surfaceFactor() const {
        if (!onRug()) return 1.0;
        double d = wheelRadiusM_ * 2000;  // diameter in mm
        return std::clamp((d - 40.0) / 50.0, 0.1, 0.9);  // Ø60 -> 0.4, Ø80 -> 0.8
    }
    void mix(double v) {
        long long q = std::llround(v * 1e6);
        hash_ = (hash_ ^ uint64_t(q)) * 1099511628211ull;
    }

    bool loaded_ = false;
    uint64_t seed_ = 0, hash_ = 0;
    double t_ = 0, x_ = 0, y_ = 0, heading_ = 0, speed_ = 0, spin_ = 0, wheelRadiusM_ = 0.03, halfTrackM_ = 0.068;
    DriveInput input_;
};

}  // namespace fulmetry::fakes
