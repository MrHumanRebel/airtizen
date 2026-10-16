# GitHub Actions build and release

`Validate, build and release AirTizen` runs on pull requests and main pushes, and can be dispatched manually. Pull requests run validation without access to signing secrets or write permissions. Main builds publish experimental WGT prereleases only after all validation and signing succeed.

## Default public sideload signing and optional Actions secrets

No secrets are required for the default community build. Like [jellyfin-tizen-builds](https://github.com/jeppevinkel/jellyfin-tizen-builds/blob/master/.github/workflows/build-new-release.yml), it uses a stable author P12 and the public distributor shipped with the official Tizen SDK. AirTizen has its own deliberately public 3072-bit community author in `certificates/community-author.p12` (password `1234`); no Jellyfin key is copied. The SDK public distributor password is part of the SDK tooling, not a repository secret.

The output is genuinely signed, not an unsigned ZIP fallback. `SIGNING-MODE.txt` records the author/distributor modes and ships alongside the WGT and checksums. The public key identifies a community sideload build; it does not authenticate a trusted publisher or establish Samsung device authorization. TV Developer Mode and testing on the target firmware remain necessary.

To retain an existing private author or use a device-specific Samsung distributor, enter these optional pairs securely in GitHub repository settings, never in an issue or chat:

| Secret | Purpose |
| --- | --- |
| `TIZEN_CERT_P12_BASE64` | Base64 author P12; retain the existing author identity for upgrades |
| `TIZEN_CERT_PASSWORD` | Author P12 password |
| `TIZEN_DISTRIBUTOR_P12_BASE64` | Samsung TV distributor P12 authorizing the intended device |
| `TIZEN_DISTRIBUTOR_PASSWORD` | Distributor P12 password |

Each configured P12 requires its password; invalid or incomplete configured credentials fail rather than silently falling back. An author P12 can be configured without a distributor P12: that combination uses the SDK public distributor. Supplied author/distributor RSA keys require at least 2048 bits. Preserve the existing author identity for upgrades; changing from an earlier identity to the community identity may require uninstall/reinstall.

The SDK public distributor has a legacy 1024-bit test key and an expired certificate. Following the SDK sideload path does not establish modern certificate-chain trust. Its exact certificate SHA-256 (`cdd81c11422bc3e78288150af2e70787a3373be548f0bcf99ac8a5bd166b9008`) is accepted only with explicit `--allow-sdk-distributor` and only for `signature1.xml`. The author requirement remains unchanged; arbitrary weak certificates remain rejected. JDK secure XML validation, RSA signature verification and every reference digest remain enabled. Default/custom package signing, signature tampering and rejection of the SDK key as an author are exercised against the real CLI in `tests/test_signing_modes.py`.

The official Tizen Studio 4.5.1 Web CLI installer is pinned by SHA-256 in `install-tizen-web-cli.sh`; its embedded payload is also verified. There is no optional checksum override. The installer uses an isolated JVM `user.home` so it works without writing to the shell home directory. Installation, staged build, headless signing with SDK-encoded inline passwords in a private temporary profile and a bounded PTY wrapper, and cryptographic validation have been exercised locally with both the community/SDK public identities and configured 3072-bit development identities. Those certificates establish the signing workflow, not Samsung device authorization. The Web CLI does not supply a compatible native Tizen 3.0 TV rootstrap, and no repository Actions run or Samsung installation is claimed by the local tests.

## Automatic versioning

`scripts/version.py` requires full Git history and tags. It selects the greatest reachable numeric SemVer tag at or above the base version in `app/config.xml`, then adds the first-parent commit distance to its patch component. Without a suitable tag, it uses the base version plus the first-parent history count. Both `v2.2.3` and `2.2.3` tags are recognized; prerelease/non-numeric tags are ignored. Component overflow fails the build.

The resulting version is deterministic for a commit and tag history. `stage-web.py` stamps only a disposable copy of `app/`, including the GUI version. Tracked manifests are never rewritten by CI. Set a deliberate higher base major/minor in source for a new version line. Do not force-update existing release tags.

The release job uses `v<version>` at the validated commit. Rerunning an existing release leaves its artifacts unchanged. It serializes branch builds and publishes with `--prerelease`, since full AirPlay and physical-TV readiness remain unresolved.

## Package validation

Before upload, `verify-wgt.py` checks archive paths/duplicates, expanded size, required files, version and signed payload coverage/digests. Its CLI also invokes `VerifyWgtSignature.java` using JDK XMLDSig to verify both RSA signatures and all XML references with external resolution disabled. A regression test modifies the actual RSA signature and requires rejection. Strict verification is the default; public SDK packages require the explicit compatibility option described above. The CLI-generated `.pwd` password-storage references are replaced with inline values encoded by the SDK’s own `CipherUtil`, using XML serialization in a mode-0600 temporary file, avoiding desktop keyring/console dependence; the cleanup trap deletes that file and restores the original CLI profile path. A regression signs with stdin disconnected, after proving missing password storage fails. The signer refuses existing signatures/output packages so the SDK cannot silently reuse a previous signed artifact.

These checks do not establish Samsung certificate-chain trust, device authorization or successful installation. SHA256SUMS detects artifact corruption; it does not prove device compatibility.

The `native-wgt` job cross-builds the actual ARM NaCl receiver on PRs and main, validates it using the Samsung SDK, tests incomplete/unsafe packages, and stages it inside the UI. The signing job consumes that payload and explicitly requires the embedded module before release. Corresponding receiver, FFmpeg and NaCl runtime source archives accompany the WGT. No separately installed TPK is required. TV execution and full protocol coverage remain unverified. Do not mark releases stable until the device acceptance criteria in PRODUCTION_READINESS.md pass.

## Headless signing regression

After installing the SDK, run:

```bash
export TIZEN_STUDIO_ROOT=/path/to/tizen-studio
export JAVA_TOOL_OPTIONS=-Duser.home=/path/to/isolated-sdk-user
python3 tests/test_signing_modes.py
```

The suite covers six real CLI scenarios, including a package command with stdin disconnected and stdout captured. Missing `.pwd` storage must fail first; encoding the inline passwords with the SDK API must then produce a cryptographically valid package without a console or desktop keyring. The regular public build also asserts that the signer does not ask for author/distributor passwords. Plaintext XML substitution is insufficient with this pinned SDK: it attempts to decrypt non-`.pwd` values using `CipherUtil`.
