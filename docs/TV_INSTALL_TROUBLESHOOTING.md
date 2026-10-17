# Samsung UE65MU6100 installation error 118

The reported log transfers AirTizen successfully but fails installation with `118`; Jellyfin installs on the same TV. This establishes a working connection and sideload path. `cmd_ret:0` is not installation success: the platform separately prints `install failed[118]`. Error 118 alone does not distinguish certificate-chain rejection, manifest parsing, an existing-author mismatch and other installer errors.

Inspection of released AirTizen 2.2.24 found a self-signed author certificate with no Tizen issuer chain and no code-signing usage. The inspected Jellyfin 10.11.z WGT uses a Tizen Developers CA-issued author with digital-signature/code-signing usage and includes its CA certificate. Both packages use the same public SDK distributor chain. This fix reissues AirTizen's existing author RSA key under that official SDK developer CA and includes the issuer in the WGT. It does not use Jellyfin's key or change AirTizen's application/package IDs. The manifest also uses an absolute widget URI, the general TV screen feature instead of an exact-resolution capability filter, and foreground-only behavior, matching the deployment assumptions of the legacy NaCl application.

The default build validates the author chain, certificate purpose, validity and unchanged public key before signing, then checks that the complete author chain was embedded in the actual WGT. Existing RSA/XML signature and payload verification remains enabled. The developer CA expires on January 1, 2027; CI fails on expiry and requires renewal rather than accepting expired author chains. Optional Samsung private signing identities remain supported.

Install the corrected signed WGT in Windows, selecting the actual target:

```bat
cd /d C:\tizen-studio\tools\ide\bin
tizen.bat install -n AirTizen.wgt -t UE65MU6100 -- C:\path\to\download
```

On Linux/macOS with the SDK on PATH:

```bash
scripts/install-to-tv.sh TV_IP /path/to/AirTizen.wgt UE65MU6100
```

The helper selects the package directory and target explicitly and rejects failure text even if the CLI returns zero. It never uninstalls an existing app automatically.

If 118 remains, retain the entire platform log, including any secondary code/reason, the WGT SHA-256/version and whether AirTizen was already installed. An existing app signed with a different author can require its original identity. Do not delete it blindly. Some firmware requires a Samsung distributor authorizing that TV; optional certificate settings are described in BUILD_GITHUB_ACTIONS.md. Physical installation of this corrected artifact on the user's TV remains the final verification; local chain/signature tests cannot establish device acceptance.

References inspected: the user's AirTizen/Jellyfin installation logs, [Jellyfin build workflow](https://github.com/jeppevinkel/jellyfin-tizen-builds/blob/master/.github/workflows/build-new-release.yml), its released author/distributor chains, and [legacy NaCl TV manifest](https://github.com/OneLiberty/moonlight-tizen-nacl/blob/samsung_nacl/config.xml).
