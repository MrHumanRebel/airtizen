# Samsung Tizen 3.0 receiver startup failures

## 2.2.29: discovery succeeds, but the iPhone disconnects

[Issue #22](https://github.com/MrHumanRebel/airtizen/issues/22) reports working
discovery and repeated accepted TCP connections followed by disconnects on
UE65MU6405 with iOS 27.0.1. The conversation reports the same symptoms on
QA75Q8CAMJXXZ with iPhone 15 Plus, Tizen 3.0 and iOS 26.5.2.
Additional LAN probes in the issue receive successful RTSP `/info`,
`/pair-setup`, `/pair-verify` and first-stage `/fp-setup` replies from another
Tizen 3.0 TV. Those probes establish that request transport can work; they do
not identify the request, response or sender decision causing the iPhone to
disconnect. The previous INFO logs omitted individual handshake requests.

The handshake compatibility update addresses concrete omissions:

- `/info` works over both HTTP and RTSP. Its response protocol follows the
  request, including when an HTTP request contains `CSeq`. RTSP still requires
  `CSeq`. HTTP pairing no longer falls through to a 404 response.
- `/info` includes display negotiation limits, audio latency descriptors,
  initial volume and explicit keepalive capabilities. The 1920x1080/30 FPS
  display configuration describes the existing mirroring path, not a panel
  measurement or verified playback throughput. Latency compensation remains
  uncalibrated; neither low-power keepalive nor statistics bodies are claimed.
- Both DNS-SD services advertise the actual receiver Ed25519 public key.
  The AirPlay service also advertises a device-ID-derived pairing UUID.
  Qualified `/info` requests, using the binary-plist `qualifier` array or query
  names `txtAirPlay`/`txtRAOP`, return the same TXT wire records as multicast
  discovery. The key remains scoped to a receiver instance; this does not add
  persistent sender authorization.
- Empty replies include `Content-Length: 0`. Without that framing, an HTTP
  client can wait for EOF while the receiver keeps the connection open.
  Multiple requests received in one TCP read are processed separately, and a
  fragmented final request is preserved. Interrupted sends are retried; a
  failed or incomplete send closes its connection.
- Response buffers grow from their used length and update their capacity.
  The old implementation repeatedly reallocated short responses and could
  underallocate accumulated long headers. This is a separate reproducible
  defect, not an established explanation of the physical-TV disconnect.

Protocol comparison used [UxPlay's handlers at
dbb8a8f451597aa5f2e56c830ce41c4a655a8f98](https://github.com/FDH2/UxPlay/blob/dbb8a8f451597aa5f2e56c830ce41c4a655a8f98/lib/raop_handlers.h)
and the [AirPlay feature reference](https://github.com/openairplay/airplay-spec/blob/master/features.md).
The feature mask stays `0x4803a383,0x0`; copying a larger mask would advertise
unimplemented authentication, DRM or AirPlay 2 features.

Host integration checks verify real Ed25519/X25519 pair verification over
HTTP and RTSP, discovery/public-key agreement, qualified info, persistent
empty replies, coalesced requests and fragmented bodies. A bounded allocator
regression covers response growth, and sanitizer checks cover the host code.
ARM NaCl validation checks the complete embedded receiver. None of these
checks reproduces an actual iPhone selection or confirms playback on a TV.

After installing the updated WGT, capture the Developer diagnostics immediately
after selecting AirTizen on the iPhone. Safe INFO-level entries now identify
the method, fixed route label and response status, for example
`AirPlay socket 10: GET /info status=200`. Queries, authorization headers and
pairing bodies are excluded. On peer closure, `received=` reports total bytes
and `requests=` reports completed requests. Zero bytes distinguishes a silent
peer from an incomplete request; a nonzero request count identifies how far
dispatch progressed. Parser and send failures retain their error codes.
The exact sender-abort cause and physical audio/mirroring compatibility remain
unverified until that device retest.

## 2.2.28: multicast TTL is set after binding

[Issue #20](https://github.com/MrHumanRebel/airtizen/issues/20) reports
`mdns_ttl_failed_errno_1` on QA75Q8CAMJXXZ (QLED Q8C), Tizen 3.0,
WebKit 538.1. NaCl loading, socket creation, RAOP listening, UDP5353 binding
and group membership have already succeeded when this startup step fails.

Pepper47's `include/ppapi/c/ppb_udp_socket.h` explicitly requires
`PP_UDPSOCKET_OPTION_MULTICAST_TTL` to be set **before Bind()**. Discovery
previously set TTL after binding and joining the group, which Linux permits.
Move TTL255 configuration before binding; membership remains after binding.
Do not skip failed TTL configuration or accept the default TTL1.

`src/nacl_io/pepper_interface.cc` maps `PP_ERROR_FAILED`,
`PP_ERROR_ABORTED` and `PP_ERROR_BLOCKS_MAIN_THREAD` to EPERM. Consequently
errno1 alone does not establish a firmware permission restriction. The
legacy wrapper's TTL5-to-TTL101 mapping is already correct and is retained.

The new `mdns_ppapi_lifecycle` test executes the real discovery source against
the documented PPAPI lifecycle restriction, using real descriptors, pipes and
worker threads. Binding is redirected to an ephemeral local port and outgoing
packets are captured. The unmodified 2.2.28 source reproduces the exact
`mdns_ttl_failed_errno_1`; the reordered source emits all seven DNS records,
sends TTL0 goodbyes and restarts. Transport TTL255 is distinct from DNS record
TTL120/TTL0. Injected TTL and group-membership errors remain fatal and release
their sockets. The SDK regression also verifies legacy TTL101 forwarding and
error propagation. These are host regressions; physical Q8C/UE65MU6100 startup,
iPhone discovery and playback still require a retest with the new WGT.

## 2.2.27: RAOP starts, then mDNS startup fails

The next physical-TV photos confirm `Embedded receiver: loaded`, initialized
server sockets and `RAOP listener started`. Startup then stops the receiver
because discovery reports `mdns_start_failed`. The photos do not identify the
failing mDNS socket operation.

Two reproducible defects exist in the pinned Pepper47 UDP implementation:

- The Samsung UDP 1.0 wrapper calls `SetMulticastMembership` with the group and
  local-interface resources reversed. This violates the SDK's own
  `PP_MulticastMembership` ABI for both joining and leaving a multicast group.
- `SO_REUSEADDR` returns success through the generic socket base without
  actually forwarding `PP_UDPSOCKET_OPTION_ADDRESS_REUSE` to the browser. The
  mDNS bind therefore cannot share UDP5353 with existing platform responders.

`prepare-nacl-udp.py` repairs a build copy of the verified SDK source;
`build-nacl-io.sh` recompiles its UDP object and replaces that member in a
separate static runtime archive. The downloaded SDK is not modified. The ARM
receiver links this corrected archive and retains a versioned UDP compatibility
symbol, required again by the independent package validator. The complete
corrected runtime ships inside the WGT. Source archives include the original
SDK and the repair/build scripts needed to reproduce it.

`test_samsung_udp.py` executes the actual SDK method bodies against a strict
PPAPI boundary. The original methods fail both address-contract and port-reuse
checks; repaired methods pass and propagate browser errors. These tests execute
on the build host, not on the physical TV. Actual multicast membership and
iPhone discovery still need the new package's physical retest.

Startup now reports the exact failing step and errno, for example
`mdns_bind_5353_failed_errno_98` or `mdns_join_group_failed_errno_22`, rather
than only `mdns_start_failed`. HTTP503 diagnostics preserve this code. A
failed multicast setup still fails startup; the fix does not falsely label an
undiscoverable receiver as working.

## 2.2.26 regression: Nexe crashed during startup

The follow-up TV photos show `Nexe crashed during startup` before loopback
status is available. Inspection of the 1.87 MiB WGT confirms a packaging
regression: the plist archive included the standalone `cnary.c` test program.
After the socket dependency ordering was corrected, its `main()` was selected
before the SDK PPAPI launcher. The linker removed the unreachable receiver
and decoder code. The earlier explanation that this size reduction was just
optimization was incorrect. Do not use the 2.2.26 package.

Exclude `cnary.c` from the library and retain the three browser PPAPI entry
points explicitly. The post-link check now requires the actual PPAPI launcher,
its call from `main()`, the receiver entry point, and AAC/ALAC/H.264 decoder
definitions, in addition to correct socket dispatch. The independent package
validator also checks these definitions directly in the ELF symbol table and
rejects dynamically linked payloads. Negative tests remove each required
symbol and introduce a dynamic segment; every altered payload is rejected.

The corrected ARM module is approximately 8.2 MiB before ZIP compression.
The signed WGT contains the statically linked receiver, FFmpeg codecs, crypto,
plist and nacl_io implementation. It requires the TV's built-in NaCl/PPAPI
and browser/media platform, but no separate receiver TPK, codec download or
PC relay. This is an inventory of the implemented receiver; it does not add
AirPlay 2, multiroom, DRM or coordinated audio/video playout.

## Earlier socket initialization failure

The October 17, 2026 device photos show Tizen 3.0, WebKit 538.1,
`Embedded receiver: loaded`, loopback status data, `raop_start_failed`, and
`Error initialising socket 38`. They also show restart requests returning
HTTP 403. This verifies installation, module loading and some loopback HTTP
communication on this TV. It does not verify AirPlay media playback.

Disassembly of the previous signed WGT confirms that `getsockname()` sets
errno to 38 (ENOSYS) and returns -1. The executable listed `nacl_io` before
the transitive receiver-core archive. Socket calls first referenced by that
archive consequently resolved to newlib's linkable stubs, including
`getsockname`, address resolution and UDP send/receive. The already-working
loopback listener does not use `getsockname`, explaining the partial startup.

The receiver core now declares its own `nacl_io` dependency so that the socket
implementation follows all receiver archives. The ARM build checks public
socket dispatch after linking, before sandbox validation and packaging.
Checking for the mere presence of `ki_*` symbols would miss the original bug;
the check rejects public functions that do not branch to their nacl_io
implementation. PNaCl may inline correct bitcode wrappers into callers.

For legacy WebKit POST requests, the control server accepts the exact
`Origin: file://` literal in nonce-protected mode and echoes it in CORS headers.
Missing/wrong nonces, foreign origins, duplicate origins and nonliteral file
origins remain rejected. The host's unauthenticated test mode does not accept
file origins. The photos do not expose the rejected request headers, so this
is a compatibility fix for a possible 403 cause; diagnostics now display safe
server error identifiers such as `invalid_origin` or `unauthorized` to make
any remaining rejection actionable without logging credentials.

After installing the new complete signed WGT, launch AirTizen and check that
diagnostics show RAOP and mDNS online. On the same LAN, select AirTizen from
iPhone's Screen Mirroring or audio output menu. Record the new version, TV
firmware, iOS version, status and logs if connection or playback fails. Cloud
tests and ARM sandbox validation cannot replace this physical retest.
