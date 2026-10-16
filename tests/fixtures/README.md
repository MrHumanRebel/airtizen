# Generated media fixtures

`stereo-44100-352.pcm` is 352 stereo signed 16-bit little-endian samples at 44.1 kHz. Left is a 440 Hz sine at amplitude 12000; right is a 660 Hz cosine at amplitude 9000. These are generated tones, not copyrighted music.

`stereo-44100-352.alac` is the single ALAC packet produced from that PCM with the system FFmpeg 7.1.5 encoder:

```
ffmpeg -f s16le -ar 44100 -ac 2 -i input.pcm -map 0:a:0 \
  -c:a alac -sample_fmt s16p -frames:a 1 -f data output.alac
```

The short final packet explicitly carries 352 samples. Regression tests require byte-for-byte lossless PCM equality, both directly and after RTP/AES-CBC transport decryption. The fixture is separate from the pinned decoder build.

`mirror-320x180.h264` contains 30 generated test-pattern frames. `shared-av.mp4` contains three seconds of a generated test pattern and sine-wave audio. Both were created with system FFmpeg; no third-party recordings are included. Equivalent fixtures can be generated with:

```bash
ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30 -frames:v 30 \
  -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p -f h264 mirror.h264
ffmpeg -f lavfi -i testsrc2=size=320x180:rate=30 \
  -f lavfi -i sine=frequency=440:sample_rate=44100 -t 3 \
  -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p \
  -c:a aac -movflags +faststart shared.mp4
```

Encoder versions may produce different bytes. Tests assert decoded behavior, transport boundaries and timestamps rather than encoded-file hashes.
