// Fake agent back end. Deterministic: each poll() advances every running agent by one step.
// Lifecycle per the UI spec: comment -> agent named after the task -> edit -> revision -> checks -> resolved.
// With askApproval, every agent stops before editing and waits (pendingApproval) until respondToApproval.
// Harnesses: a catalog shaped like T3 Code's built-in drivers. Installs take three polls; sign-in prompts follow
// the driver (browser for Claude Code, device code for Codex, an API key for Raven) and verify on the next poll.
#pragma once
#include <cctype>
#include <map>
#include <sstream>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::fakes {

class FakeAgentClient : public IAgentClient {
public:
    // Which harnesses start out ready: Fresh (a first run: nothing signed in), Some (Claude Code only), All.
    enum class Harnesses { Fresh, Some, All };

    explicit FakeAgentClient(Project project, bool askApproval = false, Harnesses harnesses = Harnesses::Some)
        : project_(std::move(project)), revision_(project_.revision.number), askApproval_(askApproval) {
        head_ = project_.revision.id.empty() ? "rev-" + std::to_string(revision_) : project_.revision.id.value;
        harnesses_ = catalog(harnesses);
    }

    Status connect(const std::string& url) override {
        if (url.rfind("ws://", 0) != 0 && url.rfind("fake://", 0) != 0) return Error{"bad_url", url};
        connected_ = true;
        return Ok{};
    }
    bool connected() const override { return connected_; }

    Result<AgentStarted> startAgentForComment(const Comment& c, const std::optional<Image>& render,
                                              const StartAgentOptions& options = {}) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        if (auto it = started_.find(options.commandId); !options.commandId.empty() && it != started_.end())
            return it->second;
        if (render) {
            if (render->width <= 0 || render->height <= 0 || render->width > kMaxRenderSide ||
                render->height > kMaxRenderSide || render->rgba.size() != size_t(render->width) * render->height * 4)
                return Error{"invalid_argument", "render must be 1.." + std::to_string(kMaxRenderSide) +
                                                     " px a side with width * height * 4 bytes"};
        }
        auto h = runnable(options.provider, options.model);
        if (!h.ok()) return h.error();
        Run& r = newRun(c.text, {c.anchor.part}, h->first, h->second);
        r.card.comment = c.id;
        r.card.branch = "fulmetry/" + slug(c.id.value);
        r.comment = c;
        AgentStarted s{r.card.id, r.card.id.value};
        if (!options.commandId.empty()) started_[options.commandId] = s;
        return s;
    }

    Result<AgentId> sendChat(const ChatRequest& req) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        if (auto it = chats_.find(req.commandId); !req.commandId.empty() && it != chats_.end()) return it->second;
        auto h = runnable(req.provider, req.model);
        if (!h.ok()) return h.error();
        Run& r = newRun(req.text, req.part ? std::vector<PartId>{*req.part} : std::vector<PartId>{}, h->first, h->second);
        r.chat = true;
        pending_.push_back(ChatPosted{{"Claude", "Started an agent for this.", r.card.id}});
        if (!req.commandId.empty()) chats_[req.commandId] = r.card.id;
        return r.card.id;
    }

    Status reply(const AgentId& id, const std::string&) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        r->stage = 0;
        r->approved = false;
        r->card.stopped = false;
        r->card.pendingApproval.reset();
        r->card.replies += 1;
        return Ok{};
    }

    Status respondToApproval(const AgentId& id, const std::string& requestId, ApprovalDecision decision) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        if (!r->card.pendingApproval || r->card.pendingApproval->requestId != requestId)
            return Error{"not_found", "no pending approval " + requestId};
        r->card.pendingApproval.reset();
        if (decision == ApprovalDecision::Cancel) return stopAgent(id);
        r->approved = true;  // a declined command is skipped and the agent carries on, as Raven does
        return Ok{};
    }

    Status stopAgent(const AgentId& id) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        if (r->stage <= kDone) r->stopping = true;  // stopping a finished agent changes nothing
        return Ok{};
    }

    void poll(EventBus& bus) override {
        for (auto& e : pending_) bus.publish(e);
        pending_.clear();
        for (auto& r : runs_) advance(r, bus);
        bool changed = harnessesChanged_;
        for (auto& h : harnesses_) changed |= advanceSetup(h);
        if (changed) bus.publish(HarnessesUpdated{harnesses_});
        harnessesChanged_ = false;
    }

    // ---- harnesses
    std::vector<Harness> harnesses() const override { return harnesses_; }

    Status installHarness(const std::string& id) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (!h->canInstall) return Error{"unsupported", h->name + " cannot be installed from here"};
        if (h->status != HarnessStatus::NeedsInstall) return Ok{};
        h->status = HarnessStatus::Checking;
        h->setup = {};
        h->setup.step = HarnessSetup::Step::Installing;
        h->setup.fraction = 0;
        h->setup.message = "Downloading";
        h->message.clear();
        harnessesChanged_ = true;
        return Ok{};
    }

    Status signInHarness(const std::string& id) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (!h->canSignIn) return Error{"unsupported", h->name + " signs in on its own"};
        if (h->status == HarnessStatus::NeedsInstall) return Error{"not_installed", h->name + " is not installed"};
        h->status = HarnessStatus::Checking;
        h->setup = {};
        h->setup.step = HarnessSetup::Step::SigningIn;
        if (h->driver == "codex") {
            h->setup.prompt = HarnessSetup::Prompt::DeviceCode;
            h->setup.url = "https://auth.openai.com/codex/device";
            h->setup.userCode = "FMTR-7Q4K";
            h->setup.message = "Enter the code on that page; this finishes by itself";
            deviceCodePolls_[id] = 0;
        } else if (h->driver == "raven" || h->fromRegistry) {
            h->setup.prompt = HarnessSetup::Prompt::Credentials;
            h->setup.fields = {{"ANTHROPIC_API_KEY", "Model provider API key", true}};
            h->setup.message = "Raven calls the model with your own key.";
        } else {
            h->setup.prompt = HarnessSetup::Prompt::Browser;
            h->setup.url = "https://claude.ai/oauth/authorize?client=t3code";
            h->setup.message = "Sign in in your browser, then come back";
        }
        harnessesChanged_ = true;
        return Ok{};
    }

    Status answerSignIn(const std::string& id, const SignInAnswer& answer) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (h->setup.step != HarnessSetup::Step::SigningIn) return Error{"not_found", "no sign-in in progress"};
        if (!answer.accept) return cancelHarnessSetup(id);
        if (h->setup.prompt == HarnessSetup::Prompt::Credentials)
            for (const auto& f : h->setup.fields) {
                auto it = answer.values.find(f.name);
                if (it == answer.values.end() || it->second.empty())
                    return Error{"invalid_argument", f.label + " is empty"};
            }
        h->setup.step = HarnessSetup::Step::Verifying;
        h->setup.prompt = HarnessSetup::Prompt::None;
        h->setup.message = "Checking the sign-in";
        harnessesChanged_ = true;
        return Ok{};
    }

    Status cancelHarnessSetup(const std::string& id) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (h->setup.step == HarnessSetup::Step::Idle) return Ok{};
        const bool installing = h->setup.step == HarnessSetup::Step::Installing;
        h->setup = {};
        h->setup.step = HarnessSetup::Step::Cancelled;
        h->status = installing ? HarnessStatus::NeedsInstall : HarnessStatus::NeedsSignIn;
        harnessesChanged_ = true;
        return Ok{};
    }

    Status signOutHarness(const std::string& id) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (!h->canSignIn) return Error{"unsupported", h->name + " signs in on its own"};
        h->account.clear();
        h->setup = {};
        if (h->status == HarnessStatus::Ready) h->status = HarnessStatus::NeedsSignIn;
        harnessesChanged_ = true;
        return Ok{};
    }

    Status setHarnessEnabled(const std::string& id, bool enabled) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (!enabled && h->status != HarnessStatus::Disabled) {
            disabledFrom_[id] = h->status;
            h->status = HarnessStatus::Disabled;
        } else if (enabled && h->status == HarnessStatus::Disabled) {
            h->status = disabledFrom_.count(id) ? disabledFrom_[id] : HarnessStatus::NeedsSignIn;
        }
        harnessesChanged_ = true;
        return Ok{};
    }

    Result<std::vector<RegistryHarness>> searchHarnessRegistry(const std::string& query) override {
        static const std::vector<RegistryHarness> all = {
            {"goose", "Goose", "1.31.0", "Block's open source agent; any model provider"},
            {"gemini", "Gemini CLI", "0.42.1", "Google's agent for the terminal"},
            {"qwen-code", "Qwen Code", "0.9.4", "Qwen's coding agent"},
            {"cline", "Cline", "4.2.0", "Plan and act agent"},
        };
        std::vector<RegistryHarness> out;
        auto lower = [](std::string s) {
            for (auto& ch : s) ch = char(std::tolower(static_cast<unsigned char>(ch)));
            return s;
        };
        for (const auto& r : all)
            if (query.empty() || lower(r.name).find(lower(query)) != std::string::npos) out.push_back(r);
        return out;
    }

    Result<std::string> addRegistryHarness(const std::string& registryId) override {
        auto found = searchHarnessRegistry("");
        for (const auto& r : found.value())
            if (r.id == registryId) {
                if (harness(registryId)) return registryId;
                Harness h;
                h.id = registryId;
                h.driver = "acpRegistry";
                h.name = r.name;
                h.maker = "ACP registry";
                h.version = r.version;
                h.fromRegistry = true;
                h.canSignIn = true;
                h.status = HarnessStatus::NeedsSignIn;
                h.models = {{"default", "Default model"}};
                harnesses_.push_back(h);
                harnessesChanged_ = true;
                return registryId;
            }
        return Error{"not_found", registryId};
    }

    Status removeHarness(const std::string& id) override {
        Harness* h = harness(id);
        if (!h) return Error{"not_found", id};
        if (!h->fromRegistry) return Error{"unsupported", h->name + " is built in; turn it off instead"};
        std::erase_if(harnesses_, [&](const Harness& x) { return x.id == id; });
        harnessesChanged_ = true;
        return Ok{};
    }

    // Names an agent after its task in a few words: "make the wheels bigger" -> "Make the wheels bigger".
    static std::string nameFor(const std::string& text) {
        std::string s = text;
        auto cut = s.find("…");
        if (cut != std::string::npos && cut + 3 < s.size()) s = s.substr(cut + 3);
        std::istringstream in(s);
        std::string w, out;
        for (int i = 0; i < 4 && in >> w; ++i) out += (out.empty() ? "" : " ") + w;
        while (!out.empty() && std::ispunct(static_cast<unsigned char>(out.back()))) out.pop_back();
        if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
        return out.empty() ? "Agent" : out;
    }

private:
    static constexpr int kEdit = 2, kSave = 3, kDone = 5;
    struct Run {
        AgentCard card;
        std::optional<Comment> comment;
        bool chat = false;
        bool approved = false;
        bool stopping = false;
        int stage = 0;  // 0 start, 1 read, 2 edit, 3 save, 4 checks, 5 done, 6 finished
    };

    static std::string slug(std::string s) {
        for (auto& ch : s) if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '-';
        return s;
    }
    Run* find(const AgentId& id) {
        for (auto& r : runs_) if (r.card.id == id) return &r;
        return nullptr;
    }
    Harness* harness(const std::string& id) {
        for (auto& h : harnesses_) if (h.id == id) return &h;
        return nullptr;
    }
    // The harness and model an agent runs on: the named one, or the first ready one. It must be ready.
    Result<std::pair<std::string, std::string>> runnable(const std::string& id, const std::string& model) {
        Harness* h = nullptr;
        if (id.empty()) {
            for (auto& x : harnesses_) if (x.status == HarnessStatus::Ready) { h = &x; break; }
            if (!h) return Error{"no_harness", "no harness is ready; connect one first"};
        } else {
            h = harness(id);
            if (!h) return Error{"not_found", "no harness " + id};
            if (h->status != HarnessStatus::Ready) return Error{"harness_not_ready", h->name + " is not ready"};
        }
        if (model.empty()) return std::pair{h->id, h->models.empty() ? std::string() : h->models.front().id};
        for (const auto& m : h->models) if (m.id == model) return std::pair{h->id, model};
        return Error{"not_found", h->name + " has no model " + model};
    }
    // One poll of an install or sign-in. True when something changed.
    bool advanceSetup(Harness& h) {
        using S = HarnessSetup::Step;
        switch (h.setup.step) {
            case S::Installing:
                h.setup.fraction += 0.34;
                h.setup.message = h.setup.fraction < 0.6 ? "Downloading" : "Verifying the download";
                if (h.setup.fraction >= 1) {
                    h.setup = {};
                    h.setup.step = S::Succeeded;
                    h.version = h.version.empty() ? "1.0.0" : h.version;
                    h.status = h.canSignIn ? HarnessStatus::NeedsSignIn : HarnessStatus::Ready;
                }
                return true;
            case S::SigningIn:
                if (h.setup.prompt == HarnessSetup::Prompt::DeviceCode && ++deviceCodePolls_[h.id] >= 4) {
                    h.setup.step = S::Verifying;
                    h.setup.prompt = HarnessSetup::Prompt::None;
                    h.setup.message = "Checking the sign-in";
                    return true;
                }
                return false;
            case S::Verifying:
                h.setup = {};
                h.setup.step = S::Succeeded;
                h.status = HarnessStatus::Ready;
                h.account = h.driver == "codex" ? "ChatGPT Pro" : h.driver == "raven" || h.fromRegistry ? "API key" : "Claude Max";
                return true;
            default: return false;
        }
    }
    static std::vector<Harness> catalog(Harnesses start) {
        auto make = [](std::string id, std::string name, std::string maker, std::string accent, HarnessStatus st,
                       bool install, bool signIn, std::vector<HarnessModel> models) {
            Harness h;
            h.id = id;
            h.driver = id;
            h.name = std::move(name);
            h.maker = std::move(maker);
            h.accent = std::move(accent);
            h.status = st;
            h.canInstall = install;
            h.canSignIn = signIn;
            h.models = std::move(models);
            if (st != HarnessStatus::NeedsInstall) h.version = "1.0.0";
            return h;
        };
        using HS = HarnessStatus;
        const bool all = start == Harnesses::All, fresh = start == Harnesses::Fresh;
        std::vector<Harness> c = {
            make("raven", "Raven", "EverMind", "#7A5AF8", all ? HS::Ready : HS::NeedsSignIn, true, true,
                 {{"claude-sonnet-5-5", "Sonnet 5.5"}, {"claude-opus-5-5", "Opus 5.5"}, {"claude-haiku-5-5", "Haiku 5.5"}}),
            make("claudeAgent", "Claude Code", "Anthropic", "#D97757", fresh ? HS::NeedsSignIn : HS::Ready, true, true,
                 {{"claude-opus-5-5", "Opus 5.5"}, {"claude-sonnet-5-5", "Sonnet 5.5"}, {"claude-haiku-5-5", "Haiku 5.5"},
                  {"claude-fable-5-1", "Fable 5.1"}}),
            make("codex", "Codex", "OpenAI", "#10A37F", all ? HS::Ready : HS::NeedsSignIn, true, true,
                 {{"gpt-6.1-sol", "GPT-6.1 Sol"}, {"gpt-6-astra", "GPT-6 Astra"}, {"gpt-6-luna", "GPT-6 Luna"}}),
            make("cursor", "Cursor Agent", "Anysphere", "#14171A", all ? HS::Ready : HS::NeedsInstall, true, true,
                 {{"auto", "Auto"}}),
            make("opencode", "OpenCode", "SST", "#F2A33A", all ? HS::Ready : HS::NeedsInstall, true, false,
                 {{"default", "Default model"}}),
            make("antigravity", "Antigravity", "Google", "#1E6FE8", all ? HS::Ready : HS::NeedsInstall, true, true,
                 {{"gemini-3.8-flash-high", "Gemini 3.8 Flash high"}, {"gemini-3.8-flash-medium", "Gemini 3.8 Flash"}}),
            make("grok", "Grok", "xAI", "#2A2F35", all ? HS::Ready : HS::NeedsInstall, true, true, {{"default", "Default model"}}),
            make("kiro", "Kiro", "AWS", "#9046FF", all ? HS::Ready : HS::NeedsInstall, true, true, {{"auto", "Auto"}}),
            make("pi", "Pi", "Mario Zechner", "#66707B", HS::Unavailable, false, false, {{"default", "Default model"}}),
        };
        for (auto& h : c) {
            if (h.status == HS::Ready) h.account = h.id == "codex" ? "ChatGPT Pro" : h.id == "raven" ? "API key" : "Claude Max";
            if (h.id == "pi") h.message = "Pi 0.31 is installed; T3 Code needs 0.40 or later";
        }
        return c;
    }
    Run& newRun(const std::string& text, std::vector<PartId> parts, const std::string& provider,
                const std::string& model) {
        Run r;
        r.card.id = AgentId("agent:" + std::to_string(++nextId_));
        r.card.name = nameFor(text);
        r.card.parts = std::move(parts);
        r.card.provider = provider;
        r.card.model = model;
        r.card.branch = "fulmetry/agent-" + std::to_string(nextId_);
        r.card.worktreePath = "/fake/worktrees/agent-" + std::to_string(nextId_);
        runs_.push_back(std::move(r));
        return runs_.back();
    }
    void publishComment(const Run& r, CommentStatus status, const std::string& reply, EventBus& bus) {
        if (!r.comment) return;
        Comment c = *r.comment;
        c.status = status;
        c.agent = r.card.id;
        c.reply = reply;
        bus.publish(CommentUpdated{c});
    }

    void advance(Run& r, EventBus& bus) {
        static const char* kSteps[] = {"Starting", "Reading the part", "Editing the model", "Saving the revision",
                                       "Re-running checks", "Done"};
        if (r.stage > kDone) return;
        if (r.stopping) {
            r.stopping = false;
            r.stage = kDone + 1;
            r.card.state = AgentState::Failed;
            r.card.stopped = true;
            r.card.step = "Stopped";
            r.card.pendingApproval.reset();
            publishComment(r, CommentStatus::Open, "", bus);
            bus.publish(AgentUpdated{r.card});
            return;
        }
        if (r.stage == kEdit && askApproval_ && !r.approved) {
            if (r.card.pendingApproval) return;  // still waiting; nothing new to say
            r.card.step = "Waiting for approval";
            r.card.pendingApproval = ApprovalRequest{"req:" + std::to_string(++nextRequest_), "Edit the CAD source?"};
            bus.publish(AgentUpdated{r.card});
            return;
        }
        r.card.step = kSteps[r.stage];
        r.card.state = r.stage == 0 ? AgentState::Starting : r.stage == kDone ? AgentState::Done : AgentState::Working;
        if (r.stage == 0) publishComment(r, CommentStatus::Working, "", bus);
        if (r.stage == kSave) {
            ++revision_;
            std::string sha = "rev-" + std::to_string(revision_);
            RevisionSaved saved{{RevisionId(sha), revision_, r.card.name}, r.card.parts, head_, r.card.branch,
                                r.card.id, r.card.comment};
            head_ = sha;
            r.card.revision = revision_;
            bus.publish(saved);
            for (auto& ch : project_.checks)
                for (const auto& in : ch.inputs)
                    for (const auto& p : r.card.parts)
                        if (in == p) {
                            Check u = ch;
                            u.verdict = Verdict::Unresolved;
                            u.reason = "Changed in r" + std::to_string(revision_) + "; re-testing";
                            bus.publish(CheckUpdated{u});
                        }
        }
        if (r.stage == kDone) {
            r.card.preview = "Done: " + r.card.name + " (r" + std::to_string(revision_) + ").";
            r.card.replies += 1;
            publishComment(r, CommentStatus::Resolved, r.card.preview, bus);
        }
        bus.publish(AgentUpdated{r.card});
        ++r.stage;
    }

    Project project_;
    int revision_ = 0;
    bool askApproval_ = false;
    std::string head_;
    bool connected_ = false;
    int nextId_ = 0;
    int nextRequest_ = 0;
    std::vector<Run> runs_;
    // ChatPosted, not Event: moving a temporary Event into a std::vector<Event> made GCC 13 at -O3 report a false
    // -Wmaybe-uninitialized on ViewModeChanged (Events.h) in Release builds.
    std::vector<ChatPosted> pending_;
    std::map<std::string, AgentStarted> started_;
    std::map<std::string, AgentId> chats_;
    std::vector<Harness> harnesses_;
    bool harnessesChanged_ = false;
    std::map<std::string, int> deviceCodePolls_;
    std::map<std::string, HarnessStatus> disabledFrom_;
};

}  // namespace fulmetry::fakes
