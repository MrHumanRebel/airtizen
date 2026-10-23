# Samsung Tizen 3.0 receiver startup failures

## After PR #27: direct hardware video and regression fixes

The contributor's QE49Q7CAM/Q7C test now establishes direct PPAPI/GLES2
macOS mirroring and separately working audio on Tizen 3.0. The UI retains
the successfully negotiated `uxplay-test` default and persists explicit
profile selection when storage is available. The Native launch-default
and nonpersistent-selection descriptions below document the earlier
2.2.31/2.2.32 investigation, not current behavior.

The follow-up fix restores photo/URL-sharing updates beside direct video,
keeps MSE for NaCl-less browsers and hardens HLS cleanup/generation handling.
See [physical evidence, regression boundaries and the retest procedure](DIRECT_VIDEO_VALIDATION.md).
UE65MU6100/UE65MU6405 compatibility and simultaneous A/V synchronization
still require the new WGT's device test.

## 2.2.31: challenge excluded; compare discovery profiles

The next physical UE65MU6405/Tizen 3.0/iOS 27.0.1 test in
[issue #22](https://github.com/MrHumanRebel/airtizen/issues/22) still receives
one 114-byte OPTIONS request, then a peer close after five seconds. The
request has no `Apple-Challenge`; the missing response therefore cannot
explain this probe's failure. Manual RSA verification and the stable Wi-Fi
MAC-derived ID pass on the TV. No `/info`, pairing or stream setup follows.
Whether iOS rejects discovery data or another probe detail remains unknown.

The new build adds the missing RAOP `ft` and `am` fields in Native mode.
It also aligns `Server: AirTunes/220.68` with DNS-SD `vs`/`srcvers` and
`/info`/`server-info`, and stops advertising the unhandled PAUSE method.
These correct observable inconsistencies without claiming that they caused
the iPhone's decision. The existing RAOP `pk` field is retained: captured
outgoing packets and qualified `/info` records verify it in both services,
including maximum-length names and both profiles. The reported missing
RAOP key on the TV may involve a different observation or cached record;
that discrepancy is not resolved by host tests.

One WGT supports two profiles under Settings, with Developer mode enabled:

| Field | Native (default) | UxPlay test |
| --- | --- | --- |
| AirPlay `features`, RAOP `ft` | `0x4803a383,0x0` | `0x5A7FFEE6,0x0` |
| AirPlay `model`, RAOP `am` | `AirTizen1,1` | `AppleTV3,2` |
| RAOP `cn` | `1,2,3` | `0,1,2,3` |
| RAOP `rhd`, `sv` | absent | `5.6.0.0`, `false` |
| RAOP `pk`, AirPlay `pk`/`pi` | current receiver identity | current receiver identity |
| AirPlay `flags`, RAOP `sf` | `0x4` | `0x4` |

The test values follow the requested comparison and the constants in
[UxPlay `dnssdint.h` at fc156bbb8a923cb0d66b7fe4ff79fe7d8abc1db7](https://github.com/FDH2/UxPlay/blob/fc156bbb8a923cb0d66b7fe4ff79fe7d8abc1db7/lib/dnssdint.h).
They are a discovery/negotiation experiment, not a claim to implement all
of UxPlay's advertised functionality or reproduce its entire runtime.
Native remains the launch default. The test profile does not add PCM,
authentication, DRM, clock synchronization or new stream handlers. The
status API reports `discovery.experimental`, advertised and implemented
feature masks separately; `productionReady` remains false. `/info` and
`server-info` use the selected model/mask, and the display rotation flag
follows the selected mask's bit 8.

Switching an active receiver sends TTL0 goodbyes with the old profile, closes
its sessions, changes the profile and restarts with the same receiver name,
device ID and pairing UUID. The existing core restart regenerates its
Ed25519 key; both TXT records and `/info` use that new key. Switching a
stopped receiver leaves it stopped. Profile selection lasts for the native
module's lifetime and is not persisted across a full app relaunch. The
nonce-protected, empty-body POST endpoints are `/discovery/native` and
`/discovery/uxplay-test`; GET, missing/wrong nonces and query-token-only
mutation attempts remain rejected.

To retest:

1. Install the complete new WGT and try Native first. Record the result and
   new native lines immediately after selecting Screen Mirroring or a photo.
2. Open Settings, enable Developer mode, then select
   `Discovery profile: Native` to change it to `UxPlay test`. Verify
   `Discovery: uxplay-test`, `AppleTV3,2` and the advertised mask in diagnostics.
3. Allow six seconds for repeated announcements, close and reopen the iPhone
   AirPlay picker, and try the same operation. Capture the profile marker,
   request/status lines, OPTIONS header summaries and closure counters.
4. Select `Discovery profile: UxPlay test` to restore Native. A failed
   experiment does not establish that any advertised feature is implemented.

OPTIONS diagnostics identify HTTP/RTSP, target kind (`asterisk`, `rtsp-uri`,
`path` or `other`), body size and header count. They log canonical allowlisted
header names and byte counts, plus only a recognized software product and
bounded numeric version from User-Agent. Device/session IDs, addresses in
request targets, authorization, challenge values, unknown header names and
arbitrary User-Agent suffixes are excluded. At most 16 header summaries are
emitted per probe; larger sets report an omitted count. Response logs include
the prepared byte length. Native startup records the selected profile, so
retained lines can be assigned to the correct trial. The diagnostics layout
keeps its buttons inside the panel while logs scroll.

Host checks cover complete multicast records, old-profile goodbyes, profile
restarts and identity agreement, metadata consistency, authenticated control,
header redaction, UI switching and bounded ARM packaging. They do not
reproduce the physical iOS decision or verify media output on this TV.

## 2.2.30: iPhone sends only OPTIONS, then closes

The October 21 follow-up in [issue #22](https://github.com/MrHumanRebel/airtizen/issues/22)
confirms that the updated `/info`, HTTP support and public-key TXT records
reach a real UE65MU6405 running Tizen 3.0. The iPhone on iOS 27.0.1 still
does not display media. Its connection completes only `OPTIONS`, then closes
after five seconds with `received=114 requests=1`. It does not reach `/info`,
pairing or stream setup. A manual probe also establishes that the receiver
omits `Apple-Response` when given `Apple-Challenge`.

The missing response is a reproducible interoperability defect. The report
does not contain the iPhone's request headers, however, so it does **not**
establish that the iPhone sent a challenge or that this omission caused its
disconnect. The earlier metadata fix is verified by the physical retest;
complete sender compatibility is not.

The new challenge handler constructs the legacy RAOP response using the
published AirPort RSA-2048 compatibility key and existing bundled crypto:
challenge bytes, the actual connection's local IPv4/IPv6 address and the six
bytes of the advertised device ID. It zero-pads the payload to at least 32
bytes, applies raw PKCS#1 v1.5 type-1 signing and returns unpadded base64 in
`Apple-Response`. This public compatibility key is separate from the
receiver's Ed25519 pairing key and does not prove device trust. The handler
accepts canonical padded or unpadded base64 for 1..16 challenge bytes;
malformed or oversized values receive a bounded, empty 400 response.
Internal signing failure receives 503. No extra crypto library, service or
relay is required.

Host regressions recover the complete RSA block with an independent public
key and check its padding, challenge, local socket address and advertised
identity. They continue through real Ed25519/X25519 pair verification on the
same HTTP or RTSP connection. The old 2.2.30 executable fails the new check
because `Apple-Response` is missing. Fixed IPv4 and IPv6 crypto vectors cover
the different payload lengths; the receiver listener and discovery remain
IPv4, so this does not claim IPv6 network support. Package validation now
also requires the actual `airtizen_apple_response` implementation in the ARM
module. These are host/build checks, not a physical iPhone retest.

Receiver identity no longer silently changes whenever browser storage fails.
An existing saved ID is preserved. Otherwise the bootstrap uses the active
network's valid unicast `macAddress`, documented for [Tizen systeminfo Wi-Fi
since 2.3 and Ethernet since 2.4](https://developer.samsung.com/smarttv/develop/api-references/tizen-web-device-api-references/systeminfo-api.html).
If no MAC is available, a generated local ID must survive a storage readback
before launch; failure produces an explicit diagnostic instead of a new ID
on every launch. Browser regressions exercise blocked storage, reloads and
preservation of the old ID when a MAC becomes available. Reinstallation
that erases a previous random ID can still cause a one-time transition to
the platform MAC; changing interfaces can change the fallback MAC. Erased
random identities cannot be reconstructed. Ed25519 keys still belong to a
receiver instance; this is not persistent sender authorization.

On the next TV attempt, inspect new native entries after the current launch:

- `Apple-Challenge answered: RSA-2048 response signed`
- `OPTIONS probe: Apple-Challenge absent`
- `Apple-Challenge rejected: malformed input` or `Apple-Challenge signing failed`

Challenge and signature values are never logged. If the iPhone again stops
after an OPTIONS probe **without** a challenge, the absent RSA response
cannot explain that probe's failure; retain its request/status and closure
counters for the next investigation. `Clear local log` intentionally clears
the web log only, so use timestamps to distinguish retained native entries.
Feature mask `0x4803a383,0x0` and model `AirTizen1,1` remain unchanged; adding
unimplemented authentication/clock features would not supply those protocols.

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
