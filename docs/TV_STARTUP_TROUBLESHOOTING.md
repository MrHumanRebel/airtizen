# UE65MU6100: installed WGT, receiver startup failure

## 2.2.26 regression: Nexe crashed during startup

The follow-up TV photos show `Nexe crashed during startup` before loopback
status is available. Inspection of the 1.87 MiB WGT confirms a packaging
regression: the plist archive included the standalone `cnary.c` test program.
After the socket dependency ordering was corrected, its `main()` was selected
before the SDK PPAPI launcher. The linker removed the unreachable receiver
and decoder code. The earlier explanation that this size reduction was just
optimization was incorrect. Do not use the 2.2.26 package.

Exclude `cnary.c` from the library and retain the three browser PPAPI entry
points explicitly. The post-link check now requires the actual PPAPI launcher,
its call from `main()`, the receiver entry point, and AAC/ALAC/H.264 decoder
definitions, in addition to correct socket dispatch. The independent package
validator also checks these definitions directly in the ELF symbol table and
rejects dynamically linked payloads. Negative tests remove each required
symbol and introduce a dynamic segment; every altered payload is rejected.

The corrected ARM module is approximately 8.2 MiB before ZIP compression.
The signed WGT contains the statically linked receiver, FFmpeg codecs, crypto,
plist and nacl_io implementation. It requires the TV's built-in NaCl/PPAPI
and browser/media platform, but no separate receiver TPK, codec download or
PC relay. This is an inventory of the implemented receiver; it does not add
AirPlay 2, multiroom, DRM or coordinated audio/video playout.

## Earlier socket initialization failure

The October 17, 2026 device photos show Tizen 3.0, WebKit 538.1,
`Embedded receiver: loaded`, loopback status data, `raop_start_failed`, and
`Error initialising socket 38`. They also show restart requests returning
HTTP 403. This verifies installation, module loading and some loopback HTTP
communication on this TV. It does not verify AirPlay media playback.

Disassembly of the previous signed WGT confirms that `getsockname()` sets
errno to 38 (ENOSYS) and returns -1. The executable listed `nacl_io` before
the transitive receiver-core archive. Socket calls first referenced by that
archive consequently resolved to newlib's linkable stubs, including
`getsockname`, address resolution and UDP send/receive. The already-working
loopback listener does not use `getsockname`, explaining the partial startup.

The receiver core now declares its own `nacl_io` dependency so that the socket
implementation follows all receiver archives. The ARM build checks public
socket dispatch after linking, before sandbox validation and packaging.
Checking for the mere presence of `ki_*` symbols would miss the original bug;
the check rejects public functions that do not branch to their nacl_io
implementation. PNaCl may inline correct bitcode wrappers into callers.

For legacy WebKit POST requests, the control server accepts the exact
`Origin: file://` literal in nonce-protected mode and echoes it in CORS headers.
Missing/wrong nonces, foreign origins, duplicate origins and nonliteral file
origins remain rejected. The host's unauthenticated test mode does not accept
file origins. The photos do not expose the rejected request headers, so this
is a compatibility fix for a possible 403 cause; diagnostics now display safe
server error identifiers such as `invalid_origin` or `unauthorized` to make
any remaining rejection actionable without logging credentials.

After installing the new complete signed WGT, launch AirTizen and check that
diagnostics show RAOP and mDNS online. On the same LAN, select AirTizen from
iPhone's Screen Mirroring or audio output menu. Record the new version, TV
firmware, iOS version, status and logs if connection or playback fails. Cloud
tests and ARM sandbox validation cannot replace this physical retest.
