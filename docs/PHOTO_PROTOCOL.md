# WGT-local legacy AirPlay photos

Implementation references: [photo requests](https://github.com/openairplay/airplay-spec/blob/master/src/photos/http_requests.md), [asset caching](https://github.com/openairplay/airplay-spec/blob/master/src/photos/photo_caching.md), [reverse events](https://github.com/openairplay/airplay-spec/blob/master/src/photos/events.md), inspected 2026-10-16. No implementation code was imported.

`PUT /photo` accepts JPEG/PNG bodies. `X-Apple-AssetAction: cacheOnly` preloads a keyed asset without replacing the displayed image; `displayCached` selects it without a body and returns 412 when absent. Cache lookup includes `X-Apple-Session-ID`. This isolates cache namespaces; that header is sender-supplied, not authentication. HTTP header lookup is case-insensitive.

The cache holds four entries and at most 16 MiB compressed data. A photo is at most 8 MiB, 8192 pixels on either axis and 16 million pixels total. Header dimension checks happen before browser decoding; malformed compressed content is separately rejected by the browser and reported to the native state. The active image stays pinned while old preload entries are evicted. Images are never written to disk; stop clears the cache. Only the `/photo` PUT body gets the expanded limit; other protocol bodies retain the 1 MiB limit.

The TV UI obtains generation-bound images from the WGT's loopback receiver using its ephemeral control token. It supports immediate replacement and a two-image dissolve, fits images to the screen and reports successful/failed decoding. Reverse PTTH channels observe a photo session and receive its `photo/stopped` event with a numeric session ID. Back stops the receiver and clears the photo. URL playback and a new mirroring configuration dismiss photos; displayed photos dismiss URL video and old mirror fragments.

Native tests cover bounds, session namespaces, eviction and retained active images. `tests/test_photo_gui.py` starts the actual receiver, uploads and decodes PNG/JPEG in Chrome, exercises >1 MiB uploads, cache commands, generation checks, reverse stop events and remote navigation. Desktop tests do not establish Samsung firmware or current iPhone behavior.

This implements legacy push photos, not the separate `/slideshows/1` reverse asset-fetch protocol, HEIF decoding or AirPlay 2. Those capabilities are not advertised. Coordinated audio/video playout and persistent sender authorization remain separate production gates.
