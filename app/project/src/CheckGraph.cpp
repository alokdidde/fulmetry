#include "fulmetry/project/CheckGraph.h"

#include <algorithm>
#include <utility>

namespace fulmetry {
namespace {

bool reads(const Check& c, const PartId& part) {
    return std::find(c.inputs.begin(), c.inputs.end(), part) != c.inputs.end();
}

}  // namespace

CheckGraph::CheckGraph(Project& project) : project_(project) {
    for (const auto& c : project_.checks)
        for (const auto& [part, hash] : c.inputHashes)
            if (!hash.empty()) hashes_[PartId(part)] = hash;
}

std::vector<CheckId> CheckGraph::checksFor(const PartId& part) const {
    std::vector<CheckId> out;
    for (const auto& c : project_.checks)
        if (reads(c, part)) out.push_back(c.id);
    return out;
}

Result<std::vector<CheckId>> CheckGraph::partChanged(const PartId& part, const std::string& newHash) {
    const Part* p = project_.findPart(part);
    if (!p) return Error{"not_found", "no part " + part.value};
    hashes_[part] = newHash;
    std::vector<CheckId> changed;
    for (auto& c : project_.checks) {
        if (!reads(c, part)) continue;
        auto it = c.inputHashes.find(part.value);
        if (it != c.inputHashes.end() && it->second == newHash) continue;
        c.verdict = Verdict::Unresolved;
        c.reason = "Changed: " + p->name;
        c.inputHashes[part.value] = newHash;
        changed.push_back(c.id);
    }
    return changed;
}

Status CheckGraph::recordVerdict(const CheckId& check, Verdict verdict, std::string reason) {
    if (verdict == Verdict::Unresolved)
        return Error{"invalid_argument", "a run records supported or refuted, not unresolved"};
    Check* c = findCheck(check);
    if (!c) return Error{"not_found", "no check " + check.value};
    c->verdict = verdict;
    c->reason = std::move(reason);
    c->inputHashes.clear();
    for (const auto& in : c->inputs) {
        auto it = hashes_.find(in);
        if (it != hashes_.end()) c->inputHashes[in.value] = it->second;
    }
    return Ok{};
}

std::string CheckGraph::currentHash(const PartId& part) const {
    auto it = hashes_.find(part);
    return it == hashes_.end() ? std::string() : it->second;
}

Check* CheckGraph::findCheck(const CheckId& id) {
    for (auto& c : project_.checks)
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace fulmetry
