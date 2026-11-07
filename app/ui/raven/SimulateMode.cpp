#include "SimulateMode.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fulmetry::ui {

using namespace kit;
using namespace kit::color;

namespace {

constexpr int kBars = 60;           // the trace: the last 6 s, one bar per 0.1 s sample
constexpr float kTraceHeight = 46;

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

void setText(Rid label, const std::string& text) {
    if (label != R_INVALID_ID) Rlabel_set_text(label, text.c_str());
}

}  // namespace

SimulateMode::SimulateMode(WorkbenchView& view, LiveStage* live, IViewport& viewport, std::unique_ptr<ISimulation> sim,
                           SimSetup setup, Options options)
    : view_(view),
      live_(live),
      vp_(viewport),
      sim_(std::move(sim)),
      opt_(std::move(options)),
      drive_(*sim_, &viewport, view.model().project(), std::move(setup)) {}

void SimulateMode::attach() {
    view_.setSimulateHandler([this] {
        if (running()) stop();
        else start();
    });
}

bool SimulateMode::start() {
    if (auto st = drive_.start(opt_.mjcf); !st.ok()) {
        std::fprintf(stderr, "simulate: %s\n", st.error().message.c_str());
        return false;
    }
    view_.setSimulating(true);
    if (live_) live_->setSimulating(true);
    vp_.setCamera(drive_.startCamera());
    drive_.runRoute();
    last_ = -1;
    shownFindings_ = size_t(-1);
    buildCard();
    std::printf("simulate: %s, %s\n", drive_.setup().name.c_str(), drive_.onRoute() ? "scripted drive" : "keys");
    std::fflush(stdout);
    return true;
}

void SimulateMode::stop() {
    drive_.stop();
    if (live_) live_->setSimulating(false);
    view_.setSimulating(false);
    Rwidget_clear_children(view_.widget("drive.body"));
    bars_.clear();
}

// ------------------------------------------------------------------ frame

void SimulateMode::keys() {
    Rid w = view_.window();
    if (w == R_INVALID_ID || Rwindow_wants_text_input(w)) return;
    const bool up = Rwindow_is_key_down(w, R_KEY_W), down = Rwindow_is_key_down(w, R_KEY_S);
    const bool left = Rwindow_is_key_down(w, R_KEY_A), right = Rwindow_is_key_down(w, R_KEY_D);
    const bool r = Rwindow_is_key_down(w, R_KEY_R);
    if (r && !rWasDown_) {
        drive_.restart();
        vp_.setCamera(drive_.startCamera());
        keyDriving_ = false;
    }
    rWasDown_ = r;
    if (up || down || left || right) {
        DriveInput in;
        in.throttle = (up ? 1.0 : 0.0) - (down ? 1.0 : 0.0);
        in.steer = (left ? 1.0 : 0.0) - (right ? 1.0 : 0.0);
        if (!keyDriving_) {
            std::printf("simulate: driving by hand\n");
            std::fflush(stdout);
        }
        drive_.setDrive(in);
        keyDriving_ = true;
    } else if (keyDriving_) {
        drive_.setDrive({});  // keys up: coast to a stop
        keyDriving_ = false;
    }
}

void SimulateMode::update(double now) {
    if (!running()) return;
    const double dt = last_ < 0 ? 0 : std::clamp(now - last_, 0.0, 0.05);
    last_ = now;
    keys();
    bool moved = false;
    if (opt_.fixedSeconds > 0) {
        // Up to a second of physics a frame, in the same 60 Hz steps, then hold.
        for (int i = 0; i < 60 && drive_.time() < opt_.fixedSeconds - 1e-9; ++i, moved = true) drive_.step(1.0 / 60);
    } else if (dt > 0) {
        drive_.step(dt);
        moved = true;
    }
    if (moved) {
        vp_.setCamera(drive_.chase(vp_.camera()));
        if (live_) live_->invalidate();
    }
    if (moved || shownAt_ < 0) {
        if (now - shownAt_ >= 0.1 || opt_.fixedSeconds > 0) {
            shownAt_ = now;
            refresh();
        }
    }
}

// ------------------------------------------------------------------ the card

void SimulateMode::buildCard() {
    Rid body = view_.widget("drive.body");
    Rwidget_clear_children(body);
    bars_.clear();
    const Fonts& f = view_.fonts();
    const SimSetup& s = drive_.setup();

    Rid head = vbox(body, 0);
    view_.eyebrow(head, "Simulation \xC2\xB7 " + s.name);
    Rid h2 = label(head, "DRIVE TEST", f.tightBold, 23, kInk);
    Rslot_set_padding(h2, 0, 2, 0, 0);
    Rid sub = wrapLabel(head, s.description + ".", f.regular, 12.5f, kMute);
    Rslot_set_padding(sub, 0, 3, 0, 0);

    Rid st = border(body, 0xE7F0FD, 1, 9, 9, 2);
    Rslot_set_halign(st, R_HALIGN_LEFT);
    state_ = label(st, "", f.semibold, 11.5f, 0x1452B3);

    // telemetry: two columns of name and value
    Rid grid = Rwidget_add_child(body, Rwidget_create(R_WIDGET_UNIFORM_GRID));
    Runiform_grid_set_columns(grid, 2);
    Rpanel_set_spacing(grid, 0);
    auto cell = [&](const char* name) {
        Rid c = vbox(grid, 1);
        Rslot_set_padding(c, 0, 4, 0, 4);
        view_.eyebrow(c, name, 10.5f);
        return label(c, "", f.mono, 14, kInk);
    };
    speed_ = cell("Speed");
    surface_ = cell("Surface");
    battery_ = cell("Battery");
    current_ = cell("Motor current");
    signal_ = cell("Signal");
    distance_ = cell("Distance");

    // the speed trace, blue where the wheels were on the rug
    Rid traceHead = hbox(body, 6);
    Rid tl = view_.eyebrow(traceHead, "Speed \xC2\xB7 last 6 s", 10.5f);
    Rslot_set_fill(tl, 1);
    Rslot_set_valign(tl, R_VALIGN_CENTER);
    swatch(traceHead, kRun, 8, 2);
    Rid rugL = label(traceHead, "on the rug", f.regular, 11, kMute);
    Rslot_set_valign(rugL, R_VALIGN_CENTER);
    Rid traceBox = border(body, kHud2, 1, 4, 6, 6);
    Rslot_set_padding(traceBox, 0, -6, 0, 0);
    outline(traceBox, kLine);
    trace_ = hbox(traceBox, 1);
    Rwidget_set_size(trace_, -1, kTraceHeight);
    for (int i = 0; i < kBars; ++i) {
        Rid b = border(trace_, kBody, 0, 1, 0, 0);
        Rwidget_set_size(b, 3.5f, 1);
        Rslot_set_valign(b, R_VALIGN_BOTTOM);
        bars_.push_back(b);
    }
    time_ = label(body, "", f.mono, 11, kMute);
    Rslot_set_padding(time_, 0, -8, 0, 0);

    view_.eyebrow(body, "Findings");
    findings_ = vbox(body, 6);
    Rslot_set_padding(findings_, 0, -6, 0, 0);

    Rid buttons = hbox(body, 8);
    spacer(buttons, 1);
    view_.button(buttons, "Restart", false, [this] {
        drive_.restart();
        vp_.setCamera(drive_.startCamera());
        if (live_) live_->invalidate();
        refresh();
    }, "reset");
    route_ = view_.button(buttons, "Run the drive", true, [this] {
        drive_.runRoute();
        vp_.setCamera(drive_.startCamera());
        if (live_) live_->invalidate();
        refresh();
    }, "play");
    refresh();
}

void SimulateMode::buildFindings() {
    Rwidget_clear_children(findings_);
    const Fonts& f = view_.fonts();
    if (drive_.findings().empty()) {
        wrapLabel(findings_, drive_.onRoute() ? "Measuring as it drives\xE2\x80\xA6" : "Nothing measured yet.", f.regular,
                  12, kMute);
        return;
    }
    for (const Finding& x : drive_.findings()) {
        Rid card = border(findings_, kHud2, 1, 4, 10, 8);
        outline(card, kLine);
        Rid row = hbox(card, 9);
        Rid dot = border(row, x.good ? kPass : kAccent, 1, 4, 0, 0);
        Rwidget_set_size(dot, 8, 8);
        Rslot_set_valign(dot, R_VALIGN_TOP);
        Rslot_set_padding(dot, 0, 5, 0, 0);
        Rid t = wrapLabel(row, x.text, f.regular, 12.5f, kInk);
        Rslot_set_fill(t, 1);
        if (x.part) {
            Rid fix = view_.button(row, "Fix it", true, [this, part = *x.part, text = x.fixText] {
                std::printf("simulate: fix it on %s\n", part.value.c_str());
                std::fflush(stdout);
                view_.askAbout(part, text);
            });
            Rslot_set_valign(fix, R_VALIGN_TOP);
        }
    }
}

void SimulateMode::refresh() {
    if (state_ == R_INVALID_ID) return;
    const DriveTest& d = drive_;
    const bool done = d.routeDone() && !d.onRoute();
    const bool byHand = d.input().throttle != 0 || d.input().steer != 0;
    setText(state_, d.onRoute() ? "Running the scripted drive" : byHand ? "Driving by hand"
                    : done && !d.findings().empty() ? "Drive done \xC2\xB7 W/S to drive it yourself"
                                                    : "Ready \xC2\xB7 W/S to drive");
    setText(speed_, fmt("%.2f m/s", std::abs(d.speed())));
    setText(surface_, d.surface());
    setRgb(surface_, d.onSurface() ? kRun : kInk);
    setText(battery_, fmt("%.2f %%", d.batteryPct()));
    setText(current_, fmt("%.2f A", d.currentA()) + fmt("  peak %.1f", d.peakCurrentA()));
    setText(signal_, fmt("%.0f dBm", d.rssiDbm()));
    setText(distance_, fmt("%.2f m", d.distanceM()));
    setText(time_, fmt("sim time %.1f s", d.time()) + (d.wheelDiameterMm() > 0
                       ? fmt("  \xC2\xB7  \xC3\x98%.0f mm wheels", d.wheelDiameterMm()) : std::string()));

    const auto& h = d.history();
    double top = 0.6;
    for (const auto& s : h) top = std::max(top, std::abs(s.speed) * 1.1);
    const size_t first = h.size() > bars_.size() ? h.size() - bars_.size() : 0;
    for (size_t i = 0; i < bars_.size(); ++i) {
        const size_t k = first + i;
        if (k >= h.size()) {
            Rwidget_set_size(bars_[i], 3.5f, 1);
            setBackground(bars_[i], kBody, 0);
            continue;
        }
        const float hgt = std::max(1.0f, static_cast<float>(std::abs(h[k].speed) / top) * kTraceHeight);
        Rwidget_set_size(bars_[i], 3.5f, hgt);
        setBackground(bars_[i], h[k].onSurface ? kRun : kBody, h[k].onSurface ? 1.0f : 0.55f);
    }
    if (d.findings().size() != shownFindings_) {
        shownFindings_ = d.findings().size();
        buildFindings();
    }
}

}  // namespace fulmetry::ui
