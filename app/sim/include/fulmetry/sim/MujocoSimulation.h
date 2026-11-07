// ISimulation on MuJoCo (S4). Built only when FULMETRY_WITH_MUJOCO is ON.
// MuJoCo types stay inside the .cpp, so including this header needs no MuJoCo include path.
#pragma once
#include <memory>

#include "fulmetry/core/ISimulation.h"

namespace fulmetry::sim {

// Rover conventions read from the MJCF:
//   body "rover"              the chassis (free joint); bodyPose("rover") is its frame in mm.
//   actuators drive_l/drive_r velocity servos on the wheel hinges; ctrl is the wheel speed in rad/s.
//   geom "rug"                optional; telemetry on_rug is 1 while any rover geom touches it.
// Drive mapping: wheel target = clamp(throttle -/+ steer, -1, 1) * max wheel speed. The max wheel speed is the
// motor's, so it is the same for every wheel size: the upper ctrlrange of drive_l when the MJCF limits it,
// otherwise kDefaultMaxWheelRadPerS.
struct MujocoSimulationOptions {
    static constexpr double kDefaultMaxWheelRadPerS = 16.755;  // 160 rpm gearmotor: 0.50 m/s on Ø60 mm wheels
    double maxWheelRadPerS = kDefaultMaxWheelRadPerS;
    double batteryCapacityMah = 2000;     // 2S 18650 pack
    double motorTorqueConstantNmPerA = 0.25;  // geared TT-class motor, at the wheel
    double idleCurrentA = 0.15;           // MCU + radio
    // Switch a model that uses the default Euler integrator to implicitfast (stable with velocity servos).
    bool preferImplicitIntegrator = true;
};

class MujocoSimulation : public ISimulation {
public:
    explicit MujocoSimulation(MujocoSimulationOptions options = {});
    ~MujocoSimulation() override;
    MujocoSimulation(const MujocoSimulation&) = delete;
    MujocoSimulation& operator=(const MujocoSimulation&) = delete;

    Status loadModel(const std::string& mjcfXml) override;
    void reset(uint64_t seed) override;
    void setDriveInput(const DriveInput& input) override;
    void setControl(const std::string& actuator, double value) override;
    void step(double dtSeconds) override;
    double time() const override;
    Result<Transform> bodyPose(const std::string& body) const override;
    std::map<std::string, double> telemetry() const override;
    uint64_t stateHash() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::unique_ptr<ISimulation> makeMujocoSimulation(MujocoSimulationOptions options = {});

}  // namespace fulmetry::sim
