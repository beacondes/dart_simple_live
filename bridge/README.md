# 数据桥接 (Data Bridge)

连接 Dart 数据层（simple_live_core）与 C++ VR 前端（vr/）的 TCP 桥接。

## 架构

```
[Quest 3 C++ VR 前端]  --TCP-->  [Dart 桥接服务器 (simple_live_core)]
     (vr/)                              (bridge/bridge_server.dart)
     下载直播流 + 渲染3D弹幕              解析直播流地址 + 转发弹幕
```

## 协议（换行分隔文本）

| 方向 | 消息 | 说明 |
|------|------|------|
| C -> S | `WATCH <platform> <roomId>` | 请求观看某平台某房间 |
| C -> S | `STOP` | 停止 |
| S -> C | `HELLO` | 连接建立 |
| S -> C | `STREAM <url>` | 直播流播放地址 |
| S -> C | `DANMAKU <user>|<text>` | 弹幕消息 |
| S -> C | `READY` / `CLOSE` | 弹幕连接状态 |
| S -> C | `ERROR <msg>` | 错误 |

## 运行桥接服务器

```bash
# 在仓库根目录（需 simple_live_core 依赖可解析）
dart run bridge/bridge_server.dart 9527
```

VR 前端默认连接 `127.0.0.1:9527`（见 vr/app/src/main/cpp/main.cpp 中的
g_bridge.Connect）。若桥接服务器运行在另一台机器，需把该地址改为
桥接服务器的局域网 IP。

## 当前状态

- [x] C++ TCP 客户端 (data_bridge.h/.cpp)
- [x] Dart 桥接服务器（直播流地址解析 + 弹幕转发参考实现）
- [ ] C++ 端下载直播流 + FLV 解封装（下一步）
- [ ] 弹幕 start() 平台相关 args 精确对接
