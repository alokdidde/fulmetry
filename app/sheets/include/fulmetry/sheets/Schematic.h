// KiCad 8 schematic (.kicad_sch) model and parser. Owner: S6 Sheets.
//
// Coordinates are in mm, schematic frame (y grows downwards), exactly as written in the file.
// Library symbol coordinates are in the symbol frame (y grows upwards); placedPins() applies the
// symbol transform (position, rotation, mirror) to give each pin's connection point on the sheet.
#pragma once
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "fulmetry/core/Result.h"

namespace fulmetry::sheets {

struct Point {
    double x = 0, y = 0;
    bool operator==(const Point&) const = default;
};

enum class PinType {
    Input, Output, Bidirectional, TriState, Passive, Free, Unspecified,
    PowerIn, PowerOut, OpenCollector, OpenEmitter, NoConnect
};
const char* toName(PinType t);  // KiCad token, e.g. "power_in"

struct LibPin {
    std::string number;  // "1", "A3"
    std::string name;    // "GPIO4", "~" when unnamed
    PinType type = PinType::Passive;
    Point at;            // connection point, symbol frame
    double angleDeg = 0; // direction from the connection point into the body
    double length = 0;
    bool hidden = false;
    int unit = 0;        // 0 = common to all units
    int bodyStyle = 0;   // 0 = common to all body styles (KiCad "convert")
};

struct LibSymbol {
    std::string name;  // "Device:R" (the lib_id, or lib_name when a placed symbol overrides it)
    bool power = false;
    std::map<std::string, std::string> properties;
    std::vector<LibPin> pins;
};

struct PlacedSymbol {
    std::string libId;    // "Device:R"
    std::string libName;  // optional override naming the lib_symbols entry
    std::string reference, value, footprint;
    std::map<std::string, std::string> properties;
    Point at;
    double rotationDeg = 0;  // counter-clockwise on screen
    bool mirrorX = false;    // mirrored about the horizontal axis (flips y)
    bool mirrorY = false;    // mirrored about the vertical axis (flips x)
    int unit = 1;
    int bodyStyle = 1;
    std::string uuid;
};

struct Wire {
    Point a, b;
};

struct Junction {
    Point at;
};

enum class LabelKind { Local, Global, Hierarchical };

struct Label {
    LabelKind kind = LabelKind::Local;
    std::string text;
    Point at;
    double angleDeg = 0;
};

struct NoConnect {
    Point at;
};

struct Schematic {
    long version = 0;  // file format date, e.g. 20231120 for KiCad 8
    std::string generator, generatorVersion, uuid, paper, title;
    std::vector<LibSymbol> libSymbols;
    std::vector<PlacedSymbol> symbols;
    std::vector<Wire> wires;  // one entry per straight segment
    std::vector<Junction> junctions;
    std::vector<Label> labels;
    std::vector<NoConnect> noConnects;

    const LibSymbol* libSymbolFor(const PlacedSymbol& s) const;
    const PlacedSymbol* findSymbol(std::string_view reference) const;
    // A power symbol (#PWR, #FLG): its lib symbol is flagged (power).
    bool isPower(const PlacedSymbol& s) const;
};

// Parses .kicad_sch text. Malformed input returns an Error ("parse_error", "invalid_schematic", ...).
Result<Schematic> parseSchematic(std::string_view text);
// Reads and parses a file; "not_found" when it cannot be read.
Result<Schematic> loadSchematic(const std::string& path);

// Maps a point in a symbol's library frame to the sheet.
Point toSheet(const PlacedSymbol& s, Point lib);

struct PlacedPin {
    const PlacedSymbol* symbol = nullptr;
    const LibPin* pin = nullptr;
    Point at;  // connection point on the sheet
};
// Pins of the symbol's unit and body style, transformed to the sheet. Empty if its lib symbol is missing.
std::vector<PlacedPin> placedPins(const Schematic& sch, const PlacedSymbol& s);

}  // namespace fulmetry::sheets
