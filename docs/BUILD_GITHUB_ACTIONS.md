# GitHub Actions WGT build

The workflow installs Tizen Studio Web CLI, creates a signing profile, runs `tizen build-web`, and packages the generated `.buildResult` directory into a signed WGT artifact.

Optional repository secrets:

- `TIZEN_CERT_P12_BASE64`: base64-encoded Samsung/Tizen author certificate.
- `TIZEN_CERT_PASSWORD`: certificate password.

Without secrets the workflow creates a temporary author certificate so CI packaging can still produce a test WGT artifact.

## Release flow

Push a tag to create a GitHub release with the generated WGT attached:

```bash
git tag -f 2.2.2
git push -f origin 2.2.2
```

The workflow now fails hard if no `.wgt` file is produced, and it prints Tizen CLI logs on failure instead of silently uploading an empty artifact.

## Real Samsung TV install note

For installing on a physical Samsung TV, use a Samsung/Tizen certificate profile that is valid for the target TV. The temporary CI certificate is useful for validating that the build and packaging pipeline works, but a real device may reject a WGT that is not signed with an appropriate TV/developer certificate.
