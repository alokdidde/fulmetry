#include "fulmetry/project/ProjectIO.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace fulmetry {
namespace {

using json = nlohmann::ordered_json;

Vec3 vec3(const json& j) { return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>()}; }
json toJson(const Vec3& v) { return json::array({v.x, v.y, v.z}); }

template <class T>
T enumOr(std::optional<T> v, const std::string& what, const std::string& s) {
    if (!v) throw std::runtime_error("unknown " + what + ": " + s);
    return *v;
}

Part parsePart(const json& j) {
    Part p;
    p.id = PartId(j.at("id").get<std::string>());
    p.name = j.at("name").get<std::string>();
    auto kind = j.at("kind").get<std::string>();
    p.kind = enumOr(partKindFromString(kind), "part kind", kind);
    p.group = j.value("group", "");
    p.material = j.value("material", "");
    p.color = j.value("color", "");
    const auto& pl = j.at("placement");
    p.placement.position = vec3(pl.at("position"));
    const auto& q = pl.at("rotation");
    p.placement.rotation = {q.at(0).get<double>(), q.at(1).get<double>(), q.at(2).get<double>(), q.at(3).get<double>()};
    p.bounds = {vec3(j.at("bounds").at("min")), vec3(j.at("bounds").at("max"))};
    p.stepFile = j.value("stepFile", "");
    p.meshFile = j.value("meshFile", "");
    p.costUsd = j.value("costUsd", 0.0);
    p.massG = j.value("massG", 0.0);
    if (j.contains("joint")) {
        const auto& jj = j.at("joint");
        Joint jt;
        jt.name = jj.at("name").get<std::string>();
        jt.type = jj.at("type").get<std::string>();
        jt.minDeg = jj.value("minDeg", 0.0);
        jt.maxDeg = jj.value("maxDeg", 0.0);
        jt.axis = vec3(jj.at("axis"));
        jt.origin = vec3(jj.at("origin"));
        p.joint = jt;
    }
    return p;
}

json partJson(const Part& p) {
    json j;
    j["id"] = p.id.value;
    j["name"] = p.name;
    j["kind"] = toName(p.kind);
    if (!p.group.empty()) j["group"] = p.group;
    if (!p.material.empty()) j["material"] = p.material;
    if (!p.color.empty()) j["color"] = p.color;
    const auto& r = p.placement.rotation;
    j["placement"] = {{"position", toJson(p.placement.position)}, {"rotation", json::array({r.w, r.x, r.y, r.z})}};
    j["bounds"] = {{"min", toJson(p.bounds.min)}, {"max", toJson(p.bounds.max)}};
    if (!p.stepFile.empty()) j["stepFile"] = p.stepFile;
    if (!p.meshFile.empty()) j["meshFile"] = p.meshFile;
    j["costUsd"] = p.costUsd;
    j["massG"] = p.massG;
    if (p.joint) {
        const auto& jt = *p.joint;
        json jj = {{"name", jt.name}, {"type", jt.type}};
        if (jt.minDeg != 0 || jt.maxDeg != 0) { jj["minDeg"] = jt.minDeg; jj["maxDeg"] = jt.maxDeg; }
        jj["axis"] = toJson(jt.axis);
        jj["origin"] = toJson(jt.origin);
        j["joint"] = jj;
    }
    return j;
}

Project parse(const json& j) {
    Project pr;
    pr.schemaVersion = j.at("schemaVersion").get<int>();
    if (pr.schemaVersion != 1) throw std::runtime_error("unsupported schemaVersion");
    pr.name = j.at("name").get<std::string>();
    if (j.contains("revision")) {
        const auto& r = j.at("revision");
        pr.revision.id = RevisionId(r.value("id", ""));
        pr.revision.number = r.at("number").get<int>();
        pr.revision.summary = r.value("summary", "");
    }
    for (const auto& o : j.at("objects")) {
        Object ob{ObjectId(o.at("id").get<std::string>()), o.at("name").get<std::string>(), {}};
        for (const auto& p : o.at("parts")) ob.parts.push_back(parsePart(p));
        pr.objects.push_back(std::move(ob));
    }
    for (const auto& l : j.value("links", json::array()))
        pr.links.push_back({ObjectId(l.at("from").get<std::string>()), ObjectId(l.at("to").get<std::string>()),
                            l.at("label").get<std::string>()});
    for (const auto& c : j.value("checks", json::array())) {
        Check ch;
        ch.id = CheckId(c.at("id").get<std::string>());
        ch.object = ObjectId(c.at("object").get<std::string>());
        ch.name = c.at("name").get<std::string>();
        auto v = c.at("verdict").get<std::string>();
        ch.verdict = enumOr(verdictFromString(v), "verdict", v);
        ch.reason = c.value("reason", "");
        for (const auto& in : c.value("inputs", json::array())) ch.inputs.emplace_back(in.get<std::string>());
        if (c.contains("inputHashes"))
            for (const auto& [k, h] : c.at("inputHashes").items()) ch.inputHashes[k] = h.get<std::string>();
        pr.checks.push_back(std::move(ch));
    }
    for (const auto& c : j.value("comments", json::array())) {
        Comment cm;
        cm.id = CommentId(c.at("id").get<std::string>());
        cm.text = c.at("text").get<std::string>();
        auto s = c.at("status").get<std::string>();
        cm.status = enumOr(commentStatusFromString(s), "comment status", s);
        cm.agent = AgentId(c.value("agent", ""));
        cm.reply = c.value("reply", "");
        const auto& a = c.at("anchor");
        cm.anchor.part = PartId(a.at("part").get<std::string>());
        if (a.contains("face")) cm.anchor.face = FaceId(a.at("face").get<std::string>());
        if (a.contains("point")) cm.anchor.point = vec3(a.at("point"));
        if (a.contains("camera")) {
            CameraState cs;
            const auto& cj = a.at("camera");
            if (cj.contains("eye")) cs.eye = vec3(cj.at("eye"));
            if (cj.contains("target")) cs.target = vec3(cj.at("target"));
            cs.fovDegrees = cj.value("fovDegrees", cs.fovDegrees);
            cm.anchor.camera = cs;
        }
        cm.anchor.sheetRef = a.value("sheetRef", "");
        cm.anchor.sheetPin = a.value("sheetPin", "");
        cm.anchor.sheetNet = a.value("sheetNet", "");
        pr.comments.push_back(std::move(cm));
    }
    return pr;
}

}  // namespace

Result<Project> parseProject(const std::string& text) {
    try {
        return parse(json::parse(text));
    } catch (const std::exception& e) {
        return Error{"invalid_manifest", e.what()};
    }
}

Result<Project> loadProject(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Error{"not_found", "cannot open " + path};
    std::stringstream ss;
    ss << in.rdbuf();
    return parseProject(ss.str());
}

std::string serializeProject(const Project& pr) {
    json j;
    j["schemaVersion"] = pr.schemaVersion;
    j["name"] = pr.name;
    j["revision"] = {{"id", pr.revision.id.value}, {"number", pr.revision.number}, {"summary", pr.revision.summary}};
    j["objects"] = json::array();
    for (const auto& o : pr.objects) {
        json parts = json::array();
        for (const auto& p : o.parts) parts.push_back(partJson(p));
        j["objects"].push_back({{"id", o.id.value}, {"name", o.name}, {"parts", parts}});
    }
    j["links"] = json::array();
    for (const auto& l : pr.links) j["links"].push_back({{"from", l.from.value}, {"to", l.to.value}, {"label", l.label}});
    j["checks"] = json::array();
    for (const auto& c : pr.checks) {
        json cj = {{"id", c.id.value}, {"object", c.object.value}, {"name", c.name}, {"verdict", toName(c.verdict)},
                   {"reason", c.reason}};
        json ins = json::array();
        for (const auto& i : c.inputs) ins.push_back(i.value);
        cj["inputs"] = ins;
        if (!c.inputHashes.empty()) cj["inputHashes"] = c.inputHashes;
        j["checks"].push_back(cj);
    }
    j["comments"] = json::array();
    for (const auto& c : pr.comments) {
        json a = {{"part", c.anchor.part.value}};
        if (c.anchor.face) a["face"] = c.anchor.face->value;
        if (c.anchor.point) a["point"] = toJson(*c.anchor.point);
        if (c.anchor.camera)
            a["camera"] = {{"eye", toJson(c.anchor.camera->eye)}, {"target", toJson(c.anchor.camera->target)},
                           {"fovDegrees", c.anchor.camera->fovDegrees}};
        if (!c.anchor.sheetRef.empty()) a["sheetRef"] = c.anchor.sheetRef;
        if (!c.anchor.sheetPin.empty()) a["sheetPin"] = c.anchor.sheetPin;
        if (!c.anchor.sheetNet.empty()) a["sheetNet"] = c.anchor.sheetNet;
        json cj = {{"id", c.id.value}, {"anchor", a}, {"text", c.text}, {"status", toName(c.status)}};
        if (!c.agent.empty()) cj["agent"] = c.agent.value;
        if (!c.reply.empty()) cj["reply"] = c.reply;
        j["comments"].push_back(cj);
    }
    return j.dump(2) + "\n";
}

Status saveProject(const Project& project, const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Error{"io", "cannot write " + path};
    out << serializeProject(project);
    return Ok{};
}

}  // namespace fulmetry
