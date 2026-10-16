# Production readiness: UE65MU6100 / Tizen 3.0

Status: **experimental; production sign-off blocked**. Target confirmed by the project owner on 2026-10-16. All local results below concern Linux/Chromium, not a Samsung TV.

## Research and architecture evidence

1. [AirTizen issue #8](https://github.com/MrHumanRebel/airtizen/issues/8), including its comments, was inspected. The contributor reports successful mDNS announcements through Samsung socket extensions; native ServiceApp packaging using a template; failure to start a simple service on stock firmware; failure of TV hybrid packaging; and, in a later comment, a successful PC → TV TCP connection to port 5000. A packet capture showing mDNS proves discovery announcements, not receiver media support. The later TCP test contradicts a blanket claim that direct inbound networking is impossible.
2. [Moonlight native-mDNS experiment](https://github.com/eduardomozart/moonlight-tizen/tree/feat/native-mdns-support), inspected at `5e01af1714ae4cc3f37acb54d88db11ca88c00aa`, uses Samsung's Emscripten SDK, a mobile native toolchain/rootstrap, and a TV manifest targeting Tizen 5.5. Its Dockerfile still attempts hybrid packaging, which the issue's later report says did not install the service. It is a reference experiment, not verified build instructions for Tizen 3.0. Do not copy its version/rootstrap assumptions or password handling into a production pipeline.
3. [UxPlay documentation](https://github.com/FDH2/UxPlay) describes a maintained mirroring/audio receiver using GStreamer and legacy AirPlay protocols, with limitations for non-mirror video and no Apple video DRM decryption. It is a candidate protocol reference or replacement core, not a drop-in Tizen renderer. Its GPLv3 obligations require review before importing code.
4. [OpenAirPlay feature definitions](https://github.com/openairplay/airplay-spec/blob/master/src/features.md) distinguish audio, video, photos, mirroring, authentication, metadata and multiroom bits. Current advertisement removes unsupported video/photo/mirroring/metadata claims; `/info` uses the receiver's actual name, network identity and signing public key instead of a serialized donor AppleTV identity. Apple-device compatibility of the restricted advertisement is unverified.

The direct native route remains an experiment until a minimal signed service can start on the exact TV. Test that first, using an actual compatible SDK and certificate. Separately investigate the Samsung NaCl/WASM socket and media APIs available on the 2017 platform; do not assume a recent Moonlight build or standard browser WebAssembly works there. Keep a LAN companion route as a fallback if the direct platform probe fails; a relay alone does not implement media decoding, playback or synchronization.

## Verified locally

| Check | Evidence / limit |
| --- | --- |
| Native build | C and C++ host targets compile; generated donor copies no longer rewrite unchanged outputs |
| AAC-ELD media | FDK encoder creates 12 frames of a 440 Hz stereo fixture; AES-encrypted RTP payloads pass through the real receiver queue/decode path to nonzero 44.1 kHz, 16-bit PCM; invalid AAC is not emitted as valid PCM |
| Pair verification | Independent Python cryptography client validates receiver Ed25519 signature over X25519 keys and completes the AES-CTR encrypted client signature exchange |
| Audio boundary | Volume, mute, nonfinite gain rejection, partial writes, error handling and host-stub status tested with captured callbacks |
| Protocol | Real-process checks cover fragmented requests, JSON escaping, exact paths, origin/Host restrictions, preflight, oversized input, parallel status requests, malformed RAOP input, mDNS records and repeated stop/start |
| UI | Real Chromium checks use the native HTTP bridge, remote arrow/OK/Back navigation, start/stop, developer toggle/logs, screen bounds and hostile peer text rendered as text |
| Version/package tools | Numeric version ordering/distance/overflow, staged manifest/UI versions, source preservation, missing signatures, mismatched versions and tampered file digests tested |

These tests do not establish audible TV output, hardware video, Apple sender compatibility, latency, A/V sync, multiroom, fairplay interoperability, persistent pairing or Samsung certificate acceptance. Native status explicitly exposes experimental capabilities and host audio as a stub. Mirroring requests return Not Implemented instead of silently consuming video.

## Required development before production

- Implement and test a Tizen 3.0 hardware video decoder and visible rendering surface; handle SPS/PPS, timestamps, orientation/resolution changes, backpressure and decoder recovery. The current video callback has no renderer.
- Implement actual codec negotiation and ALAC lossless music. The donor currently configures a single AAC-ELD decoder. There is no SDP ANNOUNCE/ALAC path despite older ALAC/AAC claims.
- Complete sender metadata, artwork, progress, remote control, audio flush/pause/resume, packet retransmission and timestamp-driven A/V synchronization. Synthetic fixtures cannot establish interoperability.
- Replace or systematically audit the legacy network/crypto/codec core. Input validation now bounds request bodies, checks crypto lengths, prevents a FairPlay reply-table index overrun, and rejects unsupported mirror setup. This is not a complete fuzz or security audit. Persistent identity, pairing authorization/PIN access, a trusted control-API authorization channel (opaque `null` origins are not authentication), complete teardown, idle clients and multi-client ownership remain review items.
- Supply a reproducible compatible native-service SDK/rootstrap, application manifest, TPK package/signing/install/start path. The existing CMake service option alone is insufficient.
- Audit third-party licenses and distribute all required notices and corresponding source/build material for any native binary. The PlayFair component is GPLv3 and FDK AAC has its own license; the top-level MIT file does not override either.
- Validate the GitHub-hosted build and signing jobs with actual repository credentials. The cloud SDK download returned HTTP 403, so no signed WGT/TPK or successful Actions run is claimed here.

## Physical-device acceptance matrix

Record TV firmware, sender model/OS, network topology and artifact commit/hash with each result. Required rows are **not run** until actual hardware evidence is attached.

| Scenario | Required outcome | Status |
| --- | --- | --- |
| Install/update separate signed packages | Same-author upgrade works; service starts on stock firmware | Not run |
| Boot/service lifecycle | Start, stop, reconnect, app exit and TV standby/wake recover cleanly | Not run |
| iPhone/iPad/Mac discovery | Correct unique name/IP, no duplicate stale service after stop | Not run |
| Pairing and unauthorized sender | Real sender connects; access policy enforced; reconnect works | Not run |
| ALAC music | Correct stereo playback, volume/mute, metadata/artwork, pause/resume | Not implemented / not run |
| AAC mirroring audio | Correct audible output and clean flush/disconnect | Not run |
| Screen sharing | 1080p target, rotation/resolution changes, correct A/V sync | Not implemented / not run |
| AirPlay video/photos | Defined supported URL/media formats and playback controls | Not implemented / not run |
| AirPlay 2 multiroom | Clock sync, session ownership and group volume | Not implemented / not run |
| Network failure | Packet loss/reordering, Wi-Fi reconnect and sender disappearance recover | Not run |
| Soak/resource limits | Long audio/video sessions, bounded memory/CPU, no leaks or crashes | Not run |
| Developer mode | Hidden by default; remote toggle; useful bounded logs without secrets | Chromium only |
| DRM-protected Apple media | Requires a supported authorized implementation; no capability claimed | Unsupported |

A green host build or an uploaded WGT must not be substituted for these gates.
