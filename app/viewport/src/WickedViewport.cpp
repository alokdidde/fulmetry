// WickedViewport: IViewport on Wicked Engine (Vulkan).
//
// Split of work in this first cut:
//  - Stage state (visibility, explode, camera, picking, projection) is plain math on Part::bounds, the same
//    rules as FakeViewport, so the conformance suite holds while parts are still boxes. Picking moves to
//    wi::scene::Pick on real meshes once S3 supplies them.
//  - Wicked owns drawing: one box entity per part, a high-key sky and lights, and the depth outline pass for
//    dark edges. renderToImage runs Wicked's frame loop offscreen and reads the colour target back.
//
// Coordinates: Fulmetry is right-handed, Z up, millimetres. Wicked is left-handed, Y up. toWicked swaps
// y and z (a mirror, which is exactly the handedness change) and scales mm to Wicked units.
#include "fulmetry/viewport/WickedViewport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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

XMFLOAT3 toWicked(const Vec3& v) {
    return XMFLOAT3(float(v.x) * kUnitsPerMm, float(v.z) * kUnitsPerMm, float(v.y) * kUnitsPerMm);
}

std::array<float, 3> parseHex(const std::string& c, std::array<float, 3> fallback) {
    if (c.size() != 7 || c[0] != '#') return fallback;
    auto hex = [&](int i) { return std::stoi(c.substr(i, 2), nullptr, 16) / 255.0f; };
    return {hex(1), hex(3), hex(5)};
}

float srgbToLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

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
        // Object pipelines compile on low-priority background jobs and draws are skipped until they exist.
        // A one-shot render must wait for them; they are published to the renderer at the thread-safe point.
        while (wi::renderer::IsPipelineCreationActive() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        wi::eventhandler::FireEvent(wi::eventhandler::EVENT_THREAD_SAFE_POINT, 0);
    }
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

    // Wicked side, created lazily on first render so stage logic works without a GPU.
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<wi::RenderPath3D> path;
    wi::scene::CameraComponent wcam;
    std::map<PartId, wi::ecs::Entity> entities;
    wi::graphics::Texture target;
    bool sceneDirty = true;

    ObjectId objectOf(const PartId& part) const {
        for (const auto& o : project.objects)
            for (const auto& p : o.parts) if (p.id == part) return o.id;
        return {};
    }
    Vec3 objectOffset(const ObjectId& id) const {
        for (size_t i = 0; i < project.objects.size(); ++i)
            if (project.objects[i].id == id) return {double(i) * 250.0, 0, 0};
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
            Vec3 c = objectCenter(objectOf(part));
            Vec3 d = t.position - c;
            if (d.length() < 1e-6) d = {0, 0, 1};
            t.position = t.position + d * (explode * kExplodeScale);
        }
        return t;
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
    static bool hit(const Aabb& b, const Vec3& o, const Vec3& d, double& tOut, Vec3& nOut) {
        double tmin = -1e300, tmax = 1e300;
        Vec3 n;
        const double os[3] = {o.x, o.y, o.z}, ds[3] = {d.x, d.y, d.z};
        const double mn[3] = {b.min.x, b.min.y, b.min.z}, mx[3] = {b.max.x, b.max.y, b.max.z};
        for (int i = 0; i < 3; ++i) {
            if (std::abs(ds[i]) < 1e-12) {
                if (os[i] < mn[i] || os[i] > mx[i]) return false;
                continue;
            }
            double t1 = (mn[i] - os[i]) / ds[i], t2 = (mx[i] - os[i]) / ds[i];
            double sign = -1;
            if (t1 > t2) { std::swap(t1, t2); sign = 1; }
            if (t1 > tmin) {
                tmin = t1;
                n = {0, 0, 0};
                (i == 0 ? n.x : i == 1 ? n.y : n.z) = sign;
            }
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
        if (tmax < 0) return false;
        tOut = tmin > 0 ? tmin : tmax;
        nOut = n;
        return true;
    }

    // ---- Wicked ------------------------------------------------------------------------------------------
    void buildScene() {
        scene = std::make_unique<wi::scene::Scene>();
        entities.clear();
        auto& s = *scene;

        // High-key stage: a white sky that tonemaps to pure white, strong ambient, one soft key light.
        wi::ecs::Entity w = wi::ecs::CreateEntity();
        auto& weather = s.weathers.Create(w);
        weather.horizon = XMFLOAT3(1.0f, 1.0f, 1.0f);
        weather.zenith = XMFLOAT3(1.0f, 1.0f, 1.0f);
        weather.ambient = XMFLOAT3(0.25f, 0.25f, 0.25f);
        weather.fogStart = 1e6f;

        wi::ecs::Entity sun = s.Entity_CreateLight("key", XMFLOAT3(0, 0, 0), XMFLOAT3(1, 1, 1), 3.0f, 100.0f,
                                                   wi::scene::LightComponent::DIRECTIONAL);
        if (auto* t = s.transforms.GetComponent(sun)) {
            t->RotateRollPitchYaw(XMFLOAT3(XM_PIDIV4 * 0.6f, 0, XM_PIDIV4 * 0.8f));
            t->UpdateTransform();
        }
        if (auto* l = s.lights.GetComponent(sun)) l->SetCastShadow(false);

        for (const Part* p : project.allParts()) {
            wi::ecs::Entity e = s.Entity_CreateCube(p->id.value);
            if (auto* o = s.objects.GetComponent(e)) o->SetCastShadow(false);
            if (auto* m = s.materials.GetComponent(e)) {
                m->SetRoughness(0.85f);
                m->SetMetalness(0.0f);
                m->SetOutlineEnabled(options.outlines);  // writes the outline stencil the edge pass reads
            }
            entities[p->id] = e;
        }
        sceneDirty = true;
    }

    void syncScene() {
        auto& s = *scene;
        for (const Part* p : project.allParts()) {
            wi::ecs::Entity e = entities[p->id];
            Vec3 pos = placement(p->id).position + p->bounds.center();
            Vec3 half = p->bounds.size() * 0.5;
            if (auto* t = s.transforms.GetComponent(e)) {
                t->ClearTransform();
                // Unit cube spans -1..1, so scale by half extents (axes swapped like positions).
                t->Scale(XMFLOAT3(float(half.x) * kUnitsPerMm, float(half.z) * kUnitsPerMm, float(half.y) * kUnitsPerMm));
                t->Translate(toWicked(pos));
                t->UpdateTransform();
            }
            if (auto* o = s.objects.GetComponent(e)) o->SetRenderable(visible(p->id));
            if (auto* m = s.materials.GetComponent(e)) {
                std::array<float, 3> c = parseHex(p->color, {0.94f, 0.94f, 0.93f});
                if (selected && *selected == p->id) c = {232 / 255.f, 64 / 255.f, 47 / 255.f};
                float a = xray ? 0.25f : 1.0f;
                m->SetBaseColor(XMFLOAT4(srgbToLinear(c[0]), srgbToLinear(c[1]), srgbToLinear(c[2]), a));
                m->userBlendMode = xray ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
            }
        }
        sceneDirty = false;
    }

    void syncCamera(int w, int h) {
        auto [f, r, u] = basis();
        Vec3 up = u;
        wcam.CreatePerspective(float(w), float(h), 0.05f, 200.0f, float(cam.fovDegrees * kPi / 180.0));
        wcam.Eye = toWicked(cam.eye);
        XMFLOAT3 at = XMFLOAT3(float(f.x), float(f.z), float(f.y));
        XMFLOAT3 wu = XMFLOAT3(float(up.x), float(up.z), float(up.y));
        wcam.At = at;
        wcam.Up = wu;
        wcam.SetDirty();
        wcam.UpdateCamera();
    }

    Status ensurePath(int w, int h) {
        Engine& eng = Engine::get();
        if (!eng.error.empty()) return Error{"wicked_init", eng.error};
        auto* device = wi::graphics::GetDevice();
        if (!path) {
            path = std::make_unique<wi::RenderPath3D>();
            path->scene = scene.get();
            path->camera = &wcam;
            path->setOutlineEnabled(options.outlines);
            path->setOutlineColor(XMFLOAT4(0.09f, 0.09f, 0.11f, 1));
            path->setOutlineThickness(1.5f);
            path->setOutlineThreshold(0.1f);
            path->setBloomEnabled(false);
            path->setEyeAdaptionEnabled(false);
            path->setExposure(1.0f);
            path->setShadowsEnabled(false);
            path->setReflectionsEnabled(false);
            path->setFXAAEnabled(true);
            path->setAO(wi::RenderPath3D::AO_DISABLED);
        }
        path->scene = scene.get();
        if (path->width != uint32_t(w) || path->height != uint32_t(h)) {
            path->init(uint32_t(w), uint32_t(h), 96);
            path->setOutlineEnabled(options.outlines);  // allocates its target, so only once the size is known
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
        }
        return Ok{};
    }

    void frame(float dt) {
        auto* device = wi::graphics::GetDevice();
        wi::eventhandler::FireEvent(wi::eventhandler::EVENT_THREAD_SAFE_POINT, 0);
        path->PreUpdate();
        path->Update(dt);
        path->PostUpdate();
        path->PreRender();
        path->Render();
        path->PostRender();
        // Compose onto a white target, drawing the 3D result only where geometry wrote stencil. The sky stays at
        // a moderate brightness so it lights the parts sensibly, and never shows: the stage reads as pure white.
        wi::graphics::CommandList cmd = device->BeginCommandList();
        wi::image::SetCanvas(*path);
        wi::graphics::Viewport vp;
        vp.width = float(target.desc.width);
        vp.height = float(target.desc.height);
        device->BindViewports(1, &vp, cmd);
        wi::graphics::RenderPassImage rp[] = {
            wi::graphics::RenderPassImage::RenderTarget(&target, wi::graphics::RenderPassImage::LoadOp::CLEAR),
            wi::graphics::RenderPassImage::DepthStencil(path->GetDepthStencil(), wi::graphics::RenderPassImage::LoadOp::LOAD,
                                                        wi::graphics::RenderPassImage::StoreOp::STORE,
                                                        wi::graphics::ResourceState::DEPTHSTENCIL,
                                                        wi::graphics::ResourceState::DEPTHSTENCIL_READONLY,
                                                        wi::graphics::ResourceState::DEPTHSTENCIL),
        };
        device->RenderPassBegin(rp, 2, cmd);
        wi::image::Params fx;
        fx.enableFullScreen();
        fx.blendFlag = wi::enums::BLENDMODE_OPAQUE;
        fx.quality = wi::image::QUALITY_NEAREST;
        fx.stencilComp = wi::image::STENCILMODE_NOT;  // stencil != 0: something was drawn here
        fx.stencilRef = wi::enums::STENCILREF_EMPTY;
        fx.stencilRefMode = wi::image::STENCILREFMODE_ENGINE;
        wi::image::Draw(&path->GetRenderResult3D(), fx, cmd);
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
void WickedViewport::resetView() { impl_->cam = CameraState{}; }
void WickedViewport::resize(int w, int h) { impl_->width = std::max(1, w); impl_->height = std::max(1, h); }

std::optional<PickHit> WickedViewport::pick(int x, int y) const {
    auto [o, d] = impl_->ray(x + 0.5, y + 0.5);
    std::optional<PickHit> best;
    double bestT = std::numeric_limits<double>::max();
    for (const Part* p : impl_->project.allParts()) {
        if (!impl_->visible(p->id)) continue;
        Vec3 pos = drawnPlacement(p->id).position;
        Aabb box{pos + p->bounds.min, pos + p->bounds.max};
        double t; Vec3 n;
        if (Impl::hit(box, o, d, t, n) && t < bestT) {
            bestT = t;
            best = PickHit{p->id, std::nullopt, o + d * t, n};
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
    for (int i = 0; i < std::max(1, m.options.warmupFrames); ++i) m.frame(1.0f / 60.0f);

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

double WickedViewport::lastRenderMs() const { return impl_->lastMs; }

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
