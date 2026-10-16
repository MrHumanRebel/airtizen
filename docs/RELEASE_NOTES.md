Experimental AirTizen receiver UI for Samsung UE65MU6100 / Tizen 3.0.

The native implementation now includes ALAC/AAC audio, encrypted H.264 mirroring with an MSE browser renderer, music metadata/artwork and HTTP URL audio/video sharing with playback controls. Linux/Chromium tests exercise these paths; real Apple sender and Samsung TV compatibility remain unverified. Mirroring A/V synchronization, AirPlay 2 multiroom, photos and DRM playback are not supplied.

This signed WGT contains the web UI only and requires a separately installed native receiver service. No verified Tizen 3.0 native service TPK is supplied by this workflow. Do not treat this prerelease as a production-ready full AirPlay receiver.

The default package uses the public community author and SDK distributor for sideloading; configured identities are also supported. See SIGNING-MODE.txt. Package signatures are verified, but public SDK signing does not prove authorization on every Samsung firmware. Enable Developer Mode and, if required by the device, re-sign with its authorized Samsung distributor. Keep the existing author identity for upgrades. SHA256SUMS accompanies the artifact.
