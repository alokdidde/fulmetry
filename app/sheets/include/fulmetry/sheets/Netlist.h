// Connectivity: turns a parsed schematic into named nets. Owner: S6 Sheets.
//
// Rules (single sheet, matching KiCad where it matters):
// - Items connect when their connection points coincide (0.1 um grid).
// - A pin, label, junction or wire end lying on a wire segment connects to that wire.
//   Wires that merely cross do not connect.
// - Labels (local, global, hierarchical) and power symbols with the same text join their nets.
// - A power symbol names its net only through a power_in pin, so PWR_FLAG does not rename it.
// - Name priority: power symbol > global label > hierarchical label > local label, ties broken
//   alphabetically. Label names are plain ("AIN1"); KiCad's netlist prefixes local ones with the
//   sheet path ("/AIN1").
// - Unlabelled nets get KiCad's default names: "Net-(<ref>-<pin name>)", preferring pins that have
//   a real name, then alphabetical; "Net-(<ref>-Pad<number>)" when no pin is named. A lone pin is
//   "unconnected-(<ref>-<pin name>-Pad<number>)".
// - Power symbols are not nodes: nets list only real component pins.
// Not yet handled: hierarchical sheets, buses, implicit hidden power_in pins on non-power parts.
#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "fulmetry/sheets/Schematic.h"

namespace fulmetry::sheets {

struct NetNode {
    std::string ref;      // "U1"
    std::string pin;      // pin number, "24"
    std::string pinName;  // "IO2"
    PinType type = PinType::Passive;
    Point at;
};

struct Net {
    std::string name;
    bool autoNamed = false;           // no label or power symbol names it
    std::vector<NetNode> nodes;       // sorted by ref, then pin
    std::vector<std::string> labels;  // every label and power name on the net, sorted, unique
    bool noConnect = false;           // a no-connect flag sits on the net

    bool has(std::string_view ref, std::string_view pin) const;
};

// Every net that has at least one component pin, sorted by name.
std::vector<Net> buildNets(const Schematic& sch);

// Nets plus lookups from a pin or a name.
class Netlist {
public:
    explicit Netlist(std::vector<Net> nets);
    static Netlist build(const Schematic& sch) { return Netlist(buildNets(sch)); }

    const std::vector<Net>& nets() const { return nets_; }
    // The net on (ref, pin); pin may be a pin number ("24") or a pin name ("IO2"). nullptr if none.
    const Net* netOf(std::string_view ref, std::string_view pin) const;
    const Net* find(std::string_view name) const;

private:
    std::vector<Net> nets_;
    std::unordered_map<std::string, std::size_t> byPin_;   // ref '\n' number -> net index
    std::unordered_map<std::string, std::size_t> byName_;  // ref '\n' name   -> net index
};

// Natural order for refs and pin numbers: "R2" < "R10".
bool naturalLess(std::string_view a, std::string_view b);

}  // namespace fulmetry::sheets
