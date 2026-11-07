#include "Kit.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace fulmetry::ui::kit {

namespace {

float ch(uint32_t rgb, int shift) { return static_cast<float>((rgb >> shift) & 0xFF) / 255.0f; }

struct ClickState {
    std::function<void()> handler;
    Hover hover;
    bool pressed = false;
};

// Owned by the widget: freed by its destroy callback.
int onEvent(Rid widget, const REvent* e, void* user) {
    auto* s = static_cast<ClickState*>(user);
    switch (e->type) {
        case R_EVENT_MOUSE_ENTER:
            if (s->hover.enabled) setBackground(widget, s->hover.hovered, s->hover.hoveredAlpha);
            return R_FALSE;
        case R_EVENT_MOUSE_LEAVE:
            if (s->hover.enabled) setBackground(widget, s->hover.normal, s->hover.normalAlpha);
            s->pressed = false;
            return R_FALSE;
        case R_EVENT_MOUSE_DOWN:
            if (e->button != R_MOUSE_LEFT) return R_FALSE;
            s->pressed = true;
            return R_TRUE;
        case R_EVENT_MOUSE_UP:
            if (e->button != R_MOUSE_LEFT || !s->pressed) return R_FALSE;
            s->pressed = false;
            if (s->handler) s->handler();
            return R_TRUE;
        default:
            return R_FALSE;
    }
}

void onDestroy(Rid, void* user) { delete static_cast<ClickState*>(user); }

}  // namespace

bool Fonts::load(const std::string& assets) {
    auto f = [&](const char* file) {
        const std::string path = assets + "/fonts/" + file;
        Rid id = Rfont_create_from_file(path.c_str());
        if (id == R_INVALID_ID) std::fprintf(stderr, "workbench: cannot load font %s\n", path.c_str());
        return id;
    };
    regular = f("Inter-Regular.ttf");
    medium = f("Inter-Medium.ttf");
    semibold = f("Inter-SemiBold.ttf");
    bold = f("Inter-Bold.ttf");
    tightSemibold = f("InterTight-SemiBold.ttf");
    tightBold = f("InterTight-Bold.ttf");
    mono = f("IBMPlexMono-Regular.ttf");
    for (Rid id : {regular, medium, semibold, bold, tightSemibold, tightBold, mono})
        if (id == R_INVALID_ID) return false;
    // Markdown **bold** (the hint line) draws with the real semibold, not a smeared regular.
    Rfont_set_variant(regular, R_FONT_BOLD, semibold);
    return true;
}

uint32_t parseHex(const std::string& hex, uint32_t fallback) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    unsigned v = 0;
    if (std::sscanf(hex.c_str() + 1, "%6x", &v) != 1) return fallback;
    return v;
}

std::string tracked(const std::string& text, int hairSpaces) {
    static const char kHair[] = "\xE2\x80\x8A";  // U+200A HAIR SPACE
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        out += text[i];
        // after the last byte of each code point but the final one
        const bool endsCodePoint =
            i + 1 < text.size() && (static_cast<unsigned char>(text[i + 1]) & 0xC0) != 0x80;
        if (endsCodePoint)
            for (int k = 0; k < hairSpaces; ++k) out += kHair;
    }
    return out;
}

std::string clampLines(const std::string& text, Rid font, float size, float width, int lines) {
    auto fits = [&](const std::string& s) {
        float w = 0, h = 0;
        Rfont_measure_text(font, size, s.c_str(), &w, &h);
        return w <= width;
    };
    // greedy word wrap, as the label will do
    std::vector<std::string> words;
    for (size_t i = 0; i < text.size();) {
        size_t j = text.find(' ', i);
        if (j == std::string::npos) j = text.size();
        words.push_back(text.substr(i, j - i));
        i = j + 1;
    }
    std::string out, line;
    int line_no = 1;
    for (size_t k = 0; k < words.size(); ++k) {
        const std::string candidate = line.empty() ? words[k] : line + " " + words[k];
        if (fits(candidate) || line.empty()) {
            line = candidate;
            continue;
        }
        if (line_no == lines) {  // last line is full: cut it and add the ellipsis
            static const char kEllipsis[] = "\xE2\x80\xA6";
            std::string last = line + " " + words[k];
            while (!last.empty() && !fits(last + kEllipsis)) {
                last.pop_back();
                while (!last.empty() && (static_cast<unsigned char>(last.back()) & 0xC0) == 0x80) last.pop_back();
            }
            while (!last.empty() && (last.back() == ' ' || last.back() == ',')) last.pop_back();
            return out + last + kEllipsis;
        }
        out += line + " ";
        line = words[k];
        ++line_no;
    }
    return out + line;
}

void trackEyebrows(Rid root) {
    if (root == R_INVALID_ID) return;
    if (Rwidget_get_type(root) == R_WIDGET_LABEL && Rwidget_has_class(root, "eyebrow") &&
        !Rwidget_has_class(root, "tracked")) {
        const std::string t = Rlabel_get_text(root) ? Rlabel_get_text(root) : "";
        Rlabel_set_text(root, tracked(t).c_str());
        Rwidget_add_class(root, "tracked");
    }
    const int n = Rwidget_get_child_count(root);
    for (int i = 0; i < n; ++i) trackEyebrows(Rwidget_get_child(root, i));
}

Rid hbox(Rid parent, float spacing) {
    Rid w = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_HBOX));
    Rpanel_set_spacing(w, spacing);
    return w;
}

Rid vbox(Rid parent, float spacing) {
    Rid w = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_VBOX));
    Rpanel_set_spacing(w, spacing);
    return w;
}

Rid spacer(Rid parent, float fill) {
    Rid w = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_SPACER));
    Rslot_set_fill(w, fill);
    return w;
}

Rid label(Rid parent, const std::string& text, Rid font, float size, uint32_t rgb, float alpha) {
    Rid w = Rwidget_add_child(parent, Rlabel_create(text.c_str()));
    if (font != R_INVALID_ID) Rlabel_set_font(w, font);
    Rlabel_set_text_size(w, size);
    setRgb(w, rgb, alpha);
    return w;
}

Rid wrapLabel(Rid parent, const std::string& text, Rid font, float size, uint32_t rgb) {
    Rid w = label(parent, text, font, size, rgb);
    Rlabel_set_wrap(w, R_TRUE);
    return w;
}

Rid border(Rid parent, uint32_t bg, float bgAlpha, float radius, float padX, float padY) {
    Rid w = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_BORDER));
    setBackground(w, bg, bgAlpha);
    Rborder_set_corner_radius(w, radius, radius, radius, radius);
    Rborder_set_padding(w, padX, padY, padX, padY);
    return w;
}

void outline(Rid b, uint32_t rgb, float alpha) { Rborder_set_outline(b, 1, ch(rgb, 16), ch(rgb, 8), ch(rgb, 0), alpha); }

Rid rule(Rid parent, bool vertical) {
    Rid w = border(parent, color::kLine, 1, 0, 0, 0);
    if (vertical) Rwidget_set_size(w, 1, -1);
    else Rwidget_set_size(w, -1, 1);
    return w;
}

Rid swatch(Rid parent, uint32_t rgb, float size, float radius) {
    Rid w = border(parent, rgb, 1, radius, 0, 0);
    outline(w, color::kInk, 0.14f);  // white parts stay visible on white rows
    Rwidget_set_size(w, size, size);
    Rslot_set_valign(w, R_VALIGN_CENTER);
    return w;
}

Rid icon(Rid parent, const std::string& assets, const std::string& name) {
    static std::map<std::string, Rid> cache;
    Rid& tex = cache[name];
    if (tex == 0 || !Rtexture_is_valid(tex)) {
        const std::string path = assets + "/icons/" + name + ".svg";
        tex = Rtexture_create_from_file(path.c_str());
    }
    Rid w = Rwidget_add_child(parent, Rwidget_create(R_WIDGET_IMAGE));
    Rimage_set_texture(w, tex);
    Rslot_set_valign(w, R_VALIGN_CENTER);
    return w;
}

void onClick(Rid widget, std::function<void()> handler, Hover hover) {
    auto* s = new ClickState{std::move(handler), hover, false};
    Rwidget_set_event_callback(widget, onEvent, s);
    Rwidget_set_destroy_callback(widget, onDestroy, s);
    Rwidget_set_cursor(widget, R_CURSOR_HAND);
}

void setRgb(Rid l, uint32_t rgb, float alpha) { Rlabel_set_color(l, ch(rgb, 16), ch(rgb, 8), ch(rgb, 0), alpha); }

void setBackground(Rid b, uint32_t rgb, float alpha) { Rborder_set_background(b, ch(rgb, 16), ch(rgb, 8), ch(rgb, 0), alpha); }

}  // namespace fulmetry::ui::kit
