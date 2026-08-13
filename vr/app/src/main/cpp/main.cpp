#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#define XR_USE_GRAPHICS_API_OPENGL_ES
#define XR_USE_PLATFORM_ANDROID
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

#include "video_decoder.h"
#include "video_renderer.h"
#include "data_bridge.h"
#include "stream_downloader.h"
#include "text_renderer.h"
#include "danmaku_renderer.h"
#include "room_list_ui.h"
#include "xr_input.h"

#define LOG_TAG "SimpleLiveVR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

XrInstance g_instance = XR_NULL_HANDLE;
XrSession g_session = XR_NULL_HANDLE;
XrSpace g_space = XR_NULL_HANDLE;
XrSessionState g_sessionState = XR_SESSION_STATE_UNKNOWN;
bool g_running = false;

EGLDisplay g_eglDisplay = EGL_NO_DISPLAY;
EGLContext g_eglContext = EGL_NO_CONTEXT;
EGLSurface g_eglSurface = EGL_NO_SURFACE;
EGLConfig g_eglConfig = nullptr;

VideoDecoder g_decoder;
VideoRenderer g_renderer;
JNIEnv* g_env = nullptr;
bool g_rendererReady = false;
DataBridge g_bridge;
StreamDownloader g_downloader;
JavaVM* g_vm = nullptr;
TextRenderer g_text;
DanmakuRenderer g_danmaku;
RoomListUI g_roomList;
XrInput g_input;

struct Swapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    int32_t width = 0;
    int32_t height = 0;
    std::vector<XrSwapchainImageOpenGLESKHR> images;
    std::vector<GLuint> framebuffers;
};
std::vector<Swapchain> g_swapchains;
std::vector<XrView> g_views;
std::vector<XrViewConfigurationView> g_configViews;

bool CheckXr(XrResult result, const char* what) {
    if (XR_SUCCEEDED(result)) return true;
    char buf[XR_MAX_RESULT_STRING_SIZE] = {0};
    xrResultToString(g_instance, result, buf);
    LOGE("%s failed: %s", what, buf);
    return false;
}

bool InitEGL(android_app* app) {
    g_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_eglDisplay == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }
    if (!eglInitialize(g_eglDisplay, nullptr, nullptr)) { LOGE("eglInitialize failed"); return false; }

    const EGLint attribs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    EGLint numConfigs = 0;
    if (!eglChooseConfig(g_eglDisplay, attribs, &g_eglConfig, 1, &numConfigs) || numConfigs < 1) {
        LOGE("eglChooseConfig failed"); return false;
    }
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    g_eglContext = eglCreateContext(g_eglDisplay, g_eglConfig, EGL_NO_CONTEXT, ctxAttribs);
    if (g_eglContext == EGL_NO_CONTEXT) { LOGE("eglCreateContext failed"); return false; }
    g_eglSurface = eglCreateWindowSurface(g_eglDisplay, g_eglConfig, app->window, nullptr);
    if (g_eglSurface == EGL_NO_SURFACE) { LOGE("eglCreateWindowSurface failed"); return false; }
    if (!eglMakeCurrent(g_eglDisplay, g_eglSurface, g_eglSurface, g_eglContext)) {
        LOGE("eglMakeCurrent failed"); return false;
    }
    LOGI("EGL initialized");
    return true;
}

bool InitOpenXR(android_app* app) {
    uint32_t extCount = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &extCount, nullptr);
    std::vector<XrExtensionProperties> exts(extCount, {XR_TYPE_EXTENSION_PROPERTIES});
    xrEnumerateInstanceExtensionProperties(nullptr, extCount, &extCount, exts.data());

    bool hasGLES = false;
    for (auto& e : exts) {
        if (strcmp(e.extensionName, XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME) == 0) hasGLES = true;
    }
    if (!hasGLES) { LOGE("XR_KHR_opengl_es_enable not supported"); return false; }

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy(createInfo.applicationInfo.applicationName, "SimpleLive VR");
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 0;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    const char* enabledExts[] = { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME };
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExts;
    if (!CheckXr(xrCreateInstance(&createInfo, &g_instance), "xrCreateInstance")) return false;

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId systemId;
    if (!CheckXr(xrGetSystem(g_instance, &systemInfo, &systemId), "xrGetSystem")) return false;

    XrGraphicsBindingOpenGLESAndroidKHR gfxBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gfxBinding.display = g_eglDisplay;
    gfxBinding.config = g_eglConfig;
    gfxBinding.context = g_eglContext;

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &gfxBinding;
    sessionInfo.systemId = systemId;
    if (!CheckXr(xrCreateSession(g_instance, &sessionInfo, &g_session), "xrCreateSession")) return false;

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    spaceInfo.poseInReferenceSpace.position = {0.0f, 0.0f, 0.0f};
    if (!CheckXr(xrCreateReferenceSpace(g_session, &spaceInfo, &g_space), "xrCreateReferenceSpace")) return false;

    XrViewConfigurationType viewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(g_instance, systemId, viewType, 0, &viewCount, nullptr);
    if (viewCount < 2) { LOGE("expected stereo view configuration"); return false; }
    g_configViews.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    xrEnumerateViewConfigurationViews(g_instance, systemId, viewType, viewCount, &viewCount, g_configViews.data());
    g_views.resize(viewCount, {XR_TYPE_VIEW});

    for (uint32_t i = 0; i < viewCount; i++) {
        XrSwapchainCreateInfo scInfo{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        scInfo.arraySize = 1;
        scInfo.format = GL_RGBA8;
        scInfo.width = g_configViews[i].recommendedImageRectWidth;
        scInfo.height = g_configViews[i].recommendedImageRectHeight;
        scInfo.mipCount = 1;
        scInfo.faceCount = 1;
        scInfo.sampleCount = 1;
        scInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;

        Swapchain sc;
        if (!CheckXr(xrCreateSwapchain(g_session, &scInfo, &sc.handle), "xrCreateSwapchain")) return false;

        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(sc.handle, 0, &imgCount, nullptr);
        sc.images.resize(imgCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        xrEnumerateSwapchainImages(sc.handle, imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(sc.images.data()));
        sc.framebuffers.resize(imgCount);
        for (uint32_t j = 0; j < imgCount; j++) {
            GLuint fb = 0;
            glGenFramebuffers(1, &fb);
            glBindFramebuffer(GL_FRAMEBUFFER, fb);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sc.images[j].image, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            sc.framebuffers[j] = fb;
        }
        sc.width = (int32_t)scInfo.width;
        sc.height = (int32_t)scInfo.height;
        g_swapchains.push_back(sc);
    }
    LOGI("OpenXR initialized (%u views)", viewCount);
    return true;
}

void RenderFrame() {
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    xrWaitFrame(g_session, nullptr, &frameState);
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame(g_session, &beginInfo);

    std::vector<XrCompositionLayerBaseHeader*> layers;
    if (frameState.shouldRender) {
        XrViewLocateInfo viewInfo{XR_TYPE_VIEW_LOCATE_INFO};
        viewInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        viewInfo.displayTime = frameState.predictedDisplayTime;
        viewInfo.space = g_space;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        xrLocateViews(g_session, &viewInfo, &viewState, (uint32_t)g_views.size(), &viewCount, g_views.data());

        if (g_rendererReady && g_decoder.IsInit() && g_env) {
            g_decoder.UpdateTexture(g_env);
        }
        g_input.Sync();
        XrPosef aimPose{};
        if (g_input.GetAimPose(g_space, frameState.predictedDisplayTime, aimPose)) {
            std::string selCtrl = g_roomList.UpdateWithController(aimPose, g_input.IsTriggerPressed(), 0.016f);
            if (!selCtrl.empty()) {
                g_bridge.Send(("WATCH bilibili " + selCtrl).c_str());
                g_roomList.Clear();
            }
        }
        g_danmaku.Update(0.016f);
        std::string sel = g_roomList.Update(g_views[0].pose, 0.016f);
        if (!sel.empty()) {
            g_bridge.Send(("WATCH bilibili " + sel).c_str());
            g_roomList.Clear();
        }

        std::vector<XrCompositionLayerProjectionView> projViews(g_views.size());
        for (uint32_t i = 0; i < viewCount; i++) {
            Swapchain& sc = g_swapchains[i];

            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t index = 0;
            xrAcquireSwapchainImage(sc.handle, &acquireInfo, &index);
            XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            waitInfo.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(sc.handle, &waitInfo);

            glBindFramebuffer(GL_FRAMEBUFFER, sc.framebuffers[index]);
            glViewport(0, 0, sc.width, sc.height);
            glClearColor(0.08f, 0.10f, 0.14f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            if (g_rendererReady) {
                g_renderer.Render(g_decoder.GetTexture(), g_views[i].pose, g_views[i].fov);
                g_danmaku.Render(g_views[i].pose, g_views[i].fov);
                g_roomList.Render(g_views[i].pose, g_views[i].fov);
            }

            glBindFramebuffer(GL_FRAMEBUFFER, 0);

            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            xrReleaseSwapchainImage(sc.handle, &releaseInfo);

            projViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projViews[i].pose = g_views[i].pose;
            projViews[i].fov = g_views[i].fov;
            projViews[i].subImage.swapchain = sc.handle;
            projViews[i].subImage.imageRect.offset = {0, 0};
            projViews[i].subImage.imageRect.extent = {sc.width, sc.height};
            projViews[i].subImage.imageArrayIndex = 0;
        }

        XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
        layer.space = g_space;
        layer.viewCount = (uint32_t)projViews.size();
        layer.views = projViews.data();
        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&layer));
    }

    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = (uint32_t)layers.size();
    endInfo.layers = layers.data();
    xrEndFrame(g_session, &endInfo);
}

void PollEvents() {
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(g_instance, &event) == XR_SUCCESS) {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            auto* se = reinterpret_cast<XrEventDataSessionStateChanged*>(&event);
            g_sessionState = se->state;
            LOGI("session state -> %d", (int)se->state);
            if (se->state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(g_session, &begin);
                g_running = true;
            } else if (se->state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(g_session);
                g_running = false;
            } else if (se->state == XR_SESSION_STATE_EXITING ||
                       se->state == XR_SESSION_STATE_LOSS_PENDING) {
                g_running = false;
            }
        }
        event.type = XR_TYPE_EVENT_DATA_BUFFER;
    }
}

void Cleanup() {
    g_downloader.Stop();
    g_decoder.Shutdown(g_env);
    g_renderer.Shutdown();
    g_danmaku.Shutdown();
    g_roomList.Shutdown();
    g_input.Shutdown();
    g_text.Shutdown();
    for (auto& sc : g_swapchains) {
        for (GLuint fb : sc.framebuffers) glDeleteFramebuffers(1, &fb);
        if (sc.handle != XR_NULL_HANDLE) xrDestroySwapchain(sc.handle);
    }
    if (g_space != XR_NULL_HANDLE) xrDestroySpace(g_space);
    if (g_session != XR_NULL_HANDLE) xrDestroySession(g_session);
    if (g_instance != XR_NULL_HANDLE) xrDestroyInstance(g_instance);
    if (g_eglDisplay != EGL_NO_DISPLAY) {
        eglMakeCurrent(g_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_eglSurface != EGL_NO_SURFACE) eglDestroySurface(g_eglDisplay, g_eglSurface);
        if (g_eglContext != EGL_NO_CONTEXT) eglDestroyContext(g_eglDisplay, g_eglContext);
        eglTerminate(g_eglDisplay);
    }
}

}  // namespace

void android_main(struct android_app* app) {
    app_dummy();

    JNIEnv* env = nullptr;
    app->activity->vm->AttachCurrentThread(&env, nullptr);
    g_env = env;
    g_vm = app->activity->vm;

    bool initialized = false;
    LOGI("android_main started");
    while (!app->destroyRequested) {
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source != nullptr) source->process(app, source);
            if (app->destroyRequested) break;
        }
        if (app->destroyRequested) break;

        if (!initialized && app->window != nullptr) {
            LOGI("window ready, initializing EGL + OpenXR...");
            initialized = InitEGL(app) && InitOpenXR(app);
            if (initialized) {
                g_rendererReady = g_renderer.Init();
                if (g_text.Init(env)) { g_danmaku.Init(&g_text); g_roomList.Init(&g_text); }
                g_input.Init(g_instance, g_session);
                if (g_rendererReady) {
                    // Hardcoded H.264 1080p decoder; real stream source is the next milestone.
                    g_decoder.Init(env, "video/avc", 1920, 1080);
                if (g_bridge.Connect("127.0.0.1", 9527)) {
                    g_bridge.Send("LIST bilibili");
                }
                }
            }
            if (!initialized) { LOGE("init failed"); break; }
        }
        if (initialized) {
            PollEvents();
            if (g_running) RenderFrame();
            std::string btype;
            std::vector<std::string> bf;
            while (g_bridge.Poll(btype, bf)) {
                if (btype == "STREAM") {
                    LOGI("bridge: stream = %s", bf.empty() ? "" : bf[0].c_str());
                    if (!bf.empty()) g_downloader.Start(bf[0].c_str(), g_vm, &g_decoder);
                }
                else if (btype == "DANMAKU") {
                    std::string du = bf.size() > 0 ? bf[0] : "";
                    std::string dtxt = bf.size() > 1 ? bf[1] : "";
                    g_danmaku.AddDanmaku(du, dtxt);
                }
                else if (btype == "ROOM") {
                    g_roomList.AddRoom(bf.size() > 0 ? bf[0] : "", bf.size() > 1 ? bf[1] : "", bf.size() > 2 ? bf[2] : "", bf.size() > 3 ? bf[3] : "");
                }
                else LOGI("bridge: %s", btype.c_str());
            }
        }
    }
    Cleanup();
}
