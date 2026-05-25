# AirTizen

AirTizen is a Samsung Tizen TV AirPlay receiver project for old Samsung TVs such as UE65MU6100 / Tizen 3.0.

It is structured as a Tizen WGT project with a TV-safe ES5 GUI and a native/WASM AirPlay core port tree based on the bundled AirPlayServerLib reference implementation.

## Build WGT with GitHub Actions

Push this repository, then run:

```text
GitHub → Actions → Build AirTizen WGT → Run workflow
```

For releases:

```bash
git tag -f 2.2.2
git push -f origin 2.2.2
```

The workflow uploads `AirTizen.wgt` as an artifact and attaches it to tag releases.

## Install to TV

```bash
sdb connect <TV_IP>
tizen install -n AirTizen.wgt
```

## Repository layout

```text
app/            Tizen TV WGT app shell and GUI
native/         AirTizen native/WASM adapter layer
third_party/    AirPlayServerLib donor receiver core
.github/        Jellyfin-style WGT build workflow
```

## Important implementation note

The included `third_party/AirPlayServerLib` is the real AirPlay/RAOP receiver core source tree. The AirTizen native layer is the Tizen platform boundary for socket/audio output. This is the correct architecture for moving beyond the previous TizenBrew JS prototype, which reached Bonjour discovery but failed at the real AirPlay verification/audio pipeline.

## v2.2.2 logo hotfix

- The user-provided AirTizen logo is embedded as `app/assets/airtizen-logo.png`.
- The Tizen launcher/taskbar icon is `app/icon.png`, generated only by resizing the provided logo.
- `config.xml` points to `<icon src="icon.png"/>`.
