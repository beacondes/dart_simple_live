#pragma once
#include <jni.h>
#include <GLES3/gl3.h>
#include <GLES3/gl2ext.h>
#include <cstdint>
#include <cstddef>

// Hardware video decoder via Android MediaCodec, output to a SurfaceTexture
// whose OES texture can be sampled by the VR renderer.
class VideoDecoder {
public:
    VideoDecoder() = default;
    ~VideoDecoder();

    // env must already be attached to the current thread.
    bool Init(JNIEnv* env, const char* mime, int width, int height);
    void SetCsd(JNIEnv* env, const uint8_t* csd0, size_t len0,
                const uint8_t* csd1, size_t len1);
    // Feed compressed access units (e.g. H.264 NALs / Annex-B).
    void Feed(JNIEnv* env, const uint8_t* data, size_t size, int64_t ptsUs);
    // Pull the latest decoded frame into the OES texture (call on GL thread).
    void UpdateTexture(JNIEnv* env);
    void Shutdown(JNIEnv* env);

    GLuint GetTexture() const { return texId_; }
    bool IsInit() const { return initialized_; }

private:
    bool CacheJni(JNIEnv* env);

    jobject mediaCodec_ = nullptr;   // global ref
    jobject surface_ = nullptr;      // global ref
    jobject surfaceTexture_ = nullptr; // global ref
    jobject bufferInfo_ = nullptr;   // global ref
    GLuint texId_ = 0;
    bool initialized_ = false;
    bool configured_ = false;
    bool started_ = false;

    jclass clsMediaCodec_ = nullptr;
    jclass clsMediaFormat_ = nullptr;
    jclass clsSurface_ = nullptr;
    jclass clsSurfaceTexture_ = nullptr;
    jclass clsBufferInfo_ = nullptr;
    jmethodID mCreateDecoder_ = nullptr;
    jmethodID mConfigure_ = nullptr;
    jmethodID mStart_ = nullptr;
    jmethodID mDequeueIn_ = nullptr;
    jmethodID mGetInputBuffer_ = nullptr;
    jmethodID mQueueInput_ = nullptr;
    jmethodID mDequeueOut_ = nullptr;
    jmethodID mReleaseOutput_ = nullptr;
    jmethodID mStop_ = nullptr;
    jmethodID mRelease_ = nullptr;
    jmethodID mCreateVideoFormat_ = nullptr;
    jmethodID mSetByteBuffer_ = nullptr;
    jmethodID mSurfaceTextureCtor_ = nullptr;
    jmethodID mSurfaceTextureUpdate_ = nullptr;
    jmethodID mSurfaceCtor_ = nullptr;
    jmethodID mBufferInfoCtor_ = nullptr;
    jfieldID fOffset_ = nullptr;
    jfieldID fSize_ = nullptr;
    jfieldID fPts_ = nullptr;
    jfieldID fFlags_ = nullptr;
};
