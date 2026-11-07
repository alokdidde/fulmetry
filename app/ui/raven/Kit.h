// Small builders over the Raven C API for the parts of the workbench made at run time (tabs, parts,
// checks, chat). Raven types stay inside app/ui. Owned by S5.
#pragma once
#include <Raven.h>

#include <cstdint>
#include <functional>
#include <string>

namespace fulmetry::ui::kit {

// The prototype's palette (high-key overrides of fulmetry-workbench.html).
namespace color {
inline constexpr uint32_t kInk = 0x14171A;
inline constexpr uint32_t kMute = 0x66707B;
inline constexpr uint32_t kLine = 0xE1E5EA;
inline constexpr uint32_t kHud = 0xFFFFFF;
inline constexpr uint32_t kHud2 = 0xF3F5F7;
inline constexpr uint32_t kAccent = 0xE5322D;
inline constexpr uint32_t kPass = 0x14A86B;
inline constexpr uint32_t kUnres = 0xE88A12;
inline constexpr uint32_t kRun = 0x1E6FE8;
inline constexpr uint32_t kBody = 0x2A2F35;
}  // namespace color

struct Fonts {
    Rid regular = R_INVALID_ID, medium = R_INVALID_ID, semibold = R_INVALID_ID, bold = R_INVALID_ID;
    Rid tightSemibold = R_INVALID_ID, tightBold = R_INVALID_ID, mono = R_INVALID_ID;
    // Loads the static TTFs from <assets>/fonts. Variable fonts draw only their default instance in Raven,
    // so every weight is its own file. Returns false when one is missing.
    bool load(const std::string& assets);
};

uint32_t parseHex(const std::string& hex, uint32_t fallback = color::kMute);  // "#rrggbb"

// Letter spacing for upper-case eyebrows: Raven has none, so a hair space goes between the letters.
std::string tracked(const std::string& text, int hairSpaces = 1);
// CSS line-clamp: the text as it wraps in `width` units, cut to `lines` lines with an ellipsis.
std::string clampLines(const std::string& text, Rid font, float size, float width, int lines);
// Applies tracked() to every Label with the class "eyebrow" under `root` (once per label).
void trackEyebrows(Rid root);

Rid hbox(Rid parent, float spacing = 0);
Rid vbox(Rid parent, float spacing = 0);
Rid spacer(Rid parent, float fill = 1);
Rid label(Rid parent, const std::string& text, Rid font, float size, uint32_t rgb = color::kInk, float alpha = 1);
Rid wrapLabel(Rid parent, const std::string& text, Rid font, float size, uint32_t rgb = color::kInk);
Rid border(Rid parent, uint32_t bg, float bgAlpha, float radius, float padX, float padY);
void outline(Rid border, uint32_t rgb = color::kLine, float alpha = 1);
Rid rule(Rid parent, bool vertical);
Rid swatch(Rid parent, uint32_t rgb, float size, float radius);
Rid icon(Rid parent, const std::string& assets, const std::string& name);  // <assets>/icons/<name>.svg

// Click and hover handling for any widget (a Border acting as a button). The handler runs from the
// event callback; it must not destroy widgets (queue that work, see WorkbenchView::post).
struct Hover {
    uint32_t normal = 0, hovered = 0;
    float normalAlpha = 0, hoveredAlpha = 0;
    bool enabled = false;
};
void onClick(Rid widget, std::function<void()> handler, Hover hover = {});

void setRgb(Rid label, uint32_t rgb, float alpha = 1);
void setBackground(Rid border, uint32_t rgb, float alpha = 1);

}  // namespace fulmetry::ui::kit
