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

## Native receiver build

Host build now compiles complete RAOP/AAC runtime (useful for protocol and discovery tests):

```bash
./scripts/build-local.sh
native/build/airtizen_runtime "AirTizen TV"
./scripts/check-runtime-status.sh
```

Tizen native SDK build must enable service target and provide `service-app`, `dlog`, and
`audio-io` packages from TV native toolchain:

```bash
cmake -S native -B native/build-tizen \
  -DCMAKE_TOOLCHAIN_FILE="$TIZEN_TOOLCHAIN" \
  -DAIRTIZEN_BUILD_TIZEN_SERVICE=ON
cmake --build native/build-tizen
```

Stock Tizen TV does not support WGT/TPK hybrid packaging. Install signed WGT UI and
signed native service TPK separately. Direct receiver needs native service running:
WGT alone cannot listen on RAOP TCP port or output PCM. Partner-level certificate and
device-tested TV native SDK remain deployment prerequisites.

## Runtime architecture

1. Native service listens on TCP 5000 using bundled RAOP receiver.
2. Native mDNS advertiser publishes `_raop._tcp` and `_airplay._tcp`, including TV IPv4 A record.
3. Decoded 16-bit PCM goes to Tizen Audio I/O (`audio_out_*`).
4. WGT reads native service status at `127.0.0.1:45110`.

No LAN relay is required when stock firmware permits installed native service to accept
port 5000. If device policy blocks service startup or inbound TCP, external LAN relay is
only viable fallback; WASM/WGT cannot bypass SMACK policy.

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
