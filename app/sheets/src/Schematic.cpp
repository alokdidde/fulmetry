#include "fulmetry/sheets/Schematic.h"

#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

#include "fulmetry/sheets/SExpr.h"

namespace fulmetry::sheets {
namespace {

constexpr double kMaxCoord = 1e5;  // mm; anything larger is not a real sheet

struct PinTypeName {
    PinType type;
    const char* name;
};
constexpr PinTypeName kPinTypes[] = {
    {PinType::Input, "input"},           {PinType::Output, "output"},
    {PinType::Bidirectional, "bidirectional"}, {PinType::TriState, "tri_state"},
    {PinType::Passive, "passive"},       {PinType::Free, "free"},
    {PinType::Unspecified, "unspecified"}, {PinType::PowerIn, "power_in"},
    {PinType::PowerOut, "power_out"},    {PinType::OpenCollector, "open_collector"},
    {PinType::OpenEmitter, "open_emitter"}, {PinType::NoConnect, "no_connect"},
};

class Parser {
public:
    std::optional<Error> error;

    bool fail(const SNode& at, const std::string& what) {
        if (!error) error = Error{"invalid_schematic", "line " + std::to_string(at.line) + ": " + what};
        return false;
    }

    bool number(const SNode& n, std::size_t i, double& out, const char* what) {
        auto v = n.number(i);
        if (!v) return fail(n, std::string("expected a number for ") + what);
        out = *v;
        return true;
    }

    bool coord(const SNode& n, std::size_t i, double& out, const char* what) {
        if (!number(n, i, out, what)) return false;
        if (std::fabs(out) > kMaxCoord) return fail(n, std::string("coordinate out of range in ") + what);
        return true;
    }

    // (at x y [angle])
    bool at(const SNode& parent, Point& p, double* angle, const char* what) {
        const SNode* a = parent.find("at");
        if (!a) return fail(parent, std::string("missing (at ...) in ") + what);
        if (!coord(*a, 1, p.x, what) || !coord(*a, 2, p.y, what)) return false;
        if (angle) {
            *angle = 0;
            if (a->at(3) && !number(*a, 3, *angle, what)) return false;
            if (std::fabs(*angle) > 3600) return fail(*a, std::string("angle out of range in ") + what);
        }
        return true;
    }

    std::string str(const SNode& n, std::size_t i) {
        auto s = n.str(i);
        return s ? std::string(*s) : std::string();
    }

    std::map<std::string, std::string> properties(const SNode& n) {
        std::map<std::string, std::string> out;
        for (const SNode* p : n.findAll("property")) {
            auto k = p->str(1);
            auto v = p->str(2);
            if (k && v) out[std::string(*k)] = std::string(*v);
        }
        return out;
    }

    bool pin(const SNode& n, int unit, int bodyStyle, LibPin& out) {
        auto type = n.str(1);
        if (!type) return fail(n, "pin without an electrical type");
        bool known = false;
        for (const auto& t : kPinTypes)
            if (*type == t.name) { out.type = t.type; known = true; }
        if (!known) return fail(n, "unknown pin type '" + std::string(*type) + "'");
        if (!at(n, out.at, &out.angleDeg, "pin")) return false;
        if (const SNode* len = n.find("length"))
            if (!number(*len, 1, out.length, "pin length")) return false;
        const SNode* num = n.find("number");
        if (!num || !num->str(1)) return fail(n, "pin without a number");
        out.number = str(*num, 1);
        if (const SNode* nm = n.find("name")) out.name = str(*nm, 1);
        out.hidden = n.hasFlag("hide");
        out.unit = unit;
        out.bodyStyle = bodyStyle;
        return true;
    }

    // Sub-symbol names end in _<unit>_<bodyStyle>, e.g. "R_1_1".
    static void unitOf(std::string_view name, int& unit, int& bodyStyle) {
        unit = 0;
        bodyStyle = 0;
        auto last = name.rfind('_');
        if (last == std::string_view::npos || last == 0) return;
        auto prev = name.rfind('_', last - 1);
        if (prev == std::string_view::npos) return;
        auto u = parseNumber(name.substr(prev + 1, last - prev - 1));
        auto b = parseNumber(name.substr(last + 1));
        if (!u || !b || *u < 0 || *b < 0 || *u > 1e6 || *b > 1e6) return;
        unit = static_cast<int>(*u);
        bodyStyle = static_cast<int>(*b);
    }

    bool libSymbol(const SNode& n, LibSymbol& out, std::string& extends) {
        auto name = n.str(1);
        if (!name) return fail(n, "lib symbol without a name");
        out.name = std::string(*name);
        out.power = n.find("power") != nullptr;
        out.properties = properties(n);
        if (const SNode* e = n.find("extends")) extends = str(*e, 1);
        for (const SNode* p : n.findAll("pin")) {
            LibPin lp;
            if (!pin(*p, 0, 0, lp)) return false;
            out.pins.push_back(std::move(lp));
        }
        for (const SNode* sub : n.findAll("symbol")) {
            int unit = 0, body = 0;
            if (auto subName = sub->str(1)) unitOf(*subName, unit, body);
            for (const SNode* p : sub->findAll("pin")) {
                LibPin lp;
                if (!pin(*p, unit, body, lp)) return false;
                out.pins.push_back(std::move(lp));
            }
        }
        return true;
    }

    bool placedSymbol(const SNode& n, PlacedSymbol& out) {
        const SNode* lib = n.find("lib_id");
        if (!lib || !lib->str(1)) return fail(n, "symbol without lib_id");
        out.libId = str(*lib, 1);
        if (const SNode* ln = n.find("lib_name")) out.libName = str(*ln, 1);
        if (!at(n, out.at, &out.rotationDeg, "symbol")) return false;
        if (const SNode* m = n.find("mirror")) {
            auto axis = m->str(1);
            out.mirrorX = axis && *axis == "x";
            out.mirrorY = axis && *axis == "y";
        }
        double v = 0;
        if (const SNode* u = n.find("unit")) {
            if (!number(*u, 1, v, "unit") || v < 0 || v > 1e6) return fail(*u, "bad unit");
            out.unit = static_cast<int>(v);
        }
        const SNode* conv = n.find("convert");
        if (!conv) conv = n.find("body_style");
        if (conv) {
            if (!number(*conv, 1, v, "body style") || v < 0 || v > 1e6) return fail(*conv, "bad body style");
            out.bodyStyle = static_cast<int>(v);
        }
        if (const SNode* u = n.find("uuid")) out.uuid = str(*u, 1);
        out.properties = properties(n);
        auto prop = [&](const char* k) {
            auto it = out.properties.find(k);
            return it == out.properties.end() ? std::string() : it->second;
        };
        out.reference = prop("Reference");
        out.value = prop("Value");
        out.footprint = prop("Footprint");
        // The instance reference is authoritative when the property is not annotated.
        if (const SNode* inst = n.find("instances"))
            for (const SNode* proj : inst->findAll("project"))
                for (const SNode* path : proj->findAll("path"))
                    if (const SNode* r = path->find("reference"))
                        if (out.reference.empty() || out.reference.find('?') != std::string::npos)
                            out.reference = str(*r, 1);
        return true;
    }

    bool wire(const SNode& n, std::vector<Wire>& out) {
        const SNode* pts = n.find("pts");
        if (!pts) return fail(n, "wire without pts");
        std::vector<Point> points;
        for (const SNode* xy : pts->findAll("xy")) {
            Point p;
            if (!coord(*xy, 1, p.x, "wire") || !coord(*xy, 2, p.y, "wire")) return false;
            points.push_back(p);
        }
        if (points.size() < 2) return fail(n, "wire needs at least two points");
        for (std::size_t i = 1; i < points.size(); ++i) out.push_back({points[i - 1], points[i]});
        return true;
    }

    bool label(const SNode& n, LabelKind kind, Label& out) {
        auto text = n.str(1);
        if (!text) return fail(n, "label without text");
        out.kind = kind;
        out.text = std::string(*text);
        return at(n, out.at, &out.angleDeg, "label");
    }

    bool schematic(const SNode& root, Schematic& sch) {
        if (root.head() != "kicad_sch") return fail(root, "not a KiCad schematic (expected kicad_sch)");
        if (const SNode* v = root.find("version")) {
            double d = 0;
            if (!number(*v, 1, d, "version")) return false;
            sch.version = static_cast<long>(d);
        }
        if (const SNode* g = root.find("generator")) sch.generator = str(*g, 1);
        if (const SNode* g = root.find("generator_version")) sch.generatorVersion = str(*g, 1);
        if (const SNode* u = root.find("uuid")) sch.uuid = str(*u, 1);
        if (const SNode* p = root.find("paper")) sch.paper = str(*p, 1);
        if (const SNode* tb = root.find("title_block"))
            if (const SNode* t = tb->find("title")) sch.title = str(*t, 1);

        std::map<std::string, std::string> extends;  // child -> parent
        if (const SNode* libs = root.find("lib_symbols")) {
            for (const SNode* s : libs->findAll("symbol")) {
                LibSymbol ls;
                std::string parent;
                if (!libSymbol(*s, ls, parent)) return false;
                if (!parent.empty()) extends[ls.name] = parent;
                sch.libSymbols.push_back(std::move(ls));
            }
        }
        // Derived symbols inherit their parent's pins (KiCad normally flattens these on save).
        for (auto& ls : sch.libSymbols) {
            auto it = extends.find(ls.name);
            if (it == extends.end() || !ls.pins.empty()) continue;
            std::string base = it->second;
            auto colon = ls.name.find(':');
            for (const auto& p : sch.libSymbols) {
                bool same = p.name == base ||
                            (colon != std::string::npos && p.name == ls.name.substr(0, colon + 1) + base);
                if (same && &p != &ls) { ls.pins = p.pins; break; }
            }
        }

        for (const auto& item : root.items) {
            std::string_view h = item.head();
            if (h == "symbol") {
                PlacedSymbol s;
                if (!placedSymbol(item, s)) return false;
                sch.symbols.push_back(std::move(s));
            } else if (h == "wire") {
                if (!wire(item, sch.wires)) return false;
            } else if (h == "junction") {
                Junction j;
                if (!at(item, j.at, nullptr, "junction")) return false;
                sch.junctions.push_back(j);
            } else if (h == "no_connect") {
                NoConnect nc;
                if (!at(item, nc.at, nullptr, "no_connect")) return false;
                sch.noConnects.push_back(nc);
            } else if (h == "label" || h == "global_label" || h == "hierarchical_label") {
                Label l;
                LabelKind k = h == "label" ? LabelKind::Local
                              : h == "global_label" ? LabelKind::Global
                                                    : LabelKind::Hierarchical;
                if (!label(item, k, l)) return false;
                sch.labels.push_back(std::move(l));
            }
        }
        return true;
    }
};

}  // namespace

const char* toName(PinType t) {
    for (const auto& p : kPinTypes)
        if (p.type == t) return p.name;
    return "";
}

const LibSymbol* Schematic::libSymbolFor(const PlacedSymbol& s) const {
    const std::string& key = s.libName.empty() ? s.libId : s.libName;
    for (const auto& l : libSymbols)
        if (l.name == key) return &l;
    return nullptr;
}

const PlacedSymbol* Schematic::findSymbol(std::string_view reference) const {
    for (const auto& s : symbols)
        if (s.reference == reference) return &s;
    return nullptr;
}

bool Schematic::isPower(const PlacedSymbol& s) const {
    const LibSymbol* l = libSymbolFor(s);
    return l ? l->power : (!s.reference.empty() && s.reference[0] == '#');
}

Result<Schematic> parseSchematic(std::string_view text) {
    auto tree = parseSExpr(text);
    if (!tree) return tree.error();
    Schematic sch;
    Parser p;
    if (!p.schematic(tree.value(), sch)) return *p.error;
    return sch;
}

Result<Schematic> loadSchematic(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"not_found", "cannot open " + path};
    std::ostringstream ss;
    ss << in.rdbuf();
    return parseSchematic(ss.str());
}

Point toSheet(const PlacedSymbol& s, Point lib) {
    // Library frame is y-up; the sheet is y-down.
    double x = lib.x, y = -lib.y;
    // Rotation is counter-clockwise on screen. Use exact values for right angles.
    double a = std::fmod(s.rotationDeg, 360.0);
    if (a < 0) a += 360.0;
    double c, sn;
    if (a == 0) { c = 1; sn = 0; }
    else if (a == 90) { c = 0; sn = 1; }
    else if (a == 180) { c = -1; sn = 0; }
    else if (a == 270) { c = 0; sn = -1; }
    else {
        constexpr double kPi = 3.14159265358979323846;
        c = std::cos(a * kPi / 180.0);
        sn = std::sin(a * kPi / 180.0);
    }
    double rx = x * c + y * sn;
    double ry = -x * sn + y * c;
    // KiCad applies the mirror after the rotation.
    if (s.mirrorX) ry = -ry;
    if (s.mirrorY) rx = -rx;
    return {s.at.x + rx, s.at.y + ry};
}

std::vector<PlacedPin> placedPins(const Schematic& sch, const PlacedSymbol& s) {
    std::vector<PlacedPin> out;
    const LibSymbol* lib = sch.libSymbolFor(s);
    if (!lib) return out;
    for (const auto& p : lib->pins) {
        if (p.unit != 0 && p.unit != s.unit) continue;
        if (p.bodyStyle != 0 && p.bodyStyle != s.bodyStyle) continue;
        out.push_back({&s, &p, toSheet(s, p.at)});
    }
    return out;
}

}  // namespace fulmetry::sheets
