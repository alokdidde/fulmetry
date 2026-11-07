// Simulate mode of the workbench: the Simulate button puts the object in a room (DriveTest), the stage follows it
// and the drive-test card (telemetry, a speed trace, findings) takes the inspector's place. Owned by S5.
//
// Keys while it runs: W/S drive, A/D steer, R restarts, Esc stops (WorkbenchView's escape action).
#pragma once
#include <Raven.h>

#include <memory>
#include <string>
#include <vector>

#include "LiveStage.h"
#include "WorkbenchView.h"
#include "fulmetry/core/ISimulation.h"
#include "fulmetry/core/IViewport.h"
#include "fulmetry/ui/DriveTest.h"

namespace fulmetry::ui {

class SimulateMode {
public:
    struct Options {
        std::string mjcf;          // the room's physics model (MJCF text)
        double fixedSeconds = 0;   // > 0: steps the physics at 60 Hz to this sim time as fast as it can, then holds
                                   // (deterministic screenshots); 0: real time
    };
    SimulateMode(WorkbenchView& view, LiveStage* live, IViewport& viewport, std::unique_ptr<ISimulation> sim,
                 SimSetup setup, Options options);

    // After view.build() and live->attach(): takes the Simulate button.
    void attach();
    bool start();
    void stop();
    bool running() const { return drive_.running(); }
    // Every frame, after view.update() and before live->update().
    void update(double now);
    const DriveTest& drive() const { return drive_; }
    // Drives by hand from now on, as the keys do (stops the scripted drive).
    void setDrive(const DriveInput& input) { drive_.setDrive(input); }

private:
    void buildCard();
    void buildFindings();
    void refresh();
    void keys();

    WorkbenchView& view_;
    LiveStage* live_;
    IViewport& vp_;
    std::unique_ptr<ISimulation> sim_;
    Options opt_;
    DriveTest drive_;
    double last_ = -1, shownAt_ = -1;
    bool keyDriving_ = false, rWasDown_ = false;
    size_t shownFindings_ = size_t(-1);
    // the card's live labels
    Rid speed_ = R_INVALID_ID, surface_ = R_INVALID_ID, battery_ = R_INVALID_ID, current_ = R_INVALID_ID;
    Rid signal_ = R_INVALID_ID, time_ = R_INVALID_ID, distance_ = R_INVALID_ID, state_ = R_INVALID_ID;
    Rid trace_ = R_INVALID_ID, findings_ = R_INVALID_ID, route_ = R_INVALID_ID;
    std::vector<Rid> bars_;
};

}  // namespace fulmetry::ui
