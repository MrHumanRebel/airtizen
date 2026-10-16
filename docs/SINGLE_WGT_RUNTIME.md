# Single-WGT receiver for UE65MU6100 / Tizen 3.0

The requested product is a receiver installed entirely through WGT packages. A web UI that depends on an unavailable native TPK does not meet that requirement. The implemented replacement is a native NaCl module inside the same signed WGT as the UI. This document records the implemented port and its remaining device-validation gates.

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

## Implemented package and build

The WGT now contains the UI, a generated ARM `.nmf`, and the actual native `.nexe`. It is cross-compiled using the official Pepper 47 LLVM toolchain and passes the SDK's `ncval` sandbox validator before and after stripping debug data. The ELF structural/package checks reject host executables, missing modules and external module URLs. This validates build/package integrity, not device execution.

The module initializes `nacl_io`, starts the receiver/control server from a worker thread, and uses PPAPI Audio with a bounded PCM queue. TV `systeminfo` supplies the LAN address; a stable local receiver identifier replaces Linux interface/MAC ioctls. The TV manifest uses `tv-samsung` and the documented public-network privilege. HTTP readiness waits and UDP timing-thread shutdown avoid reliance on socket timeouts missing from this SDK.

FFmpeg supplies AAC-LC/AAC-ELD and ALAC decoding; FDK is used only as an independent host-test encoder and is not linked into the production module. The existing encrypted H.264-to-fMP4 path feeds the browser MSE renderer. Native PCM and MSE video are not yet driven by one playout clock.

```bash
# Linux x86-64: build-essential, CMake 3.19–3.x, pkg-config, curl, unzip, xz-utils, Python 3
bash scripts/build-nacl-receiver.sh
python3 tests/test_embedded_package.py ../.airtizen-deps/receiver-nacl-build/airtizen_receiver.nexe
python3 scripts/stage-web.py /tmp/airtizen-web "$(python3 scripts/version.py)" \
  --receiver ../.airtizen-deps/receiver-nacl-build/airtizen_receiver.nexe
```

The scripts cache downloads outside the checkout, verify fixed SHA-256 values, and build an isolated Python 2.7.18 solely for the old SDK driver. Python 3 remains the application/test interpreter. The Samsung SDK archive hash is `c6fe7bfbc1bf0981d018bea47e62574a9c7575d1b102281a8f728e00a09bab2e`; its official download URL is `https://developer.samsung.com/smarttv/file/b7e3cb3a-9b3d-4651-9404-3980dac50cdf`. No TLS/checksum bypass is used. The previous CloudFront network block has been resolved.

PRs cross-build/test the native payload without signing secrets. Main builds download that exact payload, sign it, verify all payload digests and both RSA signatures, require the embedded module, and publish checksums and source archives. GUI-only packages cannot pass the release check. License texts ship inside the WGT; corresponding receiver, FFmpeg and NaCl runtime sources accompany release assets.

## Outstanding production acceptance

Install the single WGT on UE65MU6100 and establish NaCl startup, multicast discovery, current iPhone connection, audible music, mirroring frames, URL playback and lifecycle recovery. Then implement and validate coordinated audio/video playout, authorization/session ownership and sustained resource limits. AirPlay 2 multiroom, photos and DRM are not implemented. See [the acceptance matrix](PRODUCTION_READINESS.md). Neither desktop Chrome bootstrap tests nor `ncval` execute the native module on a Samsung TV.
