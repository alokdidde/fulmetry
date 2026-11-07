// Simulate: a drive test of one object in a room (fixtures/rover/sim/living-room.json). Owned by S5.
//
// GUI-free: it steps an ISimulation (MuJoCo in the app, FakeSimulation in tests), moves the object's parts on an
// IViewport to the physics poses, keeps the telemetry the drive-test card shows and turns what happened into
// findings. It reports what the physics measured, nothing scripted.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fulmetry/core/ISimulation.h"
#include "fulmetry/core/IViewport.h"
#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry::ui {

struct DriveStep {
    double seconds = 0;
    DriveInput input;
};

// The room and how the object maps onto the physics model.
struct SimSetup {
    std::string name;         // "Living room"
    std::string description;  // "Wood floor, an 8 mm pile rug..."
    std::string model;        // MJCF file, relative to the setup file
    ObjectId object;
    // MJCF body -> the parts that ride on it; "*" is every part of the object not listed under another body.
    std::map<std::string, std::vector<PartId>> bodies;
    std::string floorName;  // "Wood floor"
    StageEnvironment environment;
    std::vector<Aabb> surfaceBoxes;  // props marked "surface": the rover is on them (named by the prop)
    std::vector<std::string> surfaceNames;
    std::vector<DriveStep> route;  // the scripted test drive
};

// Parses a setup file (JSON). Unknown parts or a missing object: "invalid_setup".
Result<SimSetup> parseSimSetup(const std::string& json, const Project& project);

struct Finding {
    std::string id;
    bool good = true;
    std::string text;
    std::optional<PartId> part;  // "Fix it" starts an agent on this part
    std::string fixText;         // what that agent is asked
};

struct DriveSample {
    double t = 0;
    double speed = 0;     // m/s
    bool onSurface = false;
};

class DriveTest {
public:
    DriveTest(ISimulation& sim, IViewport* viewport, Project project, SimSetup setup);

    // Loads the model, puts the room on the stage and the object at its start. Errors from the simulation.
    Status start(const std::string& mjcfXml);
    // Takes the room and the poses off the stage.
    void stop();
    bool running() const { return running_; }

    // The scripted drive (route) or the person's keys. A manual input stops the script.
    void runRoute();
    bool onRoute() const { return route_; }
    bool routeDone() const;
    void setDrive(const DriveInput& input);
    // Restarts from the start pose; findings are kept until the next start.
    void restart();

    // Advances the physics by dt (fixed substeps inside the simulation), moves the parts and updates the card.
    void step(double dt);

    // ---- what the card shows
    const SimSetup& setup() const { return setup_; }
    double time() const { return sim_.time(); }
    double speed() const { return speed_; }  // m/s, along the chassis
    std::string surface() const;            // "Rug, 8 mm pile" or the floor's name
    bool onSurface() const { return onSurface_; }
    double batteryPct() const { return battery_; }
    double currentA() const { return current_; }
    double peakCurrentA() const { return peakCurrent_; }
    double rssiDbm() const { return rssi_; }
    double distanceM() const { return distance_; }
    const DriveInput& input() const { return input_; }
    const std::vector<DriveSample>& history() const { return history_; }
    const std::vector<Finding>& findings() const { return findings_; }
    double wheelDiameterMm() const { return wheelMm_; }
    // Where the object's body is now (world, mm), for the chase camera.
    Vec3 position() const { return position_; }
    // The camera that follows the object: `current` moved by how far the object went since the last call, so
    // orbit and zoom are kept. startCamera frames the start and resets that.
    CameraState chase(const CameraState& current);
    CameraState startCamera();

private:
    void applyPoses();
    void observe(double dt);
    void addFinding(Finding f);
    void surfaceFinding();
    void routeFindings();
    std::string obstacle() const;

    ISimulation& sim_;
    IViewport* vp_;
    Project project_;
    SimSetup setup_;
    std::map<PartId, std::string> bodyOf_;
    std::string mainBody_ = "rover";  // the body "*" parts ride on
    Vec3 cameraAnchor_;
    std::map<std::string, Transform> rest_;  // each body's pose at the start
    bool running_ = false, route_ = false;
    size_t routeIndex_ = 0;
    double routeT_ = 0;
    DriveInput input_;
    double speed_ = 0, battery_ = 100, current_ = 0, peakCurrent_ = 0, rssi_ = 0, distance_ = 0;
    bool onSurface_ = false;
    std::string surfaceName_;
    Vec3 position_, lastPosition_;
    double wheelMm_ = 0;
    double sampleT_ = 0;
    std::vector<DriveSample> history_;
    std::vector<Finding> findings_;
    // rug crossing: floor cruise speed and time on the rug at full throttle
    double floorCruise_ = 0, surfaceSpeedSum_ = 0, surfaceTime_ = 0, surfaceEnter_ = -1, stuckT_ = 0;
    int stalls_ = 0;
};

}  // namespace fulmetry::ui
