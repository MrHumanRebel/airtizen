# AirTizen

An experimental AirPlay receiver for Samsung UE65MU6100 / Tizen 3.0, with a TV remote friendly web UI and a native receiver core.

**Experimental: production acceptance on this TV is still outstanding.** The receiver implements encrypted ALAC/AAC audio, H.264 mirroring transport with a browser MSE renderer, music metadata/artwork, and HTTP URL video/audio sharing with playback controls. These paths have Linux/Chromium regression coverage. Real iPhone interoperability, Tizen 3.0 rendering and synchronized mirroring audio/video have not been established. Legacy JPEG/PNG photo sharing and preloading are implemented. AirPlay 2 multiroom, slideshow pull sessions and DRM playback are unsupported. The WGT bundles the ARM NaCl receiver, codecs and PPAPI audio output; no separately installed native service or PC relay is required by this architecture. Execution on the target TV is not yet verified.

## Development

Requirements: Linux, GCC/G++, CMake 3.19–3.x, Make, pkg-config, Python 3.12+, Node.js and curl. The build downloads a checksum-pinned minimal FFmpeg 7.1.5 dependency; cache it outside the checkout. The bundled fixed-point codec and crypto are compiled with `-fwrapv` under GCC/Clang so their intended wrapping arithmetic has defined compiler semantics. No sanitizer checks are disabled.

```bash
python3 -m pip install -r requirements-dev.txt
./scripts/build-local.sh
ctest --test-dir native/build --output-on-failure
python3 tests/test_release.py
python3 -m playwright install --with-deps chrome
export CHROMIUM_CHANNEL=chrome
python3 tests/test_gui.py
python3 tests/test_mirror_gui.py
python3 tests/test_share_gui.py
python3 tests/test_photo_gui.py
```

The ten CTest targets cover encrypted AAC-LC/AAC-ELD and lossless ALAC decoding, encrypted mirror transport, H.264 conversion/MP4 fragmentation, metadata, receiver callbacks and real-process protocol integration. The latter includes an independent X25519/Ed25519/AES pair-verify client, HTTP validation, reverse playback events, mDNS and repeated restarts. Browser tests exercise the actual native HTTP bridge, decoded mirroring frames, URL video with decoded audio, pause/seek/stop and remote navigation. Run these suites sequentially because they use the receiver's fixed ports. None proves compatibility with an Apple sender or Tizen TV.

Media tests require H.264/AAC support. CI explicitly selects branded Chrome: Playwright’s bundled Chromium may lack these codecs. Alternatively set `CHROMIUM_EXECUTABLE` to a codec-enabled system browser; it takes precedence over `CHROMIUM_CHANNEL`. Missing codecs fail a preflight check and do not skip playback assertions.

For manual development:

```bash
native/build/airtizen_runtime "AirTizen TV"
# In another terminal:
./scripts/check-runtime-status.sh
curl -fsS -X POST -H 'X-AirTizen-Control: 1' http://127.0.0.1:45110/stop
curl -fsS -X POST -H 'X-AirTizen-Control: 1' http://127.0.0.1:45110/start
```

Stop the manually launched runtime before integration tests; tests never kill someone else's service. Host audio is a test sink and produces no audible sound. Status reports `host_stub` rather than pretending audio was played. The control API accepts only its loopback Host and, when supplied, the opaque `null` Origin used by packaged web apps. POST requires the custom control header. These checks reject simple cross-origin forms and named foreign origins; an opaque `null` origin is not authentication. A trusted app/service authorization channel is still required for production. Verify the real WGT Origin on the TV before enabling additional origins.

The UI keeps diagnostics hidden by default. Settings → Developer mode exposes receiver logs and protocol state; Blue opens diagnostics when developer mode is enabled. Arrows, OK and Back work without a mouse. The developer preference persists when the platform permits local storage.

## Build and release

GitHub Actions validates native code, browser behavior and release tooling. Successful main-branch builds then sign a staged WGT and publish an **experimental prerelease**, with automatic numeric versioning and SHA-256 checksums. A failed build, failed test or invalid configured signing identity blocks the release. There is no unsigned fallback and no forced tag update.

The default build needs no signing secrets: it uses a stable public AirTizen community author and the SDK public distributor, like the Jellyfin sideload workflow. Optional private author and Samsung distributor identities are documented in [BUILD_GITHUB_ACTIONS.md](docs/BUILD_GITHUB_ACTIONS.md). `SIGNING-MODE.txt` identifies which mode produced the artifact. The workflow cross-compiles and validates the ARM NaCl receiver, packages it with the UI, and refuses to release a WGT without that module. Dependency sources and notices accompany the release.

## Samsung TV deployment

The WGT contains `receiver/receiver.nmf` and the ARM `receiver_arm.nexe`. The UI obtains the TV LAN address, initializes the bundled receiver and reports loader failures in diagnostics. The native module supplies sockets, multicast discovery and PPAPI PCM output. See [the single-WGT implementation and build instructions](docs/SINGLE_WGT_RUNTIME.md).

Sideloading requires Samsung TV Developer Mode. If the public SDK distributor is rejected, re-sign using an authorized Samsung distributor certificate. Consumer mode alone does not provide the sideload installation path. No native TPK or companion PC is required by this package.

The actual NaCl loader, incoming connections, audible output and MSE playback must still be tested on UE65MU6100. Independent audio and video playback do not yet establish synchronized screen sharing. Keep releases experimental until the device acceptance matrix passes.

See [production readiness and research](docs/PRODUCTION_READINESS.md) for evidence, outstanding work and the physical-device acceptance matrix. No signing credentials belong in source or chat.

## Layout

- `app/`: ES5 web UI and developer diagnostics.
- `native/`: receiver, control API, mDNS and Tizen audio boundary.
- `third_party/AirPlayServerLib/`: bundled legacy receiver/codec/crypto sources.
- `tests/`: native, protocol, browser and packaging regression tests.
- `scripts/`: build, versioning, staging and signed-package validation.

The logo remains the supplied AirTizen artwork. Third-party code has separate licensing obligations; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
