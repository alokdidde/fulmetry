// What the workbench shows, derived from the project model and the chat. Owned by S5 (Workbench UI).
//
// No GUI types here: the Raven view (app/ui/raven) reads this model and calls its commands, so the
// selection, totals, parts list, checks and chat feed are unit-tested without a window.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "fulmetry/core/Events.h"
#include "fulmetry/core/IAgentClient.h"
#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry::ui {

// One object tab. `object` is empty for "All objects".
struct ObjectTab {
    std::optional<ObjectId> object;
    std::string name;
    int count = 0;  // parts in the object; objects in the project for "All objects"
};

struct Totals {
    double costUsd = 0;
    double massG = 0;
    int printMinutes = 0;  // estimate from the printed parts' mass (kPrintGramsPerHour)
    int supported = 0, refuted = 0, unresolved = 0;
};

struct PartRow {
    PartId id;
    std::string name;
    std::string color;  // "#rrggbb"
    std::string tag;    // print | board | buy | hw, as in the prototype's tray
};

struct PartGroup {
    std::string title;  // "Desk rover · parts", or the object name when several objects are listed
    std::vector<PartRow> rows;
};

struct Chip {
    PartId part;
    std::string label;  // part name, or a label given by the chat ("Wheels ×2")
    std::string color;
};

struct AgentCardView {
    AgentId id;
    std::string title;
    std::string status;   // current step, or "Done"
    bool done = false;
    std::string color;    // status dot
    std::string preview;  // latest reply
    std::vector<Chip> chips;
    int replies = 0;
    std::string harness;  // "Claude Code · Opus 5.5", empty when the back end did not say
};

// What the next chat message or comment runs on.
struct HarnessChoice {
    std::string harness;  // Harness::id
    std::string model;    // HarnessModel::id
    bool operator==(const HarnessChoice&) const = default;
};

struct ChatEntry {
    enum class Kind { Message, Divider };
    Kind kind = Kind::Message;
    std::string author;  // "Claude" or "You"
    std::string time;    // "10:05"
    std::string text;
    std::vector<Chip> chips;
    bool sentToAgent = false;
    std::optional<AgentCardView> card;
};

inline constexpr double kPrintGramsPerHour = 58.0;

// "3 h 24 m"
std::string formatPrintTime(int minutes);
// "$40.89"
std::string formatCost(double usd);

class WorkbenchModel {
public:
    explicit WorkbenchModel(Project project);

    const Project& project() const { return project_; }
    // Bumped by every change; the view rebuilds what it shows when it differs.
    uint64_t version() const { return version_; }
    uint64_t chatVersion() const { return chatVersion_; }

    // ---- scope and selection
    std::vector<ObjectTab> tabs() const;
    int activeTab() const { return activeTab_; }
    void selectTab(int index);
    std::optional<ObjectId> scope() const;  // empty: all objects
    const std::optional<PartId>& selectedPart() const { return selected_; }
    // Selects a part and shows its object (the tab follows). Unknown parts are ignored (returns false).
    bool selectPart(const PartId& part);
    void clearSelection();

    // ---- stage view modes (the Actions menu toggles them; the stage draws them)
    bool exploded() const { return exploded_; }
    void setExploded(bool on);
    bool xray() const { return xray_; }
    void setXray(bool on);

    // ---- derived views for the current scope
    Totals totals() const;
    std::vector<PartGroup> partGroups() const;
    int partCount() const;
    std::vector<const Check*> checks(const ObjectId& object) const;
    std::string objectSubtitle(const ObjectId& object) const;  // "13 parts · linked to Wireless remote"
    std::string aboutLabel() const;                             // composer's About chip
    Chip chip(const PartId& part, const std::string& label = {}) const;
    const Comment* openComment() const;  // the first comment that is not resolved, pinned on the stage

    // ---- chat
    const std::vector<ChatEntry>& chat() const { return chat_; }
    // Loads the sample chat (app/ui/assets/sample-chat.json). Errors: "parse_error".
    Status loadChat(const std::string& json);
    // Adds the person's message (pinned to the selected part, if any) and returns the request for the agent
    // back end. Empty text: "invalid_argument"; no harness ready: "no_harness" (nothing is added).
    Result<ChatRequest> compose(const std::string& text, const std::string& time);
    // ---- harnesses (the onboarding screen and the composer's picker)
    const std::vector<Harness>& harnesses() const { return harnesses_; }
    uint64_t harnessVersion() const { return harnessVersion_; }
    void setHarnesses(std::vector<Harness> harnesses);
    const Harness* harness(const std::string& id) const;
    std::vector<const Harness*> readyHarnesses() const;
    // True while no harness is ready: the onboarding screen opens on its own.
    bool needsOnboarding() const { return readyHarnesses().empty(); }
    // The chosen harness and model when they are ready, else the first ready harness and its default model; empty
    // when none is ready. The stored choice is kept, so it comes back once its harness is ready again.
    HarnessChoice choice() const;
    // Chooses what new agents run on. False (and nothing changes) for an unknown or unready harness or model.
    bool choose(const std::string& harness, const std::string& model = {});
    const HarnessChoice& preferredChoice() const { return preferred_; }
    void setPreferredChoice(HarnessChoice c);  // from saved settings; not checked against the harnesses
    // "Claude Code · Opus 5.5", or "No harness connected".
    std::string choiceLabel() const;
    std::string harnessLabel(const std::string& harness, const std::string& model) const;

    // Applies a back-end event: ChatPosted, AgentUpdated, RevisionSaved, CheckUpdated, CommentUpdated,
    // HarnessesUpdated.
    void apply(const Event& event, const std::string& time);

private:
    void touch() { ++version_; }
    void touchChat() { ++chatVersion_; ++version_; }
    AgentCardView cardFrom(const AgentCard& card) const;

    Project project_;
    int activeTab_ = 1;  // the first object, as in the prototype
    std::optional<PartId> selected_;
    bool exploded_ = false, xray_ = false;
    std::vector<ChatEntry> chat_;
    uint64_t version_ = 1, chatVersion_ = 1, harnessVersion_ = 1;
    std::vector<Harness> harnesses_;
    HarnessChoice preferred_;
};

}  // namespace fulmetry::ui
