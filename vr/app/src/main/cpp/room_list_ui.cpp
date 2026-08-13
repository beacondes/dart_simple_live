#include "room_list_ui.h"

#include "vr_math.h"

#include <cmath>
#include <sstream>

#include <android/log.h>

#define LOG_TAG "RoomListUI"
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
    if (!ok) { LOGE("shader compile failed"); glDeleteShader(sh); return 0; }
    return sh;
}

bool RoomListUI::Init(TextRenderer* text) {
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
    LOGI("room list UI initialized");
    return true;
}

void RoomListUI::AddRoom(const std::string& roomId, const std::string& title,
                         const std::string& userName, const std::string& online) {
    if (!init_ || !text_ || rooms_.size() >= 12) return;
    std::string label = title + "  [" + userName + "]  " + online;
    int w = 0, h = 0;
    GLuint tex = text_->RenderText(label, w, h);
    if (!tex) return;

    Room r;
    r.roomId = roomId;
    r.texture = tex;
    r.w = w;
    r.h = h;
    const float ppm = 0.003f;
    r.halfW = w * ppm * 0.5f;
    r.halfH = h * ppm * 0.5f;
    r.x = 0.0f;
    r.z = -2.5f;
    r.y = 0.7f - (float)rooms_.size() * 0.14f;
    rooms_.push_back(r);
}

void RoomListUI::Clear() {
    for (auto& r : rooms_) if (r.texture) glDeleteTextures(1, &r.texture);
    rooms_.clear();
}

std::string RoomListUI::Update(const XrPosef& viewPose, float dt) {
    if (!init_ || rooms_.empty()) return "";

    // Gaze direction = view forward (local -Z).
    Mat4 rot = QuatToMat4(viewPose.orientation);
    float fx = -rot.m[8], fy = -rot.m[9], fz = -rot.m[10];
    if (std::fabs(fz) < 1e-6f) return "";

    std::string selected;
    for (auto& r : rooms_) {
        float t = (r.z - viewPose.position.z) / fz;
        if (t <= 0.0f) { r.gazeTime = 0.0f; continue; }
        float hx = viewPose.position.x + t * fx;
        float hy = viewPose.position.y + t * fy;
        bool hit = std::fabs(hx - r.x) < r.halfW + 0.02f && std::fabs(hy - r.y) < r.halfH + 0.02f;
        if (hit) {
            r.gazeTime += dt;
            if (r.gazeTime >= dwellThreshold_) selected = r.roomId;
        } else {
            r.gazeTime = 0.0f;
        }
    }
    return selected;
}

void RoomListUI::Render(const XrPosef& viewPose, const XrFovf& fov) {
    if (!init_ || rooms_.empty()) return;

    Mat4 proj = XrFovToProjectionMatrix(fov, 0.1f, 100.0f);
    Mat4 view = XrPoseToViewMatrix(viewPose);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(uTex_, 0);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    for (auto& r : rooms_) {
        Mat4 model;
        model.SetIdentity();
        model.m[0] = r.halfW;
        model.m[5] = r.halfH;
        model.m[12] = r.x;
        model.m[13] = r.y;
        model.m[14] = r.z;
        Mat4 mvp = Mat4::Mul(Mat4::Mul(proj, view), model);
        glBindTexture(GL_TEXTURE_2D, r.texture);
        glUniformMatrix4fv(uMvp_, 1, GL_FALSE, mvp.m);
        glBindVertexArray(vao_);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
    glUseProgram(0);
}

void RoomListUI::Shutdown() {
    Clear();
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    init_ = false;
}
