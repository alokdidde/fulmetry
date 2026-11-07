// The M1 workbench shell in Raven: assets/workbench.xml for the frame, the model for the content.
// Owned by S5.
#pragma once
#include <Raven.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "Kit.h"
#include "StageImage.h"
#include "fulmetry/core/Events.h"
#include "fulmetry/core/IAgentClient.h"
#include "fulmetry/ui/WorkbenchModel.h"

namespace fulmetry::ui {

class WorkbenchView {
public:
    struct Options {
        std::string assets;      // app/ui/assets: workbench.xml, theme.xml, fonts, icons, stage
        bool chatLeft = false;   // the "Dock left" setting
        double agentPollSeconds = 0.7;
    };

    WorkbenchView(WorkbenchModel& model, IAgentClient* agent, EventBus& bus, Options options);

    // Loads fonts and theme, registers <Stage>, builds the window. False when the layout cannot be built.
    bool build();
    Rid window() const { return window_; }
    StageImage& stage() { return stage_; }
    // Frame-space anchor of a part on the current stage image, for the pinned comment.
    void setPinAnchor(const PartId& part, float x, float y) {
        anchors_[part.value] = {x, y};
        shownVersion_ = 0;  // rebuild the pin on the next update
    }
    // Prints where the object tabs, the first chip of each part in the chat and Send are, in window
    // coordinates ("tab-at 1 362 33"), for the X11 test script (app/ui/tests/shots.sh).
    void reportLayout() const;

    // Call once per frame, after Rdraw_frame/Rwait_frame: runs queued commands, polls the agent back end,
    // brings the widgets up to date with the model and keeps the pinned comment over its part.
    void update(double now);

    // Commands (the buttons call these; the headless run and the tests call them directly).
    void setChatLeft(bool left);
    bool chatLeft() const { return chatLeft_; }
    void setTrayOpen(bool open);
    void setMenuOpen(bool open);
    void send();
    // Queues work for update(): widget callbacks must not destroy the widgets they run on.
    void post(std::function<void()> fn) { queue_.push_back(std::move(fn)); }

private:
    Rid find(const char* name) const;
    void bindStatic();
    void sync();
    void buildTabs();
    void buildTotals();
    void buildTray();
    void buildInspector();
    void buildMenu();
    void buildPin();
    void buildChat();
    void placePin();
    void selectPart(const PartId& part);

    Rid checkRow(Rid parent, const Check& c);
    Rid pill(Rid parent, Verdict v);
    Rid chip(Rid parent, const Chip& c);
    Rid closeButton(Rid parent, std::function<void()> onClick);
    Rid eyebrow(Rid parent, const std::string& text, float size = 12);
    void chatEntry(Rid list, const ChatEntry& e);
    void agentCard(Rid parent, const AgentCardView& card);
    std::string now() const;

    WorkbenchModel& model_;
    IAgentClient* agent_;
    EventBus& bus_;
    Options opt_;
    kit::Fonts fonts_;
    StageImage stage_;
    Rid window_ = R_INVALID_ID;
    bool chatLeft_ = false;
    bool trayOpen_ = false, menuOpen_ = false, inspectorOpen_ = true;
    uint64_t shownVersion_ = 0, shownChat_ = 0;
    double lastPoll_ = 0;
    int scrollFrames_ = 0;
    std::vector<Rid> tabWidgets_;
    std::map<std::string, Rid> chatChips_;  // part id -> its first chip in the chat
    std::vector<std::function<void()>> queue_;
    std::map<std::string, std::pair<float, float>> anchors_;
};

}  // namespace fulmetry::ui
