# AirPlay core port

The native receiver source is bundled under `third_party/AirPlayServerLib`.

AirTizen's adapter boundary:

- `native/src/airtizen_core.c`: RAOP lifecycle and callbacks.
- `native/ports/tizen_socket_backend.c`: Tizen socket backend boundary.
- `native/ports/tizen_audio_output.c`: PCM output boundary.

The old TizenBrew prototype proved discovery: iPhone could see the receiver. Full production receiver behavior requires the native core route so that pair-verify, FairPlay setup, RTP audio, decode and PCM output are handled by the receiver core rather than mocked from JavaScript.
