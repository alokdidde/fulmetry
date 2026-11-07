// The only translation unit that compiles the vendored stb implementations. third_party/ is a SYSTEM include
// directory, so GCC/Clang and MSVC (/external:W0) already silence warnings from these headers; the pragmas
// below keep that true even if the include style changes.
#if defined(_MSC_VER)
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(push, 0)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
