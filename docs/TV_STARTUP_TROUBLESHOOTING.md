# UE65MU6100: installed WGT, receiver startup failure

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
