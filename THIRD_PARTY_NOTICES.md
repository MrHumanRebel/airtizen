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

This is an initial inventory, not a completed licensing/provenance audit. The combined native binary must not be described as MIT-only. Resolve redistribution compatibility and include the required complete notices, corresponding source and build material before distributing a native service TPK. No UxPlay or Moonlight implementation was imported as part of the research.
