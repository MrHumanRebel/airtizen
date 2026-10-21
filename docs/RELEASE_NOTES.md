Experimental single-WGT AirTizen receiver for Samsung UE65MU6100 / Tizen 3.0.

This update adds the legacy RAOP `Apple-Challenge`/`Apple-Response` exchange
and prevents silently regenerated receiver IDs when browser storage is
unavailable. Existing IDs are preserved; new IDs can use Tizen's network MAC.
The latest UE65MU6405/iOS 27.0.1 report verifies discovery and the earlier
`/info` fixes, but still stops after OPTIONS. Whether that iPhone sends a
challenge is unknown; the new safe diagnostics distinguish this explicitly.
The new handler's RSA signatures and subsequent pairing pass host checks;
physical-device connection and playback with this update need retesting.

The native implementation now includes ALAC/AAC audio, encrypted H.264 mirroring with an MSE browser renderer, music metadata/artwork and HTTP URL audio/video sharing with playback controls. Linux/Chromium tests exercise these paths; real Apple sender and Samsung TV compatibility remain unverified. Mirroring A/V synchronization, JPEG/PNG photo uploads, preloading, cached display and dissolve transitions now run locally in the WGT. AirPlay 2 multiroom, slideshow pull sessions and DRM playback are not supplied.

This signed WGT contains the web UI and the actual ARM NaCl receiver, including codecs and PPAPI audio output. It does not require a native service TPK or PC relay. The ARM binary passes the Samsung SDK sandbox validator and package/signature checks; actual execution, discovery and media output on UE65MU6100 remain unverified. This is not yet a production-ready full AirPlay implementation. Corresponding source archives and license notices accompany the package.

The default package uses the public community author and SDK distributor for sideloading; configured identities are also supported. See SIGNING-MODE.txt. Package signatures are verified, but public SDK signing does not prove authorization on every Samsung firmware. Enable Developer Mode and, if required by the device, re-sign with its authorized Samsung distributor. Keep the existing author identity for upgrades. SHA256SUMS accompanies the artifact.
