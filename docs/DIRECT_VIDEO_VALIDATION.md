# Direct video: preserved evidence and regression coverage

## Physical evidence from PR #27

[PR #27](https://github.com/MrHumanRebel/airtizen/pull/27), by nzmcno,
was developed and deployed on Samsung QE49Q7CAM / Q7C (2017), Tizen 3.0,
Kant-M ARMv7. The contributor reported:

- A 1728×1080 macOS desktop rendered through PPAPI hardware H.264 decoding
  and GLES2 presentation. Browser MSE previously stalled or remained blank
  while audio continued. TV-local HLS displayed video with about 5–6 s delay.
- 3,360 presented frames, zero queued frames at the sampled measurement,
  and 178.327 ms average TV-side receive/decode/presentation time, without
  a decoder error in that sample. This is not end-to-end latency: sender
  encoding, network transit before receiver ingestion and panel scanout
  are excluded. It is neither a guarantee nor a stable sub-178 ms target.
- Audio worked separately. Simultaneous A/V synchronization was not measured.
- The `uxplay-test` discovery profile completed negotiation on that TV.
  Its advertised features exceed the implemented feature set; it remains
  experimental. Saved profile selection persists when storage is available;
  otherwise `uxplay-test` is the UI default.
- Reconnection after refreshing Wi-Fi worked. The cause of the observed
  iPhone screen-lock disconnect and uninterrupted locked-phone mirroring
  were not established.

The current regression fix preserves `native/nacl/video_host.h`, its
hardware-only H.264 High initialization, GLES2 rendering, 12-access-unit /
8 MiB compressed queue, pending-picture recycling, transport reconnect loop
and the separate real AES-CTR fixture. It does not reinstate the abandoned
per-frame Flush, uniform-caching or access-unit-delimiter experiments.

## Regressions corrected after integration

The GUI profile test expected Native despite the intentional new UxPlay
launch default. It now checks the default and both directions of switching.
A separate browser test exercises saved Native selection, saved UxPlay
restoration after runtime restart, reload persistence and coalesced status
requests. The video fixture resends configuration after receiver restart,
as a new sender would, rather than sending frames into a cleared muxer.

The direct controller previously never started or polled photo/URL-sharing
handlers. These handlers now run with every playback path, receive inactive
states when media types change, and hide the direct surface while shared
media is visible. Stop invalidates pending callbacks and clears playback.

NaCl-less browsers retain the previously working MSE renderer. A loaded TV
module still selects direct PPAPI first; decoder failure selects TV-local
HLS. HLS clears its source when video becomes unavailable, checks HTTP
status, and ignores responses/events after stop. Its manifest omits an empty
authentication query in host mode. Initialization and segment URLs are both
bound to the video generation; expired playlists receive HTTP 410.

Developer diagnostics display playback mode and native presentation metrics.
The package checks require native direct-video/HLS definitions, both video
controllers and the original codec/launcher/socket definitions.

The complete package also exceeds Java XMLDSig's 30-reference default.
Verification now permits at most 256 references while retaining secure
validation and every other installed JDK policy rule. The full WGT is
verified cryptographically; a signing regression uses 35 additional real
payload files, and a negative test rejects more than 256 references. RSA
minimums and the exact public-SDK distributor exception remain unchanged.

## Automated boundaries

Run the browser suites sequentially; the native runtime uses fixed ports:

```bash
./scripts/build-local.sh
ctest --test-dir native/build --output-on-failure
python3 tests/test_gui.py
python3 tests/test_mirror_gui.py
python3 tests/test_share_gui.py
python3 tests/test_photo_gui.py
python3 tests/test_direct_video_gui.py
```

`test_direct_video_gui.py` uses the real receiver, photo uploads, URL playback,
authenticated HLS manifest/init/segment routes and decoded Chromium media.
It checks missing/wrong tokens, foreign Origin, invalid/stale segments and
stream shutdown. It assembles MP4 bytes from the actual HLS manifest and
verifies decoded dimensions and advancing playback in Chromium.

Its direct-video controller test simulates only PPAPI messages and commands.
This tests media arbitration, metrics, fallback and cleanup, not the Samsung
GPU or native HLS engine. Host tests, ASan/UBSan, the ARM cross-build, `ncval`
and signed-package checks cannot establish new physical-device acceptance.
Retest the resulting WGT on Q7C and the requested UE65MU6100/UE65MU6405;
record firmware, sender OS, package hash, sustained playback and A/V offset.
AirPlay 2, DRM, multiroom and production-wide compatibility remain unverified
or unsupported as described in the production-readiness document.
