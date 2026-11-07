#include "WorkbenchView.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace fulmetry::ui {

using namespace kit;
using namespace kit::color;

namespace {

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Cuts at a word boundary near `max` bytes and adds an ellipsis (Raven labels have no line clamp).
std::string clamp(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t cut = s.rfind(' ', max);
    if (cut == std::string::npos || cut < max / 2) cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    std::string out = s.substr(0, cut);
    while (!out.empty() && (out.back() == ' ' || out.back() == ',')) out.pop_back();
    return out + "\xE2\x80\xA6";
}

const char* kindLabel(PartKind k) {
    switch (k) {
        case PartKind::Board: return "Circuit board";
        case PartKind::Printed: return "Printed part";
        case PartKind::Actuator: return "Actuator";
        case PartKind::Battery: return "Battery";
        case PartKind::Control: return "Control";
        case PartKind::Hardware: return "Hardware";
        case PartKind::Bought: return "Bought part";
    }
    return "Part";
}

uint32_t verdictColor(Verdict v) {
    return v == Verdict::Supported ? kPass : v == Verdict::Refuted ? kAccent : kUnres;
}
const char* verdictText(Verdict v) {
    return v == Verdict::Supported ? "Supported" : v == Verdict::Refuted ? "Refuted" : "Unresolved";
}

void setText(Rid label, const std::string& text) {
    if (label != R_INVALID_ID) Rlabel_set_text(label, text.c_str());
}

void fixedWidth(Rid w, float width) { Rwidget_set_size(w, width, -1); }

}  // namespace

WorkbenchView::WorkbenchView(WorkbenchModel& model, IAgentClient* agent, EventBus& bus, Options options)
    : model_(model), agent_(agent), bus_(bus), opt_(std::move(options)) {
    bus_.subscribe([this](const Event& e) { model_.apply(e, now()); });
}

std::string WorkbenchView::now() const {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

Rid WorkbenchView::find(const char* name) const { return Rwidget_find(window_, name); }

bool WorkbenchView::build() {
    if (!fonts_.load(opt_.assets)) return false;
    const std::string theme = opt_.assets + "/theme.xml";
    const char* themeName = Rtheme_load(theme.c_str());
    if (!themeName || !Rtheme_use(themeName)) {
        std::fprintf(stderr, "workbench: cannot load theme %s\n", theme.c_str());
        return false;
    }
    Rtheme_set_font(fonts_.regular);
    Rxml_set_base_path(opt_.assets.c_str());
    stage_.registerTag();
    const std::string xml = opt_.assets + "/workbench.xml";
    window_ = Rwindow_create_from_xml_file(xml.c_str());
    if (window_ == R_INVALID_ID || find("stage") == R_INVALID_ID || find("message.list") == R_INVALID_ID) {
        std::fprintf(stderr, "workbench: cannot build %s\n", xml.c_str());
        return false;
    }
    bindStatic();
    if (opt_.chatLeft) setChatLeft(true);
    sync();
    return true;
}

void WorkbenchView::bindStatic() {
    trackEyebrows(find("root"));
    // tracking for the brand: .08em
    setText(find("brand"), tracked("FULMETRY", 2));
    setText(find("chat.title"), model_.project().name + " project");
    setText(find("hint"),
            "**Drag** to orbit \xC2\xB7 **right-drag** to pan \xC2\xB7 **wheel** to zoom  \n"
            "**Click** a part to select it \xC2\xB7 **right-click** for its actions");
    Rid composer = find("composer");
    Rwidget_set_font(composer, fonts_.regular);
    Rwidget_set_metric(composer, R_METRIC_FONT_SIZE, 14);
    Rtext_input_set_committed_callback(
        composer, [](Rid, const char*, void* user) { static_cast<WorkbenchView*>(user)->post([v = static_cast<WorkbenchView*>(user)] { v->send(); }); },
        this);

    bindHarness();
    onClick(find("send"), [this] { post([this] { send(); }); },
            {kAccent, 0xD12A25, 1, 1, true});
    onClick(find("simulate"), [this] { post([] { std::printf("action: simulate\n"); std::fflush(stdout); }); },
            {kAccent, 0xD12A25, 1, 1, true});
    onClick(find("parts.button"), [this] { post([this] { setTrayOpen(!trayOpen_); }); });
    onClick(find("actions"), [this] { post([this] { setMenuOpen(!menuOpen_); }); });
    onClick(find("chat.side"), [this] { post([this] { setChatLeft(!chatLeft_); }); },
            {kHud2, 0xE9ECEF, 1, 1, true});
    Raction_bind(
        "wb.escape",
        [](Rid, const char*, void* user) {
            auto* v = static_cast<WorkbenchView*>(user);
            v->post([v] {
                if (v->pickerOpen_) v->setPickerOpen(false);
                else if (v->onboardOpen_) {
                    if (!v->model_.needsOnboarding()) v->setOnboardOpen(false);
                } else if (v->menuOpen_) v->setMenuOpen(false);
                else if (v->trayOpen_) v->setTrayOpen(false);
                else v->model_.clearSelection();
            });
        },
        this);
}

// ------------------------------------------------------------------ commands

void WorkbenchView::setChatLeft(bool left) {
    chatLeft_ = left;
    Rid root = find("root"), chat = find("chat"), ruleW = find("chat.rule"), stage = find("stage.area");
    Rwidget_remove_child(root, chat);
    Rwidget_remove_child(root, ruleW);
    Rwidget_remove_child(root, stage);
    if (left) {
        Rwidget_add_child(root, chat);
        Rwidget_add_child(root, ruleW);
        Rwidget_add_child(root, stage);
    } else {
        Rwidget_add_child(root, stage);
        Rwidget_add_child(root, ruleW);
        Rwidget_add_child(root, chat);
    }
    Rslot_set_fill(stage, 1);
    Rid side = find("chat.side");
    if (side != R_INVALID_ID && Rwidget_get_child_count(side) > 0)
        setText(Rwidget_get_child(side, 0), left ? "Dock right" : "Dock left");
    std::printf("chat docked %s\n", left ? "left" : "right");
    std::fflush(stdout);
}

void WorkbenchView::setTrayOpen(bool open) {
    trayOpen_ = open;
    Rwidget_set_visibility(find("tray"), open ? R_VISIBLE : R_COLLAPSED);
    Rwidget_set_visibility(find("parts.button"), open ? R_HIDDEN : R_VISIBLE);
    if (open) setMenuOpen(false);
}

void WorkbenchView::setMenuOpen(bool open) {
    menuOpen_ = open;
    if (open) buildMenu();
    Rwidget_set_visibility(find("menu"), open ? R_VISIBLE : R_COLLAPSED);
}

void WorkbenchView::send() {
    Rid composer = find("composer");
    const std::string text = Rtext_input_get_text(composer) ? Rtext_input_get_text(composer) : "";
    auto req = model_.compose(text, now());
    if (!req.ok()) {
        if (req.error().code == "no_harness") setOnboardOpen(true);  // the text stays in the field
        return;
    }
    std::printf("chat sent: \"%s\" on %s/%s\n", req->text.c_str(), req->provider.c_str(), req->model.c_str());
    std::fflush(stdout);
    Rtext_input_set_text(composer, "");
    if (agent_) {
        auto started = agent_->sendChat(req.value());
        if (!started.ok()) std::fprintf(stderr, "workbench: agent: %s\n", started.error().message.c_str());
    }
}

void WorkbenchView::selectPart(const PartId& part) {
    post([this, part] {
        if (model_.selectPart(part)) {
            inspectorOpen_ = true;
            std::printf("selection: %s\n", part.value.c_str());
            std::fflush(stdout);
        }
    });
}

// ------------------------------------------------------------------ frame

void WorkbenchView::update(double t) {
    auto q = std::move(queue_);
    queue_.clear();
    for (auto& fn : q) fn();
    if (agent_ && t - lastPoll_ >= opt_.agentPollSeconds) {
        lastPoll_ = t;
        agent_->poll(bus_);
    }
    bus_.drain();
    sync();
    placePin();
    // a freshly built chat lays out over a frame or two; keep the newest message in view meanwhile
    if (scrollFrames_ > 0) {
        --scrollFrames_;
        Rscroll_box_scroll_to_end(find("messages"));
    }
}

void WorkbenchView::sync() {
    if (model_.version() != shownVersion_) {
        shownVersion_ = model_.version();
        buildTabs();
        buildTotals();
        buildTray();
        buildInspector();
        buildPin();
        if (menuOpen_) buildMenu();
        setText(find("about.label"), model_.aboutLabel());
        std::string about = model_.aboutLabel();
        if (!about.empty()) about[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(about[0])));
        // The field is ~235 px wide: a part's name gets the shorter prompt so it is not cut.
        const std::string prompt = model_.selectedPart() ? "Ask about the " + about
                                   : model_.scope()      ? "Ask for a change to the " + about
                                                         : std::string("Ask for anything in this project");
        Rtext_input_set_placeholder(find("composer"), prompt.c_str());
        setText(find("parts.count"), std::to_string(model_.partCount()));
    }
    if (model_.chatVersion() != shownChat_) {
        shownChat_ = model_.chatVersion();
        buildChat();
    }
    if (model_.harnessVersion() != shownHarness_) {
        buildHarnessChip();
        if (pickerOpen_) buildPicker();
        // The first run opens the onboarding screen by itself, until the person closes it.
        if (!onboardOpen_ && onboardAuto_ && model_.needsOnboarding() && !model_.harnesses().empty()) setOnboardOpen(true);
        else if (onboardOpen_) buildOnboard();
        shownHarness_ = model_.harnessVersion();
    }
}

// ------------------------------------------------------------------ top bar

void WorkbenchView::buildTabs() {
    Rid tabs = find("tabs");
    Rwidget_clear_children(tabs);
    tabWidgets_.clear();
    auto all = model_.tabs();
    for (int i = 0; i < static_cast<int>(all.size()); ++i) {
        const bool on = i == model_.activeTab();
        Rid tab = border(tabs, on ? kHud2 : kHud, on ? 1.0f : 0.0f, 3, 0, 0);
        tabWidgets_.push_back(tab);
        Rborder_set_corner_radius(tab, 3, 3, 0, 0);
        Rid col = vbox(tab, 0);
        Rid row = hbox(col, 7);
        Rslot_set_padding(row, 12, 0, 12, 0);
        Rslot_set_fill(row, 1);
        Rid name = label(row, all[static_cast<size_t>(i)].name, fonts_.semibold, 13, on ? kInk : kMute);
        Rslot_set_valign(name, R_VALIGN_CENTER);
        Rid cnt = label(row, std::to_string(all[static_cast<size_t>(i)].count), fonts_.mono, 11, kMute);
        Rslot_set_valign(cnt, R_VALIGN_CENTER);
        Rid bar = border(col, kAccent, on ? 1.0f : 0.0f, 0, 0, 0);
        Rwidget_set_size(bar, -1, 2);
        onClick(tab, [this, i] { post([this, i] { model_.selectTab(i); inspectorOpen_ = true; std::printf("tab: %d\n", i); std::fflush(stdout); }); },
                {on ? kHud2 : kHud, kHud2, on ? 1.0f : 0.0f, 1.0f, true});
    }
}

void WorkbenchView::buildTotals() {
    const Totals t = model_.totals();
    setText(find("totals.cost"), formatCost(t.costUsd));
    char mass[32];
    std::snprintf(mass, sizeof mass, "%.0f g", t.massG);
    setText(find("totals.mass"), mass);
    setText(find("totals.print"), formatPrintTime(t.printMinutes));
    setText(find("totals.pass"), std::to_string(t.supported));
    setText(find("totals.fail"), std::to_string(t.refuted));
    setText(find("totals.open"), std::to_string(t.unresolved));
}

Rid WorkbenchView::eyebrow(Rid parent, const std::string& text, float size) {
    return label(parent, tracked(upper(text)), fonts_.tightSemibold, size, kMute);
}

Rid WorkbenchView::closeButton(Rid parent, std::function<void()> fn) {
    Rid b = border(parent, kHud, 0, 3, 4, 4);
    icon(b, opt_.assets, "close");
    onClick(b, std::move(fn), {kHud2, kHud2, 0, 1, true});
    Rslot_set_valign(b, R_VALIGN_TOP);
    return b;
}

// ------------------------------------------------------------------ parts tray

void WorkbenchView::buildTray() {
    Rid list = find("tray.list");
    Rwidget_clear_children(list);
    bool first = true;
    for (const auto& g : model_.partGroups()) {
        Rid group = vbox(list, 3);
        Rid head = hbox(group, 0);
        Rid title = eyebrow(head, g.title);
        Rslot_set_valign(title, R_VALIGN_CENTER);
        Rslot_set_fill(title, 1);
        if (first) closeButton(head, [this] { post([this] { setTrayOpen(false); }); });
        first = false;
        for (const auto& r : g.rows) {
            const bool sel = model_.selectedPart() && *model_.selectedPart() == r.id;
            Rid row = border(group, sel ? kHud2 : kHud, sel ? 1.0f : 0.0f, 3, 7, 5);
            if (sel) outline(row, kAccent);
            Rid h = hbox(row, 8);
            swatch(h, parseHex(r.color), 10, 2);
            Rid name = label(h, r.name, fonts_.regular, 13, kInk);
            Rslot_set_fill(name, 1);
            Rslot_set_valign(name, R_VALIGN_CENTER);
            Rid tag = label(h, r.tag, fonts_.mono, 11, kMute);
            Rslot_set_valign(tag, R_VALIGN_CENTER);
            onClick(row, [this, id = r.id] { selectPart(id); }, {sel ? kHud2 : kHud, kHud2, sel ? 1.0f : 0.0f, 1, true});
        }
    }
}

// ------------------------------------------------------------------ inspector

Rid WorkbenchView::pill(Rid parent, Verdict v) {
    const uint32_t c = verdictColor(v);
    Rid p = border(parent, c, 0.13f, 10, 9, 2);
    outline(p, c, 0.4f);
    Rid row = hbox(p, 6);
    Rid d = border(row, c, 1, 3.5f, 0, 0);
    Rwidget_set_size(d, 7, 7);
    Rslot_set_valign(d, R_VALIGN_CENTER);
    Rid t = label(row, verdictText(v), fonts_.semibold, 12, c);
    Rslot_set_valign(t, R_VALIGN_CENTER);
    Rslot_set_valign(p, R_VALIGN_CENTER);
    return p;
}

Rid WorkbenchView::checkRow(Rid parent, const Check& c) {
    Rid card = border(parent, kHud2, 1, 4, 10, 8);
    outline(card, kLine);
    Rid col = vbox(card, 3);
    Rid top = hbox(col, 8);
    Rid name = label(top, c.name, fonts_.regular, 13, kInk);
    Rslot_set_fill(name, 1);
    Rslot_set_valign(name, R_VALIGN_CENTER);
    pill(top, c.verdict);
    if (c.verdict != Verdict::Supported && !c.reason.empty()) wrapLabel(col, c.reason, fonts_.regular, 12, kMute);
    return card;
}

void WorkbenchView::buildInspector() {
    Rid panel = find("inspector");
    Rid body = find("inspector.body");
    Rwidget_clear_children(body);
    Rwidget_set_visibility(panel, inspectorOpen_ ? R_VISIBLE : R_COLLAPSED);
    const Project& p = model_.project();

    auto header = [&](const std::string& kicker, const std::string& title, const std::string& sub) {
        Rid head = hbox(body, 10);
        Rid col = vbox(head, 0);
        Rslot_set_fill(col, 1);
        eyebrow(col, kicker);
        Rid h2 = wrapLabel(col, upper(title), fonts_.tightBold, 23, kInk);
        Rslot_set_padding(h2, 0, 2, 0, 0);
        Rid s = label(col, sub, fonts_.regular, 12.5f, kMute);
        Rslot_set_padding(s, 0, 3, 0, 0);
        closeButton(head, [this] { post([this] { inspectorOpen_ = false; model_.clearSelection(); Rwidget_set_visibility(find("inspector"), R_COLLAPSED); }); });
    };

    if (const auto& sel = model_.selectedPart(); sel && p.findPart(*sel)) {
        const Part& part = *p.findPart(*sel);
        const Object* obj = nullptr;
        for (const auto& o : p.objects)
            for (const auto& pp : o.parts)
                if (pp.id == part.id) obj = &o;
        header((obj ? obj->name : std::string()) + " \xC2\xB7 " + kindLabel(part.kind), part.name,
               "Revision r" + std::to_string(p.revision.number));
        Rid kv = vbox(body, 0);
        auto row = [&](const std::string& k, const std::string& v) {
            rule(kv, false);
            Rid r = hbox(kv, 8);
            Rslot_set_padding(r, 0, 5, 0, 5);
            Rid kl = label(r, k, fonts_.regular, 12.5f, kMute);
            fixedWidth(kl, 132);
            Rid vl = label(r, v, fonts_.mono, 12, kInk);
            Rslot_set_fill(vl, 1);
        };
        char buf[32];
        row("Kind", kindLabel(part.kind));
        row("Group", part.group);
        if (!part.material.empty()) row("Material", part.material);
        std::snprintf(buf, sizeof buf, "%.0f g", part.massG);
        row("Mass", buf);
        row("Cost", formatCost(part.costUsd));
        if (part.joint) row("Joint", part.joint->name + " \xC2\xB7 " + part.joint->type);
        std::vector<const Check*> reads;
        for (const auto& c : p.checks)
            for (const auto& in : c.inputs)
                if (in == part.id) reads.push_back(&c);
        if (!reads.empty()) {
            eyebrow(body, "Checks that read it");
            Rid list = vbox(body, 6);
            for (const Check* c : reads) checkRow(list, *c);
        }
        return;
    }

    if (auto sc = model_.scope()) {
        const Object* o = p.findObject(*sc);
        if (!o) return;
        header("Object", o->name, model_.objectSubtitle(o->id));
        eyebrow(body, "Checks");
        Rid list = vbox(body, 6);
        Rslot_set_padding(list, 0, -6, 0, 0);
        for (const Check* c : model_.checks(o->id)) checkRow(list, *c);
        int joints = 0;
        for (const auto& part : o->parts) joints += part.joint ? 1 : 0;
        Rid foot = eyebrow(body, "Joints and fixings \xC2\xB7 " + std::to_string(joints) + " \xE2\x96\xB8");
        Rslot_set_padding(foot, 0, 4, 0, 0);
        return;
    }

    header("Project", p.name + " project",
           std::to_string(p.objects.size()) + " objects, each with its own parts and checks");
    Rid list = vbox(body, 6);
    for (const auto& o : p.objects) {
        Rid card = border(list, kHud2, 1, 4, 10, 8);
        outline(card, kLine);
        Rid col = vbox(card, 3);
        Rid top = hbox(col, 8);
        Rid n = label(top, o.name, fonts_.regular, 13, kInk);
        Rslot_set_fill(n, 1);
        label(top, std::to_string(o.parts.size()) + " parts", fonts_.semibold, 12, kMute);
        std::string open;
        for (const Check* c : model_.checks(o.id))
            if (c->verdict != Verdict::Supported) open += (open.empty() ? "" : ", ") + c->name;
        wrapLabel(col, open.empty() ? "All checks supported" : open, fonts_.regular, 12, kMute);
    }
}

// ------------------------------------------------------------------ action menu

void WorkbenchView::buildMenu() {
    Rid body = find("menu.body");
    Rwidget_clear_children(body);
    std::string scopeName = "Project";
    if (auto sc = model_.scope())
        if (const Object* o = model_.project().findObject(*sc)) scopeName = o->name;
    const Part* part = model_.selectedPart() ? model_.project().findPart(*model_.selectedPart()) : nullptr;

    Rid head = hbox(body, 8);
    Rslot_set_padding(head, 2, 0, 2, 0);
    Rid title = label(head, tracked(upper(part ? part->name : scopeName)), fonts_.tightBold, 17, kInk);
    Rslot_set_fill(title, 1);
    Rslot_set_valign(title, R_VALIGN_CENTER);
    Rid kicker = eyebrow(head, part ? kindLabel(part->kind) : "Actions", 11);
    Rslot_set_valign(kicker, R_VALIGN_CENTER);

    struct Item { const char* icon; const char* caption; const char* key; const char* action; bool on = false; };
    auto group = [&](const char* title2, std::initializer_list<Item> items) {
        Rid g = vbox(body, 5);
        Rid t = eyebrow(g, title2, 11);
        Rslot_set_padding(t, 2, 0, 2, 0);
        Rid grid = Rwidget_add_child(g, Rwidget_create(R_WIDGET_UNIFORM_GRID));
        Runiform_grid_set_columns(grid, 4);
        Rpanel_set_spacing(grid, 4);
        for (const auto& it : items) {
            const uint32_t bg = it.on ? 0xFCEBEA : kHud2;
            Rid tile = border(grid, bg, 1, 6, 2, 8);
            Rborder_set_padding(tile, 2, 8, 2, 6);
            outline(tile, it.on ? kAccent : kLine);
            Rwidget_set_min_size(tile, -1, 62);
            Rid col = vbox(tile, 4);
            Rid ic = icon(col, opt_.assets, it.icon);
            Rslot_set_halign(ic, R_HALIGN_CENTER);
            Rid cap = label(col, it.caption, fonts_.regular, 11, kInk);
            Rlabel_set_text_align(cap, R_TEXT_ALIGN_CENTER);
            Rslot_set_halign(cap, R_HALIGN_CENTER);
            if (it.key[0]) {
                Rid k = label(col, it.key, fonts_.mono, 9.5f, kMute);
                Rslot_set_halign(k, R_HALIGN_CENTER);
            }
            const std::string action = it.action;
            onClick(tile, [this, action] {
                post([this, action] {
                    std::printf("action: %s\n", action.c_str());
                    std::fflush(stdout);
                    setMenuOpen(false);
                    if (action == "all") model_.selectTab(0);
                    if (action == "explode") model_.setExploded(!model_.exploded());
                    if (action == "xray") model_.setXray(!model_.xray());
                    if (action == "open-chat") Rwidget_focus(find("composer"));
                    if (actionHandler_) actionHandler_(action);
                });
            }, {bg, it.on ? 0xF9DEDCu : 0xECEFF2u, 1, 1, true});
        }
    };
    const bool ex = model_.exploded(), xr = model_.xray();
    const char* explodeCaption = ex ? "Assemble" : "Explode";
    if (part) {
        group("This part", {{"pin", "Comment here", "C", "pin-here"}, {"chat", "Ask in chat", "", "ask"},
                            {"show", "Details", "", "details"}, {"explode", explodeCaption, "X", "explode", ex}});
    }
    group("View", {{"explode", explodeCaption, "X", "explode", ex}, {"xray", "X-ray", "", "xray", xr},
                   {"reset", "Reset view", "R", "reset-view"}, {"joint", "Joints", "J", "joints"},
                   {"show", "Show hidden", "", "show-all"}, {"cube", "All objects", "", "all", model_.activeTab() == 0}});
    group("Project", {{"plus", "New object", "N", "new"}, {"sim", "Simulate", "S", "sim"},
                      {"pin", "Pin comment", "C", "pin-mode"}, {"chat", "Open chat", "", "open-chat"}});
}

// ------------------------------------------------------------------ pinned comment

void WorkbenchView::buildPin() {
    Rid pinW = find("pin");
    Rid body = find("pin.body");
    Rwidget_clear_children(body);
    const Comment* c = model_.openComment();
    if (!c || (!pinLocator_ && !anchors_.count(c->anchor.part.value))) {
        Rwidget_set_visibility(pinW, R_HIDDEN);
        return;
    }
    const Part* part = model_.project().findPart(c->anchor.part);
    Rid head = hbox(body, 4);
    label(head, "You", fonts_.semibold, 11, kInk);
    Rid on = label(head, "on " + (part ? part->name : c->anchor.part.value), fonts_.regular, 11, kMute);
    Rslot_set_fill(on, 1);
    label(head, "\xE2\x80\x93", fonts_.regular, 11, kMute);
    std::string text = c->text;
    if (auto at = text.find("\xE2\x80\xA6 "); at != std::string::npos) text.replace(at, 4, "\xE2\x80\xA6 ");
    wrapLabel(body, text, fonts_.regular, 13, kInk);
    if (c->status != CommentStatus::Open) {
        const bool done = c->status == CommentStatus::Resolved;
        Rid s = border(body, done ? 0xE3F6EE : 0xE7F0FD, 1, 9, 7, 1);
        Rslot_set_halign(s, R_HALIGN_LEFT);
        label(s, done ? "Resolved" : "Agent working", fonts_.semibold, 11, done ? 0x0E6B45 : 0x1452B3);
        if (done && !c->reply.empty()) wrapLabel(body, c->reply, fonts_.regular, 12.5f, kBody);
    }
    Rid foot = hbox(body, 8);
    Rid open = border(foot, 0xF1F3F5, 1, 9, 8, 2);
    label(open, "Open", fonts_.semibold, 11, kInk);
    Rslot_set_valign(open, R_VALIGN_CENTER);
    onClick(open, [this, part = c->anchor.part] { selectPart(part); }, {0xF1F3F5, 0xE6E9EC, 1, 1, true});
    spacer(foot, 1);
    if (c->status == CommentStatus::Open) {
        Rid sendB = border(foot, kInk, 1, 3, 9, 3);
        label(sendB, "Send", fonts_.semibold, 12, kHud);
        onClick(sendB, [this, comment = *c] {
            post([this, comment] {
                if (!agent_) return;
                const HarnessChoice runOn = model_.choice();
                if (runOn.harness.empty()) {
                    setOnboardOpen(true);
                    return;
                }
                StartAgentOptions opts;
                opts.provider = runOn.harness;
                opts.model = runOn.model;
                auto id = agent_->startAgentForComment(comment, std::nullopt, opts);
                std::printf("comment %s sent to %s\n", comment.id.value.c_str(), id.ok() ? id->agent.value.c_str() : "nobody");
                std::fflush(stdout);
            });
        }, {kInk, 0x2C3137, 1, 1, true});
    }
    Rwidget_set_visibility(pinW, R_VISIBLE);
}

void WorkbenchView::setPinLocator(PinLocator locator, std::function<std::optional<Rect>()> avoid) {
    pinLocator_ = std::move(locator);
    pinAvoid_ = std::move(avoid);
    Rwidget_set_visibility(find("pin.stem"), pinLocator_ ? R_COLLAPSED : R_VISIBLE);
    shownVersion_ = 0;
}

WorkbenchView::Rect WorkbenchView::freeStageRect() const {
    float sx, sy, sw, sh;
    Rwidget_get_rect(find("stage.area"), &sx, &sy, &sw, &sh);
    Rect r{24, 24, sw - 24, sh - 24};
    auto box = [&](const char* name, float& x0, float& y0, float& x1, float& y1) {
        Rid w = find(name);
        if (w == R_INVALID_ID || Rwidget_get_visibility(w) != R_VISIBLE) return false;
        float x, y, ww, hh;
        Rwidget_get_rect(w, &x, &y, &ww, &hh);
        if (ww <= 0 || hh <= 0) return false;
        x0 = x - sx, y0 = y - sy, x1 = x0 + ww, y1 = y0 + hh;
        return true;
    };
    float x0, y0, x1, y1;
    for (const char* top : {"topbar", "totals", "parts.button"})
        if (box(top, x0, y0, x1, y1)) r.y0 = std::max(r.y0, y1 + 20);
    for (const char* bottom : {"actions", "hint"})
        if (box(bottom, x0, y0, x1, y1)) r.y1 = std::min(r.y1, y0 - 12);
    if (box("inspector", x0, y0, x1, y1)) r.x1 = std::min(r.x1, x0 - 20);
    return r;
}

void WorkbenchView::placePin() {
    Rid pinW = find("pin");
    const Comment* c = model_.openComment();
    if (pinW == R_INVALID_ID || !c) {
        stage_.setLeader(std::nullopt);
        return;
    }
    if (pinLocator_) {
        auto at = pinLocator_(*c);
        if (!at) {
            Rwidget_set_visibility(pinW, R_HIDDEN);
            stage_.setLeader(std::nullopt);
            return;
        }
        Rwidget_set_visibility(pinW, R_VISIBLE);
        const float ax = at->first, ay = at->second;
        float dw, dh, sx, sy, sw, sh;
        Rwidget_get_desired_size(pinW, &dw, &dh);
        Rwidget_get_rect(find("stage.area"), &sx, &sy, &sw, &sh);
        // Up and to the right of the anchor, clear of the subject's screen box: beside it or above it, whichever
        // keeps the leader shorter (beside for one object, above when All objects spreads them across the stage).
        float x = ax + 40, y = ay - dh - 46;
        if (auto avoid = pinAvoid_ ? pinAvoid_() : std::nullopt) {
            float best = 1e9f;
            auto consider = [&](float cx, float cy) {
                if (cx < 16 || cx + dw > sw - 16 || cy < 70) return;
                const float len = std::hypot(cx + 18 - ax, cy + dh - ay);
                if (len < best) best = len, x = cx, y = cy;
            };
            consider(std::max(ax + 40, avoid->x1 + 24), ay - dh - 46);
            consider(ax + 40, std::min(ay - dh - 46, avoid->y0 - 24 - dh));
        }
        x = std::clamp(x, 16.0f, std::max(16.0f, sw - dw - 16));
        y = std::clamp(y, 70.0f, std::max(70.0f, sh - dh - 16));
        // Never under a panel: lift the card above it, or failing that move it to the panel's left.
        for (const char* name : {"inspector", "menu", "tray", "totals"}) {
            Rid w = find(name);
            if (w == R_INVALID_ID || Rwidget_get_visibility(w) != R_VISIBLE) continue;
            float px, py, pw, ph;
            Rwidget_get_rect(w, &px, &py, &pw, &ph);
            px -= sx, py -= sy;
            if (x > px + pw + 8 || x + dw < px - 8 || y > py + ph + 8 || y + dh < py - 8) continue;
            if (py - dh - 16 >= 70) y = py - dh - 16;
            else if (py + ph + 16 + dh <= sh - 16) y = py + ph + 16;
            else x = std::max(16.0f, px - dw - 16);
        }
        Rslot_set_padding(pinW, x, y, 0, 0);
        // The leader meets the card's lower edge a little in from the accent bar.
        stage_.setLeader(StageImage::Leader{ax, ay, x + 18, y + dh});
        return;
    }
    auto it = anchors_.find(c->anchor.part.value);
    if (it == anchors_.end()) return;
    float ax, ay;
    stage_.frameToWidget(it->second.first, it->second.second, ax, ay);
    float dw, dh;
    Rwidget_get_desired_size(pinW, &dw, &dh);
    Rslot_set_padding(pinW, ax - 128.0f, ay - dh, 0, 0);
}

// ------------------------------------------------------------------ chat

Rid WorkbenchView::chip(Rid parent, const Chip& c) {
    Rid b = border(parent, kHud2, 1, 10, 7, 2);
    Rborder_set_padding(b, 6, 2, 8, 2);
    outline(b, kLine);
    Rid row = hbox(b, 5);
    swatch(row, parseHex(c.color), 10, 2);
    Rid t = label(row, c.label, fonts_.regular, 11.5f, kInk);
    Rslot_set_valign(t, R_VALIGN_CENTER);
    onClick(b, [this, id = c.part] { selectPart(id); }, {kHud2, 0xE9ECEF, 1, 1, true});
    chatChips_.emplace(c.part.value, b);
    return b;
}

void WorkbenchView::agentCard(Rid parent, const AgentCardView& card) {
    Rid box = border(parent, kHud, 1, 8, 12, 10);
    outline(box, kLine);
    Rid col = vbox(box, 6);
    Rid top = hbox(col, 8);
    Rid dot = border(top, parseHex(card.color, kRun), 1, 4.5f, 0, 0);
    Rwidget_set_size(dot, 9, 9);
    Rslot_set_valign(dot, R_VALIGN_CENTER);
    Rid title = label(top, card.title, fonts_.semibold, 13, kInk);
    Rslot_set_fill(title, 1);
    Rslot_set_valign(title, R_VALIGN_CENTER);
    Rid st = label(top, clamp(card.status, 22), fonts_.regular, 11.5f, card.done ? kMute : kRun);
    Rslot_set_valign(st, R_VALIGN_CENTER);
    // two lines, as the prototype clamps it: card width 352 - 2 x 14 - 38 (avatar) - 2 x 13 (card) - the scroll bar, with a margin
    if (!card.preview.empty())
        wrapLabel(col, clampLines(card.preview, fonts_.regular, 12.5f, 246, 2), fonts_.regular, 12.5f, kMute);
    if (!card.harness.empty()) label(col, card.harness, fonts_.regular, 11, kMute);
    if (!card.chips.empty()) {
        Rid chips = Rwidget_add_child(col, Rwidget_create(R_WIDGET_WRAP_BOX));
        Rpanel_set_spacing(chips, 5);
        size_t shown = 0;
        for (const auto& c : card.chips) {
            if (shown++ == 3) break;
            chip(chips, c);
        }
        if (card.chips.size() > 3) label(chips, "+" + std::to_string(card.chips.size() - 3), fonts_.regular, 11, kMute);
    }
    Rid foot = hbox(col, 4);
    icon(foot, opt_.assets, "reply");
    Rid n = label(foot, std::to_string(card.replies) + (card.replies == 1 ? " reply" : " replies"), fonts_.regular, 12, kRun);
    Rslot_set_valign(n, R_VALIGN_CENTER);
    spacer(foot, 1);
    icon(foot, opt_.assets, "chevron");
}

void WorkbenchView::chatEntry(Rid list, const ChatEntry& e) {
    if (e.kind == ChatEntry::Kind::Divider) {
        Rid row = hbox(list, 8);
        Rslot_set_padding(row, 0, 2, 0, 2);
        Rid l = rule(row, false);
        Rslot_set_fill(l, 1);
        Rslot_set_valign(l, R_VALIGN_CENTER);
        label(row, e.text, fonts_.regular, 11.5f, kMute);
        Rid r = rule(row, false);
        Rslot_set_fill(r, 1);
        Rslot_set_valign(r, R_VALIGN_CENTER);
        return;
    }
    const bool you = e.author == "You";
    Rid row = hbox(list, 10);
    Rid av = border(row, you ? 0xF2B705 : 0xD97757, 1, 14, 0, 0);
    Rwidget_set_size(av, 28, 28);
    Rslot_set_valign(av, R_VALIGN_TOP);
    Rid letter = label(av, you ? "Y" : "C", fonts_.tightBold, 14, you ? kInk : kHud);
    Rslot_set_halign(letter, R_HALIGN_CENTER);
    Rslot_set_valign(letter, R_VALIGN_CENTER);

    Rid col = vbox(row, 3);
    Rslot_set_fill(col, 1);
    Rid who = hbox(col, 8);
    Rid name = label(who, e.author, fonts_.semibold, 12.5f, kInk);
    Rslot_set_valign(name, R_VALIGN_CENTER);
    Rid time = label(who, e.time, fonts_.mono, 10.5f, kMute);
    Rslot_set_valign(time, R_VALIGN_CENTER);
    Rslot_set_padding(time, 0, 1, 0, 0);
    Rid text = wrapLabel(col, e.text, fonts_.regular, 13.5f, kBody);
    Rlabel_set_selectable(text, R_TRUE);
    Rslot_set_padding(text, 0, 4, 0, 0);
    if (!e.chips.empty()) {
        Rid chips = Rwidget_add_child(col, Rwidget_create(R_WIDGET_WRAP_BOX));
        Rpanel_set_spacing(chips, 5);
        Rslot_set_padding(chips, 0, 3, 0, 0);
        for (const auto& c : e.chips) chip(chips, c);
    }
    if (e.sentToAgent) {
        Rid link = label(col, "Sent to an agent \xE2\x80\xBA", fonts_.regular, 12, kRun);
        Rslot_set_padding(link, 0, 1, 0, 0);
    }
    if (e.card) {
        Rid holder = vbox(col, 0);
        Rslot_set_padding(holder, 0, 6, 0, 0);
        agentCard(holder, *e.card);
    }
}

void WorkbenchView::buildChat() {
    Rid list = find("message.list");
    Rwidget_clear_children(list);
    chatChips_.clear();
    for (const auto& e : model_.chat()) chatEntry(list, e);
    scrollFrames_ = 3;
}

void WorkbenchView::reportLayout() const {
    auto centre = [](Rid w, float& cx, float& cy) {
        float x, y, ww, hh;
        Rwidget_get_rect(w, &x, &y, &ww, &hh);
        cx = x + ww * 0.5f;
        cy = y + hh * 0.5f;
    };
    float x, y;
    for (size_t i = 0; i < tabWidgets_.size(); ++i) {
        centre(tabWidgets_[i], x, y);
        std::printf("tab-at %zu %.0f %.0f\n", i, static_cast<double>(x), static_cast<double>(y));
    }
    for (const auto& [part, w] : chatChips_) {
        centre(w, x, y);
        std::printf("chip-at %s %.0f %.0f\n", part.c_str(), static_cast<double>(x), static_cast<double>(y));
    }
    centre(find("send"), x, y);
    std::printf("send-at %.0f %.0f\n", static_cast<double>(x), static_cast<double>(y));
    centre(find("composer"), x, y);
    std::printf("composer-at %.0f %.0f\n", static_cast<double>(x), static_cast<double>(y));
    centre(find("harness.chip"), x, y);
    std::printf("harness-chip-at %.0f %.0f\n", static_cast<double>(x), static_cast<double>(y));
    std::fflush(stdout);
}

}  // namespace fulmetry::ui
