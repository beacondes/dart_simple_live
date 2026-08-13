# SimpleLive VR (Quest 3)

基于 dart_simple_live 的原生 OpenXR 版本，面向 Meta Quest 3 的多平台直播聚合应用，支持 3D 弹幕。

## 架构
- simple_live_core (Dart)：直播源解析 + 弹幕流解析（数据后端）
- vr/ (C++/OpenXR)：原生 VR 渲染前端（视频解码 + 3D 弹幕渲染）

## 依赖
- Android SDK / NDK r26 / CMake 3.22+
- Khronos OpenXR SDK：vr/third_party/OpenXR-SDK-Source
- Gradle 8.7 / AGP 8.5

## 本地构建
cd vr
git clone --depth 1 --branch release-1.0.34 https://github.com/KhronosGroup/OpenXR-SDK-Source.git third_party/OpenXR-SDK-Source
gradle assembleRelease

## 侧载到 Quest 3
adb install app/build/outputs/apk/release/app-release.apk

## CI
.github/workflows/build-vr.yml 在每次 push 时远程编译并产出 APK artifact。

## 路线图
- [x] 工程骨架 + CI 编译流水线
- [ ] OpenXR 会话 + 双目立体渲染
- [ ] MediaCodec 视频解码管线
- [ ] Dart 数据桥接（拉流 + 弹幕）
- [ ] 3D 弹幕渲染
