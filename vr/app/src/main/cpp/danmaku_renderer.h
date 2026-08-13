#pragma once
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <string>
#include <vector>

#include "text_renderer.h"

// Renders floating danmaku (bullet comments) as textured quads in VR space.
class DanmakuRenderer {
public:
    bool Init(TextRenderer* text);
    void AddDanmaku(const std::string& user, const std::string& text, uint32_t rgba = 0xFFFFFFFF);
    void Update(float dt);
    void Render(const XrPosef& viewPose, const XrFovf& fov);
    void Shutdown();

private:
    struct Item {
        GLuint texture = 0;
        int w = 0, h = 0;
        float x = 0.0f, y = 0.0f, z = -2.0f;
        float vx = -0.5f;
    };

    std::vector<Item> items_;
    TextRenderer* text_ = nullptr;
    GLuint program_ = 0;
    GLuint vao_ = 0, vbo_ = 0;
    GLint uMvp_ = -1, uTex_ = -1;
    bool init_ = false;
};
