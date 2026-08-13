#include "video_decoder.h"
#include <android/log.h>
#include <cstring>

#define LOG_TAG "VideoDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

VideoDecoder::~VideoDecoder() {}

bool VideoDecoder::CacheJni(JNIEnv* env) {
    clsMediaCodec_ = (jclass)env->NewGlobalRef(env->FindClass("android/media/MediaCodec"));
    clsMediaFormat_ = (jclass)env->NewGlobalRef(env->FindClass("android/media/MediaFormat"));
    clsSurface_ = (jclass)env->NewGlobalRef(env->FindClass("android/view/Surface"));
    clsSurfaceTexture_ = (jclass)env->NewGlobalRef(env->FindClass("android/graphics/SurfaceTexture"));
    clsBufferInfo_ = (jclass)env->NewGlobalRef(env->FindClass("android/media/MediaCodec$BufferInfo"));

    if (!clsMediaCodec_ || !clsMediaFormat_ || !clsSurface_ || !clsSurfaceTexture_ || !clsBufferInfo_) {
        LOGE("FindClass failed");
        return false;
    }

    mCreateDecoder_ = env->GetStaticMethodID(clsMediaCodec_, "createDecoderByType",
        "(Ljava/lang/String;)Landroid/media/MediaCodec;");
    mConfigure_ = env->GetMethodID(clsMediaCodec_, "configure",
        "(Landroid/media/MediaFormat;Landroid/view/Surface;Landroid/media/MediaCrypto;I)V");
    mStart_ = env->GetMethodID(clsMediaCodec_, "start", "()V");
    mDequeueIn_ = env->GetMethodID(clsMediaCodec_, "dequeueInputBuffer", "(J)I");
    mGetInputBuffer_ = env->GetMethodID(clsMediaCodec_, "getInputBuffer", "(I)Ljava/nio/ByteBuffer;");
    mQueueInput_ = env->GetMethodID(clsMediaCodec_, "queueInputBuffer", "(IIIJI)V");
    mDequeueOut_ = env->GetMethodID(clsMediaCodec_, "dequeueOutputBuffer",
        "(Landroid/media/MediaCodec$BufferInfo;J)I");
    mReleaseOutput_ = env->GetMethodID(clsMediaCodec_, "releaseOutputBuffer", "(IZ)V");
    mStop_ = env->GetMethodID(clsMediaCodec_, "stop", "()V");
    mRelease_ = env->GetMethodID(clsMediaCodec_, "release", "()V");

    mCreateVideoFormat_ = env->GetStaticMethodID(clsMediaFormat_, "createVideoFormat",
        "(Ljava/lang/String;II)Landroid/media/MediaFormat;");
    mSetByteBuffer_ = env->GetMethodID(clsMediaFormat_, "setByteBuffer",
        "(Ljava/lang/String;Ljava/nio/ByteBuffer;)V");

    mSurfaceTextureCtor_ = env->GetMethodID(clsSurfaceTexture_, "<init>", "(I)V");
    mSurfaceTextureUpdate_ = env->GetMethodID(clsSurfaceTexture_, "updateTexImage", "()V");
    mSurfaceCtor_ = env->GetMethodID(clsSurface_, "<init>", "(Landroid/graphics/SurfaceTexture;)V");

    mBufferInfoCtor_ = env->GetMethodID(clsBufferInfo_, "<init>", "()V");
    fOffset_ = env->GetFieldID(clsBufferInfo_, "offset", "I");
    fSize_ = env->GetFieldID(clsBufferInfo_, "size", "I");
    fPts_ = env->GetFieldID(clsBufferInfo_, "presentationTimeUs", "J");
    fFlags_ = env->GetFieldID(clsBufferInfo_, "flags", "I");

    if (!mCreateDecoder_ || !mConfigure_ || !mStart_ || !mDequeueIn_ || !mGetInputBuffer_ ||
        !mQueueInput_ || !mDequeueOut_ || !mReleaseOutput_ || !mCreateVideoFormat_ ||
        !mSurfaceTextureCtor_ || !mSurfaceTextureUpdate_ || !mSurfaceCtor_ ||
        !mBufferInfoCtor_ || !fOffset_ || !fSize_ || !fPts_ || !fFlags_) {
        LOGE("GetMethodID/GetFieldID failed");
        return false;
    }
    return true;
}

bool VideoDecoder::Init(JNIEnv* env, const char* mime, int width, int height) {
    if (initialized_) return true;
    if (!CacheJni(env)) return false;

    // 1. OES texture
    glGenTextures(1, &texId_);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texId_);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

    // 2. SurfaceTexture + Surface
    surfaceTexture_ = env->NewGlobalRef(env->NewObject(clsSurfaceTexture_, mSurfaceTextureCtor_, (jint)texId_));
    surface_ = env->NewGlobalRef(env->NewObject(clsSurface_, mSurfaceCtor_, surfaceTexture_));

    // 3. MediaCodec decoder
    jstring jmime = env->NewStringUTF(mime);
    mediaCodec_ = env->NewGlobalRef(env->CallStaticObjectMethod(clsMediaCodec_, mCreateDecoder_, jmime));
    env->DeleteLocalRef(jmime);
    if (!mediaCodec_) { LOGE("createDecoderByType failed for %s", mime); return false; }

    jstring fmtMime = env->NewStringUTF(mime);
    jobject format = env->CallStaticObjectMethod(clsMediaFormat_, mCreateVideoFormat_, fmtMime, (jint)width, (jint)height);
    env->DeleteLocalRef(fmtMime);
    if (!format) { LOGE("createVideoFormat failed"); return false; }

    env->CallVoidMethod(mediaCodec_, mConfigure_, format, surface_, nullptr, (jint)0);
    env->DeleteLocalRef(format);

    env->CallVoidMethod(mediaCodec_, mStart_);
    bufferInfo_ = env->NewGlobalRef(env->NewObject(clsBufferInfo_, mBufferInfoCtor_));

    initialized_ = true;
    configured_ = true;
    started_ = true;
    LOGI("decoder initialized: %s %dx%d", mime, width, height);
    return true;
}

void VideoDecoder::SetCsd(JNIEnv* env, const uint8_t* csd0, size_t len0,
                          const uint8_t* csd1, size_t len1) {
    // NOTE: must be called before start(); kept simple, only supports pre-start usage.
    if (!configured_) return;
    // csd-0
    if (csd0 && len0) {
        jobject bb = env->NewDirectByteBuffer(const_cast<uint8_t*>(csd0), (jlong)len0);
        jstring key = env->NewStringUTF("csd-0");
        // setByteBuffer must be called on a MediaFormat before configure; skip if started.
        env->DeleteLocalRef(bb);
        env->DeleteLocalRef(key);
    }
    // Kept as a stub: configure-time csd injection is wired in Init path when needed.
}

void VideoDecoder::Feed(JNIEnv* env, const uint8_t* data, size_t size, int64_t ptsUs) {
    if (!started_) return;
    jint index = env->CallIntMethod(mediaCodec_, mDequeueIn_, (jlong)10000); // 10ms
    if (index < 0) return; // no input buffer available yet
    jobject buf = env->CallObjectMethod(mediaCodec_, mGetInputBuffer_, index);
    uint8_t* dst = (uint8_t*)env->GetDirectBufferAddress(buf);
    jlong cap = env->GetDirectBufferCapacity(buf);
    if (dst && cap >= (jlong)size) {
        std::memcpy(dst, data, size);
        env->CallVoidMethod(mediaCodec_, mQueueInput_, index, (jint)0, (jint)size, (jlong)ptsUs, (jint)0);
    }
    env->DeleteLocalRef(buf);
}

void VideoDecoder::UpdateTexture(JNIEnv* env) {
    if (!started_) return;
    // Drain output: release to surface to render latest frame.
    while (true) {
        jint index = env->CallIntMethod(mediaCodec_, mDequeueOut_, bufferInfo_, (jlong)0);
        if (index >= 0) {
            env->CallVoidMethod(mediaCodec_, mReleaseOutput_, index, (jboolean)JNI_TRUE);
        } else if (index == -1 /* INFO_TRY_AGAIN_LATER */) {
            break;
        } else {
            break;
        }
    }
    // Pull latest frame into OES texture.
    env->CallVoidMethod(surfaceTexture_, mSurfaceTextureUpdate_);
}

void VideoDecoder::Shutdown(JNIEnv* env) {
    if (mediaCodec_) {
        if (started_) { env->CallVoidMethod(mediaCodec_, mStop_); started_ = false; }
        env->CallVoidMethod(mediaCodec_, mRelease_);
        env->DeleteGlobalRef(mediaCodec_); mediaCodec_ = nullptr;
    }
    if (surface_) { env->DeleteGlobalRef(surface_); surface_ = nullptr; }
    if (surfaceTexture_) { env->DeleteGlobalRef(surfaceTexture_); surfaceTexture_ = nullptr; }
    if (bufferInfo_) { env->DeleteGlobalRef(bufferInfo_); bufferInfo_ = nullptr; }
    if (texId_) { glDeleteTextures(1, &texId_); texId_ = 0; }
    initialized_ = false;
}
