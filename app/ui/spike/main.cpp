// Fulmetry S5 spike: is Raven GUI (github.com/oscar-soirey/Raven) good enough for the workbench?
//
// One window: a custom-drawn stage standing in for the Wicked Engine viewport, the project chat docked on the
// right (a scrolling list of ~50 messages and a single-line composer), and floating panels over the stage.
// The layout is workbench.xml (live reload on). The program draws frames continuously, measures --frames of
// them after a warm-up and prints the timings, then keeps running for --run-seconds so a script can type
// into the composer (xdotool) and take a screenshot.
#include <Raven.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifndef RAVEN_SPIKE_XML
#define RAVEN_SPIKE_XML "workbench.xml"
#endif

namespace {

struct Options {
    std::string xml = RAVEN_SPIKE_XML;
    std::string font;        // e.g. InterTight-Variable.ttf; empty keeps the embedded Roboto
    std::string monoFont;    // e.g. IBMPlexMono-Regular.ttf, used for the stage overlay text
    int warmup = 60;
    int frames = 600;
    double runSeconds = 0;   // keep running this long in total (0: quit once measured)
    int width = 0, height = 0;  // --size WxH overrides the size in the XML
    bool emptyStage = false;    // --empty-stage: paint nothing on the stage (baseline cost of the UI alone)
};

struct App {
    Rid window = R_INVALID_ID;
    Rid stage = R_INVALID_ID;
    Rid messages = R_INVALID_ID;
    Rid list = R_INVALID_ID;
    Rid composer = R_INVALID_ID;
    Rid stats = R_INVALID_ID;
    Rid mono = R_INVALID_ID;
    int messageCount = 0;
    std::vector<std::string> committed;
};

App g;

// Fulmetry palette: high-key white, red accent.
constexpr float kAccent[3] = {0.89f, 0.16f, 0.15f};

void onError(RError code, RSeverity severity, const char* detail) {
    std::fprintf(stderr, "[raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity),
                 detail ? detail : "");
}

// ---------------------------------------------------------------- stage

struct V3 { float x, y, z; };

bool g_emptyStage = false;

void paintStage(Rid, RPainter* p, float w, float h, void*) {
    if (g_emptyStage) return;
    const auto t = static_cast<float>(Rget_time());
    // backdrop: high-key studio, a few horizontal bands for a soft gradient
    for (int i = 0; i < 16; ++i) {
        const float k = static_cast<float>(i) / 15.0f;
        const float c = 0.985f - 0.06f * k;
        Rdraw_rect(p, 0, h * k - 1, w, h / 15.0f + 2, 0, c, c, c + 0.004f, 1);
    }

    const float cx = w * 0.5f, cy = h * 0.56f, f = std::min(w, h) * 1.1f;
    const float yaw = t * 0.6f, pitch = -0.42f;
    auto project = [&](V3 v, float& sx, float& sy) {
        const float x = v.x * std::cos(yaw) - v.z * std::sin(yaw);
        const float z0 = v.x * std::sin(yaw) + v.z * std::cos(yaw);
        const float y = v.y * std::cos(pitch) - z0 * std::sin(pitch);
        const float z = v.y * std::sin(pitch) + z0 * std::cos(pitch) + 8.0f;
        sx = cx + f * x / z;
        sy = cy - f * y / z;
    };
    auto line3 = [&](V3 a, V3 b, float th, float r, float gg, float bb, float al) {
        float x0, y0, x1, y1;
        project(a, x0, y0);
        project(b, x1, y1);
        Rdraw_line(p, x0, y0, x1, y1, th, r, gg, bb, al);
    };

    // floor grid: 41 x 41 lines
    for (int i = -20; i <= 20; ++i) {
        const float s = static_cast<float>(i) * 0.25f;
        const float a = (i % 4 == 0) ? 0.22f : 0.09f;
        line3({s, -1, -5}, {s, -1, 5}, 1.0f, 0.2f, 0.2f, 0.25f, a);
        line3({-5, -1, s}, {5, -1, s}, 1.0f, 0.2f, 0.2f, 0.25f, a);
    }
    // the "rover": a box body and four wheels as polylines
    const std::array<V3, 8> c = {{{-1, -0.4f, -0.6f}, {1, -0.4f, -0.6f}, {1, 0.3f, -0.6f}, {-1, 0.3f, -0.6f},
                                  {-1, -0.4f, 0.6f}, {1, -0.4f, 0.6f}, {1, 0.3f, 0.6f}, {-1, 0.3f, 0.6f}}};
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    for (auto& ed : e) line3(c[ed[0]], c[ed[1]], 2.0f, 0.12f, 0.12f, 0.14f, 1);
    for (int wi = 0; wi < 4; ++wi) {
        const float wx = (wi & 1) ? 0.7f : -0.7f, wz = (wi & 2) ? 0.75f : -0.75f;
        V3 prev{};
        for (int k = 0; k <= 32; ++k) {
            const float a = static_cast<float>(k) / 32.0f * 6.2831853f + t * 2.0f;
            V3 v{wx + 0.35f * std::cos(a), -0.6f + 0.35f * std::sin(a), wz};
            if (k) line3(prev, v, 2.0f, kAccent[0], kAccent[1], kAccent[2], wi == 0 ? 1.0f : 0.55f);
            prev = v;
        }
    }
    // axis gizmo, bottom right of the stage
    const float gx = w - 56, gy = h - 56;
    Rdraw_circle(p, gx, gy, 34, 1, 1, 1, 0.9f);
    Rdraw_line(p, gx, gy, gx + 26 * std::cos(yaw), gy + 10 * std::sin(yaw), 2, kAccent[0], kAccent[1], kAccent[2], 1);
    Rdraw_line(p, gx, gy, gx, gy - 26, 2, 0.2f, 0.65f, 0.3f, 1);
    Rdraw_line(p, gx, gy, gx - 26 * std::sin(yaw), gy + 10 * std::cos(yaw), 2, 0.2f, 0.4f, 0.9f, 1);
    Rdraw_text(p, g.mono, 12, 16, h - 72, "stage: custom-drawn (Rdraw_*), redrawn every frame", 0.35f, 0.35f, 0.4f, 1);
}

Rid createStage(const char*, void*) {
    Rid w = Rwidget_create(R_WIDGET_CUSTOM);
    Rcustom_set_paint_callback(w, paintStage, nullptr);
    return w;
}

// ---------------------------------------------------------------- chat

void addMessage(const char* author, const char* text, bool mine) {
    Rid bubble = Rwidget_add_child(g.list, Rwidget_create(R_WIDGET_BORDER));
    Rborder_set_corner_radius(bubble, 3, 3, 3, 3);
    Rborder_set_padding(bubble, 10, 8, 10, 8);
    if (mine) {
        Rborder_set_background(bubble, 1.0f, 0.95f, 0.945f, 1);
        Rborder_set_outline(bubble, 1, kAccent[0], kAccent[1], kAccent[2], 0.25f);
    } else {
        Rborder_set_background(bubble, 0.965f, 0.965f, 0.97f, 1);
    }
    Rid col = Rwidget_add_child(bubble, Rwidget_create(R_WIDGET_VBOX));
    Rpanel_set_spacing(col, 2);
    Rid who = Rwidget_add_child(col, Rlabel_create(author));
    Rlabel_set_text_size(who, 11);
    if (mine) Rlabel_set_color(who, kAccent[0], kAccent[1], kAccent[2], 1);
    else Rlabel_set_color(who, 0.45f, 0.45f, 0.5f, 1);
    Rid body = Rwidget_add_child(col, Rlabel_create(text));
    Rlabel_set_wrap(body, R_TRUE);
    Rlabel_set_selectable(body, R_TRUE);
    ++g.messageCount;
}

void seedMessages() {
    static const char* agentLines[] = {
        "Resized the wheels to 64 mm and moved the axles 4 mm outward. Revision 12 saved.",
        "Clearance check passed: 2.1 mm between the left wheel and the chassis at full steer.",
        "The motor bracket now uses M3 heat-set inserts; the hole diameter is 4.0 mm.",
        "Printing estimate for the chassis: 3 h 40 min, 86 g of PETG at 0.2 mm layers.",
        "Pinout updated: the encoder moved from GPIO 4 to GPIO 17 to free the I2C bus.",
    };
    static const char* userLines[] = {
        "Make the wheels a bit bigger, it keeps getting stuck on the rug.",
        "Does the battery still fit with the new axle position?",
        "Add a cable channel along the left side, 6 mm wide.",
        "What does the drop test say?",
        "Ok, ship it. Export the STL for the chassis.",
    };
    for (int i = 0; i < 50; ++i) {
        char text[256];
        if (i % 2 == 0)
            std::snprintf(text, sizeof text, "#%d  %s", i + 1, userLines[(i / 2) % 5]);
        else
            std::snprintf(text, sizeof text, "#%d  %s", i + 1, agentLines[(i / 2) % 5]);
        addMessage(i % 2 == 0 ? "Alok" : "Resize wheels (agent)", text, i % 2 == 0);
    }
}

void send(const char* text) {
    if (!text || !*text) return;
    std::printf("composer committed: \"%s\"\n", text);
    std::fflush(stdout);
    g.committed.emplace_back(text);
    addMessage("Alok", text, true);
    Rtext_input_set_text(g.composer, "");
    Rscroll_box_scroll_to_end(g.messages);
}

void onCommitted(Rid, const char* text, void*) { send(text); }
void onSendClicked(Rid, const char*, void*) { send(Rtext_input_get_text(g.composer)); }

bool bind() {
    g.stage = Rwidget_find(g.window, "stage");
    g.messages = Rwidget_find(g.window, "messages");
    g.list = Rwidget_find(g.window, "message.list");
    g.composer = Rwidget_find(g.window, "composer");
    g.stats = Rwidget_find(g.window, "stats");
    if (g.stage == R_INVALID_ID || g.list == R_INVALID_ID || g.composer == R_INVALID_ID) {
        std::fprintf(stderr, "workbench.xml: missing stage, message.list or composer\n");
        return false;
    }
    Rtext_input_set_committed_callback(g.composer, onCommitted, nullptr);
    return true;
}

// ---------------------------------------------------------------- theme

void applyTheme(const Options& o) {
    Rlook_set(R_LOOK_RAVEN_LIGHT);
    Rtheme_set_accent(kAccent[0], kAccent[1], kAccent[2]);
    Rtheme_set_color(R_COLOR_WINDOW, 0.97f, 0.97f, 0.975f, 1);
    Rtheme_set_color(R_COLOR_PANEL, 1, 1, 1, 1);
    Rtheme_set_metric(R_METRIC_CORNER_RADIUS, 3);
    Rtheme_set_metric(R_METRIC_POPUP_RADIUS, 3);
    if (!o.font.empty()) {
        Rid font = Rfont_create_from_file(o.font.c_str());
        if (font != R_INVALID_ID) Rtheme_set_font(font);
        else std::fprintf(stderr, "could not load font %s\n", o.font.c_str());
    }
    if (!o.monoFont.empty()) g.mono = Rfont_create_from_file(o.monoFont.c_str());
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--xml") o.xml = next();
        else if (a == "--font") o.font = next();
        else if (a == "--mono-font") o.monoFont = next();
        else if (a == "--warmup") o.warmup = std::atoi(next());
        else if (a == "--frames") o.frames = std::atoi(next());
        else if (a == "--run-seconds") o.runSeconds = std::atof(next());
        else if (a == "--empty-stage") o.emptyStage = true;
        else if (a == "--size") std::sscanf(next(), "%dx%d", &o.width, &o.height);
    }
    return o;
}

double percentile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(q * static_cast<double>(v.size() - 1))];
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse(argc, argv);
    g_emptyStage = o.emptyStage;
    Rerror_set_callback(onError);

    RInitDesc desc{};
    desc.backend = R_BACKEND_VULKAN;
    desc.app_name = "fulmetry-raven-spike";
    desc.disable_vsync = R_TRUE;  // measure the real cost of a frame, not the refresh rate
    if (!Rinit_ex(&desc)) {
        std::fprintf(stderr, "Rinit_ex failed\n");
        return 1;
    }
    applyTheme(o);
    Rxml_register_tag("Stage", createStage, nullptr, nullptr);
    Raction_bind("chat.send", onSendClicked, nullptr);

    g.window = Rwindow_create_from_xml_file(o.xml.c_str());
    if (g.window == R_INVALID_ID || !bind()) {
        Rshutdown();
        return 1;
    }
    if (o.width > 0 && o.height > 0) Rwindow_set_size(g.window, o.width, o.height);
    seedMessages();
    Rscroll_box_scroll_to_end(g.messages);
    Rwidget_focus(g.composer);
    Rwindow_set_continuous_redraw(g.window, R_TRUE);

    std::printf("window scale: %.2f, vulkan device: %p\n", static_cast<double>(Rwindow_get_scale(g.window)),
                Rvulkan_get_device());

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    std::vector<double> wall, layout, paint, render;
    int frame = 0;
    bool reported = false;
    auto last = clock::now();
    while (Rdraw_frame()) {
        const auto now = clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - last).count();
        last = now;
        Rwidget_invalidate(g.stage);  // the stage animates: paint it again next frame
        ++frame;
        if (frame > o.warmup && frame <= o.warmup + o.frames) {
            RFrameStats st{};
            Rwindow_get_frame_stats(g.window, &st);
            wall.push_back(ms);
            layout.push_back(st.layout_ms);
            paint.push_back(st.paint_ms);
            render.push_back(st.render_ms);
            if (frame % 60 == 0 && g.stats != R_INVALID_ID) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "rev 12 - desk rover - %.1f ms/frame - %u widgets", ms, st.widget_count);
                Rlabel_set_text(g.stats, buf);
            }
        }
        if (!reported && frame == o.warmup + o.frames) {
            reported = true;
            double sum = 0;
            for (double v : wall) sum += v;
            auto mean = [](const std::vector<double>& v) {
                double s = 0;
                for (double x : v) s += x;
                return v.empty() ? 0.0 : s / static_cast<double>(v.size());
            };
            RFrameStats st{};
            Rwindow_get_frame_stats(g.window, &st);
            std::printf("frames measured: %zu (after %d warm-up)\n", wall.size(), o.warmup);
            std::printf("wall ms/frame: mean %.2f  p50 %.2f  p95 %.2f  p99 %.2f  max %.2f  -> %.1f fps\n",
                        mean(wall), percentile(wall, 0.5), percentile(wall, 0.95), percentile(wall, 0.99),
                        percentile(wall, 1.0), 1000.0 * static_cast<double>(wall.size()) / sum);
            std::printf("raven ms: layout %.3f  paint %.3f  render %.3f (means)\n", mean(layout), mean(paint),
                        mean(render));
            std::printf("last frame: %u widgets, %u instances, %u draw calls, %u backdrops, %d messages\n",
                        st.widget_count, st.instance_count, st.draw_call_count, st.backdrop_count, g.messageCount);
            float x, y, cw, ch;
            Rwidget_get_rect(Rwidget_find(g.window, "chat"), &x, &y, &cw, &ch);
            int ww, wh;
            Rwindow_get_size(g.window, &ww, &wh);
            std::printf("window %dx%d, chat panel %.0fx%.0f at x=%.0f\n", ww, wh, static_cast<double>(cw),
                        static_cast<double>(ch), static_cast<double>(x));
            std::fflush(stdout);
        }
        const double elapsed = std::chrono::duration<double>(clock::now() - start).count();
        if (reported && elapsed >= o.runSeconds) break;
    }
    std::printf("composer text at exit: \"%s\"; messages: %d; committed: %zu\n", Rtext_input_get_text(g.composer),
                g.messageCount, g.committed.size());
    Rshutdown();
    return 0;
}
