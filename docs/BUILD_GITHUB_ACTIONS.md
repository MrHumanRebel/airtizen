# GitHub Actions WGT build

The workflow installs Tizen Studio Web CLI, creates a signing profile, runs `tizen build-web`, then packages `AirTizen.wgt`.

Optional repository secrets:

- `TIZEN_CERT_P12_BASE64`: base64 encoded Samsung/Tizen author certificate.
- `TIZEN_CERT_PASSWORD`: certificate password.

Without secrets the workflow creates a temporary author certificate for test builds.
