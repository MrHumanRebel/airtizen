# Production readiness: UE65MU6100 / Tizen 3.0

Status: **experimental; production sign-off blocked**. Target confirmed by the project owner on 2026-10-16. All local results below concern Linux/Chromium, not a Samsung TV.

## Research and architecture evidence

1. [AirTizen issue #8](https://github.com/MrHumanRebel/airtizen/issues/8), including its comments, was inspected. The contributor reports successful mDNS announcements through Samsung socket extensions; native ServiceApp packaging using a template; failure to start a simple service on stock firmware; failure of TV hybrid packaging; and, in a later comment, a successful PC → TV TCP connection to port 5000. A packet capture showing mDNS proves discovery announcements, not receiver media support. The later TCP test contradicts a blanket claim that direct inbound networking is impossible.
2. [Moonlight native-mDNS experiment](https://github.com/eduardomozart/moonlight-tizen/tree/feat/native-mdns-support), inspected at `5e01af1714ae4cc3f37acb54d88db11ca88c00aa`, uses Samsung's Emscripten SDK, a mobile native toolchain/rootstrap, and a TV manifest targeting Tizen 5.5. Its Dockerfile still attempts hybrid packaging, which the issue's later report says did not install the service. It is a reference experiment, not verified build instructions for Tizen 3.0. Do not copy its version/rootstrap assumptions or password handling into a production pipeline.
3. [UxPlay documentation](https://github.com/FDH2/UxPlay) describes a maintained mirroring/audio receiver using GStreamer and legacy AirPlay protocols, with limitations for non-mirror video and no Apple video DRM decryption. It is a candidate protocol reference or replacement core, not a drop-in Tizen renderer. Its GPLv3 obligations require review before importing code.
4. [OpenAirPlay feature definitions](https://github.com/openairplay/airplay-spec/blob/master/src/features.md) distinguish audio, video, photos, mirroring, authentication, metadata and multiroom bits. Current advertisement includes the implemented audio, video, mirroring and metadata paths, includes photo upload/cache and excludes slideshow/DRM/multiroom claims, and `/info` uses the receiver's actual name, network identity and signing public key instead of a serialized donor AppleTV identity. Apple-device compatibility of this advertisement is unverified.

The receiver is now compiled into an ARM NaCl module inside the signed WGT. See [the implemented single-WGT route](SINGLE_WGT_RUNTIME.md). The official Pepper 47 toolchain builds the complete receiver and codecs; its sandbox validator accepts the binary. No separately installed native TPK or LAN companion is required by this architecture. The user’s UE65MU6100 photos confirm installation, NaCl module loading and loopback status, but show receiver socket startup failure. The static-link fix and remaining physical retest are documented in [startup troubleshooting](TV_STARTUP_TROUBLESHOOTING.md). Synchronized media remains unverified.

## Reported physical progress

[PR #27](https://github.com/MrHumanRebel/airtizen/pull/27) reports direct PPAPI/GLES2 macOS mirroring and separately verified audio on Samsung QE49Q7CAM/Q7C, Tizen 3.0. The selected `uxplay-test` profile completed negotiation. Its 178.327 ms sample is TV-side presentation time, not end-to-end latency. Synchronized A/V and other models remain outstanding. See [preserved evidence and regression coverage](DIRECT_VIDEO_VALIDATION.md). This supersedes the earlier startup-only evidence for that model, without marking the full acceptance matrix complete.

## Verified locally

| Check | Evidence / limit |
| --- | --- |
| Native build | Host targets and Samsung ARM NaCl receiver compile; SDK sandbox validation passes; generated donor copies do not rewrite unchanged outputs |
| AAC-LC / AAC-ELD | FDK-generated stereo frames pass through AES-encrypted RTP and the real decode queue; invalid AAC is dropped |
| ALAC | Independent FFmpeg fixture produces byte-exact 16-bit PCM directly and through encrypted RTP; negotiated 44.1/48 kHz, mono/stereo boundaries are validated |
| Mirroring transport | Fragmented TCP headers, bounded AVC configuration/access units, EOF/stop and AES-CTR keystream continuity across short packets tested |
| Visible mirroring | H.264 AVCC/Annex-B conversion, IDR initialization and bounded fMP4 fragments feed the real loopback HTTP API; Chromium decodes 320×180 frames and advances playback; Back stops the receiver |
| URL sharing | AirPlay HTTP play/rate/scrub/stop and reverse PTTH event/ack flow implemented; Chromium decodes MP4 video and audio, pauses, seeks and stops using actual native endpoints |
| Photos | Real PUT /photo uploads, session-scoped cacheOnly/displayCached, HTTP 412 misses, 16 MiB eviction bounds, JPEG/PNG decoding, dissolve, reverse stopped event and remote stop tested |
| Music metadata | Bounded DMAP text, artwork/version handling and progress parsing tested; GUI displays peer text without HTML interpretation |
| Pair verification | Independent Python client verifies receiver Ed25519 signature over X25519 keys and completes AES-CTR client signature exchange |
| NaCl PCM queue | Bounded stereo/mono buffering, underrun silence and full-queue behavior tested; actual PPAPI output needs TV |
| Audio boundary | Volume/mute, nonfinite gain rejection, partial writes and errors tested; Linux explicitly reports a silent host stub |
| Protocol | Real-process tests cover fragmented requests, exact routes, Host/Origin/custom-header rules, size limits, parallel status, malformed RAOP, reverse events, mDNS and repeated stop/start |
| UI | Chromium tests cover remote arrows/OK/Back, start/stop, hidden developer logs, bounds and peer-text injection |
| Sanitizers | Native CTest targets pass with ASan leak checking and UBSan halt-on-error; downloaded optimized FFmpeg is not sanitizer-instrumented |
| Packaging | Real Web CLI build/sign with temporary 3072-bit development identities; both XML signatures verified cryptographically; a mutated RSA signature is rejected |
| Release tooling | Numeric version ordering/distance/overflow, staged versions/source preservation, unsigned packages, incorrect versions and tampered payload rejection tested |

These tests establish host implementation behavior, not audible TV output, Apple sender compatibility, Tizen hardware decoding, latency, A/V synchronization or Samsung certificate acceptance. Status retains `productionReady: false`. The desktop browser result does not establish that the 2017 TV's MediaSource implementation supports this stream.

## Remaining production gates

- **Native TV deployment:** install and execute the complete signed WGT on UE65MU6100; verify NaCl loader, inbound sockets/multicast, PPAPI audio, direct hardware video and HLS fallback. Developer Mode is required for sideloading; use an authorized Samsung distributor if public SDK signing is rejected. Cross-compilation and sandbox validation do not establish successful execution.
- **Mirroring audio/video synchronization:** audio currently goes to the native output while video uses independent direct/HLS presentation (MSE in host browsers). Implement sender-clock mapping, coordinated playout, clock-drift correction, packet-loss recovery and discontinuity handling. Test resolution/rotation changes, B-frame assumptions, decoder recovery and memory limits on Tizen. Independent audio/video tests do not prove synchronized screen sharing.
- **Sender interoperability:** test current iPhone/iPad/macOS discovery, FairPlay exchange, negotiated codec parameters, pause/flush/reconnect and URL sharing. Current support is the legacy AirPlay path; AirPlay 2 buffered audio, multiroom, slideshow pull sessions and DRM playback are not implemented. Legacy photo upload/cache is implemented with decoded JPEG/PNG browser tests. Legacy RSA/SDP ANNOUNCE audio is not implemented. URL playback depends on formats supported by the TV browser; HLS/AVPlay behavior is unverified.
- **Authorization and session ownership:** implement persistent identity/pairing authorization or PIN policy, media-session ownership, idle-client limits and systematic security/fuzz review. The WGT now authenticates its local HTTP control/media channel with an ephemeral 256-bit token passed through embed attributes; host development can opt into that mode. This does not authenticate Apple senders. Existing input bounds and crypto fixes are not a full security audit.
- **Licensing and native distribution:** complete the legacy donor provenance/license audit and include corresponding source, notices and required relinking material. PlayFair is GPLv3 and downloaded FFmpeg is LGPL; the production module no longer links FDK AAC (host test encoder only). Release artifacts include corresponding sources/build scripts and the WGT includes dependency notices. The combined native binary is not MIT-only.
- **Release infrastructure:** run GitHub Actions with the default public SDK sideload identity or optional configured credentials. Local community/SDK and configured-certificate success does not prove Samsung authorization or a successful hosted workflow. Keep releases experimental until the device and feature acceptance gates pass.

## Physical-device acceptance matrix

Record TV firmware, sender model/OS, network topology and artifact commit/hash with each result. The Q7C samples above provide partial physical evidence; the complete criteria below remain outstanding. Do not infer success from host tests or independent audio/video samples.

| Scenario | Required outcome | Evidence still needed |
| --- | --- | --- |
| Install/update single signed WGT | Same-author upgrade works; embedded NaCl receiver starts in Developer Mode | TV / WGT |
| Lifecycle | Start/stop/reconnect/app exit/standby/wake recover | TV |
| iPhone/iPad/Mac discovery | Unique name/IP, no stale duplicate after stop | Apple sender / TV |
| Pairing/access policy | Authorized sender connects; unauthorized access rejected | Authorization implementation / devices |
| ALAC/AAC music | Audible stereo, volume/mute, metadata/artwork, flush/pause/resume | Apple sender / TV |
| Screen sharing with audio | 1080p target, orientation changes, bounded latency and A/V sync | Synchronization implementation / devices |
| URL video/audio | Supported media formats, playback controls, clean end/disconnect | Apple sender / TV |
| Network failure | Loss/reordering/Wi-Fi reconnect/sender disappearance recover | Recovery work / device testing |
| Soak/resources | Long sessions; bounded memory/CPU; no leak/crash | Sustained device tests |
| Developer UI | Hidden default, remote toggle, bounded useful logs without secrets | TV validation |
| AirPlay 2 multiroom / slideshow pull | Separate implementation and acceptance criteria | Not implemented |
| DRM-protected Apple media | Supported authorized implementation | Unsupported |

A green host build or an uploaded WGT must not be substituted for these gates.
