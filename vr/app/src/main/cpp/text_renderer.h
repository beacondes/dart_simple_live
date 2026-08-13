#pragma once
#include <GLES3/gl3.h>
#include <jni.h>
#include <cstdint>
#include <string>

// Renders text (incl. CJK) to a GL texture using Android Canvas/Bitmap via JNI.
class TextRenderer {
public:
    bool Init(JNIEnv* env);
    GLuint RenderText(const std::string& text, int& outW, int& outH, uint32_t rgba = 0xFFFFFFFF);
    void Shutdown();
    bool IsInit() const { return init_; }

private:
    bool CacheJni(JNIEnv* env);

    JNIEnv* env_ = nullptr;
    jclass clsBitmap_ = nullptr;
    jclass clsCanvas_ = nullptr;
    jclass clsPaint_ = nullptr;
    jfieldID fArgb8888_ = nullptr;
    jmethodID mCreateBitmap_ = nullptr;
    jmethodID mGetPixels_ = nullptr;
    jmethodID mCanvasCtor_ = nullptr;
    jmethodID mDrawText_ = nullptr;
    jmethodID mPaintCtor_ = nullptr;
    jmethodID mSetTextSize_ = nullptr;
    jmethodID mSetColor_ = nullptr;
    jmethodID mSetAntiAlias_ = nullptr;
    jmethodID mMeasureText_ = nullptr;
    bool init_ = false;
};
