# Generated media fixtures

`stereo-44100-352.pcm` is 352 stereo signed 16-bit little-endian samples at 44.1 kHz. Left is a 440 Hz sine at amplitude 12000; right is a 660 Hz cosine at amplitude 9000. These are generated tones, not copyrighted music.

`stereo-44100-352.alac` is the single ALAC packet produced from that PCM with the system FFmpeg 7.1.5 encoder:

```
ffmpeg -f s16le -ar 44100 -ac 2 -i input.pcm -map 0:a:0 \
  -c:a alac -sample_fmt s16p -frames:a 1 -f data output.alac
```

The short final packet explicitly carries 352 samples. Regression tests require byte-for-byte lossless PCM equality, both directly and after RTP/AES-CBC transport decryption. The fixture is separate from the pinned decoder build.
