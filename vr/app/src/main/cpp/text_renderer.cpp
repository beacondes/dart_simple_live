#include "text_renderer.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <vector>

#include <android/log.h>

#define LOG_TAG "TextRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Common Android system font locations (CJK-capable first).
static const char* kFontPaths[] = {
    "/system/fonts/NotoSansCJK-Regular.ttc",
    "/system/fonts/NotoSansCJKsc-Regular.otf",
    "/system/fonts/DroidSansFallback.ttf",
    "/system/fonts/Roboto-Regular.ttf",
};

static std::vector<uint32_t> Utf8Decode(const std::string& s) {
    std::vector<uint32_t> cps;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        uint32_t cp = 0;
        int len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { i++; continue; }
        if (i + len > s.size()) break;
        for (int k = 1; k < len; k++) cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        cps.push_back(cp);
        i += len;
    }
    return cps;
}

bool TextRenderer::Init() {
    FT_Library ft = nullptr;
    if (FT_Init_FreeType(&ft) != 0) { LOGE("FT_Init_FreeType failed"); return false; }

    FT_Face face = nullptr;
    for (const char* path : kFontPaths) {
        if (FT_New_Face(ft, path, 0, &face) == 0) {
            LOGI("loaded font: %s", path);
            break;
        }
    }
    if (!face) { LOGE("no font found"); FT_Done_FreeType(ft); return false; }

    ft_ = ft;
    face_ = face;
    init_ = true;
    return true;
}

GLuint TextRenderer::RenderText(const std::string& text, int& outW, int& outH, uint32_t rgba) {
    if (!init_) return 0;
    FT_Face face = (FT_Face)face_;

    const int px = 48;  // font height in pixels
    FT_Set_Pixel_Sizes(face, 0, px);

    auto cps = Utf8Decode(text);
    if (cps.empty()) return 0;

    // First pass: measure.
    int totalW = 0, maxH = 0, maxTop = 0;
    for (uint32_t cp : cps) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        totalW += face->glyph->bitmap.width + 2;
        maxH = std::max(maxH, (int)face->glyph->bitmap.rows);
        maxTop = std::max(maxTop, face->glyph->bitmap_top);
    }
    if (totalW <= 0 || maxH <= 0) return 0;
    int baseline = maxTop;

    // Second pass: composite into one bitmap.
    std::vector<uint8_t> pixels((size_t)totalW * maxH, 0);
    int x = 0;
    for (uint32_t cp : cps) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_Bitmap& bm = face->glyph->bitmap;
        int y0 = baseline - face->glyph->bitmap_top;
        for (int row = 0; row < (int)bm.rows; row++) {
            for (int col = 0; col < (int)bm.width; col++) {
                int py = y0 + row;
                int px2 = x + col;
                if (py >= 0 && py < maxH && px2 >= 0 && px2 < totalW) {
                    pixels[(size_t)py * totalW + px2] = bm.buffer[row * bm.pitch + col];
                }
            }
        }
        x += bm.width + 2;
    }

    // Convert grayscale -> RGBA with the given color.
    uint8_t r = (rgba >> 24) & 0xFF, g = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF, a = rgba & 0xFF;
    std::vector<uint8_t> rgbaBuf((size_t)totalW * maxH * 4);
    for (size_t i = 0; i < (size_t)totalW * maxH; i++) {
        rgbaBuf[i * 4 + 0] = r;
        rgbaBuf[i * 4 + 1] = g;
        rgbaBuf[i * 4 + 2] = b;
        rgbaBuf[i * 4 + 3] = (uint8_t)((uint16_t)pixels[i] * a / 255);
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, totalW, maxH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuf.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    outW = totalW;
    outH = maxH;
    return tex;
}

void TextRenderer::Shutdown() {
    if (face_) { FT_Done_Face((FT_Face)face_); face_ = nullptr; }
    if (ft_) { FT_Done_FreeType((FT_Library)ft_); ft_ = nullptr; }
    init_ = false;
}
