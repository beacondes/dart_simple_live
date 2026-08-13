#pragma once
#include <GLES3/gl3.h>
#include <GLES3/gl2ext.h>
#include <openxr/openxr.h>

// Renders a textured quad (OES external texture from SurfaceTexture) in VR space.
class VideoRenderer {
public:
    bool Init();
    // Draw the video quad for one eye using the view pose + fov.
    void Render(GLuint oesTexture, const XrPosef& viewPose, const XrFovf& fov);
    void Shutdown();

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint uMvp_ = -1;
    GLint uTex_ = -1;
    bool init_ = false;
};
