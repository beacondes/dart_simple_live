cmake_minimum_required(VERSION 3.22.1)
project(simplelivevr LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NativeActivity glue
add_library(native_app_glue STATIC
    ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
target_include_directories(native_app_glue PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue)

# OpenXR SDK (headers + loader)
set(OPENXR_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../third_party/OpenXR-SDK-Source")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_LOADER ON CACHE BOOL "" FORCE)
add_subdirectory(${OPENXR_SDK_SOURCE_DIR} openxr-sdk)

# Main library
add_library(simplelivevr SHARED
    main.cpp
    video_decoder.cpp
    video_renderer.cpp
    data_bridge.cpp
    flv_demuxer.cpp
    stream_downloader.cpp
    text_renderer.cpp
    danmaku_renderer.cpp)
target_include_directories(simplelivevr PRIVATE
    ${OPENXR_SDK_SOURCE_DIR}/include
    ${ANDROID_NDK}/sources/android/native_app_glue)
target_link_libraries(simplelivevr
    native_app_glue
    openxr_loader
    android
    log
    EGL
    GLESv3)
