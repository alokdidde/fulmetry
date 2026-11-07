#include "fulmetry/sheets/Netlist.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numeric>
#include <utility>

namespace fulmetry::sheets {
namespace {

// Connection points are compared on a 0.1 um grid, like KiCad's internal units.
using Key = std::pair<std::int64_t, std::int64_t>;
Key keyOf(Point p) { return {std::llround(p.x * 10000.0), std::llround(p.y * 10000.0)}; }

class UnionFind {
public:
    std::size_t add() {
        parent_.push_back(parent_.size());
        return parent_.size() - 1;
    }
    std::size_t find(std::size_t i) {
        while (parent_[i] != i) {
            parent_[i] = parent_[parent_[i]];
            i = parent_[i];
        }
        return i;
    }
    void unite(std::size_t a, std::size_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent_[std::max(a, b)] = std::min(a, b);
    }

private:
    std::vector<std::size_t> parent_;
};

// Strictly inside segment ab (endpoints are matched by key already).
bool onSegmentInterior(Key p, Key a, Key b) {
    if (p == a || p == b) return false;
    std::int64_t cross = (b.first - a.first) * (p.second - a.second) - (b.second - a.second) * (p.first - a.first);
    if (cross != 0) return false;
    return std::min(a.first, b.first) <= p.first && p.first <= std::max(a.first, b.first) &&
           std::min(a.second, b.second) <= p.second && p.second <= std::max(a.second, b.second);
}

struct NameCandidate {
    int priority;  // higher wins
    std::string name;
};

int priorityOf(LabelKind k) {
    switch (k) {
        case LabelKind::Global: return 3;
        case LabelKind::Hierarchical: return 2;
        case LabelKind::Local: return 1;
    }
    return 0;
}

bool nodeLess(const NetNode& a, const NetNode& b) {
    if (a.ref != b.ref) return naturalLess(a.ref, b.ref);
    return naturalLess(a.pin, b.pin);
}

bool hasRealName(const NetNode& n) { return !n.pinName.empty() && n.pinName != "~" && n.pinName != n.pin; }

// KiCad's default names: "Net-(U1-IO2)", "Net-(R1-Pad1)", "unconnected-(U1-RXD0-Pad34)".
std::string autoName(const std::vector<NetNode>& nodes) {
    if (nodes.size() == 1) {
        const NetNode& n = nodes.front();
        return "unconnected-(" + n.ref + "-" + (hasRealName(n) ? n.pinName + "-" : std::string()) + "Pad" + n.pin + ")";
    }
    std::string best;
    bool bestNamed = false;
    for (const auto& n : nodes) {
        bool named = hasRealName(n);
        std::string name = "Net-(" + n.ref + "-" + (named ? n.pinName : "Pad" + n.pin) + ")";
        if (best.empty() || (named && !bestNamed) || (named == bestNamed && name < best)) {
            best = std::move(name);
            bestNamed = named;
        }
    }
    return best;
}

std::string pinKey(std::string_view ref, std::string_view pin) {
    std::string k(ref);
    k += '\n';
    k += pin;
    return k;
}

}  // namespace

bool naturalLess(std::string_view a, std::string_view b) {
    std::size_t i = 0, j = 0;
    auto digit = [](char c) { return c >= '0' && c <= '9'; };
    while (i < a.size() && j < b.size()) {
        if (digit(a[i]) && digit(b[j])) {
            std::size_t i0 = i, j0 = j;
            while (i0 < a.size() && a[i0] == '0') ++i0;
            while (j0 < b.size() && b[j0] == '0') ++j0;
            std::size_t i1 = i0, j1 = j0;
            while (i1 < a.size() && digit(a[i1])) ++i1;
            while (j1 < b.size() && digit(b[j1])) ++j1;
            if (i1 - i0 != j1 - j0) return i1 - i0 < j1 - j0;
            auto da = a.substr(i0, i1 - i0), db = b.substr(j0, j1 - j0);
            if (da != db) return da < db;
            i = i1;
            j = j1;
        } else {
            if (a[i] != b[j]) return a[i] < b[j];
            ++i;
            ++j;
        }
    }
    if ((a.size() - i) != (b.size() - j)) return a.size() - i < b.size() - j;
    return a < b;
}

bool Net::has(std::string_view ref, std::string_view pin) const {
    for (const auto& n : nodes)
        if (n.ref == ref && (n.pin == pin || n.pinName == pin)) return true;
    return false;
}

std::vector<Net> buildNets(const Schematic& sch) {
    UnionFind uf;
    std::map<Key, std::size_t> points;
    auto point = [&](Point p) {
        Key k = keyOf(p);
        auto it = points.find(k);
        if (it != points.end()) return it->second;
        std::size_t id = uf.add();
        points.emplace(k, id);
        return id;
    };

    std::vector<std::pair<std::size_t, NetNode>> pinNodes;
    std::vector<std::pair<std::size_t, NameCandidate>> names;
    std::vector<std::size_t> noConnects;

    for (const auto& s : sch.symbols) {
        bool power = sch.isPower(s);
        for (const auto& pp : placedPins(sch, s)) {
            std::size_t id = point(pp.at);
            if (power) {
                if (pp.pin->type == PinType::PowerIn) {
                    std::string name = s.value.empty() ? pp.pin->name : s.value;
                    if (!name.empty()) names.push_back({id, {4, name}});
                }
                continue;
            }
            pinNodes.push_back({id, NetNode{s.reference, pp.pin->number, pp.pin->name, pp.pin->type, pp.at}});
        }
    }
    for (const auto& l : sch.labels) names.push_back({point(l.at), {priorityOf(l.kind), l.text}});
    for (const auto& j : sch.junctions) point(j.at);
    for (const auto& nc : sch.noConnects) noConnects.push_back(point(nc.at));

    std::vector<std::pair<Key, Key>> segments;
    for (const auto& w : sch.wires) {
        Key a = keyOf(w.a), b = keyOf(w.b);
        uf.unite(point(w.a), point(w.b));
        if (a != b) segments.push_back({a, b});
    }
    // Anything that lands on the middle of a wire joins it (labels, pins, junctions, T-joints).
    for (const auto& [k, id] : points)
        for (const auto& [a, b] : segments)
            if (onSegmentInterior(k, a, b)) uf.unite(id, points.at(a));

    // Same name, same net.
    std::map<std::string, std::size_t> firstWithName;
    for (const auto& [id, cand] : names) {
        auto [it, inserted] = firstWithName.emplace(cand.name, id);
        if (!inserted) uf.unite(id, it->second);
    }

    std::map<std::size_t, Net> byRoot;
    std::map<std::size_t, NameCandidate> best;
    for (auto& [id, node] : pinNodes) byRoot[uf.find(id)].nodes.push_back(std::move(node));
    for (const auto& [id, cand] : names) {
        auto it = byRoot.find(uf.find(id));
        if (it == byRoot.end()) continue;
        it->second.labels.push_back(cand.name);
        auto b = best.find(it->first);
        if (b == best.end() || cand.priority > b->second.priority ||
            (cand.priority == b->second.priority && cand.name < b->second.name))
            best[it->first] = cand;
    }
    for (std::size_t id : noConnects) {
        auto it = byRoot.find(uf.find(id));
        if (it != byRoot.end()) it->second.noConnect = true;
    }

    std::vector<Net> out;
    out.reserve(byRoot.size());
    for (auto& [root, net] : byRoot) {
        std::sort(net.nodes.begin(), net.nodes.end(), nodeLess);
        std::sort(net.labels.begin(), net.labels.end());
        net.labels.erase(std::unique(net.labels.begin(), net.labels.end()), net.labels.end());
        auto b = best.find(root);
        if (b != best.end()) {
            net.name = b->second.name;
        } else {
            net.autoNamed = true;
            net.name = autoName(net.nodes);
        }
        out.push_back(std::move(net));
    }
    std::sort(out.begin(), out.end(), [](const Net& a, const Net& b) { return naturalLess(a.name, b.name); });
    return out;
}

Netlist::Netlist(std::vector<Net> nets) : nets_(std::move(nets)) {
    for (std::size_t i = 0; i < nets_.size(); ++i) {
        byName_.emplace("\n" + nets_[i].name, i);
        for (const auto& n : nets_[i].nodes) {
            byPin_.emplace(pinKey(n.ref, n.pin), i);
            if (!n.pinName.empty()) byName_.emplace(pinKey(n.ref, n.pinName), i);
        }
    }
}

const Net* Netlist::netOf(std::string_view ref, std::string_view pin) const {
    if (ref.empty()) return nullptr;
    std::string k = pinKey(ref, pin);
    if (auto it = byPin_.find(k); it != byPin_.end()) return &nets_[it->second];
    if (auto it = byName_.find(k); it != byName_.end()) return &nets_[it->second];
    return nullptr;
}

const Net* Netlist::find(std::string_view name) const {
    auto it = byName_.find("\n" + std::string(name));
    return it == byName_.end() ? nullptr : &nets_[it->second];
}

}  // namespace fulmetry::sheets
