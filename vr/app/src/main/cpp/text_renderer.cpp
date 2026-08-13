#include "text_renderer.h"

#include <vector>
#include <cmath>

#include <android/log.h>

#define LOG_TAG "TextRenderer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

bool TextRenderer::CacheJni(JNIEnv* env) {
    clsBitmap_ = (jclass)env->NewGlobalRef(env->FindClass("android/graphics/Bitmap"));
    clsCanvas_ = (jclass)env->NewGlobalRef(env->FindClass("android/graphics/Canvas"));
    clsPaint_ = (jclass)env->NewGlobalRef(env->FindClass("android/graphics/Paint"));
    if (!clsBitmap_ || !clsCanvas_ || !clsPaint_) { LOGE("FindClass failed"); return false; }

    fArgb8888_ = env->GetStaticFieldID(clsBitmap_, "ARGB_8888", "Landroid/graphics/Bitmap$Config;");
    mCreateBitmap_ = env->GetStaticMethodID(clsBitmap_, "createBitmap",
        "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;");
    mGetPixels_ = env->GetMethodID(clsBitmap_, "getPixels", "([IIIIII)V");
    mCanvasCtor_ = env->GetMethodID(clsCanvas_, "<init>", "(Landroid/graphics/Bitmap;)V");
    mDrawText_ = env->GetMethodID(clsCanvas_, "drawText",
        "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");
    mPaintCtor_ = env->GetMethodID(clsPaint_, "<init>", "()V");
    mSetTextSize_ = env->GetMethodID(clsPaint_, "setTextSize", "(F)V");
    mSetColor_ = env->GetMethodID(clsPaint_, "setColor", "(I)V");
    mSetAntiAlias_ = env->GetMethodID(clsPaint_, "setAntiAlias", "(Z)V");
    mMeasureText_ = env->GetMethodID(clsPaint_, "measureText", "(Ljava/lang/String;)F");

    if (!fArgb8888_ || !mCreateBitmap_ || !mGetPixels_ || !mCanvasCtor_ || !mDrawText_ ||
        !mPaintCtor_ || !mSetTextSize_ || !mSetColor_ || !mSetAntiAlias_ || !mMeasureText_) {
        LOGE("GetMethodID failed");
        return false;
    }
    return true;
}

bool TextRenderer::Init(JNIEnv* env) {
    if (init_) return true;
    env_ = env;
    if (!CacheJni(env)) return false;
    init_ = true;
    LOGI("text renderer initialized");
    return true;
}

GLuint TextRenderer::RenderText(const std::string& text, int& outW, int& outH, uint32_t rgba) {
    if (!init_ || !env_) return 0;

    const float textSize = 48.0f;
    jobject paint = env_->NewObject(clsPaint_, mPaintCtor_);
    env_->CallVoidMethod(paint, mSetTextSize_, (jfloat)textSize);
    env_->CallVoidMethod(paint, mSetAntiAlias_, (jboolean)JNI_TRUE);
    // rgba is 0xRRGGBBAA; Android setColor wants 0xAARRGGBB.
    uint32_t a = rgba & 0xFF, r = (rgba >> 24) & 0xFF, g = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF;
    env_->CallVoidMethod(paint, mSetColor_, (jint)((a << 24) | (r << 16) | (g << 8) | b));

    jstring jtext = env_->NewStringUTF(text.c_str());
    float tw = env_->CallFloatMethod(paint, mMeasureText_, jtext);
    int w = (int)std::ceil(tw) + 8;
    int h = (int)(textSize * 1.4f);
    if (w <= 0 || h <= 0) { env_->DeleteLocalRef(jtext); env_->DeleteLocalRef(paint); return 0; }

    jobject config = env_->GetStaticObjectField(clsBitmap_, fArgb8888_);
    jobject bitmap = env_->CallStaticObjectMethod(clsBitmap_, mCreateBitmap_, (jint)w, (jint)h, config);
    env_->DeleteLocalRef(config);
    if (!bitmap) { env_->DeleteLocalRef(jtext); env_->DeleteLocalRef(paint); return 0; }

    jobject canvas = env_->NewObject(clsCanvas_, mCanvasCtor_, bitmap);
    env_->CallVoidMethod(canvas, mDrawText_, jtext, (jfloat)4.0f, (jfloat)(textSize), paint);

    jintArray arr = env_->NewIntArray(w * h);
    env_->CallVoidMethod(bitmap, mGetPixels_, arr, (jint)0, (jint)w, (jint)0, (jint)0, (jint)w, (jint)h);
    jint* pixels = env_->GetIntArrayElements(arr, nullptr);

    std::vector<uint8_t> rgbaBuf((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        jint p = pixels[i];
        rgbaBuf[i * 4 + 0] = (p >> 16) & 0xFF;  // R
        rgbaBuf[i * 4 + 1] = (p >> 8) & 0xFF;   // G
        rgbaBuf[i * 4 + 2] = p & 0xFF;          // B
        rgbaBuf[i * 4 + 3] = (p >> 24) & 0xFF;  // A
    }
    env_->ReleaseIntArrayElements(arr, pixels, 0);

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgbaBuf.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    env_->DeleteLocalRef(canvas);
    env_->DeleteLocalRef(bitmap);
    env_->DeleteLocalRef(arr);
    env_->DeleteLocalRef(jtext);
    env_->DeleteLocalRef(paint);

    outW = w;
    outH = h;
    return tex;
}

void TextRenderer::Shutdown() {
    if (clsBitmap_) { env_->DeleteGlobalRef(clsBitmap_); clsBitmap_ = nullptr; }
    if (clsCanvas_) { env_->DeleteGlobalRef(clsCanvas_); clsCanvas_ = nullptr; }
    if (clsPaint_) { env_->DeleteGlobalRef(clsPaint_); clsPaint_ = nullptr; }
    init_ = false;
}
