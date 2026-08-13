// bridge/bridge_server.dart
// 数据桥接服务器（参考实现）——解析直播流地址 + 转发弹幕给 VR 前端。
//
// 运行（在仓库根目录，需要 simple_live_core 依赖可解析）：
//   dart run bridge/bridge_server.dart 9527
//
// 协议（换行分隔文本）：
//   客户端 -> 服务端:  WATCH <platform> <roomId>
//   服务端 -> 客户端:  STREAM <url> | DANMAKU <user>|<text> | READY | CLOSE | ERROR <msg>
//
// 支持的 platform: bilibili / douyu / huya / douyin / kuaishou
import 'dart:async';
import 'dart:convert';
import 'dart:io';

import 'package:simple_live_core/simple_live_core.dart';

LiveSite makeSite(String platform) {
  switch (platform.toLowerCase()) {
    case 'bilibili':
      return BilibiliSite();
    case 'douyu':
      return DouyuSite();
    case 'huya':
      return HuyaSite();
    case 'douyin':
      return DouyinSite();
    case 'kuaishou':
      return KuaishouSite();
    default:
      throw ArgumentError('unknown platform: ' + platform);
  }
}

Future<void> startWatch(Socket sock, LiveSite site, String roomId) async {
  try {
    final detail = await site.getRoomDetail(roomId: roomId);
    if (!detail.status) {
      sock.write('ERROR room offline\n');
      return;
    }

    final qualities = await site.getPlayQualites(detail: detail);
    if (qualities.isEmpty) {
      sock.write('ERROR no qualities\n');
      return;
    }
    final quality = qualities.first;

    final playUrl = await site.getPlayUrls(detail: detail, quality: quality);
    if (playUrl.urls.isEmpty) {
      sock.write('ERROR no play url\n');
      return;
    }
    final url = playUrl.urls.first;
    sock.write('STREAM ' + url + '\n');

    // 弹幕（平台相关 args，参考实现；各平台 start 入参不同，需按需调整）
    final danmaku = site.getDanmaku();
    danmaku.onMessage = (LiveMessage msg) {
      if (msg.type != LiveMessageType.chat) return;
      final name = msg.userName.replaceAll('\n', '').replaceAll('|', '');
      final text = msg.message.replaceAll('\n', '').replaceAll('|', '');
      sock.write('DANMAKU ' + name + '|' + text + '\n');
    };
    danmaku.onReady = () => sock.write('READY\n');
    danmaku.onClose = (String m) => sock.write('CLOSE\n');
    // TODO: 各平台 start 需要特定 args 对象（如 BiliBiliDanmakuArgs），
    // 这里以 detail 作为占位，需按平台补充。
    await danmaku.start(detail);
  } catch (e) {
    sock.write('ERROR ' + e.toString() + '\n');
  }
}

Future<void> handle(Socket sock) async {
  sock.write('HELLO\n');
  final lines = StreamIterator(
      utf8.decoder.bind(sock).transform(const LineSplitter()));
  try {
    while (await lines.moveNext()) {
      final line = lines.current.trim();
      if (line.startsWith('WATCH ')) {
        final parts = line.split(' ');
        if (parts.length < 3) {
          sock.write('ERROR usage: WATCH <platform> <roomId>\n');
          continue;
        }
        final site = makeSite(parts[1]);
        await startWatch(sock, site, parts[2]);
      } else if (line == 'STOP') {
        break;
      }
    }
  } catch (e) {
    sock.write('ERROR ' + e.toString() + '\n');
  } finally {
    await sock.flush().catchError((_) => sock);
    sock.destroy();
  }
}

Future<void> main(List<String> args) async {
  final port = int.tryParse(args.isNotEmpty ? args[0] : '9527') ?? 9527;
  final server = await ServerSocket.bind(InternetAddress.anyIPv4, port);
  stdout.writeln('bridge listening on :' + port.toString());
  server.listen(handle);
}
