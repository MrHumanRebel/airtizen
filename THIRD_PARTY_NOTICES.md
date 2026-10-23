# Third-party notices

The bundled `third_party/AirPlayServerLib` came from a user-provided reference archive. Its precise upstream revision/provenance is not established. Preserve every existing copyright and license header.

The top-level MIT license applies to original AirTizen work; it does not relicense bundled components. In particular:

| Component | License evidence in this checkout |
| --- | --- |
| RAOP and plist sources | LGPL 2.1 or later headers in `lib/raop.c`, `lib/plist/plist.c` and related files |
| PlayFair | GPLv3 text at `third_party/AirPlayServerLib/lib/playfair/LICENSE.md` |
| FDK AAC | Fraunhofer FDK AAC license at `third_party/AirPlayServerLib/lib/fdk-aac/NOTICE` |
| axTLS crypto | BSD-style terms in `third_party/AirPlayServerLib/lib/crypto/crypto.h` and related files |
| curve25519-donna | BSD-style terms in `third_party/AirPlayServerLib/lib/curve25519/curve25519-donna.c` |

This is an initial inventory, not a completed licensing/provenance audit. The combined native binary must not be described as MIT-only. Resolve redistribution compatibility and include the required complete notices, corresponding source and build material for native binary distribution. No UxPlay or Moonlight implementation was imported as part of the research.

## Legacy RAOP challenge interoperability data

`native/ports/airport_key.h` contains the publicly documented AirPort RSA
interoperability key from [Shairplay's `airport.key` at
096b61ad14c90169f438e690d096e3fcf87e504e](https://github.com/juhovh/shairplay/blob/096b61ad14c90169f438e690d096e3fcf87e504e/airport.key).
The upstream data file SHA-256 is
`7cec0bf8683f595f534f6cf2636b162aec7f006324eccdf17669bbc3583f82db`.
The same published key is used by Shairport Sync. It is protocol compatibility
data, not an installation secret, proof of device trust or an Ed25519 pairing
identity. AirTizen's new handler uses the existing bundled LGPL `rsakey` and
BSD-style axTLS arithmetic implementation; no new crypto library is downloaded
or linked. `tests/fixtures/airport-public.pem` contains only its public key for
independent verification of the response's RSA block.

## Downloaded media dependency

The GLES2 presentation in `native/nacl/video_host.h` follows the Chromium
Pepper 47 `examples/api/video_decode/video_decode.cc` example, copyright
2014 The Chromium Authors. Its BSD redistribution terms are reproduced in
`native/nacl/LICENSE.chromium` and included in the WGT as
`licenses/Chromium-video-decode.txt`. The pinned Samsung SDK contains the
reference example; the release's NaCl runtime archive includes its library
sources, public headers and SDK notices.

`scripts/build-media-deps.sh` builds a minimal static FFmpeg 7.1.5 dependency from the official FFmpeg repository, commit `3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`. The codeload archive SHA-256 is `83220fe0807da10df19714176f02ec3d3812ef0e055f69e940d01b3ad1379cdb`. It enables AAC/ALAC/H.264 decoding, H.264 parsing and MP4 muxing, without GPL/nonfree configuration flags. FFmpeg's applicable LGPL notices and source are in the downloaded source tree (`COPYING.LGPLv2.1`, `LICENSE.md` and individual headers).

Before distributing a statically linked native binary, provide the corresponding dependency source, build configuration, notices and any object/relinking material required by the applicable licenses. This build script is reproducibility evidence, not a completed redistribution compliance assessment. The receiver is now distributed inside the WGT; FDK is used only by host encoder tests, not linked into the NaCl module. The workflow includes GPLv3, FFmpeg and Samsung NaCl notices in the WGT and publishes receiver, FFmpeg and NaCl runtime source archives with build scripts. The combined binary includes GPLv3 PlayFair; the repository MIT license does not cover the entire binary. Donor provenance and a complete redistribution audit remain outstanding.
