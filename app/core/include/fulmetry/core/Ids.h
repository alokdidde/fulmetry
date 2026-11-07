// Stable string ids shared by the app, agents and the worker.
// Format: "obj:<slug>", "part:<object>/<slug>", "face:<part>#<n>", "cmt:<n>", ...
#pragma once
#include <compare>
#include <functional>
#include <string>

namespace fulmetry {

template <class Tag>
struct Id {
    std::string value;
    Id() = default;
    explicit Id(std::string v) : value(std::move(v)) {}
    bool empty() const { return value.empty(); }
    auto operator<=>(const Id&) const = default;
};

struct ObjectTag {};
struct PartTag {};
struct FaceTag {};
struct CommentTag {};
struct CheckTag {};
struct RevisionTag {};
struct AgentTag {};
struct JobTag {};

using ObjectId = Id<ObjectTag>;
using PartId = Id<PartTag>;
using FaceId = Id<FaceTag>;
using CommentId = Id<CommentTag>;
using CheckId = Id<CheckTag>;
using RevisionId = Id<RevisionTag>;
using AgentId = Id<AgentTag>;
using JobId = Id<JobTag>;

}  // namespace fulmetry

template <class Tag>
struct std::hash<fulmetry::Id<Tag>> {
    size_t operator()(const fulmetry::Id<Tag>& id) const noexcept { return std::hash<std::string>{}(id.value); }
};
