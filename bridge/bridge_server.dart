// bridge/bridge_server.dart
// 数据桥接服务器（参考实现）——解析直播流地址 + 房间列表 + 转发弹幕给 VR 前端。
//
// 运行（在仓库根目录，需要 simple_live_core 依赖可解析）：
//   dart run bridge/bridge_server.dart 9527
//
// 协议（换行分隔文本，字段用 | 分隔）：
//   客户端 -> 服务端:
//     WATCH <platform> <roomId>    观看房间
//     LIST <platform>              推荐房间列表
//     SEARCH <platform> <keyword>  搜索房间
//     STOP                         断开
//   服务端 -> 客户端:
//     STREAM <url>                 直播流地址
//     ROOM <id>|<title>|<user>|<online>  房间条目（LIST/SEARCH 返回）
//     DONE                         列表结束
//     DANMAKU <user>|<text>        弹幕
//     READY / CLOSE / ERROR <msg>  状态
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

String clean(String s) => s.replaceAll('|', '').replaceAll('\n', '');

Future<void> sendRooms(Socket sock, List<LiveRoomItem> rooms) async {
  for (final room in rooms) {
    sock.write('ROOM ' +
        clean(room.roomId) + '|' +
        clean(room.title) + '|' +
        clean(room.userName) + '|' +
        room.online.toString() + '\n');
  }
  sock.write('DONE\n');
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

    // 弹幕（平台相关 args，参考实现）
    final danmaku = site.getDanmaku();
    danmaku.onMessage = (LiveMessage msg) {
      if (msg.type != LiveMessageType.chat) return;
      sock.write('DANMAKU ' + clean(msg.userName) + '|' + clean(msg.message) + '\n');
    };
    danmaku.onReady = () => sock.write('READY\n');
    danmaku.onClose = (String m) => sock.write('CLOSE\n');
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
        await startWatch(sock, makeSite(parts[1]), parts[2]);
      } else if (line.startsWith('LIST ')) {
        final parts = line.split(' ');
        if (parts.length < 2) {
          sock.write('ERROR usage: LIST <platform>\n');
          continue;
        }
        try {
          final result = await makeSite(parts[1]).getRecommendRooms();
          await sendRooms(sock, result.items);
        } catch (e) {
          sock.write('ERROR ' + e.toString() + '\n');
        }
      } else if (line.startsWith('SEARCH ')) {
        final parts = line.split(' ');
        if (parts.length < 3) {
          sock.write('ERROR usage: SEARCH <platform> <keyword>\n');
          continue;
        }
        try {
          final keyword = parts.sublist(2).join(' ');
          final result = await makeSite(parts[1]).searchRooms(keyword);
          await sendRooms(sock, result.items);
        } catch (e) {
          sock.write('ERROR ' + e.toString() + '\n');
        }
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
