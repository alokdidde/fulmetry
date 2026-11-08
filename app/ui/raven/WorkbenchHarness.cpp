// Harnesses in the workbench: the onboarding screen (connect, install, sign in, pick the default) and the picker
// under the composer. The state lives in WorkbenchModel; setup calls go to the agent back end (T3 Code).
#include <algorithm>
#include <cstdio>

#include "WorkbenchView.h"

namespace fulmetry::ui {

using namespace kit;
using namespace kit::color;

namespace {

constexpr const char* kRegistry = "registry";

void setText(Rid label, const std::string& text) {
    if (label != R_INVALID_ID) Rlabel_set_text(label, text.c_str());
}

uint32_t accentOf(const Harness& h) { return parseHex(h.accent, kBody); }

struct Tone {
    const char* text;
    uint32_t bg, fg;
};
Tone toneOf(const Harness& h) {
    switch (h.status) {
        case HarnessStatus::Ready: return {"Ready", 0xE3F6EE, 0x0E6B45};
        case HarnessStatus::NeedsSignIn: return {"Sign in", 0xFDF1E2, 0x9A5A08};
        case HarnessStatus::NeedsInstall: return {"Not installed", 0xF1F3F5, 0x5D6670};
        case HarnessStatus::Checking:
            return {h.setup.step == HarnessSetup::Step::Installing ? "Installing" : "Signing in", 0xE7F0FD, 0x1452B3};
        case HarnessStatus::Disabled: return {"Off", 0xF1F3F5, 0x8A929B};
        case HarnessStatus::Unavailable: return {"Can't run", 0xFCEBEA, 0xB3261E};
    }
    return {"", kHud2, kMute};
}

std::string statusLine(const Harness& h) {
    const std::string ver = h.version.empty() ? "" : "Version " + h.version;
    switch (h.status) {
        case HarnessStatus::Ready:
            return "Signed in" + (h.account.empty() ? "" : " with " + h.account) + (ver.empty() ? "" : " \xC2\xB7 " + ver);
        case HarnessStatus::NeedsSignIn:
            return std::string(h.version.empty() ? "Installed" : "Installed (version " + h.version + ")") +
                   ". Sign in so agents can run on it.";
        case HarnessStatus::NeedsInstall:
            return h.canInstall ? "Not on this computer yet. T3 Code can install and update it for you."
                                : "Not on this computer. Install it yourself, then check again.";
        case HarnessStatus::Checking:
            switch (h.setup.prompt) {
                case HarnessSetup::Prompt::DeviceCode: return "Sign in with a one-time code.";
                case HarnessSetup::Prompt::Browser: return "Sign in with your " + h.maker + " account in your browser.";
                case HarnessSetup::Prompt::Credentials: return "Add your key. It stays on this computer, in T3 Code's secret store.";
                case HarnessSetup::Prompt::Terminal: return "Finish signing in in the terminal below.";
                case HarnessSetup::Prompt::None: break;
            }
            return h.setup.step == HarnessSetup::Step::Installing ? "Installing it for you." : "Checking the sign-in.";
        case HarnessStatus::Disabled: return "Turned off. Its install and sign-in are kept.";
        case HarnessStatus::Unavailable: return h.message.empty() ? "It cannot run on this computer." : h.message;
    }
    return {};
}

}  // namespace

// ------------------------------------------------------------------ shared pieces

Rid WorkbenchView::monogram(Rid parent, const Harness& h, float size) {
    Rid tile = border(parent, accentOf(h), 1, size * 0.22f, 0, 0);
    Rwidget_set_size(tile, size, size);
    Rslot_set_valign(tile, R_VALIGN_CENTER);
    std::string letter = h.name.empty() ? "?" : h.name.substr(0, 1);
    Rid l = label(tile, letter, fonts_.tightBold, size * 0.5f, kHud);
    Rslot_set_halign(l, R_HALIGN_CENTER);
    Rslot_set_valign(l, R_VALIGN_CENTER);
    return tile;
}

Rid WorkbenchView::statusPill(Rid parent, const Harness& h) {
    const Tone t = toneOf(h);
    Rid pill = border(parent, t.bg, 1, 9, 8, 2);
    Rslot_set_valign(pill, R_VALIGN_CENTER);
    label(pill, t.text, fonts_.semibold, 11, t.fg);
    return pill;
}

Rid WorkbenchView::button(Rid parent, const std::string& text, bool primary, std::function<void()> fn,
                          const std::string& iconName) {
    const uint32_t bg = primary ? kAccent : kHud2;
    Rid b = border(parent, bg, 1, 3, primary ? 16 : 12, 8);
    if (!primary) outline(b, kLine);
    Rslot_set_valign(b, R_VALIGN_CENTER);
    Rid row = hbox(b, 7);
    if (!iconName.empty()) icon(row, opt_.assets, iconName);
    Rid l = label(row, text, fonts_.semibold, 13, primary ? kHud : kInk);
    Rslot_set_valign(l, R_VALIGN_CENTER);
    onClick(b, [this, fn = std::move(fn)] { post(fn); }, {bg, primary ? 0xD12A25u : 0xE9ECEFu, 1, 1, true});
    return b;
}

void WorkbenchView::report(const char* what, const std::string& id, const Status& st) {
    if (st.ok()) std::printf("harness %s: %s\n", what, id.c_str());
    else std::printf("harness %s: %s refused: %s\n", what, id.c_str(), st.error().message.c_str());
    std::fflush(stdout);
}

bool WorkbenchView::harnessCommand(const std::string& command) {
    const auto colon = command.find(':');
    if (colon == std::string::npos) return false;
    const std::string verb = command.substr(0, colon), id = command.substr(colon + 1);
    if (verb == "choose") {
        const auto slash = id.find('/');
        const bool ok = model_.choose(id.substr(0, slash), slash == std::string::npos ? "" : id.substr(slash + 1));
        std::printf("harness choose: %s%s\n", id.c_str(), ok ? "" : " refused");
        std::fflush(stdout);
        return ok;
    }
    if (!agent_) return false;
    std::string id2 = id;
    if (verb == "answer") {
        // answer:<id>=<value> fills every field of the prompt with <value> (the scripted runs)
        if (auto eq = id.find('='); eq != std::string::npos) {
            id2 = id.substr(0, eq);
            for (const auto& x : agent_->harnesses())
                if (x.id == id2)
                    for (const auto& f : x.setup.fields) credentialDraft_[f.name] = id.substr(eq + 1);
        }
    }
    return runHarness(verb, id2);
}

bool WorkbenchView::runHarness(const std::string& verb, const std::string& id) {
    Status st = Ok{};
    if (verb == "install") st = agent_->installHarness(id);
    else if (verb == "signin") st = agent_->signInHarness(id);
    else if (verb == "answer") {
        SignInAnswer a;
        for (const auto& [name, input] : credentialInputs_) {
            const char* t = Rtext_input_get_text(input);
            if (t && *t) a.values[name] = t;
        }
        for (const auto& [name, v] : credentialDraft_)
            if (!a.values.count(name)) a.values[name] = v;
        st = agent_->answerSignIn(id, a);
        if (st.ok()) credentialDraft_.clear();
    } else if (verb == "cancel") st = agent_->cancelHarnessSetup(id);
    else if (verb == "signout") st = agent_->signOutHarness(id);
    else if (verb == "off") st = agent_->setHarnessEnabled(id, false);
    else if (verb == "on") st = agent_->setHarnessEnabled(id, true);
    else if (verb == "remove") st = agent_->removeHarness(id);
    else if (verb == "add") {
        auto added = agent_->addRegistryHarness(id);
        st = added.ok() ? Status(Ok{}) : Status(added.error());
        if (added.ok()) onboardSel_ = added.value();
    } else return false;
    report(verb.c_str(), id, st);
    // The back end's answer is the new list; show it now rather than at the next poll.
    model_.setHarnesses(agent_->harnesses());
    return st.ok();
}

// ------------------------------------------------------------------ binding and the chip

void WorkbenchView::bindHarness() {
    onClick(find("harness.chip"), [this] { post([this] { setPickerOpen(!pickerOpen_); }); },
            {kHud, kHud2, 1, 1, true});
    onClick(find("onboard.close"), [this] { post([this] { setOnboardOpen(false); }); },
            {kHud2, 0xE9ECEF, 1, 1, true});
    onClick(find("onboard.done"), [this] {
        post([this] {
            if (!model_.needsOnboarding()) setOnboardOpen(false);
        });
    });
    setText(find("onboard.title"), "Connect your harnesses");
    Rid sub = find("onboard.sub");
    Rlabel_set_wrap(sub, R_TRUE);
    setText(sub,
            "Harnesses are the agent runtimes that do the work for each comment and chat request. Fulmetry runs "
            "them through T3 Code, so any harness T3 Code supports works here. Connect as many as you like and "
            "pick one per message.");
    if (agent_) model_.setHarnesses(agent_->harnesses());
}

void WorkbenchView::buildHarnessChip() {
    const HarnessChoice c = model_.choice();
    const Harness* h = model_.harness(c.harness);
    setText(find("harness.label"), c.harness.empty() ? "Connect a harness" : model_.choiceLabel());
    setBackground(find("harness.dot"), h ? accentOf(*h) : kAccent);
    setText(find("onboard.choice"), c.harness.empty() ? "Nothing yet: connect a harness" : model_.choiceLabel());
    setBackground(find("onboard.choice.dot"), h ? accentOf(*h) : kMute);
}

// ------------------------------------------------------------------ picker

void WorkbenchView::setPickerOpen(bool open) {
    pickerOpen_ = open;
    if (open) {
        setMenuOpen(false);
        buildPicker();
    }
    Rid p = find("picker");
    // Over the composer, on whichever side the chat is docked.
    Rslot_set_halign(p, chatLeft_ ? R_HALIGN_LEFT : R_HALIGN_RIGHT);
    Rslot_set_padding(p, chatLeft_ ? 12 : 0, 0, chatLeft_ ? 0 : 12, 88);
    Rwidget_set_visibility(p, open ? R_VISIBLE : R_COLLAPSED);
    std::printf("picker %s\n", open ? "open" : "closed");
    std::fflush(stdout);
}

void WorkbenchView::buildPicker() {
    Rid body = find("picker.body");
    Rwidget_clear_children(body);
    const HarnessChoice cur = model_.choice();
    Rid head = eyebrow(body, "Run new agents on", 11);
    Rslot_set_padding(head, 8, 6, 8, 6);
    for (const Harness* h : model_.readyHarnesses()) {
        const bool on = h->id == cur.harness;
        Rid row = border(body, on ? 0xFCEBEA : kHud, 1, 4, 8, 7);
        Rid line = hbox(row, 9);
        monogram(line, *h, 22);
        Rid names = vbox(line, 0);
        Rslot_set_fill(names, 1);
        label(names, h->name, fonts_.semibold, 13, kInk);
        label(names, h->maker, fonts_.regular, 11, kMute);
        if (on) icon(line, opt_.assets, "check");
        const std::string id = h->id;
        onClick(row, [this, id] {
            post([this, id] {
                harnessCommand("choose:" + id);
                buildPicker();
            });
        }, {on ? 0xFCEBEAu : kHud, on ? 0xF9DEDCu : kHud2, 1, 1, true});
        if (!on || h->models.size() < 2) continue;
        // the chosen harness lists its models
        Rid models = vbox(body, 0);
        Rslot_set_padding(models, 39, 2, 0, 4);
        for (const auto& m : h->models) {
            const bool mOn = m.id == cur.model;
            Rid mrow = border(models, kHud, 1, 3, 8, 4);
            Rid ml = hbox(mrow, 8);
            Rid dot = border(ml, mOn ? kAccent : kHud, 1, 5, 0, 0);
            outline(dot, mOn ? kAccent : 0xB8BFC7);
            Rwidget_set_size(dot, 10, 10);
            Rslot_set_valign(dot, R_VALIGN_CENTER);
            Rid name = label(ml, m.name, mOn ? fonts_.semibold : fonts_.regular, 12.5f, kInk);
            Rslot_set_fill(name, 1);
            label(ml, m.id, fonts_.mono, 10.5f, kMute);
            const std::string cmd = "choose:" + id + "/" + m.id;
            onClick(mrow, [this, cmd] {
                post([this, cmd] {
                    harnessCommand(cmd);
                    setPickerOpen(false);
                });
            }, {kHud, kHud2, 1, 1, true});
        }
    }
    int waiting = 0;
    for (const auto& h : model_.harnesses())
        if (h.status != HarnessStatus::Ready) ++waiting;
    Rid r = rule(body, false);
    Rslot_set_padding(r, 0, 6, 0, 6);
    Rid manage = border(body, kHud, 1, 4, 8, 8);
    Rid ml = hbox(manage, 8);
    icon(ml, opt_.assets, "harness");
    Rid t = label(ml, "Connect and manage harnesses", fonts_.semibold, 12.5f, kInk);
    Rslot_set_fill(t, 1);
    if (waiting > 0) label(ml, std::to_string(waiting) + " not set up", fonts_.regular, 11, kMute);
    onClick(manage, [this] {
        post([this] {
            setPickerOpen(false);
            setOnboardOpen(true);
        });
    }, {kHud, kHud2, 1, 1, true});
}

// ------------------------------------------------------------------ onboarding

void WorkbenchView::setOnboardOpen(bool open, const std::string& select) {
    if (open == onboardOpen_ && select.empty()) return;
    onboardOpen_ = open;
    // Opened by hand, or closed: it is no longer the first run, and it does not reopen on its own.
    if (!open || !model_.needsOnboarding()) onboardAuto_ = false;
    if (open) {
        setPickerOpen(false);
        setMenuOpen(false);
        if (!select.empty()) onboardSel_ = select;
        if (onboardSel_.empty() || (onboardSel_ != kRegistry && !model_.harness(onboardSel_))) {
            // the first harness that needs something, else the chosen one
            onboardSel_ = model_.choice().harness;
            for (const auto& h : model_.harnesses())
                if (h.status == HarnessStatus::NeedsSignIn) { onboardSel_ = h.id; break; }
            if (onboardSel_.empty() && !model_.harnesses().empty()) onboardSel_ = model_.harnesses().front().id;
        }
        shownHarness_ = 0;
        buildOnboard();
    }
    Rwidget_set_visibility(find("onboard"), open ? R_VISIBLE : R_COLLAPSED);
    std::printf("onboarding %s\n", open ? "open" : "closed");
    std::fflush(stdout);
}

void WorkbenchView::buildOnboard() {
    // keep what the person typed into a credential field across a rebuild
    for (const auto& [name, input] : credentialInputs_)
        if (const char* t = Rtext_input_get_text(input)) credentialDraft_[name] = t;
    credentialInputs_.clear();

    const bool ready = !model_.needsOnboarding();
    Rwidget_set_visibility(find("onboard.close"), ready ? R_VISIBLE : R_HIDDEN);
    setBackground(find("onboard.done"), ready ? kAccent : 0xC9CED4);
    setText(find("onboard.done.label"), !ready ? "Connect a harness to continue" : onboardAuto_ ? (homeOpen_ ? "Continue" : "Open the workbench") : "Done");
    setBackground(find("onboard.scrim"), 0xE9EDF1, 1);

    Rid list = find("onboard.list");
    Rwidget_clear_children(list);
    Rid ready1 = eyebrow(list, "Built into T3 Code", 11);
    Rslot_set_padding(ready1, 8, 4, 8, 6);
    bool registryHead = false;
    for (const auto& h : model_.harnesses()) {
        if (h.fromRegistry && !registryHead) {
            registryHead = true;
            Rid t = eyebrow(list, "Added from the ACP registry", 11);
            Rslot_set_padding(t, 8, 12, 8, 6);
        }
        onboardRow(list, h);
    }
    // the registry search
    const bool regOn = onboardSel_ == kRegistry;
    Rid add = border(list, regOn ? kHud : 0xF7F8FA, 1, 4, 10, 10);
    Rslot_set_padding(add, 0, 8, 0, 0);
    outline(add, regOn ? kAccent : kLine);
    Rid al = hbox(add, 10);
    Rid plus = border(al, kHud, 1, 7, 0, 0);
    outline(plus, kLine);
    Rwidget_set_size(plus, 32, 32);
    Rid pi = icon(plus, opt_.assets, "plus");
    Rslot_set_halign(pi, R_HALIGN_CENTER);
    Rid an = vbox(al, 0);
    Rslot_set_fill(an, 1);
    label(an, "Add another harness", fonts_.semibold, 13, kInk);
    label(an, "Any agent in the ACP registry", fonts_.regular, 11.5f, kMute);
    onClick(add, [this] {
        post([this] {
            onboardSel_ = kRegistry;
            buildOnboard();
        });
    }, {regOn ? kHud : 0xF7F8FAu, kHud, 1, 1, true});

    Rid detail = find("onboard.detail");
    Rwidget_clear_children(detail);
    if (onboardSel_ == kRegistry) onboardRegistry(detail);
    else if (const Harness* h = model_.harness(onboardSel_)) onboardDetail(detail, *h);
    buildHarnessChip();
    shownHarness_ = model_.harnessVersion();
    shownOnboardSel_ = onboardSel_;
}

void WorkbenchView::onboardRow(Rid list, const Harness& h) {
    const bool on = h.id == onboardSel_;
    const bool chosen = h.id == model_.choice().harness;
    Rid row = border(list, on ? kHud : 0xF7F8FA, 1, 4, 10, 7);
    if (on) {
        outline(row, kLine);
        Rborder_set_shadow(row, 10, 0, 2);
    }
    Rid line = hbox(row, 11);
    monogram(line, h, 32);
    Rid names = vbox(line, 1);
    Rslot_set_fill(names, 1);
    Rslot_set_valign(names, R_VALIGN_CENTER);
    Rid top = hbox(names, 6);
    label(top, h.name, fonts_.semibold, 13.5f, h.status == HarnessStatus::Disabled ? kMute : kInk);
    if (chosen) {
        Rid d = border(top, kInk, 1, 2, 5, 0);
        Rslot_set_valign(d, R_VALIGN_CENTER);
        label(d, "DEFAULT", fonts_.tightSemibold, 9, kHud);
    }
    label(names, h.maker, fonts_.regular, 11.5f, kMute);
    statusPill(line, h);
    const std::string id = h.id;
    onClick(row, [this, id] {
        post([this, id] {
            onboardSel_ = id;
            buildOnboard();
        });
    }, {on ? kHud : 0xF7F8FAu, kHud, 1, 1, true});
}

void WorkbenchView::onboardDetail(Rid body, const Harness& h) {
    Rid head = hbox(body, 16);
    monogram(head, h, 52);
    Rid names = vbox(head, 2);
    Rslot_set_fill(names, 1);
    Rslot_set_valign(names, R_VALIGN_CENTER);
    label(names, h.name, fonts_.tightBold, 24, kInk);
    std::string sub = h.maker;
    if (h.driver != h.id) sub += " \xC2\xB7 " + h.driver;
    label(names, sub, fonts_.regular, 13, kMute);
    Rid pill = statusPill(head, h);
    Rslot_set_valign(pill, R_VALIGN_TOP);

    // where it is in setup: install, sign in, pick a model
    if (h.status != HarnessStatus::Unavailable && h.status != HarnessStatus::Disabled) {
        const bool installing = h.setup.step == HarnessSetup::Step::Installing;
        const bool installed = h.status != HarnessStatus::NeedsInstall && !installing;
        const bool signedIn = h.status == HarnessStatus::Ready;
        struct StepView { const char* name; bool done, current; };
        const StepView steps[] = {{"Install", installed, !installed},
                                  {"Sign in", signedIn, installed && !signedIn},
                                  {"Pick a model", signedIn && model_.choice().harness == h.id, signedIn && model_.choice().harness != h.id}};
        Rid strip = hbox(body, 0);
        int n = 0;
        for (const auto& st : steps) {
            if (n > 0) {
                Rid join = border(strip, st.done || st.current ? kInk : kLine, 1, 0, 0, 0);
                Rwidget_set_size(join, 36, 1);
                Rslot_set_valign(join, R_VALIGN_CENTER);
                Rslot_set_padding(join, 10, 0, 10, 0);
            }
            ++n;
            Rid item = hbox(strip, 8);
            const uint32_t ring = st.done ? kPass : st.current ? kAccent : 0xC9CED4;
            Rid c = border(item, st.done ? kPass : kHud, 1, 11, 0, 0);
            outline(c, ring);
            Rwidget_set_size(c, 22, 22);
            Rslot_set_valign(c, R_VALIGN_CENTER);
            Rid num = label(c, st.done ? "\xE2\x9C\x93" : std::to_string(n), fonts_.semibold, 11.5f,
                            st.done ? kHud : st.current ? kAccent : kMute);
            Rslot_set_halign(num, R_HALIGN_CENTER);
            Rslot_set_valign(num, R_VALIGN_CENTER);
            Rid nl = label(item, st.name, st.current ? fonts_.semibold : fonts_.regular, 13, st.done || st.current ? kInk : kMute);
            Rslot_set_valign(nl, R_VALIGN_CENTER);
        }
    }
    wrapLabel(body, statusLine(h), fonts_.regular, 14, kBody);
    onboardSetup(body, h);

    if (h.status == HarnessStatus::Ready && !h.models.empty()) {
        const HarnessChoice cur = model_.choice();
        const bool isDefault = cur.harness == h.id;
        Rid sec = vbox(body, 8);
        Rslot_set_padding(sec, 0, 6, 0, 0);
        eyebrow(sec, "Models", 11);
        Rid box = border(sec, kHud, 1, 4, 0, 0);
        outline(box, kLine);
        Rid rows = vbox(box, 0);
        for (size_t i = 0; i < h.models.size(); ++i) {
            const auto& m = h.models[i];
            const bool mOn = isDefault && m.id == cur.model;
            if (i > 0) rule(rows, false);
            Rid row = border(rows, mOn ? 0xFDF4F3 : kHud, 1, 0, 14, 10);
            Rid rl = hbox(row, 10);
            Rid dot = border(rl, mOn ? kAccent : kHud, 1, 6, 0, 0);
            outline(dot, mOn ? kAccent : 0xB8BFC7);
            Rwidget_set_size(dot, 12, 12);
            Rslot_set_valign(dot, R_VALIGN_CENTER);
            Rid nm = label(rl, m.name, mOn ? fonts_.semibold : fonts_.regular, 13.5f, kInk);
            Rslot_set_valign(nm, R_VALIGN_CENTER);
            if (i == 0) {
                Rid d = label(rl, "its default", fonts_.regular, 11.5f, kMute);
                Rslot_set_valign(d, R_VALIGN_CENTER);
            }
            spacer(rl, 1);
            label(rl, m.id, fonts_.mono, 11, kMute);
            const std::string cmd = "choose:" + h.id + "/" + m.id;
            const uint32_t bg = mOn ? 0xFDF4F3 : kHud;
            onClick(row, [this, cmd] {
                post([this, cmd] {
                    harnessCommand(cmd);
                    buildOnboard();
                });
            }, {bg, kHud2, 1, 1, true});
        }
        Rid actions = hbox(body, 8);
        if (!isDefault)
            button(actions, "Use for new agents", true, [this, id = h.id] {
                harnessCommand("choose:" + id);
                buildOnboard();
            });
        else {
            Rid ok = border(actions, 0xE3F6EE, 1, 3, 12, 8);
            Rslot_set_valign(ok, R_VALIGN_CENTER);
            label(ok, "New agents run on this", fonts_.semibold, 13, 0x0E6B45);
        }
        spacer(actions, 1);
        if (h.canSignIn) button(actions, "Sign out", false, [this, id = h.id] { harnessCommand("signout:" + id); });
        button(actions, "Turn off", false, [this, id = h.id] { harnessCommand("off:" + id); });
        if (h.fromRegistry) button(actions, "Remove", false, [this, id = h.id] { harnessCommand("remove:" + id); });
    }
}

void WorkbenchView::onboardSetup(Rid body, const Harness& h) {
    using S = HarnessSetup::Step;
    using P = HarnessSetup::Prompt;
    const std::string id = h.id;
    const auto& st = h.setup;

    if (st.step == S::Installing) {
        Rid box = border(body, kHud2, 1, 4, 18, 16);
        Rid col = vbox(box, 10);
        Rid top = hbox(col, 8);
        Rid m = label(top, st.message.empty() ? "Installing" : st.message, fonts_.semibold, 13, kInk);
        Rslot_set_fill(m, 1);
        if (st.fraction >= 0)
            label(top, std::to_string(static_cast<int>(st.fraction * 100 + 0.5)) + "%", fonts_.mono, 12, kMute);
        Rid track = border(col, 0xDDE2E7, 1, 3, 0, 0);
        Rwidget_set_size(track, -1, 6);
        Rid barRow = hbox(track, 0);
        const float f = st.fraction < 0 ? 0.1f : static_cast<float>(std::min(1.0, st.fraction));
        Rid fill = border(barRow, kRun, 1, 3, 0, 0);
        Rslot_set_fill(fill, f);
        Rid rest = border(barRow, 0, 0, 0, 0, 0);
        Rslot_set_fill(rest, 1 - f + 0.0001f);
        Rid acts = hbox(body, 8);
        button(acts, "Cancel", false, [this, id] { harnessCommand("cancel:" + id); });
        return;
    }
    if (st.step == S::Verifying) {
        Rid box = border(body, 0xE7F0FD, 1, 4, 18, 14);
        label(box, st.message.empty() ? "Checking the sign-in\xE2\x80\xA6" : st.message + "\xE2\x80\xA6", fonts_.semibold, 13,
              0x1452B3);
        return;
    }
    if (st.step == S::SigningIn) {
        Rid box = border(body, kHud, 1, 4, 22, 20);
        outline(box, kLine);
        Rid col = vbox(box, 14);
        Rid acts = R_INVALID_ID;
        auto fields = [&](Rid parent) {
            for (const auto& f : st.fields) {
                Rid fcol = vbox(parent, 6);
                label(fcol, f.label, fonts_.semibold, 12.5f, kInk);
                Rid frame = border(fcol, kHud2, 1, 4, 10, 7);
                outline(frame, kLine);
                Rid input = Rwidget_add_child(frame, Rwidget_create(R_WIDGET_TEXT_INPUT));
                Rwidget_set_class(input, "composer");
                Rwidget_set_font(input, fonts_.mono);
                Rwidget_set_metric(input, R_METRIC_FONT_SIZE, 13);
                Rtext_input_set_password(input, f.secret ? 1 : 0);
                Rtext_input_set_placeholder(input, f.name.c_str());
                if (auto it = credentialDraft_.find(f.name); it != credentialDraft_.end())
                    Rtext_input_set_text(input, it->second.c_str());
                credentialInputs_[f.name] = input;
            }
        };
        switch (st.prompt) {
            case P::DeviceCode: {
                eyebrow(col, "Enter this code", 11);
                Rid codeRow = hbox(col, 6);
                for (char ch : st.userCode) {
                    if (ch == '-') {
                        Rid dash = label(codeRow, "\xE2\x80\x93", fonts_.mono, 26, kMute);
                        Rslot_set_valign(dash, R_VALIGN_CENTER);
                        continue;
                    }
                    Rid cell = border(codeRow, kHud2, 1, 4, 0, 0);
                    outline(cell, kLine);
                    Rwidget_set_size(cell, 38, 50);
                    Rid l = label(cell, std::string(1, ch), fonts_.mono, 26, kInk);
                    Rslot_set_halign(l, R_HALIGN_CENTER);
                    Rslot_set_valign(l, R_VALIGN_CENTER);
                }
                Rid where = hbox(col, 6);
                label(where, "at", fonts_.regular, 13, kMute);
                label(where, st.url, fonts_.mono, 12.5f, kRun);
                acts = hbox(col, 8);
                button(acts, "Open the page", true, [url = st.url] { Ropen_url(url.c_str()); }, "external");
                button(acts, "Copy code", false, [code = st.userCode] { Rclipboard_set_text(code.c_str()); });
                wrapLabel(col, st.message.empty() ? "This finishes by itself once you enter the code." : st.message,
                          fonts_.regular, 12.5f, kMute);
                break;
            }
            case P::Browser: {
                wrapLabel(col, st.message.empty() ? "Sign in in your browser, then come back here." : st.message,
                          fonts_.regular, 13.5f, kBody);
                label(col, st.url.size() > 64 ? st.url.substr(0, 64) + "\xE2\x80\xA6" : st.url, fonts_.mono, 11.5f, kMute);
                acts = hbox(col, 8);
                button(acts, "Open sign-in page", !st.fields.empty() ? false : true,
                       [url = st.url] { Ropen_url(url.c_str()); }, "external");
                if (st.fields.empty()) {
                    button(acts, "I've signed in", false, [this, id] { harnessCommand("answer:" + id); });
                } else {
                    // a pasted code finishes the sign-in when the browser could not hand it back by itself
                    fields(col);
                    acts = hbox(col, 8);
                    button(acts, "Submit code", true, [this, id] { harnessCommand("answer:" + id); });
                }
                break;
            }
            case P::Credentials: {
                fields(col);
                if (!st.message.empty()) wrapLabel(col, st.message, fonts_.regular, 12.5f, kMute);
                acts = hbox(col, 8);
                button(acts, "Save and check", true, [this, id] { harnessCommand("answer:" + id); });
                break;
            }
            case P::Terminal: {
                Rid term = border(col, kInk, 1, 4, 12, 10);
                wrapLabel(term, st.output.empty() ? "$" : st.output, fonts_.mono, 12, 0xDDE2E7);
                acts = hbox(col, 8);
                break;
            }
            case P::None: {
                label(col, "Starting the sign-in\xE2\x80\xA6", fonts_.semibold, 13, kInk);
                acts = hbox(col, 8);
                break;
            }
        }
        spacer(acts, 1);
        button(acts, "Cancel", false, [this, id] { harnessCommand("cancel:" + id); });
        return;
    }
    if (st.step == S::Failed && !st.message.empty()) {
        Rid box = border(body, 0xFCEBEA, 1, 4, 16, 12);
        wrapLabel(box, st.message, fonts_.semibold, 13, 0xB3261E);
    }

    Rid acts = hbox(body, 8);
    switch (h.status) {
        case HarnessStatus::NeedsInstall:
            if (h.canInstall) button(acts, "Install " + h.name, true, [this, id] { harnessCommand("install:" + id); });
            break;
        case HarnessStatus::NeedsSignIn:
            button(acts, "Sign in", true, [this, id] { harnessCommand("signin:" + id); });
            break;
        case HarnessStatus::Disabled:
            button(acts, "Turn on", true, [this, id] { harnessCommand("on:" + id); });
            break;
        default: break;
    }
    if (h.status != HarnessStatus::Ready && !h.docsUrl.empty())
        button(acts, "Setup guide", false, [url = h.docsUrl] { Ropen_url(url.c_str()); });
    if (h.status != HarnessStatus::Ready && h.fromRegistry)
        button(acts, "Remove", false, [this, id] { harnessCommand("remove:" + id); });
}

void WorkbenchView::onboardRegistry(Rid body) {
    Rid head = vbox(body, 4);
    label(head, "Add another harness", fonts_.tightBold, 24, kInk);
    wrapLabel(head,
              "T3 Code can run any agent listed in the Agent Client Protocol registry. Adding one installs it the "
              "same way as the built-in harnesses; you sign in next.",
              fonts_.regular, 14, kBody);
    Rid frame = border(body, kHud2, 1, 4, 10, 7);
    outline(frame, kLine);
    Rid fl = hbox(frame, 8);
    icon(fl, opt_.assets, "search");
    Rid input = Rwidget_add_child(fl, Rwidget_create(R_WIDGET_TEXT_INPUT));
    Rslot_set_fill(input, 1);
    Rwidget_set_class(input, "composer");
    Rwidget_set_font(input, fonts_.regular);
    Rwidget_set_metric(input, R_METRIC_FONT_SIZE, 14);
    Rtext_input_set_placeholder(input, "Search the registry");
    Rtext_input_set_text(input, registryQuery_.c_str());
    Rtext_input_set_committed_callback(
        input,
        [](Rid w, const char* text, void* user) {
            auto* v = static_cast<WorkbenchView*>(user);
            std::string q = text ? text : "";
            (void)w;
            v->post([v, q] {
                v->registryQuery_ = q;
                v->registryHits_.clear();
                v->buildOnboard();
            });
        },
        this);
    if (registryHits_.empty() && agent_)
        if (auto found = agent_->searchHarnessRegistry(registryQuery_); found.ok()) registryHits_ = found.value();
    Rid box = border(body, kHud, 1, 4, 0, 0);
    outline(box, kLine);
    Rid rows = vbox(box, 0);
    if (registryHits_.empty()) {
        Rid none = border(rows, kHud, 1, 0, 16, 14);
        label(none, "Nothing in the registry matches.", fonts_.regular, 13, kMute);
    }
    for (size_t i = 0; i < registryHits_.size(); ++i) {
        const auto& r = registryHits_[i];
        if (i > 0) rule(rows, false);
        Rid row = border(rows, kHud, 1, 0, 16, 12);
        Rid rl = hbox(row, 12);
        Harness fake;
        fake.name = r.name;
        fake.accent = "#5D6670";
        monogram(rl, fake, 32);
        Rid names = vbox(rl, 1);
        Rslot_set_fill(names, 1);
        Rid top = hbox(names, 8);
        label(top, r.name, fonts_.semibold, 13.5f, kInk);
        label(top, r.version, fonts_.mono, 11, kMute);
        label(names, r.description, fonts_.regular, 12, kMute);
        const bool added = model_.harness(r.id) != nullptr;
        if (added) {
            Rid ok = border(rl, 0xE3F6EE, 1, 9, 8, 2);
            Rslot_set_valign(ok, R_VALIGN_CENTER);
            label(ok, "Added", fonts_.semibold, 11, 0x0E6B45);
        } else {
            button(rl, "Add", false, [this, id = r.id] {
                harnessCommand("add:" + id);
                buildOnboard();
            });
        }
    }
}

}  // namespace fulmetry::ui
