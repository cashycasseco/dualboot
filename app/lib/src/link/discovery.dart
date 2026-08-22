/// Finding the device again, fast.
///
/// The thing that makes a remote feel broken is the wait after the device reboots — you
/// pick Bruce in the launcher, the ESP32 restarts, and the app sits there. So the order
/// here is deliberately cheapest-first:
///
///   1. the address that worked last time (a reboot almost always lands on the same DHCP
///      lease, so this hits within a few hundred milliseconds),
///   2. mDNS — `bruce.local`, `tembed.local`,
///   3. the AP fallback 192.168.4.1, for when the device gave up on the home network,
///   4. a TCP sweep of the local /24, which is the only step that costs real time.
library;

import 'dart:async';
import 'dart:io';

import 'package:http/http.dart' as http;
import 'package:multicast_dns/multicast_dns.dart';

/// What answered, and as which firmware.
class Found {
  const Found(this.host, this.kind);
  final String host;

  /// 'bruce' or 'launcher'.
  final String kind;

  @override
  String toString() => '$kind @ $host';
}

/// Asks one host what it is. Cheap enough to run against a whole subnet.
Future<Found?> identify(String host, {Duration timeout = const Duration(milliseconds: 900)}) async {
  final client = http.Client();
  try {
    // The launcher answers this without any auth and names its framebuffer size.
    try {
      final res = await client.get(Uri.http(host, '/fb', {'s': '4'})).timeout(timeout);
      if (res.statusCode == 200 && res.headers.containsKey('x-width')) {
        return Found(host, 'launcher');
      }
    } catch (_) {}

    // Bruce answers 401 until you log in — which is itself a positive identification.
    try {
      final res = await client.get(Uri.http(host, '/getscreen')).timeout(timeout);
      if (res.statusCode == 200 || res.statusCode == 401) {
        return Found(host, 'bruce');
      }
    } catch (_) {}
  } finally {
    client.close();
  }
  return null;
}

/// Resolves the well-known mDNS names both firmwares register.
Future<List<String>> mdnsHosts({
  Duration timeout = const Duration(seconds: 2),
  List<String> names = const ['bruce.local', 'tembed.local'],
}) async {
  final out = <String>[];
  final client = MDnsClient();
  try {
    await client.start();
    for (final name in names) {
      try {
        await for (final rec in client
            .lookup<IPAddressResourceRecord>(ResourceRecordQuery.addressIPv4(name))
            .timeout(timeout)) {
          final addr = rec.address.address;
          if (!out.contains(addr)) out.add(addr);
        }
      } on TimeoutException {
        // No answer for this name; try the next.
      }
    }
  } catch (_) {
    // mDNS is blocked on some networks and on Android when multicast is restricted.
  } finally {
    client.stop();
  }
  return out;
}

/// The /24s worth sweeping, best first.
///
/// A phone usually has more than one IPv4 interface up: Wi-Fi, and the carrier link, and
/// sometimes a VPN. Sweeping the carrier's /24 is 254 connections that can never find a
/// T-Embed and takes seconds we would rather spend on the interface the device is actually
/// on — including the phone's own hotspot, when it is the one hosting the network.
Future<List<String>> localPrefixes() async {
  // Cellular, VPN and 464XLAT interfaces. Nothing on the other end of these is ours.
  const skip = ['rmnet', 'ccmni', 'pdp', 'wwan', 'clat', 'tun', 'ppp', 'ipsec', 'dummy'];
  // Wi-Fi client and hotspot interfaces, in the order Android and iOS tend to name them.
  const prefer = ['wlan', 'ap', 'swlan', 'softap', 'en', 'eth', 'bridge'];

  final good = <String>[], rest = <String>[];
  try {
    final ifs = await NetworkInterface.list(type: InternetAddressType.IPv4, includeLoopback: false);
    for (final iface in ifs) {
      final name = iface.name.toLowerCase();
      if (skip.any(name.startsWith)) continue;
      for (final a in iface.addresses) {
        // Link-local means DHCP never answered; there is no network to scan.
        if (a.address.startsWith('169.254.')) continue;
        final parts = a.address.split('.');
        if (parts.length != 4) continue;
        final prefix = '${parts[0]}.${parts[1]}.${parts[2]}';
        final into = prefer.any(name.startsWith) ? good : rest;
        if (!good.contains(prefix) && !rest.contains(prefix)) into.add(prefix);
      }
    }
  } catch (_) {}
  return [...good, ...rest];
}

/// Knocks on port 80 across a /24. A TCP handshake is an order of magnitude cheaper than
/// an HTTP request, so only the hosts that actually listen get identified properly.
Future<List<String>> sweep(
  String prefix, {
  Duration timeout = const Duration(milliseconds: 400),
  int concurrency = 40,
  void Function(double progress)? onProgress,
}) async {
  final open = <String>[];
  for (var start = 1; start < 255; start += concurrency) {
    final batch = <Future<void>>[];
    for (var i = start; i < start + concurrency && i < 255; i++) {
      final host = '$prefix.$i';
      batch.add(() async {
        try {
          final s = await Socket.connect(host, 80, timeout: timeout);
          s.destroy();
          open.add(host);
        } catch (_) {}
      }());
    }
    await Future.wait(batch);
    onProgress?.call((start + concurrency) / 255);
  }
  return open;
}

/// The whole ladder. Yields every device as soon as it is identified.
Stream<Found> discover({String? lastHost, void Function(String stage)? onStage}) async* {
  final seen = <String>{};

  Future<Found?> tryHost(String host) async {
    if (!seen.add(host)) return null;
    return identify(host);
  }

  if (lastHost != null && lastHost.isNotEmpty) {
    onStage?.call('checking $lastHost');
    final hit = await tryHost(lastHost);
    if (hit != null) yield hit;
  }

  onStage?.call('mDNS');
  for (final host in await mdnsHosts()) {
    final hit = await tryHost(host);
    if (hit != null) yield hit;
  }

  onStage?.call('access point');
  final ap = await tryHost('192.168.4.1');
  if (ap != null) yield ap;

  for (final prefix in await localPrefixes()) {
    onStage?.call('scanning $prefix.0/24');
    for (final host in await sweep(prefix)) {
      final hit = await tryHost(host);
      if (hit != null) yield hit;
    }
  }
  onStage?.call('done');
}
