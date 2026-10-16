# GitHub Actions build and release

`Validate, build and release AirTizen` runs on pull requests and main pushes, and can be dispatched manually. Pull requests run validation without access to signing secrets or write permissions. Main builds publish experimental WGT prereleases only after all validation and signing succeed.

## Required repository Actions secrets

Enter values securely in GitHub repository settings, never in an issue or chat:

| Secret | Purpose |
| --- | --- |
| `TIZEN_CERT_P12_BASE64` | Base64 author P12; retain the existing author identity for upgrades |
| `TIZEN_CERT_PASSWORD` | Author P12 password |
| `TIZEN_DISTRIBUTOR_P12_BASE64` | Samsung TV distributor P12 authorizing the intended device |
| `TIZEN_DISTRIBUTOR_PASSWORD` | Distributor P12 password |

The workflow requires actual author and Samsung distributor credentials. It never substitutes a development identity or unsigned ZIP. Use RSA keys of at least 2048 bits. Preserve the existing author identity for upgrades.

The official Tizen Studio 4.5.1 Web CLI installer is pinned by SHA-256 in `install-tizen-web-cli.sh`; its embedded payload is also verified. There is no optional checksum override. The installer uses an isolated JVM `user.home` so it works without writing to the shell home directory. Installation, staged build, interactive signing through a bounded PTY, and cryptographic validation have been exercised locally with temporary 3072-bit development certificates. Those certificates establish the signing workflow, not Samsung device authorization. The Web CLI does not supply a compatible native Tizen 3.0 TV rootstrap, and no repository Actions run or Samsung installation is claimed by the local tests.

## Automatic versioning

`scripts/version.py` requires full Git history and tags. It selects the greatest reachable numeric SemVer tag at or above the base version in `app/config.xml`, then adds the first-parent commit distance to its patch component. Without a suitable tag, it uses the base version plus the first-parent history count. Both `v2.2.3` and `2.2.3` tags are recognized; prerelease/non-numeric tags are ignored. Component overflow fails the build.

The resulting version is deterministic for a commit and tag history. `stage-web.py` stamps only a disposable copy of `app/`, including the GUI version. Tracked manifests are never rewritten by CI. Set a deliberate higher base major/minor in source for a new version line. Do not force-update existing release tags.

The release job uses `v<version>` at the validated commit. Rerunning an existing release leaves its artifacts unchanged. It serializes branch builds and publishes with `--prerelease`, since full AirPlay and physical-TV readiness remain unresolved.

## Package validation

Before upload, `verify-wgt.py` checks archive paths/duplicates, expanded size, required files, version and signed payload coverage/digests. Its CLI also invokes `VerifyWgtSignature.java` using JDK XMLDSig to verify both RSA signatures and all XML references with external resolution disabled. A regression test modifies the actual RSA signature and requires rejection. The signer refuses existing signatures/output packages so the SDK cannot silently reuse a previous signed artifact.

These checks do not establish Samsung certificate-chain trust, device authorization or successful installation. SHA256SUMS detects artifact corruption; it does not prove device compatibility.

The current workflow releases the **WGT UI only**. It does not create a native service TPK and cannot be called a complete receiver release. Release notes explicitly state this limitation. Do not mark releases stable until the device acceptance criteria in PRODUCTION_READINESS.md pass.
