// The home screen (the "home" overlay in workbench.xml): start a project from an idea, open a recent one or the
// sample. It is the launch screen when no project is given; the brand in the top bar opens it again.
#include <cstdio>

#include "WorkbenchView.h"

namespace fulmetry::ui {

using namespace kit;
using namespace kit::color;

namespace {

void setText(Rid label, const std::string& text) {
    if (label != R_INVALID_ID) Rlabel_set_text(label, text.c_str());
}

}  // namespace

void WorkbenchView::bindHome() {
    Rid idea = find("home.idea");
    Rwidget_set_font(idea, fonts_.regular);
    Rwidget_set_metric(idea, R_METRIC_FONT_SIZE, 14);
    Rtext_input_set_committed_callback(
        idea,
        [](Rid w, const char*, void* user) {
            auto* v = static_cast<WorkbenchView*>(user);
            const std::string text = Rtext_input_get_text(w) ? Rtext_input_get_text(w) : "";
            v->post([v, text] { v->createProject(text); });
        },
        this);
    onClick(find("home.create"), [this] {
        Rid input = find("home.idea");
        const std::string text = Rtext_input_get_text(input) ? Rtext_input_get_text(input) : "";
        post([this, text] { createProject(text); });
    }, {kAccent, 0xD12A25, 1, 1, true});
    onClick(find("home.close"), [this] { post([this] { setHomeOpen(false); }); }, {kHud2, 0xE9ECEF, 1, 1, true});
    onClick(find("home.agents"), [this] { post([this] { setOnboardOpen(true); }); }, {kHud2, 0xE9ECEF, 1, 1, true});
    onClick(find("brand"), [this] { post([this] { setHomeOpen(true); }); });
    Rid note = find("home.note");
    Rlabel_set_wrap(note, R_TRUE);
    setText(note,
            "Describe it in a sentence or two. The project opens with your idea as its first message in the project "
            "chat, where you and your coding agent take it from there.");
}

void WorkbenchView::setHomeLists(HomeLists lists) {
    home_ = std::move(lists);
    if (homeOpen_) buildHome();
}

void WorkbenchView::setHomeOpen(bool open) {
    // The home screen stands in for a project until one is opened: it closes only over a real one.
    if (!open && !homeClosable_) return;
    homeOpen_ = open;
    if (open) {
        setPickerOpen(false);
        setMenuOpen(false);
        buildHome();
    }
    Rwidget_set_visibility(find("home"), open ? R_VISIBLE : R_COLLAPSED);
    std::printf("home %s\n", open ? "open" : "closed");
    std::fflush(stdout);
}

void WorkbenchView::buildHome() {
    Rwidget_set_visibility(find("home.close"), homeClosable_ ? R_VISIBLE : R_HIDDEN);
    const bool signedIn = !model_.needsOnboarding();
    setText(find("home.harness"), signedIn ? model_.choiceLabel() : "Not signed in yet");
    setBackground(find("home.harness.dot"), signedIn ? kPass : kAccent);

    Rid lists = find("home.lists");
    Rwidget_clear_children(lists);
    auto section = [&](const std::string& title, const std::vector<ProjectEntry>& entries) {
        if (entries.empty()) return;
        Rid head = eyebrow(lists, title, 11);
        Rslot_set_padding(head, 0, 10, 0, 2);
        for (const auto& e : entries) {
            Rid row = border(lists, kHud, 1, 4, 16, 12);
            outline(row, kLine);
            Rid line = hbox(row, 12);
            Rid text = vbox(line, 3);
            Rslot_set_fill(text, 1);
            label(text, e.name, fonts_.semibold, 14.5, kInk);
            label(text, e.detail, fonts_.regular, 12.5, kMute);
            Rid open = label(line, "Open", fonts_.semibold, 13, kInk);
            Rslot_set_valign(open, R_VALIGN_CENTER);
            onClick(row, [this, path = e.path] { post([this, path] { openProject(path); }); },
                    {kHud, kHud2, 1, 1, true});
        }
    };
    if (home_.recent.empty()) {
        Rid head = eyebrow(lists, "Your projects", 11);
        Rslot_set_padding(head, 0, 10, 0, 2);
        label(lists, "Projects you make show up here.", fonts_.regular, 12.5, kMute);
    }
    section("Your projects", home_.recent);
    section("Sample", home_.samples);
}

bool WorkbenchView::createProject(const std::string& idea) {
    if (!createHandler_) return false;
    const bool ok = createHandler_(idea);
    if (!ok) setText(find("home.note"), "Write a sentence about what you want to make first.");
    return ok;
}

void WorkbenchView::openProject(const std::string& path) {
    std::printf("open project: %s\n", path.c_str());
    std::fflush(stdout);
    if (openHandler_) openHandler_(path);
}

}  // namespace fulmetry::ui
