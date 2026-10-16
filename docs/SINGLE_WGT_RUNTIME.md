# Single-WGT receiver for UE65MU6100 / Tizen 3.0

The requested product is a receiver installed entirely through WGT packages. A web UI that depends on an unavailable native TPK does not meet that requirement. The proposed replacement is a native NaCl module inside the same signed WGT as the UI. This document records the researched port target; it is not an implemented or production-validated receiver.

## Confirmed platform route

Samsung documents native C/C++ modules embedded in TV web applications with `<embed type="application/x-nacl">`. The browser loads an `.nmf` manifest and an architecture-specific `.nexe` from the application's files. Samsung explicitly requires ahead-of-time translation for TVs; shipping a portable `.pexe` alone is insufficient.

The official web-engine table identifies 2017 / Tizen 3.0 with Chromium M47. Samsung's NaCl download page provides Pepper Toolchain 47 dated 2017-04-04. This is the toolchain candidate for the target, rather than the Samsung Emscripten/WASM path used by the newer Moonlight experiment. Actual compatibility must still be established on the UE65MU6100 firmware.

Samsung's NaCl documentation describes sockets, multicast UDP, PCM audio and video interfaces. Its Pepper 42 notes explicitly describe multicast support in `nacl_io`; Pepper 47 is newer. Its PPAPI privilege table includes Tizen 3.0 support. These are platform capabilities, not evidence that AirTizen has already been ported or that the requested inbound sockets are reachable on the particular TV.

Sources inspected on 2026-10-16:

- [Samsung NaCl overview](https://developer.samsung.com/smarttv/develop/extension-libraries/nacl/overview.html)
- [Official NaCl toolchains and release notes](https://developer.samsung.com/smarttv/develop/extension-libraries/nacl/download.html)
- [Socket example and lifecycle](https://developer.samsung.com/smarttv/develop/extension-libraries/nacl/sample-based-tutorials/sockets-in-cplusplus.html)
- [PPAPI permissions and Tizen privileges](https://developer.samsung.com/smarttv/develop/extension-libraries/nacl/managing-nacl-projects/adding-privileges-and-permissions.html)
- [Samsung web-engine versions](https://developer.samsung.com/smarttv/develop/specifications/web-engine-specifications.html)

## Required implementation

The intended WGT contains the existing UI plus an `.nmf` and the target architecture's complete `.nexe`, including receiver code and linked codecs. A normal native Linux/Tizen executable cannot be renamed to `.nexe`; it must be compiled for the NaCl sandbox ABI and pass its loader validation. A TPK placed inside a ZIP/WGT does not install or launch itself.

1. Cross-build the protocol/crypto/codec core with the Samsung toolchain. Validate compiler support for the pinned FFmpeg dependency and produce NaCl-compatible static libraries; host `.a` files cannot be reused.
2. Add a `pp::Module` / `pp::Instance` entry point and initialize `nacl_io` before starting receiver threads. Start/stop the actual receiver from module lifecycle callbacks, reporting loader and bind failures to the GUI.
3. Adapt network identity and interface discovery; Linux `getifaddrs`/MAC ioctls cannot be assumed inside the sandbox. Verify TCP listen/accept, UDP receive, retransmission, multicast membership and shutdown against the Samsung interfaces.
4. Replace the host audio stub/Tizen service audio boundary with a real PPAPI audio callback and bounded PCM buffering. Implement coordinated sender-clock audio/video playout; independent MSE video and native audio playback are insufficient for synchronized mirroring.
5. Validate the existing MSE renderer against the TV. If it cannot sustain the stream, implement the appropriate NaCl hardware decoder/player integration. Verify resolution changes, rotation, decoder reset and memory limits.
6. Load the bundled module from the UI, declare the actual PPAPI permissions and corresponding Tizen privileges, and update the CSP deliberately. The current `object-src 'none'` prevents this load.
7. Stage and verify the complete manifest/module/library assets before signing. Only a package containing and starting the real receiver may be called standalone. Current signing tests establish package integrity, not native module execution.
8. Install that WGT on the target TV and test discovery, real iPhone pairing, ALAC/AAC music, screen sharing with audio, URL media controls, reconnect, standby and sustained playback. Full AirPlay 2/multiroom, photos and protected media remain separate missing capabilities.

The existing separately installed native-service route remains an experiment, not a prerequisite the end user is expected to solve for this product. No companion PC/relay is assumed for the requested standalone product.

## Current external blocker

The official Pepper 47 download is linked by Samsung at:

`https://developer.samsung.com/smarttv/file/b7e3cb3a-9b3d-4651-9404-3980dac50cdf`

It redirects to `d13643hualop71.cloudfront.net`. The cloud environment rejects the redirected request with HTTP 403, `Domain forbidden`. A supported network-permission request was granted, but the configured outbound domain policy still rejects the request. The required domain addition has been saved in the environment configuration draft and needs to be applied/published in environment settings. No SDK payload was downloaded or used; checksums, signatures and TLS have not been bypassed.

`api.github.com` has also been added to the draft for PR operations; Git HTTPS push works while GitHub API requests currently return `Forbidden`. Applying a network draft enables a retry, not a guarantee that API authorization will succeed.

After the network change is applied, retrieve the official toolchain, establish artifact integrity/provenance, inspect its headers/libraries and run the port build. Until that succeeds and device behavior is verified, there is no production standalone AirTizen WGT to publish.
