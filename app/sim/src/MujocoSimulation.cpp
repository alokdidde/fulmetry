#include "fulmetry/sim/MujocoSimulation.h"

#include <mujoco/mujoco.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace fulmetry::sim {
namespace {

struct ModelDeleter {
    void operator()(mjModel* m) const { mj_deleteModel(m); }
};
struct DataDeleter {
    void operator()(mjData* d) const { mj_deleteData(d); }
};

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void fnv(uint64_t& h, const void* bytes, size_t n) {
    const auto* p = static_cast<const unsigned char*>(bytes);
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kFnvPrime;
}
void fnvDoubles(uint64_t& h, const mjtNum* v, int n) {
    for (int i = 0; i < n; ++i) {
        double x = v[i] == 0 ? 0.0 : double(v[i]);  // fold -0 into +0
        fnv(h, &x, sizeof x);
    }
}

}  // namespace

struct MujocoSimulation::Impl {
    MujocoSimulationOptions opt;
    std::unique_ptr<mjModel, ModelDeleter> m;
    std::unique_ptr<mjData, DataDeleter> d;
    int driveL = -1, driveR = -1, roverBody = -1, rugGeom = -1;
    double maxWheel = MujocoSimulationOptions::kDefaultMaxWheelRadPerS;
    uint64_t seed = 0;
    double pending = 0;      // simulated time owed to the caller, less than one timestep
    double usedMah = 0;      // battery charge drawn since reset
    double currentA = 0;     // last estimate

    void applyCtrl(int id, double v) {
        if (id < 0) return;
        if (m->actuator_ctrllimited[id]) v = std::clamp(v, m->actuator_ctrlrange[2 * id], m->actuator_ctrlrange[2 * id + 1]);
        d->ctrl[id] = v;
    }

    double estimateCurrent() const {
        double a = opt.idleCurrentA;
        for (int id : {driveL, driveR}) {
            if (id < 0) continue;
            int j = m->actuator_trnid[2 * id];
            double w = (j >= 0 && m->actuator_trntype[id] == mjTRN_JOINT) ? d->qvel[m->jnt_dofadr[j]] : 0;
            // load current from torque, plus a small no-load current that grows with speed
            a += std::abs(d->actuator_force[id]) / opt.motorTorqueConstantNmPerA + 0.08 * std::min(1.0, std::abs(w) / maxWheel);
        }
        return a;
    }

    bool roverOnRug() const {
        if (rugGeom < 0 || roverBody < 0) return false;
        for (int i = 0; i < d->ncon; ++i) {
            const mjContact& c = d->contact[i];
            int other = c.geom[0] == rugGeom ? c.geom[1] : c.geom[1] == rugGeom ? c.geom[0] : -1;
            if (other >= 0 && m->body_rootid[m->geom_bodyid[other]] == m->body_rootid[roverBody]) return true;
        }
        return false;
    }

    double roverForwardSpeed() const {
        if (roverBody < 0) return 0;
        mjtNum vel[6];  // [angular; linear] in world frame at the body origin
        mj_objectVelocity(m.get(), d.get(), mjOBJ_BODY, roverBody, vel, 0);
        const mjtNum* R = d->xmat + 9 * roverBody;  // column 0 is the body's forward (+x) axis
        return vel[3] * R[0] + vel[4] * R[3] + vel[5] * R[6];
    }
};

MujocoSimulation::MujocoSimulation(MujocoSimulationOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->opt = options;
}
MujocoSimulation::~MujocoSimulation() = default;

Status MujocoSimulation::loadModel(const std::string& xml) {
    if (xml.find("<mujoco") == std::string::npos) return Error{"invalid_model", "not MJCF: no <mujoco> element"};
    if (xml.size() > size_t(1) << 30) return Error{"invalid_model", "MJCF too large"};

    mjVFS vfs;
    mj_defaultVFS(&vfs);
    static constexpr const char* kName = "fulmetry_model.xml";
    if (mj_addBufferVFS(&vfs, kName, xml.data(), int(xml.size())) != 0) {
        mj_deleteVFS(&vfs);
        return Error{"invalid_model", "could not stage MJCF in the MuJoCo VFS"};
    }
    std::array<char, 1024> err{};
    mjModel* raw = mj_loadXML(kName, &vfs, err.data(), int(err.size()));
    mj_deleteVFS(&vfs);
    if (!raw) return Error{"invalid_model", std::string("MuJoCo: ") + err.data()};

    std::unique_ptr<mjModel, ModelDeleter> m(raw);
    // Velocity servos on light wheels (I/kv ~ 0.2 ms) are unstable under the explicit Euler default at 2 ms.
    // implicitfast integrates actuator damping implicitly; MuJoCo recommends it for velocity actuators.
    if (impl_->opt.preferImplicitIntegrator && m->opt.integrator == mjINT_EULER) m->opt.integrator = mjINT_IMPLICITFAST;
    std::unique_ptr<mjData, DataDeleter> d(mj_makeData(m.get()));
    if (!d) return Error{"invalid_model", "MuJoCo: mj_makeData failed"};

    auto& s = *impl_;
    s.m = std::move(m);
    s.d = std::move(d);
    s.driveL = mj_name2id(s.m.get(), mjOBJ_ACTUATOR, "drive_l");
    s.driveR = mj_name2id(s.m.get(), mjOBJ_ACTUATOR, "drive_r");
    s.roverBody = mj_name2id(s.m.get(), mjOBJ_BODY, "rover");
    s.rugGeom = mj_name2id(s.m.get(), mjOBJ_GEOM, "rug");
    s.maxWheel = s.opt.maxWheelRadPerS;
    if (s.driveL >= 0 && s.m->actuator_ctrllimited[s.driveL]) s.maxWheel = s.m->actuator_ctrlrange[2 * s.driveL + 1];
    reset(0);
    return Ok{};
}

void MujocoSimulation::reset(uint64_t seed) {
    auto& s = *impl_;
    s.seed = seed;
    s.pending = 0;
    s.usedMah = 0;
    if (!s.m) return;
    mj_resetData(s.m.get(), s.d.get());  // qpos0, zero qvel/ctrl/act, time 0
    mj_forward(s.m.get(), s.d.get());
    s.currentA = s.estimateCurrent();
}

void MujocoSimulation::setDriveInput(const DriveInput& in) {
    auto& s = *impl_;
    if (!s.m) return;
    double throttle = std::clamp(in.throttle, -1.0, 1.0), steer = std::clamp(in.steer, -1.0, 1.0);
    // positive steer turns left: right wheel faster than left
    s.applyCtrl(s.driveL, std::clamp(throttle - steer, -1.0, 1.0) * s.maxWheel);
    s.applyCtrl(s.driveR, std::clamp(throttle + steer, -1.0, 1.0) * s.maxWheel);
}

void MujocoSimulation::setControl(const std::string& actuator, double value) {
    auto& s = *impl_;
    if (!s.m) return;
    s.applyCtrl(mj_name2id(s.m.get(), mjOBJ_ACTUATOR, actuator.c_str()), value);
}

void MujocoSimulation::step(double dt) {
    auto& s = *impl_;
    if (!s.m || !(dt > 0)) return;
    const double h = s.m->opt.timestep;
    s.pending += dt;
    // Fixed substeps of the model's timestep; the remainder carries to the next call.
    auto n = static_cast<long long>(std::floor(s.pending / h + 1e-9));
    s.pending = std::max(0.0, s.pending - double(n) * h);
    for (long long i = 0; i < n; ++i) {
        mj_step(s.m.get(), s.d.get());
        s.currentA = s.estimateCurrent();
        s.usedMah += s.currentA * h / 3.6;  // A*s -> mAh
    }
    mj_forward(s.m.get(), s.d.get());  // contacts and kinematics match the new state
}

double MujocoSimulation::time() const { return impl_->d ? impl_->d->time : 0.0; }

Result<Transform> MujocoSimulation::bodyPose(const std::string& body) const {
    const auto& s = *impl_;
    if (!s.m) return Error{"not_loaded", "no model loaded"};
    int id = mj_name2id(s.m.get(), mjOBJ_BODY, body.c_str());
    if (id < 0) return Error{"not_found", body};
    const mjtNum* p = s.d->xpos + 3 * id;
    const mjtNum* q = s.d->xquat + 4 * id;
    Transform t;
    t.position = {p[0] * 1000, p[1] * 1000, p[2] * 1000};
    t.rotation = {q[0], q[1], q[2], q[3]};
    return t;
}

std::map<std::string, double> MujocoSimulation::telemetry() const {
    const auto& s = *impl_;
    if (!s.m) return {};
    double distM = 0;
    if (s.roverBody >= 0) {
        const mjtNum* p = s.d->xpos + 3 * s.roverBody;
        distM = std::sqrt(p[0] * p[0] + p[1] * p[1]);  // base station at the origin
    }
    return {
        {"speed_mps", s.roverForwardSpeed()},
        {"on_rug", s.roverOnRug() ? 1.0 : 0.0},
        {"current_a", s.currentA},
        {"battery_pct", std::clamp(100.0 * (1.0 - s.usedMah / s.opt.batteryCapacityMah), 0.0, 100.0)},
        // free-space path loss from -48 dBm at 1 m
        {"rssi_dbm", -48.0 - 20.0 * std::log10(std::max(distM, 1.0))},
    };
}

uint64_t MujocoSimulation::stateHash() const {
    const auto& s = *impl_;
    uint64_t h = kFnvOffset ^ s.seed;
    if (!s.m) return h;
    fnvDoubles(h, &s.d->time, 1);
    fnvDoubles(h, s.d->qpos, s.m->nq);
    fnvDoubles(h, s.d->qvel, s.m->nv);
    fnvDoubles(h, s.d->act, s.m->na);
    return h;
}

std::unique_ptr<ISimulation> makeMujocoSimulation(MujocoSimulationOptions options) {
    return std::make_unique<MujocoSimulation>(options);
}

}  // namespace fulmetry::sim
