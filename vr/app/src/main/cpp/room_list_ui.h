#pragma once
#include <GLES3/gl3.h>
#include <openxr/openxr.h>
#include <string>
#include <vector>

#include "text_renderer.h"

// Renders a vertical room list in VR space and supports gaze-based selection.
class RoomListUI {
public:
    bool Init(TextRenderer* text);
    void AddRoom(const std::string& roomId, const std::string& title,
                 const std::string& userName, const std::string& online);
    void Clear();
    // Gaze-based selection. Returns a non-empty roomId once gaze dwell triggers.
    std::string Update(const XrPosef& viewPose, float dt);
    void Render(const XrPosef& viewPose, const XrFovf& fov);
    void Shutdown();

private:
    struct Room {
        std::string roomId;
        GLuint texture = 0;
        int w = 0, h = 0;
        float x = 0.0f, y = 0.0f, z = -2.5f;
        float halfW = 0.0f, halfH = 0.0f;
        float gazeTime = 0.0f;
    };

    std::vector<Room> rooms_;
    TextRenderer* text_ = nullptr;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint uMvp_ = -1, uTex_ = -1;
    bool init_ = false;
    float dwellThreshold_ = 1.5f;
};
