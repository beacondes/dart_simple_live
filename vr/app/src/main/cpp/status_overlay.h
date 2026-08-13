#pragma once
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <string>

#include "text_renderer.h"

// Renders a single status line in VR space (on-screen debug / error text).
class StatusOverlay {
public:
    bool Init(TextRenderer* text);
    void SetStatus(const std::string& text);
    void Render(const XrPosef& viewPose, const XrFovf& fov);
    void Shutdown();

private:
    TextRenderer* text_ = nullptr;
    GLuint texture_ = 0;
    int w_ = 0, h_ = 0;
    std::string status_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint uMvp_ = -1, uTex_ = -1;
    bool init_ = false;
};
