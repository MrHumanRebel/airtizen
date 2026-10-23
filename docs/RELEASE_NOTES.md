Experimental single-WGT AirTizen receiver for Samsung Tizen 3.0.

Preserves PR #27's direct PPAPI hardware H.264 decoder, GLES2 rendering,
TV-local HLS fallback and mirror transport reconnection. That contributor
verified 1728×1080 macOS mirroring and separate audio on QE49Q7CAM/Q7C.
The reported 178.327 ms average is a TV-side presentation sample, not
end-to-end latency or a guarantee. A/V synchronization was not measured.

Fixes the merged GUI test's stale Native-default assumption while retaining
the working `uxplay-test` default and saved profile selection. Restores
photo/URL-sharing handling during direct playback and MSE mirroring in
NaCl-less browsers. HLS stops on stream end, clears stale callbacks and
uses generation-bound initialization URLs. Host HLS no longer emits empty
authentication queries. Diagnostics show playback mode/presentation metrics.
Regression coverage includes real decoded media and authenticated HLS routes;
the simulated PPAPI message test does not execute a Samsung GPU.
Complete-package signature verification now supports up to 256 file/property
references without disabling XMLDSig secure validation or weakening key policy.

The WGT contains the web UI and statically linked ARM NaCl receiver/codecs.
No native service TPK, codec download or PC relay is required. The TV supplies
its built-in NaCl/PPAPI/media platform. Corresponding sources and license
notices accompany the package. Device-wide production acceptance remains
outstanding; AirPlay 2 multiroom, slideshow pull and DRM are unsupported.

The default package uses the public community author and SDK distributor for
sideloading. See SIGNING-MODE.txt and SHA256SUMS. Developer Mode is required;
firmware may require re-signing with its authorized Samsung distributor.
Keep the existing author identity for upgrades. See
`docs/DIRECT_VIDEO_VALIDATION.md` for evidence and the next device checks.
