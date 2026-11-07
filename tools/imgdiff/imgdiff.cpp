// fulmetry_imgdiff: compares a rendered image with a golden image for CTest (see cmake/FulmetryGolden.cmake).
//
//   fulmetry_imgdiff compare <actual> <golden> [--diff out.png] [--threshold N] [--max-rmse X] [--max-fraction F]
//   fulmetry_imgdiff convert <in> <out.png>
//
// Inputs are binary/ASCII PPM (P6/P3) or PNG, detected by content. Only RGB is compared; alpha is dropped.
//   rmse          root mean square of the per-channel differences, in 0..255 units, over all RGB samples
//   over          pixels whose largest per-channel difference exceeds --threshold
//   fraction      over / pixel count
// Exit codes: 0 within limits, 1 over a limit or sizes differ, 2 usage or I/O error.
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"

namespace {

struct Rgb {
    int width = 0, height = 0;
    std::vector<std::uint8_t> px;  // width * height * 3
};

bool readFile(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

// Netpbm header token reader: skips whitespace and '#' comments.
bool ppmToken(const std::vector<std::uint8_t>& b, std::size_t& i, long& out) {
    for (;;) {
        while (i < b.size() && std::isspace(b[i])) ++i;
        if (i < b.size() && b[i] == '#') {
            while (i < b.size() && b[i] != '\n') ++i;
            continue;
        }
        break;
    }
    if (i >= b.size() || !std::isdigit(b[i])) return false;
    out = 0;
    while (i < b.size() && std::isdigit(b[i])) {
        out = out * 10 + (b[i] - '0');
        if (out > 1000000) return false;
        ++i;
    }
    return true;
}

std::optional<Rgb> decodePpm(const std::vector<std::uint8_t>& b, std::string& err) {
    const bool binary = b[1] == '6';
    std::size_t i = 2;
    long w = 0, h = 0, maxval = 0;
    if (!ppmToken(b, i, w) || !ppmToken(b, i, h) || !ppmToken(b, i, maxval) || w <= 0 || h <= 0 || maxval <= 0 ||
        maxval > 255) {
        err = "bad PPM header (8-bit P6/P3 only)";
        return std::nullopt;
    }
    Rgb img;
    img.width = static_cast<int>(w);
    img.height = static_cast<int>(h);
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3;
    img.px.resize(n);
    if (binary) {
        ++i;  // exactly one whitespace byte after maxval
        if (b.size() < i + n) {
            err = "truncated PPM data";
            return std::nullopt;
        }
        for (std::size_t k = 0; k < n; ++k) img.px[k] = b[i + k];
    } else {
        for (std::size_t k = 0; k < n; ++k) {
            long v = 0;
            if (!ppmToken(b, i, v) || v > maxval) {
                err = "bad P3 sample";
                return std::nullopt;
            }
            img.px[k] = static_cast<std::uint8_t>(v);
        }
    }
    if (maxval != 255)
        for (auto& v : img.px) v = static_cast<std::uint8_t>((v * 255 + maxval / 2) / maxval);
    return img;
}

std::optional<Rgb> load(const std::string& path, std::string& err) {
    std::vector<std::uint8_t> bytes;
    if (!readFile(path, bytes)) {
        err = "cannot read " + path;
        return std::nullopt;
    }
    if (bytes.size() >= 2 && bytes[0] == 'P' && (bytes[1] == '6' || bytes[1] == '3')) {
        auto img = decodePpm(bytes, err);
        if (!img) err = path + ": " + err;
        return img;
    }
    if (bytes.size() > static_cast<std::size_t>(INT32_MAX)) {
        err = path + ": too large";
        return std::nullopt;
    }
    int w = 0, h = 0, comp = 0;
    std::uint8_t* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, 3);
    if (!data) {
        err = path + ": not a PPM or PNG (" + stbi_failure_reason() + ")";
        return std::nullopt;
    }
    Rgb img;
    img.width = w;
    img.height = h;
    img.px.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
    stbi_image_free(data);
    return img;
}

bool writePng(const Rgb& img, const std::string& path) {
    return stbi_write_png(path.c_str(), img.width, img.height, 3, img.px.data(), img.width * 3) != 0;
}

bool parseNumber(const char* s, double& out) {
    char* end = nullptr;
    out = std::strtod(s, &end);
    return end != s && *end == '\0' && std::isfinite(out) && out >= 0;
}

int usage() {
    std::cerr << "usage: fulmetry_imgdiff compare <actual> <golden> [--diff out.png] [--threshold N]\n"
                 "                                 [--max-rmse X] [--max-fraction F]\n"
                 "       fulmetry_imgdiff convert <in.ppm|in.png> <out.png>\n";
    return 2;
}

int convert(int argc, char** argv) {
    if (argc != 4) return usage();
    std::string err;
    auto img = load(argv[2], err);
    if (!img) {
        std::cerr << "imgdiff: " << err << "\n";
        return 2;
    }
    if (!writePng(*img, argv[3])) {
        std::cerr << "imgdiff: cannot write " << argv[3] << "\n";
        return 2;
    }
    std::cout << "imgdiff: wrote " << argv[3] << " (" << img->width << "x" << img->height << ")\n";
    return 0;
}

int compare(int argc, char** argv) {
    if (argc < 4) return usage();
    const std::string actualPath = argv[2], goldenPath = argv[3];
    std::string diffPath;
    double threshold = 8, maxRmse = 4, maxFraction = 0.002;
    for (int i = 4; i < argc; ++i) {
        const std::string a = argv[i];
        if (i + 1 >= argc) return usage();
        const char* v = argv[++i];
        bool ok = true;
        if (a == "--diff") diffPath = v;
        else if (a == "--threshold") ok = parseNumber(v, threshold);
        else if (a == "--max-rmse") ok = parseNumber(v, maxRmse);
        else if (a == "--max-fraction") ok = parseNumber(v, maxFraction);
        else ok = false;
        if (!ok) return usage();
    }

    std::string err;
    auto actual = load(actualPath, err);
    if (!actual) {
        std::cerr << "imgdiff: " << err << "\n";
        return 2;
    }
    auto golden = load(goldenPath, err);
    if (!golden) {
        std::cerr << "imgdiff: " << err << "\n";
        return 2;
    }
    if (actual->width != golden->width || actual->height != golden->height) {
        std::cout << "imgdiff: FAIL size " << actual->width << "x" << actual->height << " vs golden " << golden->width
                  << "x" << golden->height << "\n";
        return 1;
    }

    const std::size_t pixels = static_cast<std::size_t>(golden->width) * static_cast<std::size_t>(golden->height);
    Rgb diff{golden->width, golden->height, std::vector<std::uint8_t>(pixels * 3)};
    double sumSq = 0;
    std::size_t over = 0;
    int maxDelta = 0;
    for (std::size_t p = 0; p < pixels; ++p) {
        const std::uint8_t* a = &actual->px[p * 3];
        const std::uint8_t* g = &golden->px[p * 3];
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(int(a[c]) - int(g[c]));
            sumSq += double(d) * d;
            if (d > worst) worst = d;
        }
        if (worst > maxDelta) maxDelta = worst;
        std::uint8_t* o = &diff.px[p * 3];
        if (worst > threshold) {
            ++over;  // red: over the threshold
            o[0] = 220; o[1] = 20; o[2] = 60;
        } else if (worst > 0) {
            o[0] = 255; o[1] = 176; o[2] = 0;  // amber: differs, within threshold
        } else {
            // Faded golden for context.
            const double lum = 0.299 * g[0] + 0.587 * g[1] + 0.114 * g[2];
            const auto v = static_cast<std::uint8_t>(190.0 + lum * (60.0 / 255.0));
            o[0] = o[1] = o[2] = v;
        }
    }
    const double rmse = std::sqrt(sumSq / double(pixels * 3));
    const double fraction = double(over) / double(pixels);
    const bool pass = rmse <= maxRmse && fraction <= maxFraction;

    std::printf("imgdiff: %dx%d rmse=%.4f (max %.4f) over-threshold=%zu/%zu = %.4f%% (max %.4f%%) threshold=%g "
                "max-delta=%d -> %s\n",
                golden->width, golden->height, rmse, maxRmse, over, pixels, fraction * 100, maxFraction * 100,
                threshold, maxDelta, pass ? "PASS" : "FAIL");
    if (!diffPath.empty()) {
        if (!writePng(diff, diffPath)) {
            std::cerr << "imgdiff: cannot write " << diffPath << "\n";
            return 2;
        }
        std::printf("imgdiff: diff image %s\n", diffPath.c_str());
    }
    return pass ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    if (!std::strcmp(argv[1], "compare")) return compare(argc, argv);
    if (!std::strcmp(argv[1], "convert")) return convert(argc, argv);
    return usage();
}
