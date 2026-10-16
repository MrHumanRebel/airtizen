# Public community author identity

`community-author.p12` is an intentionally public AirTizen sideload author key.
Its password is `1234`; neither the key nor this password is a secret. It is
not a Samsung-issued identity and does not authenticate a trusted publisher.
Never use it for production distribution or protect confidential material with it.

It was generated specifically for this project using a 3072-bit RSA key and
SHA-256 self-signed certificate. The P12 uses the legacy encryption supported by
Tizen Studio's bundled JDK 8. Certificate SHA-256 fingerprint:

`cbe1f3a0df46fa71d04d7e05d5cd7c8185272a00d2a876d039d42888c00ce9b6`

Keep this identity stable: a different author key can prevent updates of an
already installed app. Builds made with earlier random or private author keys
may require uninstall/reinstall or signing with the original author instead.

By default CI uses this author and the public distributor P12 shipped in the
checksum-verified Tizen SDK, following the public sideload build pattern used
by jellyfin-tizen-builds. No Jellyfin private key was copied. For your own author,
supply both TIZEN_CERT_P12_BASE64 and TIZEN_CERT_PASSWORD. For a Samsung device
distributor identity, also supply its P12 and password through Actions secrets.

The SDK public distributor is a legacy 1024-bit test certificate. Its exact
certificate fingerprint is allowlisted only for the distributor XML signature
and only when explicitly requesting the public SDK verification policy. All
signatures and payload references are still verified. Neither that exception
nor a valid package signature proves Samsung device trust or installation.
