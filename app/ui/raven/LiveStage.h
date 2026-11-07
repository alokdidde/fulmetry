// The live stage: an IViewport (the Wicked render, S2) drawn into the workbench's <Stage> and driven by the mouse.
//
//  - Drag orbits round the subject, right- or middle-drag pans, the wheel zooms towards the cursor.
//  - While the camera moves each frame is one quick render (hard shadow); once it rests for `idleDelay`, one
//    soft-shadow render (`idleFrames` frames averaged) replaces it.
//  - A click picks a part and selects it in the WorkbenchModel (part card, tray row); a click on nothing clears
//    the selection. The part under a resting cursor is hovered.
//  - The model drives the viewport: object tab (the view fits it), selection outline, explode, x-ray.
//  - The pinned comment follows its anchor through IViewport::project.
//
// Only core types here (IViewport): the Wicked specifics come in through Options hooks. Owned by S5; wired by
// the integrator in main.cpp.
#pragma once
#include <Raven.h>

#include <functional>
#include <optional>
#include <string>

#include "WorkbenchView.h"
#include "fulmetry/core/IViewport.h"
#include "fulmetry/ui/WorkbenchModel.h"

namespace fulmetry::ui {

class LiveStage {
public:
    struct Options {
        int idleFrames = 8;          // soft-shadow frames of the resting render
        double idleDelay = 0.25;     // seconds without input before it
        double hoverDelay = 0.12;    // seconds a cursor rests before the part under it is hovered
        float interactiveScale = 1;  // resolution of the moving renders (the resting one is always full size)
        bool log = true;             // prints renders, picks and camera to stdout (the X11 test reads them)
        // Frames averaged by the next renderToImage (WickedViewport::setSoftShadowFrames).
        std::function<void(int)> setFrames;
        // Where resetView puts the subject, as fractions of the view (WickedViewport::setFramingRect).
        std::function<void(double, double, double, double)> setFraming;
    };

    LiveStage(IViewport& viewport, WorkbenchModel& model, WorkbenchView& view, Options options);

    // After view.build(): takes the stage's mouse input and the pinned comment.
    void attach();
    // Every frame, after view.update(): applies the model, renders when something changed.
    void update(double now);
    // Simulate mode: the simulation owns what is shown and the camera, so the object tabs, explode and fitting
    // the view wait; leaving it shows the model's view again.
    void setSimulating(bool on);
    // The scene moved (a simulation step): renders a quick frame now and a resting one once it stops.
    void invalidate() { changed(); }
    // Nothing changed since the last resting render.
    bool settled() const { return settled_; }

    // The gestures, also for scripts (--orbit) and tests. Pixels are stage widget units.
    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float notches, float x, float y);
    void click(float x, float y);
    void hover(float x, float y);
    void hoverPart(const std::optional<PartId>& part);
    void fit();

    // Prints "part-at <id> x y" (window units) for the parts the X11 test clicks.
    void reportParts() const;

    double interactiveMs() const { return interactiveMs_; }
    double idleMs() const { return idleMs_; }

private:
    static int onEvent(Rid widget, const REvent* e, void* user);
    bool stageSize(int& w, int& h, float& scale) const;
    std::optional<std::pair<float, float>> toStage(const Vec3& world) const;  // stage widget units
    std::optional<std::pair<float, float>> anchorOf(const Comment& c) const;
    std::optional<WorkbenchView::Rect> subjectRect() const;
    Vec3 pivot() const;
    void changed() { dirty_ = true; settled_ = false; lastChange_ = now_; }
    void render(bool resting);
    void syncModel();

    IViewport& vp_;
    WorkbenchModel& model_;
    WorkbenchView& view_;
    Options opt_;

    double now_ = 0, lastChange_ = 0;
    bool dirty_ = true, settled_ = false, needFit_ = true, simulating_ = false;
    int width_ = 0, height_ = 0;  // stage in pixels: the viewport's size
    float scale_ = 1;             // pixels per stage unit

    // Mirrors of what the viewport shows.
    int shownTab_ = -1;
    std::optional<PartId> shownSel_, hovered_;
    bool shownExplode_ = false, shownXray_ = false;

    // Input.
    int button_ = -1;
    float downX_ = 0, downY_ = 0, lastX_ = 0, lastY_ = 0;
    bool dragged_ = false;
    std::optional<std::pair<float, float>> hoverAt_;
    double hoverSince_ = 0;
    std::optional<std::pair<float, float>> clickAt_;

    double interactiveMs_ = 0, idleMs_ = 0;
    int renders_ = 0;
};

}  // namespace fulmetry::ui
