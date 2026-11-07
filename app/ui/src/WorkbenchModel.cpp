#include "fulmetry/ui/WorkbenchModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <nlohmann/json.hpp>

namespace fulmetry::ui {

namespace {

const char* kGroups[] = {"Printed", "Electronics", "Bought", "Fasteners"};

std::string tagFor(const std::string& group) {
    if (group == "Printed") return "print";
    if (group == "Electronics") return "board";
    if (group == "Fasteners") return "hw";
    return "buy";
}

constexpr const char* kRunning = "#1e6fe8";
constexpr const char* kDone = "#14a86b";

}  // namespace

std::string formatPrintTime(int minutes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d h %d m", minutes / 60, minutes % 60);
    return buf;
}

std::string formatCost(double usd) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "$%.2f", usd);
    return buf;
}

WorkbenchModel::WorkbenchModel(Project project) : project_(std::move(project)) {
    if (project_.objects.empty()) activeTab_ = 0;
}

std::vector<ObjectTab> WorkbenchModel::tabs() const {
    std::vector<ObjectTab> out;
    out.push_back({std::nullopt, "All objects", static_cast<int>(project_.objects.size())});
    for (const auto& o : project_.objects) out.push_back({o.id, o.name, static_cast<int>(o.parts.size())});
    return out;
}

void WorkbenchModel::selectTab(int index) {
    if (index < 0 || index > static_cast<int>(project_.objects.size()) || index == activeTab_) return;
    activeTab_ = index;
    // A selected part outside the new scope is no longer shown.
    if (selected_ && index > 0) {
        const auto& o = project_.objects[static_cast<size_t>(index - 1)];
        if (std::none_of(o.parts.begin(), o.parts.end(), [&](const Part& p) { return p.id == *selected_; }))
            selected_.reset();
    }
    touch();
}

std::optional<ObjectId> WorkbenchModel::scope() const {
    if (activeTab_ <= 0) return std::nullopt;
    return project_.objects[static_cast<size_t>(activeTab_ - 1)].id;
}

bool WorkbenchModel::selectPart(const PartId& part) {
    for (size_t i = 0; i < project_.objects.size(); ++i)
        for (const auto& p : project_.objects[i].parts)
            if (p.id == part) {
                selected_ = part;
                activeTab_ = static_cast<int>(i) + 1;
                touch();
                return true;
            }
    return false;
}

void WorkbenchModel::setExploded(bool on) {
    if (on == exploded_) return;
    exploded_ = on;
    touch();
}

void WorkbenchModel::setXray(bool on) {
    if (on == xray_) return;
    xray_ = on;
    touch();
}

void WorkbenchModel::clearSelection() {
    if (!selected_) return;
    selected_.reset();
    touch();
}

Totals WorkbenchModel::totals() const {
    Totals t;
    const auto sc = scope();
    double printedG = 0;
    for (const auto& o : project_.objects) {
        if (sc && o.id != *sc) continue;
        for (const auto& p : o.parts) {
            t.costUsd += p.costUsd;
            t.massG += p.massG;
            if (p.kind == PartKind::Printed) printedG += p.massG;
        }
    }
    t.printMinutes = static_cast<int>(std::lround(printedG / kPrintGramsPerHour * 60.0));
    for (const auto& c : project_.checks) {
        if (sc && c.object != *sc) continue;
        if (c.verdict == Verdict::Supported) ++t.supported;
        else if (c.verdict == Verdict::Refuted) ++t.refuted;
        else ++t.unresolved;
    }
    return t;
}

std::vector<PartGroup> WorkbenchModel::partGroups() const {
    std::vector<PartGroup> out;
    const auto sc = scope();
    const bool several = !sc && project_.objects.size() > 1;
    for (const auto& o : project_.objects) {
        if (sc && o.id != *sc) continue;
        PartGroup g;
        g.title = several ? o.name : o.name + " \xC2\xB7 parts";
        for (const char* group : kGroups)
            for (const auto& p : o.parts)
                if (p.group == group) g.rows.push_back({p.id, p.name, p.color, tagFor(p.group)});
        // Parts with a group the prototype does not list still show, last.
        for (const auto& p : o.parts)
            if (std::none_of(std::begin(kGroups), std::end(kGroups), [&](const char* k) { return p.group == k; }))
                g.rows.push_back({p.id, p.name, p.color, tagFor(p.group)});
        out.push_back(std::move(g));
    }
    return out;
}

int WorkbenchModel::partCount() const {
    int n = 0;
    for (const auto& g : partGroups()) n += static_cast<int>(g.rows.size());
    return n;
}

std::vector<const Check*> WorkbenchModel::checks(const ObjectId& object) const {
    std::vector<const Check*> out;
    for (const auto& c : project_.checks)
        if (c.object == object) out.push_back(&c);
    return out;
}

std::string WorkbenchModel::objectSubtitle(const ObjectId& object) const {
    const Object* o = project_.findObject(object);
    if (!o) return {};
    std::string s = std::to_string(o->parts.size()) + " parts";
    std::string links;
    for (const auto& l : project_.links) {
        const ObjectId* other = l.from == object ? &l.to : l.to == object ? &l.from : nullptr;
        if (!other) continue;
        const Object* oo = project_.findObject(*other);
        links += (links.empty() ? "linked to " : ", ") + (oo ? oo->name : other->value);
    }
    return s + " \xC2\xB7 " + (links.empty() ? std::string("standalone") : links);
}

std::string WorkbenchModel::aboutLabel() const {
    if (selected_)
        if (const Part* p = project_.findPart(*selected_)) return p->name;
    if (auto sc = scope())
        if (const Object* o = project_.findObject(*sc)) return o->name;
    return "Whole project";
}

Chip WorkbenchModel::chip(const PartId& part, const std::string& label) const {
    const Part* p = project_.findPart(part);
    return {part, !label.empty() ? label : p ? p->name : part.value, p ? p->color : "#c9ccd1"};
}

const Comment* WorkbenchModel::openComment() const {
    for (const auto& c : project_.comments)
        if (c.status != CommentStatus::Resolved) return &c;
    return project_.comments.empty() ? nullptr : &project_.comments.front();
}

Status WorkbenchModel::loadChat(const std::string& text) {
    nlohmann::json j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("entries") || !j["entries"].is_array())
        return Error{"parse_error", "sample chat: expected {\"entries\": [...]}"};
    std::vector<ChatEntry> out;
    auto chipsOf = [&](const nlohmann::json& arr) {
        std::vector<Chip> cs;
        if (!arr.is_array()) return cs;
        for (const auto& c : arr) {
            if (c.is_string()) cs.push_back(chip(PartId(c.get<std::string>())));
            else if (c.is_object()) cs.push_back(chip(PartId(c.value("part", "")), c.value("label", "")));
        }
        return cs;
    };
    for (const auto& e : j["entries"]) {
        ChatEntry c;
        if (e.value("kind", "message") == "divider") {
            c.kind = ChatEntry::Kind::Divider;
            c.text = e.value("text", "");
            out.push_back(std::move(c));
            continue;
        }
        c.author = e.value("author", "Claude");
        c.time = e.value("time", "");
        c.text = e.value("text", "");
        c.sentToAgent = e.value("sentToAgent", false);
        if (e.contains("chips")) c.chips = chipsOf(e["chips"]);
        if (e.contains("agent") && e["agent"].is_object()) {
            const auto& a = e["agent"];
            AgentCardView v;
            v.id = AgentId(a.value("id", ""));
            v.title = a.value("title", "");
            v.done = a.value("done", true);
            v.status = a.value("status", v.done ? "Done" : "Working");
            v.color = a.value("color", v.done ? kDone : kRunning);
            v.preview = a.value("preview", "");
            v.replies = a.value("replies", 0);
            if (a.contains("chips")) v.chips = chipsOf(a["chips"]);
            c.card = std::move(v);
        }
        out.push_back(std::move(c));
    }
    chat_ = std::move(out);
    touchChat();
    return Ok{};
}

Result<ChatRequest> WorkbenchModel::compose(const std::string& text, const std::string& time) {
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return Error{"invalid_argument", "empty message"};
    const HarnessChoice runOn = choice();
    if (runOn.harness.empty()) return Error{"no_harness", "connect a harness first"};
    ChatRequest req;
    req.text = text.substr(first);
    req.provider = runOn.harness;
    req.model = runOn.model;
    ChatEntry e;
    e.author = "You";
    e.time = time;
    e.text = req.text;
    e.sentToAgent = true;
    if (selected_) {
        req.scope = ChatScope::Part;
        req.part = selected_;
        if (const Part* p = project_.findPart(*selected_)) e.chips.push_back(chip(p->id));
    } else if (auto sc = scope()) {
        req.scope = ChatScope::Object;
        req.object = sc;
    }
    chat_.push_back(std::move(e));
    touchChat();
    return req;
}

AgentCardView WorkbenchModel::cardFrom(const AgentCard& card) const {
    AgentCardView v;
    v.id = card.id;
    v.title = card.name;
    v.done = card.state == AgentState::Done;
    v.status = v.done ? "Done" : card.step.empty() ? "Starting" : card.step;
    v.color = card.state == AgentState::Failed ? "#e5322d" : v.done ? kDone : kRunning;
    v.preview = card.preview;
    v.replies = card.replies;
    for (const auto& p : card.parts) v.chips.push_back(chip(p));
    if (!card.provider.empty()) v.harness = harnessLabel(card.provider, card.model);
    return v;
}

void WorkbenchModel::setHarnesses(std::vector<Harness> harnesses) {
    harnesses_ = std::move(harnesses);
    ++harnessVersion_;
    touch();
}

const Harness* WorkbenchModel::harness(const std::string& id) const {
    for (const auto& h : harnesses_)
        if (h.id == id) return &h;
    return nullptr;
}

std::vector<const Harness*> WorkbenchModel::readyHarnesses() const {
    std::vector<const Harness*> out;
    for (const auto& h : harnesses_)
        if (h.status == HarnessStatus::Ready) out.push_back(&h);
    return out;
}

HarnessChoice WorkbenchModel::choice() const {
    const Harness* h = harness(preferred_.harness);
    if (h && h->status == HarnessStatus::Ready) {
        for (const auto& m : h->models)
            if (m.id == preferred_.model) return preferred_;
        return {h->id, h->models.empty() ? std::string() : h->models.front().id};
    }
    auto ready = readyHarnesses();
    if (ready.empty()) return {};
    return {ready.front()->id, ready.front()->models.empty() ? std::string() : ready.front()->models.front().id};
}

bool WorkbenchModel::choose(const std::string& id, const std::string& model) {
    const Harness* h = harness(id);
    if (!h || h->status != HarnessStatus::Ready) return false;
    std::string m = model;
    if (m.empty()) m = h->models.empty() ? std::string() : h->models.front().id;
    else if (std::none_of(h->models.begin(), h->models.end(), [&](const HarnessModel& x) { return x.id == m; }))
        return false;
    setPreferredChoice({id, m});
    return true;
}

void WorkbenchModel::setPreferredChoice(HarnessChoice c) {
    if (c == preferred_) return;
    preferred_ = std::move(c);
    ++harnessVersion_;
    touch();
}

std::string WorkbenchModel::harnessLabel(const std::string& id, const std::string& model) const {
    const Harness* h = harness(id);
    std::string out = h ? h->name : id;
    if (h)
        for (const auto& m : h->models)
            if (m.id == model) return out + " \xC2\xB7 " + m.name;
    if (!model.empty()) out += " \xC2\xB7 " + model;
    return out;
}

std::string WorkbenchModel::choiceLabel() const {
    const HarnessChoice c = choice();
    return c.harness.empty() ? "No harness connected" : harnessLabel(c.harness, c.model);
}

void WorkbenchModel::apply(const Event& event, const std::string& time) {
    if (const auto* posted = std::get_if<ChatPosted>(&event)) {
        ChatEntry e;
        e.author = posted->message.author;
        e.time = time;
        e.text = posted->message.text;
        if (posted->message.agent) {
            AgentCard starting;
            starting.id = *posted->message.agent;
            starting.name = "Starting an agent";
            e.card = cardFrom(starting);
        }
        chat_.push_back(std::move(e));
        touchChat();
    } else if (const auto* upd = std::get_if<AgentUpdated>(&event)) {
        for (auto& e : chat_)
            if (e.card && e.card->id == upd->card.id) {
                e.card = cardFrom(upd->card);
                touchChat();
                return;
            }
    } else if (const auto* rev = std::get_if<RevisionSaved>(&event)) {
        project_.revision = rev->revision;
        ChatEntry d;
        d.kind = ChatEntry::Kind::Divider;
        d.text = "Revision r" + std::to_string(rev->revision.number) + " saved";
        chat_.push_back(std::move(d));
        touchChat();
    } else if (const auto* chk = std::get_if<CheckUpdated>(&event)) {
        for (auto& c : project_.checks)
            if (c.id == chk->check.id) c = chk->check;
        touch();
    } else if (const auto* hs = std::get_if<HarnessesUpdated>(&event)) {
        setHarnesses(hs->harnesses);
    } else if (const auto* cu = std::get_if<CommentUpdated>(&event)) {
        for (auto& c : project_.comments)
            if (c.id == cu->comment.id) c = cu->comment;
        touch();
    }
}

}  // namespace fulmetry::ui
