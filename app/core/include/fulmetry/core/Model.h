// Plain project data. Mirrors contracts/schemas/project.schema.json.
// Owned by the integrator; S1 (app/project) loads and saves it.
#pragma once
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "fulmetry/core/Ids.h"
#include "fulmetry/core/Math.h"

namespace fulmetry {

// The kind decides which tools a part shows (UI spec, "Parts, kinds and tailored tools").
enum class PartKind { Board, Printed, Actuator, Battery, Control, Hardware, Bought };


struct Joint {
    std::string name;      // "Head pan"
    std::string type;      // "revolute" | "prismatic" | "continuous" | "fixed"
    double minDeg = 0, maxDeg = 0;
    Vec3 axis{0, 0, 1};
    Vec3 origin;           // mm, in the object's frame
};

struct Part {
    PartId id;
    std::string name;
    PartKind kind = PartKind::Printed;
    std::string group;          // parts list group: Printed, Electronics, Bought, Fasteners
    std::string material;       // "PLA", "PETG", "ASA", "TPU 95A", or empty
    std::string color;          // "#rrggbb", high-key palette
    Transform placement;        // assembled pose in the object's frame
    Aabb bounds;                // mm, local frame; used by fakes and as a sanity check
    std::string stepFile;       // relative to the project root, may be empty
    std::string meshFile;       // glTF, relative to the project root, may be empty
    // Hash of the part's CAD source as last built ("sha256:<hex>" from the worker's cad.build), or empty when
    // never built. Checks compare it with Check::inputHashes to know whether they are stale.
    std::string sourceHash;
    std::optional<Joint> joint; // set when the part moves relative to its parent
    double costUsd = 0;
    double massG = 0;
};

struct Object {
    ObjectId id;
    std::string name;
    std::vector<Part> parts;
};

struct Link {
    ObjectId from, to;
    std::string label;  // "ESP-NOW 2.4 GHz"
};

enum class Verdict { Supported, Refuted, Unresolved };

struct Check {
    CheckId id;
    ObjectId object;
    std::string name;
    Verdict verdict = Verdict::Unresolved;
    std::string reason;
    std::vector<PartId> inputs;                    // parts whose change invalidates this check
    std::map<std::string, std::string> inputHashes; // part id -> source hash at last run
};

// Where a comment points: a spot on a part in 3D, or a ref/pin/net on a sheet.
struct Anchor {
    PartId part;
    std::optional<FaceId> face;
    std::optional<Vec3> point;      // mm, world
    std::optional<CameraState> camera;
    std::string sheetRef;           // "U1", "R3"
    std::string sheetPin;           // "GPIO4"
    std::string sheetNet;           // "VBAT"
};

enum class CommentStatus { Open, Working, Resolved };

struct Comment {
    CommentId id;
    Anchor anchor;
    std::string text;
    CommentStatus status = CommentStatus::Open;
    AgentId agent;          // set once an agent picks it up
    std::string reply;      // agent's final reply
};

struct Revision {
    RevisionId id;   // git commit sha
    int number = 0;  // r1, r2, ...
    std::string summary;
};

struct Project {
    int schemaVersion = 1;
    std::string name;
    std::vector<Object> objects;
    std::vector<Link> links;
    std::vector<Check> checks;
    std::vector<Comment> comments;
    Revision revision;

    const Object* findObject(const ObjectId& id) const {
        for (const auto& o : objects) if (o.id == id) return &o;
        return nullptr;
    }
    const Part* findPart(const PartId& id) const {
        for (const auto& o : objects) for (const auto& p : o.parts) if (p.id == id) return &p;
        return nullptr;
    }
    Part* findPart(const PartId& id) { return const_cast<Part*>(std::as_const(*this).findPart(id)); }
    std::vector<const Part*> allParts() const {
        std::vector<const Part*> out;
        for (const auto& o : objects) for (const auto& p : o.parts) out.push_back(&p);
        return out;
    }
};

// ---- string forms used in the manifest -------------------------------------------------
namespace detail {
template <class E, size_t N>
inline std::optional<E> lookup(const std::string& s, const std::pair<E, const char*> (&table)[N]) {
    for (const auto& [e, n] : table) if (s == n) return e;
    return std::nullopt;
}
template <class E, size_t N>
inline const char* name(E e, const std::pair<E, const char*> (&table)[N]) {
    for (const auto& [v, n] : table) if (v == e) return n;
    return "";
}
inline constexpr std::pair<PartKind, const char*> kPartKinds[] = {
    {PartKind::Board, "board"}, {PartKind::Printed, "printed"}, {PartKind::Actuator, "actuator"},
    {PartKind::Battery, "battery"}, {PartKind::Control, "control"}, {PartKind::Hardware, "hardware"},
    {PartKind::Bought, "bought"}};
inline constexpr std::pair<Verdict, const char*> kVerdicts[] = {
    {Verdict::Supported, "supported"}, {Verdict::Refuted, "refuted"}, {Verdict::Unresolved, "unresolved"}};
inline constexpr std::pair<CommentStatus, const char*> kCommentStatuses[] = {
    {CommentStatus::Open, "open"}, {CommentStatus::Working, "working"}, {CommentStatus::Resolved, "resolved"}};
}  // namespace detail

inline const char* toName(PartKind k) { return detail::name(k, detail::kPartKinds); }
inline std::optional<PartKind> partKindFromString(const std::string& s) { return detail::lookup(s, detail::kPartKinds); }
inline const char* toName(Verdict v) { return detail::name(v, detail::kVerdicts); }
inline std::optional<Verdict> verdictFromString(const std::string& s) { return detail::lookup(s, detail::kVerdicts); }
inline const char* toName(CommentStatus c) { return detail::name(c, detail::kCommentStatuses); }
inline std::optional<CommentStatus> commentStatusFromString(const std::string& s) { return detail::lookup(s, detail::kCommentStatuses); }

}  // namespace fulmetry
