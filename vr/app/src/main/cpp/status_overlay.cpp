#include "status_overlay.h"

#include "vr_math.h"

#include <android/log.h>

#define LOG_TAG "StatusOverlay"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static const char* kVs = R"GLSL(#version 300 es
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
uniform mat4 uMvp;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = uMvp * vec4(aPos, 1.0);
}
)GLSL";

static const char* kFs = R"GLSL(#version 300 es
precision mediump float;
uniform sampler2D uTex;
in vec2 vUV;
out vec4 fragColor;
void main() {
    fragColor = texture(uTex, vUV);
}
)GLSL";

static GLuint CompileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { glDeleteShader(sh); return 0; }
    return sh;
}

bool StatusOverlay::Init(TextRenderer* text) {
    text_ = text;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVs);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFs);
    if (!vs || !fs) return false;
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) { LOGE("link failed"); return false; }
    uMvp_ = glGetUniformLocation(program_, "uMvp");
    uTex_ = glGetUniformLocation(program_, "uTex");

    const float verts[] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f, -1.0f, 0.0f,  1.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,  0.0f, 1.0f,
         1.0f,  1.0f, 0.0f,  1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f,  0.0f, 0.0f,
    };
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    init_ = true;
    return true;
}

void StatusOverlay::SetStatus(const std::string& text) {
    if (!init_ || !text_) return;
    if (text == status_) return;
    status_ = text;
    if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
    texture_ = text_->RenderText(text, w_, h_, 0xFFFFD700);  // gold
}

void StatusOverlay::Render(const XrPosef& viewPose, const XrFovf& fov) {
    if (!init_ || !texture_) return;
    Mat4 proj = XrFovToProjectionMatrix(fov, 0.1f, 100.0f);
    Mat4 view = XrPoseToViewMatrix(viewPose);

    Mat4 model;
    model.SetIdentity();
    const float ppm = 0.0025f;
    model.m[0] = w_ * ppm * 0.5f;
    model.m[5] = h_ * ppm * 0.5f;
    model.m[12] = 0.0f;
    model.m[13] = -0.5f;
    model.m[14] = -1.5f;
    Mat4 mvp = Mat4::Mul(Mat4::Mul(proj, view), model);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uTex_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void StatusOverlay::Shutdown() {
    if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    init_ = false;
}
