#pragma once
#include <GLES3/gl3.h>
#include <cstdint>
#include <string>

// Renders text (incl. CJK) to an OpenGL RGBA texture using FreeType.
class TextRenderer {
public:
    bool Init();
    // Render text to a new GL texture (RGBA). Returns 0 on failure.
    // outW/outH receive the texture size in pixels.
    GLuint RenderText(const std::string& text, int& outW, int& outH, uint32_t rgba = 0xFFFFFFFF);
    void Shutdown();
    bool IsInit() const { return init_; }

private:
    void* ft_ = nullptr;    // FT_Library
    void* face_ = nullptr;  // FT_Face
    bool init_ = false;
};
