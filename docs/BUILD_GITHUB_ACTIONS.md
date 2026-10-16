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

The workflow does not create a self-signed identity or substitute an unsigned ZIP if signing fails. The SDK is downloaded from the official HTTPS Tizen installer endpoint with TLS verification enabled. Optionally set repository variable `TIZEN_INSTALLER_SHA256` to an independently verified checksum; the build checks it when supplied. Never copy an error page hash into this variable to satisfy verification.

The pinned Web CLI installer is Tizen Studio 4.5.1. Its availability and signing operation have not yet been validated in this cloud environment: the installer request returned HTTP 403. This is distinct from the native Tizen 3.0 TV toolchain, which the Web CLI does not provide.

## Automatic versioning

`scripts/version.py` requires full Git history and tags. It selects the greatest reachable numeric SemVer tag at or above the base version in `app/config.xml`, then adds the first-parent commit distance to its patch component. Without a suitable tag, it uses the base version plus the first-parent history count. Both `v2.2.3` and `2.2.3` tags are recognized; prerelease/non-numeric tags are ignored. Component overflow fails the build.

The resulting version is deterministic for a commit and tag history. `stage-web.py` stamps only a disposable copy of `app/`, including the GUI version. Tracked manifests are never rewritten by CI. Set a deliberate higher base major/minor in source for a new version line. Do not force-update existing release tags.

The release job uses `v<version>` at the validated commit. Rerunning an existing release leaves its artifacts unchanged. It serializes branch builds and publishes with `--prerelease`, since full AirPlay and physical-TV readiness remain unresolved.

## Package validation

Before upload, `verify-wgt.py` requires both author/distributor signature documents, checks archive paths and duplicates, checks every author-covered payload digest, and requires the staged version. This is structural and digest validation, **not a full XML signature or Samsung certificate trust verifier**. Signing is delegated to the Tizen CLI; acceptance on the target TV must still be tested. SHA256SUMS detects artifact corruption; it does not prove device compatibility.

The current workflow releases the **WGT UI only**. It does not create a native service TPK and cannot be called a complete receiver release. Release notes explicitly state this limitation. Do not mark releases stable until the device acceptance criteria in PRODUCTION_READINESS.md pass.
