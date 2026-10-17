# Public community author identity

`community-author.p12` is an intentionally public AirTizen sideload author key.
Its password is `1234`; neither the key nor this password is a secret. It is
not a Samsung-issued identity and does not authenticate a trusted publisher.
Never use it for production distribution or protect confidential material with it.

It was generated specifically for this project using a 3072-bit RSA key and
SHA-256 certificate. The original self-signed certificate lacked the SDK developer trust chain; it has been reissued under the official SDK Tizen Developers CA, preserving the exact RSA key and subject. The SDK CA certificate is included in the P12 and author XML signature. The certificate has digital-signature and code-signing usage. The P12 uses the legacy encryption supported by
Tizen Studio's bundled JDK 8. Certificate SHA-256 fingerprint:

`200c458f222065ccd8a17bff3795ce2ed561f54a91ccbd8d72f616adf8b264d5`

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

The author and SDK developer CA expire on 2027-01-01. CI checks their validity and fails with a renewal error after expiry; it does not bypass date verification. `scripts/repair-community-author.py` documents reproducible reissuance using the verified SDK and the existing public identity. The SDK CA signing material is deliberately distributed by Tizen for developer certificates; no Jellyfin signing key is used. `scripts/check-public-author.py` checks the pinned CA, stable public key, certificate signature, purpose, dates and the actual WGT author chain. This checks the known SDK development chain, not Samsung device authorization.
