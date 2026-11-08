#include "fulmetry/net/T3AgentClient.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <thread>

#include "Subprocess.h"

namespace fulmetry::net {

using json = nlohmann::json;
using namespace std::chrono_literals;

namespace {

constexpr auto kStartTimeout = 120s;  // the bridge may start a T3 Code server first
constexpr auto kCallTimeout = 60s;

std::string freshId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::mutex mu;
    std::lock_guard lock(mu);
    char buf[33];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(rng()),
                  static_cast<unsigned long long>(rng()));
    return buf;
}

const char* scopeName(ChatScope s) {
    switch (s) {
        case ChatScope::Project: return "project";
        case ChatScope::Object: return "object";
        case ChatScope::Part: return "part";
        case ChatScope::Simulation: return "simulation";
        case ChatScope::NewObject: return "newObject";
    }
    return "project";
}

const char* decisionName(ApprovalDecision d) {
    switch (d) {
        case ApprovalDecision::Accept: return "accept";
        case ApprovalDecision::AcceptForSession: return "acceptForSession";
        case ApprovalDecision::Decline: return "decline";
        case ApprovalDecision::Cancel: return "cancel";
    }
    return "decline";
}

std::string s(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

AgentCard toCard(const json& e) {
    AgentCard c;
    c.id = AgentId(s(e, "agentId"));
    c.name = s(e, "name");
    const auto state = s(e, "state");
    c.state = state == "starting" ? AgentState::Starting
              : state == "done"   ? AgentState::Done
              : state == "failed" || state == "stopped" ? AgentState::Failed
                                                        : AgentState::Working;
    c.stopped = state == "stopped";
    c.step = s(e, "step");
    c.preview = s(e, "preview");
    if (auto it = e.find("parts"); it != e.end() && it->is_array())
        for (const auto& p : *it) if (p.is_string()) c.parts.emplace_back(p.get<std::string>());
    c.replies = e.value("replies", 0);
    if (auto id = s(e, "commentId"); !id.empty()) c.comment = CommentId(id);
    if (auto it = e.find("pendingApproval"); it != e.end() && it->is_object())
        c.pendingApproval = ApprovalRequest{s(*it, "requestId"), s(*it, "title")};
    c.provider = s(e, "provider");
    c.model = s(e, "model");
    c.branch = s(e, "branch");
    c.worktreePath = s(e, "worktreePath");
    if (auto it = e.find("revision"); it != e.end() && it->is_number_integer()) c.revision = it->get<int>();
    return c;
}

HarnessStatus statusOf(const std::string& v) {
    if (v == "ready") return HarnessStatus::Ready;
    if (v == "needsSignIn") return HarnessStatus::NeedsSignIn;
    if (v == "checking") return HarnessStatus::Checking;
    if (v == "disabled") return HarnessStatus::Disabled;
    if (v == "unavailable") return HarnessStatus::Unavailable;
    return HarnessStatus::NeedsInstall;
}

Harness toHarness(const json& j) {
    Harness h;
    h.id = s(j, "id");
    h.driver = s(j, "driver");
    h.name = s(j, "name");
    h.maker = s(j, "maker");
    h.accent = s(j, "accent");
    h.status = statusOf(s(j, "status"));
    h.version = s(j, "version");
    h.account = s(j, "account");
    h.canInstall = j.value("canInstall", false);
    h.canSignIn = j.value("canSignIn", false);
    h.fromRegistry = j.value("fromRegistry", false);
    h.docsUrl = s(j, "docsUrl");
    h.message = s(j, "message");
    if (auto it = j.find("models"); it != j.end() && it->is_array())
        for (const auto& m : *it) h.models.push_back({s(m, "id"), s(m, "name")});
    if (auto it = j.find("setup"); it != j.end() && it->is_object()) {
        const auto& u = *it;
        using St = HarnessSetup::Step;
        using Pr = HarnessSetup::Prompt;
        const auto step = s(u, "step"), prompt = s(u, "prompt");
        h.setup.step = step == "installing"  ? St::Installing
                       : step == "signingIn" ? St::SigningIn
                       : step == "verifying" ? St::Verifying
                       : step == "succeeded" ? St::Succeeded
                       : step == "failed"    ? St::Failed
                       : step == "cancelled" ? St::Cancelled
                                             : St::Idle;
        h.setup.prompt = prompt == "browser"       ? Pr::Browser
                         : prompt == "deviceCode"  ? Pr::DeviceCode
                         : prompt == "credentials" ? Pr::Credentials
                         : prompt == "terminal"    ? Pr::Terminal
                                                   : Pr::None;
        h.setup.fraction = u.value("fraction", -1.0);
        h.setup.url = s(u, "url");
        h.setup.userCode = s(u, "userCode");
        h.setup.output = s(u, "output");
        h.setup.message = s(u, "message");
        if (auto f = u.find("fields"); f != u.end() && f->is_array())
            for (const auto& x : *f) h.setup.fields.push_back({s(x, "name"), s(x, "label"), x.value("secret", false)});
    }
    return h;
}

}  // namespace

AgentCard wire::agentCard(const std::string& eventJson) { return toCard(json::parse(eventJson)); }
Harness wire::harness(const std::string& harnessJson) { return toHarness(json::parse(harnessJson)); }

struct T3AgentClient::Impl {
    Project project;
    std::string root;
    std::vector<std::string> command;
    std::unique_ptr<Subprocess> child;
    std::thread reader;
    std::atomic<bool> connected{false};

    std::mutex mu;
    std::condition_variable cv;
    bool ready = false;
    bool exited = false;
    long long nextId = 0;
    std::map<long long, json> replies;
    std::deque<json> events;
    std::vector<Harness> harnesses;
    bool haveHarnesses = false;
    std::map<std::string, Comment> comments;  // the comments agents work on, to merge commentUpdated into

    void readLoop() {
        std::string line;
        while (child->readLine(line)) {
            if (line.empty() || line[0] != '{') continue;  // stray output (a warning) is not a frame
            json frame = json::parse(line, nullptr, false);
            if (frame.is_discarded() || !frame.is_object()) continue;
            std::lock_guard lock(mu);
            if (auto e = frame.find("event"); e != frame.end()) {
                const auto type = s(*e, "type");
                if (type == "fulmetry.ready") ready = true;
                else if (type == "fulmetry.harnessesUpdated") {
                    harnesses.clear();
                    for (const auto& h : (*e)["harnesses"]) harnesses.push_back(toHarness(h));
                    haveHarnesses = true;
                    events.push_back(*e);
                } else events.push_back(*e);
            } else if (auto id = frame.find("id"); id != frame.end() && id->is_number_integer()) {
                const long long key = id->get<long long>();
                replies[key] = std::move(frame);
            }
            cv.notify_all();
        }
        std::lock_guard lock(mu);
        exited = true;
        connected = false;
        events.push_back({{"type", "fulmetry.banner"}, {"text", "Lost the connection to T3 Code; agents are paused."}});
        cv.notify_all();
    }

    Result<json> call(const std::string& method, json params, std::chrono::seconds timeout = kCallTimeout) {
        if (!child) return Error{"not_connected", "connect first"};
        long long id;
        {
            std::lock_guard lock(mu);
            if (exited) return Error{"not_connected", "T3 Code's bridge has stopped"};
            id = ++nextId;
        }
        if (!child->writeLine(json{{"id", id}, {"method", method}, {"params", std::move(params)}}.dump()))
            return Error{"not_connected", "could not write to T3 Code's bridge"};
        std::unique_lock lock(mu);
        if (!cv.wait_for(lock, timeout, [&] { return replies.count(id) || exited; }))
            return Error{"timeout", method + " got no answer"};
        if (!replies.count(id)) return Error{"not_connected", "T3 Code's bridge has stopped"};
        json reply = std::move(replies[id]);
        replies.erase(id);
        if (auto e = reply.find("error"); e != reply.end())
            return Error{e->value("code", std::string("failed")), e->value("message", std::string())};
        return reply.value("result", json::object());
    }

    Status callStatus(const std::string& method, json params) {
        auto r = call(method, std::move(params));
        if (!r.ok()) return r.error();
        return Ok{};
    }
};

T3AgentClient::T3AgentClient(Project project, std::string projectRoot, std::vector<std::string> command)
    : impl_(std::make_unique<Impl>()) {
    impl_->project = std::move(project);
    impl_->root = std::move(projectRoot);
    impl_->command = std::move(command);
    for (const auto& c : impl_->project.comments) impl_->comments[c.id.value] = c;
}

T3AgentClient::~T3AgentClient() {
    if (impl_->child) impl_->child->closeInput();  // the bridge exits at end of input, stopping what it started
    if (impl_->reader.joinable()) impl_->reader.join();
}

Status T3AgentClient::connect(const std::string&) {
    if (impl_->connected) return Ok{};
    if (impl_->command.empty()) return Error{"not_found", "no T3 Code bridge command"};
    auto child = Subprocess::start(impl_->command);
    if (!child.ok()) return child.error();
    impl_->child = std::move(child.value());
    impl_->reader = std::thread([this] { impl_->readLoop(); });
    {
        std::unique_lock lock(impl_->mu);
        if (!impl_->cv.wait_for(lock, kStartTimeout, [&] { return impl_->ready || impl_->exited; }) || !impl_->ready)
            return Error{"not_connected", "T3 Code's bridge did not start"};
    }
    auto sub = impl_->call("fulmetry.subscribe", {{"fulmetryExt", kFulmetryExtVersion},
                                                  {"projectRoot", impl_->root},
                                                  {"projectTitle", impl_->project.name}}, kStartTimeout);
    if (!sub.ok()) return sub.error();
    {
        // The onboarding screen decides on the first frame, so wait briefly for the harness list.
        std::unique_lock lock(impl_->mu);
        impl_->cv.wait_for(lock, 10s, [&] { return impl_->haveHarnesses || impl_->exited; });
    }
    impl_->connected = true;
    return Ok{};
}

bool T3AgentClient::connected() const { return impl_->connected; }

Result<AgentStarted> T3AgentClient::startAgentForComment(const Comment& c, const std::optional<Image>& render,
                                                         const StartAgentOptions& options) {
    if (render) {
        if (render->width <= 0 || render->height <= 0 || render->width > kMaxRenderSide ||
            render->height > kMaxRenderSide || render->rgba.size() != size_t(render->width) * render->height * 4)
            return Error{"invalid_argument", "render must be 1.." + std::to_string(kMaxRenderSide) +
                                                 " px a side with width * height * 4 bytes"};
    }
    json params = {{"commandId", options.commandId.empty() ? freshId() : options.commandId},
                   {"commentId", c.id.value},
                   {"anchor", {{"part", c.anchor.part.value}}},
                   {"text", c.text},
                   {"projectRoot", impl_->root}};
    const auto base = options.baseRevision.empty() ? impl_->project.revision.id : options.baseRevision;
    if (!base.empty()) params["baseRevision"] = base.value;
    if (options.object) params["objectId"] = options.object->value;
    if (!options.provider.empty()) params["provider"] = options.provider;
    if (!options.model.empty()) params["model"] = options.model;
    {
        std::lock_guard lock(impl_->mu);
        impl_->comments[c.id.value] = c;
    }
    auto r = impl_->call("fulmetry.startAgentForComment", std::move(params));
    if (!r.ok()) return r.error();
    return AgentStarted{AgentId(s(r.value(), "agentId")), s(r.value(), "threadId")};
}

Result<AgentId> T3AgentClient::sendChat(const ChatRequest& req) {
    json params = {{"commandId", req.commandId.empty() ? freshId() : req.commandId},
                   {"text", req.text},
                   {"scope", scopeName(req.scope)}};
    if (req.object) params["objectId"] = req.object->value;
    if (req.part) params["partId"] = req.part->value;
    if (!req.provider.empty()) params["provider"] = req.provider;
    if (!req.model.empty()) params["model"] = req.model;
    auto r = impl_->call("fulmetry.sendChat", std::move(params));
    if (!r.ok()) return r.error();
    return AgentId(s(r.value(), "agentId"));
}

Status T3AgentClient::reply(const AgentId& agent, const std::string& text) {
    return impl_->callStatus("fulmetry.reply", {{"agentId", agent.value}, {"text", text}});
}

Status T3AgentClient::respondToApproval(const AgentId& agent, const std::string& requestId, ApprovalDecision d) {
    return impl_->callStatus("fulmetry.respondToApproval",
                             {{"agentId", agent.value}, {"requestId", requestId}, {"decision", decisionName(d)}});
}

Status T3AgentClient::stopAgent(const AgentId& agent) {
    return impl_->callStatus("fulmetry.stopAgent", {{"agentId", agent.value}});
}

std::vector<Harness> T3AgentClient::harnesses() const {
    std::lock_guard lock(impl_->mu);
    return impl_->harnesses;
}

Status T3AgentClient::installHarness(const std::string& h) { return impl_->callStatus("harness.install", {{"harness", h}}); }
Status T3AgentClient::signInHarness(const std::string& h) { return impl_->callStatus("harness.signIn", {{"harness", h}}); }
Status T3AgentClient::cancelHarnessSetup(const std::string& h) { return impl_->callStatus("harness.cancel", {{"harness", h}}); }
Status T3AgentClient::signOutHarness(const std::string& h) { return impl_->callStatus("harness.signOut", {{"harness", h}}); }
Status T3AgentClient::removeHarness(const std::string& h) { return impl_->callStatus("harness.remove", {{"harness", h}}); }

Status T3AgentClient::answerSignIn(const std::string& h, const SignInAnswer& a) {
    return impl_->callStatus("harness.answerSignIn",
                             {{"harness", h}, {"accept", a.accept}, {"values", a.values}, {"terminalInput", a.terminalInput}});
}

Status T3AgentClient::setHarnessEnabled(const std::string& h, bool enabled) {
    return impl_->callStatus("harness.setEnabled", {{"harness", h}, {"enabled", enabled}});
}

Result<std::vector<RegistryHarness>> T3AgentClient::searchHarnessRegistry(const std::string& query) {
    auto r = impl_->call("harness.searchRegistry", {{"query", query}});
    if (!r.ok()) return r.error();
    std::vector<RegistryHarness> out;
    for (const auto& x : r.value()["results"]) out.push_back({s(x, "id"), s(x, "name"), s(x, "version"), s(x, "description")});
    return out;
}

Result<std::string> T3AgentClient::addRegistryHarness(const std::string& registryId) {
    auto r = impl_->call("harness.addRegistry", {{"registryId", registryId}});
    if (!r.ok()) return r.error();
    return s(r.value(), "harness");
}

void T3AgentClient::poll(EventBus& bus) {
    std::deque<json> batch;
    {
        std::lock_guard lock(impl_->mu);
        batch.swap(impl_->events);
    }
    for (const auto& e : batch) {
        const auto type = s(e, "type");
        if (type == "fulmetry.agentUpdated") {
            bus.publish(AgentUpdated{toCard(e)});
        } else if (type == "fulmetry.chatPosted") {
            std::optional<AgentId> agent;
            if (auto id = s(e, "agentId"); !id.empty()) agent = AgentId(id);
            bus.publish(ChatPosted{{s(e, "author"), s(e, "text"), agent}});
        } else if (type == "fulmetry.commentUpdated") {
            // The wire carries no anchor or text; merge into the comment the agent works on.
            Comment c;
            {
                std::lock_guard lock(impl_->mu);
                auto it = impl_->comments.find(s(e, "commentId"));
                if (it == impl_->comments.end()) continue;
                const auto status = s(e, "status");
                it->second.status = status == "resolved" ? CommentStatus::Resolved
                                    : status == "working" ? CommentStatus::Working
                                                          : CommentStatus::Open;
                if (auto a = s(e, "agentId"); !a.empty()) it->second.agent = AgentId(a);
                if (auto r = s(e, "reply"); !r.empty()) it->second.reply = r;
                c = it->second;
            }
            bus.publish(CommentUpdated{c});
        } else if (type == "fulmetry.banner") {
            bus.publish(Banner{s(e, "text")});
        } else if (type == "fulmetry.harnessesUpdated") {
            std::vector<Harness> hs;
            for (const auto& h : e["harnesses"]) hs.push_back(toHarness(h));
            bus.publish(HarnessesUpdated{std::move(hs)});
        }
    }
}

}  // namespace fulmetry::net
