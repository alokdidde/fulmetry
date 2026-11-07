// WickedViewport: IViewport on Wicked Engine (Vulkan).
//
//  - Stage state (visibility, explode, camera, projection) is plain math, the same rules as FakeViewport.
//  - Every part has a MeshAsset in its own frame: the glTF from Part.meshFile, or a box over Part.bounds. Picking
//    is a ray cast against those triangles on the CPU, so it works before the first frame and without a GPU, and
//    it hits exactly what is drawn.
//  - Wicked draws: one mesh entity per part with glTF materials, a white ground that catches a soft key-light
//    shadow and ambient occlusion, depth outlines, MSAA, and selection / hover outlines from user stencil masks.
//    renderToImage runs Wicked's frame loop offscreen and reads the colour target back.
//
// Coordinates: Fulmetry is right-handed, Z up, millimetres. Wicked is left-handed, Y up. toWicked swaps
// y and z (a mirror, which is exactly the handedness change) and scales mm to Wicked units. Index order is kept:
// Wicked's front faces are counter-clockwise in its left-handed frame, which is what the swap makes of ours.
#include "fulmetry/viewport/WickedViewport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <thread>
#include <tuple>

#include "WickedEngine.h"
#include "wiGraphicsDevice_Vulkan.h"

namespace fulmetry::viewport {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kExplodeScale = 0.8;
constexpr float kUnitsPerMm = 0.01f;  // 1 Wicked unit = 100 mm; keeps the rover near 2 units across
constexpr float kFarPlane = 400.0f;    // Wicked units
constexpr float kKeyLightAngle = 0.08f;  // radians: angular radius of the key light (sets the penumbra width)

// Stage look (high-key). Lighting is in Wicked's units and was tuned by eye on lavapipe renders: the unshadowed
// ground must tonemap to pure white (Uchimura's shoulder plus a small brightness lift), so the key light dominates,
// the sky only lifts the shadows, and side faces settle at light greys (about 205-215 of 255).
// Key from high behind-left of the default view, so the shadow falls to the front-right; fill from the camera side.
constexpr XMFLOAT3 kKeyToLight{0.10f, 0.88f, -0.46f};  // Wicked space (x, up, depth), points towards the light
constexpr XMFLOAT3 kFillToLight{0.60f, 0.40f, 1.00f};
constexpr float kKeyIntensity = 6.0f, kFillIntensity = 1.1f;
constexpr float kSky = 0.26f, kAmbient = 0.05f;  // M1: 0.35 -> 0.26 for a ground shadow that reads at 1440 x 900
constexpr float kExposure = 1.0f, kBrightness = 0.012f;
constexpr float kShadowFilterRadius = 2.0f;  // PCF spread per frame; the frame average does the real softening
constexpr float kAORange = 0.3f, kAOPower = 1.6f;
constexpr float kXrayAlpha = 0.4f;
const std::array<float, 3> kSelectColor{232 / 255.f, 64 / 255.f, 47 / 255.f};
const std::array<float, 3> kDefaultPartColor{0.94f, 0.94f, 0.93f};
// Default camera direction (from target to eye, Fulmetry frame): front-left three-quarter view, from above.
const Vec3 kViewDir{0.62, 1.0, 0.72};

XMFLOAT3 toWicked(const Vec3& v) {
    return XMFLOAT3(float(v.x) * kUnitsPerMm, float(v.z) * kUnitsPerMm, float(v.y) * kUnitsPerMm);
}
XMFLOAT4 toWickedRotation(const Quat& q) {
    // R' = M R M with M the y/z swap: the axis is mirrored and, being a pseudo-vector, negated.
    return XMFLOAT4(float(-q.x), float(-q.z), float(-q.y), float(q.w));
}

std::array<float, 3> parseHex(const std::string& c, std::array<float, 3> fallback) {
    if (c.size() != 7 || c[0] != '#') return fallback;
    auto hex = [&](int i) { return std::stoi(c.substr(i, 2), nullptr, 16) / 255.0f; };
    return {hex(1), hex(3), hex(5)};
}

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

XMFLOAT4 quatFromTo(XMFLOAT3 from, XMFLOAT3 to) {
    XMVECTOR a = XMVector3Normalize(XMLoadFloat3(&from)), b = XMVector3Normalize(XMLoadFloat3(&to));
    float d = XMVectorGetX(XMVector3Dot(a, b));
    XMVECTOR axis = XMVector3Cross(a, b);
    if (XMVectorGetX(XMVector3Length(axis)) < 1e-6f) axis = XMVectorSet(1, 0, 0, 0);
    XMFLOAT4 q;
    XMStoreFloat4(&q, XMQuaternionRotationAxis(XMVector3Normalize(axis), std::acos(std::clamp(d, -1.0f, 1.0f))));
    return q;
}

// One graphics device and one engine initialisation per process. Wicked keeps global renderer state.
struct Engine {
    std::unique_ptr<wi::graphics::GraphicsDevice> device;
    std::string error;

    static Engine& get() {
        static Engine e;
        return e;
    }

private:
    Engine() {
        wi::backlog::SetLogLevel(wi::backlog::LogLevel::Warning);
        // Shader binaries compiled at runtime by libdxcompiler (next to the executable) are cached here.
        std::string cache = std::filesystem::temp_directory_path().string() + "/fulmetry-wicked-shaders/";
        if (const char* env = std::getenv("FULMETRY_WICKED_SHADER_CACHE")) cache = std::string(env) + "/";
        wi::renderer::SetShaderPath(cache);
        // Job threads also bound how many pipelines compile at once (memory-heavy on lavapipe).
        if (const char* env = std::getenv("FULMETRY_WICKED_THREADS")) wi::jobsystem::Initialize(uint32_t(std::atoi(env)));
        // nullptr window: no surface, no swapchain. Everything renders to our own textures.
        device = std::make_unique<wi::graphics::GraphicsDevice_Vulkan>(nullptr, wi::graphics::ValidationMode::Disabled);
        wi::graphics::GetDevice() = device.get();
        wi::initializer::InitializeComponentsImmediate();
        wi::renderer::SetShadowProps2D(2048);
        wi::renderer::SetScreenSpaceShadowsEnabled(true);  // contact shadows where parts meet each other and the ground
        // GPU occlusion culling decides from earlier frames' queries. A live stage renders on demand, so those
        // results are stale: after some camera, selection and hover sequences it culled the ground (the grey
        // sky showed instead). The stage is a few dozen objects; draw them all.
        wi::renderer::SetOcclusionCullingEnabled(false);
        // Object pipelines compile on low-priority background jobs and draws are skipped until they exist.
        // A one-shot render must wait for them; they are published to the renderer at the thread-safe point.
        while (wi::renderer::IsPipelineCreationActive() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wi::eventhandler::FireEvent(wi::eventhandler::EVENT_THREAD_SAFE_POINT, 0);
    }
};

struct PartEntity {
    wi::ecs::Entity object = wi::ecs::INVALID_ENTITY;
    std::vector<std::pair<wi::ecs::Entity, MeshSubset>> materials;
};

}  // namespace

struct WickedViewport::Impl {
    WickedOptions options;

    // Stage state, mirrors FakeViewport's rules.
    Project project;
    std::set<PartId> hidden;
    std::optional<PartId> isolated, selected, hovered;
    std::optional<ObjectId> object;
    double explode = 0;
    bool xray = false, joints = false;
    StageStyle style = StageStyle::HighKey;
    std::optional<double> slice;
    CameraState cam;
    int width = 1280, height = 800;
    double lastMs = 0;
    std::array<double, 4> framing{0, 0, 1, 1};  // resetView's rectangle (fractions of the view) and fill
    double framingFill = 0.7;

    // Geometry, loaded with the project (no GPU needed).
    std::map<PartId, std::shared_ptr<const MeshAsset>> meshes;
    int meshFiles = 0;
    std::vector<std::string> warnings;

    // Wicked side, created lazily on first render so stage logic works without a GPU.
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<wi::RenderPath3D> path;
    wi::scene::CameraComponent wcam;
    std::map<PartId, PartEntity> entities;
    wi::ecs::Entity ground = wi::ecs::INVALID_ENTITY, keyLight = wi::ecs::INVALID_ENTITY;
    wi::graphics::Texture target, accum, mask[2], maskMSAA;

    ObjectId objectOf(const PartId& part) const {
        for (const auto& o : project.objects)
            for (const auto& p : o.parts) if (p.id == part) return o.id;
        return {};
    }
    Vec3 objectOffset(const ObjectId& id) const {
        for (size_t i = 0; i < project.objects.size(); ++i)
            // All objects: side by side across the default 3/4 view (its screen-right axis), 230 mm apart.
            if (project.objects[i].id == id) return {double(i) * -195.0, double(i) * 121.0, 0};
        return {};
    }
    Vec3 objectCenter(const ObjectId& id) const {
        const Object* o = project.findObject(id);
        if (!o || o->parts.empty()) return {};
        Vec3 s;
        for (const auto& p : o->parts) s = s + p.placement.position;
        return objectOffset(id) + s * (1.0 / o->parts.size());
    }
    bool visible(const PartId& part) const {
        const Part* p = project.findPart(part);
        if (!p || hidden.count(part)) return false;
        if (isolated) return *isolated == part;
        if (object) return objectOf(part) == *object;
        return true;
    }
    Transform placement(const PartId& part) const {
        const Part* p = project.findPart(part);
        if (!p) return {};
        Transform t = p->placement;
        t.position = objectOffset(objectOf(part)) + t.position;
        if (explode > 0) {
            // Assembly-drawing explode: parts above the centre lift (stacked lids and heads separate the most),
            // parts to the side move out, and nothing moves down, so internals stay in the opened body.
            Vec3 c = objectCenter(objectOf(part));
            Vec3 d = t.position - c;
            d = {d.x * 1.2, d.y * 1.4, std::max(0.0, d.z) * 2.5};
            if (d.length() < 1e-6) d = {0, 0, 1};
            t.position = t.position + d * (explode * kExplodeScale);
        }
        return t;
    }
    const MeshAsset* mesh(const PartId& part) const {
        auto it = meshes.find(part);
        return it == meshes.end() ? nullptr : it->second.get();
    }
    // World-space bounds of a part as drawn.
    Aabb drawnBounds(const PartId& part) const {
        const MeshAsset* m = mesh(part);
        Transform t = placement(part);
        Aabb b = m ? m->bounds : Aabb{};
        Aabb out{{1e300, 1e300, 1e300}, {-1e300, -1e300, -1e300}};
        for (int i = 0; i < 8; ++i) {
            Vec3 c{(i & 1) ? b.max.x : b.min.x, (i & 2) ? b.max.y : b.min.y, (i & 4) ? b.max.z : b.min.z};
            Vec3 w = t.position + rotate(t.rotation, c);
            out.min = {std::min(out.min.x, w.x), std::min(out.min.y, w.y), std::min(out.min.z, w.z)};
            out.max = {std::max(out.max.x, w.x), std::max(out.max.y, w.y), std::max(out.max.z, w.z)};
        }
        return out;
    }
    std::optional<Aabb> visibleBounds() const {
        std::optional<Aabb> all;
        for (const Part* p : project.allParts()) {
            if (!visible(p->id) || !mesh(p->id)) continue;
            Aabb b = drawnBounds(p->id);
            if (!all) { all = b; continue; }
            all->min = {std::min(all->min.x, b.min.x), std::min(all->min.y, b.min.y), std::min(all->min.z, b.min.z)};
            all->max = {std::max(all->max.x, b.max.x), std::max(all->max.y, b.max.y), std::max(all->max.z, b.max.z)};
        }
        return all;
    }
    std::tuple<Vec3, Vec3, Vec3> basis() const {
        Vec3 f = cam.target - cam.eye;
        f = f * (1.0 / f.length());
        Vec3 up{0, 0, 1};
        Vec3 r{f.y * up.z - f.z * up.y, f.z * up.x - f.x * up.z, f.x * up.y - f.y * up.x};
        r = r * (1.0 / r.length());
        Vec3 u{r.y * f.z - r.z * f.y, r.z * f.x - r.x * f.z, r.x * f.y - r.y * f.x};
        return {f, r, u};
    }
    std::pair<Vec3, Vec3> ray(double px, double py) const {
        auto [f, r, u] = basis();
        double th = std::tan(cam.fovDegrees * kPi / 360.0);
        double aspect = double(width) / height;
        double nx = (px / width) * 2 - 1, ny = 1 - (py / height) * 2;
        Vec3 d = f + r * (nx * th * aspect) + u * (ny * th);
        return {cam.eye, d * (1.0 / d.length())};
    }
    // Frames the visible parts in the default 3/4 view: the corners of every visible part's drawn bounds are
    // centred on screen and the widest fills `margin` of the frame.
    void frameVisible() {
        // The rectangle's half-size in NDC scales the fill; its centre is where the subject lands.
        const double hw = std::clamp(framing[2] - framing[0], 0.05, 1.0), hh = std::clamp(framing[3] - framing[1], 0.05, 1.0);
        const double cx = framing[0] + framing[2] - 1, cy = 1 - (framing[1] + framing[3]);
        const double mx = framingFill * hw, my = framingFill * hh;
        auto b = visibleBounds();
        cam = CameraState{};
        cam.fovDegrees = 30;
        if (!b) return;
        std::vector<Vec3> pts;
        for (const Part* p : project.allParts()) {
            if (!visible(p->id) || !mesh(p->id)) continue;
            Aabb d = drawnBounds(p->id);
            for (int i = 0; i < 8; ++i)
                pts.push_back({(i & 1) ? d.max.x : d.min.x, (i & 2) ? d.max.y : d.min.y, (i & 4) ? d.max.z : d.min.z});
        }
        const Vec3 dir = kViewDir * (1.0 / kViewDir.length());
        const double th = std::tan(cam.fovDegrees * kPi / 360.0), aspect = double(width) / height;
        cam.target = b->center();
        double dist = 1;
        for (int pass = 0; pass < 3; ++pass) {
            cam.eye = cam.target + dir;
            auto [f, r, u] = basis();
            // Distance that fits every corner, then shift the target so the corners' screen extent is centred.
            dist = 1;
            for (const Vec3& c : pts) {
                Vec3 v = c - cam.target;
                double along = v.dot(dir);
                dist = std::max({dist, along + std::abs(v.dot(r)) / (mx * th * aspect),
                                 along + std::abs(v.dot(u)) / (my * th)});
            }
            double lo[2] = {1e9, 1e9}, hi[2] = {-1e9, -1e9};
            for (const Vec3& c : pts) {
                Vec3 v = c - (cam.target + dir * dist);
                double z = -v.dot(dir), sx = v.dot(r) / z, sy = v.dot(u) / z;
                lo[0] = std::min(lo[0], sx), hi[0] = std::max(hi[0], sx);
                lo[1] = std::min(lo[1], sy), hi[1] = std::max(hi[1], sy);
            }
            cam.target = cam.target + r * ((lo[0] + hi[0]) * 0.5 * dist) + u * ((lo[1] + hi[1]) * 0.5 * dist);
        }
        // Off-centre framing: move the camera sideways so the subject's centre lands on the rectangle's centre.
        cam.eye = cam.target + dir * dist;
        auto [fwd, right, up] = basis();
        (void)fwd;
        cam.target = cam.target - right * (cx * th * aspect * dist) - up * (cy * th * dist);
        cam.eye = cam.target + dir * dist;
    }

    void loadMeshes() {
        meshes.clear();
        warnings.clear();
        meshFiles = 0;
        for (const Part* p : project.allParts()) {
            if (!p->meshFile.empty()) {
                std::filesystem::path file(p->meshFile);
                if (file.is_relative() && !options.assetRoot.empty()) file = std::filesystem::path(options.assetRoot) / file;
                auto m = loadGltf(file.string(), p->bounds);
                if (m.ok()) {
                    meshes[p->id] = std::make_shared<const MeshAsset>(std::move(m.value()));
                    ++meshFiles;
                    continue;
                }
                warnings.push_back(p->id.value + ": " + m.error().message +
                                   (p->kind == PartKind::Hardware ? "; not drawn" : "; drawing its bounds"));
            }
            // A hardware part (screws, inserts) is spread over the assembly: its bounds box would cover the
            // whole object. Without its mesh it is not drawn (and so not pickable).
            if (p->kind == PartKind::Hardware) continue;
            meshes[p->id] = std::make_shared<const MeshAsset>(boxMesh(p->bounds));
        }
    }

    // ---- Wicked ------------------------------------------------------------------------------------------
    void buildScene() {
        scene = std::make_unique<wi::scene::Scene>();
        entities.clear();
        auto& s = *scene;

        // Sky: a white dome that also lights the parts from every side. It only shows at low camera angles,
        // and there the final compose replaces it with pure white.
        wi::ecs::Entity w = wi::ecs::CreateEntity();
        auto& weather = s.weathers.Create(w);
        weather.horizon = XMFLOAT3(kSky, kSky, kSky);
        weather.zenith = XMFLOAT3(kSky, kSky, kSky);
        weather.ambient = XMFLOAT3(kAmbient, kAmbient, kAmbient);
        weather.fogStart = 1e6f;

        auto light = [&](const char* name, XMFLOAT3 toLight, float intensity, bool shadow) -> wi::ecs::Entity {
            wi::ecs::Entity e = s.Entity_CreateLight(name, XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), intensity, 100.0f,
                                                     wi::scene::LightComponent::DIRECTIONAL);
            if (auto* t = s.transforms.GetComponent(e)) {
                t->ClearTransform();
                t->Rotate(quatFromTo(XMFLOAT3(0, 1, 0), toLight));
                t->UpdateTransform();
            }
            if (auto* l = s.lights.GetComponent(e)) {
                l->SetCastShadow(shadow && options.shadows);
                l->radius = kShadowFilterRadius;
                l->forced_shadow_resolution = 4096;
            }
            return e;
        };
        keyLight = light("key", kKeyToLight, kKeyIntensity, true);
        light("fill", kFillToLight, kFillIntensity, false);

        ground = s.Entity_CreatePlane("ground");
        if (auto* o = s.objects.GetComponent(ground)) o->SetCastShadow(false);
        if (auto* m = s.materials.GetComponent(ground)) {
            m->SetBaseColor(XMFLOAT4(1, 1, 1, 1));
            m->SetRoughness(1.0f);
            m->SetMetalness(0.0f);
            m->SetReflectance(0.0f);
        }

        for (const Part* p : project.allParts()) {
            if (!mesh(p->id)) continue;  // hardware without a mesh
            const MeshAsset& a = *mesh(p->id);
            PartEntity pe;
            pe.object = s.Entity_CreateObject(p->id.value);
            wi::ecs::Entity me = s.Entity_CreateMesh(p->id.value + "#mesh");
            auto& o = *s.objects.GetComponent(pe.object);
            o.meshID = me;
            o.SetCastShadow(true);
            auto& m = *s.meshes.GetComponent(me);
            m.vertex_positions.reserve(a.positions.size());
            for (size_t i = 0; i < a.positions.size(); ++i) {
                const auto& v = a.positions[i];
                const auto& n = a.normals[i];
                m.vertex_positions.push_back(XMFLOAT3(v[0] * kUnitsPerMm, v[2] * kUnitsPerMm, v[1] * kUnitsPerMm));
                m.vertex_normals.push_back(XMFLOAT3(n[0], n[2], n[1]));
            }
            m.indices.assign(a.indices.begin(), a.indices.end());
            for (const MeshSubset& sub : a.subsets) {
                wi::ecs::Entity mat = s.Entity_CreateMaterial(p->id.value + "#" + sub.material);
                auto& ws = m.subsets.emplace_back();
                ws.materialID = mat;
                ws.indexOffset = sub.indexOffset;
                ws.indexCount = sub.indexCount;
                pe.materials.push_back({mat, sub});
            }
            m.CreateRenderData();
            entities[p->id] = std::move(pe);
        }
    }

    void syncScene() {
        auto& s = *scene;
        for (const Part* p : project.allParts()) {
            auto ent = entities.find(p->id);
            if (ent == entities.end()) continue;
            const PartEntity& pe = ent->second;
            Transform tr = placement(p->id);
            if (auto* t = s.transforms.GetComponent(pe.object)) {
                t->ClearTransform();
                t->Rotate(toWickedRotation(tr.rotation));
                t->Translate(toWicked(tr.position));
                t->UpdateTransform();
            }
            bool isSel = selected && *selected == p->id, isHover = hovered && *hovered == p->id && !isSel;
            if (auto* o = s.objects.GetComponent(pe.object)) {
                o->SetRenderable(visible(p->id));
                o->SetUserStencilRef(isSel ? 1 : isHover ? 2 : 0);
            }
            const std::array<float, 3> partColor = parseHex(p->color, kDefaultPartColor);
            for (const auto& [mat, sub] : pe.materials) {
                auto* m = s.materials.GetComponent(mat);
                if (!m) continue;
                std::array<float, 4> c = sub.baseColor.value_or(std::array<float, 4>{
                    srgbToLinear(partColor[0]), srgbToLinear(partColor[1]), srgbToLinear(partColor[2]), 1.0f});
                // X-ray: everything see-through except the selected part, which stays solid inside the others.
                const bool clear = xray && !isSel;
                c[3] = clear ? kXrayAlpha : c[3];
                m->SetBaseColor(XMFLOAT4(c[0], c[1], c[2], c[3]));
                m->SetRoughness(sub.roughness);
                m->SetMetalness(sub.metallic);
                m->SetOutlineEnabled(options.outlines && !clear);
                m->SetCastShadow(!clear);
                m->userBlendMode = clear ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
            }
        }
        // The ground sits under the lowest visible part, wide enough to fill every view.
        double floorZ = 0;
        if (auto b = visibleBounds()) floorZ = b->min.z;
        // Two shadow cascades: a sharp one that just holds the stage, and a coarse one for the rest of the ground.
        if (auto* l = s.lights.GetComponent(keyLight))
            l->cascade_distances = {float((cam.eye - cam.target).length()) * kUnitsPerMm * 1.6f, kFarPlane};
        if (auto* t = s.transforms.GetComponent(ground)) {
            t->ClearTransform();
            t->Scale(XMFLOAT3(400, 1, 400));
            t->Translate(XMFLOAT3(0, float(floorZ) * kUnitsPerMm - 0.0005f, 0));
            t->UpdateTransform();
        }
    }

    void syncCamera(int w, int h) {
        auto [f, r, u] = basis();
        wcam.CreatePerspective(float(w), float(h), 0.05f, kFarPlane, float(cam.fovDegrees * kPi / 180.0));
        wcam.Eye = toWicked(cam.eye);
        wcam.At = XMFLOAT3(float(f.x), float(f.z), float(f.y));
        wcam.Up = XMFLOAT3(float(u.x), float(u.z), float(u.y));
        wcam.SetDirty();
        wcam.UpdateCamera();
    }

    Status ensurePath(int w, int h) {
        Engine& eng = Engine::get();
        if (!eng.error.empty()) return Error{"wicked_init", eng.error};
        auto* device = wi::graphics::GetDevice();
        if (!path) {
            path = std::make_unique<wi::RenderPath3D>();
            path->camera = &wcam;
            path->setMSAASampleCount(uint32_t(std::max(1, options.msaa)));
            path->setOutlineColor(XMFLOAT4(0.16f, 0.16f, 0.18f, 1));  // dark grey, not black: crisp without a cartoon line
            path->setOutlineThreshold(0.06f);
            path->setBloomEnabled(false);
            path->setEyeAdaptionEnabled(false);
            path->setExposure(kExposure);
            path->setBrightness(kBrightness);  // lifts the lit ground the last few levels to pure white
            path->setTonemap(wi::renderer::Tonemap::Uchimura);  // linear mid-tones, a soft shoulder to pure white
            path->setDitherEnabled(false);
            path->setShadowsEnabled(options.shadows);
            path->setReflectionsEnabled(false);
            path->setFXAAEnabled(options.msaa <= 1);
            if (options.ambientOcclusion) {
                // Plain SSAO with a short range. MSAO's large scales grey the far ground and ring the model with a
                // halo; HBAO runs at a lower resolution and leaves blocky steps along every shadow edge.
                path->setAO(wi::RenderPath3D::AO_SSAO);
                path->setAORange(kAORange);
                path->setAOPower(kAOPower);
            } else {
                path->setAO(wi::RenderPath3D::AO_DISABLED);
            }
        }
        path->scene = scene.get();
        if (path->width != uint32_t(w) || path->height != uint32_t(h)) {
            path->init(uint32_t(w), uint32_t(h), 96);
            path->setOutlineEnabled(options.outlines);  // allocates its target, so only once the size is known
            path->setOutlineThickness(std::max(1.0f, 1.0f * float(h) / 800.0f));
        }
        if (!target.IsValid() || target.desc.width != uint32_t(w) || target.desc.height != uint32_t(h)) {
            wi::graphics::TextureDesc desc;
            desc.width = uint32_t(w);
            desc.height = uint32_t(h);
            desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
            desc.bind_flags = wi::graphics::BindFlag::RENDER_TARGET | wi::graphics::BindFlag::SHADER_RESOURCE;
            desc.layout = wi::graphics::ResourceState::SHADER_RESOURCE;
            desc.clear.color[0] = desc.clear.color[1] = desc.clear.color[2] = desc.clear.color[3] = 1.0f;
            if (!device->CreateTexture(&desc, nullptr, &target)) return Error{"wicked_texture", "could not create render target"};
            desc.format = wi::graphics::Format::R16G16B16A16_FLOAT;  // running average of the soft-shadow frames
            if (!device->CreateTexture(&desc, nullptr, &accum)) return Error{"wicked_texture", "could not create accumulator"};
            desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
            // Selection and hover masks: 1 where the part's user stencil matches, then edge-detected.
            desc.format = wi::graphics::Format::R8_UNORM;
            desc.clear.color[0] = 0;
            for (auto& m : mask)
                if (!device->CreateTexture(&desc, nullptr, &m)) return Error{"wicked_texture", "could not create mask"};
            if (path->getMSAASampleCount() > 1) {
                desc.sample_count = path->getMSAASampleCount();
                desc.bind_flags = wi::graphics::BindFlag::RENDER_TARGET;
                desc.layout = wi::graphics::ResourceState::RENDERTARGET;
                if (!device->CreateTexture(&desc, nullptr, &maskMSAA)) return Error{"wicked_texture", "could not create mask"};
            }
        }
        return Ok{};
    }

    // Points the key light at sample `i` of `n` on a small disk around its direction (golden-angle spiral).
    // Averaging the frames turns the shadow-map edge into a wide, smooth penumbra: an area light.
    void jitterKeyLight(int i, int n) {
        XMVECTOR k = XMVector3Normalize(XMLoadFloat3(&kKeyToLight));
        XMVECTOR a = XMVector3Normalize(XMVector3Cross(k, XMVectorSet(0, 0, 1, 0)));
        XMVECTOR b = XMVector3Cross(k, a);
        const float r = n > 1 ? kKeyLightAngle * std::sqrt((i + 0.5f) / n) : 0.0f, th = 2.39996323f * float(i);
        XMFLOAT3 dir;
        XMStoreFloat3(&dir, XMVector3Normalize(k + a * (r * std::cos(th)) + b * (r * std::sin(th))));
        if (auto* t = scene->transforms.GetComponent(keyLight)) {
            t->ClearTransform();
            t->Rotate(quatFromTo(XMFLOAT3(0, 1, 0), dir));
            t->UpdateTransform();
        }
    }

    void drawMask(int i, uint8_t stencilRef, wi::graphics::CommandList cmd) {
        using namespace wi::graphics;
        auto* device = GetDevice();
        const Texture* ds = path->GetDepthStencil();
        std::vector<RenderPassImage> rp;
        if (path->getMSAASampleCount() > 1) {
            rp.push_back(RenderPassImage::RenderTarget(&maskMSAA, RenderPassImage::LoadOp::CLEAR, RenderPassImage::StoreOp::DONTCARE));
            rp.push_back(RenderPassImage::Resolve(&mask[i]));
        } else {
            rp.push_back(RenderPassImage::RenderTarget(&mask[i], RenderPassImage::LoadOp::CLEAR));
        }
        rp.push_back(RenderPassImage::DepthStencil(ds, RenderPassImage::LoadOp::LOAD, RenderPassImage::StoreOp::STORE,
                                                   ResourceState::DEPTHSTENCIL, ResourceState::DEPTHSTENCIL_READONLY,
                                                   ResourceState::DEPTHSTENCIL));
        device->RenderPassBegin(rp.data(), uint32_t(rp.size()), cmd);
        Viewport vp;
        vp.width = float(mask[i].desc.width);
        vp.height = float(mask[i].desc.height);
        device->BindViewports(1, &vp, cmd);
        wi::image::Params fx;
        fx.enableFullScreen();
        fx.stencilComp = wi::image::STENCILMODE_EQUAL;
        fx.stencilRef = stencilRef;
        fx.stencilRefMode = wi::image::STENCILREFMODE_USER;
        wi::image::Draw(nullptr, fx, cmd);  // white
        device->RenderPassEnd(cmd);
    }

    // Renders frame `i` of `n` and folds it into the running average; the last frame composes the final image.
    void frame(int i, int n) {
        using namespace wi::graphics;
        auto* device = GetDevice();
        jitterKeyLight(i, n);
        wi::eventhandler::FireEvent(wi::eventhandler::EVENT_THREAD_SAFE_POINT, 0);
        path->PreUpdate();
        path->Update(1.0f / 60.0f);
        path->PostUpdate();
        path->PreRender();
        path->Render();
        path->PostRender();

        CommandList cmd = device->BeginCommandList();
        wi::image::SetCanvas(*path);
        Viewport vp;
        vp.width = float(target.desc.width);
        vp.height = float(target.desc.height);
        const Texture* ds = path->GetDepthStencil();
        const bool msaa = path->getMSAASampleCount() > 1;
        {
            RenderPassImage rp[] = {
                RenderPassImage::RenderTarget(&accum, i == 0 ? RenderPassImage::LoadOp::CLEAR : RenderPassImage::LoadOp::LOAD),
                RenderPassImage::DepthStencil(ds, RenderPassImage::LoadOp::LOAD, RenderPassImage::StoreOp::STORE,
                                              ResourceState::DEPTHSTENCIL, ResourceState::DEPTHSTENCIL_READONLY,
                                              ResourceState::DEPTHSTENCIL),
            };
            // The 3D result is drawn only where geometry wrote stencil (the ground covers every view from above), so
            // a low camera that sees the sky still gets a pure white background. With MSAA the depth buffer is
            // multisampled and cannot back this single-sample target, so the test is skipped there.
            device->RenderPassBegin(rp, msaa ? 1 : 2, cmd);
            device->BindViewports(1, &vp, cmd);
            wi::image::Params fx;
            fx.enableFullScreen();
            fx.blendFlag = i == 0 ? wi::enums::BLENDMODE_OPAQUE : wi::enums::BLENDMODE_ALPHA;
            fx.opacity = 1.0f / float(i + 1);
            fx.quality = wi::image::QUALITY_NEAREST;
            if (!msaa) {
                fx.stencilComp = wi::image::STENCILMODE_NOT;  // stencil != 0: something was drawn here
                fx.stencilRef = wi::enums::STENCILREF_EMPTY;
                fx.stencilRefMode = wi::image::STENCILREFMODE_ENGINE;
            }
            wi::image::Draw(&path->GetRenderResult3D(), fx, cmd);
            device->RenderPassEnd(cmd);
        }
        if (i + 1 < n) {
            device->SubmitCommandLists();
            return;
        }

        const bool sel = selected && visible(*selected), hov = hovered && visible(*hovered) && hovered != selected;
        if (sel) drawMask(0, 1, cmd);
        if (hov) drawMask(1, 2, cmd);
        RenderPassImage rp[] = {RenderPassImage::RenderTarget(&target, RenderPassImage::LoadOp::CLEAR)};
        device->RenderPassBegin(rp, 1, cmd);
        device->BindViewports(1, &vp, cmd);
        wi::image::Params fx;
        fx.enableFullScreen();
        fx.blendFlag = wi::enums::BLENDMODE_OPAQUE;
        fx.quality = wi::image::QUALITY_NEAREST;
        wi::image::Draw(&accum, fx, cmd);
        wi::renderer::BindCommonResources(cmd);
        const float px = float(target.desc.height) / 800.0f;
        if (hov)
            wi::renderer::Postprocess_Outline(mask[1], cmd, 0.1f, 1.3f * px,
                                              XMFLOAT4(kSelectColor[0], kSelectColor[1], kSelectColor[2], 0.5f));
        if (sel) {
            // A white halo under the red line keeps it readable on red parts and on dark outlines.
            wi::renderer::Postprocess_Outline(mask[0], cmd, 0.1f, 3.4f * px, XMFLOAT4(1, 1, 1, 0.95f));
            wi::renderer::Postprocess_Outline(mask[0], cmd, 0.1f, 2.0f * px,
                                              XMFLOAT4(kSelectColor[0], kSelectColor[1], kSelectColor[2], 1.0f));
        }
        device->RenderPassEnd(cmd);
        device->SubmitCommandLists();
    }
};

WickedViewport::WickedViewport(WickedOptions options) : impl_(std::make_unique<Impl>()) { impl_->options = options; }
WickedViewport::~WickedViewport() {
    if (impl_ && impl_->path) wi::graphics::GetDevice()->WaitForGPU();
}

Status WickedViewport::loadProject(const Project& project) {
    impl_->project = project;
    impl_->hidden.clear();
    impl_->isolated.reset();
    impl_->object.reset();
    impl_->loadMeshes();
    impl_->scene.reset();  // rebuilt on next render
    resetView();
    return Ok{};
}
void WickedViewport::showObject(const std::optional<ObjectId>& object) { impl_->object = object; }
void WickedViewport::setSelected(const std::optional<PartId>& part) { impl_->selected = part; }
void WickedViewport::setHovered(const std::optional<PartId>& part) { impl_->hovered = part; }
void WickedViewport::setHidden(const PartId& part, bool hidden) {
    if (hidden) impl_->hidden.insert(part); else impl_->hidden.erase(part);
}
void WickedViewport::isolate(const std::optional<PartId>& part) { impl_->isolated = part; }
bool WickedViewport::isVisible(const PartId& part) const { return impl_->visible(part); }
void WickedViewport::setExplode(double amount) { impl_->explode = std::clamp(amount, 0.0, 1.0); }
void WickedViewport::setXray(bool on) { impl_->xray = on; }
void WickedViewport::setJointsVisible(bool on) { impl_->joints = on; }
void WickedViewport::setStyle(StageStyle style) { impl_->style = style; }
void WickedViewport::setSlicePlane(const std::optional<double>& h) { impl_->slice = h; }
Transform WickedViewport::drawnPlacement(const PartId& part) const { return impl_->placement(part); }
CameraState WickedViewport::camera() const { return impl_->cam; }
void WickedViewport::setCamera(const CameraState& c) { impl_->cam = c; }
void WickedViewport::flyTo(const PartId& part) {
    const Part* p = impl_->project.findPart(part);
    if (!p) return;
    Vec3 c = drawnPlacement(part).position + p->bounds.center();
    double r = std::max(40.0, p->bounds.size().length());
    impl_->cam.target = c;
    impl_->cam.eye = c + Vec3{1, -1, 0.8} * (r * 1.6);
}
void WickedViewport::resetView() { impl_->frameVisible(); }
void WickedViewport::resize(int w, int h) { impl_->width = std::max(1, w); impl_->height = std::max(1, h); }

std::optional<PickHit> WickedViewport::pick(int x, int y) const {
    const auto& m = *impl_;
    auto [o, d] = m.ray(x + 0.5, y + 0.5);
    std::optional<PickHit> best;
    double bestT = std::numeric_limits<double>::max();
    for (const Part* p : m.project.allParts()) {
        if (!m.visible(p->id)) continue;
        const MeshAsset* mesh = m.mesh(p->id);
        if (!mesh) continue;
        // Ray into the part's frame; the rotation keeps lengths, so t is in world mm.
        Transform t = m.placement(p->id);
        auto hit = intersect(*mesh, rotateInverse(t.rotation, o - t.position), rotateInverse(t.rotation, d));
        if (hit && hit->t < bestT) {
            bestT = hit->t;
            best = PickHit{p->id, std::nullopt, o + d * hit->t, rotate(t.rotation, hit->normal)};
        }
    }
    return best;
}

std::optional<std::pair<int, int>> WickedViewport::project(const Vec3& w) const {
    const auto& m = *impl_;
    auto [f, r, u] = m.basis();
    Vec3 v = w - m.cam.eye;
    double z = v.dot(f);
    if (z <= 1e-6) return std::nullopt;
    double th = std::tan(m.cam.fovDegrees * kPi / 360.0);
    double aspect = double(m.width) / m.height;
    double nx = v.dot(r) / (z * th * aspect), ny = v.dot(u) / (z * th);
    return std::pair<int, int>{int(std::floor((nx + 1) * 0.5 * m.width)), int(std::floor((1 - ny) * 0.5 * m.height))};
}

void WickedViewport::update(double) {}

Result<Image> WickedViewport::renderToImage(int w, int h) {
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return Error{"bad_size", "render size must be 1..8192 px"};
    auto& m = *impl_;
    auto t0 = std::chrono::steady_clock::now();
    Engine& eng = Engine::get();
    if (!eng.error.empty()) return Error{"wicked_init", eng.error};
    if (!m.scene) m.buildScene();
    if (auto st = m.ensurePath(w, h); !st.ok()) return st.error();
    int ow = m.width, oh = m.height;
    resize(w, h);
    m.syncScene();
    m.syncCamera(w, h);
    const int frames = std::max(1, m.options.frames);
    for (int i = 0; i < frames; ++i) m.frame(i, frames);

    wi::vector<uint8_t> data;
    if (!wi::helper::saveTextureToMemory(m.target, data)) {
        resize(ow, oh);
        return Error{"wicked_readback", "could not read the render target back"};
    }
    resize(ow, oh);
    Image img{w, h, std::vector<uint8_t>(size_t(w) * h * 4)};
    // saveTextureToMemory returns tightly packed rows for RGBA8.
    if (data.size() < img.rgba.size()) return Error{"wicked_readback", "short readback"};
    std::copy(data.begin(), data.begin() + img.rgba.size(), img.rgba.begin());
    for (size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;
    m.lastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return img;
}

void WickedViewport::setSoftShadowFrames(int frames) { impl_->options.frames = std::clamp(frames, 1, 64); }
int WickedViewport::softShadowFrames() const { return impl_->options.frames; }
void WickedViewport::setFramingRect(double left, double top, double right, double bottom, double fill) {
    impl_->framing = {std::clamp(left, 0.0, 1.0), std::clamp(top, 0.0, 1.0), std::clamp(right, 0.0, 1.0),
                      std::clamp(bottom, 0.0, 1.0)};
    impl_->framingFill = std::clamp(fill, 0.1, 1.0);
}

double WickedViewport::lastRenderMs() const { return impl_->lastMs; }
int WickedViewport::meshFileCount() const { return impl_->meshFiles; }
const std::vector<std::string>& WickedViewport::meshWarnings() const { return impl_->warnings; }

Status writePng(const Image& image, const std::string& path) {
    wi::graphics::TextureDesc desc;
    desc.width = uint32_t(image.width);
    desc.height = uint32_t(image.height);
    desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
    wi::vector<uint8_t> data(image.rgba.begin(), image.rgba.end());
    if (!wi::helper::saveTextureToFile(data, desc, path)) return Error{"png_write", "could not write " + path};
    return Ok{};
}

}  // namespace fulmetry::viewport
